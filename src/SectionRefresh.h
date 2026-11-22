#pragma once
// Shared, testable rules for opportunistic refreshes.
// Period completion and staleness are separate: resets invalidate period
// caches before these checks, and manual Refresh always requests everything.
#include <algorithm>
#include <ctime>

namespace AlterEgo::SectionRefresh {
inline constexpr std::time_t kRefreshIntervalSeconds = 15 * 60;
inline bool Stale(std::time_t lastSuccess, std::time_t now) {
    return lastSuccess <= 0 ||
           (now > lastSuccess && now - lastSuccess > kRefreshIntervalSeconds);
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
