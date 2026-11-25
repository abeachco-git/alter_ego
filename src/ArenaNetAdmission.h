#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <chrono>
#include <condition_variable>
namespace AlterEgo::ArenaNet {
// FIFO, bounded waiting list. No secrets or request contents enter the queue.
class Admission {
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<uint64_t> queue_;
    uint64_t next_=0;
    bool active_=false;
    size_t capacity_;
public:
    explicit Admission(size_t capacity=64):capacity_(capacity){}
    Admission(const Admission&)=delete;
    Admission& operator=(const Admission&)=delete;
    template<class Stop> bool Acquire(Stop stop,std::chrono::milliseconds limit){
        std::unique_lock<std::mutex> lock(mutex_);
        if(stop()||queue_.size()>=capacity_)return false;
        const auto ticket=++next_;
        queue_.push_back(ticket);
        const auto deadline=std::chrono::steady_clock::now()+limit;
        for(;;){
            if(stop()||std::chrono::steady_clock::now()>=deadline){
                queue_.erase(std::find(queue_.begin(),queue_.end(),ticket));
                cv_.notify_all();return false;
            }
            if(!active_&&queue_.front()==ticket){
                queue_.pop_front();active_=true;return true;
            }
            cv_.wait_for(lock,std::chrono::milliseconds(25));
        }
    }
    void Release(){std::lock_guard<std::mutex> lock(mutex_);active_=false;cv_.notify_all();}
    size_t Waiting(){std::lock_guard<std::mutex> lock(mutex_);return queue_.size();}
};
}
