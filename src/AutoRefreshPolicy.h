#pragma once
// Single, thread-safe policy for background/automatic API refresh requests.
// Manual refresh actions and the completion/retry phases of an already-started
// manual request remain independent of this option.
#include <atomic>
namespace AlterEgo::AutoRefreshPolicy {
inline std::atomic<bool>& Switch() {
    static std::atomic<bool> enabled{false}; // opt-in, including before settings load
    return enabled;
}
inline void SetEnabled(bool enabled) {
    Switch().store(enabled, std::memory_order_release);
}
inline bool Enabled() {
    return Switch().load(std::memory_order_acquire);
}
inline bool AllowRequest(bool userInitiated) {
    return userInitiated || Enabled();
}
} // namespace AlterEgo::AutoRefreshPolicy
