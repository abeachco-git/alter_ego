#include "../src/BackgroundWorkers.h"
#include "../src/RevisionSnapshot.h"
#include <unordered_map>
#include <vector>
#include <future>
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
    // A long local delay must wake promptly on shutdown, including JoinAll.
    assert(group.Open());
    std::promise<void> waiting,interrupted;
    auto wake=interrupted.get_future();
    assert(group.Launch([&]{
        waiting.set_value();
        assert(!group.WaitFor(std::chrono::seconds(30)));
        interrupted.set_value();
    }));
    waiting.get_future().wait();
    auto joining=std::async(std::launch::async,[&]{group.JoinAll();});
    assert(wake.wait_for(std::chrono::seconds(2))==std::future_status::ready);
    joining.get();
    assert(group.Open());
    assert(group.WaitFor(std::chrono::milliseconds(1)));
    group.RequestStop();
    assert(!group.WaitFor(std::chrono::seconds(30)));
    group.JoinAll();
    // Retry/failure state records only finished non-cancelled operations.
    int recorded=0;bool stopped=false;
    {AlterEgo::Background::CompletedAttempt a([&]{return stopped;},[&]{++recorded;});a.Finish();a.Finish();}
    assert(recorded==1);
    {AlterEgo::Background::CompletedAttempt a([&]{return stopped;},[&]{++recorded;});stopped=true;}
    assert(recorded==1);stopped=false;
    try{AlterEgo::Background::CompletedAttempt a([&]{return stopped;},[&]{++recorded;});throw std::runtime_error("failed");}
    catch(const std::runtime_error&){}
    assert(recorded==2);
    {AlterEgo::Background::CompletedAttempt a([&]{return stopped;},[&]{++recorded;});a.Dismiss();}
    assert(recorded==2);
    // A large catalog is not copied/rebuilt on steady-state render frames.
    using Catalog=std::unordered_map<unsigned,std::shared_ptr<const std::vector<unsigned>>>;
    Catalog canonical;
    for(unsigned id=1;id<=10000;++id)canonical.emplace(id,std::make_shared<const std::vector<unsigned>>(20,id));
    AlterEgo::RevisionSnapshot<Catalog> snapshots;
    unsigned builds=0;
    auto capture=[&](uint64_t revision){
        return snapshots.Acquire(revision,[&]{++builds;return canonical;});
    };
    const auto first=capture(1);
    for(int i=0;i<1000;++i)assert(capture(1)==first);
    assert(builds==1&&first->at(2)==canonical.at(2));
    // A reader may retain old immutable payloads while a revision is replaced.
    std::promise<void> reading,updated;
    auto read=reading.get_future(),update=updated.get_future();
    std::thread reader([&]{
        reading.set_value();update.wait();
        assert(first->at(1)->front()==1&&first->at(2)->size()==20);
    });
    read.wait();
    canonical[1]=std::make_shared<const std::vector<unsigned>>(20,99);
    const auto second=capture(2);
    assert(builds==2&&second!=first&&second->at(1)->front()==99);
    assert(second->at(2)==first->at(2)); // unchanged definition payload shared
    updated.set_value();reader.join();
    try{snapshots.Acquire(3,[]()->Catalog{throw std::runtime_error("snapshot");});assert(false);}
    catch(const std::runtime_error&){}
    assert(capture(2)==second&&builds==2);
    assert(capture(3)!=second&&builds==3);
    return 0;
}
