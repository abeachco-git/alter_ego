#pragma once
// Retrieved character equipment tabs; independent of any SavedBuild.
namespace AlterEgo::EquipmentTabCore {
template<class Character>
int Count(const Character& ch) {
    int maxTab = 1;
    for (const auto& item : ch.equipment)
        if (item.tab > maxTab) maxTab = item.tab;
    for (const auto& item : ch.equipment_tab_names)
        if (item.first > maxTab) maxTab = item.first;
    if (ch.active_equipment_tab > maxTab)
        maxTab = ch.active_equipment_tab;
    return maxTab;
}
} // namespace AlterEgo::EquipmentTabCore
