#include "../src/ComposableGearPresentation.h"
#include "../src/TooltipCore.h"
#include "../src/ReferenceExtras.h"
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
    const Json component={{"gear",{{"Helm",{{"item_id",123},{"stat_id",999},{"rune_id",7},{"infusion_ids",Json::array({4,4,0,-1,4294967296ull,"9"})},{"sigil_id",2},{"future_item_id",12}}}}},{"rune_id",7}};
    assert((Presentation::ComponentItemIds("armor",component)==std::vector<uint32_t>{123}));
    assert((Presentation::ComponentItemIds("aquatic",component)==std::vector<uint32_t>{2,4,7,123}));
    assert((Presentation::ComponentItemIds("runes",component)==std::vector<uint32_t>{7}));
    assert(Presentation::ComponentItemIds("skills",component).empty());
    assert((Presentation::ComponentItemIds("infusions",Json{{"infusion_totals",Json::array({Json{{"item_id",6}},Json{{"item_id",-6}},Json{{"item_id",0}}})}})==std::vector<uint32_t>{6}));
    const Json details={{"type","Harpoon"},{"weight_class","Heavy"},{"defense",42},{"min_power",10},{"max_power",20},{"infix_upgrade",{{"attributes",Json::array({Json{{"attribute","CritDamage"},{"modifier",5}},Json{{"attribute","Power"},{"modifier",-2}},Json{{"attribute","Unknown"},{"modifier",18446744073709551615ull}},Json{{"attribute","Bad"},{"modifier","9"}}})},{"buff",{{"description","Buff"}}}}},{"bonuses",Json::array({"First",nullptr,"Third"})}};
    auto lines=ItemDefinitionLines("Weapon","Exotic","Description",details);
    auto contains=[&](const std::string& expected){for(const auto& line:lines)if(line.text==expected)return true;return false;};
    assert(contains("Heavy Spear")&&contains("Defense: 42")&&contains("Weapon Strength: 10 - 20"));
    assert(contains("+5 Ferocity")&&contains("-2 Power")&&contains("Buff")&&contains("(3): Third")&&contains("Description"));
    assert(lines.size()==10);
    lines=ItemDefinitionLines("Weapon","Exotic","Description",details,ItemCombat);
    assert(lines.size()==2&&!contains("Fixed definition attributes"));
    lines=ItemDefinitionLines("Relic","Legendary","Misleading effect",details,ItemEffects|ItemDescription);
    assert(lines.size()==1&&!contains("Misleading effect")&&!contains("Buff"));
    lines=ItemDefinitionLines("Armor","Rare","",Json{{"defense","bad"},{"min_power",9},{"max_power",2}},ItemCombat|ItemAttributes);
    assert(lines.empty());
    const auto extras=AlterEgo::ReferenceExtras::ProjectItem(Json{{"level",80},{"vendor_value",0},{"restrictions",Json::array({"Human","Norn"})},{"flags",Json::array({"NoSell"})}});
    auto acquisition=ItemAcquisitionLines(extras);
    assert(acquisition.size()==3&&acquisition[0].text=="Item level: 80"&&acquisition[1].text=="API vendor value: 0 copper"&&acquisition[2].text=="Restrictions: Human, Norn");
    assert(ItemAcquisitionLines(Json::object()).empty());
    assert(ItemAcquisitionLines(Json{{"restrictions",Json::array()}}).empty());
    assert(!extras.contains("flags"));
    std::cout << "Composable gear presentation PASS\n";
    return 0;
}
