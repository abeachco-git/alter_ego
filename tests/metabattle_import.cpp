#include "../src/MetaBattleImport.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>

static std::string Tile(int id, const std::string& label) {
    return "<div class=\"equipment-slot\"><div data-armory-embed=\"items\" "
           "data-armory-ids=\"" + std::to_string(id) +
           "\" data-armory-size=\"46\"></div><small>" + label + "</small></div>";
}
int main(int argc, char** argv) {
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
    // Complete secondary loadout on the same wiki page. Never overwrite
    // primary slots or count its separate +18 infusions.
    html += R"(<h3><span id="Equipment_Variants">Equipment Variants</span></h3>
        <h3><span id="Impact_Sigil_Variant">Impact Sigil Variant</span></h3>)";
    html += Tile(79830, "Backpiece<br />Assassin");
    html += Tile(79124, "Ring<br />Assassin");
    html += Tile(79712, "Ring<br />Assassin");
    html += Tile(37131, "Infusion<br />x18");
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
    assert(parsed.gear.at("Backpack").itemId == 37009);
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
           parsed.infusions[0].itemId == 37131 && parsed.infusions[0].count == 14 &&
           parsed.infusions[1].itemId == 37132 && parsed.infusions[1].count == 4);

    // Open-world pages may place the first equipment grid below an <h3>
    // ("Standard Equipment"). Never mistake this for an alternative loadout.
    std::string openWorldFixture = html;
    const size_t gridAt = openWorldFixture.find("<div id=\"eq-box-1\">");
    assert(gridAt != std::string::npos);
    openWorldFixture.insert(gridAt,
        "<h3><span id=\"Standard_Equipment\">Standard Equipment</span></h3>");
    SnowCrows::Page openWorldParsed;
    assert(MetaBattle::Parse(url, openWorldFixture, openWorldParsed, error));
    assert(error.empty());
    assert(openWorldParsed.gear.size() == 15);
    assert(openWorldParsed.gear.at("Backpack").itemId == 37009);
    assert(openWorldParsed.gear.at("Ring2").itemId == 37086);
    assert(openWorldParsed.infusions.size() == 2);

    // Open-world pages legitimately omit statistical infusions. The
    // equipment import must remain complete, not fall back to chat-code only.
    std::string missingInfusions = openWorldFixture;
    missingInfusions = std::regex_replace(missingInfusions,
        std::regex(R"MB(<div class="equipment-slot"><div data-armory-embed="items" data-armory-ids="3713[12]"[^>]*></div><small>Infusion<br\s*/>x[0-9]+</small></div>)MB"),
        "");
    SnowCrows::Page noInfusionsParsed;
    assert(MetaBattle::Parse(url, missingInfusions, noInfusionsParsed, error));
    assert(error.empty());
    assert(noInfusionsParsed.infusions.empty());
    assert(noInfusionsParsed.gear.size() == 15);

    const auto original = parsed.gear.size();
    const auto pos = html.find("<small>Relic</small>");
    assert(pos != std::string::npos);
    html.replace(pos, std::string("<small>Relic</small>").size(), "<small>Unknown</small>");
    // A missing relic is not fatal: preserve extractable gear and annotate the gap.
    assert(MetaBattle::Parse(url, html, parsed, error));
    assert(error.empty());
    assert(parsed.gear.size() == original);
    assert(parsed.relicId == 0);
    assert(parsed.importWarning.find("Relic not resolved") != std::string::npos);

    // Local four-page regression verification against actual server-rendered
    // MetaBattle snapshots. The CI workflow downloads these files, so this
    // runs the same parser called by the in-game Import dialog.
    if (argc > 1) {
        auto live = [&](const char* name) -> std::string {
            const std::string path = std::string(argv[1]) + "/" + name + ".html";
            std::ifstream file(path, std::ios::binary);
            assert(file.good());
            return std::string(std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>());
        };
        auto verify = [&](const char* tag, const char* article,
                          const char* profession, size_t slots,
                          const std::vector<std::pair<uint32_t, int>>& infusions) {
            SnowCrows::Page page;
            const std::string link = "https://metabattle.com/wiki/Build:" +
                std::string(article);
            const std::string body = live(tag);
            const bool ok = MetaBattle::Parse(link, body, page, error);
            if (!ok)
                std::cerr << "MetaBattle " << tag << " failed: " << error << '\n';
            assert(ok && error.empty());
            assert(page.profession == profession);
            assert(page.gear.size() == slots);
            for (const char* slot : {"Helm", "Shoulders", "Coat", "Gloves",
                                     "Leggings", "Boots", "Backpack",
                                     "Accessory1", "Accessory2", "Amulet",
                                     "Ring1", "Ring2"}) {
                assert(page.gear.count(slot));
                assert(page.gear.at(slot).itemId > 0);
                assert(!page.gear.at(slot).statName.empty());
            }
            assert(page.runeId != 0 && page.relicId != 0);
            assert(page.specs[0].id && page.specs[1].id && page.specs[2].id);
            assert(page.skills[0] && page.skills[4]);
            assert(page.infusions.size() == infusions.size());
            for (size_t i = 0; i < infusions.size(); ++i) {
                assert(page.infusions[i].itemId == infusions[i].first);
                assert(page.infusions[i].count == infusions[i].second);
            }
            std::cout << "MetaBattle live PASS: " << tag
                      << " gear=" << page.gear.size()
                      << " infusions=" << page.infusions.size() << '\n';
            return page;
        };

        const auto reaper = verify(
            "reaper", "Reaper_-_Power_Greatsword_Reaper", "Necromancer", 15, {});
        assert(reaper.gear.at("Helm").statName == "Assassin");
        assert(reaper.gear.at("Backpack").itemId == 37009);
        assert(reaper.gear.at("Ring1").itemId == 37075);
        assert(reaper.gear.at("Ring2").itemId == 37086);
        assert(reaper.gear.at("WeaponA1").weaponType == "Greatsword");
        assert(reaper.gear.at("WeaponB1").weaponType == "Sword");
        assert(reaper.gear.at("WeaponB2").weaponType == "Sword");

        const auto harbinger = verify(
            "harbinger", "Harbinger_-_Boon_DPS_Condi_Harbinger_Open_World",
            "Necromancer", 16, {});
        assert(harbinger.gear.at("Helm").statName == "Ritualist");
        assert(harbinger.gear.at("Backpack").itemId == 79830);
        assert(harbinger.gear.at("Ring1").itemId == 79124);
        assert(harbinger.gear.at("Ring2").itemId == 79712);
        assert(harbinger.gear.at("WeaponA1").weaponType == "Pistol");
        assert(harbinger.gear.at("WeaponA2").weaponType == "Dagger");
        assert(harbinger.gear.at("WeaponB1").weaponType == "Scepter");
        assert(harbinger.gear.at("WeaponB2").weaponType == "Torch");

        const auto chronomancer = verify(
            "chronomancer", "Chronomancer_-_Power_DPS",
            "Mesmer", 15, {{37131, 14}, {37132, 4}});
        assert(chronomancer.gear.at("Backpack").itemId == 37009);
        assert(chronomancer.gear.at("Ring1").itemId == 37075);
        assert(chronomancer.gear.at("Ring2").itemId == 37086);
        assert(chronomancer.gear.at("WeaponA1").weaponType == "Dagger");
        assert(chronomancer.gear.at("WeaponA2").weaponType == "Sword");
        assert(chronomancer.gear.at("WeaponB1").weaponType == "Greatsword");

        const auto mechanist = verify(
            "mechanist", "Mechanist_-_Condi_DPS",
            "Engineer", 15, {{37130, 18}});
        assert(mechanist.gear.at("Helm").statName == "Viper");
        assert(mechanist.gear.at("Shoulders").statName == "Sinister");
        assert(mechanist.gear.at("Backpack").itemId == 79830);
        assert(mechanist.gear.at("Ring1").itemId == 79124);
        assert(mechanist.gear.at("Ring2").itemId == 79712);
        assert(mechanist.gear.at("WeaponA1").weaponType == "Spear");
        assert(mechanist.gear.at("WeaponB1").weaponType == "Pistol");
        assert(mechanist.gear.at("WeaponB2").weaponType == "Pistol");
    }
    return 0;
}
