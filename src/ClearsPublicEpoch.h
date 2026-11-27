#pragma once
// Session-only, public Clears evidence. No account IDs, progress, storage,
// HTTP, UI or Vault dependencies. A reset timestamp alone is not evidence.
#include "ClearsReset.h"
#include <array>
#include <map>
#include <mutex>
#include <optional>
#include <set>

namespace ClearsPublicEpoch {
using Ids=std::vector<uint32_t>;
using Baselines=std::vector<Ids>;
inline constexpr int64_t Day=86400,Week=7*Day;
inline constexpr uint32_t WeeklyStrikeId=9125;
inline int64_t Period(size_t category){return category<2?Day:Week;}
inline bool InWindow(int64_t epoch,int64_t period,int64_t started,int64_t now){
    return epoch>0&&started>=epoch+ClearsReset::kResetBufferSeconds&&now>=started&&now<epoch+period;
}
inline bool ValidIds(const Ids& ids){
    return !ids.empty()&&std::none_of(ids.begin(),ids.end(),[](uint32_t id){return !id;})&&
        std::set<uint32_t>(ids.begin(),ids.end()).size()==ids.size();
}
inline bool Same(const Ids& a,const Ids& b){return ClearsReset::SortedIds(a)==ClearsReset::SortedIds(b);}
inline void AddBaseline(Baselines& baselines,const Ids& ids){
    if(!ValidIds(ids))return;
    if(std::none_of(baselines.begin(),baselines.end(),[&](const auto& old){return Same(old,ids);}))
        baselines.push_back(ClearsReset::SortedIds(ids));
}
inline bool Held(const Baselines& baselines,const Ids& ids){
    return !ValidIds(ids)||std::any_of(baselines.begin(),baselines.end(),
        [&](const auto& old){return ClearsReset::LooksUnrotated(old,ids);});
}
struct Category {
    int64_t epoch=0,started=0,token=0;
    Ids ids;
    // The actual preceding-day fingerprint which this observation differed
    // from. Empty means no baseline was available; it is NOT rollover proof.
    Ids proofPrior;
};
inline bool CategoryUsable(const Category& value,size_t category,int64_t epoch,int64_t now,
                           const Baselines& baselines,bool reuse,bool verifiedBaseline=false){
    if(value.epoch!=epoch||value.token<=0||!InWindow(epoch,Period(category),value.started,now)||
       Held(baselines,value.ids))return false;
    if(category>=2)return true; // weekly IDs may be unchanged; progress is separate
    if(!value.proofPrior.empty()&&ClearsReset::LooksUnrotated(value.proofPrior,value.ids))return false;
    // No-baseline cold start retains the old live-fetch behavior, but another
    // account cannot use this observation as proven rotation without evidence.
    return !reuse||!value.proofPrior.empty()||verifiedBaseline;
}
struct Definition {
    uint32_t id=0;
    std::string name;
    int32_t max=0;
    std::vector<std::string> bitNames;
};
struct Section {
    int64_t epoch=0,started=0,token=0;
    Ids categoryIds,wanted;
    std::vector<Definition> definitions;
};
inline bool ExactDefinitions(const Ids& wanted,const std::vector<Definition>& definitions){
    Ids found;
    for(const auto& d:definitions){if(!d.id||d.name.empty()||d.max<0)return false;found.push_back(d.id);}
    return ValidIds(wanted)&&ValidIds(found)&&Same(wanted,found);
}
inline bool SectionUsable(const Section& value,size_t section,const Category& category,
                          const Ids& wanted,int64_t now){
    const auto categoryIndex=section<2?section:2;
    if(!CategoryUsable(category,categoryIndex,category.epoch,now,{},false))return false;
    Ids expected;
    for(auto id:category.ids)if(section<2||(id==WeeklyStrikeId)==(section==3))expected.push_back(id);
    return value.epoch==category.epoch&&value.token>0&&
        InWindow(value.epoch,Period(categoryIndex),value.started,now)&&
        Same(value.categoryIds,category.ids)&&Same(value.wanted,wanted)&&Same(wanted,expected)&&
        ExactDefinitions(wanted,value.definitions)&&
        (section!=2||std::any_of(value.definitions.begin(),value.definitions.end(),
            [](const auto& d){return !d.bitNames.empty();}));
}
class Cache {
    mutable std::mutex mutex_;
    std::array<std::map<int64_t,Category>,3> categories_;
    std::array<std::map<int64_t,Section>,4> sections_;
    // Existing verified account snapshots can contribute public IDs as
    // previous-day comparison hints, never current category evidence.
    std::array<std::map<int64_t,std::set<Ids>>,2> hints_;
    template<class T> static std::optional<T> Find(const std::map<int64_t,T>& records,int64_t epoch){
        auto it=records.find(epoch);return it==records.end()?std::nullopt:std::optional<T>(it->second);
    }
    template<class T> static bool Put(std::map<int64_t,T>& records,T value){
        auto it=records.find(value.epoch);
        if(it!=records.end()&&it->second.token>=value.token)return false;
        records[value.epoch]=std::move(value);
        while(records.size()>2)records.erase(records.begin());
        return true;
    }
    Baselines VerifiedPriorLocked(size_t category,int64_t epoch)const{
        Baselines out;
        if(category>=2)return out;
        const auto& records=categories_.at(category);
        if(auto current=Find(records,epoch))AddBaseline(out,current->proofPrior);
        if(auto previous=Find(records,epoch-Day))AddBaseline(out,previous->ids);
        auto hints=hints_.at(category).find(epoch-Day);
        if(hints!=hints_.at(category).end())for(const auto& ids:hints->second)AddBaseline(out,ids);
        return out;
    }
public:
    void Reset(){
        std::lock_guard<std::mutex> lock(mutex_);categories_={};sections_={};hints_={};
    }
    void RememberDailyFingerprint(size_t category,int64_t epoch,const Ids& ids){
        if(category>=2||epoch<=0||!ValidIds(ids))return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto& records=hints_.at(category);
        records[epoch].insert(ClearsReset::SortedIds(ids));
        while(records.size()>2)records.erase(records.begin());
    }
    std::optional<Category> GetCategory(size_t category,int64_t epoch)const{
        std::lock_guard<std::mutex> lock(mutex_);return Find(categories_.at(category),epoch);
    }
    Baselines DailyBaselines(size_t category,int64_t epoch,const Ids& accountPrior)const{
        std::lock_guard<std::mutex> lock(mutex_);
        auto out=VerifiedPriorLocked(category,epoch);
        if(category<2)AddBaseline(out,accountPrior);
        return out;
    }
    // The existing account fingerprint always vetoes an unchanged result.
    // Only evidence explicitly associated with the immediately preceding
    // epoch qualifies cross-account daily reuse (not an older held baseline).
    std::optional<Category> ReuseCategory(size_t category,int64_t epoch,int64_t now,
                                          const Ids& accountPrior,int64_t newerThan=0){
        std::lock_guard<std::mutex> lock(mutex_);
        auto& records=categories_.at(category);
        auto it=records.find(epoch);
        if(it==records.end()||it->second.token<=newerThan)return {};
        const auto known=VerifiedPriorLocked(category,epoch);
        auto baselines=known;if(category<2)AddBaseline(baselines,accountPrior);
        if(!CategoryUsable(it->second,category,epoch,now,baselines,true,!known.empty()))return {};
        if(category<2&&it->second.proofPrior.empty()&&!known.empty())it->second.proofPrior=known.front();
        return it->second;
    }
    // Recheck the strongest public baseline inside the publication lock.
    bool StoreCategory(size_t category,Category value,const Ids& accountPrior,int64_t now){
        std::lock_guard<std::mutex> lock(mutex_);
        auto& records=categories_.at(category);
        const auto known=VerifiedPriorLocked(category,value.epoch);
        auto baselines=known;if(category<2)AddBaseline(baselines,accountPrior);
        if(!CategoryUsable(value,category,value.epoch,now,baselines,false))return false;
        if(category<2&&!known.empty())value.proofPrior=known.front();
        return Put(records,std::move(value));
    }
    std::optional<Section> GetSection(size_t section,int64_t epoch)const{
        std::lock_guard<std::mutex> lock(mutex_);return Find(sections_.at(section),epoch);
    }
    bool StoreSection(size_t section,Section value,const Category& category,int64_t now){
        if(!SectionUsable(value,section,category,value.wanted,now))return false;
        std::lock_guard<std::mutex> lock(mutex_);return Put(sections_.at(section),std::move(value));
    }
};
} // namespace ClearsPublicEpoch
