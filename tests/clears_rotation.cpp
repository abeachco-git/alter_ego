#include "../src/ClearsRotation.h"
#include "../src/FractalScales.h"
#include <cassert>
#include <string>
int main() {
    using namespace ClearsRotation;
    constexpr int64_t nov21 = 1795219200;
    assert(BountySet(nov21) == 1);
    assert(std::string(BountyName(BountySet(nov21), 1)) == "Cardinal Sabir");
    assert(std::string(BountyName(BountySet(nov21 + kDay), 2)) == "Xera");
    assert(NextBountyDate("Matthias", nov21 + 3600, nov21 + 2 * kDay + 27000, false) == "11/21");
    assert(NextBountyDate("Matthias", nov21 + 3600, nov21 + 2 * kDay + 27000, true).empty());
    assert(NextBountyDate("Vale Guardian", nov21 + 3600, nov21 + 2 * kDay, false).empty());
    assert(NextBountyDate("Vale Guardian", nov21 + 3600, nov21 + 3 * kDay + 27000, false) == "11/23");
    assert(NextBountyDate("Sloth", nov21 + 3600, nov21 + 2 * kDay + 27000, false).empty());
    assert(NextBountyDate("Voice of the Fallen and Claw of the Fallen",
                          nov21, nov21 + kDay, false) == "11/21");
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
    assert(FractalScales::DailyTooltip("Sunqua Peak").find("T4 (98)") != std::string::npos);
    for (int scale = 1; scale <= 100; ++scale)
        assert(FractalScales::NameForScale(scale) != nullptr);
}
