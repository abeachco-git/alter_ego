#pragma once
// Pure, responsive width distribution for Weekly Raids encounter chips.
// W5: four secondary encounters have compact target widths; Soulless Horror
// and Dhuum evenly divide all remaining space. When a window becomes too
// narrow, the compact columns shrink proportionally instead of overflowing.
#include <algorithm>
#include <string>
#include <vector>
namespace AlterEgo {
namespace RaidChipWidths {
inline float CompactWidth(const std::string& label) {
    if (label == "River") return 72.0f;
    if (label == "Broken King") return 115.0f;
    if (label == "Eater") return 70.0f;
    if (label == "Eyes") return 68.0f;
    return 0.0f;
}
inline bool IsWideBoss(const std::string& label) {
    return label == "Soulless Horror" || label == "Dhuum";
}
inline std::vector<float> Calculate(unsigned wingId,
                                    const std::vector<std::string>& labels,
                                    float width, float gap) {
    if (labels.empty()) return {};
    const float usedGaps = gap * static_cast<float>(labels.size() - 1);
    const float available = std::max(0.0f, width - usedGaps);
    const float even = available / static_cast<float>(labels.size());
    if (wingId != 9111)
        return std::vector<float>(labels.size(), even);

    float compactTotal = 0.0f;
    size_t wideCount = 0;
    for (const auto& label : labels) {
        const float target = CompactWidth(label);
        if (target > 0) compactTotal += target;
        else ++wideCount; // both bosses, plus any future unknown encounter
    }
    if (!wideCount) {
        // With Hide Complete there may be only secondary chips.
        // Allow them to use the available row instead of leaving unused space.
        std::vector<float> result;
        result.reserve(labels.size());
        if (compactTotal <= 0) return std::vector<float>(labels.size(), even);
        for (const auto& label : labels)
            result.push_back(available * CompactWidth(label) / compactTotal);
        return result;
    }
    // Reserve more space for boss names and a bounty date. When the window
    // gets very narrow, keep a portion for the secondary chips instead of
    // collapsing their hitboxes to zero.
    constexpr float kPreferredBossMin = 170.0f;
    constexpr float kMinimumCompactFraction = 0.25f;
    const float compactBudget = std::min(compactTotal,
        std::max(available * kMinimumCompactFraction,
                 available - kPreferredBossMin * static_cast<float>(wideCount)));
    const float compactScale = compactTotal > 0
        ? std::min(1.0f, compactBudget / compactTotal) : 0.0f;
    const float bossWidth = std::max(0.0f,
        (available - compactTotal * compactScale) / static_cast<float>(wideCount));
    std::vector<float> result;
    result.reserve(labels.size());
    for (const auto& label : labels) {
        const float compact = CompactWidth(label);
        result.push_back(compact > 0 ? compact * compactScale : bossWidth);
    }
    return result;
}
} // namespace RaidChipWidths
} // namespace AlterEgo
