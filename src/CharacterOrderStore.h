#pragma once
#include "CharacterIdentityStore.h"
#include "CharacterIdentityRuntime.h"
#include <nlohmann/json.hpp>

namespace AlterEgo::CharacterOrder {
using Json=nlohmann::json;
using Ref=CharacterIdentity::Ref;
using RanksByRef=std::map<Ref,int64_t>;
struct PortableRank {
    std::string account,name;
    CharacterIdentity::Evidence evidence;
    int64_t rank=0;
    bool operator<(const PortableRank& o)const{
        return std::tie(account,name,evidence,rank)<std::tie(o.account,o.name,o.evidence,o.rank);
    }
};
using PortableRanks=std::set<PortableRank>;
struct OrderProjection {RanksByRef ranks;PortableRanks pending;};
inline bool Valid(const PortableRank& r){
    const auto e=CharacterIdentity::Normalize(r.evidence);
    return CharacterIdentity::Text(r.account)&&CharacterIdentity::Text(r.name)&&e&&*e==r.evidence&&r.rank>=0&&r.rank<65536;
}
inline Json EncodeRanks(const PortableRanks& ranks){
    Json out=Json::array();
    for(const auto& r:ranks)out.push_back({{"account",r.account},{"character",r.name},{"created",r.evidence.created},{"profession",r.evidence.profession},{"race",r.evidence.race},{"rank",r.rank}});
    return out;
}
inline PortableRanks DecodeRanks(const Json& j){
    if(!j.is_array()||j.size()>65536)throw std::runtime_error("Invalid custom order");
    PortableRanks out;
    for(const auto& row:j){
        PortableRank r{row.at("account").get<std::string>(),row.at("character").get<std::string>(),
            {row.at("created").get<std::string>(),row.at("profession").get<std::string>(),row.at("race").get<std::string>()},row.at("rank").get<int64_t>()};
        if(!Valid(r)||!out.insert(r).second)throw std::runtime_error("Invalid custom order entry");
    }
    return out;
}
inline PortableRank ReadRank(Database::Statement& q){
    PortableRank r{q.Text(0),q.Text(1),{q.Text(2),q.Text(3),q.Text(4)},q.Integer(5)};
    if(!Valid(r))throw std::runtime_error("Invalid stored custom order");
    return r;
}
inline void BindRank(Database::Statement& q,const PortableRank& r){
    q.Bind(1,r.account);q.Bind(2,r.name);q.Bind(3,r.evidence.created);q.Bind(4,r.evidence.profession);q.Bind(5,r.evidence.race);q.Bind(6,r.rank);
}
inline OrderProjection ReadOrder(Database::Connection& db){
    OrderProjection out;
    auto q=db.Prepare("SELECT account,local_id,rank FROM character_custom_order");
    while(q.Step()){Ref ref{q.Text(0),q.Integer(1)};const auto rank=q.Integer(2);if(!ref.Valid()||rank<0||rank>=65536)throw std::runtime_error("Invalid custom order rank");out.ranks.emplace(ref,rank);}
    auto p=db.Prepare("SELECT account,name,created,profession,race,rank FROM character_order_pending WHERE resolved=0");
    while(p.Step())out.pending.insert(ReadRank(p));
    return out;
}
inline void PutRank(Database::Connection& db,const Ref& ref,int64_t rank){
    auto q=db.Prepare("INSERT INTO character_custom_order VALUES(?,?,?) ON CONFLICT(account,local_id) DO UPDATE SET rank=excluded.rank");
    q.Bind(1,ref.account);q.Bind(2,ref.id);q.Bind(3,rank);q.Execute();
}
inline void StageRanks(Database::Connection& db,const PortableRanks& records){
    auto q=db.Prepare("INSERT OR IGNORE INTO character_order_pending(account,name,created,profession,race,rank) VALUES(?,?,?,?,?,?)");
    for(const auto& r:records){if(!Valid(r))throw std::runtime_error("Invalid portable rank");BindRank(q,r);q.Execute();q.Reset();}
}
inline void AdoptRanks(Database::Connection& db,const CharacterIdentity::State& state){
    const auto view=ReadOrder(db);std::map<Ref,PortableRanks> groups;
    for(const auto& p:view.pending){
        if(p.account!=state.account)continue;
        const CharacterIdentity::Record* match=nullptr;bool ambiguous=false;
        for(const auto& [id,r]:state.records)if(r.evidence==p.evidence){if(match){ambiguous=true;break;}match=&r;}
        if(ambiguous||!match||state.Current(match->name)!=match)continue;
        groups[{state.account,match->id}].insert(p);
    }
    for(const auto& [ref,records]:groups){
        const auto rank=records.begin()->rank;
        if(std::any_of(records.begin(),records.end(),[&](const auto& r){return r.rank!=rank;}))continue;
        const auto prior=view.ranks.find(ref);
        if(prior!=view.ranks.end()&&prior->second!=rank)continue;
        PutRank(db,ref,rank);
        auto q=db.Prepare("UPDATE character_order_pending SET resolved=1 WHERE account=? AND name=? AND created=? AND profession=? AND race=? AND rank=?");
        for(const auto& p:records){BindRank(q,p);q.Execute();q.Reset();}
    }
}
inline PortableRanks ExportRanks(Database::Connection& db){
    auto out=ReadOrder(db).pending;
    auto q=db.Prepare("SELECT p.account,i.name,i.created,i.profession,i.race,p.rank FROM character_custom_order p JOIN character_identities i ON i.account=p.account AND i.local_id=p.local_id");
    while(q.Step())out.insert(ReadRank(q));
    return out;
}
// Retained and temporarily unavailable characters keep their slots; a drag
// updates only the explicitly represented account and verified identities.
inline bool Reorder(Database::Connection& db,const std::string& account,const std::vector<Ref>& ordered){
    if(ordered.empty()||ordered.size()>CharacterIdentity::MaxRoster)return false;
    std::set<Ref> incoming;
    for(const auto& r:ordered)if(!r.Valid()||r.account!=account||!incoming.insert(r).second)return false;
    const auto view=ReadOrder(db);std::vector<std::pair<int64_t,Ref>> slots;
    for(const auto& [ref,rank]:view.ranks)if(ref.account==account)slots.emplace_back(rank,ref);
    std::sort(slots.begin(),slots.end());
    size_t i=0;std::vector<Ref> next;
    for(const auto& [rank,ref]:slots){
        if(incoming.count(ref))next.push_back(ordered[i++]);
        else next.push_back(ref);
    }
    for(;i<ordered.size();++i)next.push_back(ordered[i]);
    for(size_t j=0;j<next.size();++j)PutRank(db,next[j],static_cast<int64_t>(j));
    return true;
}
} // namespace AlterEgo::CharacterOrder
