#include "../src/SectionRefresh.h"
#include "../src/CharacterActivity.h"
#include "../src/CharacterScopes.h"
#include "../src/ApiActivity.h"
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
    static_assert(F::CharacterSeconds==600);
    assert(!F::CharacterDue(true,now-599,now,0,0,now));
    assert(F::CharacterDue(true,now-600,now,0,0,now));
    assert(F::CharacterDue(true,now-1,now,0,0,now,false,true));
    assert(F::CharacterDue(true,now-600,now,now-100000,0,now));
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
    namespace A=AlterEgo::CharacterActivity;
    A::Tracker tracker;const A::Key key{"account-a","Reaper"},other{"account-b","Reaper"};
    tracker.Observe(key.first,key.second,34,true,now);
    assert(!tracker.Due(key,"Bags",0,0,now+599));
    assert(tracker.Due(key,"Bags",0,0,now+600));
    assert(tracker.Due(key,"Bags",0,0,now+1,false,true));
    assert(tracker.Due({key.first,{}},"Bank",0,0,now+1,false));
    assert(!tracker.Due(key,"Bags",now-90000,0,now+10));
    assert(tracker.Due(key,"Bags",now-90000,0,now+646));
    assert(tracker.Fresh(other,"Bags",now-90000,now+646));
    tracker.Observe(key.first,key.second,35,true,now+20);
    assert(!tracker.Due(key,"Character",now-100,0,now+27));
    assert(tracker.Due(key,"Character",now-100,0,now+28));
    assert(!tracker.Due(key,"Bags",now-100,0,now+28,false));
    tracker.Observe(key.first,key.second,35,false,now+40);
    tracker.Observe(key.first,key.second,35,true,now+45); // transient map load
    assert(tracker.ActiveKey()==key);
    tracker.Observe(key.first,key.second,35,false,now+60);
    tracker.Observe(key.first,key.second,35,false,now+72);
    assert(tracker.ActiveKey().first.empty());
    assert(!tracker.Due(key,"Bags",now-100,0,now+659));
    auto early=tracker.Begin(key,"Bags",now+300);
    tracker.Success(key,"Bags",early,now+301);
    assert(!tracker.Fresh(key,"Bags",now+301,now+660));
    assert(tracker.Due(key,"Bags",now+301,0,now+660));
    auto late=tracker.Begin(key,"Bags",now+660);tracker.Success(key,"Bags",late,now+661);
    assert(tracker.Fresh(key,"Bags",now+661,now+999999));
    assert(!tracker.Due(key,"Bags",now+661,0,now+999999));
    assert(tracker.Due(key,"Equipment",now-100,0,now+999999));
    A::Tracker restored;restored.Load(tracker.Save());
    assert(restored.Fresh(key,"Bags",now+661,now+999999));
    assert(restored.Due(key,"Equipment",now-100,0,now+999999));
    // A snapshot timestamp that advanced without its matching verification
    // ledger must never be treated as a verified legacy snapshot.
    assert(!restored.Fresh(key,"Bags",now+900,now+1000));
    assert(restored.Due(key,"Bags",now+900,0,now+1000));
    A::Tracker failed;
    failed.Observe(key.first,key.second,34,true,now);
    failed.Finish(now+10);
    failed.Begin(key,"Bags",now+20);
    assert(!failed.Fresh(key,"Bags",now+620,now+1000));
    // Unknown account identity during a brief transition preserves the
    // session; a long observation gap resets active freshness.
    A::Tracker gaps;
    gaps.Observe(key.first,key.second,34,true,now);
    gaps.Observe("",key.second,34,true,now+100);
    assert(gaps.ActiveKey()==key);
    gaps.Observe(key.first,key.second,34,true,now+101);
    assert(gaps.Due(key,"Character",now-100,0,now+646));
    gaps.Observe("",key.second,34,true,now+700);
    gaps.Observe(key.first,key.second,34,true,now+713);
    assert(!gaps.Due(key,"Character",now-100,0,now+714));
    // An offline response that overlaps a resumed session cannot cover it.
    gaps.Finish(now+720);
    auto pending=gaps.Begin(key,"Bags",now+1320);
    gaps.Observe(key.first,key.second,34,true,now+1321);
    gaps.Finish(now+1322);
    gaps.Success(key,"Bags",pending,now+1323);
    assert(!gaps.Fresh(key,"Bags",now+1323,now+1922));
    assert(gaps.Due(key,"Bags",now+1323,0,now+1922));
    auto a=F::ClaimIds("skills",{1,1,2});assert(a->ids.size()==2);
    auto b=F::ClaimIds("skills",{2,3},true);assert(b->ids==std::vector<uint32_t>{3});
    a.reset();assert(F::ClaimIds("skills",{1})->ids.empty());
    assert(F::ClaimIds("skills",{1},true)->ids.size()==1);
    assert(F::ClaimIds("items",{1})->ids.size()==1);
    auto joined=F::ClaimIds("join-test",{42});
    assert(!F::WaitForIds("join-test",{42},[]{return false;},std::chrono::milliseconds(1)));
    assert(!F::WaitForIds("join-test",{42},[]{return true;}));
    assert(F::WaitForIds("join-test",{43},[]{return false;}));
    joined.reset();
    assert(F::WaitForIds("join-test",{42},[]{return false;}));
    namespace S=AlterEgo::CharacterScopes;
    A::Tracker scoped;S::Stamps stamps{};
    assert(S::Visible(1)==(S::Core|S::Crafting|S::Builds));
    assert((S::Compare&S::Crafting)==0);
    scoped.Observe(key.first,key.second,34,true,now);
    stamps.fill(now-100);
    assert(!S::Due(scoped,key,stamps,S::All,now+10,true));
    scoped.Observe(key.first,key.second,35,true,now+20);
    assert(S::Due(scoped,key,stamps,S::All,now+28,true)==(S::Builds|S::Tabs|S::Physical));
    assert(!S::Due(scoped,key,stamps,S::Visible(1),now+28,false));
    assert(S::Due(scoped,key,stamps,S::Visible(1),now+28,true)==S::Builds);
    assert(!S::Fresh(scoped,key,stamps,S::Visible(1),now+28,true));
    const auto buildTicket=scoped.Begin(key,S::Name(S::Builds),now+29);
    scoped.Success(key,S::Name(S::Builds),buildTicket,now+30);
    stamps[S::Index(S::Builds)]=now+30;
    assert(S::Fresh(scoped,key,stamps,S::Visible(1),now+31,true));
    assert(S::Oldest(stamps,S::Builds)==now+30);
    assert(S::Oldest(stamps,S::Compare)==now-100);
    assert(S::Due(scoped,key,stamps,S::Compare,now+31,true)==(S::Tabs|S::Physical));
    // A coalesced response obtained before the grace cannot cover logout,
    // even if the supplement consumer joined after the grace expired.
    A::Tracker joinedActivity;
    joinedActivity.Observe(key.first,key.second,34,true,now);
    joinedActivity.Finish(now+10);
    auto joinedTicket=joinedActivity.Begin(key,S::Name(S::Physical),now+610);
    joinedTicket.verificationStarted=now+609;
    joinedActivity.Success(key,S::Name(S::Physical),joinedTicket,now+611);
    assert(!joinedActivity.Fresh(key,S::Name(S::Physical),now+611,now+612));
    assert(joinedActivity.Due(key,S::Name(S::Physical),now+611,0,now+612));
    namespace D=AlterEgo::ApiActivity;
    assert(D::ApiEndpoint("/v2/characters/Private%20Name/equipmenttabs?tabs=all")==D::Endpoint::Equipment);
    assert(D::ApiEndpoint("/v2/characters/Private%20Name/inventory")==D::Endpoint::Bags);
    assert(D::ApiEndpoint("/v2/account/achievements?access_token=secret")==D::Endpoint::AchProgress);
    assert(D::WikiUrl("https://wiki.guildwars2.com/api.php?action=query"));
    assert(!D::WikiUrl("https://wiki.guildwars2.com.example.org/api.php"));
    assert(D::RenderUrl("https://render.guildwars2.com/file/a/123.png"));
    assert(!D::RenderUrl("https://render.guildwars2.com.example.org/file/a/123.png"));
    assert(D::Source(D::Endpoint::RenderArtwork)==2);
    assert(D::Priority(D::Reason::Manual)>D::Priority(D::Reason::Objective));
    assert(D::CurrentReason()==D::Reason::Unclassified);
    {D::ReasonScope outer(D::Reason::Manual);
        assert(D::CurrentReason()==D::Reason::Manual);
        {D::ReasonScope inner(D::Reason::Background);assert(D::CurrentReason()==D::Reason::Background);}
        assert(D::CurrentReason()==D::Reason::Manual);}
    assert(D::CurrentReason()==D::Reason::Unclassified);
    D::Recorder rec;
    rec.Start(D::Endpoint::Equipment,100);rec.Finish(D::Endpoint::Equipment,200,true);
    rec.Start(D::Endpoint::Equipment,100);rec.Finish(D::Endpoint::Equipment,429,false);
    rec.Start(D::Endpoint::WikiApi,101,D::Reason::Objective);rec.Finish(D::Endpoint::WikiApi,200,false);
    rec.Start(D::Endpoint::RenderArtwork,101,D::Reason::Visible);rec.Finish(D::Endpoint::RenderArtwork,200,true);
    rec.Start(D::Endpoint::Bags,700);
    rec.Shared(false);rec.Shared(true);
    rec.MetadataClaim(2,3,4,true);rec.MetadataClaim(1,0,5,false);
    auto diag=rec.Read(700);
    assert(diag.last10[0]==1&&diag.last10[1]==1&&diag.last60[0]==3);
    assert(diag.peak[0]==2&&diag.peak[1]==1&&diag.peak[2]==1);
    assert(diag.last10[2]==1&&diag.reasons[1][static_cast<size_t>(D::Reason::Objective)]==1);
    assert(diag.reasons[2][static_cast<size_t>(D::Reason::Visible)]==1);
    auto eq=diag.endpoints[static_cast<size_t>(D::Endpoint::Equipment)];
    assert(eq.started==2&&eq.success==1&&eq.failed==1&&eq.limited==1&&!eq.active);
    assert(diag.endpoints[static_cast<size_t>(D::Endpoint::Bags)].active==1);
    assert(diag.sharedHits==1&&diag.sharedCoalesced==1);
    assert(diag.metadataOwned==3&&diag.metadataManual==2&&diag.metadataCoalesced==3&&diag.metadataCooldown==9);
    assert(rec.Read(3700).last60[0]==1);
    rec.Start(D::Endpoint::Catalog,3700);
    assert(rec.Read(3700).last60[0]==2);
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
