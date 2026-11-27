#pragma once
// Single item request owner; transient raw response evidence is scoped to
// overlapping consumers, not a second durable item catalog.
#include "PublicCatalogCache.h"
#include "RefreshEngine.h"
#include "BackgroundWorkers.h"
#include "UntrustedInput.h"
#include "HttpClient.h"
#include <condition_variable>
#include <functional>
#include <memory>
namespace AlterEgo::ItemMetadata {
using Json=nlohmann::json;
inline constexpr const char* Dataset="items";
enum class Demand { Details, Inventory, Discovery, InitialDiscovery };
struct Observation {
    uint32_t id=0;
    bool persist=false,aliases=false,upgrades=false,done=false;
    std::shared_ptr<const Json> raw;
    int64_t stamp=0;
};
struct Exchange {
    std::mutex mutex;
    std::condition_variable changed;
    std::map<uint32_t,std::shared_ptr<Observation>> active;
};
inline Exchange& State(){static Exchange s;return s;}
inline void MergeDemand(Observation& slot,Demand demand){
    slot.persist|=demand!=Demand::InitialDiscovery;
    slot.aliases|=demand==Demand::Details;
    slot.upgrades|=demand==Demand::InitialDiscovery;
}
struct Work {
    std::shared_ptr<RefreshEngine::IdBatch> claim;
    std::map<uint32_t,std::shared_ptr<Observation>> owned,wanted;
    // State mutex is the admission/exchange lock. Release the existing
    // broker claim while it is held, so a join never misses a pending result.
    void FinishLocked(const std::vector<uint32_t>& ids){
        auto& s=State();
        for(auto id:ids){
            auto it=owned.find(id);if(it==owned.end()||it->second->done)continue;
            it->second->done=true;
            auto active=s.active.find(id);
            if(active!=s.active.end()&&active->second==it->second)s.active.erase(active);
        }
        if(claim)claim->ReleaseIds(ids.begin(),ids.end());
        s.changed.notify_all();
    }
    ~Work(){
        if(!claim)return;
        auto& s=State();std::lock_guard<std::mutex> lock(s.mutex);
        FinishLocked(claim->ids);
    }
};
// Claims are acquired before dispatch. Only the existing per-ID registry
// admits network owners; this exchange retains raw responses for their joiners.
inline std::shared_ptr<Work> Admit(const std::vector<uint32_t>& ids,Demand demand,bool bypassCooldown,
                                   const std::function<bool(uint32_t)>& stillNeeded={}){
    // stillNeeded is an optional in-memory recheck, never a SQL/HTTP callback.
    // The active exchange lock closes the cache-check/admission race.
    auto job=std::make_shared<Work>();auto& s=State();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<uint32_t> candidates;
    for(auto id:std::set<uint32_t>(ids.begin(),ids.end()))if(id){
        const auto it=s.active.find(id);
        if(it!=s.active.end()){
            MergeDemand(*it->second,demand);job->wanted.emplace(id,it->second);
        }else if(!stillNeeded||stillNeeded(id))candidates.push_back(id);
    }
    // Still count joined IDs through the common registry's diagnostics.
    std::vector<uint32_t> claimIds=candidates;
    for(const auto& [id,slot]:job->wanted){(void)slot;claimIds.push_back(id);}
    job->claim=RefreshEngine::ClaimIds(Dataset,claimIds,bypassCooldown);
    for(auto id:job->claim->ids){
        auto slot=std::make_shared<Observation>();slot->id=id;MergeDemand(*slot,demand);
        job->owned.emplace(id,slot);job->wanted[id]=slot;s.active.emplace(id,std::move(slot));
    }
    return job;
}
inline Json Validate(const std::vector<uint32_t>& ids,const Json& raw){
    if(!raw.is_array())throw std::runtime_error("Invalid item batch");
    const std::set<uint32_t> expected(ids.begin(),ids.end());
    std::set<uint32_t> seen;
    for(const auto& row:raw){
        const auto id=Cache::ReferenceId(row.at("id"));
        if(!id||!expected.count(id)||!seen.insert(id).second||row.at("name").get<std::string>().empty())
            throw std::runtime_error("Invalid item response identity");
    }
    return raw;
}
struct Result {Json rows=Json::array();Cache::ItemRequestStamps stamps;};
inline Result Await(const Work& job,std::chrono::milliseconds timeout=std::chrono::seconds(30)){
    auto& s=State();const auto until=std::chrono::steady_clock::now()+timeout;
    std::unique_lock<std::mutex> lock(s.mutex);
    const auto pending=[&]{
        for(const auto& [id,slot]:job.wanted){(void)id;if(!slot->done)return true;}return false;
    };
    while(pending()){
        if(Background::Workers().Stopping())throw Background::Cancelled{};
        if(std::chrono::steady_clock::now()>=until)break;
        s.changed.wait_until(lock,std::min(until,std::chrono::steady_clock::now()+std::chrono::milliseconds(50)));
    }
    if(Background::Workers().Stopping())throw Background::Cancelled{};
    Result out;
    for(const auto& [id,slot]:job.wanted)if(slot->done&&slot->raw){
        out.rows.push_back(*slot->raw);out.stamps.emplace(id,slot->stamp);
    }
    return out;
}
// Persist receives canonical full rows and the original request-start stamp;
// adopt receives only the durable winning rows. Both run under the existing
// item publication gate. No exchange or publication lock spans HTTP.
template<class Send,class Persist,class Adopt>
inline void Fetch(Work& job,Send&& send,Persist&& persist,Adopt&& adopt){
    if(!job.claim)return;
    const auto& ids=job.claim->ids;
    auto& state=State();
    constexpr size_t batchSize=RefreshEngine::PublicBulkIdLimit;
    for(size_t start=0;start<ids.size();start+=batchSize){
        Background::Checkpoint();
        const auto end=std::min(start+batchSize,ids.size());
        const std::vector<uint32_t> batch(ids.begin()+start,ids.begin()+end);
        std::string query;
        for(auto id:batch){if(!query.empty())query+=',';query+=std::to_string(id);}
        const auto stamp=Cache::ReferenceSnapshotTime();
        bool attempted=false;
        try{
            auto response=send("/v2/items?ids="+query);
            Background::Checkpoint();attempted=response.attempted;
            if(response.status_code<200||response.status_code>=300)throw std::runtime_error("Item HTTP failure");
            auto raw=Validate(batch,Untrusted::Parse(response.body,Untrusted::PublicApi));
            std::map<uint32_t,std::shared_ptr<const Json>> rows;
            for(auto& row:raw){const auto id=Cache::ReferenceId(row.at("id"));rows.emplace(id,std::make_shared<const Json>(std::move(row)));}
            std::lock_guard<std::mutex> publication(Cache::ItemPublicationMutex());
            Background::Checkpoint();
            std::set<uint32_t> persisted,aliased;
            std::map<uint32_t,Json> winners;
            // A detail/Inventory join can arrive during a bootstrap batch's
            // commit. Honor its full-persistence demand before releasing that
            // ID. Never persist all ordinary bootstrap rows just to join.
            for(;;){
                Json save=Json::array(),aliases=Json::array();
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    for(const auto& [id,row]:rows){
                        const auto& slot=*job.owned.at(id);
                        if(!persisted.count(id)&&(slot.persist||(slot.upgrades&&!PublicCatalog::Kind(*row).empty()))){
                            save.push_back(*row);persisted.insert(id);
                        }
                        const auto winner=winners.find(id);
                        if(slot.aliases&&winner!=winners.end()&&aliased.insert(id).second)aliases.push_back(winner->second);
                    }
                    if(save.empty()&&aliases.empty()){
                        for(const auto& [id,row]:rows){
                            auto& slot=*job.owned.at(id);slot.raw=row;slot.stamp=stamp;
                        }
                        if(attempted)job.claim->MarkAttempted(batch.begin(),batch.end());
                        job.FinishLocked(batch);break;
                    }
                }
                Background::Checkpoint();
                if(!save.empty()){
                    Json accepted=Json::array();
                    if(!persist(save,stamp,accepted))throw std::runtime_error("Item persistence failed");
                    adopt(accepted,false);
                    for(const auto& row:accepted)winners.emplace(Cache::ReferenceId(row.at("id")),row);
                }
                if(!aliases.empty())adopt(aliases,true);
            }
        }catch(const Background::Cancelled&){throw;}catch(...){
            Background::Checkpoint();
            std::lock_guard<std::mutex> lock(state.mutex);
            if(attempted)job.claim->MarkAttempted(batch.begin(),batch.end());
            job.FinishLocked(batch);
        }
        if(end<ids.size())Background::Pause(std::chrono::milliseconds(200));
    }
}
} // namespace AlterEgo::ItemMetadata
