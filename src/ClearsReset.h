#pragma once
// Pure daily/weekly rollover policy, shared by Vault and Clears.
// Reset times and timestamps are UTC epoch seconds. No GW2/Windows deps.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
namespace ClearsReset {
inline constexpr int64_t kResetBufferSeconds = 60;
inline constexpr const char* NoCacheHeaders="Cache-Control: no-cache\r\nPragma: no-cache\r\n";
inline int64_t RequestToken(){
    static std::atomic<int64_t> last{0};
    const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto old=last.load(std::memory_order_relaxed);
    for(;;){const auto next=std::max<int64_t>(wall,old+1);
        if(last.compare_exchange_weak(old,next,std::memory_order_relaxed))return next;}
}
inline std::string FreshPath(std::string path,int64_t token){
    if(token>0)path+=(path.find('?')==std::string::npos?"?":"&")+std::string("ae_refresh=")+std::to_string(token);
    return path;
}

inline bool GracePassed(int64_t reset,int64_t now){return now>=reset+kResetBufferSeconds;}
inline bool Due(int64_t lastHandledReset, int64_t currentReset, int64_t now) {
    return currentReset > lastHandledReset &&
           GracePassed(currentReset,now);
}

// At startup inside the grace minute, retain the previous-day marker so the
// first reset is not silently consumed before the 60-second API buffer passes.
inline int64_t InitialMarker(int64_t currentReset, int64_t now,
                             int64_t periodSeconds) {
    return now < currentReset + kResetBufferSeconds
        ? currentReset - periodSeconds : currentReset;
}

// A reset/attempt-specific URL avoids a previously cached 1-hour GW2 API
// category response. The extra parameter is accepted by the public endpoint.
// Keep retries distinct: a single per-day URL could itself cache a stale
// response for the next hour if first queried before backend rollover.
inline std::string FreshCategoryUrl(uint32_t categoryId, int64_t requestToken) {
    std::string url = "https://api.guildwars2.com/v2/achievements/categories/" +
                      std::to_string(categoryId);
    return FreshPath(std::move(url),requestToken);
}

// Explicit modern schema (including object-shaped achievement IDs) on the
// alternate bulk endpoint. Do not assume an arbitrary cache-buster changes
// the upstream cache key. Both responses must still pass rollover validation.
inline std::string AlternateCategoryUrl(uint32_t categoryId,int64_t requestToken) {
    return "https://api.guildwars2.com/v2/achievements/categories?ids="+
        std::to_string(categoryId)+"&v=2022-03-23T19%3A00%3A00.000Z&lang=en&ae_refresh="+
        std::to_string(requestToken);
}

// Accept only a complete, unambiguous category response. The old schema
// returns numbers; the modern schema returns objects with an id field.
template<class Json>
std::vector<uint32_t> CategoryIds(const Json& response,uint32_t expectedCategory) {
    const Json* obj=&response;
    if(response.is_array()){
        if(response.size()!=1)return {};
        obj=&response[0];
    }
    if(!obj->is_object() || !obj->contains("id") ||
       !(*obj)["id"].is_number_integer() ||
       (*obj)["id"].template get<int64_t>()!=expectedCategory ||
       !obj->contains("achievements") || !(*obj)["achievements"].is_array())
        return {};
    std::vector<uint32_t> ids;
    for(const auto& item:(*obj)["achievements"]){
        const auto& value=item.is_object()&&item.contains("id")?item["id"]:item;
        if(!value.is_number_integer())return {};
        const auto id=value.template get<int64_t>();
        if(id<=0 || id>UINT32_MAX)return {};
        // Access variants can repeat an achievement ID.
        const auto n=static_cast<uint32_t>(id);
        if(std::find(ids.begin(),ids.end(),n)==ids.end())ids.push_back(n);
    }
    return ids;
}

inline std::vector<uint32_t> SortedIds(std::vector<uint32_t> ids) {
    std::sort(ids.begin(), ids.end());
    return ids;
}

inline bool LooksUnrotated(const std::vector<uint32_t>& before,
                          const std::vector<uint32_t>& after) {
    // A missing new list is never a confirmed update. Without a baseline,
    // no comparison can prove or disprove a new daily rotation.
    return after.empty() || (!before.empty() &&
           SortedIds(before) == SortedIds(after));
}

// Daily, weekly and seasonal objectives may legitimately reuse IDs.
// Only structural rotation or a completed-to-incomplete / claimed-to-unclaimed
// transition is reset evidence. Forward progress can be yesterday's late update.
// A single changed objective is enough; order, reward amount and target changes
// alone are not evidence. The caller has already validated the API payload.
template<class Period>
bool LooksUnrotatedVaultPeriod(const Period& previous, const Period& current) {
    if (current.objectives.empty()) return true;
    if (previous.objectives.empty()) return false; // no comparison baseline
    if (previous.objectives.size() != current.objectives.size()) return false;
    const auto done=[](int32_t progress,int32_t target){return target>0&&progress>=target;};
    if (previous.meta_reward_claimed && !current.meta_reward_claimed) return false;
    if (done(previous.meta_progress_current,previous.meta_progress_complete) &&
        !done(current.meta_progress_current,current.meta_progress_complete)) return false;
    for (const auto& old : previous.objectives) {
        const auto next=std::find_if(current.objectives.begin(),current.objectives.end(),
            [&](const auto& o){return o.id==old.id;});
        if (next==current.objectives.end()) return false;
        if (old.claimed && !next->claimed) return false;
        if (done(old.progress_current,old.progress_complete) &&
            !done(next->progress_current,next->progress_complete)) return false;
    }
    return true;
}

// An identical all-zero period is ambiguous: the API has no period ID.
 // After the propagation window and repeated fresh requests, allow a reset-shaped
 // response. Never use this escape hatch for yesterday's claimed/progressed rows.
template<class Period> bool ResetShaped(const Period& period){
    if(period.objectives.empty()||period.meta_progress_current!=0||period.meta_reward_claimed)return false;
    for(const auto& o:period.objectives)if(o.claimed||o.progress_current!=0)return false;
    return true;
}
template<class Period> bool HoldVault(const Period& prior,const Period& current,int64_t elapsed,int failures,bool allowEmpty=false){
    if(current.objectives.empty())return !allowEmpty;
    if(!LooksUnrotatedVaultPeriod(prior,current))return false;
    return !(elapsed>=900&&failures>=3&&ResetShaped(current));
}
// Weekly raid/strike definitions may retain IDs indefinitely. A matching
// nonzero prior completion fingerprint is not proof of reset. Unlike Vault,
// the account achievement endpoint omits entries with no progress.
template<class Entries> bool HoldClearProgress(const Entries& prior,const Entries& current){
    if(prior.empty()||prior.size()!=current.size())return false;
    bool progressed=false;
    for(const auto& old:prior){
        progressed=progressed||old.done||old.current>0||std::any_of(old.bitDone.begin(),old.bitDone.end(),[](bool b){return b;});
        const auto it=std::find_if(current.begin(),current.end(),[&](const auto& e){return e.id==old.id;});
        if(it==current.end()||it->done!=old.done||it->current!=old.current||it->bitDone!=old.bitDone)return false;
    }
    return progressed;
}
// A held-over API result is retried with bounded, gradually slower polling.
inline int64_t RetryDelaySeconds(int attempted) {
    if (attempted <= 1) return 30;
    if (attempted == 2) return 60;
    if (attempted == 3) return 120;
    return 300;
}
// Same retry state for each independently published section. Caller owns its
// domain mutex; steady time is milliseconds, never wall-clock time.
struct RetryState {
    int failures=0;int64_t at=0;std::string reason;
    bool Ready(int64_t now,bool manual=false)const{return manual||at==0||now>=at;}
    void Reset(){failures=0;at=0;reason.clear();}
    void Fail(int64_t now,std::string why){
        if(failures<4)++failures;
        at=now+RetryDelaySeconds(failures)*1000;reason=std::move(why);
    }
    int64_t Remaining(int64_t now)const{return std::max<int64_t>(0,(at-now+999)/1000);}
};
} // namespace ClearsReset
