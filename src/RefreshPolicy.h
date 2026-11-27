#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <set>
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
inline int64_t ActivitySeconds(bool accountWide,const std::string& scope){
    using namespace ScopeName;
    return !accountWide||scope==Bank||scope==Shared||scope==Materials?CharacterSeconds:BrowseSeconds;
}
inline int64_t InventorySeconds(const std::string& scope){return scope==ScopeName::Roster?RosterSeconds:BrowseSeconds;}
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
enum class WaitReason { Current, Due, LoginGrace, ActiveInterval, Stagger, LogoutGrace, SpecDebounce, Retry };
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
// A login may activate storage demand, not restart a stale snapshot's
// lifetime. If already overdue when logging in, skip recurring stagger.
inline Eligibility ActiveSharedStorageEligibility(int64_t success,int64_t attempt,int64_t now,
    int64_t login,int64_t interval,int64_t offset){
    const bool overdueAtLogin=login>0&&login<=now&&!TimedFresh(success,login,interval)&&success<=login;
    return TimedEligibility(success,attempt,now,interval,false,0,overdueAtLogin?0:offset);
}
}
