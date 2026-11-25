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

int case_section_refresh(int, char**) {
    using namespace AlterEgo::SectionRefresh;
    const std::time_t now = 2000000;
    namespace F=AlterEgo::RefreshEngine;
    assert(F::ObjectiveMinutes()==15);
    F::SetObjectiveMinutes(20);assert(F::ObjectiveSeconds()==1200);
    F::SetObjectiveMinutes(15);
    assert(F::Due(now-900,0,now,900));
    assert(!F::Due(now-900,now-1,now,900));
    assert(F::Due(now-900,now-1,now,900,true));
    assert(F::Due(now-10,0,now,900,false,now-5));
    assert(!F::Due(now-900,0,now,900,false,0,30));
    assert(F::Due(now-930,0,now,900,false,0,30));
    assert(F::Offset("account/vault/0")<=45);
    assert(!F::CharacterDue(false,0,now-100000,now-90000,0,now));
    assert(!F::CharacterDue(true,now-10,now,now-100000,0,now));
    assert(F::CharacterDue(true,now-1800,now,now-100000,0,now));
    assert(F::CharacterDue(true,now-10,now,now-100000,0,now,true));
    static_assert(F::LogoutGraceSeconds==600);
    assert(F::CharacterDue(false,0,now-600,now-1000,0,now));
    assert(!F::CharacterDue(false,0,now-599,now-1000,0,now));
    assert(!F::CharacterDue(false,0,now-599,0,0,now)); // even a missing offline snapshot waits
    assert(F::CharacterDue(false,0,0,0,0,now)); // never observed; initial retrieval
    assert(F::CharacterDue(false,0,now-10,now-1000,0,now,false,true)); // explicit refresh
    assert(!F::CanReconcileLogout(now-599,now));
    assert(F::CanReconcileLogout(now-600,now));
    // An early manual success must not suppress the final eligible fetch.
    assert(F::CharacterDue(false,0,now-600,now-300,0,now,false,false,now-1000));
    assert(!F::CharacterDue(false,0,now-600,now-300,0,now,false,false,now-600));
    assert(!F::CharacterDue(false,0,now-600,now-1000,now-10,now));
    auto a=F::ClaimIds("skills",{1,1,2});assert(a->ids.size()==2);
    auto b=F::ClaimIds("skills",{2,3},true);assert(b->ids==std::vector<uint32_t>{3});
    a.reset();assert(F::ClaimIds("skills",{1})->ids.empty());
    assert(F::ClaimIds("skills",{1},true)->ids.size()==1);
    assert(F::ClaimIds("items",{1})->ids.size()==1);
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
    return 0;
}
