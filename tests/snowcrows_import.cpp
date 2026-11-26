#include "../src/SnowCrowsImport.h"
#include "../src/SourceRefreshCore.h"
#include <cassert>
#include <string>
using namespace AlterEgo::SnowCrows;

static std::string row(unsigned id, const std::string& label,
                       const std::string& hint, unsigned stat,
                       const std::string& upgrades) {
    const std::string base = std::to_string(id);
    std::string attributes = "data-armory-embed=\"items\" data-armory-ids=\"" + base + "\"";
    if (stat && id % 2 == 1)
        attributes = "data-armory-embed=\"items\" data-armory-" + base +
            "-stat=\"" + std::to_string(stat) + "\" data-armory-ids=\"" + base + "\"";
    else if (stat)
        attributes += " data-armory-" + base + "-stat=\"" + std::to_string(stat) + "\"";
    if (!upgrades.empty())
        attributes += " data-armory-" + base + "-upgrades=\"" + upgrades + "\"";
    return "<tr><td><div " + attributes + "></div></td><td><p class=\"mb-1\">" +
        label + "<span class=\"text-neutral-400 text-sm ml-2\">" +
        hint + "</span></p></td></tr>";
}
int case_snowcrows_import(int, char**) {
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
    using namespace AlterEgo;
    namespace SR=AlterEgo::SourceRefresh;
    struct Slot { uint32_t item_id=0,stat_id=0,rune_id=0,sigil_id=0,sigil2_id=0;
        std::string slot,stat_name,weapon_type,rune,sigil,sigil2,infusion;
        bool infusion_snapshot=false;
        std::vector<uint32_t> infusion_ids;std::vector<std::string> infusion_attributes; };
    struct SpecLine {uint32_t spec_id=0;int traits[3]={};};
    struct Bar {uint32_t heal=0,utilities[3]={},elite=0;};
    struct Requirement {uint32_t item_id;int count;std::string attribute;};
    struct Build {std::string profession,source_url,import_warning,name,notes,chat_link,rune_name,relic_name;
        uint32_t rune_id=0,relic_id=0;SpecLine specializations[3];Bar terrestrial_skills,aquatic_skills;
        std::map<std::string,Slot> gear;std::vector<Requirement> infusion_totals;
        bool infusion_summary_present=false;int infusion_ar_total=0;std::map<std::string,int> infusion_stat_totals;};
    Build old;old.profession="Mesmer";old.source_url=url;old.name="Local";old.notes="Keep";
    old.specializations[2]={40,{1995,999,1913}};old.aquatic_skills.heal=123;
    old.gear["Helm"].item_id=999;old.gear["Helm"].stat_id=1268;old.gear["Helm"].stat_name="Viper";
    old.gear["HelmAquatic"].item_id=77;old.gear["HelmAquatic"].infusion_ids={88};
    old.gear["WeaponA2"].item_id=222;old.gear["WeaponA1"].weapon_type="Sword";
    old.gear["Ring1"].infusion_attributes={"Power"};old.relic_id=12;
    auto patch=SR::FromPage(out,[](uint32_t,uint32_t id){return id==1995?0:id==1913?2:-1;});
    Build merged;SR::Report report;
    auto two=[](const auto& s){return s=="Staff"||s=="Spear"||s=="Greatsword";};
    assert(SR::Apply(old,patch,merged,report,error,two));
    assert(merged.name=="Local"&&merged.notes=="Keep"&&merged.aquatic_skills.heal==123);
    assert(merged.gear.at("Helm").item_id==999&&merged.gear.at("HelmAquatic").infusion_ids[0]==88);
    assert(merged.specializations[2].traits[1]==999&&!merged.gear.count("WeaponA2"));
    assert(report.offerInfusions&&merged.infusion_totals.empty()&&merged.gear.at("Ring1").infusion_attributes[0]=="Power");
    SR::AdoptInfusions(merged,*patch.infusions);
    assert(GetInfusionMode(merged)==InfusionMode::Totals&&merged.gear.at("HelmAquatic").infusion_ids.empty());
    SR::Patch partial;partial.profession=old.profession;partial.sourceUrl=url;
    partial.skills[0]=0;partial.relic=0;partial.gear["Ring1"].statName="Berserker";
    old.terrestrial_skills.heal=1;old.terrestrial_skills.elite=99;
    assert(SR::Apply(old,partial,merged,report,error,two));
    assert(!merged.terrestrial_skills.heal&&merged.terrestrial_skills.elite==99&&!merged.relic_id);
    assert(merged.gear.count("WeaponA2")&&merged.gear.at("Ring1").infusion_attributes[0]=="Power");
    old.terrestrial_skills.utilities[0]=10;old.terrestrial_skills.utilities[1]=20;
    partial.skills[1]=20; // omission of utility 2 is not permission to swap it
    assert(SR::Apply(old,partial,merged,report,error,two)&&merged.terrestrial_skills.utilities[0]==10&&merged.terrestrial_skills.utilities[1]==20);
    partial.gear["Helm"].statName="Berserker";partial.gear["Helm"].item=555;
    assert(SR::Apply(old,partial,merged,report,error,two,[](const auto&,const auto&){return SR::ItemCompatibility::Compatible;}));
    assert(merged.gear["Helm"].item_id==999);
    assert(SR::Apply(old,partial,merged,report,error,two,[](const auto&,const auto&){return SR::ItemCompatibility::Incompatible;}));
    assert(merged.gear["Helm"].item_id==555);
    partial.profession="Engineer";assert(!SR::Apply(old,partial,merged,report,error,two));
    assert(PositionalIds("1,0,3,4,5")==std::vector<uint32_t>({1,0,3,4,5}));
    assert(PositionalIds("1,,3").empty());
    return 0;
}
