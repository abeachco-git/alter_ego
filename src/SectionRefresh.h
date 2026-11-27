#pragma once
// Shared, testable rules for opportunistic refreshes.
// Period completion and staleness are separate: resets invalidate period
// caches before these checks, and manual Refresh always requests everything.
#include <algorithm>
#include <ctime>
#include "RefreshEngine.h"
#include "RefreshStatusCore.h"

namespace AlterEgo::SectionRefresh {
// Period completion is valid only for the current verified epoch. The
// caller retains ownership of reset evidence and monotonic retry clocks.
inline RefreshStatus::Scope Inspect(int64_t success,int64_t now,int64_t interval,int64_t offset,
    int64_t minimumEpoch,int64_t resetReadyAt,bool completed,bool retryPending,
    int64_t retryRemaining,bool automatic,bool manual=false,bool blocked=false){
    namespace F=RefreshEngine;
    RefreshStatus::Scope out;out.success=success;out.automatic=automatic;
    const bool verified=success>0&&success<=now&&success>=minimumEpoch;
    out.periodComplete=verified&&completed&&!retryPending&&!blocked&&now>=resetReadyAt;
    out.fresh=out.periodComplete||(!retryPending&&!blocked&&now>=resetReadyAt&&F::TimedFresh(success,now,interval,minimumEpoch));
    if(blocked||now<resetReadyAt){out.eligibility={F::WaitReason::ResetGrace,resetReadyAt,false};return out;}
    if(!manual&&retryRemaining>0){out.eligibility={F::WaitReason::Retry,now+retryRemaining,false};return out;}
    if(manual||retryPending){out.eligibility={F::WaitReason::Due,now,true};return out;}
    if(out.periodComplete)return out;
    out.eligibility=F::TimedEligibility(success,0,now,interval,false,minimumEpoch,offset);
    return out;
}
template<class Period>
bool VaultClaimed(const Period& period) {
    if (period.objectives.empty()) return false;
    for (const auto& objective : period.objectives)
        if (!objective.claimed) return false;
    // An available meta reward must also have been claimed.
    return period.meta_reward_astral <= 0 || period.meta_reward_claimed;
}
template<class EntryContainer>
bool ClearsDone(const EntryContainer& entries) {
    return !entries.empty() &&
        std::all_of(entries.begin(), entries.end(),
                    [](const auto& entry) { return entry.done; });
}
} // namespace AlterEgo::SectionRefresh
