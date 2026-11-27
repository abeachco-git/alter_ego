#include "../src/ClearsRotation.h"
#include "../src/ClearsReset.h"
#include "../src/FractalScales.h"
#include "../src/FractalCardLayout.h"
#include <cassert>
#include <nlohmann/json.hpp>
#include <string>
int case_clears_rotation(int, char**) {
    using namespace ClearsRotation;
    using namespace ClearsReset;
    // Actual visible cards determine wrapping and section height.
    std::vector<FractalCardLayout::Card> cards{
        {0,240,false,true},{1,260,false,true},{2,250,false,true},{3,150,true,true}};
    auto wide=FractalCardLayout::Pack(cards,1050,6);
    assert(wide.rows==1 && wide.cards.size()==4);
    assert(wide.cards.back().width==150 && wide.cards.back().x+150<=1050.01f);
    auto narrow=FractalCardLayout::Pack(cards,650,6);
    assert(narrow.rows==2 && narrow.cards[2].row==1 && narrow.cards[3].row==1);
    assert(narrow.cards.back().width==150);
    cards[0].visible=cards[1].visible=cards[2].visible=false;
    auto rec=FractalCardLayout::Pack(cards,1050,6);
    assert(rec.rows==1 && rec.cards.size()==1 && rec.cards[0].index==3);
    assert(rec.cards[0].x==0 && rec.cards[0].width==150);
    cards[3].visible=false;
    assert(FractalCardLayout::Pack(cards,1050,6).rows==0);
    cards[1].visible=true; cards[3].visible=true;
    auto tiny=FractalCardLayout::Pack(cards,100,6);
    assert(tiny.rows==2 && tiny.cards[0].width==100 && tiny.cards[1].width==100);
    // At 00:00 UTC we must NOT consume the reset marker before the API grace
    // period. This was the cause of Vault + daily Fractals missing rollover.
    constexpr int64_t day = 86400, reset = 1795305600;
    assert(InitialMarker(reset, reset + 15, day) == reset - day);
    assert(InitialMarker(reset, reset + 61, day) == reset);
    assert(!Due(reset - day, reset, reset + 59));
    assert(Due(reset - day, reset, reset + 60));
    assert(!Due(reset, reset, reset + 3600));
    assert(!GracePassed(reset,reset+59)&&GracePassed(reset,reset+60));
    assert(LooksUnrotated({101, 102}, {102, 101}));
    assert(!LooksUnrotated({101, 102}, {101, 103}));
    assert(LooksUnrotated({101, 102}, {}));
    assert(!LooksUnrotated({}, {101, 102}));
    // A weekly response may contain the identical IDs but must reset its
    // progress/claim information. A held-over response is not fresh.
    struct WeeklyObjective {
        uint32_t id; int32_t progress_current, progress_complete; bool claimed;
    };
    struct WeeklyPeriod {
        int32_t meta_progress_current = 0;
        int32_t meta_progress_complete = 0;
        int32_t meta_reward_astral = 0;
        bool meta_reward_claimed = false;
        std::vector<WeeklyObjective> objectives;
    };
    WeeklyPeriod lastWeek{8, 8, 350, true,
        {{51, 10, 10, true}, {52, 1, 1, true}}};
    WeeklyPeriod oldResponse = lastWeek;
    std::reverse(oldResponse.objectives.begin(), oldResponse.objectives.end());
    assert(LooksUnrotatedVaultPeriod(lastWeek, oldResponse));
    WeeklyPeriod newWeek = lastWeek;
    newWeek.meta_progress_current = 0;
    newWeek.meta_reward_claimed = false;
    for (auto& o : newWeek.objectives) {
        o.progress_current = 0;
        o.claimed = false;
    }
    assert(!LooksUnrotatedVaultPeriod(lastWeek, newWeek));
    assert(LooksUnrotatedVaultPeriod(lastWeek, WeeklyPeriod{}));
    assert(!LooksUnrotatedVaultPeriod(WeeklyPeriod{}, newWeek));
    WeeklyPeriod changedIds = lastWeek;
    changedIds.objectives.front().id = 999;
    assert(!LooksUnrotatedVaultPeriod(lastWeek, changedIds));
    WeeklyPeriod changedProgress = lastWeek;
    changedProgress.objectives.front().progress_current = 0;
    assert(!LooksUnrotatedVaultPeriod(lastWeek, changedProgress));
    // Directional evidence: one completed-to-incomplete or claimed-to-unclaimed
    // objective is enough, but late forward progress is not a new period.
    WeeklyPeriod forward = newWeek;
    forward.objectives.front().progress_current = 1;
    forward.meta_progress_current = 1;
    assert(LooksUnrotatedVaultPeriod(newWeek,forward));
    assert(HoldVault(newWeek,forward,3600,4));
    WeeklyPeriod partiallyDone = forward;
    partiallyDone.objectives.front().progress_current = 2;
    assert(HoldVault(forward,partiallyDone,60,0));
    assert(HoldVault(partiallyDone,forward,60,0)); // neither is complete
    WeeklyPeriod completed = forward;
    completed.objectives.front().progress_current = 10;
    assert(HoldVault(forward,completed,60,0));
    assert(!HoldVault(completed,forward,60,0)); // only one objective resets
    WeeklyPeriod claimed = completed;
    claimed.objectives.front().claimed = true;
    assert(HoldVault(completed,claimed,60,0));
    assert(!HoldVault(claimed,completed,60,0)); // claim resets independently
    WeeklyPeriod metaDone = forward;
    metaDone.meta_progress_current = metaDone.meta_progress_complete;
    assert(HoldVault(forward,metaDone,60,0));
    assert(!HoldVault(metaDone,forward,60,0));
    WeeklyPeriod metaClaimed = metaDone;
    metaClaimed.meta_reward_claimed = true;
    assert(HoldVault(metaDone,metaClaimed,60,0));
    assert(!HoldVault(metaClaimed,metaDone,60,0));
    WeeklyPeriod changedTarget = forward;
    changedTarget.objectives.front().progress_complete += 5;
    changedTarget.meta_reward_astral += 1;
    assert(HoldVault(forward,changedTarget,60,0));
    WeeklyPeriod removed = lastWeek;
    removed.objectives.pop_back();
    assert(!HoldVault(lastWeek,removed,60,0));
    assert(!HoldVault(removed,lastWeek,60,0));
    assert(!HoldVault(lastWeek,newWeek,60,0)); // same daily IDs with reset progress
    assert(HoldVault(lastWeek,oldResponse,7200,8)); // never accept claimed prior snapshot
    assert(HoldVault(newWeek,newWeek,899,4));
    assert(HoldVault(newWeek,newWeek,900,2));
    assert(!HoldVault(newWeek,newWeek,900,3)); // identical all-zero rotation
    assert(!HoldVault(lastWeek,WeeklyPeriod{},60,0,true)); // valid empty Special
    assert(HoldVault(lastWeek,WeeklyPeriod{},60,0,false));
    RetryState first,second;first.Fail(1000,"held");assert(!first.Ready(30000)&&second.Ready(30000));
    assert(first.Ready(31000)&&first.Ready(2000,true));first.Fail(31000,"held");
    struct ClearProgress{uint32_t id;bool done;int current;std::vector<bool> bitDone;};
    const std::vector<ClearProgress> prior{{1,true,2,{true,true}}},zero{{1,false,0,{false,false}}},changed{{1,false,1,{true,false}}};
    assert(HoldClearProgress(prior,prior));assert(!HoldClearProgress(prior,zero));
    assert(!HoldClearProgress(prior,changed));assert(!HoldClearProgress(zero,zero));
    assert(first.at==91000&&first.Remaining(31000)==60);
    first.Reset();assert(first.Ready(0)&&first.reason.empty());
    assert(FreshPath("/v2/account/wizardsvault/daily",42)=="/v2/account/wizardsvault/daily?ae_refresh=42");
    assert(FreshPath("/v2/test?ids=all",43)=="/v2/test?ids=all&ae_refresh=43");
    const auto token1=RequestToken(),token2=RequestToken();assert(token1<token2);
    // Retry URLs must change even on the same day, otherwise an initially
    // stale max-age=3600 result could be cached for a full hour.
    assert(FreshCategoryUrl(88, 0) ==
           "https://api.guildwars2.com/v2/achievements/categories/88");
    assert(FreshCategoryUrl(88, reset + 61000) ==
           "https://api.guildwars2.com/v2/achievements/categories/88?ae_refresh=" +
           std::to_string(reset + 61000));
    assert(FreshCategoryUrl(475, reset + 61001) !=
           FreshCategoryUrl(475, reset + 61000));
    assert(AlternateCategoryUrl(88,1234)==
           "https://api.guildwars2.com/v2/achievements/categories?ids=88&v=2022-03-23T19%3A00%3A00.000Z&lang=en&ae_refresh=1234");
    using nlohmann::json;
    const auto oldCategory=json::parse(R"({"id":88,"achievements":[101,102]})");
    const auto newCategory=json::parse(R"([{"id":88,"achievements":[{"id":101},{"id":103},{"id":103}]}])");
    assert(CategoryIds(oldCategory,88)==std::vector<uint32_t>({101,102}));
    assert(CategoryIds(newCategory,88)==std::vector<uint32_t>({101,103}));
    assert(LooksUnrotated({101,102},CategoryIds(oldCategory,88)));
    assert(!LooksUnrotated({101,102},CategoryIds(newCategory,88)));
    assert(CategoryIds(oldCategory,475).empty());
    assert(CategoryIds(json::parse(R"({"id":88,"achievements":[101,"bad"]})"),88).empty());
    assert(CategoryIds(json::parse(R"({"id":88,"achievements":[0,1]})"),88).empty());
    assert(CategoryIds(json::parse(R"({"id":88,"achievements":[-1]})"),88).empty());
    assert(CategoryIds(json::parse(R"([{"id":88,"achievements":[1]},{"id":88,"achievements":[2]}])"),88).empty());
    assert(RetryDelaySeconds(1) == 30 && RetryDelaySeconds(2) == 60);
    assert(RetryDelaySeconds(3) == 120 && RetryDelaySeconds(4) == 120);
    assert(RetryDelaySeconds(5) == 120 && RetryDelaySeconds(6) == 120);
    // Repeated failures remain eligible indefinitely; retain the failure-count
    // threshold for the guarded zero-progress Vault rollover fallback.
    RetryState repeated;
    int64_t attemptTime=1000;
    for(int i=1;i<=8;++i){
        repeated.Fail(attemptTime,"held");
        const int64_t delay=i==1?30:i==2?60:120;
        assert(repeated.failures==std::min(i,4));
        assert(repeated.at==attemptTime+delay*1000);
        assert(!repeated.Ready(repeated.at-1)&&repeated.Ready(repeated.at));
        attemptTime=repeated.at;
    }
    repeated.Reset();assert(repeated.failures==0&&repeated.at==0);
    constexpr int64_t nov21 = 1795219200;
    assert(BountySet(nov21) == 1);
    // Sun Nov 22: game day has already rolled over at 7 PM Eastern,
    // but next reset is Monday 07:30 UTC; show only Sun/Mon before that.
    constexpr int64_t nov22 = nov21 + kDay;
    constexpr int64_t nov23 = nov22 + kDay;
    const int64_t weeklyReset = nov23 + 7 * 3600 + 30 * 60;
    assert(BountySet(nov22 - 1) == BountySet(nov21));
    assert(BountySet(nov22) == 2);
    // The calendar anchor can drift; derive the provisional next set from a
    // validated prior set rather than claiming the date anchor is always right.
    auto* forecast = NextFractalsFromLive(
        {"Aetherblade", "Chaos", "Nightmare"});
    assert(forecast != nullptr);
    assert(std::string(forecast->dailies[0]) == "Cliffside");
    assert(std::string(forecast->dailies[1]) == "Lonely Tower");
    assert(std::string(forecast->dailies[2]) == "Kinfall");
    assert(NextFractalsFromLive({}) == nullptr);
    assert(std::string(BountyName(BountySet(nov22), 0)) == "Fraenir of Jormag");
    assert(std::string(BountyName(BountySet(nov22), 1)) == "Whisper of Jormag");
    assert(std::string(BountyName(BountySet(nov22), 2)) == "Xera");
    assert(std::string(BountyName(BountySet(nov22), 3)) == "Soulless Horror");
    assert(std::string(BountyName(BountySet(nov23), 0)) == "Gorseval");
    assert(std::string(BountyName(BountySet(nov23), 1)) == "Vale Guardian");
    assert(std::string(BountyName(BountySet(nov23), 2)) == "Samarog");
    assert(std::string(BountyName(BountySet(nov23), 3)) == "Harvest Temple");
    // Weekly completion has no role in the schedule; today's bounty retains its date.
    assert(NextBountyDate("Xera", nov22 + 3600, weeklyReset) == "Sun Nov 22");
    assert(NextBountyDate("Gorseval", nov22 + 3600, weeklyReset) == "Mon Nov 23");
    assert(NextBountyDate("Gorseval", nov23 + 3600, weeklyReset) == "Mon Nov 23");
    assert(NextBountyDate("Gorseval", weeklyReset, weeklyReset).empty());
    // The first day after the weekly reset must never leak into this week.
    assert(NextBountyDate("Cairn", nov22 + 3600, weeklyReset).empty());
    assert(std::string(BountyName(BountySet(nov21), 1)) == "Cardinal Sabir");
    assert(std::string(BountyName(BountySet(nov21 + kDay), 2)) == "Xera");
    assert(NextBountyDate("Matthias", nov21 + 3600, nov21 + 2 * kDay + 27000) == "Sat Nov 21");
    assert(NextBountyDate("Matthias", nov21 + 3600, nov21 + 2 * kDay + 27000) == "Sat Nov 21");
    assert(NextBountyDate("Vale Guardian", nov21 + 3600, nov21 + 2 * kDay).empty());
    assert(NextBountyDate("Vale Guardian", nov21 + 3600, nov21 + 3 * kDay + 27000) == "Mon Nov 23");
    assert(NextBountyDate("Sloth", nov21 + 3600, nov21 + 2 * kDay + 27000).empty());
    assert(NextBountyDate("Voice of the Fallen and Claw of the Fallen",
                          nov21, nov21 + kDay) == "Sat Nov 21");
    assert(FractalSetIndex(nov21) == 9);
    assert(std::string(Fractals(FractalSetIndex(nov21)).dailies[0]) == "Aquatic Ruins");
    assert(std::string(NextFractals(nov21).dailies[0]) == "Sunqua Peak");
    assert(NextFractals(nov21).recScales[2] == 59);
    assert(FractalSetIndex(nov21 + 15 * kDay) == FractalSetIndex(nov21));
    // Never display a misleading next-day forecast when the current
    // schedule differs from a hard-coded date anchor.
    assert(FindDailyFractalSet({"Aquatic Ruins", "Lonely Tower",
                                "Thaumanova Reactor"}) == 9);
    assert(FindDailyFractalSet({"Urban Battleground", "Sunqua Peak",
                                "Underground Facility"}) == 10);
    assert(FindDailyFractalSet({"Sunqua Peak", "Sunqua Peak", "Urban Battleground"}) == -1);
    assert(FindDailyFractalSet({"Sunqua Peak", "Unknown", "Urban Battleground"}) == -1);
    assert(FindDailyFractalSet({"Sunqua Peak", "Urban Battleground"}) == -1);
    auto next = NextFractalsFromLive({"Sunqua Peak", "Underground Facility",
                                     "Urban Battleground"});
    assert(next != nullptr);
    assert(std::string(next->dailies[0]) == "Aetherblade");
    assert(NextFractalsFromLive({"Unrecognized", "Example", "Fractal"}) == nullptr);
    assert(DateLabel(nov21) == "11/21");
    assert(BountyDateLabel(nov21) == "Sat Nov 21");
    assert(BountyDateLabel(nov21 + 2 * kDay) == "Mon Nov 23");
    // 2026 Solitary Throne reshuffle and late-tier scale shifts.
    assert(std::string(FractalScales::NameForScale(19)) == "Solitary Throne");
    assert(std::string(FractalScales::NameForScale(44)) == "Solitary Throne");
    assert(std::string(FractalScales::NameForScale(64)) == "Solitary Throne");
    assert(std::string(FractalScales::NameForScale(94)) == "Solitary Throne");
    assert(std::string(FractalScales::NameForScale(89)) == "Molten Boss");
    assert(std::string(FractalScales::NameForScale(90)) == "Captain Mai Trin Boss");
    assert(std::string(FractalScales::NameForScale(91)) == "Volcanic");
    assert(std::string(FractalScales::NameForScale(92)) == "Aetherblade");
    assert(FractalScales::NameForScale(0) == nullptr);
    assert(FractalScales::NameForScale(101) == nullptr);
    assert(FractalScales::ForTier("Aetherblade", 1) == std::vector<int>{14});
    assert(FractalScales::ForTier("Aetherblade", 4) == std::vector<int>{92});
    assert(FractalScales::ForTier("Solitary Throne", 3) == std::vector<int>{64});
    assert(FractalScales::ForTier("Volcanic", 1) == std::vector<int>{1});
    // Current rotation's recommended scales and names must remain aligned.
    for (int i = 0; i < 15; ++i) {
        const auto& set = Fractals(i);
        for (int j = 0; j < 3; ++j) {
            const char* actual = FractalScales::NameForScale(set.recScales[j]);
            assert(actual != nullptr);
            assert(FractalScales::Normalized(actual) ==
                   FractalScales::Normalized(set.recNames[j]));
        }
    }
    assert(FractalScales::DailyTooltip("Sunqua Peak").find("T4 (98)") != std::string::npos);
    for (int scale = 1; scale <= 100; ++scale)
        assert(FractalScales::NameForScale(scale) != nullptr);
    return 0;
}
