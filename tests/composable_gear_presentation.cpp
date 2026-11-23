#include "../src/ComposableGearPresentation.h"
#include <cassert>
#include <iostream>

using namespace AlterEgo::Composable;
int main() {
    const Json gear = {
        {"FutureSlot", {{"extension", "keep"}}},
        {"Ring2", {{"stat_name", "Berserker"}, {"stat_id", 1130}, {"item_id", 23}}},
        {"Helm", {{"item_id", 123}, {"stat_name", "Ritualist"}, {"future", 7}}},
        {"WeaponB1", {{"weapon_type", "Greatsword"}, {"item_id", 456}}}
    };
    const Json before = gear;
    const auto order = Presentation::SortedGearSlots(gear);
    assert((order == std::vector<std::string>{"Helm", "WeaponB1", "Ring2", "FutureSlot"}));
    assert(Presentation::SortedGearSlots(Json::array()).empty());
    assert(Presentation::GearSlotSummary("armor", gear.at("Helm")) ==
           "Item #123 | Stats Ritualist");
    assert(Presentation::GearSlotSummary("weapons", gear.at("WeaponB1")) ==
           "Item #456 | Greatsword");
    assert(Presentation::GearSlotSummary("trinkets", gear.at("Ring2")) ==
           "Item #23 | Stats Berserker");
    assert(Presentation::GearSlotSummary("armor", gear.at("FutureSlot")) ==
           "Additional imported fields (preserved)");
    assert(Presentation::GearSlotSummary("runes",
           Json{{"rune_id", 777}}) == "Rune #777");
    assert(Presentation::GearSlotSummary("sigils",
           Json{{"sigil_id", 1}, {"sigil2", "Sigil of Force"}}) ==
           "Sigil #1 | Sigil 2 Sigil of Force");
    assert(Presentation::GearSlotSummary("infusions",
           Json{{"infusion_ids", Json::array({11, 12})},
                {"infusion_snapshot", true}}) ==
           "2 infusion positions | captured snapshot");
    assert(Presentation::GearSlotSummary("weapons", Json("unexpected")).find("preserved") != std::string::npos);
    assert(Presentation::GearSlotSummary("armor", Json::object()) == "(empty recorded slot)");
    assert(gear == before);
    std::cout << "Composable gear presentation PASS\n";
}
