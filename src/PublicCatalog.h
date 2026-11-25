#pragma once
#include "PublicCatalogCache.h"
#include "ArenaNetTransport.h"
#include "BackgroundWorkers.h"
#include "AutoRefreshPolicy.h"
#include <memory>
#include <atomic>

namespace AlterEgo::PublicCatalog {
inline std::mutex stateMutex;
inline std::shared_ptr<const Snapshot> state=std::make_shared<const Snapshot>();
inline std::atomic<bool> vaultBusy{false};
inline std::string vaultStatus;
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
    {std::lock_guard<std::mutex> lock(stateMutex);state=std::make_shared<const Snapshot>();vaultStatus.clear();}
    try{WithDb(root,[](auto& db){Publish(db);});}catch(...){}
}
inline void Attempt(const std::string& root,const std::string& dataset) {
    WithDb(root,[&](auto& db){Mark(db,dataset+"_attempt",Now());Publish(db);});
}
inline Json Fetch(const std::string& path) {
    Background::Checkpoint();
    auto r=ArenaNet::Get(path);
    if(r.status_code!=200)throw std::runtime_error("Public API HTTP "+std::to_string(r.status_code));
    return Json::parse(r.body);
}
inline void CommitUpgrades(const std::string& root,const std::set<uint32_t>& ids,const Json& rows) {
    WithDb(root,[&](auto& db){Database::Transaction tx(db);StoreUpgrades(db,ids,rows,Now());tx.Commit();Publish(db);});
}
inline void RefreshUpgrades(const std::string& root) {
    const auto old=Get();
    Attempt(root,"public_upgrades");
    const auto index=Index(Fetch("/v2/items"));
    std::vector<uint32_t> todo;
    for(auto id:index)if(!old->index.count(id)||old->upgradeIds.count(id))todo.push_back(id);
    Json rows=Json::array();
    for(size_t start=0;start<todo.size();start+=200) {
        std::set<uint32_t> expected;std::string query;
        for(size_t i=start;i<std::min(todo.size(),start+200);++i){
            if(!query.empty())query+=',';
            query+=std::to_string(todo[i]);expected.insert(todo[i]);
        }
        auto batch=Fetch("/v2/items?ids="+query);ValidateBatch(batch,expected);
        for(const auto& item:batch)if(!Kind(item).empty())rows.push_back(item);
    }
    Background::Checkpoint();CommitUpgrades(root,index,rows);
}
inline void RefreshVault(const std::string& root,bool manual) {
    if(!AutoRefreshPolicy::AllowRequest(manual,AutoRefreshPolicy::Dataset::PublicCatalog))return;
    auto old=Get();if(!Due(old->vault,old->vaultAttempt,Now(),manual)||vaultBusy.exchange(true))return;
    {std::lock_guard<std::mutex> lock(stateMutex);vaultStatus="Refreshing...";}
    Background::Workers().Launch([root]{
        Attempt(root,"public_vault");
        auto rows=ParseObjectives(Fetch("/v2/wizardsvault/objectives?ids=all"));
        Background::Checkpoint();
        WithDb(root,[&](auto& db){Database::Transaction tx(db);StoreObjectives(db,rows,Now());tx.Commit();Publish(db);});
        {std::lock_guard<std::mutex> lock(stateMutex);vaultStatus="Up to date";}
        vaultBusy=false;
    },[]{std::lock_guard<std::mutex> lock(stateMutex);vaultStatus="Refresh failed; cached data retained";vaultBusy=false;});
}
inline std::string VaultStatus(){std::lock_guard<std::mutex> lock(stateMutex);return vaultStatus;}
} // namespace AlterEgo::PublicCatalog
