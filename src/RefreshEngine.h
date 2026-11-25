#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include "ApiActivity.h"
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace AlterEgo::RefreshEngine {
inline constexpr int64_t BrowseSeconds=1800, DefaultObjectiveSeconds=900, FailureSeconds=900;
inline constexpr int64_t CharacterSeconds=600, LogoutGraceSeconds=600;
inline constexpr int64_t SeasonSeconds=86400;
inline std::atomic<int>& ObjectiveMinutesValue(){static std::atomic<int> n{15};return n;}
inline int ObjectiveMinutes(){return ObjectiveMinutesValue().load();}
inline void SetObjectiveMinutes(int n){ObjectiveMinutesValue()=std::max(5,std::min(60,n));}
inline int64_t ObjectiveSeconds(){return ObjectiveMinutes()*60LL;}
inline int64_t Offset(const std::string& key){
    uint32_t h=2166136261u;
    for(unsigned char c:key){h^=c;h*=16777619u;}
    return h%46; // stable 0-45 second spread on ordinary recurring requests
}
inline bool Due(int64_t success,int64_t attempt,int64_t now,int64_t interval,
                bool manual=false,int64_t minimumEpoch=0,int64_t offset=0,
                int64_t retry=FailureSeconds){
    if(manual)return true;
    if(attempt>0&&attempt<=now&&now-attempt<retry)return false;
    if(success<=0||success>now||success<minimumEpoch)return true;
    return now-success>=interval+offset;
}
// A request started during the propagation window cannot verify the final
// logged-out state, even when a manual request completes successfully.
inline bool LogoutReady(int64_t lastSeen,int64_t now){
    return lastSeen<=0||(now>=lastSeen&&now-lastSeen>=LogoutGraceSeconds);
}
inline bool CanReconcileLogout(int64_t lastSeen,int64_t requestStarted){
    return lastSeen>0&&LogoutReady(lastSeen,requestStarted);
}
// reconciledThrough is the latest observed activity explicitly covered by a
// verified snapshot. Legacy callers may use success as their baseline, but
// the activity scheduler must persist an independent watermark.
inline bool CharacterDue(bool active,int64_t activeSince,int64_t lastSeen,int64_t success,
                         int64_t attempt,int64_t now,bool changedSpec=false,bool manual=false,
                         int64_t reconciledThrough=-1){
    if(manual)return true;
    if(attempt>0&&attempt<=now&&now-attempt<FailureSeconds)return false;
    if(active)return success<=0||changedSpec||now-std::max(activeSince,success)>=CharacterSeconds;
    if(!LogoutReady(lastSeen,now))return false;
    if(success<=0)return true;
    const auto covered=reconciledThrough>=0?reconciledThrough:success;
    return lastSeen>covered;
}
// Claims are made BEFORE dispatching a worker. They live until the final
// owner releases them, including cancellation/failed worker creation.
struct MetadataRegistry {
    using Clock=std::chrono::steady_clock;
    struct Entry{bool pending=false;Clock::time_point retry{};};
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::pair<std::string,uint32_t>,Entry> records;
    Clock::time_point pruneAt{};
};
inline MetadataRegistry& Metadata(){static MetadataRegistry r;return r;}
struct IdBatch {
    std::string dataset;
    std::vector<uint32_t> ids;
    ~IdBatch(){
        auto& r=Metadata();std::lock_guard<std::mutex> lock(r.mutex);
        for(auto id:ids){auto& e=r.records[{dataset,id}];e.pending=false;e.retry=MetadataRegistry::Clock::now()+std::chrono::seconds(FailureSeconds);}
        r.changed.notify_all();
    }
};
inline std::shared_ptr<IdBatch> ClaimIds(const std::string& dataset,const std::vector<uint32_t>& ids,bool manual=false){
    auto batch=std::make_shared<IdBatch>();batch->dataset=dataset;
    auto& r=Metadata();std::lock_guard<std::mutex> lock(r.mutex);
    const auto now=MetadataRegistry::Clock::now();
    if(now>=r.pruneAt){
        for(auto it=r.records.begin();it!=r.records.end();){
            if(!it->second.pending&&it->second.retry<=now)it=r.records.erase(it);else ++it;
        }
        r.pruneAt=now+std::chrono::minutes(1);
    }
    uint64_t coalesced=0,cooldown=0;
    for(auto id:ids){
        if(!id)continue;
        auto& e=r.records[{dataset,id}];
        if(e.pending){++coalesced;continue;}
        if(!manual&&now<e.retry){++cooldown;continue;}
        batch->ids.push_back(id);e.pending=true;
    }
    ApiActivity::Session().MetadataClaim(batch->ids.size(),coalesced,cooldown,manual);
    return batch;
}
// Synchronous callers that actually consume metadata immediately can join
// an existing owner instead of starting a duplicate HTTP request. Ordinary
// asynchronous discovery never waits; shutdown and timeouts remain bounded.
template<class Stopping>
inline bool WaitForIds(const std::string& dataset,const std::vector<uint32_t>& ids,
                      Stopping stopping,std::chrono::milliseconds timeout=std::chrono::seconds(30)){
    auto& r=Metadata();
    const auto until=MetadataRegistry::Clock::now()+timeout;
    std::unique_lock<std::mutex> lock(r.mutex);
    const auto pending=[&]{
        for(auto id:ids){auto it=r.records.find({dataset,id});if(it!=r.records.end()&&it->second.pending)return true;}
        return false;
    };
    while(pending()){
        if(stopping()||MetadataRegistry::Clock::now()>=until)return false;
        r.changed.wait_until(lock,std::min(until,MetadataRegistry::Clock::now()+std::chrono::milliseconds(50)));
    }
    return !stopping();
}
}
