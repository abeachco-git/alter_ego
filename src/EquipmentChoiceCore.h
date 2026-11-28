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
inline bool WeaponEnvironmentAllowed(const std::string& slot,const std::string& type){
    if(type.empty())return true;
    const bool waterOnly=type=="Harpoon"||type=="Speargun"||type=="Trident";
    return IsAquaticWeaponSlot(slot)==waterOnly;
}
inline bool WeaponTypeEligible(const std::string& slot,const std::string& type,
    bool mainhand,bool offhand,bool twoHanded) {
    if(!WeaponEnvironmentAllowed(slot,type))return false;
    return WeaponEligible(slot,mainhand,offhand,twoHanded);
}
// Physical weapon families are independent of profession eligibility.
struct PhysicalWeaponType {const char* name;bool mainhand,offhand,twoHanded;};
inline constexpr PhysicalWeaponType PhysicalWeaponTypes[]={
    {"Axe",true,true,false},{"Dagger",true,true,false},{"Focus",false,true,false},
    {"Greatsword",false,false,true},{"Hammer",false,false,true},{"Harpoon",false,false,true},
    {"Longbow",false,false,true},{"Mace",true,true,false},{"Pistol",true,true,false},
    {"Rifle",false,false,true},{"Scepter",true,false,false},{"Shield",false,true,false},
    {"Shortbow",false,false,true},{"Spear",false,false,true},{"Speargun",false,false,true},
    {"Staff",false,false,true},{"Sword",true,true,false},{"Torch",false,true,false},
    {"Trident",false,false,true},{"Warhorn",false,true,false}
};
// A deliberate weapon type change invalidates the old exact physical item.
// Aquatic weapons never remove a terrestrial offhand. A repeated selection
// must not discard captured IDs, upgrades or other recorded fields.
struct WeaponChange {
    bool changed=false;
    bool trimToOne=false;
    std::string displacedOffhand;
};
inline WeaponChange PlanWeaponChange(const std::string& slot,const std::string& current,
    const std::string& next,bool twoHanded) {
    WeaponChange plan;
    if(current==next)return plan;
    plan.changed=true;
    if(next.empty())return plan;
    if(IsAquaticWeaponSlot(slot))return plan;
    plan.trimToOne=!twoHanded;
    if(twoHanded&&slot=="WeaponA1")plan.displacedOffhand="WeaponA2";
    if(twoHanded&&slot=="WeaponB1")plan.displacedOffhand="WeaponB2";
    return plan;
}
} }
