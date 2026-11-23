#pragma once
// Shared profession weapon eligibility for the original Loadout editor and
// the standalone Weapons component editor.
#include <string>
namespace AlterEgo { namespace EquipmentChoice {
inline bool WeaponEligible(const std::string& slot, bool mainhand,
                           bool offhand, bool twoHanded) {
    if (slot == "WeaponA1" || slot == "WeaponB1")
        return mainhand || twoHanded;
    if (slot == "WeaponA2" || slot == "WeaponB2")
        return offhand;
    // Aquatic and future slot classes may have different restrictions.
    return true;
}
} }
