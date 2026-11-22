#pragma once
// Pure daily/weekly rollover policy, shared by Vault and Clears.
// Reset times and timestamps are UTC epoch seconds. No GW2/Windows deps.
#include <algorithm>
#include <cstdint>
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

// A held-over API result is retried with bounded, gradually slower polling.
inline int64_t RetryDelaySeconds(int attempted) {
    if (attempted <= 1) return 30;
    if (attempted == 2) return 60;
    if (attempted == 3) return 120;
    return 300;
}
} // namespace ClearsReset
