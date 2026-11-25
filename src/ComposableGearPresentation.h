#pragma once
// Pure presentation helpers shared by the standalone component editor and
// the read-only Components inventory. No dependency on ImGui or Windows.
// Ordering never changes stored JSON, and summaries never invent missing data.
#include "ComposableLoadouts.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace AlterEgo { namespace Composable { namespace Presentation {

inline const std::array<const char*, 19>& StandardGearSlots() {
    static const std::array<const char*, 19> slots = {{
        "Helm", "Shoulders", "Coat", "Gloves", "Leggings", "Boots",
        "WeaponA1", "WeaponA2", "WeaponB1", "WeaponB2",
        "AquaticA", "AquaticB", "Backpack", "Accessory1", "Accessory2",
        "Amulet", "Ring1", "Ring2", "Relic"
    }};
    return slots;
}

inline std::vector<std::string> SortedGearSlots(const Json& gear) {
    std::vector<std::string> result;
    if (!gear.is_object()) return result;
    result.reserve(gear.size());
    for (const char* slot : StandardGearSlots()) {
        if (gear.contains(slot)) result.emplace_back(slot);
    }
    // Unfamiliar slots are *visible* and round-trip unchanged.
    for (auto it = gear.begin(); it != gear.end(); ++it) {
        if (std::find(result.begin(), result.end(), it.key()) == result.end())
            result.push_back(it.key());
    }
    return result;
}

inline std::string SafeNameOrId(const Json& value, const char* nameKey,
                                const char* idKey) {
    const auto name = value.find(nameKey);
    if (name != value.end() && name->is_string() && !name->get<std::string>().empty())
        return name->get<std::string>();
    const auto id = value.find(idKey);
    if (id != value.end() && id->is_number_integer() && *id != 0)
        return "#" + id->dump();
    return {};
}

inline std::string GearSlotSummary(const std::string& kind, const Json& slot) {
    if (!slot.is_object()) return "Unknown imported representation (preserved)";
    std::vector<std::string> pieces;
    auto add = [&](const std::string& label, const std::string& value) {
        if (!value.empty()) pieces.push_back(label + value);
    };
    if (kind == "armor" || kind == "weapons" || kind == "trinkets") {
        add("Item ", SafeNameOrId(slot, "item_name", "item_id"));
        add("Stats ", SafeNameOrId(slot, "stat_name", "stat_id"));
        const auto weapon = slot.find("weapon_type");
        if (weapon != slot.end() && weapon->is_string() && !weapon->get<std::string>().empty())
            pieces.push_back(weapon->get<std::string>());
    }
    if (kind == "runes") add("Rune ", SafeNameOrId(slot, "rune", "rune_id"));
    if (kind == "sigils") {
        add("Sigil ", SafeNameOrId(slot, "sigil", "sigil_id"));
        add("Sigil 2 ", SafeNameOrId(slot, "sigil2", "sigil2_id"));
    }
    if (kind == "infusions") {
        const auto ids = slot.find("infusion_ids");
        if (ids != slot.end() && ids->is_array())
            pieces.push_back(std::to_string(ids->size()) + " infusion positions");
        const auto snapshot = slot.find("infusion_snapshot");
        if (snapshot != slot.end() && snapshot->is_boolean() && *snapshot)
            pieces.push_back("captured snapshot");
        const auto legacy = slot.find("infusion");
        if (legacy != slot.end() && legacy->is_string() && !legacy->get<std::string>().empty())
            pieces.push_back(legacy->get<std::string>());
    }
    // Unknown fields are accessible in the exact-details disclosure. Do not
    // represent the absence of known fields as proof that a slot is empty.
    if (pieces.empty()) return slot.empty()
        ? "(empty recorded slot)" : "Additional imported fields (preserved)";
    std::string out;
    for (const auto& piece : pieces) {
        if (!out.empty()) out += " | ";
        out += piece;
    }
    return out;
}

} } } // namespace AlterEgo::Composable::Presentation
