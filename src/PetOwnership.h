#pragma once
#include "CharacterIdentityRuntime.h"
#include "Database.h"
#include <algorithm>
#include <tuple>
#include <vector>
#include <map>
#include <set>

namespace AlterEgo::PetOwnership {
enum class Manual : int { Unknown=0, Owned=1, NotOwned=2 };
enum class Status { Unknown, Observed, Owned, NotOwned };
struct Evidence {
    bool observed=false;
    Manual manual=Manual::Unknown;
    Status State()const{
        if(manual==Manual::Owned)return Status::Owned;
        if(observed)return Status::Observed;
        return manual==Manual::NotOwned?Status::NotOwned:Status::Unknown;
    }
};
using Collection=std::map<uint32_t,Evidence>;
using Projection=std::map<CharacterIdentity::Ref,Collection>;
inline Evidence Get(const Collection& collection,uint32_t id){
    const auto it=collection.find(id);return it==collection.end()?Evidence{}:it->second;
}
inline const char* Label(Status value){
    switch(value){
    case Status::Owned:return "Owned";
    case Status::Observed:return "Observed";
    case Status::NotOwned:return "Not owned";
    default:return "Unknown";
    }
}
// Current membership is derived, never persisted. Physical tab number is
// authoritative, not vector position or display name.
struct Membership {
    int tab=0;
    bool aquatic=false;
    int slot=0; // one-based
};
using Memberships=std::map<uint32_t,std::vector<Membership>>;
template<class Tabs> Memberships BuildMemberships(const Tabs& tabs){
    Memberships result;
    for(const auto& tab:tabs){
        if(tab.profession!="Ranger"||!tab.pets_known||tab.tab<=0)continue;
        for(int i=0;i<2;++i){
            if(tab.pets.terrestrial[i])result[tab.pets.terrestrial[i]].push_back({tab.tab,false,i+1});
            if(tab.pets.aquatic[i])result[tab.pets.aquatic[i]].push_back({tab.tab,true,i+1});
        }
    }
    for(auto& entry:result){
        auto& slots=entry.second;
        std::sort(slots.begin(),slots.end(),[](const Membership& a,const Membership& b){
            return std::tie(a.tab,a.aquatic,a.slot)<std::tie(b.tab,b.aquatic,b.slot);
        });
        slots.erase(std::unique(slots.begin(),slots.end(),[](const Membership& a,const Membership& b){
            return a.tab==b.tab&&a.aquatic==b.aquatic&&a.slot==b.slot;
        }),slots.end());
    }
    return result;
}
// Only pass membership from an identity-qualified, verified Builds scope.
// Current positive selections are evidence even if the durable registry
// predates them or its observation write failed. Never mutate it here.
inline Collection WithVerifiedMembership(Collection collection,const Memberships& membership){
    for(const auto& [id,slots]:membership)if(id&&!slots.empty())
        collection[id].observed=true;
    return collection;
}
inline bool InTab(const Memberships& membership,uint32_t id,int tab){
    if(tab<=0)return false;
    const auto it=membership.find(id);
    return it!=membership.end()&&std::any_of(it->second.begin(),it->second.end(),[&](const auto& m){return m.tab==tab;});
}
template<class Tabs> int TabIndex(const Tabs& tabs,int tab){
    if(tab<=0)return -1;
    for(size_t i=0;i<tabs.size();++i)
        if(tabs[i].tab==tab&&tabs[i].profession=="Ranger"&&tabs[i].pets_known)return static_cast<int>(i);
    return -1;
}
// All-tab membership is ownership evidence only if the caller has verified
// character identity and committed the Builds scope.
template<class Tabs> std::set<uint32_t> Selected(const Tabs& tabs,int activeTab=0){
    std::set<uint32_t> result;
    const auto membership=BuildMemberships(tabs);
    for(const auto& entry:membership)
        if(!activeTab||InTab(membership,entry.first,activeTab))result.insert(entry.first);
    return result;
}
enum class Filter { All, Confirmed, Observed, ManualOwned, NotOwned, Unknown, Active, AnyBuild };
inline bool Matches(Filter filter,const Evidence& evidence,bool active,bool inBuild){
    switch(filter){
    case Filter::Confirmed:return evidence.observed||evidence.manual==Manual::Owned;
    case Filter::Observed:return evidence.observed;
    case Filter::ManualOwned:return evidence.manual==Manual::Owned;
    case Filter::NotOwned:return evidence.State()==Status::NotOwned;
    case Filter::Unknown:return evidence.State()==Status::Unknown;
    case Filter::Active:return active;
    case Filter::AnyBuild:return inBuild;
    default:return true;
    }
}
struct Counts {
    size_t confirmed=0,observed=0,manualOwned=0,notOwned=0,unknown=0;
    void Add(const Evidence& e){
        if(Matches(Filter::Confirmed,e,false,false))++confirmed;
        if(e.observed)++observed;
        if(e.manual==Manual::Owned)++manualOwned;
        if(e.State()==Status::NotOwned)++notOwned;
        if(e.State()==Status::Unknown)++unknown;
    }
};
inline bool Ranger(Database::Connection& db,const CharacterIdentity::Ref& ref){
    if(!ref.Valid())return false;
    auto q=db.Prepare("SELECT profession FROM character_identities WHERE account=? AND local_id=? AND present=1");
    q.Bind(1,ref.account);q.Bind(2,ref.id);
    return q.Step()&&q.Text(0)=="Ranger";
}
inline Projection Read(Database::Connection& db){
    Projection result;
    auto q=db.Prepare("SELECT account,local_id,pet_id,observed,manual FROM character_pet_ownership");
    while(q.Step()){
        const auto manual=static_cast<Manual>(q.Integer(4));
        result[{q.Text(0),q.Integer(1)}].emplace(static_cast<uint32_t>(q.Integer(2)),Evidence{q.Integer(3)!=0,manual});
    }
    return result;
}
// Caller owns the existing account registry transaction and identity guard.
inline bool Observe(Database::Connection& db,const CharacterIdentity::Ref& ref,const std::set<uint32_t>& ids){
    if(!Ranger(db,ref)||ids.count(0)||ids.size()>256)return false;
    auto q=db.Prepare("INSERT INTO character_pet_ownership VALUES(?,?,?,1,0) "
        "ON CONFLICT(account,local_id,pet_id) DO UPDATE SET observed=1,manual=CASE WHEN manual=2 THEN 0 ELSE manual END "
        "WHERE observed=0");
    for(auto id:ids){
        q.Bind(1,ref.account);q.Bind(2,ref.id);q.Bind(3,id);q.Execute();q.Reset();
    }
    return true;
}
inline bool SetManual(Database::Connection& db,const CharacterIdentity::Ref& ref,uint32_t id,Manual manual){
    if(!id||!Ranger(db,ref)||(manual!=Manual::Unknown&&manual!=Manual::Owned&&manual!=Manual::NotOwned))return false;
    auto prior=db.Prepare("SELECT observed FROM character_pet_ownership WHERE account=? AND local_id=? AND pet_id=?");
    prior.Bind(1,ref.account);prior.Bind(2,ref.id);prior.Bind(3,id);
    const bool observed=prior.Step()&&prior.Integer(0)!=0;
    if(manual==Manual::NotOwned&&observed)return false;
    if(manual==Manual::Unknown&&!observed){
        auto q=db.Prepare("DELETE FROM character_pet_ownership WHERE account=? AND local_id=? AND pet_id=?");
        q.Bind(1,ref.account);q.Bind(2,ref.id);q.Bind(3,id);q.Execute();return true;
    }
    auto q=db.Prepare("INSERT INTO character_pet_ownership VALUES(?,?,?,?,?) "
        "ON CONFLICT(account,local_id,pet_id) DO UPDATE SET manual=excluded.manual");
    q.Bind(1,ref.account);q.Bind(2,ref.id);q.Bind(3,id);q.Bind(4,observed);q.Bind(5,static_cast<int>(manual));q.Execute();
    return true;
}
} // namespace AlterEgo::PetOwnership
