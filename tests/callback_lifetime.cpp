#include "../src/CallbackLifetime.h"
#include "../src/UiCallbackInbox.h"
#include <cassert>
#include <future>
#include <thread>
int case_callback_lifetime(int,char**) {
    using AlterEgo::Callbacks::Gate;
    Gate gate;
    assert(!Gate::Lease(gate));
    gate.Open();
    std::promise<void> entered, release;
    auto released = release.get_future();
    std::thread worker([&] {
        Gate::Lease lease(gate);
        assert(lease);
        { Gate::Lease nested(gate); assert(nested); }
        entered.set_value();
        released.wait();
    });
    entered.get_future().wait();
    gate.Close();
    assert(!Gate::Lease(gate));
    gate.Open(); // a retired session cannot reopen while a lease is active
    assert(!Gate::Lease(gate));
    auto draining=std::async(std::launch::async,[&] { gate.Drain(); });
    assert(draining.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    release.set_value();
    worker.join();
    draining.get();
    gate.Open();
    { Gate::Lease lease(gate); assert(lease); }
    gate.Close();
    gate.Drain();
    // Concurrent producers publish owned values; no callback executes UI work.
    AlterEgo::Callbacks::UiInbox<unsigned,256> inbox;
    AlterEgo::Callbacks::UiToggles toggles;
    std::thread a([&]{for(unsigned i=0;i<100;++i){inbox.Push(i);toggles.Toggle(1);}});
    std::thread b([&]{for(unsigned i=100;i<200;++i){inbox.Push(i);toggles.Toggle(2);}});
    a.join();b.join();
    auto values=inbox.Take();assert(values.size()==200);
    bool seen[200]={};for(auto value:values){assert(value<200&&!seen[value]);seen[value]=true;}
    assert(inbox.Take().empty()&&toggles.Take()==0);
    toggles.Toggle(1);toggles.Toggle(2);toggles.Toggle(1);assert(toggles.Take()==2);
    AlterEgo::Callbacks::UiInbox<unsigned,2> limited;
    limited.Push(1);limited.Push(2);limited.Push(3);
    auto newest=limited.Take();assert(newest.size()==2&&newest.front()==2&&newest.back()==3);
    return 0;
}
