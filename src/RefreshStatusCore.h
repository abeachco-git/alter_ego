#pragma once
#include "RefreshPolicy.h"
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <cstdint>
namespace AlterEgo::RefreshStatus {
using RefreshEngine::Eligibility;
using RefreshEngine::WaitReason;
enum class Access { Allowed, NoAPI, Unverified, Denied };
enum class Phase { Idle, Queued, Working };
enum class Kind { Ready, NoData, NoScopes, Due, Scheduled, Sync, Disabled, Retry, Failed, NoAPI, Unverified, Denied, Queued, Working, Complete };
struct Scope {
    int64_t success=0,attempt=0;
    bool fresh=false,automatic=false,failed=false,demandAllowed=true;
    Access access=Access::Allowed;
    Phase phase=Phase::Idle;
    Eligibility eligibility;
    std::string error;
};
inline std::string Duration(int64_t seconds){
    seconds=std::max<int64_t>(0,seconds);
    return std::to_string(seconds/60)+"m "+std::to_string(seconds%60)+"s";
}
inline std::string Age(int64_t stamp,int64_t now){
    if(stamp<=0)return "not available";
    if(stamp>now)return "future timestamp";
    const auto seconds=now-stamp;
    if(seconds<60)return "just now";
    if(seconds<3600)return std::to_string(seconds/60)+"m ago";
    if(seconds<86400)return std::to_string(seconds/3600)+"h ago";
    return std::to_string(seconds/86400)+"d ago";
}
inline const char* Reason(WaitReason reason){
    switch(reason){
    case WaitReason::Due:return "Refresh due";
    case WaitReason::LoginGrace:return "Login propagation grace";
    case WaitReason::ActiveInterval:return "Active refresh interval";
    case WaitReason::Stagger:return "Staggered refresh";
    case WaitReason::LogoutGrace:return "Logout propagation grace";
    case WaitReason::SpecDebounce:return "Specialization change";
    case WaitReason::Retry:return "Retry cooldown";
    default:return "Current";
    }
}
inline const char* AccessLabel(Access access){
    switch(access){
    case Access::NoAPI:return "No API account";
    case Access::Unverified:return "Ownership unverified";
    case Access::Denied:return "Missing permission";
    default:return "Allowed";
    }
}
inline bool Transient(const std::string& error){
    return error=="Rate limited; retry later"||error=="Transport failed or cancelled"||
        error=="HTTP 429"||error.rfind("HTTP 5",0)==0;
}
inline Kind Project(const Scope& s,int64_t now){
    if(s.phase==Phase::Working)return Kind::Working;
    if(s.phase==Phase::Queued)return Kind::Queued;
    if(s.access==Access::NoAPI)return Kind::NoAPI;
    if(s.access==Access::Unverified)return Kind::Unverified;
    if(s.access==Access::Denied)return Kind::Denied;
    if(s.failed){
        if(s.automatic&&Transient(s.error)&&s.eligibility.reason==WaitReason::Retry&&s.eligibility.at>now)
            return Kind::Retry;
        return Kind::Failed;
    }
    if(s.fresh)return Kind::Ready;
    if(!s.automatic)return Kind::Disabled;
    if(!s.demandAllowed)return Kind::Scheduled;
    if(s.eligibility.reason==WaitReason::Retry&&s.eligibility.at>now)return Kind::Retry;
    if(s.eligibility.due)return Kind::Due;
    if(s.eligibility.reason==WaitReason::LoginGrace||s.eligibility.reason==WaitReason::LogoutGrace)return Kind::Sync;
    if(s.eligibility.at>now)return Kind::Scheduled;
    return s.success>0?Kind::Scheduled:Kind::NoData;
}
inline int Priority(Kind k){
    switch(k){
    case Kind::Working:return 100;
    case Kind::Queued:return 90;
    case Kind::NoAPI:return 85;
    case Kind::Denied:return 84;
    case Kind::Unverified:return 83;
    case Kind::Failed:return 80;
    case Kind::Retry:return 70;
    case Kind::Disabled:return 60;
    case Kind::Due:return 50;
    case Kind::Sync:return 40;
    case Kind::Scheduled:return 30;
    case Kind::NoData:return 20;
    case Kind::Complete:return 10;
    default:return 0;
    }
}
struct Aggregate {
    unsigned count=0;
    bool missing=false;
    int64_t oldest=0,retryAt=0;
    Kind kind=Kind::Ready;
    void Add(const Scope& s,int64_t now){
        ++count;
        if(s.success<=0||s.success>now)missing=true;
        else if(!oldest||s.success<oldest)oldest=s.success;
        const auto next=Project(s,now);
        if(next==Kind::Retry&&(!retryAt||s.eligibility.at<retryAt))retryAt=s.eligibility.at;
        if(Priority(next)>Priority(kind))kind=next;
    }
    int64_t Timestamp()const{return count&&!missing?oldest:0;}
    Kind Status()const{return count?kind:Kind::NoScopes;}
};
inline std::string Label(Kind kind,int64_t now=0,int64_t retryAt=0){
    switch(kind){
    case Kind::NoData:return "Not retrieved";
    case Kind::NoScopes:return "No scopes";
    case Kind::Due:return "Refresh due";
    case Kind::Scheduled:return "Scheduled";
    case Kind::Sync:return "Awaiting sync";
    case Kind::Disabled:return "Auto disabled";
    case Kind::Retry:return retryAt>now?"Retry in "+std::to_string((retryAt-now-1)/60+1)+"m":"Retry cooldown";
    case Kind::Failed:return "Failed";
    case Kind::NoAPI:return "No API";
    case Kind::Unverified:return "Unverified";
    case Kind::Denied:return "Permission denied";
    case Kind::Queued:return "Queued";
    case Kind::Working:return "Refreshing";
    case Kind::Complete:return "Complete";
    default:return "Ready";
    }
}
inline std::string Describe(const Scope& s,int64_t now,int64_t sharedLogoutAt=0){
    std::string text=s.success<=0?"Not retrieved":s.success>now?"Unverified timestamp":s.fresh?"Current":"Stale";
    text+="; last success: "+Age(s.success,now);
    if(s.attempt>0)text+="; last attempt: "+Age(s.attempt,now);
    if(s.access!=Access::Allowed)text+="; "+std::string(AccessLabel(s.access));
    else if(!s.automatic)text+="; automatic refresh disabled";
    else if(!s.demandAllowed)text+="; awaiting eligible account demand";
    else if(s.eligibility.reason!=WaitReason::Current&&
        !(sharedLogoutAt&&s.eligibility.reason==WaitReason::LogoutGrace&&s.eligibility.at==sharedLogoutAt)){
        text+="; "+std::string(Reason(s.eligibility.reason));
        if(s.eligibility.at>now)text+=" "+Duration(s.eligibility.at-now);
    }
    else if(s.eligibility.reason==WaitReason::Current&&s.eligibility.at>now)text+="; next eligible in "+Duration(s.eligibility.at-now);
    if(s.phase!=Phase::Idle)text+="; "+Label(s.phase==Phase::Queued?Kind::Queued:Kind::Working);
    if(!s.error.empty())text+="; "+s.error;
    return text;
}
// Read-only phase registry for shared endpoint owners. Multiple consumers can
// queue behind one active owner for the same exact key. No admission semantics.
template<class Key> class WorkPhases {
    struct Counts { unsigned queued=0,working=0; };
    std::map<Key,Counts> rows_;
public:
    void Queue(const Key& key){++rows_[key].queued;}
    void Start(const Key& key){
        auto i=rows_.find(key);if(i==rows_.end()||!i->second.queued)return;
        --i->second.queued;++i->second.working;
    }
    void Finish(const Key& key,Phase phase){
        auto i=rows_.find(key);if(i==rows_.end())return;
        if(phase==Phase::Working&&i->second.working)--i->second.working;
        else if(phase==Phase::Queued&&i->second.queued)--i->second.queued;
        if(!i->second.queued&&!i->second.working)rows_.erase(i);
    }
    Phase Get(const Key& key)const{
        auto i=rows_.find(key);if(i==rows_.end())return Phase::Idle;
        return i->second.working?Phase::Working:i->second.queued?Phase::Queued:Phase::Idle;
    }
};
// Session-local observations only: never used to admit, cancel, dispatch,
// persist or retry a request. Caller owns mutex.
using CharacterKey=std::pair<std::string,std::string>;
struct CharacterWork {
    uint64_t generation=0;
    uint32_t requested=0,verified=0,failed=0,active=0;
    Phase phase=Phase::Idle;
    int64_t finished=0;
    std::string detail;
};
class CharacterBatch {
    std::map<CharacterKey,CharacterWork> rows_;
public:
    void Reset(){rows_.clear();}
    void Queue(const CharacterKey& key,uint64_t generation,uint32_t mask){
        if(!key.first.empty()&&!key.second.empty()&&mask)
            rows_[key]={generation,mask,0,0,0,Phase::Queued,0,{}};
    }
    void Start(const CharacterKey& key,uint64_t generation,uint32_t mask){
        if(!mask){rows_.erase(key);return;}
        Queue(key,generation,mask);
        auto i=rows_.find(key);if(i!=rows_.end())i->second.phase=Phase::Working;
    }
    void Activate(const CharacterKey& key,uint32_t mask){
        auto i=rows_.find(key);if(i!=rows_.end())i->second.active=mask&i->second.requested;
    }
    void Finish(const CharacterKey& key,uint32_t verified,int64_t now,const std::string& detail={}){
        auto i=rows_.find(key);if(i==rows_.end())return;
        auto& r=i->second;r.verified=verified&r.requested;r.failed=r.requested&~r.verified;
        r.phase=Phase::Idle;r.active=0;r.finished=now;r.detail=detail;
    }
    void Stop(int64_t now,const std::string& detail){
        for(auto i=rows_.begin();i!=rows_.end();){
            if(i->second.phase==Phase::Queued){i=rows_.erase(i);continue;}
            if(i->second.phase==Phase::Working)Finish(i->first,0,now,detail);
            ++i;
        }
    }
    CharacterWork Get(const CharacterKey& key,uint64_t generation)const{
        auto i=rows_.find(key);
        return i!=rows_.end()&&generation&&i->second.generation==generation?i->second:CharacterWork{};
    }
};
}
