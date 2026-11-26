#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include "EquipmentChoiceCore.h"

namespace AlterEgo { namespace GearEdit {
enum class CopyField { All, Stats, Upgrade, Infusions, Relic };
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
// Empty placeholders do not count as configured aquatic gear.
template<class Slot> bool HasRecordedGear(const Slot& s) {
    return s.item_id||s.stat_id||s.rune_id||s.sigil_id||s.sigil2_id||
        !s.stat_name.empty()||!s.rune.empty()||!s.sigil.empty()||!s.sigil2.empty()||
        !s.weapon_type.empty()||!s.infusion.empty()||
        std::any_of(s.infusion_ids.begin(),s.infusion_ids.end(),[](auto id){return id!=0;})||
        std::any_of(s.infusion_attributes.begin(),s.infusion_attributes.end(),[](const auto& a){return !a.empty();});
}
template<class Gear> bool HasRecordedAquaticGear(const Gear& gear) {
    for(const auto& [slot,value]:gear)
        if(EquipmentChoice::IsAquaticGearSlot(slot)&&HasRecordedGear(value))return true;
    return false;
}
template<class Gear> std::vector<std::string> BulkSlots(const Gear& gear) {
    std::vector<std::string> slots={"Helm","Shoulders","Coat","Gloves","Leggings","Boots",
        "WeaponA1","WeaponA2","WeaponB1","WeaponB2","Backpack","Accessory1",
        "Accessory2","Amulet","Ring1","Ring2"};
    for(const auto& [slot,value]:gear)
        if(EquipmentChoice::IsAquaticGearSlot(slot)&&HasRecordedGear(value))slots.push_back(slot);
    return slots;
}
inline int InfusionCapacity(const std::string& slot, bool twoHanded, bool blocked = false) {
    if (blocked || slot == "Amulet" || slot == "Relic") return 0;
    if (slot == "Ring1" || slot == "Ring2") return 3;
    if (slot == "Backpack") return 2;
    if (slot.rfind("Weapon",0) == 0 || EquipmentChoice::IsAquaticWeaponSlot(slot)) return twoHanded ? 2 : 1;
    return 1;
}
template<class T> std::vector<T> FillPattern(const std::vector<T>& pattern, int capacity) {
    std::vector<T> result;
    if (pattern.empty()) return result;
    for (int i = 0; i < capacity; ++i)
        result.push_back(pattern[static_cast<size_t>(i) % pattern.size()]);
    return result;
}
// Removal is an explicit recorded empty snapshot, not unknown data.
template<class Slot> bool RemoveInfusion(Slot& slot, size_t index) {
    if(index>=slot.infusion_ids.size())return false;
    slot.infusion_ids.erase(slot.infusion_ids.begin()+index);
    if(index<slot.infusion_attributes.size())
        slot.infusion_attributes.erase(slot.infusion_attributes.begin()+index);
    if(!slot.infusion_attributes.empty())
        slot.infusion_attributes.resize(slot.infusion_ids.size());
    slot.infusion_snapshot=true;
    slot.infusion.clear();
    return true;
}
// Keep selection on the same surviving requirement where possible.
inline int InfusionSelectionAfterRemoval(int selected,size_t removed,size_t remaining) {
    if(!remaining)return -1;
    if(selected<0)return -1;
    if(static_cast<size_t>(selected)>removed)--selected;
    return std::min(selected,static_cast<int>(remaining-1));
}
// An empty snapshot is an explicit requirement; missing data must not clear
// another slot. An unknown socket count is not permission to fill the maximum.
template<class Slot> bool HasInfusionRequirements(const Slot& source) {
    return source.infusion_snapshot ||
        std::any_of(source.infusion_ids.begin(),source.infusion_ids.end(),[](auto id){return id!=0;}) ||
        std::any_of(source.infusion_attributes.begin(),source.infusion_attributes.end(),[](const auto& a){return !a.empty();});
}
template<class Slot> bool CopyInfusions(Slot& dest, const Slot& source, int capacity, bool capacityVerified=true) {
    if(!HasInfusionRequirements(source))return false;
    using Id = typename decltype(source.infusion_ids)::value_type;
    std::vector<Id> ids;
    std::vector<std::string> attrs;
    const size_t n = std::max(source.infusion_ids.size(), source.infusion_attributes.size());
    for (size_t i = 0; i < n; ++i) {
        const Id id = i < source.infusion_ids.size() ? source.infusion_ids[i] : 0;
        const std::string attr = i < source.infusion_attributes.size() ? source.infusion_attributes[i] : "";
        if (id || !attr.empty()) { ids.push_back(id); attrs.push_back(attr); }
    }
    if(!capacityVerified && ids.size()<static_cast<size_t>(std::max(0,capacity)))
        capacity=static_cast<int>(ids.size());
    dest.infusion_ids = FillPattern(ids, capacity);
    dest.infusion_attributes = FillPattern(attrs, capacity);
    dest.infusion_snapshot = true;
    dest.infusion.clear();
    return true;
}
template<class Slot> void CopyStats(Slot& dest,const Slot& source){
    dest.stat_id=source.stat_id;dest.stat_name=source.stat_name;
}
template<class Slot> void CopyRune(Slot& dest,const Slot& source){
    dest.rune_id=source.rune_id;dest.rune=source.rune;
}
template<class Slot> void ClearInfusions(Slot& dest){
    dest.infusion_ids.clear();dest.infusion_attributes.clear();dest.infusion.clear();dest.infusion_snapshot=true;
}
template<class Slot> void CopySigils(Slot& dest, const Slot& source, int capacity) {
    dest.sigil = capacity > 0 ? source.sigil : "";
    dest.sigil_id = capacity > 0 ? source.sigil_id : 0;
    const bool second = source.sigil2_id || !source.sigil2.empty();
    dest.sigil2 = capacity > 1 ? (second ? source.sigil2 : source.sigil) : "";
    dest.sigil2_id = capacity > 1 ? (second ? source.sigil2_id : source.sigil_id) : 0;
}
} }
