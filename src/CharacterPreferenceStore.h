#pragma once
#include "CharacterIdentityStore.h"
#include "CharacterIdentityRuntime.h"
#include "CharacterLoadoutExpectation.h"

namespace AlterEgo::CharacterLoadout {
using Json=nlohmann::json;
struct PortableChoice {
    std::string account,name;
    CharacterIdentity::Evidence evidence;
    Choice choice;
    bool HasEvidence()const{return !evidence.created.empty();}
    bool operator<(const PortableChoice& o)const{
        return std::tie(account,name,evidence,choice.mode,choice.id)<std::tie(o.account,o.name,o.evidence,o.choice.mode,o.choice.id);
    }
};
using PortableChoices=std::set<PortableChoice>;
struct Projection {
    std::map<CharacterIdentity::Ref,Choice> choices;
    PortableChoices pending;
    Choice Get(const CharacterIdentity::Ref& ref)const{
        const auto it=choices.find(ref);return it==choices.end()?Choice{}:it->second;
    }
};
inline bool ValidPortable(const PortableChoice& p){
    if(!ValidKey({p.account,p.name})||!CharacterIdentity::Text(p.account)||!CharacterIdentity::Text(p.name)||
       p.choice.mode==Mode::Automatic||!ValidChoice(p.choice))return false;
    if(!p.HasEvidence())return p.evidence.profession.empty()&&p.evidence.race.empty();
    const auto normalized=CharacterIdentity::Normalize(p.evidence);
    return normalized&&*normalized==p.evidence;
}
inline PortableChoices DecodePortable(const Json& j){
    if(j.at("format")!="alter_ego_character_loadouts"||j.at("version")!=1||
       !j.at("entries").is_array()||j.at("entries").size()>16384)throw std::runtime_error("Invalid character Loadout preferences");
    PortableChoices result;
    for(const auto& row:j.at("entries")){
        const auto mode=row.at("mode").get<std::string>();
        if(mode!="none"&&mode!="explicit")throw std::runtime_error("Invalid Loadout preference mode");
        PortableChoice p{row.at("account").get<std::string>(),row.at("character").get<std::string>(),
            {row.value("created",std::string{}),row.value("profession",std::string{}),row.value("race",std::string{})},
            {mode=="none"?Mode::None:Mode::Explicit,row.at("id").get<std::string>()}};
        if(!ValidPortable(p)||!result.insert(std::move(p)).second)throw std::runtime_error("Invalid or duplicate Loadout preference");
    }
    return result;
}
inline Json EncodePortable(const PortableChoices& choices){
    Json entries=Json::array();
    for(const auto& p:choices)entries.push_back({{"account",p.account},{"character",p.name},
        {"created",p.evidence.created},{"profession",p.evidence.profession},{"race",p.evidence.race},
        {"mode",p.choice.mode==Mode::None?"none":"explicit"},{"id",p.choice.id}});
    return {{"format","alter_ego_character_loadouts"},{"version",1},{"entries",entries}};
}
inline PortableChoices LegacyChoices(const Json& legacy){
    Preferences old;if(!old.Decode(legacy))throw std::runtime_error("Invalid legacy Loadout preferences");
    return DecodePortable({{"format","alter_ego_character_loadouts"},{"version",1},{"entries",old.Encode()}});
}
inline Choice ReadChoice(Database::Statement& q,int column){
    const auto mode=q.Integer(column);
    if(mode!=1&&mode!=2)throw std::runtime_error("Invalid stored Loadout preference");
    Choice c{mode==1?Mode::None:Mode::Explicit,q.Text(column+1)};
    if(!ValidChoice(c))throw std::runtime_error("Invalid stored Loadout reference");
    return c;
}
inline PortableChoice ReadPortable(Database::Statement& q){
    PortableChoice p{q.Text(0),q.Text(1),{q.Text(2),q.Text(3),q.Text(4)},ReadChoice(q,5)};
    if(!ValidPortable(p))throw std::runtime_error("Invalid stored portable Loadout preference");
    return p;
}
inline Projection ReadProjection(Database::Connection& db){
    Projection out;
    auto q=db.Prepare("SELECT account,local_id,mode,loadout_id FROM character_loadout_preferences");
    while(q.Step()){
        CharacterIdentity::Ref ref{q.Text(0),q.Integer(1)};
        if(!ref.Valid()||!out.choices.emplace(ref,ReadChoice(q,2)).second)throw std::runtime_error("Invalid Loadout preference identity");
    }
    auto pending=db.Prepare("SELECT account,name,created,profession,race,mode,loadout_id FROM character_loadout_pending WHERE resolved=0");
    while(pending.Step())out.pending.insert(ReadPortable(pending));
    return out;
}
inline void BindPortable(Database::Statement& q,const PortableChoice& p){
    q.Bind(1,p.account);q.Bind(2,p.name);q.Bind(3,p.evidence.created);q.Bind(4,p.evidence.profession);
    q.Bind(5,p.evidence.race);q.Bind(6,static_cast<int64_t>(p.choice.mode));q.Bind(7,p.choice.id);
}
inline void Stage(Database::Connection& db,const PortableChoices& choices){
    auto q=db.Prepare("INSERT OR IGNORE INTO character_loadout_pending(account,name,created,profession,race,mode,loadout_id) VALUES(?,?,?,?,?,?,?)");
    for(const auto& p:choices){
        if(!ValidPortable(p))throw std::runtime_error("Invalid pending Loadout preference");
        BindPortable(q,p);q.Execute();q.Reset();
    }
}
inline bool SaveChoice(Database::Connection& db,const CharacterIdentity::Ref& ref,const Choice& c){
    if(!ref.Valid()||!ValidChoice(c))return false;
    if(c.mode==Mode::Automatic){
        auto q=db.Prepare("DELETE FROM character_loadout_preferences WHERE account=? AND local_id=?");
        q.Bind(1,ref.account);q.Bind(2,ref.id);q.Execute();return true;
    }
    auto q=db.Prepare("INSERT INTO character_loadout_preferences VALUES(?,?,?,?) ON CONFLICT(account,local_id) DO UPDATE SET mode=excluded.mode,loadout_id=excluded.loadout_id");
    q.Bind(1,ref.account);q.Bind(2,ref.id);q.Bind(3,static_cast<int64_t>(c.mode));q.Bind(4,c.id);q.Execute();return true;
}
inline void ResolvePending(Database::Connection& db,const PortableChoice& p){
    auto q=db.Prepare("UPDATE character_loadout_pending SET resolved=1 WHERE account=? AND name=? AND created=? AND profession=? AND race=? AND mode=? AND loadout_id=?");
    BindPortable(q,p);q.Execute();
}
// Caller holds the account gate and one database transaction. Automatic
// adoption requires unique evidence across the complete retained registry;
// aliases and unverified name-only records never authorize adoption.
inline void AdoptPortable(Database::Connection& db,const CharacterIdentity::State& state){
    if(!state.initialized)return;
    const auto projection=ReadProjection(db);
    std::map<CharacterIdentity::Ref,PortableChoices> proposals;
    for(const auto& p:projection.pending){
        if(p.account!=state.account||!p.HasEvidence())continue;
        const CharacterIdentity::Record* match=nullptr;bool ambiguous=false;
        for(const auto& [id,r]:state.records)if(r.evidence==p.evidence){
            if(match){ambiguous=true;break;}match=&r;
        }
        if(ambiguous||!match||!match->present||state.Current(match->name)!=match)continue;
        proposals[{state.account,match->id}].insert(p);
    }
    for(const auto& [ref,records]:proposals){
        const auto& first=*records.begin();
        if(std::any_of(records.begin(),records.end(),[&](const auto& p){return !(p.choice==first.choice);}))continue;
        const auto current=projection.Get(ref);
        if(current.mode!=Mode::Automatic&&!(current==first.choice))continue;
        if(!SaveChoice(db,ref,first.choice))throw std::runtime_error("Cannot adopt Loadout preference");
        for(const auto& p:records)ResolvePending(db,p);
    }
}
inline bool CanRecover(const CharacterIdentity::State& state,const CharacterIdentity::Ref& ref,const PortableChoice& p){
    if(!ref.Valid()||state.account!=ref.account||p.account!=ref.account)return false;
    const auto it=state.records.find(ref.id);
    if(it==state.records.end()||state.Current(it->second.name)!=&it->second)return false;
    const auto& r=it->second;
    return p.HasEvidence()?p.evidence==r.evidence:p.name==r.name||r.names.count(p.name);
}
inline bool Recover(Database::Connection& db,const CharacterIdentity::State& state,const CharacterIdentity::Ref& ref,const PortableChoice& p){
    const auto projection=ReadProjection(db);
    if(!projection.pending.count(p)||!CanRecover(state,ref,p)||!SaveChoice(db,ref,p.choice))return false;
    ResolvePending(db,p);return true;
}
inline PortableChoices ExportPortable(Database::Connection& db){
    auto out=ReadProjection(db).pending;
    auto q=db.Prepare("SELECT p.account,i.name,i.created,i.profession,i.race,p.mode,p.loadout_id FROM character_loadout_preferences p JOIN character_identities i ON i.account=p.account AND i.local_id=p.local_id");
    while(q.Step())out.insert(ReadPortable(q));
    return out;
}
} // namespace AlterEgo::CharacterLoadout
