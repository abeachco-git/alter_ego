#include "../src/ClearsRotation.h"
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
    assert(DateLabel(nov21) == "11/21");
}
