#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace AlterEgo { namespace GearEdit {
// A child gear editor works on a private draft. Applying it updates only
// equipment fields, preserving concurrent metadata/build edits on the parent.
template<class Build> void ApplyEquipmentDraft(Build& parent,const Build& draft) {
    parent.gear=draft.gear;
    parent.infusion_totals=draft.infusion_totals;
    parent.infusion_summary_present=draft.infusion_summary_present;
    parent.infusion_ar_total=draft.infusion_ar_total;
    parent.infusion_stat_totals=draft.infusion_stat_totals;
    parent.rune_name=draft.rune_name;parent.rune_id=draft.rune_id;
    parent.relic_name=draft.relic_name;parent.relic_id=draft.relic_id;
}
inline int InfusionCapacity(const std::string& slot, bool twoHanded, bool blocked = false) {
    if (blocked || slot == "Amulet" || slot == "Relic") return 0;
    if (slot == "Ring1" || slot == "Ring2") return 3;
    if (slot == "Backpack") return 2;
    if (slot.rfind("Weapon",0) == 0) return twoHanded ? 2 : 1;
    return 1;
}
template<class T> std::vector<T> FillPattern(const std::vector<T>& pattern, int capacity) {
    std::vector<T> result;
    if (pattern.empty()) return result;
    for (int i = 0; i < capacity; ++i)
        result.push_back(pattern[static_cast<size_t>(i) % pattern.size()]);
    return result;
}
template<class Slot> void CopyInfusions(Slot& dest, const Slot& source, int capacity) {
    using Id = typename decltype(source.infusion_ids)::value_type;
    std::vector<Id> ids;
    std::vector<std::string> attrs;
    const size_t n = std::max(source.infusion_ids.size(), source.infusion_attributes.size());
    for (size_t i = 0; i < n; ++i) {
        const Id id = i < source.infusion_ids.size() ? source.infusion_ids[i] : 0;
        const std::string attr = i < source.infusion_attributes.size() ? source.infusion_attributes[i] : "";
        if (id || !attr.empty()) { ids.push_back(id); attrs.push_back(attr); }
    }
    dest.infusion_ids = FillPattern(ids, capacity);
    dest.infusion_attributes = FillPattern(attrs, capacity);
    dest.infusion_snapshot = true;
    dest.infusion.clear();
}
template<class Slot> void CopySigils(Slot& dest, const Slot& source, int capacity) {
    dest.sigil = capacity > 0 ? source.sigil : "";
    dest.sigil_id = capacity > 0 ? source.sigil_id : 0;
    const bool second = source.sigil2_id || !source.sigil2.empty();
    dest.sigil2 = capacity > 1 ? (second ? source.sigil2 : source.sigil) : "";
    dest.sigil2_id = capacity > 1 ? (second ? source.sigil2_id : source.sigil_id) : 0;
}
} }
