#include "../src/ComposableGearPresentation.h"
#include "../src/TooltipCore.h"
#include <cassert>
#include <iostream>

using namespace AlterEgo::Composable;
int case_composable_gear_presentation(int, char**) {
    using namespace AlterEgo::TooltipCore;
    assert(EntityName("Known","Stored")=="Known");
    assert(EntityName("","Stored")=="Stored");
    assert(EntityName("").empty());
    assert(std::string(FriendlyAttribute("Healing"))=="Healing Power");
    assert(std::string(FriendlyAttribute("HealingPower"))=="Healing Power");
    assert(DyeName(0,"")=="Empty"&&DyeName(1,"")=="Dye Remover"&&DyeName(2,"").empty());
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
    const auto helm = Presentation::GearSlotSummary("armor", gear.at("Helm"));
    assert(helm.find("#123") != std::string::npos && helm.find("Ritualist") != std::string::npos);
    const auto weapon = Presentation::GearSlotSummary("weapons", gear.at("WeaponB1"));
    assert(weapon.find("#456") != std::string::npos && weapon.find("Greatsword") != std::string::npos);
    const auto ring = Presentation::GearSlotSummary("trinkets", gear.at("Ring2"));
    assert(ring.find("#23") != std::string::npos && ring.find("Berserker") != std::string::npos);
    const auto unknown = Presentation::GearSlotSummary("armor", gear.at("FutureSlot"));
    const auto empty = Presentation::GearSlotSummary("armor", Json::object());
    assert(!unknown.empty() && !empty.empty() && unknown != empty);
    assert(Presentation::GearSlotSummary("runes", Json{{"rune_id", 777}}).find("#777") != std::string::npos);
    const auto sigils = Presentation::GearSlotSummary("sigils", Json{{"sigil_id", 1}, {"sigil2", "Sigil of Force"}});
    assert(sigils.find("#1") != std::string::npos && sigils.find("Sigil of Force") != std::string::npos);
    const Json captured = {{"infusion_ids", Json::array({11, 12})}, {"infusion_snapshot", true}};
    auto uncaptured = captured;
    uncaptured["infusion_snapshot"] = false;
    const auto infusions = Presentation::GearSlotSummary("infusions", captured);
    assert(infusions.find("2") != std::string::npos);
    assert(infusions != Presentation::GearSlotSummary("infusions", uncaptured));
    assert(Presentation::GearSlotSummary("infusions",Json{{"infusion_attributes",Json::array({"Power","Precision"})}}).find("2 infusion positions")!=std::string::npos);
    assert(Presentation::GearSlotSummary("weapons", Json("unexpected")).find("preserved") != std::string::npos);
    assert(Presentation::GearSlotSummary("armor", Json("unexpected")) != empty);
    assert(gear == before);
    std::cout << "Composable gear presentation PASS\n";
    return 0;
}
