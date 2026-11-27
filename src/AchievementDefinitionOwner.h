#pragma once
// Public definitions only; private progress and rotating membership retain
// their independent owners. Configure once after loading the existing cache.
#include "AchievementCatalogCache.h"
#include "AchievementNamesCore.h"
#include "BackgroundWorkers.h"
#include "HttpClient.h"
#include "RefreshEngine.h"
#include "UntrustedInput.h"
#include <ctime>
#include <functional>
#include <limits>
#include <unordered_map>
namespace AlterEgo::AchievementDefinitions {
using Json=nlohmann::json;
struct Bit {
    std::string type;
    uint32_t item_id=0;
    std::string text;
};
struct Definition {
    uint32_t id=0;
    std::string name,description,requirement,icon_url,type;
    std::vector<std::string> flags;
    int max_count=0,total_ap=0;
    std::vector<Bit> bits;
    std::vector<uint32_t> prerequisites;
};
using Ptr=std::shared_ptr<const Definition>;
inline int Nonnegative(const Json& value){
    if(!value.is_number_integer()||(!value.is_number_unsigned()&&value.get<int64_t>()<0)||
       value.get<uint64_t>()>static_cast<uint64_t>((std::numeric_limits<int>::max)()))
        throw std::runtime_error("Invalid achievement tier");
    return value.get<int>();
}
inline Definition ParseApi(const Json& row){
    Definition d;
    d.id=AchievementNames::Id(row.at("id"));d.name=row.at("name").get<std::string>();
    if(d.name.empty())throw std::runtime_error("Empty achievement name");
    d.description=row.at("description").get<std::string>();d.requirement=row.at("requirement").get<std::string>();
    d.icon_url=row.value("icon","");d.type=row.at("type").get<std::string>();
    if(d.type.empty()||!row.at("flags").is_array()||!row.at("tiers").is_array())
        throw std::runtime_error("Incomplete achievement definition");
    for(const auto& flag:row.at("flags"))d.flags.push_back(flag.get<std::string>());
    if(row.contains("prerequisites")){
        if(!row.at("prerequisites").is_array())throw std::runtime_error("Invalid achievement prerequisites");
        for(const auto& id:row.at("prerequisites"))d.prerequisites.push_back(AchievementNames::Id(id));
    }
    if(row.contains("bits")){
        if(!row.at("bits").is_array())throw std::runtime_error("Invalid achievement bits");
        for(const auto& bit:row.at("bits")){
            Bit b;b.type=bit.at("type").get<std::string>();
            if(b.type.empty())throw std::runtime_error("Invalid achievement bit");
            if(bit.contains("id"))b.item_id=AchievementNames::Id(bit.at("id"));
            b.text=bit.value("text","");d.bits.push_back(std::move(b));
        }
    }
    for(const auto& tier:row.at("tiers")){
        const auto count=Nonnegative(tier.at("count")),points=Nonnegative(tier.at("points"));
        if(count<d.max_count||points>(std::numeric_limits<int>::max)()-d.total_ap)
            throw std::runtime_error("Invalid achievement tiers");
        d.max_count=count;d.total_ap+=points;
    }
    return d;
}
inline Json Encode(const Definition& d){
    Json bits=Json::array();
    for(const auto& bit:d.bits)bits.push_back({{"type",bit.type},{"item_id",bit.item_id},{"text",bit.text}});
    return {{"id",d.id},{"name",d.name},{"description",d.description},{"requirement",d.requirement},
        {"icon_url",d.icon_url},{"type",d.type},{"flags",d.flags},{"max_count",d.max_count},
        {"total_ap",d.total_ap},{"prerequisites",d.prerequisites},{"bits",std::move(bits)}};
}
struct Record{Ptr def;int64_t stamp=0;};
using Plan=std::map<uint32_t,int64_t>;
struct State{
    std::mutex mutex,publication;
    std::string root;
    std::unordered_map<uint32_t,Record> records;
    std::function<void(const Ptr&)> publish;
};
inline State& Owner(){static State s;return s;}
inline void Configure(const std::string& root,const std::unordered_map<uint32_t,Ptr>& initial,
                      std::function<void(const Ptr&)> publish){
    auto& s=Owner();std::map<uint32_t,int64_t> stamps;
    try{stamps=Cache::ReadAchievementWatermarks(root);}catch(...){}
    std::lock_guard<std::mutex> lock(s.mutex);
    s.root=root;s.publish=std::move(publish);s.records.clear();
    for(const auto& [id,def]:initial)if(id&&def)s.records.emplace(id,Record{def,stamps.count(id)?stamps.at(id):0});
}
inline Plan MakePlan(const std::vector<uint32_t>& ids,bool force,bool revalidateStale){
    Plan out;auto& s=Owner();const auto now=std::time(nullptr);
    std::lock_guard<std::mutex> lock(s.mutex);
    for(auto id:ids)if(id){
        const auto it=s.records.find(id);
        const auto stamp=it==s.records.end()?0:it->second.stamp;
        if(force||it==s.records.end()||(revalidateStale&&!AchievementNames::DefinitionFresh(stamp,now)))
            out.emplace(id,stamp);
    }
    return out;
}
inline std::vector<uint32_t> Ids(const Plan& plan){
    std::vector<uint32_t> ids;ids.reserve(plan.size());
    for(const auto& [id,stamp]:plan){(void)stamp;ids.push_back(id);}
    return ids;
}
inline bool CoveredLocked(const State& s,uint32_t id,int64_t baseline){
    const auto it=s.records.find(id);return it!=s.records.end()&&it->second.stamp>baseline;
}
inline bool Pending(const std::vector<uint32_t>& ids){
    for(auto id:ids)if(RefreshEngine::InspectId(Cache::AchievementDefinitionWatermark,id).pending)return true;
    return false;
}
// Every actual producer shares the per-ID claim and durable publication path.
inline void Fetch(const Plan& plan,const std::shared_ptr<RefreshEngine::IdBatch>& claim){
    auto& s=Owner();
    for(size_t start=0;start<claim->ids.size();start+=AchievementNames::BatchSize){
        Background::Checkpoint();
        const auto end=std::min(start+AchievementNames::BatchSize,claim->ids.size());
        std::vector<uint32_t> batch,covered;
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            for(size_t i=start;i<end;++i){
                const auto id=claim->ids[i];
                (CoveredLocked(s,id,plan.at(id))?covered:batch).push_back(id);
            }
        }
        claim->ReleaseIds(covered.begin(),covered.end(),true);
        if(batch.empty())continue;
        std::string idStr;
        for(auto id:batch){if(!idStr.empty())idStr+=",";idStr+=std::to_string(id);}
        const auto stamp=Cache::ReferenceSnapshotTime();
        const auto reply=Skinventory::HttpClient::GetEx("https://api.guildwars2.com/v2/achievements?ids="+idStr);
        Background::Checkpoint();
        bool committed=false;
        try{
            if(reply.status_code!=200)throw std::runtime_error("Achievement HTTP failure");
            const auto raw=Untrusted::Parse(reply.body,Untrusted::PublicApi);
            AchievementNames::ValidateBatch(batch,raw);
            Json rows=Json::object();
            std::unordered_map<uint32_t,Ptr> parsed;
            for(const auto& row:raw){
                auto d=std::make_shared<const Definition>(ParseApi(row));
                rows[std::to_string(d->id)]=Encode(*d);parsed.emplace(d->id,d);
            }
            std::lock_guard<std::mutex> publication(s.publication);
            Background::Checkpoint();
            Json accepted=Json::object();
            if(Cache::WriteAchievementObservations(s.root,rows,stamp,accepted)){
                for(const auto& [key,row]:accepted.items()){
                    (void)key;const auto id=AchievementNames::Id(row.at("id"));const auto def=parsed.at(id);
                    {std::lock_guard<std::mutex> lock(s.mutex);s.records[id]={def,stamp};}
                    if(s.publish)s.publish(def);
                }
                committed=true;
            }
        }catch(const Background::Cancelled&){throw;}catch(...){}
        Background::Checkpoint();
        // Only an actual failed transport attempt imposes ordinary cooldown.
        if(!committed&&reply.attempted)claim->MarkAttempted(batch.begin(),batch.end());
        claim->ReleaseIds(batch.begin(),batch.end(),committed);
        if(end<claim->ids.size())Background::Pause(std::chrono::milliseconds(200));
    }
}
inline void Request(const std::vector<uint32_t>& ids,bool manual,bool revalidateStale,ApiActivity::Reason reason){
    const auto plan=MakePlan(ids,false,revalidateStale);
    auto claim=RefreshEngine::ClaimIds(Cache::AchievementDefinitionWatermark,Ids(plan),manual);
    if(claim->ids.empty())return;
    Background::Workers().Launch([plan,claim,reason]{ApiActivity::ReasonScope scope(reason);Fetch(plan,claim);});
}
// Synchronous consumers join overlapping requests. A forced observation must
// be newer than the admission baseline; a cached definition cannot prove rollover.
inline std::vector<Ptr> Acquire(const std::vector<uint32_t>& ids,bool force,bool bypassCooldown){
    const auto plan=MakePlan(ids,force,true);
    const auto wanted=Ids(plan);
    auto claim=RefreshEngine::ClaimIds(Cache::AchievementDefinitionWatermark,wanted,bypassCooldown);
    if(!claim->ids.empty())Fetch(plan,claim);
    if(!RefreshEngine::WaitForIds(Cache::AchievementDefinitionWatermark,wanted,[]{return Background::Workers().Stopping();}))
        return {};
    Background::Checkpoint();
    auto& s=Owner();std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<Ptr> out;out.reserve(ids.size());
    for(auto id:ids){
        const auto it=s.records.find(id);const auto baseline=plan.find(id);
        if(it==s.records.end()||(baseline!=plan.end()&&!CoveredLocked(s,id,baseline->second)))return {};
        out.push_back(it->second.def);
    }
    return out;
}
} // namespace AlterEgo::AchievementDefinitions
