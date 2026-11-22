#include "../src/SnowCrowsImport.h"
#include <cassert>
#include <string>
using namespace AlterEgo::SnowCrows;

static std::string row(unsigned id, const std::string& label,
                       const std::string& hint, unsigned stat,
                       const std::string& upgrades) {
    const std::string base = std::to_string(id);
    std::string attributes = "data-armory-embed=\"items\" data-armory-ids=\"" + base + "\"";
    if (stat)
        attributes += " data-armory-" + base + "-stat=\"" + std::to_string(stat) + "\"";
    if (!upgrades.empty())
        attributes += " data-armory-" + base + "-upgrades=\"" + upgrades + "\"";
    return "<tr><td><div " + attributes + "></div></td><td><p class=\"mb-1\">" +
        label + "<span class=\"text-neutral-400 text-sm ml-2\">" +
        hint + "</span></p></td></tr>";
}
int main() {
    const std::string url = "https://snowcrows.com/builds/raids/mesmer/"
        "condition-boon-chronomancer-dueling-staff-scepter-pistol";
    assert(NormalizeUrl("http://www.snowcrows.com/builds/raids/mesmer/build") ==
           "https://snowcrows.com/builds/raids/mesmer/build");
    assert(NormalizeUrl("https://snowcrows.com.evil.net/builds/raids/mesmer/build").empty());
    assert(NormalizeUrl("https://snowcrows.com/builds/raids/mesmer/../secret").empty());
    assert(NormalizeUrl("https://snowcrows.com/@attacker/builds/raids/mesmer").empty());

    std::string sample = "<meta name=\"description\" content=\"Guild Wars 2 "
        "Condition Boon Chronomancer - Dueling - Staff / Scepter &amp; Pistol Meta build for raids\">";
    sample += std::string(1100, ' ');
    sample += "<div id=\"gearset\"><table>";
    sample += row(75770, "Viper&#039;s", "Helm", 1268, "67339");
    sample += row(73225, "Viper&#039;s", "Shoulders", 1268, "67339");
    sample += row(71436, "Viper&#039;s", "Coat", 1268, "67339");
    sample += row(73852, "Viper&#039;s", "Gloves", 1268, "67339");
    sample += row(75378, "Viper&#039;s", "Leggings", 1268, "67339");
    sample += row(74264, "Viper&#039;s", "Boots", 1268, "67339");
    sample += row(79837, "Viper&#039;s", "Backpiece", 1268, "");
    sample += row(79745, "Viper&#039;s", "Accessory", 1268, "");
    sample += row(79444, "Viper&#039;s", "Accessory", 1268, "");
    sample += row(81169, "Viper&#039;s", "Amulet", 1268, "");
    sample += row(79710, "Viper&#039;s", "Ring", 1268, "");
    sample += row(81167, "Viper&#039;s", "Ring", 1268, "");
    sample += row(100432, "Relic of Akeem", "Relic", 0, "");
    sample += "<thead><th colspan=\"2\">Primary Weapon Set</th></thead>";
    sample += row(71387, "Viper&#039;s Staff", "Main Hand", 1268, "24583,48911");
    sample += "<thead><th colspan=\"2\">Secondary Weapon Set</th></thead>";
    sample += row(76688, "Viper&#039;s Scepter", "Main Hand", 1268, "24583");
    sample += row(72239, "Viper&#039;s Pistol", "Off Hand", 1268, "24560");
    sample += row(39619, "Infusion", "x15", 0, "");
    sample += row(85971, "Infusion", "x3", 0, "");
    sample += "</table></div><div id=\"skills\">";
    sample += "<div data-armory-embed=\"skills\" data-armory-ids=\"21750,10232,10234,10202,29519\"></div>";
    sample += "<div data-armory-embed=\"specializations\" data-armory-ids=\"1\" data-armory-1-traits=\"701,1889,1950\"></div>";
    sample += "<div data-armory-embed=\"specializations\" data-armory-ids=\"24\" data-armory-24-traits=\"721,1690,753\"></div>";
    sample += "<div data-armory-embed=\"specializations\" data-armory-ids=\"40\" data-armory-40-traits=\"1995,1913\"></div>";
    Page out;
    std::string error;
    assert(Parse(url, sample, out, error));
    assert(out.name == "Condition Boon Chronomancer - Dueling - Staff / Scepter & Pistol");
    assert(out.profession == "Mesmer");
    assert(out.gameMode == "Raid");
    assert(out.gear.size() == 15);
    assert(out.gear.at("WeaponA1").weaponType == "Staff");
    assert(out.gear.at("WeaponA1").upgrades.size() == 2);
    assert(out.gear.at("WeaponB1").weaponType == "Scepter");
    assert(out.gear.at("WeaponB2").upgrades.at(0) == 24560);
    assert(out.gear.at("Accessory1").statId == 1268);
    assert(out.gear.at("Accessory2").statId == 1268);
    assert(out.runeId == 67339);
    assert(out.relicId == 100432);
    assert(out.skills[0] == 21750 && out.skills[4] == 29519);
    assert(out.specs[0].traits.size() == 3);
    assert(out.specs[2].traits.size() == 2);
    assert(out.infusions.size() == 2);
    assert(out.infusions[0].itemId == 39619 && out.infusions[0].count == 15);
    assert(out.infusions[1].itemId == 85971 && out.infusions[1].count == 3);
    const std::string previousName = out.name;
    assert(!Parse(url, "<html>error</html>", out, error));
    assert(out.name == previousName); // invalid refresh cannot destroy existing data
}
