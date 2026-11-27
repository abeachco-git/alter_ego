#pragma once
#include "CharacterIdentityCore.h"
#include "Database.h"
#include "StoragePaths.h"

namespace AlterEgo::CharacterIdentity {
// The durable identity registry belongs with accounts, not the disposable
// cache. Unlinking credentials must not delete historical identity records.
inline void InitializeAccountRegistry(Database::Connection& db){
    Database::Migrate(db,Storage::kAccountRegistryApplicationId,
        Storage::kAccountRegistrySchemaVersion,[](Database::Connection& c,int version){
        if(version==1)c.Exec("CREATE TABLE accounts(id TEXT PRIMARY KEY,name TEXT NOT NULL,label TEXT NOT NULL,permissions TEXT NOT NULL) STRICT");
        else if(version==2)c.Exec(R"SQL(
CREATE TABLE character_identity_state(account TEXT PRIMARY KEY,revision INTEGER NOT NULL CHECK(revision>=0),next_id INTEGER NOT NULL CHECK(next_id>0)) STRICT;
CREATE TABLE character_identities(account TEXT NOT NULL REFERENCES character_identity_state(account) ON DELETE CASCADE,local_id INTEGER NOT NULL CHECK(local_id>0),created TEXT NOT NULL,profession TEXT NOT NULL,race TEXT NOT NULL,name TEXT NOT NULL,present INTEGER NOT NULL CHECK(present IN (0,1)),PRIMARY KEY(account,local_id),UNIQUE(account,created,profession,race)) STRICT;
CREATE UNIQUE INDEX character_identity_current_name ON character_identities(account,name) WHERE present=1;
CREATE TABLE character_identity_names(account TEXT NOT NULL,local_id INTEGER NOT NULL,name TEXT NOT NULL,PRIMARY KEY(account,local_id,name),FOREIGN KEY(account,local_id) REFERENCES character_identities(account,local_id) ON DELETE CASCADE) STRICT;
)SQL");
    });
}
// Caller owns a consistent transaction or serial account-registry access.
// A missing registry is not equivalent to verified empty character evidence.
inline State Read(Database::Connection& db,const std::string& account){
    State s;s.account=account;
    auto state=db.Prepare("SELECT revision,next_id FROM character_identity_state WHERE account=?");
    state.Bind(1,account);
    if(state.Step()){s.revision=state.Integer(0);s.nextId=state.Integer(1);}
    auto rows=db.Prepare("SELECT local_id,created,profession,race,name,present FROM character_identities WHERE account=?");
    rows.Bind(1,account);
    while(rows.Step()){
        const auto id=rows.Integer(0);
        if(s.records.size()>=MaxRecords||!s.records.emplace(id,Record{id,
            {rows.Text(1),rows.Text(2),rows.Text(3)},rows.Text(4),rows.Integer(5)!=0,{}}).second)
            throw std::runtime_error("Invalid character identity registry");
    }
    auto aliases=db.Prepare("SELECT local_id,name FROM character_identity_names WHERE account=?");
    aliases.Bind(1,account);
    while(aliases.Step()){
        auto& names=s.records.at(aliases.Integer(0)).names;
        if(names.size()>=MaxAliases||!names.insert(aliases.Text(1)).second)
            throw std::runtime_error("Invalid character identity history");
    }
    if(!Valid(s))throw std::runtime_error("Invalid character identity registry");
    return s;
}
// This is the durable commit primitive, NOT a live roster publisher. The
// future owner must hold the credential/roster generation gate and stage
// dependent migration/recovery before publishing a changed identity.
inline std::optional<Plan> Commit(Database::Connection& db,const std::string& account,
    int64_t expectedRevision,const std::set<std::string>& roster,
    const std::vector<Observation>& observations){
    Database::Transaction tx(db);
    const auto old=Read(db,account);
    if(old.revision!=expectedRevision)return std::nullopt;
    auto p=Reconcile(old,roster,observations);
    if(!p)return p;
    // Persist the first verified empty roster (revision zero also denotes
    // absent state); subsequent unchanged verifications need no writes.
    if(p.changes.empty()&&old.revision!=0){tx.Commit();return p;}
    auto state=db.Prepare("INSERT INTO character_identity_state VALUES(?,?,?) ON CONFLICT(account) DO UPDATE SET revision=excluded.revision,next_id=excluded.next_id");
    state.Bind(1,account);state.Bind(2,p.next.revision);state.Bind(3,p.next.nextId);state.Execute();
    // Free all present-name claims in the same transaction before swaps. Do
    // not delete identities or reallocate their stable local IDs.
    auto clear=db.Prepare("UPDATE character_identities SET present=0 WHERE account=?");
    clear.Bind(1,account);clear.Execute();
    auto row=db.Prepare("INSERT INTO character_identities VALUES(?,?,?,?,?,?,?) ON CONFLICT(account,local_id) DO UPDATE SET name=excluded.name,present=excluded.present");
    auto alias=db.Prepare("INSERT OR IGNORE INTO character_identity_names VALUES(?,?,?)");
    for(const auto& [id,r]:p.next.records){
        row.Bind(1,account);row.Bind(2,id);row.Bind(3,r.evidence.created);
        row.Bind(4,r.evidence.profession);row.Bind(5,r.evidence.race);row.Bind(6,r.name);
        row.Bind(7,static_cast<int64_t>(r.present));row.Execute();row.Reset();
        for(const auto& n:r.names){
            alias.Bind(1,account);alias.Bind(2,id);alias.Bind(3,n);alias.Execute();alias.Reset();
        }
    }
    tx.Commit();return p;
}
} // namespace AlterEgo::CharacterIdentity
