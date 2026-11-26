#pragma once
// Shared profession weapon eligibility for the original Loadout editor and
// the standalone Weapons component editor.
#include <string>
namespace AlterEgo { namespace EquipmentChoice {
inline bool IsAquaticWeaponSlot(const std::string& slot) {
    return slot=="WeaponAquaticA"||slot=="WeaponAquaticB"||slot=="AquaticA"||slot=="AquaticB";
}
inline bool IsAquaticGearSlot(const std::string& slot) {
    return slot=="HelmAquatic"||IsAquaticWeaponSlot(slot);
}
inline bool WeaponEligible(const std::string& slot, bool mainhand,
                           bool offhand, bool twoHanded) {
    if (slot == "WeaponA1" || slot == "WeaponB1")
        return mainhand || twoHanded;
    if (slot == "WeaponA2" || slot == "WeaponB2")
        return offhand;
    // Aquatic and future slot classes may have different restrictions.
    return true;
}
inline bool WeaponTypeEligible(const std::string& slot,const std::string& type,
    bool mainhand,bool offhand,bool twoHanded) {
    const bool waterOnly=type=="Harpoon"||type=="Speargun"||type=="Trident";
    if(IsAquaticWeaponSlot(slot))return waterOnly;
    if(waterOnly)return false;
    return WeaponEligible(slot,mainhand,offhand,twoHanded);
}
} }
