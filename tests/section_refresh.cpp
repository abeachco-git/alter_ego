#include "../src/SectionRefresh.h"
#include "../src/CharacterActivity.h"
#include "../src/CharacterScopes.h"
#include "../src/RefreshStatusCore.h"
#include "../src/ApiActivity.h"
#include "../src/AutoRefreshPolicy.h"
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
    {
        namespace P=AlterEgo::AutoRefreshPolicy;
        const auto oldEnabled=P::Enabled();const auto oldMask=P::GetMask();
        P::SetEnabled(true);P::SetMask(P::AllMask);
        P::RetryIntent automatic,manual{true};
        assert(automatic.Allowed(true,P::Dataset::Vault));
        assert(!automatic.Allowed(false,P::Dataset::Vault));
        assert(!manual.Allowed(false,P::Dataset::Vault));
        P::SetEnabled(false);
        assert(!automatic.Allowed(true,P::Dataset::Vault));
        assert(manual.Allowed(true,P::Dataset::Vault));
        manual.EndDemand();
        assert(!manual.Allowed(true,P::Dataset::Vault)); // reopen is ordinary eligibility
        P::SetEnabled(true);P::SetIncluded(P::Dataset::Vault,false);
        assert(!automatic.Allowed(true,P::Dataset::Vault));
        manual.manual=true;assert(manual.Allowed(true,P::Dataset::Vault));
        P::SetIncluded(P::Dataset::Vault,true);
        manual.EndDemand();assert(manual.Allowed(true,P::Dataset::Vault));
        P::SetEnabled(oldEnabled);P::SetMask(oldMask);
    }
    // Ordinary retry is independent of the eligibility predicate. A newer
    // confirmed success supersedes the failed attempt; equal seconds are
    // conservative until the owner clears its attempt on success.
    assert(F::RetryUntil(now-1000,now-899,now)==now+1);
    assert(!F::RetryUntil(now-1000,now-900,now));
    assert(!F::RetryUntil(now-10,now-11,now));
    assert(F::RetryUntil(now-10,now-10,now)==now+890);
    assert(!F::RetryUntil(0,now+1,now));
    assert(!F::RetryUntil(0,now-1,now,F::FailureSeconds,true));
    assert(F::Due(now-10,now-11,now,900,false,now-5));
    assert(!F::Due(now-10,now-10,now,900,false,now-5));
    assert(F::TimedEligibility(now-1800,0,now,1800).due);
    assert(!F::TimedEligibility(now-1799,0,now,1800).due);
    assert(F::TimedEligibility(now-1799,0,now,1800).at==now+1);
    assert(F::TimedEligibility(now+1,0,now,1800).due);
    assert(F::TimedEligibility(0,now-899,now,1800).reason==F::WaitReason::Retry);
    {
        namespace R=AlterEgo::RefreshStatus;
        R::Scope current;current.success=now-60;current.fresh=true;current.automatic=true;
        R::Scope stale=current;stale.success=now-1800;stale.fresh=false;
        stale.eligibility={F::WaitReason::Due,now,true};
        assert(R::Project(stale,now)==R::Kind::Due);
        stale.access=R::Access::Denied;
        assert(R::Project(stale,now)==R::Kind::Denied);
        assert(R::Describe(stale,now).find("Refresh due")==std::string::npos);
        stale.access=R::Access::Unverified;assert(R::Project(stale,now)==R::Kind::Unverified);
        stale.access=R::Access::NoAPI;assert(R::Project(stale,now)==R::Kind::NoAPI);
        stale.access=R::Access::Allowed;stale.automatic=false;
        assert(R::Project(stale,now)==R::Kind::Disabled);
        stale.automatic=true;stale.demandAllowed=false;
        assert(R::Project(stale,now)==R::Kind::Scheduled);
        assert(R::Describe(stale,now).find("Refresh due")==std::string::npos);
        stale.demandAllowed=true;stale.failed=true;stale.error="Invalid response";
        stale.eligibility={F::WaitReason::Retry,now+30,false};
        assert(R::Project(stale,now)==R::Kind::Failed);
        stale.error="HTTP 503";assert(R::Project(stale,now)==R::Kind::Retry);
        stale.phase=R::Phase::Queued;assert(R::Project(stale,now)==R::Kind::Queued);
        stale.phase=R::Phase::Working;assert(R::Project(stale,now)==R::Kind::Working);
        stale.phase=R::Phase::Idle;R::Aggregate all;all.Add(current,now);all.Add(stale,now);
        assert(all.Timestamp()==now-1800&&all.Status()==R::Kind::Retry&&all.retryAt==now+30);
        R::Scope missing;all.Add(missing,now);assert(all.Timestamp()==0);
        R::Aggregate none;assert(none.Status()==R::Kind::NoScopes&&none.Timestamp()==0);
        R::Scope future=current;future.success=now+1;future.fresh=false;
        R::Aggregate invalid;invalid.Add(future,now);assert(!invalid.Timestamp());
        R::CharacterBatch batch;R::CharacterKey a{"account-a","Hero"},b{"account-b","Hero"};
        batch.Queue(a,1,3);batch.Queue(b,2,3);
        assert(batch.Get(a,1).phase==R::Phase::Queued&&batch.Get(a,2).phase==R::Phase::Idle);
        batch.Start(a,1,3);batch.Activate(a,2);
        assert(batch.Get(a,1).phase==R::Phase::Working&&batch.Get(a,1).active==2);
        batch.Finish(a,1,now);assert(batch.Get(a,1).verified==1&&batch.Get(a,1).failed==2);
        assert(batch.Get(a,1).phase==R::Phase::Idle&&batch.Get(a,1).active==0);
        assert(batch.Get(b,2).phase==R::Phase::Queued);
        batch.Stop(now,"Cancelled");assert(batch.Get(b,2).requested==0&&batch.Get(a,1).verified==1);
        R::WorkPhases<std::pair<uint64_t,R::CharacterKey>> shared;
        const auto epochA=std::make_pair(uint64_t(1),a),epochB=std::make_pair(uint64_t(2),a);
        shared.Queue(epochA);shared.Queue(epochA);shared.Queue(epochB);
        shared.Start(epochA);assert(shared.Get(epochA)==R::Phase::Working&&shared.Get(epochB)==R::Phase::Queued);
        shared.Finish(epochA,R::Phase::Working);assert(shared.Get(epochA)==R::Phase::Queued);
        shared.Finish(epochA,R::Phase::Queued);assert(shared.Get(epochA)==R::Phase::Idle&&shared.Get(epochB)==R::Phase::Queued);
        shared.Start(epochB);shared.Finish(epochB,R::Phase::Working);assert(shared.Get(epochB)==R::Phase::Idle);
        batch.Start(b,2,4);batch.Stop(now+1,"Cancelled");
        assert(batch.Get(b,2).phase==R::Phase::Idle&&batch.Get(b,2).failed==4&&batch.Get(b,1).requested==0);
    }
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
    A::Tracker logins;
    assert(logins.ImportLogin(key,now-100));
    assert(!logins.ImportLogin(key,now-200));
    assert(logins.LastSeen(key)==0); // migration cannot fabricate freshness activity
    assert(logins.LastLogin(other)==0);
    assert(A::LoginBefore(now,now-100,true)&&A::LoginBefore(now-100,now,false));
    assert(A::LoginBefore(now,0,true)&&A::LoginBefore(now,0,false));
    assert(!A::LoginBefore(0,now,true)&&!A::LoginBefore(0,0,false));
    auto revision=logins.LoginRevision();
    logins.Observe(key.first,key.second,34,true,now);
    assert(logins.LastLogin(key)==now&&logins.LoginRevision()!=revision);
    revision=logins.LoginRevision();
    logins.Observe(key.first,key.second,35,true,now+1);
    assert(logins.LoginRevision()==revision);
    logins.Observe(key.first,key.second,35,false,now+2);
    logins.Observe(key.first,key.second,35,false,now+14);
    assert(logins.LoginRevision()==revision);
    logins.Observe(key.first,key.second,35,true,now+15);
    assert(logins.LastLogin(key)==now+15&&logins.LoginRevision()!=revision);
    A::Tracker loginReload;loginReload.Load(logins.Save());
    assert(loginReload.LastLogin(key)==now+15&&loginReload.ActiveKey().first.empty());
    tracker.Observe(key.first,key.second,34,true,now);
    assert(!tracker.Due(key,"Bags",0,0,now+599));
    assert(tracker.Due(key,"Bags",0,0,now+600));
    assert(tracker.Due(key,"Bags",0,0,now+1,false,true));
    assert(tracker.Due({key.first,{}}, "Bank",0,0,now+1,false));
    // The account-scoped policy drives both freshness labels and eligibility.
    // Ordinary recurring requests retain their independent deterministic spread.
    {
        A::Tracker storage;
        const A::Key account{key.first,{}};
        storage.Observe(key.first,key.second,34,true,now);
        std::set<int64_t> offsets;
        for(const auto* scope:{"Bank","Shared inventory","Materials","Legendary Armory"}){
            const auto interval=std::string(scope)=="Legendary Armory"?1800:600;
            const auto offset=F::Offset(account.first+"//"+scope);
            if(interval==600)offsets.insert(offset);
            assert(F::ActivitySeconds(true,scope)==interval);
            assert(storage.Due(account,scope,0,0,now+1,false)); // first retrieval unchanged
            assert(storage.Fresh(account,scope,now,now+interval-1,false));
            assert(!storage.Fresh(account,scope,now,now+interval,false));
            const auto eligible=storage.Eligible(account,scope,now,0,now+interval,false);
            assert(eligible.at==now+interval+offset);
            assert(!storage.Due(account,scope,now,0,now+interval+offset-1,false));
            assert(storage.Due(account,scope,now,0,now+interval+offset,false));
            assert(!storage.Due(account,scope,now,now+interval+offset,now+interval+offset+1,false));
            assert(storage.Due(account,scope,now,now+interval+offset,now+interval+offset+1,false,true));
        }
        assert(offsets.size()>1); // sample account does not schedule all three together
        storage.Observe(key.first,"Another",34,true,now+100);
        assert(storage.Eligible(account,"Bank",now,0,now+101,false).at==now+600+F::Offset(account.first+"//Bank"));
        storage.Finish(now+200);
        for(const auto* scope:{"Bank","Shared inventory","Materials","Legendary Armory"}){
            assert(!storage.Due(account,scope,now,0,now+799,false));
            assert(storage.Due(account,scope,now,0,now+800,false));
            const auto ticket=storage.Begin(account,scope,now+800);
            storage.Success(account,scope,ticket,now+801);
            assert(storage.Fresh(account,scope,now+801,now+99999,false));
            assert(!storage.Due(account,scope,now+801,0,now+99999,false));
        }
    }
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
    a->MarkAttempted(a->ids.begin(),a->ids.begin()+1);
    a.reset();assert(F::ClaimIds("skills",{1})->ids.empty());
    assert(F::ClaimIds("skills",{2})->ids.size()==1); // never attempted
    auto abandonedClaim=F::ClaimIds("cancel-test",{7,8});
    abandonedClaim.reset();
    auto replacementClaim=F::ClaimIds("cancel-test",{7,8});
    assert(replacementClaim->ids.size()==2);
    replacementClaim->MarkAttempted(replacementClaim->ids.begin(),replacementClaim->ids.begin()+1);
    replacementClaim.reset();
    assert(F::ClaimIds("cancel-test",{7,8})->ids==std::vector<uint32_t>{8});
    auto manualCancelled=F::ClaimIds("cancel-test",{7},true);
    assert(manualCancelled->ids.size()==1);
    manualCancelled.reset();
    assert(F::ClaimIds("cancel-test",{7})->ids.empty()); // preserve original cooldown
    assert(F::ClaimIds("skills",{1},true)->ids.size()==1);
    assert(F::ClaimIds("items",{1})->ids.size()==1);
    auto joined=F::ClaimIds("join-test",{42});
    assert(!F::WaitForIds("join-test",{42},[]{return false;},std::chrono::milliseconds(1)));
    assert(!F::WaitForIds("join-test",{42},[]{return true;}));
    assert(F::WaitForIds("join-test",{43},[]{return false;}));
    joined.reset();
    assert(F::WaitForIds("join-test",{42},[]{return false;}));
    namespace S=AlterEgo::CharacterScopes;
    // Footer reasons use the exact scheduler eligibility, never a parallel timer.
    {
        using W=A::WaitReason;
        A::Tracker explain;
        const A::Key hero{"account-explain","Hero"};
        explain.Observe(hero.first,hero.second,34,true,now);
        auto waiting=explain.Inspect(hero,"Core",0,now+1);
        assert(waiting.active&&!waiting.fresh&&!waiting.eligibility.due);
        assert(waiting.eligibility.reason==W::LoginGrace&&waiting.eligibility.at==now+600);
        assert(explain.Due(hero,"Core",0,0,now+600));
        auto attempt=explain.Begin(hero,"Core",now+600);
        explain.Failure(hero,"Core",attempt,"HTTP 503");
        auto retry=explain.Inspect(hero,"Core",0,now+601);
        assert(retry.eligibility.reason==W::Retry&&retry.eligibility.at==now+1500);
        assert(retry.error=="HTTP 503"&&!retry.eligibility.due);
        auto newer=explain.Begin(hero,"Core",now+602);
        explain.Failure(hero,"Core",attempt,"obsolete");
        assert(explain.Inspect(hero,"Core",0,now+602).error.empty());
        explain.Success(hero,"Core",newer,now+603);
        auto current=explain.Inspect(hero,"Core",now+603,now+604);
        assert(current.fresh&&current.error.empty()&&current.eligibility.reason==W::ActiveInterval);
        explain.Observe(hero.first,hero.second,35,true,now+605);
        auto spec=explain.Inspect(hero,"BuildTabs",now+603,now+606);
        assert(spec.eligibility.reason==W::SpecDebounce&&spec.eligibility.at==now+613);
        explain.Finish(now+620);
        auto offline=explain.Inspect(hero,"Core",now+603,now+621);
        assert(!offline.active&&!offline.fresh&&offline.eligibility.reason==W::LogoutGrace);
        assert(offline.eligibility.at==now+1220);
        assert(explain.Due(hero,"Core",now+603,0,now+1220));
        auto finalPull=explain.Begin(hero,"Core",now+1220);
        explain.Success(hero,"Core",finalPull,now+1221);
        assert(explain.Inspect(hero,"Core",now+1221,now+99999).eligibility.reason==W::Current);
    }
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
    A::Tracker cancelling;
    cancelling.Observe(key.first,key.second,34,true,now);
    cancelling.Finish(now+10);
    auto abandoned=cancelling.Begin(key,"Bags",now+610);
    auto replacement=cancelling.Begin(key,"Bags",now+611);
    cancelling.Cancel(key,"Bags",abandoned);
    assert(!cancelling.Due(key,"Bags",0,0,now+612));
    cancelling.Cancel(key,"Bags",replacement);
    assert(cancelling.Due(key,"Bags",0,0,now+612));
    auto sameSecondOld=cancelling.Begin(key,"Bags",now+612);
    auto sameSecondNew=cancelling.Begin(key,"Bags",now+612);
    cancelling.Cancel(key,"Bags",sameSecondOld);
    cancelling.Success(key,"Bags",sameSecondOld,now+612);
    assert(!cancelling.Due(key,"Bags",0,0,now+612));
    cancelling.Cancel(key,"Bags",sameSecondNew);
    assert(cancelling.Due(key,"Bags",0,0,now+612));
    auto success=cancelling.Begin(key,"Bags",now+613);
    cancelling.Success(key,"Bags",success,now+614);
    cancelling.Cancel(key,"Bags",success);
    assert(cancelling.Fresh(key,"Bags",now+614,now+615));
    A::Tracker missingAtShutdown;
    missingAtShutdown.Observe(key.first,key.second,34,true,now);
    missingAtShutdown.Observe("",{},0,false,now+10);
    missingAtShutdown.Finish(now+20);
    assert(missingAtShutdown.LastSeen(key)==now+10);
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
    rec.Start(D::Endpoint::Catalog,3700);rec.Finish(D::Endpoint::Catalog,0,false,true);
    const auto cancelled=rec.Read(3700).endpoints[static_cast<size_t>(D::Endpoint::Catalog)];
    assert(cancelled.cancelled==1&&cancelled.failed==0&&cancelled.active==1);
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
