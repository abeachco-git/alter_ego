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

inline const std::array<const char*, 22>& StandardGearSlots() {
    static const std::array<const char*, 22> slots = {{
        "Helm", "Shoulders", "Coat", "Gloves", "Leggings", "Boots",
        "WeaponA1", "WeaponA2", "WeaponB1", "WeaponB2",
        "Backpack", "Accessory1", "Accessory2", "Amulet", "Ring1", "Ring2", "Relic",
        "HelmAquatic", "WeaponAquaticA", "WeaponAquaticB", "AquaticA", "AquaticB"
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
    const auto id = value.find(idKey);
    if (name != value.end() && name->is_string() && !name->get<std::string>().empty())
        return name->get<std::string>()+(id!=value.end()&&id->is_number_integer()&&*id!=0?" (#"+id->dump()+")":"");
    if (id != value.end() && id->is_number_integer() && *id != 0)
        return "#" + id->dump();
    return {};
}

// Exact positive typed item fields only. Never treat names, stat IDs, palette
// IDs or unknown extension fields as item identities.
inline std::vector<uint32_t> ComponentItemIds(const std::string& kind,const Json& data){
    std::vector<uint32_t> ids;
    auto add=[&](const Json& value){
        if(value.is_number_unsigned()){
            const auto v=value.get<uint64_t>();if(v&&v<=UINT32_MAX)ids.push_back(static_cast<uint32_t>(v));
        }else if(value.is_number_integer()){
            const auto v=value.get<int64_t>();if(v>0&&static_cast<uint64_t>(v)<=UINT32_MAX)ids.push_back(static_cast<uint32_t>(v));
        }
    };
    auto field=[&](const Json& obj,const char* key){
        if(obj.is_object()){auto i=obj.find(key);if(i!=obj.end())add(*i);}
    };
    if(!data.is_object())return ids;
    if(kind=="relic")field(data,"relic_id");
    if(kind=="runes")field(data,"rune_id");
    if(kind=="infusions"){
        auto totals=data.find("infusion_totals");
        if(totals!=data.end()&&totals->is_array())for(const auto& row:*totals)field(row,"item_id");
    }
    auto gear=data.find("gear");
    if(gear!=data.end()&&gear->is_object())for(const auto& slot:*gear){
        if(!slot.is_object())continue;
        if(kind=="armor"||kind=="weapons"||kind=="trinkets"||kind=="aquatic")field(slot,"item_id");
        if(kind=="runes"||kind=="aquatic")field(slot,"rune_id");
        if(kind=="sigils"||kind=="aquatic"){field(slot,"sigil_id");field(slot,"sigil2_id");}
        if(kind=="infusions"||kind=="aquatic"){
            auto inf=slot.find("infusion_ids");
            if(inf!=slot.end()&&inf->is_array())for(const auto& v:*inf)add(v);
        }
    }
    std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    return ids;
}

inline std::vector<uint32_t> ComponentStatIds(const std::string& kind,const Json& data){
    std::vector<uint32_t> ids;
    if(kind!="armor"&&kind!="weapons"&&kind!="trinkets"&&kind!="aquatic")return ids;
    if(!data.is_object())return ids;
    auto gear=data.find("gear");if(gear==data.end()||!gear->is_object())return ids;
    for(const auto& slot:*gear){
        if(!slot.is_object())continue;
        auto value=slot.find("stat_id");uint32_t id=0;
        if(value!=slot.end()&&ReadItemId(*value,id)&&id)ids.push_back(id);
    }
    std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    return ids;
}
inline std::string GearSlotSummary(const std::string& kind, const Json& slot) {
    if (!slot.is_object()) return "Unknown imported representation (preserved)";
    std::vector<std::string> pieces;
    auto add = [&](const std::string& label, const std::string& value) {
        if (!value.empty()) pieces.push_back(label + value);
    };
    if (kind == "armor" || kind == "weapons" || kind == "trinkets" || kind=="aquatic") {
        add("Item ", SafeNameOrId(slot, "item_name", "item_id"));
        add("Stats ", SafeNameOrId(slot, "stat_name", "stat_id"));
        const auto weapon = slot.find("weapon_type");
        if (weapon != slot.end() && weapon->is_string() && !weapon->get<std::string>().empty())
            pieces.push_back(weapon->get<std::string>());
    }
    if (kind == "runes" || kind=="aquatic") add("Rune ", SafeNameOrId(slot, "rune", "rune_id"));
    if (kind == "sigils" || kind=="aquatic") {
        add("Sigil ", SafeNameOrId(slot, "sigil", "sigil_id"));
        add("Sigil 2 ", SafeNameOrId(slot, "sigil2", "sigil2_id"));
    }
    if (kind == "infusions" || kind=="aquatic") {
        const auto ids = slot.find("infusion_ids");
        const auto attrs=slot.find("infusion_attributes");
        const size_t positions=std::max(ids!=slot.end()&&ids->is_array()?ids->size():size_t(0),
            attrs!=slot.end()&&attrs->is_array()?attrs->size():size_t(0));
        if(positions)pieces.push_back(std::to_string(positions)+" infusion positions");
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
