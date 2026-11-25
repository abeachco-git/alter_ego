#pragma once

#include <string_view>

namespace AlterEgo::BuildRelay {

// Developer-only feature flag. Change this single value and rebuild to restore
// the legacy mobile relay, its import guidance and all share entry points.
inline constexpr bool Enabled = false;

constexpr bool HasCodeShape(std::string_view value) {
    if (value.size() != 4) return false;
    for (char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z'))) return false;
    }
    return true;
}

constexpr bool AcceptsCode(std::string_view value, bool enabled = Enabled) {
    return enabled && HasCodeShape(value);
}

constexpr const char* ImportHint(bool enabled = Enabled) {
    return enabled
        ? "GW2 build or wardrobe chat link, AE2 or one-time code\nSnow Crows or MetaBattle build URL"
        : "GW2 build or wardrobe chat link, AE2 code\nSnow Crows or MetaBattle build URL";
}

constexpr const char* ImportTooltip(bool enabled = Enabled) {
    return enabled
        ? "GW2 build or wardrobe chat link, AE2 or one-time code, Snow Crows or MetaBattle URL"
        : "GW2 build or wardrobe chat link, AE2 code, Snow Crows or MetaBattle URL";
}

constexpr const char* EmptyImportError(bool enabled = Enabled) {
    return enabled
        ? "Paste a GW2 build, wardrobe link, AE2 or one-time code, or source URL."
        : "Paste a GW2 build, wardrobe link, AE2 code, or source URL.";
}

constexpr const char* ImportNameLabel(bool enabled = Enabled) {
    return enabled
        ? "Name (optional for wardrobe / source URLs / AE2 / one-time codes)"
        : "Name (optional for wardrobe / source URLs / AE2)";
}

} // namespace AlterEgo::BuildRelay
