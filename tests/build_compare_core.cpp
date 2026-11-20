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
}
