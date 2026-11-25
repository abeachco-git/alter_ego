#pragma once
// Authoritative Fractal of the Mists scale map (November 2026).
// Source: https://wiki.guildwars2.com/wiki/Template:Fractal_info
// Each tier currently has 25 distinct fractals; lookup intentionally returns
// ALL matching scales so future duplicate scales can be displayed accurately.
#include <cctype>
#include <string>
#include <vector>

namespace FractalScales {
inline constexpr const char* kNames[101] = {
    nullptr,
#include "WikiFractalScales.inc"
};
inline const char* NameForScale(int scale) {
    return scale >= 1 && scale <= 100 ? kNames[scale] : nullptr;
}
inline std::string Normalized(const std::string& name) {
    std::string result;
    for (unsigned char c : name)
        if (std::isalnum(c)) result.push_back(static_cast<char>(std::tolower(c)));
    return result;
}
inline std::vector<int> ForTier(const std::string& name, int tier) {
    std::vector<int> scales;
    if (tier < 1 || tier > 4) return scales;
    const std::string target = Normalized(name);
    if (target.empty()) return scales;
    for (int scale = (tier-1)*25 + 1; scale <= tier*25; ++scale)
        if (Normalized(kNames[scale]) == target) scales.push_back(scale);
    return scales;
}
inline std::string TierScales(const std::string& name, int tier) {
    const auto scales = ForTier(name, tier);
    std::string out = "T" + std::to_string(tier) + " (";
    if (scales.empty()) return out + "unknown)";
    for (size_t i = 0; i < scales.size(); ++i) {
        if (i) out += ", ";
        out += std::to_string(scales[i]);
    }
    return out + ")";
}
inline std::string DailyTooltip(const std::string& name) {
    std::string out = "Daily - " + name;
    for (int tier = 1; tier <= 4; ++tier)
        out += "\n" + TierScales(name, tier);
    return out;
}
} // namespace FractalScales
