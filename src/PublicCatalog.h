#pragma once
#include "PublicCatalogCache.h"
#include "UntrustedInput.h"
#include "ArenaNetTransport.h"
#include "BackgroundWorkers.h"
#include "AutoRefreshPolicy.h"
#include "GW2API.h"
#include <memory>
#include <atomic>

namespace AlterEgo::PublicCatalog {
inline std::mutex stateMutex;
inline std::shared_ptr<const Snapshot> state=std::make_shared<const Snapshot>();
inline std::atomic<bool> vaultBusy{false};
inline std::string vaultStatus;
inline std::map<std::string,std::string> status;
inline std::set<std::string> busy;
inline std::shared_ptr<const Snapshot> Get() {
    std::lock_guard<std::mutex> lock(stateMutex);return state;
}
inline void Publish(Database::Connection& db) {
    auto next=std::make_shared<const Snapshot>(Read(db));
    std::lock_guard<std::mutex> lock(stateMutex);state=std::move(next);
}
template<class F> inline void WithDb(const std::string& root,F action) {
    std::lock_guard<std::mutex> lock(Cache::Mutex());
    auto path=Storage::Paths(root).Cache();std::filesystem::create_directories(path.parent_path());
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);Cache::Initialize(db);
    action(db);
}
inline void Load(const std::string& root) {
    {std::lock_guard<std::mutex> lock(stateMutex);state=std::make_shared<const Snapshot>();vaultStatus.clear();status.clear();busy.clear();}
    try{WithDb(root,[](auto& db){Publish(db);});}catch(...){}
}
// Busy ownership already suppresses duplicate work while a request runs.
// Persist the original attempt time only after a non-cancelled outcome.
class DatasetAttempt {
    std::string root_,dataset_;int64_t started_;bool armed_;
public:
    DatasetAttempt(const std::string& root,const std::string& dataset,bool enabled=true)
        :root_(root),dataset_(dataset+"_attempt"),started_(Now()),armed_(enabled){}
    DatasetAttempt(const DatasetAttempt&)=delete;
    DatasetAttempt& operator=(const DatasetAttempt&)=delete;
    ~DatasetAttempt(){Finish();}
    void Finish() noexcept {
        if(!armed_)return;armed_=false;
        try{
            if(Background::Workers().Stopping())return;
            WithDb(root_,[&](auto& db){
                if(Background::Workers().Stopping())return;
                MarkAttempt(db,dataset_,started_);Publish(db);
            });
        }catch(...){}
    }
};
inline Json Fetch(const std::string& path) {
    Background::Checkpoint();
    auto r=ArenaNet::Get(path);
    Background::Checkpoint();
    if(r.status_code!=200)throw std::runtime_error("Public API HTTP "+std::to_string(r.status_code));
    return Untrusted::Parse(r.body,Untrusted::PublicApi);
}
inline void CommitUpgrades(const std::string& root,const std::set<uint32_t>& ids,const Json& rows,const Cache::ItemRequestStamps& stamps) {
    std::lock_guard<std::mutex> publication(Cache::ItemPublicationMutex());
    Background::Checkpoint();auto accepted=Json::array();
    WithDb(root,[&](auto& db){Database::Transaction tx(db);StoreUpgrades(db,ids,rows,Now(),&stamps,&accepted);tx.Commit();Publish(db);});
    GW2API::AdoptPublicItems(accepted);
}
inline void CommitIncrementalItems(const std::string& root,const std::set<uint32_t>& index,
                                   const Json& rows,bool discover,bool refresh,const Cache::ItemRequestStamps& stamps) {
    std::lock_guard<std::mutex> publication(Cache::ItemPublicationMutex());
    Background::Checkpoint();auto accepted=Json::array();
    WithDb(root,[&](auto& db){Database::Transaction tx(db);StoreIncrementalItems(db,index,rows,discover,refresh,Now(),&stamps,&accepted);tx.Commit();Publish(db);});
    GW2API::AdoptPublicItems(accepted);
}
inline void CompleteCheck(const std::string& root,const std::string& dataset) {
    WithDb(root,[&](auto& db){Mark(db,dataset,Now());Publish(db);});
}
inline void RefreshVault(const std::string& root,bool manual) {
    if(!AutoRefreshPolicy::AllowRequest(manual,AutoRefreshPolicy::Dataset::PublicCatalog))return;
    auto old=Get();if(!Due(old->vault,old->vaultAttempt,Now(),manual)||vaultBusy.exchange(true))return;
    {std::lock_guard<std::mutex> lock(stateMutex);vaultStatus="Refreshing...";}
    Background::Workers().Launch([root,manual]{
        ApiActivity::ReasonScope reason(manual?ApiActivity::Reason::Manual:ApiActivity::Reason::Background);
        DatasetAttempt attempt(root,"public_vault");
        auto rows=ParseObjectives(Fetch("/v2/wizardsvault/objectives?ids=all"));
        Background::Checkpoint();
        WithDb(root,[&](auto& db){Background::Checkpoint();Database::Transaction tx(db);StoreObjectives(db,rows,Now());tx.Commit();Publish(db);});
        attempt.Finish();
        {std::lock_guard<std::mutex> lock(stateMutex);vaultStatus="Up to date";}
        vaultBusy=false;
    },[]{std::lock_guard<std::mutex> lock(stateMutex);vaultStatus=Background::Workers().Stopping()?"Refresh cancelled":"Refresh failed; cached data retained";vaultBusy=false;});
}
inline bool BeginDataset(const std::string& name,bool manual,bool initialRequired=false) {
    std::lock_guard<std::mutex> lock(stateMutex);
    if(!manual&&!initialRequired&&!AutoRefreshPolicy::Enabled(AutoRefreshPolicy::Dataset::PublicCatalog))return false;
    if(!DatasetDue(*state,name,Now(),manual)||!busy.insert(name).second)return false;
    status[name]="Refreshing...";
    return true;
}
inline void EndDataset(const std::string& name,bool ok) {
    std::lock_guard<std::mutex> lock(stateMutex);busy.erase(name);
    status[name]=ok?"Up to date":Background::Workers().Stopping()?"Refresh cancelled":"Refresh failed; cached data retained";
}
inline std::string DatasetStatus(const std::string& name) {
    std::lock_guard<std::mutex> lock(stateMutex);
    auto it=status.find(name);return it==status.end()?std::string{}:it->second;
}
inline void RefreshPets(const std::string& root,bool manual=false,bool initialRequired=false) {
    if(!BeginDataset("public_pets",manual,initialRequired))return;
    Background::Workers().Launch([root,manual]{
        ApiActivity::ReasonScope reason(manual?ApiActivity::Reason::Manual:ApiActivity::Reason::Background);
        DatasetAttempt attempt(root,"public_pets");
        auto rows=ParsePets(Fetch("/v2/pets?ids=all"));
        Background::Checkpoint();
        WithDb(root,[&](auto& db){Background::Checkpoint();Database::Transaction tx(db);StorePets(db,rows,Now());tx.Commit();Publish(db);});
        attempt.Finish();
        EndDataset("public_pets",true);
    },[]{EndDataset("public_pets",false);});
}
inline void RefreshLegends(const std::string& root,bool manual=false,bool initialRequired=false) {
    if(!BeginDataset("public_legends",manual,initialRequired))return;
    Background::Workers().Launch([root,manual]{
        ApiActivity::ReasonScope reason(manual?ApiActivity::Reason::Manual:ApiActivity::Reason::Background);
        DatasetAttempt attempt(root,"public_legends");
        auto raw=Fetch("/v2/legends?ids=all&v=2019-12-19T00:00:00.000Z");
        if(!raw.is_array()||raw.empty())throw std::runtime_error("Empty legend API");
        std::set<uint32_t> swaps;
        for(const auto& row:raw)swaps.insert(Id(row.at("swap")));
        const std::vector<uint32_t> swapIds(swaps.begin(),swaps.end());
        auto skills=GW2API::AcquireLegendSwapSkills(swapIds,manual);
        if(skills.size()!=swapIds.size())throw std::runtime_error("Incomplete verified legend swap skills");
        auto rows=ParseLegends(raw,skills);
        Background::Checkpoint();
        WithDb(root,[&](auto& db){Background::Checkpoint();Database::Transaction tx(db);StoreLegends(db,rows,Now());tx.Commit();Publish(db);});
        // Names/icons come from the committed canonical skill projection.
        // Template identity remains the schema-pinned legend code.
        attempt.Finish();
        EndDataset("public_legends",true);
    },[]{EndDataset("public_legends",false);});
}
inline std::string VaultStatus(){std::lock_guard<std::mutex> lock(stateMutex);return vaultStatus;}
} // namespace AlterEgo::PublicCatalog
