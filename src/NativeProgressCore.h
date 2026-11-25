#pragma once
#include "CacheDatabase.h"
#include "NativeInventoryCore.h"
#include <limits>
namespace AlterEgo::NativeProgress {
using Json=nlohmann::json;
inline int Number(const Json& row,const char* key){
    const auto& j=row.at(key);
    if(!j.is_number_integer())throw std::runtime_error("Invalid progress number");
    const auto n=j.get<int64_t>();if(n<0||n>(std::numeric_limits<int>::max)())throw std::runtime_error("Progress number outside range");
    return static_cast<int>(n);
}
inline Cache::AchievementSnapshot ParseAchievements(const Json& j,int64_t time){
    if(!j.is_array())throw std::runtime_error("Invalid achievements response");
    Cache::AchievementSnapshot out;out.fetchedAt=time;
    for(const auto& row:j){
        Cache::AchievementProgress p;p.id=NativeInventory::ItemId(row.at("id"));
        p.done=row.at("done").get<bool>();
        p.current=row.contains("current")?Number(row,"current"):0;
        p.max=row.contains("max")?Number(row,"max"):0;
        p.repeated=row.contains("repeated")?Number(row,"repeated"):0;
        p.unlocked=row.value("unlocked",true);
        if(row.contains("bits")){
            if(!row.at("bits").is_array())throw std::runtime_error("Invalid achievement bits");
            for(const auto& b:row.at("bits")){
                if(!b.is_number_integer())throw std::runtime_error("Invalid achievement bit");
                const auto n=b.get<int64_t>();if(n<0||n>UINT32_MAX)throw std::runtime_error("Invalid achievement bit");
                p.completed_bits.insert(static_cast<uint32_t>(n));
            }
        }
        if(!out.entries.emplace(p.id,std::move(p)).second)throw std::runtime_error("Duplicate achievement ID");
    }
    return out;
}
struct Objective {
    uint32_t id=0;std::string title,track;
    int32_t acclaim=0,progress_current=0,progress_complete=0;
    bool claimed=false;
    bool isDone() const {return progress_complete>0&&progress_current>=progress_complete;}
};
struct Period {
    int32_t meta_progress_current=0,meta_progress_complete=0,meta_reward_astral=0;
    bool meta_reward_claimed=false;std::vector<Objective> objectives;
};
inline Period ParseVault(const Json& j,bool special){
    Period out;
    if(!special){
        out.meta_progress_current=Number(j,"meta_progress_current");
        out.meta_progress_complete=Number(j,"meta_progress_complete");
        out.meta_reward_astral=Number(j,"meta_reward_astral");
        out.meta_reward_claimed=j.at("meta_reward_claimed").get<bool>();
    }
    const auto& objectives=j.at("objectives");
    if(!objectives.is_array()||(!special&&objectives.empty()))throw std::runtime_error("Invalid Vault objectives");
    std::set<uint32_t> seen;
    for(const auto& raw:objectives){
        Objective o;o.id=NativeInventory::ItemId(raw.at("id"));
        if(!seen.insert(o.id).second)throw std::runtime_error("Duplicate Vault objective");
        o.title=raw.at("title").get<std::string>();o.track=raw.at("track").get<std::string>();
        if(o.title.empty())throw std::runtime_error("Missing objective title");
        o.acclaim=Number(raw,"acclaim");o.progress_current=Number(raw,"progress_current");o.progress_complete=Number(raw,"progress_complete");
        o.claimed=raw.at("claimed").get<bool>();out.objectives.push_back(std::move(o));
    }
    return out;
}
}
