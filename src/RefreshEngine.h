#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace AlterEgo::RefreshEngine {
inline constexpr int64_t BrowseSeconds=1800, DefaultObjectiveSeconds=900, FailureSeconds=900;
inline constexpr int64_t CharacterSeconds=1800, LogoutGraceSeconds=300;
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
inline bool CharacterDue(bool active,int64_t activeSince,int64_t lastSeen,int64_t success,
                         int64_t attempt,int64_t now,bool changedSpec=false,bool manual=false){
    if(manual)return true;
    if(attempt>0&&attempt<=now&&now-attempt<FailureSeconds)return false;
    if(success<=0)return true;
    if(active)return changedSpec||now-std::max(activeSince,success)>=CharacterSeconds;
    return lastSeen>success&&now-lastSeen>=LogoutGraceSeconds;
}
// Claims are made BEFORE dispatching a worker. They live until the final
// owner releases them, including cancellation/failed worker creation.
struct MetadataRegistry {
    using Clock=std::chrono::steady_clock;
    struct Entry{bool pending=false;Clock::time_point retry{};};
    std::mutex mutex;
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
    for(auto id:ids){
        if(!id)continue;
        auto& e=r.records[{dataset,id}];
        if(e.pending||(!manual&&now<e.retry))continue;
        batch->ids.push_back(id);e.pending=true;
    }
    return batch;
}
}
