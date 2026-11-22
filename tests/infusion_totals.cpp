#include "../src/InfusionTotalsCore.h"
#include <cassert>
using namespace AlterEgo;
int main() {
    assert(AgonyRequirement(162) == 150);
    assert(AgonyRequirement(135) == 135);
    assert(AgonyRequirement(-1) == 0);
    assert(ParseAgonyResistance("+9 Agony Resistance") == 9);
    assert(ParseAgonyResistance("Grants +12 Agony Resistance") == 12);
    assert(ParseAgonyResistance("Agony Infusion +9") == 9);
    assert(ParseAgonyResistance("Power +5") == 0);
    InfusionSummary saved{true, true, 162, {{"Power", 25}, {"Precision", 20}}};
    InfusionSummary live{true, true, 150, {{"Power", 25}, {"Precision", 15}}};
    auto warnings = CompareInfusionTotals(saved, live);
    assert(warnings.size() == 1 && warnings[0].attribute == "Precision");
    live.agonyResistance = 149;
    warnings = CompareInfusionTotals(saved, live);
    assert(warnings.size() == 2 && warnings[0].attribute == "Agony Resistance");
    saved.agonyResistance = 135; live.agonyResistance = 135;
    live.stats["Precision"] = 20;
    assert(CompareInfusionTotals(saved, live).empty());
    saved.present = false;
    assert(CompareInfusionTotals(saved, live).empty());
    saved.present = true; saved.complete = false;
    assert(CompareInfusionTotals(saved, live).empty());
    return 0;
}
