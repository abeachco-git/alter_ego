#pragma once
// Pure width distribution for Weekly Raids encounter chips.
// Hall of Chains assigns extra space to the two major boss names/dates while
// the four shorter traversals/statues use tighter chips.
#include <algorithm>
#include <string>
#include <vector>
namespace AlterEgo {
namespace RaidChipWidths {
inline float Weight(unsigned wingId, const std::string& label) {
    if (wingId != 9111) return 1.0f;
    if (label == "Soulless Horror") return 2.55f;
    if (label == "Dhuum") return 1.95f;
    if (label == "Broken King") return 1.25f;
    if (label == "River") return 0.78f;
    if (label == "Eater") return 0.72f;
    if (label == "Eyes") return 0.70f;
    return 1.0f;
}
inline std::vector<float> Calculate(unsigned wingId,
                                    const std::vector<std::string>& labels,
                                    float width, float gap) {
    if (labels.empty()) return {};
    const float usedGaps = gap * static_cast<float>(labels.size() - 1);
    const float available = std::max(0.0f, width - usedGaps);
    float weightTotal = 0.0f;
    for (const auto& label : labels) weightTotal += Weight(wingId, label);
    std::vector<float> widths;
    widths.reserve(labels.size());
    for (const auto& label : labels)
        widths.push_back(available * Weight(wingId, label) / weightTotal);
    return widths;
}
} // namespace RaidChipWidths
} // namespace AlterEgo
