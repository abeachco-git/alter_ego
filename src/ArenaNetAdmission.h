#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <chrono>
#include <condition_variable>
namespace AlterEgo::ArenaNet {
// Bounded priority queue; FIFO within priority, aged requests cannot starve.
// No secrets or request contents enter the queue.
class Admission {
    std::mutex mutex_;
    std::condition_variable cv_;
    using Clock=std::chrono::steady_clock;
    struct Entry{uint64_t ticket;unsigned priority;Clock::time_point queued;};
    std::deque<Entry> queue_;
    uint64_t next_=0;
    bool active_=false;
    size_t capacity_;
public:
    explicit Admission(size_t capacity=64):capacity_(capacity){}
    Admission(const Admission&)=delete;
    Admission& operator=(const Admission&)=delete;
    template<class Stop> bool Acquire(Stop stop,std::chrono::milliseconds limit,unsigned priority=1){
        std::unique_lock<std::mutex> lock(mutex_);
        // Leave a few slots for interactive work when a large background
        // queue fills. Small custom capacities preserve their exact bound.
        const auto cap=priority==0&&capacity_>8?capacity_-4:capacity_;
        if(stop()||queue_.size()>=cap)return false;
        const auto ticket=++next_;
        const auto queued=Clock::now();
        queue_.push_back({ticket,std::min(3u,priority),queued});
        const auto deadline=queued+limit;
        for(;;){
            const auto now=Clock::now();
            if(stop()||now>=deadline){
                queue_.erase(std::find_if(queue_.begin(),queue_.end(),[&](const Entry& e){return e.ticket==ticket;}));
                cv_.notify_all();return false;
            }
            if(!active_){
                const auto rank=[&](const Entry& e){return now-e.queued>=std::chrono::seconds(30)?3u:e.priority;};
                auto best=std::max_element(queue_.begin(),queue_.end(),[&](const Entry& x,const Entry& y){return rank(x)<rank(y);});
                if(best!=queue_.end()&&best->ticket==ticket){queue_.erase(best);active_=true;return true;}
            }
            cv_.wait_for(lock,std::chrono::milliseconds(25));
        }
    }
    void Release(){std::lock_guard<std::mutex> lock(mutex_);active_=false;cv_.notify_all();}
    size_t Waiting(){std::lock_guard<std::mutex> lock(mutex_);return queue_.size();}
};
}
