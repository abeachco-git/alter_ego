#include "../src/MetaBattleImport.h"
#include <cassert>
#include <string>

static std::string Tile(int id, const std::string& label) {
    return "<div class=\"equipment-slot\"><div data-armory-embed=\"items\" "
           "data-armory-ids=\"" + std::to_string(id) +
           "\" data-armory-size=\"46\"></div><small>" + label + "</small></div>";
}
int main() {
    using namespace AlterEgo;
    const std::string url =
        "https://metabattle.com/wiki/Build:Chronomancer_-_Power_DPS";
    assert(MetaBattle::NormalizeUrl("http://www.metabattle.com/wiki/Build:Chronomancer_-_Power_DPS") == url);
    assert(MetaBattle::NormalizeUrl(url) == url);
    assert(MetaBattle::NormalizeUrl("https://metabattle.com/wiki/Build%3AChronomancer_-_Power_DPS") ==
           "https://metabattle.com/wiki/Build%3AChronomancer_-_Power_DPS");
    assert(MetaBattle::NormalizeUrl("https://metabattle.com.evil.org/wiki/Build:Something").empty());
    assert(MetaBattle::NormalizeUrl("https://metabattle.com/wiki/Other:Not_a_build").empty());
    assert(MetaBattle::NormalizeUrl(url + "?action=raw").empty());
    assert(MetaBattle::NormalizeUrl("https://metabattle.com/wiki/Build:../../evil").empty());
    assert(MetaBattle::NormalizeUrl("file:///etc/passwd").empty());

    std::string html = R"(<html><head><title>Chronomancer - Power DPS - MetaBattle Guild Wars 2 Builds</title>
        <script>RLCONF={"wgCategories":["Meta builds","Mesmer builds","Raid builds"]};</script></head>
        <body><h1  id="firstHeading" class="title">Chronomancer - Power DPS</h1>
        <h2><span id="Skill_Bar">Skill Bar</span></h2>
        <div data-armory-embed="skills" data-armory-ids="56930,56928,56873,10192,29830"></div>
        <div>Greatsword</div><div data-armory-embed="skills" data-armory-ids="1,2,3,4,5"></div>
        <div>Utility</div><div data-armory-embed="skills" data-armory-ids="21750,10211,30525,10267,30359"></div>
        <div id="Template_Code">Template Code</div>
        <div class="build-template-code mb-3 me-2">[&amp;DQcKGQEdKDUjDyMPaQFpAQQTawGPAY8BsxK8AQAAAAAAAAAAAAAAAAAAAAA=]</div>
        <div id="Specializations">Specializations</div>
        <div data-armory-embed="specializations" data-armory-ids="10" data-armory-10-traits="686,713,681"></div>
        <div data-armory-embed="specializations" data-armory-ids="1" data-armory-1-traits="701,708,692"></div>
        <div data-armory-embed="specializations" data-armory-ids="40" data-armory-40-traits="1838,2009,1890"></div>
        <h2><span id="Equipment">Equipment</span></h2>)";
    html += R"(<div id="eq-box-1"><div class="d-flex flex-row">)";
    html += Tile(48081, "Head<br />Berserker");
    html += Tile(48083, "Shoulders<br />Zojja");
    html += Tile(48079, "Chest<br />Berserker");
    html += Tile(48080, "Hands<br />Berserker");
    html += Tile(48136, "Legs<br />Assassin");
    html += Tile(48078, "Feet<br />Berserker");
    html += Tile(37009, "Backpiece<br />Berserker");
    html += Tile(39233, "Accessory<br />Berserker");
    html += Tile(39232, "Accessory<br />Berserker");
    html += Tile(39273, "Amulet<br />Berserker");
    html += Tile(37075, "Ring<br />Berserker");
    html += Tile(37086, "Ring<br />Berserker");
    html += R"(</div></div><div id="eq-box-2">)";
    html += Tile(46760, "Dagger<br />Yassith");
    html += Tile(46774, "Sword<br />Berserker");
    html += Tile(24615, "Sigil");
    html += Tile(24618, "Sigil");
    html += R"(</div><div id="eq-box-3">)";
    html += Tile(46762, "Greatsword<br />Berserker");
    html += R"(<div class="equipment-slot" data-armory-embed="items" data-armory-ids="-1"></div>)";
    html += Tile(24619, "Sigil");
    html += Tile(24620, "Sigil");
    html += R"(</div>)";
    html += Tile(74978, "Rune<br />x6");
    html += Tile(37131, "Infusion<br />x14");
    html += Tile(37132, "Infusion<br />x4");
    html += Tile(103574, "Relic");
    html += R"(<h2><span id="Consumables">Consumables</span></h2>
        <div data-armory-embed="items" data-armory-ids="999999"></div>
        <p>Alternate gear: Viper's, Zojja's, Yassith's, and various optional relics.</p></body></html>)";

    SnowCrows::Page parsed;
    std::string error;
    assert(MetaBattle::Parse(url, html, parsed, error) && error.empty());
    assert(parsed.profession == "Mesmer");
    assert(parsed.gameMode == "Raid");
    assert(parsed.name == "Chronomancer - Power DPS");
    assert(parsed.sourceUrl == url);
    assert(parsed.templateCode == "[&DQcKGQEdKDUjDyMPaQFpAQQTawGPAY8BsxK8AQAAAAAAAAAAAAAAAAAAAAA=]");
    assert(parsed.skills[0] == 21750 && parsed.skills[4] == 30359);
    assert(parsed.specs[0].id == 10 && parsed.specs[1].id == 1 && parsed.specs[2].id == 40);
    assert(parsed.specs[2].traits[1] == 2009);
    assert(parsed.gear.size() == 15);
    assert(parsed.gear.at("Helm").itemId == 48081);
    assert(parsed.gear.at("Shoulders").statName == "Berserker");
    assert(parsed.gear.at("Leggings").statName == "Assassin");
    assert(parsed.gear.at("Accessory1").itemId == 39233);
    assert(parsed.gear.at("Accessory2").itemId == 39232);
    assert(parsed.gear.at("Ring1").itemId == 37075);
    assert(parsed.gear.at("Ring2").itemId == 37086);
    assert(parsed.gear.at("WeaponA1").statName == "Viper");
    assert(parsed.gear.at("WeaponA1").weaponType == "Dagger");
    assert(parsed.gear.at("WeaponA1").upgrades.size() == 1 &&
           parsed.gear.at("WeaponA1").upgrades[0] == 24615);
    assert(parsed.gear.at("WeaponA2").upgrades.size() == 1 &&
           parsed.gear.at("WeaponA2").upgrades[0] == 24618);
    assert(parsed.gear.at("WeaponB1").weaponType == "Greatsword");
    assert(parsed.gear.at("WeaponB1").upgrades.size() == 2 &&
           parsed.gear.at("WeaponB1").upgrades[0] == 24619 &&
           parsed.gear.at("WeaponB1").upgrades[1] == 24620);
    assert(!parsed.gear.count("WeaponB2"));
    assert(parsed.runeId == 74978 && parsed.relicId == 103574);
    assert(parsed.infusions.size() == 2 &&
           parsed.infusions[0].count == 14 && parsed.infusions[1].count == 4);

    const auto original = parsed.gear.size();
    const auto pos = html.find("<small>Relic</small>");
    assert(pos != std::string::npos);
    html.replace(pos, std::string("<small>Relic</small>").size(), "<small>Unknown</small>");
    assert(!MetaBattle::Parse(url, html, parsed, error));
    assert(!error.empty() && parsed.gear.size() == original);
    return 0;
}
