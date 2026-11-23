#include "../src/ClearsRotation.h"
#include "../src/ClearsReset.h"
#include "../src/FractalScales.h"
#include <cassert>
#include <string>
int main() {
    using namespace ClearsRotation;
    using namespace ClearsReset;
    // At 00:00 UTC we must NOT consume the reset marker before the API grace
    // period. This was the cause of Vault + daily Fractals missing rollover.
    constexpr int64_t day = 86400, reset = 1795305600;
    assert(InitialMarker(reset, reset + 15, day) == reset - day);
    assert(InitialMarker(reset, reset + 61, day) == reset);
    assert(!Due(reset - day, reset, reset + 59));
    assert(Due(reset - day, reset, reset + 60));
    assert(!Due(reset, reset, reset + 3600));
    assert(LooksUnrotated({101, 102}, {102, 101}));
    assert(!LooksUnrotated({101, 102}, {101, 103}));
    assert(LooksUnrotated({101, 102}, {}));
    assert(!LooksUnrotated({}, {101, 102}));
    // Retry URLs must change even on the same day, otherwise an initially
    // stale max-age=3600 result could be cached for a full hour.
    assert(FreshCategoryUrl(88, 0) ==
           "https://api.guildwars2.com/v2/achievements/categories/88");
    assert(FreshCategoryUrl(88, reset + 61000) ==
           "https://api.guildwars2.com/v2/achievements/categories/88?ae_refresh=" +
           std::to_string(reset + 61000));
    assert(FreshCategoryUrl(475, reset + 61001) !=
           FreshCategoryUrl(475, reset + 61000));
    assert(RetryDelaySeconds(1) == 30 && RetryDelaySeconds(2) == 60);
    assert(RetryDelaySeconds(3) == 120 && RetryDelaySeconds(6) == 300);
    constexpr int64_t nov21 = 1795219200;
    assert(BountySet(nov21) == 1);
    // Sun Nov 22: game day has already rolled over at 7 PM Eastern,
    // but next reset is Monday 07:30 UTC; show only Sun/Mon before that.
    constexpr int64_t nov22 = nov21 + kDay;
    constexpr int64_t nov23 = nov22 + kDay;
    const int64_t weeklyReset = nov23 + 7 * 3600 + 30 * 60;
    assert(BountySet(nov22 - 1) == BountySet(nov21));
    assert(BountySet(nov22) == 2);
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
}
