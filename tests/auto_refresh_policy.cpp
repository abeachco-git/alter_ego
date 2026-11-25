#include "../src/AutoRefreshPolicy.h"
#include <cassert>
#include "../src/ArenaNetAdmission.h"
#include <thread>
#include <atomic>
#include <vector>
int case_auto_refresh_policy(int, char**) {
    using namespace AlterEgo::AutoRefreshPolicy;
    assert(!Enabled());
    assert(!AllowRequest(false));
    assert(AllowRequest(true));
    SetEnabled(true);
    assert(Enabled());
    assert(AllowRequest(false));
    assert(AllowRequest(true));
    SetEnabled(false);
    assert(!Enabled());
    assert(!AllowRequest(false));
    assert(AllowRequest(true));
    SetEnabled(true);
    SetIncluded(Dataset::Wallet,false);
    assert(!Enabled(Dataset::Wallet)&&Enabled(Dataset::Bank));
    assert(AllowRequest(true,Dataset::Wallet)&&!AllowRequest(false,Dataset::Wallet));
    SetMask(0xffffffffu);assert(GetMask()==AllMask&&Enabled(Dataset::Wallet));
    SetEnabled(false);assert(!Enabled(Dataset::Bank)&&Included(Dataset::Bank));
    using namespace std::chrono;
    AlterEgo::ArenaNet::Admission queue(2);
    assert(queue.Acquire([]{return false;},seconds(1)));
    std::vector<int> order;
    auto worker=[&](int id){assert(queue.Acquire([]{return false;},seconds(5)));order.push_back(id);queue.Release();};
    std::thread one(worker,1);
    while(queue.Waiting()!=1)std::this_thread::yield();
    std::thread two(worker,2);
    while(queue.Waiting()!=2)std::this_thread::yield();
    assert(!queue.Acquire([]{return false;},milliseconds(10)));
    queue.Release();one.join();two.join();
    assert((order==std::vector<int>{1,2}));
    assert(queue.Acquire([]{return false;},seconds(1)));
    assert(!queue.Acquire([]{return false;},milliseconds(10)));
    assert(queue.Waiting()==0);queue.Release();
    assert(!queue.Acquire([]{return true;},seconds(1)));
    return 0;
}
