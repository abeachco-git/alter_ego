#include "AccountIdentity.h"
#include "CallbackLifetime.h"
#include "BackgroundWorkers.h"
#include "OwnedSkins.h"
#include "SkinDataCache.h"
#include "SkinCache.h"
#include "HoardAndSeekAPI.h"

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
    std::atomic<bool> OwnedSkins::s_hs_available{false};
    std::atomic<bool> OwnedSkins::s_querying{false};
    std::atomic<bool> OwnedSkins::s_data_updated{false};
    std::string OwnedSkins::s_status_message = "Waiting for Hoard & Seek...";
    std::time_t OwnedSkins::s_last_success_time = 0;
    std::atomic<uint64_t> OwnedSkins::s_generation{0};
    std::unordered_set<uint32_t> OwnedSkins::s_locally_marked;
    std::vector<uint32_t> OwnedSkins::s_pending_ids;
    size_t OwnedSkins::s_batch_index = 0;
    std::atomic<bool> OwnedSkins::s_batch_pending{false};
    std::string OwnedSkins::s_account_name;

    #define SKINVENTORY_REQUESTER "Skinventory"
    #define EV_SKINVENTORY_SKINS_RESPONSE "EV_SKINVENTORY_SKINS_RESPONSE"
    #define EV_SKINVENTORY_API_RESPONSE "EV_SKINVENTORY_API_RESPONSE"

    // --- Single-shot backoff retry when H&S returns HOARD_STATUS_BUSY ---
    // (proxy queue full / per-addon rate limit). We schedule ONE retry at a
    // future time rather than spinning every frame. Driven from Tick().
    namespace {
        // All request state below is protected by OwnedSkins::s_mutex.
        enum class RequestKind { None, Api, Batch };
        RequestKind g_requestKind=RequestKind::None;
        uint64_t g_accountEpoch=0;
        bool g_batchAwaiting=false;
        std::unordered_set<uint32_t> g_batchOwned, g_batchQueried;
        std::atomic<int>       g_retryKind{0};   // 0 = none, 1 = API query, 2 = batch query
        std::atomic<long long> g_retryAtMs{0};   // steady_clock ms at which to retry

        long long NowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        void ScheduleRetry(int kind, uint32_t retry_after_ms) {
            g_retryKind = kind;
            g_retryAtMs = NowMs() + (retry_after_ms ? (long long)retry_after_ms : 1000LL);
        }
    }

    void OwnedSkins::Tick() {
        // Fire a scheduled BUSY backoff retry once its delay has elapsed.
        int kind = g_retryKind.load();
        if (kind != 0 && g_retryAtMs.load() != 0 && NowMs() >= g_retryAtMs.load()) {
            g_retryKind = 0;
            g_retryAtMs = 0;
            if (kind == 1) {
                RequestOwnedSkinsViaApi();
            } else if (kind == 2) {
                SendNextBatch();           // resend current batch (s_batch_index unchanged)
            }
        }
        if (s_batch_pending.exchange(false)) {
            SendNextBatch();
        }
    }

    void OwnedSkins::SetDataDirectory(const std::string& dir) {
        s_data_dir = dir;
    }

    void OwnedSkins::Initialize(AddonAPI_t* api) {
        s_API = api;
        if (!s_API) return;

        // The selected account is set by the main render loop; do not load
        // legacy global ownership into an unknown account at startup.

        s_API->Events_Subscribe(EV_HOARD_PONG, OnPong);
        s_API->Events_Subscribe(EV_HOARD_DATA_UPDATED, OnDataUpdated);
        s_API->Events_Subscribe(EV_SKINVENTORY_SKINS_RESPONSE, OnSkinsResponse);
        s_API->Events_Subscribe(EV_SKINVENTORY_API_RESPONSE, OnApiResponse);
    }

    void OwnedSkins::Shutdown() {
        if (!s_API) return;

        s_API->Events_Unsubscribe(EV_HOARD_PONG, OnPong);
        s_API->Events_Unsubscribe(EV_HOARD_DATA_UPDATED, OnDataUpdated);
        s_API->Events_Unsubscribe(EV_SKINVENTORY_SKINS_RESPONSE, OnSkinsResponse);
        s_API->Events_Unsubscribe(EV_SKINVENTORY_API_RESPONSE, OnApiResponse);
        s_API = nullptr;
    }

    void OwnedSkins::PingHoardAndSeek() {
        if (!s_API) return;
        s_API->Events_Raise(EV_HOARD_PING, nullptr);
    }

    void OwnedSkins::SetAccountName(const std::string& name) {
        // Old installs used a global file. Retry its one-time migration after
        // H&S confirms the account count, even if the selected name did not
        // change between frames.
        const bool singleKnown = AlterEgo::GW2API::HasAccountsData() &&
                                 AlterEgo::GW2API::GetAccountCount() == 1;
        static std::string legacyAttemptAccount;
        bool load = false;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_account_name != name) {
                load = !name.empty();
                s_account_name = name;
                ++g_accountEpoch;
                g_requestKind=RequestKind::None;
                g_batchAwaiting=false;
                g_batchOwned.clear();g_batchQueried.clear();
                g_retryKind=0;g_retryAtMs=0;
                s_owned_skins.clear();
                s_queried_skins.clear();
                s_locally_marked.clear();
                s_pending_ids.clear();
                s_batch_index = 0;
                s_batch_pending = false;
                s_querying = false;
                s_has_data = false;
                s_last_success_time = 0;
                s_generation++;
                s_status_message = "No ownership data - click Refresh";
            } else if (singleKnown && !name.empty() && !s_has_data &&
                       legacyAttemptAccount != name) {
                legacyAttemptAccount = name;
                load = true;
            }
        }
        if (load && LoadFromCache()) {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_account_name == name) {
                s_has_data = true;
                s_status_message = "Owned: " + std::to_string(s_owned_skins.size()) + " (cached)";
            }
        }
    }

    void OwnedSkins::OnPong(void* eventArgs) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
        if (!eventArgs) return;
        auto* pong = (HoardPongPayload*)eventArgs;
        if (pong->api_version < 2) return;

        s_hs_available = true;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (pong->has_data) {
                s_status_message = "Hoard & Seek connected";
            } else {
                s_status_message = "Hoard & Seek has no data - refresh in H&S first";
            }
        }
    }

    void OwnedSkins::OnDataUpdated(void* eventArgs) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
        // H&S finished a data refresh — signal that we should re-query skins
        s_hs_available = true;
        s_data_updated = true;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_status_message = "Hoard & Seek data updated";
        }
    }

    bool OwnedSkins::ConsumeDataUpdatedFlag() {
        return s_data_updated.exchange(false);
    }

    void OwnedSkins::RequestOwnedSkinsViaApi() {
        if (!s_API || !s_hs_available) return;
        HoardQueryApiRequest req{};
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(s_querying || s_account_name.empty())return;
            s_querying=true;
            g_requestKind=RequestKind::Api;
            g_retryKind=0;g_retryAtMs=0;
            s_status_message="Querying /v2/account/skins...";
            req.api_version=HOARD_API_VERSION;
            strncpy(req.requester,SKINVENTORY_REQUESTER,sizeof(req.requester)-1);
            strncpy(req.endpoint,"/v2/account/skins",sizeof(req.endpoint)-1);
            strncpy(req.response_event,EV_SKINVENTORY_API_RESPONSE,sizeof(req.response_event)-1);
            strncpy(req.account_name,s_account_name.c_str(),sizeof(req.account_name)-1);
        }
        s_API->Events_Raise(EV_HOARD_QUERY_API,&req);
    }

    void OwnedSkins::OnApiResponse(void* eventArgs) {
        AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
        std::unique_ptr<HoardQueryApiResponse> guard(static_cast<HoardQueryApiResponse*>(eventArgs));
        if(!callbackLease || !guard || guard->api_version<2)return;
        const auto& resp=*guard;
        const bool single=AlterEgo::GW2API::HasAccountsData() && AlterEgo::GW2API::GetAccountCount()==1;
        const std::string echoed=resp.api_version>=3 ?
            std::string(resp.account_name,strnlen(resp.account_name,sizeof(resp.account_name))) : std::string{};
        if(std::string(resp.endpoint,strnlen(resp.endpoint,sizeof(resp.endpoint)))!="/v2/account/skins")return;
        uint64_t epoch;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(!s_querying || g_requestKind!=RequestKind::Api ||
               !AlterEgo::Identity::MatchesReply({s_account_name},{echoed},resp.api_version>=3,single))return;
            epoch=g_accountEpoch;
            if(resp.status!=HOARD_STATUS_OK) {
                s_querying=false;g_requestKind=RequestKind::None;
                if(resp.status==HOARD_STATUS_BUSY) {
                    ScheduleRetry(1,resp.api_version>=4?resp.retry_after_ms:0);
                    s_status_message="Hoard & Seek busy, retrying shortly...";
                } else s_status_message=resp.status==HOARD_STATUS_DENIED ?
                    "Permission denied by Hoard & Seek" : "Ownership request failed";
                return;
            }
        }
        if(resp.truncated || resp.json_length>=sizeof(resp.json)) {
            auto allIds=SkinCache::GetAllSkinIds();
            std::sort(allIds.begin(),allIds.end());
            allIds.erase(std::unique(allIds.begin(),allIds.end()),allIds.end());
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                if(epoch!=g_accountEpoch || g_requestKind!=RequestKind::Api || !s_querying)return;
                if(allIds.empty()) {
                    g_requestKind=RequestKind::None;s_querying=false;
                    s_status_message="Ownership response truncated; load skin catalog to retry";
                    return;
                }
                g_requestKind=RequestKind::Batch;
                g_batchAwaiting=false;g_batchOwned.clear();g_batchQueried.clear();
                s_pending_ids=std::move(allIds);s_batch_index=0;
                s_status_message="API response truncated, querying skins in batches...";
                // Keep dispatch on the existing Tick path, outside the mutex.
                s_batch_pending=true;
            }
            return;
        }
        std::unordered_set<uint32_t> parsed;
        bool valid=false;
        try {
            const auto len=strnlen(resp.json,sizeof(resp.json));
            if(len>=sizeof(resp.json) || (resp.json_length!=len && resp.json_length!=len+1))
                throw std::runtime_error("Invalid ownership response length");
            auto j=json::parse(resp.json,resp.json+len);
            if(!j.is_array())throw std::runtime_error("Expected skin array");
            for(const auto& value:j) {
                if(!value.is_number_integer())throw std::runtime_error("Invalid skin ID");
                const auto id=value.get<int64_t>();
                if(id<=0 || id>UINT32_MAX)throw std::runtime_error("Invalid skin ID");
                parsed.insert(static_cast<uint32_t>(id));
            }
            valid=true;
        } catch(...) {}
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(epoch!=g_accountEpoch || g_requestKind!=RequestKind::Api || !s_querying)return;
            s_querying=false;g_requestKind=RequestKind::None;
            if(!valid) {s_status_message="Failed to parse /v2/account/skins response";return;}
            parsed.insert(s_locally_marked.begin(),s_locally_marked.end());
            s_owned_skins=std::move(parsed);
            s_has_data=true;s_last_success_time=std::time(nullptr);++s_generation;
            s_status_message="Owned: "+std::to_string(s_owned_skins.size());
        }
        SaveToCache();
    }

    void OwnedSkins::RequestOwnedSkins(const std::vector<uint32_t>& skin_ids) {
        if(!s_API || !s_hs_available || skin_ids.empty())return;
        auto ids=skin_ids;
        std::sort(ids.begin(),ids.end());
        ids.erase(std::remove(ids.begin(),ids.end(),0),ids.end());
        ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        if(ids.empty())return;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(s_querying || s_account_name.empty())return;
            g_requestKind=RequestKind::Batch;g_batchAwaiting=false;
            g_batchOwned.clear();g_batchQueried.clear();
            g_retryKind=0;g_retryAtMs=0;
            s_pending_ids=std::move(ids);s_batch_index=0;s_querying=true;
            s_status_message="Querying owned skins (batch)...";
        }
        SendNextBatch();
    }

    void OwnedSkins::SendNextBatch() {
        if(!s_API)return;
        HoardQuerySkinsRequest req{};
        bool allDone=false;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(!s_querying || g_requestKind!=RequestKind::Batch || g_batchAwaiting || s_account_name.empty())return;
            if(s_batch_index>=s_pending_ids.size()) {
                // Only a complete verified batch sequence updates ownership.
                // Preserve previously known skins outside this query's scope.
                for(auto id:g_batchQueried)s_owned_skins.erase(id);
                s_owned_skins.insert(g_batchOwned.begin(),g_batchOwned.end());
                s_owned_skins.insert(s_locally_marked.begin(),s_locally_marked.end());
                s_queried_skins.insert(g_batchQueried.begin(),g_batchQueried.end());
                g_batchOwned.clear();g_batchQueried.clear();
                s_querying=false;g_requestKind=RequestKind::None;s_has_data=true;
                s_last_success_time=std::time(nullptr);++s_generation;
                s_status_message="Owned: "+std::to_string(s_owned_skins.size())+" / "+std::to_string(s_queried_skins.size());
                allDone=true;
            } else {
                size_t end=std::min(s_batch_index+200,s_pending_ids.size());
                req.api_version=HOARD_API_VERSION;
                strncpy(req.requester,SKINVENTORY_REQUESTER,sizeof(req.requester)-1);
                strncpy(req.response_event,EV_SKINVENTORY_SKINS_RESPONSE,sizeof(req.response_event)-1);
                strncpy(req.account_name,s_account_name.c_str(),sizeof(req.account_name)-1);
                for(size_t i=s_batch_index;i<end;++i)req.ids[req.id_count++]=s_pending_ids[i];
                g_batchAwaiting=true;
                s_status_message="Querying skins "+std::to_string(s_batch_index)+"-"+std::to_string(end)+"/"+std::to_string(s_pending_ids.size());
            }
        }
        if(allDone){SaveToCache();return;}
        s_API->Events_Raise(EV_HOARD_QUERY_SKINS,&req);
    }

    void OwnedSkins::OnSkinsResponse(void* eventArgs) {
        AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
        std::unique_ptr<HoardQuerySkinsResponse> guard(static_cast<HoardQuerySkinsResponse*>(eventArgs));
        if(!callbackLease || !guard || guard->api_version<2)return;
        const auto& resp=*guard;
        const bool single=AlterEgo::GW2API::HasAccountsData() && AlterEgo::GW2API::GetAccountCount()==1;
        const std::string echoed=resp.api_version>=3 ?
            std::string(resp.account_name,strnlen(resp.account_name,sizeof(resp.account_name))) : std::string{};
        std::lock_guard<std::mutex> lock(s_mutex);
        if(!s_querying || g_requestKind!=RequestKind::Batch || !g_batchAwaiting ||
           !AlterEgo::Identity::MatchesReply({s_account_name},{echoed},resp.api_version>=3,single))return;
        if(resp.status==HOARD_STATUS_BUSY) {
            g_batchAwaiting=false;ScheduleRetry(2,resp.api_version>=4?resp.retry_after_ms:0);
            s_status_message="Hoard & Seek busy, retrying shortly...";return;
        }
        if(resp.status!=HOARD_STATUS_OK) {
            g_batchAwaiting=false;g_requestKind=RequestKind::None;s_querying=false;
            s_status_message=resp.status==HOARD_STATUS_DENIED ? "Permission denied by Hoard & Seek" : "Ownership batch failed";
            return;
        }
        const size_t end=std::min(s_batch_index+200,s_pending_ids.size());
        if(resp.entry_count>200)return;
        std::vector<uint32_t> expected(s_pending_ids.begin()+s_batch_index,s_pending_ids.begin()+end), returned;
        for(uint32_t i=0;i<resp.entry_count;++i)returned.push_back(resp.entries[i].id);
        if(!AlterEgo::Identity::ExactIds(expected,returned)) {
            // A late response for a different batch must not advance this
            // request. If all IDs are a subset, fail the incomplete response
            // rather than leaving the UI permanently busy.
            const bool subset=std::all_of(returned.begin(),returned.end(),[&](uint32_t id) {
                return std::find(expected.begin(),expected.end(),id)!=expected.end();
            });
            if(subset) {
                g_batchAwaiting=false;g_requestKind=RequestKind::None;s_querying=false;
                s_status_message="Incomplete ownership batch; refresh to retry";
            }
            return;
        }
        for(uint32_t i=0;i<resp.entry_count;++i) {
            auto id=resp.entries[i].id;
            g_batchQueried.insert(id);
            if(resp.entries[i].unlocked)g_batchOwned.insert(id);
        }
        s_batch_index=end;g_batchAwaiting=false;s_batch_pending=true;
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

    bool OwnedSkins::IsHoardAndSeekAvailable() {
        return s_hs_available;
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

    static std::string AccountCachePath(const std::string& dir, const std::string& account) {
        std::string slug;
        slug.reserve(account.size());
        for (unsigned char c : account)
            slug.push_back(std::isalnum(c) || c == '-' || c == '_' ? (char)c : '_');
        return dir + "/owned_skins_" + slug + ".json";
    }

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

    bool OwnedSkins::LoadFromCache() {
        std::string account;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            account=s_account_name;
        }
        if(s_data_dir.empty()||account.empty())return false;
        const auto root=AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir);
        try {
            auto snapshot=AlterEgo::Cache::ReadOwnership(root,account);
            if(!snapshot) {
                auto path=AccountCachePath(s_data_dir,account);
                std::ifstream file(path);
                bool unscoped=false;
                if(!file.is_open()&&AlterEgo::GW2API::HasAccountsData()&&
                    AlterEgo::GW2API::GetAccountCount()==1) {
                    path=s_data_dir+"/owned_skins.json";file.clear();file.open(path);
                    unscoped=file.is_open();
                }
                if(!file)return false;
                auto j=json::parse(file);file.close();
                const json* ids=&j;
                AlterEgo::Cache::OwnershipSnapshot legacy;
                if(j.is_object()) {
                    if(!j.contains("skins")||!j["skins"].is_array())return false;
                    if(j.contains("account")) {
                        if(j["account"].get<std::string>()!=account)return false;
                    } else if(!unscoped)return false;
                    ids=&j["skins"];legacy.fetchedAt=j.value("last_success",int64_t(0));
                } else if(!unscoped)return false;
                if(!ids->is_array())return false;
                for(const auto& id:*ids)legacy.ids.insert(id.get<uint32_t>());
                AlterEgo::Cache::WriteOwnership(root,account,legacy);
                snapshot=AlterEgo::Cache::ReadOwnership(root,account);
                if(snapshot) {
                    std::error_code ec;std::filesystem::remove(std::filesystem::u8path(path),ec);
                }
            }
            if(!snapshot)return false;
            std::unordered_set<uint32_t> restored(snapshot->ids.begin(),snapshot->ids.end());
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                if(s_account_name!=account)return false;
                s_owned_skins=std::move(restored);
                s_last_success_time=static_cast<std::time_t>(snapshot->fetchedAt);
                s_generation++;
                s_has_data=true;
            }
            return true;
        } catch(...) { return false; }
    }

}
