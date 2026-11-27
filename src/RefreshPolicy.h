#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <set>
#include <vector>
#include <algorithm>
namespace AlterEgo::RefreshEngine {
// Ordinary wall-clock policy; specialized metadata/transport/reset backoffs
// retain their own owners and timing.
inline constexpr int64_t BrowseSeconds=1800, DefaultObjectiveSeconds=900, FailureSeconds=900;
inline constexpr int64_t CharacterSeconds=600, LogoutGraceSeconds=600, SkinOwnershipSeconds=900;
inline constexpr int64_t RosterSeconds=86400, SeasonSeconds=86400;
inline constexpr int64_t SpecializationDebounceSeconds=8, StaggerMaxSeconds=45;
namespace ScopeName {
inline constexpr char Core[]="Core", Builds[]="BuildTabs", Tabs[]="EquipmentTabs",
    Physical[]="PhysicalSupplement", Crafting[]="Crafting", Bank[]="Bank",
    Shared[]="Shared inventory", Materials[]="Materials", Armory[]="Legendary Armory",
    Bags[]="Bags", Equipment[]="Equipment", Roster[]="Roster";
}
inline bool SharedActivityStorage(const std::string& scope){
    using namespace ScopeName;
    return scope==Bank||scope==Shared||scope==Materials||scope==Armory;
}
inline bool ActivityInventoryScope(const std::string& scope){
    using namespace ScopeName;
    return scope==Bags||scope==Equipment||SharedActivityStorage(scope);
}
inline bool SharedAccountStorage(const std::string& scope){return SharedActivityStorage(scope)||scope=="Wallet"||scope=="Delivery box";}
inline constexpr const char* AccountStorageScopes[]={"Wallet",ScopeName::Bank,ScopeName::Shared,ScopeName::Materials,ScopeName::Armory,"Delivery box"};
inline int64_t ActivitySeconds(bool accountWide,const std::string& scope){
    return !accountWide||(SharedAccountStorage(scope)&&scope!=ScopeName::Armory)?CharacterSeconds:BrowseSeconds;
}
inline int64_t InventorySeconds(const std::string& scope){
    using namespace ScopeName;
    if(scope==Roster)return RosterSeconds;
    return ActivitySeconds(scope!=Bags&&scope!=Equipment,scope);
}
inline bool InventoryPermitted(const std::set<std::string>& permissions,const std::string& scope){
    using namespace ScopeName;
    return scope=="Wallet"?permissions.count("wallet"):
        scope=="Guild access"?permissions.count("guilds"):
        scope==Roster?permissions.count("characters"):
        scope=="Delivery box"?permissions.count("tradingpost"):
        permissions.count("inventories")&&((scope!=Bags&&scope!=Equipment)||permissions.count("characters"))&&(scope!=Armory||permissions.count("unlocks"));
}
// Zero means no unresolved attempt. Equality is a possible same-second
// failure; successful producers clear their attempt. A newer success wins.
inline int64_t RetryUntil(int64_t success,int64_t attempt,int64_t now,
                          int64_t retry=FailureSeconds,bool manual=false){
    if(manual||attempt<=0||attempt<success||attempt>now||retry<=0||now-attempt>=retry)return 0;
    return attempt>std::numeric_limits<int64_t>::max()-retry?
        std::numeric_limits<int64_t>::max():attempt+retry;
}
enum class WaitReason { Current, Due, ActiveInterval, Stagger, LogoutGrace, SpecDebounce, Retry, ResetGrace };
struct Eligibility { WaitReason reason=WaitReason::Current; int64_t at=0; bool due=false; };
inline bool TimedFresh(int64_t success,int64_t now,int64_t interval,int64_t minimumEpoch=0){
    return success>0&&success<=now&&success>=minimumEpoch&&now-success<interval;
}
inline int64_t AddSeconds(int64_t stamp,int64_t seconds){
    return seconds>0&&stamp>std::numeric_limits<int64_t>::max()-seconds?
        std::numeric_limits<int64_t>::max():stamp+seconds;
}
// The ordinary clock, optional minimum epoch and recurring offset are
// common to status, timed owners and account-wide active storage.
inline Eligibility TimedEligibility(int64_t success,int64_t attempt,int64_t now,int64_t interval,
    bool manual=false,int64_t minimumEpoch=0,int64_t offset=0,int64_t retrySeconds=FailureSeconds){
    if(manual)return {WaitReason::Due,now,true};
    if(const auto retry=RetryUntil(success,attempt,now,retrySeconds))return {WaitReason::Retry,retry,false};
    if(success<=0||success>now||success<minimumEpoch)return {WaitReason::Due,now,true};
    const auto at=AddSeconds(AddSeconds(success,interval),offset);
    if(now>=at)return {WaitReason::Due,now,true};
    return {TimedFresh(success,now,interval,minimumEpoch)?WaitReason::Current:WaitReason::Stagger,at,false};
}
// Login activates demand, never freshness. Overdue data at login skips
// ordinary staggering; specialization debounce is an independent earlier trigger.
inline Eligibility ActiveEligibility(int64_t success,int64_t attempt,int64_t now,
    int64_t login,int64_t interval,int64_t offset,int64_t specAt=0){
    const bool overdueAtLogin=login>0&&login<=now&&!TimedFresh(success,login,interval)&&success<=login;
    auto normal=TimedEligibility(success,0,now,interval,false,0,overdueAtLogin?0:offset);
    if(specAt>0&&!normal.due&&specAt<normal.at)
        normal={now>=specAt?WaitReason::Due:WaitReason::SpecDebounce,specAt,now>=specAt};
    if(const auto retry=RetryUntil(success,attempt,now))return {WaitReason::Retry,retry,false};
    return normal;
}
struct AccountStorageCandidate {
    std::string scope;
    bool allowed=false,seed=false,previouslyRetrieved=false;
    Eligibility eligibility;
};
// One visible, actually due seed is required. Existing cached peers may join
// within the ordinary stagger window, never during propagation or retry waits.
inline std::set<std::string> PlanAccountStorage(const std::vector<AccountStorageCandidate>& rows,int64_t now){
    std::set<std::string> out;
    const bool triggered=std::any_of(rows.begin(),rows.end(),[](const auto& r){
        return SharedAccountStorage(r.scope)&&r.allowed&&r.seed&&r.eligibility.due;
    });
    if(!triggered)return out;
    for(const auto& r:rows){
        if(!SharedAccountStorage(r.scope)||!r.allowed||(!r.seed&&!r.previouslyRetrieved))continue;
        const auto& q=r.eligibility;
        const bool nearDue=(q.reason==WaitReason::Current||q.reason==WaitReason::ActiveInterval||q.reason==WaitReason::Stagger)&&
            q.at>now&&q.at-now<=StaggerMaxSeconds;
        if(q.due||nearDue)out.insert(r.scope);
    }
    return out;
}
}
