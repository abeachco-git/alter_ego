#pragma once
#include "CharacterIdentityRuntime.h"
#include "CacheDatabase.h"
namespace AlterEgo::CharacterIdentity {
// Exact-account verified core data is historical evidence, not a fresh
// roster observation. Preserve it before the existing cache can be pruned.
struct HistoricalCore {
    std::set<std::string> names;
    std::vector<Observation> observations;
};
inline HistoricalCore ReadHistoricalCore(Database::Connection& db,const std::string& account){
    Database::Transaction tx(db);
    HistoricalCore result;
    auto roster=db.Prepare("SELECT character FROM native_rosters WHERE account=?");
    roster.Bind(1,account);
    while(roster.Step()){
        if(!Text(roster.Text(0)))throw std::runtime_error("Invalid historical roster");
        result.names.insert(roster.Text(0));
    }
    auto cores=db.Prepare("SELECT name,created,profession,race,core_verified FROM character_rows WHERE account_name=?");
    cores.Bind(1,account);
    while(cores.Step()){
        const auto name=cores.Text(0);
        if(!Text(name))throw std::runtime_error("Invalid historical character name");
        result.names.insert(name);
        if(cores.Integer(4)>0){
            const auto evidence=Normalize({cores.Text(1),cores.Text(2),cores.Text(3)});
            if(evidence)result.observations.push_back({name,*evidence});
        }
    }
    if(result.names.size()>MaxRoster)throw std::runtime_error("Historical character roster exceeds bounds");
    tx.Commit();return result;
}
inline HistoricalCore ReadHistoricalCore(const std::string& root,const std::string& account){
    if(root.empty())throw std::runtime_error("Missing cache root");
    std::lock_guard<std::mutex> lock(Cache::Mutex());
    const auto path=Storage::Paths(root).Cache();
    if(!std::filesystem::exists(path))return {};
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
    Cache::Initialize(db);return ReadHistoricalCore(db,account);
}
// Rebuildable cache adoption is an independent transaction. The durable
// identity registry is committed first; the same state may then be replayed
// after interruption. The caller MUST gate readers and publications until
// adoption succeeds and discard the affected in-memory projections before
// releasing the gate. No transport or UI calls here.
inline std::optional<std::set<std::string>> AdoptCache(Database::Connection& db,const State& state){
    if(!state.initialized||!Valid(state))return std::nullopt;
    Database::Transaction tx(db);
    auto revision=db.Prepare("SELECT revision FROM cache_character_identity_state WHERE account=?");
    revision.Bind(1,state.account);
    const bool known=revision.Step();
    const int64_t oldRevision=known?revision.Integer(0):-1;
    if(oldRevision>state.revision)return std::nullopt; // registry rollback, require explicit recovery
    Bindings prior;
    auto rows=db.Prepare("SELECT name,local_id,created,profession,race,binding_revision FROM cache_character_bindings WHERE account=?");
    rows.Bind(1,state.account);
    while(rows.Step()){
        const auto name=rows.Text(0);Binding binding{rows.Integer(1),{rows.Text(2),rows.Text(3),rows.Text(4)},rows.Integer(5)};
        const auto evidence=Normalize(binding.evidence);
        if(prior.size()>=MaxRoster||!Text(name)||binding.id<=0||!evidence||!(*evidence==binding.evidence)||
           binding.revision<0||binding.revision>oldRevision||!prior.emplace(name,binding).second)
            return std::nullopt;
    }
    const auto next=CurrentBindings(state);
    if(known&&oldRevision==state.revision&&prior!=next)return std::nullopt;
    auto affected=ChangedAddresses(prior,next);
    // Legacy or otherwise unbound cached character scopes are never assumed
    // to belong to a new identity. Preserve unrelated account-wide scopes.
    auto cached=db.Prepare("SELECT name FROM character_rows WHERE account_name=? UNION SELECT character FROM native_inventory_scopes WHERE account=? AND scope IN ('Bags','Equipment') AND character<>''");
    cached.Bind(1,state.account);cached.Bind(2,state.account);
    while(cached.Step()){
        const auto name=cached.Text(0);
        if(!next.count(name)||!prior.count(name))affected.insert(name);
    }
    auto characters=db.Prepare("DELETE FROM character_rows WHERE account_name=? AND name=?");
    auto inventory=db.Prepare("DELETE FROM native_inventory_scopes WHERE account=? AND character=? AND scope IN ('Bags','Equipment')");
    for(const auto& name:affected){
        characters.Bind(1,state.account);characters.Bind(2,name);characters.Execute();characters.Reset();
        inventory.Bind(1,state.account);inventory.Bind(2,name);inventory.Execute();inventory.Reset();
    }
    if(!known||oldRevision!=state.revision){
        auto mark=db.Prepare("INSERT INTO cache_character_identity_state VALUES(?,?) ON CONFLICT(account) DO UPDATE SET revision=excluded.revision");
        mark.Bind(1,state.account);mark.Bind(2,state.revision);mark.Execute();
        auto clear=db.Prepare("DELETE FROM cache_character_bindings WHERE account=?");
        clear.Bind(1,state.account);clear.Execute();
        auto put=db.Prepare("INSERT INTO cache_character_bindings VALUES(?,?,?,?,?,?,?)");
        for(const auto& [name,b]:next){
            put.Bind(1,state.account);put.Bind(2,name);put.Bind(3,b.id);
            put.Bind(4,b.evidence.created);put.Bind(5,b.evidence.profession);put.Bind(6,b.evidence.race);
            put.Bind(7,b.revision);put.Execute();put.Reset();
        }
    }
    tx.Commit();return affected;
}
inline std::optional<std::set<std::string>> AdoptCache(const std::string& root,const State& state){
    if(root.empty())return std::nullopt;
    try{
        std::lock_guard<std::mutex> lock(Cache::Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Cache::Initialize(db);return AdoptCache(db,state);
    }catch(...){return std::nullopt;}
}
} // namespace AlterEgo::CharacterIdentity
