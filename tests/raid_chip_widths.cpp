#include "../src/RaidChipWidths.h"
#include <cassert>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>
using AlterEgo::RaidChipWidths::Calculate;
static float Sum(const std::vector<float>& v) {
    return std::accumulate(v.begin(), v.end(), 0.0f);
}
int main() {
    const std::vector<std::string> labels = {
        "Soulless Horror", "River", "Broken King", "Eater", "Eyes", "Dhuum"
    };
    // At normal widths only the two bosses grow; the four secondary chips
    // use the same compact fixed targets even when the window resizes.
    const auto normal = Calculate(9111, labels, 930.0f, 4.0f);
    const auto wide = Calculate(9111, labels, 1150.0f, 4.0f);
    assert(normal.size() == 6 && wide.size() == 6);
    assert(std::abs(normal[0] - normal[5]) < 0.01f);
    assert(std::abs(wide[0] - wide[5]) < 0.01f);
    for (size_t i=1; i<=4; ++i) assert(std::abs(normal[i] - wide[i]) < 0.01f);
    assert(normal[1] == 72.0f && normal[2] == 115.0f);
    assert(normal[3] == 70.0f && normal[4] == 68.0f);
    assert(normal[0] > normal[2]);
    assert(std::abs(Sum(normal) + 5*4 - 930.0f) < 0.1f);
    assert(std::abs(Sum(wide) + 5*4 - 1150.0f) < 0.1f);
    // Below the target width the short chips shrink together, but the
    // two large chips stay equal rather than clipping their neighbors.
    const auto narrow = Calculate(9111, labels, 550.0f, 4.0f);
    assert(std::abs(narrow[0] - narrow[5]) < 0.01f);
    assert(narrow[0] >= 169.9f);
    assert(narrow[1] < normal[1] && narrow[2] < normal[2]);
    assert(std::abs(Sum(narrow) + 5*4 - 550.0f) < 0.1f);
    // Tiny sizes still leave nonzero hit targets for the short encounters.
    const auto tiny = Calculate(9111, labels, 250.0f, 4.0f);
    assert(std::abs(tiny[0]-tiny[5]) < 0.01f);
    for (float chip : tiny) assert(chip > 0.0f);
    assert(std::abs(Sum(tiny) + 5*4 - 250.0f) < 0.1f);
    // Responsive Hide Complete layout retains equal boss widths.
    const auto filtered = Calculate(9111, {"Dhuum","Eyes","Soulless Horror"}, 600.0f, 4.0f);
    assert(filtered.size() == 3 && std::abs(filtered[0]-filtered[2]) < 0.01f);
    // With both bosses hidden, the short chips occupy all available space.
    const auto secondaries = Calculate(9111, {"River","Eater"}, 300.0f, 4.0f);
    assert(std::abs(Sum(secondaries) + 4.0f - 300.0f) < 0.1f);
    // All other wings preserve equal-width chips.
    const auto regular = Calculate(9128, labels, 930.0f, 4.0f);
    for (size_t i=1; i<regular.size(); ++i)
        assert(std::abs(regular[0]-regular[i]) < 0.01f);
    assert(Calculate(9111, {}, 100.0f, 4.0f).empty());
}
