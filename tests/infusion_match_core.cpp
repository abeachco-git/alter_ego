#include "../src/InfusionMatchCore.h"
#include <cassert>
#include <vector>

using AlterEgo::CompareInfusionRequirements;
using AlterEgo::InfusionMatchVerdict;
using AlterEgo::InfusionRequirement;
using AlterEgo::ObservedInfusion;

int case_infusion_match_core(int, char**) {
    // Item IDs are preserved for exact matches, independent of slot ordering.
    const std::vector<InfusionRequirement> exact = {{111, ""}, {222, ""}};
    assert(CompareInfusionRequirements(exact,
        {{222, {}, false}, {111, {}, false}}) == InfusionMatchVerdict::Matches);
    assert(CompareInfusionRequirements(exact,
        {{111, {}, false}, {333, {}, false}}) == InfusionMatchVerdict::Different);

    // Any positive stat bonus qualifies; exact item ID and cosmetic variant
    // have no effect for an attribute requirement.
    const std::vector<InfusionRequirement> power = {{0, "Power"}};
    assert(CompareInfusionRequirements(power,
        {{999, {"Power"}, true}}) == InfusionMatchVerdict::Matches);
    assert(CompareInfusionRequirements(power,
        {{999, {"AgonyResistance"}, true}}) == InfusionMatchVerdict::Different);

    // No inferred match when the API hasn't returned item attributes.
    assert(CompareInfusionRequirements(power,
        {{777, {}, false}}) == InfusionMatchVerdict::NotVerified);
    assert(CompareInfusionRequirements(power,
        {{0, {}, false}}) == InfusionMatchVerdict::Different);

    // A mixed set must assign each live infusion exactly once.
    const std::vector<InfusionRequirement> mixed = {{111, ""}, {0, "Precision"}};
    assert(CompareInfusionRequirements(mixed,
        {{900, {"Precision"}, true}, {111, {}, false}}) == InfusionMatchVerdict::Matches);
    assert(CompareInfusionRequirements(mixed,
        {{900, {"Power"}, true}, {111, {}, false}}) == InfusionMatchVerdict::Different);
    assert(CompareInfusionRequirements(mixed,
        {{900, {}, false}, {111, {}, false}}) == InfusionMatchVerdict::NotVerified);
    assert(CompareInfusionRequirements(mixed,
        {{111, {}, false}}) == InfusionMatchVerdict::Different);

    // Distinct requirements must not reuse the same live infusion.
    const std::vector<InfusionRequirement> twoPower = {{0, "Power"}, {0, "Power"}};
    assert(CompareInfusionRequirements(twoPower,
        {{31, {"Power"}, true}, {32, {"Power"}, true}}) == InfusionMatchVerdict::Matches);
    assert(CompareInfusionRequirements(twoPower,
        {{31, {"Power"}, true}, {32, {"Precision"}, true}}) == InfusionMatchVerdict::Different);

    // Attribute matching may reuse a retained exact ID when the user switches
    // modes, but remains governed by the attribute requirement.
    const std::vector<InfusionRequirement> retained = {{111, "Power"}};
    assert(CompareInfusionRequirements(retained,
        {{222, {"Power"}, true}}) == InfusionMatchVerdict::Matches);
    assert(AlterEgo::CompareInfusionRequirements({}, {},false)==AlterEgo::InfusionMatchVerdict::NotVerified);
    assert(AlterEgo::CompareInfusionRequirements({}, {},true)==AlterEgo::InfusionMatchVerdict::Matches);
    return 0;
}
