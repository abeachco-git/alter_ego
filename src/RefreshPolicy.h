#pragma once
#include <cstdint>
#include <limits>
#include <string>
namespace AlterEgo::RefreshEngine {
// Ordinary wall-clock policy; specialized metadata/transport/reset backoffs
// retain their own owners and timing.
inline constexpr int64_t BrowseSeconds=1800, DefaultObjectiveSeconds=900, FailureSeconds=900;
inline constexpr int64_t CharacterSeconds=600, LogoutGraceSeconds=600;
inline constexpr int64_t RosterSeconds=86400, SeasonSeconds=86400;
inline constexpr int64_t SpecializationDebounceSeconds=8, StaggerMaxSeconds=45;
namespace ScopeName {
inline constexpr char Core[]="Core", Builds[]="BuildTabs", Tabs[]="EquipmentTabs",
    Physical[]="PhysicalSupplement", Crafting[]="Crafting", Bank[]="Bank",
    Shared[]="Shared inventory", Materials[]="Materials", Armory[]="Legendary Armory",
    Bags[]="Bags", Equipment[]="Equipment", Roster[]="Roster";
}
inline bool ActivityInventoryScope(const std::string& scope){
    using namespace ScopeName;
    return scope==Bags||scope==Equipment||scope==Bank||scope==Shared||scope==Materials||scope==Armory;
}
inline int64_t ActivitySeconds(bool accountWide,const std::string& scope){
    using namespace ScopeName;
    return !accountWide||scope==Bank||scope==Shared||scope==Materials?CharacterSeconds:BrowseSeconds;
}
inline int64_t InventorySeconds(const std::string& scope){return scope==ScopeName::Roster?RosterSeconds:BrowseSeconds;}
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
inline Eligibility TimedEligibility(int64_t success,int64_t attempt,int64_t now,int64_t interval){
    if(const auto retry=RetryUntil(success,attempt,now))return {WaitReason::Retry,retry,false};
    if(success<=0||success>now||now-success>=interval)return {WaitReason::Due,now,true};
    return {};
}
}
