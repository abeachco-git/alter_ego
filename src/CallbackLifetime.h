#pragma once
#include <condition_variable>
#include <mutex>

namespace AlterEgo::Callbacks {
// Tracks locally admitted callback bodies. Never hold the gate mutex while
// running external code. Host deregistration must still prevent invocation
// of the DLL's code after it has been unloaded.
class Gate {
    std::mutex mutex_;
    std::condition_variable drained_;
    bool open_ = false;
    unsigned active_ = 0;
public:
    void Open() {
        std::lock_guard<std::mutex> lock(mutex_);
        // Reopening a retired session before its admitted callbacks drain
        // would mix two lifetimes. Only the lifecycle owner may reopen.
        if (active_ == 0) open_ = true;
    }
    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
    }
    void Drain() {
        std::unique_lock<std::mutex> lock(mutex_);
        drained_.wait(lock, [&] { return active_ == 0; });
    }
    class Lease {
        Gate* gate_ = nullptr;
    public:
        explicit Lease(Gate& gate) {
            std::lock_guard<std::mutex> lock(gate.mutex_);
            if (gate.open_) {
                ++gate.active_;
                gate_ = &gate;
            }
        }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease() {
            if (!gate_) return;
            std::lock_guard<std::mutex> lock(gate_->mutex_);
            if (--gate_->active_ == 0) gate_->drained_.notify_all();
        }
        explicit operator bool() const { return gate_ != nullptr; }
    };
};
inline Gate& Runtime() { static Gate gate; return gate; }
}
