#include "../src/RaidChipWidths.h"
#include <cassert>
#include <cmath>
#include <numeric>
#include <vector>
#include <string>
int main() {
    const std::vector<std::string> labels = {
        "Soulless Horror", "River", "Broken King", "Eater", "Eyes", "Dhuum"
    };
    const auto w5 = AlterEgo::RaidChipWidths::Calculate(9111, labels, 700, 4);
    assert(w5.size() == 6);
    assert(w5[0] > w5[2] && w5[5] > w5[2]);
    assert(w5[1] < w5[0] && w5[3] < w5[5] && w5[4] < w5[5]);
    float sum = std::accumulate(w5.begin(), w5.end(), 0.0f);
    assert(std::abs(sum + 5 * 4 - 700) < 0.1f);
    const auto regular = AlterEgo::RaidChipWidths::Calculate(9128, labels, 700, 4);
    for (size_t i = 1; i < regular.size(); ++i)
        assert(std::abs(regular[0] - regular[i]) < 0.01f);
    const auto filtered = AlterEgo::RaidChipWidths::Calculate(
        9111, {"River", "Dhuum"}, 300, 4);
    assert(filtered[1] > filtered[0]);
    assert(AlterEgo::RaidChipWidths::Calculate(9111, {}, 100, 4).empty());
}
