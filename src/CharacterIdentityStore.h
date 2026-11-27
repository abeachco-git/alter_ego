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
        else if(version==3)c.Exec(R"SQL(
CREATE TABLE character_identities_v3(account TEXT NOT NULL REFERENCES character_identity_state(account) ON DELETE CASCADE,local_id INTEGER NOT NULL CHECK(local_id>0),created TEXT NOT NULL,profession TEXT NOT NULL,race TEXT NOT NULL,name TEXT NOT NULL,present INTEGER NOT NULL CHECK(present IN (0,1)),PRIMARY KEY(account,local_id)) STRICT;
CREATE TABLE character_identity_names_v3(account TEXT NOT NULL,local_id INTEGER NOT NULL,name TEXT NOT NULL,PRIMARY KEY(account,local_id,name),FOREIGN KEY(account,local_id) REFERENCES character_identities_v3(account,local_id) ON DELETE CASCADE) STRICT;
INSERT INTO character_identities_v3 SELECT * FROM character_identities;
INSERT INTO character_identity_names_v3 SELECT * FROM character_identity_names;
DROP TABLE character_identity_names;
DROP TABLE character_identities;
ALTER TABLE character_identities_v3 RENAME TO character_identities;
ALTER TABLE character_identity_names_v3 RENAME TO character_identity_names;
CREATE UNIQUE INDEX character_identity_current_name ON character_identities(account,name) WHERE present=1;
CREATE INDEX character_identity_fingerprint ON character_identities(account,created,profession,race);
CREATE TABLE character_identity_blocked(account TEXT NOT NULL REFERENCES character_identity_state(account) ON DELETE CASCADE,name TEXT NOT NULL,PRIMARY KEY(account,name)) STRICT;
CREATE TABLE character_identity_pending(account TEXT NOT NULL,local_id INTEGER NOT NULL,PRIMARY KEY(account,local_id),FOREIGN KEY(account,local_id) REFERENCES character_identities(account,local_id) ON DELETE CASCADE) STRICT;
)SQL");
        else if(version==4)c.Exec(R"SQL(
ALTER TABLE character_identities ADD COLUMN binding_revision INTEGER NOT NULL DEFAULT 0 CHECK(binding_revision>=0);
UPDATE character_identities SET binding_revision=(SELECT revision FROM character_identity_state WHERE account=character_identities.account);
)SQL");
        else if(version==5)c.Exec(R"SQL(
CREATE TABLE character_identity_roster_state(account TEXT PRIMARY KEY REFERENCES character_identity_state(account) ON DELETE CASCADE,attempted INTEGER NOT NULL CHECK(attempted>0),success INTEGER NOT NULL CHECK(success>=attempted)) STRICT;
CREATE TABLE character_identity_roster(account TEXT NOT NULL REFERENCES character_identity_roster_state(account) ON DELETE CASCADE,name TEXT NOT NULL,PRIMARY KEY(account,name)) STRICT;
)SQL");
        else if(version==6)c.Exec(R"SQL(
CREATE TABLE character_loadout_preferences(account TEXT NOT NULL,local_id INTEGER NOT NULL,mode INTEGER NOT NULL CHECK(mode IN (1,2)),loadout_id TEXT NOT NULL,CHECK((mode=1 AND loadout_id='') OR (mode=2 AND length(loadout_id)>0)),PRIMARY KEY(account,local_id),FOREIGN KEY(account,local_id) REFERENCES character_identities(account,local_id) ON DELETE CASCADE) STRICT;
CREATE TABLE character_loadout_pending(account TEXT NOT NULL,name TEXT NOT NULL,created TEXT NOT NULL,profession TEXT NOT NULL,race TEXT NOT NULL,mode INTEGER NOT NULL CHECK(mode IN (1,2)),loadout_id TEXT NOT NULL,CHECK((mode=1 AND loadout_id='') OR (mode=2 AND length(loadout_id)>0)),resolved INTEGER NOT NULL DEFAULT 0 CHECK(resolved IN (0,1)),PRIMARY KEY(account,name,created,profession,race,mode,loadout_id)) STRICT;
)SQL");
    });
}
// Caller owns a consistent transaction or serial account-registry access.
// A missing registry is not equivalent to verified empty character evidence.
inline State Read(Database::Connection& db,const std::string& account){
    State s;s.account=account;
    auto state=db.Prepare("SELECT revision,next_id FROM character_identity_state WHERE account=?");
    state.Bind(1,account);
    if(state.Step()){s.initialized=true;s.revision=state.Integer(0);s.nextId=state.Integer(1);}
    auto rows=db.Prepare("SELECT local_id,created,profession,race,name,present,binding_revision FROM character_identities WHERE account=?");
    rows.Bind(1,account);
    while(rows.Step()){
        const auto id=rows.Integer(0);
        if(s.records.size()>=MaxRecords||!s.records.emplace(id,Record{id,
            {rows.Text(1),rows.Text(2),rows.Text(3)},rows.Text(4),rows.Integer(5)!=0,{},rows.Integer(6)}).second)
            throw std::runtime_error("Invalid character identity registry");
    }
    auto aliases=db.Prepare("SELECT local_id,name FROM character_identity_names WHERE account=?");
    aliases.Bind(1,account);
    while(aliases.Step()){
        auto& names=s.records.at(aliases.Integer(0)).names;
        if(names.size()>=MaxAliases||!names.insert(aliases.Text(1)).second)
            throw std::runtime_error("Invalid character identity history");
    }
    auto blocked=db.Prepare("SELECT name FROM character_identity_blocked WHERE account=?");
    blocked.Bind(1,account);
    while(blocked.Step()){
        if(s.blocked.size()>=MaxRecords+MaxRoster||!s.blocked.insert(blocked.Text(0)).second)
            throw std::runtime_error("Invalid blocked character identity");
    }
    auto pending=db.Prepare("SELECT local_id FROM character_identity_pending WHERE account=?");
    pending.Bind(1,account);
    while(pending.Step()){
        if(s.pendingAbsence.size()>=MaxRoster||!s.pendingAbsence.insert(pending.Integer(0)).second)
            throw std::runtime_error("Invalid pending character identity");
    }
    if(!Valid(s))throw std::runtime_error("Invalid character identity registry");
    return s;
}
inline std::optional<RosterEvidence> ReadVerifiedRoster(Database::Connection& db,const std::string& account){
    auto q=db.Prepare("SELECT attempted,success FROM character_identity_roster_state WHERE account=?");
    q.Bind(1,account);if(!q.Step())return std::nullopt;
    RosterEvidence r{{},q.Integer(0),q.Integer(1)};
    auto names=db.Prepare("SELECT name FROM character_identity_roster WHERE account=?");
    names.Bind(1,account);
    while(names.Step()){
        if(r.names.size()>=MaxRoster||!r.names.insert(names.Text(0)).second)
            throw std::runtime_error("Invalid durable character roster");
    }
    if(!ValidRoster(r))throw std::runtime_error("Invalid durable character roster");
    return r;
}
// This is the durable commit primitive, NOT a live roster publisher. The
// future owner must hold the credential/roster generation gate and stage
// dependent migration/recovery before publishing a changed identity.
inline std::optional<Plan> Commit(Database::Connection& db,const std::string& account,
    int64_t expectedRevision,const std::set<std::string>& roster,
    const std::vector<Observation>& observations,bool confirmAbsences=false,
    const std::optional<RosterEvidence>& verifiedRoster=std::nullopt){
    Database::Transaction tx(db);
    const auto old=Read(db,account);
    if(old.revision!=expectedRevision)return std::nullopt;
    if(verifiedRoster){
        if(!ValidRoster(*verifiedRoster)||verifiedRoster->names!=roster)return std::nullopt;
        const auto prior=ReadVerifiedRoster(db,account);
        if(prior&&(prior->attempted>verifiedRoster->attempted||prior->success>verifiedRoster->success))
            return std::nullopt;
    }
    auto p=Reconcile(old,roster,observations,confirmAbsences);
    if(!p)return p;
    // Revision zero is also a legitimate initialized empty mapping.
    const auto writeRoster=[&]{
        if(!verifiedRoster)return;
        auto mark=db.Prepare("INSERT INTO character_identity_roster_state VALUES(?,?,?) ON CONFLICT(account) DO UPDATE SET attempted=excluded.attempted,success=excluded.success");
        mark.Bind(1,account);mark.Bind(2,verifiedRoster->attempted);mark.Bind(3,verifiedRoster->success);mark.Execute();
        auto clear=db.Prepare("DELETE FROM character_identity_roster WHERE account=?");
        clear.Bind(1,account);clear.Execute();
        auto put=db.Prepare("INSERT INTO character_identity_roster VALUES(?,?)");
        for(const auto& name:roster){put.Bind(1,account);put.Bind(2,name);put.Execute();put.Reset();}
    };
    if(old.initialized&&p.next.revision==old.revision){writeRoster();tx.Commit();return p;}
    auto state=db.Prepare("INSERT INTO character_identity_state VALUES(?,?,?) ON CONFLICT(account) DO UPDATE SET revision=excluded.revision,next_id=excluded.next_id");
    state.Bind(1,account);state.Bind(2,p.next.revision);state.Bind(3,p.next.nextId);state.Execute();
    // Free all present-name claims in the same transaction before swaps. Do
    // not delete identities or reallocate their stable local IDs.
    auto clear=db.Prepare("UPDATE character_identities SET present=0 WHERE account=?");
    clear.Bind(1,account);clear.Execute();
    auto row=db.Prepare("INSERT INTO character_identities(account,local_id,created,profession,race,name,present,binding_revision) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(account,local_id) DO UPDATE SET name=excluded.name,present=excluded.present,binding_revision=excluded.binding_revision");
    auto alias=db.Prepare("INSERT OR IGNORE INTO character_identity_names VALUES(?,?,?)");
    for(const auto& [id,r]:p.next.records){
        row.Bind(1,account);row.Bind(2,id);row.Bind(3,r.evidence.created);
        row.Bind(4,r.evidence.profession);row.Bind(5,r.evidence.race);row.Bind(6,r.name);
        row.Bind(7,static_cast<int64_t>(r.present));row.Bind(8,r.bindingRevision);row.Execute();row.Reset();
        for(const auto& n:r.names){
            alias.Bind(1,account);alias.Bind(2,id);alias.Bind(3,n);alias.Execute();alias.Reset();
        }
    }
    auto clearBlocked=db.Prepare("DELETE FROM character_identity_blocked WHERE account=?");
    clearBlocked.Bind(1,account);clearBlocked.Execute();
    auto blocked=db.Prepare("INSERT INTO character_identity_blocked VALUES(?,?)");
    for(const auto& name:p.next.blocked){blocked.Bind(1,account);blocked.Bind(2,name);blocked.Execute();blocked.Reset();}
    auto clearPending=db.Prepare("DELETE FROM character_identity_pending WHERE account=?");
    clearPending.Bind(1,account);clearPending.Execute();
    auto pending=db.Prepare("INSERT INTO character_identity_pending VALUES(?,?)");
    for(auto id:p.next.pendingAbsence){pending.Bind(1,account);pending.Bind(2,id);pending.Execute();pending.Reset();}
    writeRoster();tx.Commit();return p;
}
} // namespace AlterEgo::CharacterIdentity
