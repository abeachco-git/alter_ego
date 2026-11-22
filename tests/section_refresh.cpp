#include "../src/SectionRefresh.h"
#include <cassert>
#include <ctime>
#include <vector>

struct Objective { bool claimed = false; };
struct Vault {
    std::vector<Objective> objectives;
    int meta_reward_astral = 0;
    bool meta_reward_claimed = false;
};
struct Clear { bool done = false; };

int main() {
    using namespace AlterEgo::SectionRefresh;
    const std::time_t now = 2000000;
    assert(Stale(0, now));
    assert(!Stale(now - 900, now));
    assert(Stale(now - 901, now));
    assert(!Stale(now - 10, now));
    assert(!Stale(now + 20, now)); // tolerate clock skew
    Vault vault;
    assert(!VaultClaimed(vault)); // missing response must refresh
    vault.objectives = {{true}, {false}};
    assert(!VaultClaimed(vault));
    vault.objectives[1].claimed = true;
    assert(VaultClaimed(vault));
    vault.meta_reward_astral = 50;
    assert(!VaultClaimed(vault)); // objective complete is not reward claimed
    vault.meta_reward_claimed = true;
    assert(VaultClaimed(vault));
    std::vector<Clear> entries;
    assert(!ClearsDone(entries));
    entries = {{true}, {false}};
    assert(!ClearsDone(entries));
    entries[1].done = true;
    assert(ClearsDone(entries));
}
