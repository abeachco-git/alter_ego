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
    assert(WeaponEligible("FutureSlot", false, false, false));
    return 0;
}
