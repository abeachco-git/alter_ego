#include "../src/BuildCompareCore.h"
#include <cassert>
int main() {
    AlterEgo::BuildComparison c;
    c.Add("Skills", "Heal", "100", "100", "Skill (name available)", "#100 (name missing)");
    c.Add("Gear", "Helm stat", "161", "168", "Berserker's", "Viper's");
    assert(c.Rows().size() == 2);
    assert(c.DifferenceCount() == 1);
    assert(c.Rows()[0].matches && !c.Rows()[1].matches);

    // Regressions: a shared relic is visible in every equipment tab and must
    // be captured there; ordinary gear and gathering tools must not leak in.
    using AlterEgo::IsEquipmentInCapturedBuild;
    assert(IsEquipmentInCapturedBuild("Relic", 0, 1));
    assert(IsEquipmentInCapturedBuild("Relic", 0, 4));
    assert(IsEquipmentInCapturedBuild("Relic", 3, 3));
    assert(!IsEquipmentInCapturedBuild("Relic", 3, 1));
    assert(IsEquipmentInCapturedBuild("Coat", 2, 2));
    assert(!IsEquipmentInCapturedBuild("Coat", 0, 2));
    assert(!IsEquipmentInCapturedBuild("PowerCore", 0, 1));
    assert(!IsEquipmentInCapturedBuild("Sickle", 0, 1));

    // A true mismatch must be explicit; missing or incomplete data must not
    // be presented as an equipment mismatch.
    using AlterEgo::GearMatchStatus;
    using AlterEgo::ClassifyGearMatch;
    assert(ClassifyGearMatch(true, true, 3, false, true) == GearMatchStatus::Different);
    assert(ClassifyGearMatch(true, true, 3, true, true) == GearMatchStatus::Matches);
    assert(ClassifyGearMatch(true, true, 3, true, false) == GearMatchStatus::NotVerified);
    assert(ClassifyGearMatch(true, true, 0, true, false) == GearMatchStatus::NotVerified);
    assert(ClassifyGearMatch(false, true, 0, true, false) == GearMatchStatus::NoSavedGear);
    assert(ClassifyGearMatch(true, false, 0, true, false) == GearMatchStatus::NoRetrievedGear);
}
