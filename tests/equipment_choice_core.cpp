#include "../src/EquipmentChoiceCore.h"
#include <cassert>
int case_equipment_choice_core(int, char**) {
    using AlterEgo::EquipmentChoice::WeaponEligible;
    assert(WeaponEligible("WeaponA1", true, false, false));
    assert(WeaponEligible("WeaponB1", false, false, true));
    assert(!WeaponEligible("WeaponA1", false, true, false));
    assert(!WeaponEligible("WeaponB2", true, false, false));
    assert(!WeaponEligible("WeaponA2", false, false, true));
    assert(WeaponEligible("WeaponA2", false, true, false));
    assert(WeaponEligible("AquaticA", false, false, false));
    using AlterEgo::EquipmentChoice::WeaponTypeEligible;
    assert(WeaponTypeEligible("WeaponAquaticA","Harpoon",false,false,false));
    assert(!WeaponTypeEligible("WeaponAquaticB","Sword",true,true,false));
    assert(!WeaponTypeEligible("WeaponA1","Trident",false,false,true));
    assert(WeaponTypeEligible("WeaponA1","Spear",false,false,true));
    assert(WeaponEligible("FutureSlot", false, false, false));
    using AlterEgo::EquipmentChoice::PlanWeaponChange;
    assert(!PlanWeaponChange("WeaponA1","Spear","Spear",true).changed);
    const auto land=PlanWeaponChange("WeaponA1","Sword","Spear",true);
    assert(land.changed&&!land.trimToOne&&land.displacedOffhand=="WeaponA2");
    const auto water=PlanWeaponChange("WeaponAquaticA","Trident","Harpoon",false);
    assert(water.changed&&!water.trimToOne&&water.displacedOffhand.empty());
    const auto single=PlanWeaponChange("WeaponB1","Greatsword","Sword",false);
    assert(single.changed&&single.trimToOne&&single.displacedOffhand.empty());
    return 0;
}
