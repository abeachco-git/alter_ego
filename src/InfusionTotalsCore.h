#pragma once
// Shared, UI-independent infusion totals and minimum-only comparisons.
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace AlterEgo {
struct InfusionSummary {
    bool present = false;
    bool complete = true; // False if public metadata is unavailable.
    int agonyResistance = 0;
    std::map<std::string, int> stats;
};
inline int AgonyRequirement(int savedTotal) {
    return std::min(150, std::max(0, savedTotal));
}
// GW2 item buffs commonly read "+9 Agony Resistance"; fall back to the
// standard "Agony Infusion +9" item name. Never infer a value from an item ID.
inline int ParseAgonyResistance(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t at = lower.find("agony resistance");
    if (at != std::string::npos) {
        size_t end = at;
        while (end > 0 && std::isspace(static_cast<unsigned char>(lower[end - 1]))) --end;
        size_t begin = end;
        while (begin > 0 && std::isdigit(static_cast<unsigned char>(lower[begin - 1]))) --begin;
        if (begin > 0 && lower[begin - 1] == '+' && begin < end)
            return std::min(1000, std::stoi(lower.substr(begin, end - begin)));
    }
    at = lower.find("agony infusion +");
    if (at != std::string::npos) {
        at += std::string("agony infusion +").size();
        size_t end = at;
        while (end < lower.size() && std::isdigit(static_cast<unsigned char>(lower[end]))) ++end;
        if (end > at) return std::min(1000, std::stoi(lower.substr(at, end - at)));
    }
    return 0;
}
struct InfusionShortfall {
    std::string attribute; // Agony Resistance or canonical attribute name.
    int required = 0;
    int current = 0;
};
inline std::vector<InfusionShortfall> CompareInfusionTotals(
    const InfusionSummary& saved, const InfusionSummary& live) {
    std::vector<InfusionShortfall> issues;
    if (!saved.present || !saved.complete || !live.complete)
        return issues; // Unknown is not a confirmed shortage.
    const int ar = AgonyRequirement(saved.agonyResistance);
    if (ar > live.agonyResistance)
        issues.push_back({"Agony Resistance", ar, live.agonyResistance});
    for (const auto& [attr, total] : saved.stats) {
        if (total <= 0) continue;
        auto it = live.stats.find(attr);
        int actual = it == live.stats.end() ? 0 : it->second;
        if (actual < total) issues.push_back({attr, total, actual});
    }
    return issues;
}
} // namespace AlterEgo
