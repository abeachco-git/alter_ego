#pragma once
// Read-only Nexus MumbleLink identity tracker and request scheduler.
// This module performs no API calls; the UI drives it once per frame.
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace AlterEgo {
class AutoRefreshTracker {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    struct IdentityChange {
        bool login = false;
        bool specializationChanged = false;
    };

    // A short grace window avoids mistaking transient blank identity data
    // during a map transition for a logout and another login.
    IdentityChange ObserveIdentity(const std::string& name, uint32_t spec,
                                   TimePoint now, bool enabled,
                                   bool onLogin, bool onSpecChange) {
        IdentityChange change;
        if (name.empty()) {
            if (identityMissingSince_ == TimePoint{}) identityMissingSince_ = now;
            return change;
        }
        const bool wasLoggedOut = identityMissingSince_ != TimePoint{} &&
            now - identityMissingSince_ >= std::chrono::seconds(12);
        identityMissingSince_ = TimePoint{};
        if (name != currentCharacter_ || wasLoggedOut) {
            currentCharacter_ = name;
            currentSpecialization_ = spec;
            pendingCharacter_.clear(); // never refresh the character we left
            change.login = true;
            if (enabled && onLogin) Queue(name, now);
            return change;
        }
        // Ignore transient unknown (zero) specialization during loading.
        if (spec != 0) {
            change.specializationChanged =
                currentSpecialization_ != 0 && currentSpecialization_ != spec;
            currentSpecialization_ = spec;
            if (change.specializationChanged && enabled && onSpecChange)
                Queue(name, now);
        }
        return change;
    }

    bool LiveSpecializationDiffers(const std::string& name, uint32_t cachedSpec) const {
        return !name.empty() && name == currentCharacter_ &&
               currentSpecialization_ != 0 && cachedSpec != 0 &&
               currentSpecialization_ != cachedSpec;
    }
    uint32_t LiveSpecialization() const { return currentSpecialization_; }
    const std::string& CurrentCharacter() const { return currentCharacter_; }
    const std::string& PendingCharacter() const { return pendingCharacter_; }
    void CancelPending() { pendingCharacter_.clear(); }

    // Invoked by the render callback, not the Mumble identity callback.
    // Requests wait for H&S to be available and idle, and are rate-limited
    // per character. Multiple events coalesce into one request.
    bool TakeDueRequest(TimePoint now, bool enabled, bool hoardReady,
                        bool fetchBusy, std::string& character) {
        if (!enabled) {
            pendingCharacter_.clear();
            return false;
        }
        if (pendingCharacter_.empty() || pendingCharacter_ != currentCharacter_ ||
            now < due_ || !hoardReady || fetchBusy)
            return false;
        const auto it = lastSent_.find(pendingCharacter_);
        if (it != lastSent_.end() && now - it->second < std::chrono::seconds(90))
            return false;
        character = pendingCharacter_;
        pendingCharacter_.clear();
        lastSent_[character] = now;
        return true;
    }

private:
    void Queue(const std::string& name, TimePoint now) {
        pendingCharacter_ = name;
        due_ = now + std::chrono::seconds(8);
    }

    std::string currentCharacter_;
    uint32_t currentSpecialization_ = 0;
    TimePoint identityMissingSince_{};
    std::string pendingCharacter_;
    TimePoint due_{};
    std::unordered_map<std::string, TimePoint> lastSent_;
};
} // namespace AlterEgo
