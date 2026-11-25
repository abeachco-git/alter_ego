#include "../src/AutoRefreshCore.h"
#include <chrono>
#include <cassert>
#include <string>
int case_auto_refresh_tracker(int, char**) {
    // MumbleLink-only auto-refresh scheduling, independent of GW2 or Nexus.
    using Clock = AlterEgo::AutoRefreshTracker::Clock;
    const auto t0 = Clock::time_point{} + std::chrono::seconds(100);
    AlterEgo::AutoRefreshTracker tracker;
    std::string requestChar;
    auto change = tracker.ObserveIdentity("Nuly", 70, t0, true, true, true);
    assert(change.login && !change.specializationChanged);
    assert(tracker.LiveSpecializationDiffers("Nuly", 71));
    assert(!tracker.LiveSpecializationDiffers("Nuly", 70));
    assert(!tracker.LiveSpecializationDiffers("Other", 71));
    assert(!tracker.LiveSpecializationDiffers("Nuly", 0));
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(7), true, true, false, requestChar));
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(8), true, false, false, requestChar));
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(8), true, true, true, requestChar));
    assert(tracker.TakeDueRequest(t0 + std::chrono::seconds(8), true, true, false, requestChar));
    assert(requestChar == "Nuly");
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(12), true, true, false, requestChar));

    change = tracker.ObserveIdentity("Nuly", 70, t0 + std::chrono::seconds(15), true, true, true);
    assert(!change.login && !change.specializationChanged && tracker.PendingCharacter().empty());
    change = tracker.ObserveIdentity("Nuly", 0, t0 + std::chrono::seconds(18), true, true, true);
    assert(!change.specializationChanged);
    change = tracker.ObserveIdentity("Nuly", 91, t0 + std::chrono::seconds(20), true, true, true);
    assert(!change.login && change.specializationChanged);
    change = tracker.ObserveIdentity("Nuly", 92, t0 + std::chrono::seconds(22), true, true, true);
    assert(change.specializationChanged); // debounce and coalesce rapid switches
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(29), true, true, false, requestChar));
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(30), true, true, false, requestChar));
    assert(tracker.TakeDueRequest(t0 + std::chrono::seconds(98), true, true, false, requestChar));
    assert(requestChar == "Nuly");

    // Disabling auto refresh cancels a queued request.
    tracker.ObserveIdentity("Nuly", 93, t0 + std::chrono::seconds(101), true, true, true);
    assert(!tracker.PendingCharacter().empty());
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(120), false, true, false, requestChar));
    assert(tracker.PendingCharacter().empty());

    // Same-name logout/relogin is observed only after an extended blank identity.
    tracker.ObserveIdentity("", 0, t0 + std::chrono::seconds(130), true, true, true);
    change = tracker.ObserveIdentity("Nuly", 93, t0 + std::chrono::seconds(132), true, true, true);
    assert(!change.login); // transient loading blank
    tracker.ObserveIdentity("", 0, t0 + std::chrono::seconds(140), true, true, true);
    change = tracker.ObserveIdentity("Nuly", 93, t0 + std::chrono::seconds(153), true, true, true);
    assert(change.login);
    assert(tracker.PendingCharacter() == "Nuly");

    // Switching characters discards the previous character's pending work.
    change = tracker.ObserveIdentity("Nul Sul", 202, t0 + std::chrono::seconds(154), true, true, true);
    assert(change.login && tracker.PendingCharacter() == "Nul Sul");
    assert(tracker.LiveSpecializationDiffers("Nul Sul", 201));
    assert(!tracker.LiveSpecializationDiffers("Nuly", 201));

    // Optional settings: an off switch still tracks live specializations.
    AlterEgo::AutoRefreshTracker disabled;
    disabled.ObserveIdentity("Nuly", 70, t0, false, true, true);
    assert(disabled.PendingCharacter().empty());
    change = disabled.ObserveIdentity("Nuly", 71, t0 + std::chrono::seconds(9), false, true, true);
    assert(change.specializationChanged);
    assert(disabled.LiveSpecializationDiffers("Nuly", 70));
    return 0;
}
