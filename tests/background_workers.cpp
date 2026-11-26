#include "../src/BackgroundWorkers.h"
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

int case_background_workers(int, char**) {
    AlterEgo::Background::WorkerGroup group;
    std::atomic<int> failures{0}, completed{0};
    assert(!group.Launch([] { assert(false); }, [&] { ++failures; }));
    assert(group.Open());
    assert(!group.Open()); // already open, cannot start a replacement session
    assert(group.Launch([&] { ++completed; }));
    assert(group.Launch([] { throw std::runtime_error("test"); }, [&] { ++failures; }));
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, release = false;
    assert(group.Launch([&] {
        {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true; cv.notify_one();
            cv.wait(lock, [&] { return release; });
        }
        assert(!group.Launch([] { assert(false); }, [&] { ++failures; }));
        group.Checkpoint();
    }, [&] { ++failures; }));
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return entered; });
    }
    assert(group.ActiveCount() >= 1);
    group.RequestStop();
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    cv.notify_one();
    group.JoinAll();
    assert(group.ActiveCount() == 0);
    assert(completed == 1 && failures == 4 && group.FailureCount() == 1);
    assert(group.Open());
    assert(group.Launch([&] { ++completed; }));
    group.JoinAll();
    assert(completed == 2);
    // A throwing cleanup is contained and counted; the next launch still works.
    assert(group.Open());
    assert(group.Launch([] { throw std::runtime_error("job"); },
                        [] { throw std::runtime_error("cleanup"); }));
    group.JoinAll();
    assert(group.ActiveCount()==0 && group.FailureCount()==3);
    assert(group.Open());
    for(int i=0;i<32;++i)assert(group.Launch([&] { ++completed; }));
    group.JoinAll();
    assert(group.ActiveCount()==0 && completed==34);
    return 0;
}
