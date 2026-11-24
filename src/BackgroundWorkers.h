#pragma once
#include <atomic>
#include <cstddef>
#include <list>
#include <mutex>
#include <thread>
#include <utility>

namespace AlterEgo::Background {

// Owner-thread lifecycle: Open on addon load, RequestStop before deregistration,
// JoinAll before destroying any worker-accessible state. Never join from a job.
struct Cancelled {};
class WorkerGroup {
    struct Entry {
        std::atomic<bool> done{false};
        std::thread thread;
    };
    std::mutex mutex_;
    std::list<Entry> jobs_;
    std::atomic<bool> stopping_{true};
    std::atomic<std::size_t> active_{0}, failures_{0};

    void ReapLocked() {
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            if (!it->done.load()) { ++it; continue; }
            it->thread.join();
            it = jobs_.erase(it);
        }
    }
public:
    bool Open() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!jobs_.empty() || active_.load() != 0) return false;
        stopping_ = false;
        return true;
    }
    bool Stopping() const noexcept { return stopping_.load(); }
    std::size_t ActiveCount() const noexcept { return active_.load(); }
    std::size_t FailureCount() const noexcept { return failures_.load(); }
    void Checkpoint() const { if (Stopping()) throw Cancelled{}; }
    void RequestStop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    template<class Job, class Failure>
    bool Launch(Job job, Failure failure) {
        // Failure runs without the registry lock, including rejection and
        // thread-creation failure. Domain owners restore their busy markers.
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stopping_) {
                ReapLocked();
                jobs_.emplace_back();
                auto* entry = &jobs_.back();
                ++active_;
                try {
                    entry->thread = std::thread([this, entry, job = std::move(job), failure]() mutable {
                        try { job(); }
                        catch (const Cancelled&) {
                            try { failure(); } catch (...) { ++failures_; }
                        }
                        catch (...) {
                            ++failures_;
                            try { failure(); } catch (...) { ++failures_; }
                        }
                        --active_;
                        entry->done = true;
                    });
                    return true;
                } catch (...) {
                    --active_;
                    jobs_.pop_back();
                    throw;
                }
            }
        } catch (...) { ++failures_; }
        try { failure(); } catch (...) { ++failures_; }
        return false;
    }
    template<class Job>
    bool Launch(Job job) { return Launch(std::move(job), [] {}); }

    // Only the lifecycle owner calls this, without any domain locks. Moving
    // nodes preserves references held by executing workers.
    void JoinAll() {
        std::list<Entry> joining;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            joining.splice(joining.end(), jobs_);
        }
        for (auto& entry : joining) entry.thread.join();
    }
};

inline WorkerGroup& Workers() {
    static WorkerGroup group;
    return group;
}
inline void Checkpoint() { Workers().Checkpoint(); }

} // namespace AlterEgo::Background
