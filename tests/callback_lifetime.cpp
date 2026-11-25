#include "../src/CallbackLifetime.h"
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
    auto draining=std::async(std::launch::async,[&] { gate.Drain(); });
    assert(draining.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    release.set_value();
    worker.join();
    draining.get();
    gate.Open();
    { Gate::Lease lease(gate); assert(lease); }
    gate.Close();
    gate.Drain();
    return 0;
}
