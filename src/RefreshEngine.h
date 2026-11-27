#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include "ApiActivity.h"
#include "RefreshPolicy.h"
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <mutex>
#include <string>
#include <vector>
namespace AlterEgo::RefreshEngine {
inline std::atomic<int>& ObjectiveMinutesValue(){static std::atomic<int> n{15};return n;}
inline int ObjectiveMinutes(){return ObjectiveMinutesValue().load();}
inline void SetObjectiveMinutes(int n){ObjectiveMinutesValue()=std::max(5,std::min(60,n));}
inline int64_t ObjectiveSeconds(){return ObjectiveMinutes()*60LL;}
inline int64_t Offset(const std::string& key){
    uint32_t h=2166136261u;
    for(unsigned char c:key){h^=c;h*=16777619u;}
    return h%(StaggerMaxSeconds+1); // stable 0-45 second spread on ordinary recurring requests
}
inline bool Due(int64_t success,int64_t attempt,int64_t now,int64_t interval,
                bool manual=false,int64_t minimumEpoch=0,int64_t offset=0,
                int64_t retry=FailureSeconds){
    if(manual)return true;
    if(RetryUntil(success,attempt,now,retry))return false;
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
    if(RetryUntil(success,attempt,now))return false;
    if(active)return changedSpec||now-std::max(activeSince,success>now?int64_t(0):success)>=CharacterSeconds;
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
// Called only after callback drain and worker join; no live claim owners.
inline void ResetMetadata(){auto& r=Metadata();std::lock_guard<std::mutex> lock(r.mutex);r.records.clear();r.pruneAt={};r.changed.notify_all();}
struct IdBatch {
    std::string dataset;
    std::vector<uint32_t> ids;
    // Only the owning worker records completed transport attempts. An
    // abandoned claim (including failed launch) must not create a cooldown.
    std::set<uint32_t> attempted;
    template<class Iterator> void MarkAttempted(Iterator first,Iterator last){
        attempted.insert(first,last);
    }
    // Incremental consumers release committed IDs without waiting for a
    // long bulk request. The destructor only releases outstanding ownership.
    std::set<uint32_t> released;
    template<class Iterator> void ReleaseIds(Iterator first,Iterator last,bool completed=false){
        auto& r=Metadata();std::lock_guard<std::mutex> lock(r.mutex);
        for(;first!=last;++first){
            const auto id=*first;if(!released.insert(id).second)continue;
            auto it=r.records.find({dataset,id});if(it==r.records.end())continue;
            it->second.pending=false;
            if(attempted.count(id))it->second.retry=MetadataRegistry::Clock::now()+std::chrono::seconds(FailureSeconds);
            else if(completed)it->second.retry={};
        }
        r.changed.notify_all();
    }
    ~IdBatch(){
        auto& r=Metadata();std::lock_guard<std::mutex> lock(r.mutex);
        for(auto id:ids){
            if(released.count(id))continue;
            auto it=r.records.find({dataset,id});
            if(it==r.records.end())continue;
            auto& e=it->second;
            e.pending=false;
            if(attempted.count(id))e.retry=MetadataRegistry::Clock::now()+std::chrono::seconds(FailureSeconds);
        }
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
// Read-only inspection for refresh controls; no claim or diagnostic mutation.
struct IdStatus {bool pending=false;int64_t retrySeconds=0;};
inline IdStatus InspectId(const std::string& dataset,uint32_t id){
    auto& r=Metadata();std::lock_guard<std::mutex> lock(r.mutex);
    auto it=r.records.find({dataset,id});if(it==r.records.end())return {};
    const auto remaining=it->second.retry-MetadataRegistry::Clock::now();
    return {it->second.pending,remaining>MetadataRegistry::Clock::duration::zero()?
        std::chrono::duration_cast<std::chrono::seconds>(remaining).count()+1:0};
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
