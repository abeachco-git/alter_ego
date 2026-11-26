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
    {
        using namespace AlterEgo::RefreshEngine;
        auto first=ClaimIds("commerce_item_prices",{11,12,11});assert(first->ids.size()==2);
        assert(InspectId("commerce_item_prices",11).pending);
        auto overlap=ClaimIds("commerce_item_prices",{12,13},true);assert(overlap->ids.size()==1&&overlap->ids.front()==13);
        first->attempted.insert(11);first.reset();
        assert(!InspectId("commerce_item_prices",11).pending&&InspectId("commerce_item_prices",11).retrySeconds>0);
        assert(ClaimIds("commerce_item_prices",{11,12})->ids.size()==1);
        assert(ClaimIds("commerce_item_prices",{11},true)->ids.size()==1);
        overlap.reset();
        auto batch=ClaimIds("commerce_item_prices",{21,22});std::vector<uint32_t> firstId{21};
        batch->ReleaseIds(firstId.begin(),firstId.end(),true);
        assert(!InspectId("commerce_item_prices",21).pending&&InspectId("commerce_item_prices",22).pending);
        auto newer=ClaimIds("commerce_item_prices",{21});assert(newer->ids.size()==1);
        batch.reset();assert(InspectId("commerce_item_prices",21).pending&&!InspectId("commerce_item_prices",22).pending);
        newer.reset();ResetMetadata();
    }
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
    // Pending interactive and objective requests precede background work,
    // but the currently admitted request remains non-preemptible.
    AlterEgo::ArenaNet::Admission priorityQueue(8);
    assert(priorityQueue.Acquire([]{return false;},seconds(1)));
    order.clear();
    auto prioritized=[&](int id,unsigned priority){
        assert(priorityQueue.Acquire([]{return false;},seconds(5),priority));
        order.push_back(id);priorityQueue.Release();
    };
    std::thread background(prioritized,1,0);
    while(priorityQueue.Waiting()!=1)std::this_thread::yield();
    std::thread manual(prioritized,2,3);
    while(priorityQueue.Waiting()!=2)std::this_thread::yield();
    std::thread objective(prioritized,3,2);
    while(priorityQueue.Waiting()!=3)std::this_thread::yield();
    std::thread manual2(prioritized,4,3);
    while(priorityQueue.Waiting()!=4)std::this_thread::yield();
    priorityQueue.Release();background.join();manual.join();objective.join();manual2.join();
    assert((order==std::vector<int>{2,4,3,1}));
    return 0;
}
