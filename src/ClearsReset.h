#pragma once
// Pure daily/weekly rollover policy, shared by Vault and Clears.
// Reset times and timestamps are UTC epoch seconds. No GW2/Windows deps.
#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
namespace ClearsReset {
inline constexpr int64_t kResetBufferSeconds = 60;

inline bool Due(int64_t lastHandledReset, int64_t currentReset, int64_t now) {
    return currentReset > lastHandledReset &&
           now >= currentReset + kResetBufferSeconds;
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
    if (requestToken > 0)
        url += "?ae_refresh=" + std::to_string(requestToken);
    return url;
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

// Weekly objectives can legitimately reuse IDs. Compare claimed state and
// progress as well as IDs: a reset progress counter confirms a fresh response,
// while an unchanged pre-reset snapshot is not evidence of a new week.
// The check is independent of ordering and handles incomplete API responses.
template<class Period>
bool LooksUnrotatedVaultWeek(const Period& previous, const Period& current) {
    if (current.objectives.empty()) return true;
    if (previous.objectives.empty()) return false; // no comparison baseline
    if (previous.meta_progress_current != current.meta_progress_current ||
        previous.meta_progress_complete != current.meta_progress_complete ||
        previous.meta_reward_astral != current.meta_reward_astral ||
        previous.meta_reward_claimed != current.meta_reward_claimed ||
        previous.objectives.size() != current.objectives.size()) return false;
    auto snapshot = [](const Period& period) {
        std::vector<std::tuple<uint32_t, int32_t, int32_t, bool>> rows;
        rows.reserve(period.objectives.size());
        for (const auto& o : period.objectives)
            rows.emplace_back(o.id, o.progress_current, o.progress_complete,
                              o.claimed);
        std::sort(rows.begin(), rows.end());
        return rows;
    };
    return snapshot(previous) == snapshot(current);
}

// A held-over API result is retried with bounded, gradually slower polling.
inline int64_t RetryDelaySeconds(int attempted) {
    if (attempted <= 1) return 30;
    if (attempted == 2) return 60;
    if (attempted == 3) return 120;
    return 300;
}
} // namespace ClearsReset
