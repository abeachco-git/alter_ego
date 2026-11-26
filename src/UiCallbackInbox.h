#pragma once
// Host callbacks may execute on the sending thread. Copy small values into
// an inbox; only the UI owner may consume them or mutate UI state.
#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace AlterEgo::Callbacks {
template<class T,std::size_t Capacity>
class UiInbox {
    static_assert(Capacity>0,"Inbox needs capacity");
    std::mutex mutex_;
    std::deque<T> pending_;
public:
    // For transient notifications the newest entries are most relevant.
    // Explicit user actions use their own inbox so chat floods cannot evict them.
    void Push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(std::move(value));
        if(pending_.size()>Capacity)pending_.pop_front();
    }
    std::deque<T> Take() {
        std::deque<T> result;
        std::lock_guard<std::mutex> lock(mutex_);
        result.swap(pending_);
        return result;
    }
};
class UiToggles {
    std::atomic<unsigned> pending_{0};
public:
    void Toggle(unsigned mask) noexcept {pending_.fetch_xor(mask);}
    unsigned Take() noexcept {return pending_.exchange(0);}
};
} // namespace AlterEgo::Callbacks
