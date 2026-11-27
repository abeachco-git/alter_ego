#pragma once
#include "CharacterIdentityRuntime.h"
#include "Database.h"

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
// All-tab membership is ownership evidence only if the caller has verified
// the character identity and successfully committed the Builds scope.
// The exact live active tab is separate from historical ownership.
template<class Tabs> std::set<uint32_t> Selected(const Tabs& tabs,int activeTab=0){
    std::set<uint32_t> result;
    for(const auto& tab:tabs){
        if(tab.profession!="Ranger"||!tab.pets_known||(activeTab&&tab.tab!=activeTab))continue;
        for(auto id:tab.pets.terrestrial)if(id)result.insert(id);
        for(auto id:tab.pets.aquatic)if(id)result.insert(id);
    }
    return result;
}
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
