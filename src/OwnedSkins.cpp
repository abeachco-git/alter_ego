#include "AccountIdentity.h"
#include "CallbackLifetime.h"
#include "BackgroundWorkers.h"
#include "OwnedSkins.h"
#include "SkinDataCache.h"
#include "SkinCache.h"
#include "NativeAccounts.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <memory>
#include <chrono>
#include <ctime>
#include <windows.h>
#include "GW2API.h"

using json = nlohmann::json;

namespace Skinventory {

    AddonAPI_t* OwnedSkins::s_API = nullptr;
    std::string OwnedSkins::s_data_dir;
    std::unordered_set<uint32_t> OwnedSkins::s_owned_skins;
    std::unordered_set<uint32_t> OwnedSkins::s_queried_skins;
    std::mutex OwnedSkins::s_mutex;
    std::atomic<bool> OwnedSkins::s_has_data{false};
    std::atomic<bool> OwnedSkins::s_querying{false};
    std::atomic<bool> OwnedSkins::s_data_updated{false};
    std::string OwnedSkins::s_status_message = "Link an account in Nexus options";
    std::time_t OwnedSkins::s_last_success_time = 0;
    std::atomic<uint64_t> OwnedSkins::s_generation{0};
    std::unordered_set<uint32_t> OwnedSkins::s_locally_marked;
    std::string OwnedSkins::s_account_name;

    namespace {uint64_t accountEpoch=0;}
    void OwnedSkins::Tick(){}
    void OwnedSkins::SetDataDirectory(const std::string& dir){s_data_dir=dir;}
    void OwnedSkins::Initialize(AddonAPI_t* api){s_API=api;}
    void OwnedSkins::Shutdown(){s_API=nullptr;}
    void OwnedSkins::SetAccountName(const std::string& name){
        uint64_t epoch=0;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(s_account_name==name)return;
            s_account_name=name;epoch=++accountEpoch;s_querying=false;s_has_data=false;
            s_owned_skins.clear();s_queried_skins.clear();s_locally_marked.clear();
            s_last_success_time=0;++s_generation;
            s_status_message="No ownership data - click Refresh";
        }
        // Native GUID snapshots only; never adopt an unscoped H&S cache.
        if(name.empty())return;
        const auto dir=s_data_dir;
        AlterEgo::Background::Workers().Launch([name,epoch,dir]{
            try{
                auto snapshot=AlterEgo::Cache::ReadOwnership(AlterEgo::Cache::RootFromLegacySkinDirectory(dir),name);
                if(!snapshot)return;
                std::lock_guard<std::mutex> lock(s_mutex);
                if(epoch!=accountEpoch||s_account_name!=name||s_has_data||AlterEgo::Background::Workers().Stopping())return;
                s_owned_skins=std::unordered_set<uint32_t>(snapshot->ids.begin(),snapshot->ids.end());
                s_owned_skins.insert(s_locally_marked.begin(),s_locally_marked.end());
                s_last_success_time=static_cast<std::time_t>(snapshot->fetchedAt);s_has_data=true;++s_generation;
                s_status_message="Owned: "+std::to_string(s_owned_skins.size())+" (cached)";
            }catch(...){}
        });
    }
    bool OwnedSkins::ConsumeDataUpdatedFlag(){return s_data_updated.exchange(false);}
    void OwnedSkins::RequestOwnedSkinsViaApi(){
        std::string account;uint64_t epoch;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(s_querying||s_account_name.empty())return;
            account=s_account_name;epoch=++accountEpoch;s_querying=true;s_status_message="Retrieving skin ownership...";
        }
        AlterEgo::Background::Workers().Launch([account,epoch]{
            auto result=AlterEgo::NativeAccounts::Request(account,"/v2/account/skins",{"unlocks"});
            AlterEgo::Background::Checkpoint();
            std::unordered_set<uint32_t> parsed;bool valid=false;
            try{
                if(result&&result.data.is_array()){
                    for(const auto& value:result.data){
                        if(!value.is_number_integer())throw std::runtime_error("Invalid skin ID");
                        const auto id=value.get<int64_t>();if(id<=0||id>UINT32_MAX)throw std::runtime_error("Invalid skin ID");parsed.insert(static_cast<uint32_t>(id));
                    }valid=true;
                }
            }catch(...){}
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                if(accountEpoch!=epoch||s_account_name!=account)return;
                s_querying=false;
                if(!valid){s_status_message=result.error.empty()?"Invalid skin ownership response":result.error;return;}
                parsed.insert(s_locally_marked.begin(),s_locally_marked.end());s_owned_skins=std::move(parsed);
                s_has_data=true;s_last_success_time=std::time(nullptr);++s_generation;
                s_status_message="Owned: "+std::to_string(s_owned_skins.size());
            }
            SaveToCache();
        },[epoch]{std::lock_guard<std::mutex> lock(s_mutex);if(accountEpoch==epoch){s_querying=false;s_status_message="Ownership refresh cancelled";}});
    }
    void OwnedSkins::RequestOwnedSkins(const std::vector<uint32_t>&){RequestOwnedSkinsViaApi();}
    bool OwnedSkins::HasDataForAccount(const std::string& account){
        std::lock_guard<std::mutex> lock(s_mutex);return !account.empty()&&account==s_account_name&&s_has_data;
    }
    bool OwnedSkins::IsOwnedForAccount(const std::string& account,uint32_t id){
        std::lock_guard<std::mutex> lock(s_mutex);return !account.empty()&&account==s_account_name&&s_owned_skins.count(id)>0;
    }

    bool OwnedSkins::IsOwned(uint32_t skin_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_owned_skins.count(skin_id) > 0;
    }

    void OwnedSkins::MarkOwned(uint32_t skin_id) {
        // Alerts are from the logged-in character, which may be on a different
        // account than the one the user has selected in Skinventory.
        const std::string loggedIn = AlterEgo::GW2API::GetCurrentAccountName();
        bool persist = false;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_account_name.empty() || loggedIn != s_account_name) return;
            s_owned_skins.insert(skin_id);
            s_locally_marked.insert(skin_id);
            persist = s_has_data.load();
        }
        s_generation++;
        // Do not replace a complete ownership snapshot with a partial
        // one-item alert if no full ownership data has been loaded.
        if (persist) SaveToCache();
    }

    bool OwnedSkins::HasData() {
        return s_has_data;
    }

    bool OwnedSkins::IsAccountAvailable() {
        return AlterEgo::NativeAccounts::HasLinkedAccounts();
    }

    bool OwnedSkins::IsQuerying() {
        return s_querying;
    }

    std::string OwnedSkins::GetStatusMessage() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_status_message;
    }

    std::time_t OwnedSkins::GetLastSuccessTime() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_last_success_time;
    }

    size_t OwnedSkins::GetOwnedCount() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_owned_skins.size();
    }

    size_t OwnedSkins::GetQueriedCount() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_queried_skins.size();
    }

    std::unordered_set<uint32_t> OwnedSkins::GetOwnedSnapshot() {
        std::lock_guard<std::mutex> lock(s_mutex);return s_owned_skins;
    }
    uint64_t OwnedSkins::GetGeneration() {
        return s_generation;
    }

    // --- Disk Cache ---

    bool OwnedSkins::SaveToCache() {
        static std::mutex saveMutex;
        std::lock_guard<std::mutex> saving(saveMutex);
        std::string account;
        AlterEgo::Cache::OwnershipSnapshot snapshot;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            account=s_account_name;
            if(!s_has_data.load())return false;
            snapshot.fetchedAt=static_cast<int64_t>(s_last_success_time);
            snapshot.ids.insert(s_owned_skins.begin(),s_owned_skins.end());
        }
        if(s_data_dir.empty()||account.empty())return false;
        try {
            return AlterEgo::Cache::WriteOwnership(
                AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir),account,snapshot);
        } catch(...) { return false; }
    }


}
