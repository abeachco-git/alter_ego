#include "../src/SnowCrowsImport.h"
#include "../src/SourceRefreshCore.h"
#include <cassert>
#include <string>
#include <fstream>
#include <iostream>
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
int case_snowcrows_import(int argc, char** argv) {
    // Explicit manual probe, never part of normal CTest. Read a bounded
    // transient provider page, run the exact runtime parser, emit only IDs.
    if(argc==4&&std::string(argv[1])=="--probe"){
        std::ifstream in(argv[3],std::ios::binary|std::ios::ate);
        if(!in||in.tellg()<0||in.tellg()>3*1024*1024)return 2;
        const auto size=static_cast<size_t>(in.tellg());
        std::string html(size,'\0');in.seekg(0);
        if(!in.read(html.data(),static_cast<std::streamsize>(size)))return 2;
        Page parsed;std::string error;
        if(!Parse(argv[2],html,parsed,error)){std::cout<<"Runtime parser rejected: "<<error<<"\n";return 1;}
        std::cout<<"Runtime parser accepted "<<parsed.profession<<"; skills";
        for(auto id:parsed.skills)std::cout<<" "<<id;
        std::cout<<"; gear slots "<<parsed.gear.size()<<"\n";
        AlterEgo::DecodedBuildLink decoded{};
        if(!parsed.templateCode.empty()&&AlterEgo::ChatLink::DecodeBuild(parsed.templateCode,decoded)){
            std::cout<<"Template profession "<<static_cast<unsigned>(decoded.profession)
                <<"; companion block "<<decoded.companion_choices_known<<"; active palettes "<<decoded.active_palettes_known
                <<"; inactive block "<<decoded.inactive_rev_skills_known<<"\n";
            if(decoded.companion_choices_known){
                std::cout<<"Companion codes";
                if(decoded.profession==AlterEgo::PROF_RANGER)for(auto id:decoded.pets)std::cout<<" "<<static_cast<unsigned>(id);
                if(decoded.profession==AlterEgo::PROF_REVENANT)for(auto id:decoded.legends)std::cout<<" "<<static_cast<unsigned>(id);
                std::cout<<"\n";
            }
            if(decoded.active_palettes_known){
                std::cout<<"Terrestrial palettes";
                for(auto id:decoded.terrestrial_skills)std::cout<<" "<<id;
                std::cout<<"\nAquatic palettes";
                for(auto id:decoded.aquatic_skills)std::cout<<" "<<id;
                std::cout<<"\n";
            }
        }else std::cout<<"No unique decodable header template\n";
        return 0;
    }
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
    assert(MissingRequiredGear(out,true).empty());
    {
        auto missing=out;missing.gear.erase("Backpack");
        assert(missing.gear.size()>=12&&MissingRequiredGear(missing)==std::vector<std::string>{"Backpack"});
        missing=out;missing.gear.at("Ring1").statName.clear();
        assert(MissingRequiredGear(missing).empty()&&MissingRequiredGear(missing,true)==std::vector<std::string>{"Ring1"});
        auto damaged=sample;
        const auto backpiece=row(79837,"Viper&#039;s","Backpiece",1268,"");
        const auto at=damaged.find(backpiece);assert(at!=std::string::npos);
        damaged.erase(at,backpiece.size());
        Page retained=out;assert(!Parse(url,damaged,retained,error));
        assert(retained.name==out.name&&retained.gear.size()==out.gear.size());
        assert(error.find("Backpack")!=std::string::npos);
    }
    {
        // Live Snow Crows Revenant pages expose an invocation pair where
        // other professions publish five typed skills. Use synthetic markup
        // with the observed structural layout, not a copied guide snapshot.
        AlterEgo::DecodedBuildLink code{};code.profession=AlterEgo::PROF_REVENANT;
        code.legends[0]=8;code.legends[1]=2;code.legends[2]=3;code.legends[3]=4;
        code.specs[0].spec_id=1;code.specs[1].spec_id=24;code.specs[2].spec_id=40;
        for(int i=0;i<5;++i)code.terrestrial_skills[i]=static_cast<uint16_t>(100+i);
        auto guide=sample;const std::string ids="21750,10232,10234,10202,29519";
        guide.replace(guide.find(ids),ids.size(),"76610,28134");
        const auto base=guide;
        const std::string revUrl="https://snowcrows.com/builds/raids/revenant/build";
        const auto withLink=[&](const auto& packet){
            auto text=base;text.insert(text.find("<div id=\"gearset\">"),AlterEgo::ChatLink::EncodeBuild(packet));return text;
        };
        Page rev;assert(!Parse(revUrl,guide,rev,error)); // no native position evidence
        guide=withLink(code);
        assert((Parse(revUrl,guide,rev,error)&&rev.skills==std::array<uint32_t,5>{}));
        assert((rev.legendInvocationSkills==std::array<uint32_t,2>{76610,28134}));
        assert(!Parse(url,guide,rev,error)); // non-Revenant never treats pair as a skill bar
        auto tier=[](uint32_t,uint32_t id){return id==1995?0:id==1913?2:-1;};
        auto invocation=[](uint32_t id)->uint32_t{return id==8?76610:id==2?28134:0;};
        auto patch=AlterEgo::SourceRefresh::FromPage(rev,tier,false,AlterEgo::SourceRefresh::UnknownPaletteResolver{},
            AlterEgo::SourceRefresh::UnknownPalettePair{},invocation);
        assert(patch.warning.empty()&&patch.companions[0]==8&&patch.companions[1]==2&&patch.activePalettes);
        for(const auto& skill:patch.skills)assert(!skill);
        std::swap(rev.legendInvocationSkills[0],rev.legendInvocationSkills[1]);
        patch=AlterEgo::SourceRefresh::FromPage(rev,tier,false,AlterEgo::SourceRefresh::UnknownPaletteResolver{},
            AlterEgo::SourceRefresh::UnknownPalettePair{},invocation);
        assert(patch.warning.empty()); // the pair is not positional
        rev.legendInvocationSkills[0]=999;
        patch=AlterEgo::SourceRefresh::FromPage(rev,tier,false,AlterEgo::SourceRefresh::UnknownPaletteResolver{},
            AlterEgo::SourceRefresh::UnknownPalettePair{},invocation);
        assert(!patch.warning.empty()&&patch.companions[0]==8&&patch.companions[1]==2);
        code.legends[1]=0;assert(!Parse(revUrl,withLink(code),rev,error));
        code.legends[1]=2;code.terrestrial_skills[2]=0;
        assert(!Parse(revUrl,withLink(code),rev,error));
    }
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
        struct Pets {uint32_t terrestrial[2]={},aquatic[2]={};} pets;
        uint8_t legend_codes[4]={};std::optional<InactiveLegendPalettes> inactive_rev_skills;
        std::optional<InactiveLegendContext> rev_inactive_context;
        std::optional<RevenantActivePaletteEvidence> rev_active_palettes;
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
    SR::Patch runes;runes.profession=old.profession;runes.sourceUrl=url;
    old.rune_id=123;old.gear["Coat"].rune_id=456;old.gear["Boots"].item_id=77;
    runes.gear["Helm"].rune=789;
    assert(SR::Apply(old,runes,merged,report,error,two));
    assert(merged.gear["Helm"].rune_id==789&&merged.gear["Coat"].rune_id==456&&merged.gear["Boots"].rune_id==123);
    assert(!merged.gear["HelmAquatic"].rune_id&&merged.gear["HelmAquatic"].infusion_ids[0]==88);
    SR::Patch offhand;offhand.profession=old.profession;offhand.sourceUrl=url;
    old.gear["WeaponA1"].weapon_type="Staff";old.gear.erase("WeaponA2");
    offhand.gear["WeaponA2"].type="Sword";
    assert(SR::Apply(old,offhand,merged,report,error,two)&&!merged.gear.count("WeaponA2"));
    offhand.gear["WeaponA1"].type="Staff";
    assert(!SR::Apply(old,offhand,merged,report,error,two));
    SR::Patch explicitRemove;explicitRemove.profession=old.profession;explicitRemove.sourceUrl=url;
    explicitRemove.removedSlots.insert("Coat");
    assert(SR::Apply(old,explicitRemove,merged,report,error,two)&&!merged.gear.count("Coat"));
    partial.profession="Engineer";assert(!SR::Apply(old,partial,merged,report,error,two));
    {
        Build local;local.profession="Revenant";local.source_url=url;
        local.rev_active_palettes=RevenantActivePaletteEvidence{};
        (*local.rev_active_palettes)[0]={11,0,7,2,42};
        (*local.rev_active_palettes)[1]={12,55,7,2,42};
        (*local.rev_active_palettes)[9]={13,0,3,4,42};
        local.terrestrial_skills.utilities[0]=55;
        SR::Patch patch;patch.profession=local.profession;patch.sourceUrl=url;
        assert(SR::Apply(local,patch,merged,report,error,two));
        assert(merged.rev_active_palettes==local.rev_active_palettes);
        patch.skills[0]=0;patch.skills[1]=56;
        assert(SR::Apply(local,patch,merged,report,error,two));
        assert((*merged.rev_active_palettes)[0][0]==0&&(*merged.rev_active_palettes)[1][0]==0&&(*merged.rev_active_palettes)[9][0]==13);
    }
    {
        // Guide IDs and wire palettes are independent; retain both only
        // with exact positive agreement and the accepted legend context.
        Build local;local.profession="Revenant";local.source_url=url;
        Page guide;guide.profession=local.profession;guide.sourceUrl=url;
        guide.specs[0].id=1;guide.specs[1].id=2;guide.specs[2].id=42;
        DecodedBuildLink code{};code.profession=PROF_REVENANT;code.specs[2].spec_id=42;
        code.legends[0]=7;code.legends[1]=2;code.legends[2]=3;code.legends[3]=4;
        code.terrestrial_skills[0]=11;code.terrestrial_skills[1]=12;
        code.terrestrial_skills[2]=13;code.aquatic_skills[4]=14;
        guide.skills={100,200,300,0,0};guide.templateCode=ChatLink::EncodeBuild(code);
        auto tier=[](uint32_t,uint32_t){return -1;};
        auto resolve=[](uint16_t p,uint32_t,int,int)->uint32_t{
            return p==11?100:p==13?301:0;
        };
        const PalettePairs pairs={{12,200},{12,201}};
        auto match=[&](uint16_t p,uint32_t id){return ContainsPalettePair(pairs,p,id);};
        auto patch=SR::FromPage(guide,tier,true,resolve,match);
        assert(patch.activePalettes&&(*patch.activePalettes)[0][1]==100&&(*patch.activePalettes)[1][1]==200);
        assert((*patch.activePalettes)[2][0]==0&&!patch.warning.empty());
        assert(SR::Apply(local,patch,merged,report,error,two,SR::UnknownCompatibility{},true));
        assert(merged.terrestrial_skills.heal==100&&merged.terrestrial_skills.utilities[0]==200&&merged.terrestrial_skills.utilities[1]==300);
        assert(merged.rev_active_palettes&&(*merged.rev_active_palettes)[0][0]==11&&(*merged.rev_active_palettes)[1][0]==12);
        assert((*merged.rev_active_palettes)[2][0]==0&&(*merged.rev_active_palettes)[9][0]==14);
        local=merged;local.aquatic_skills.elite=900;
        assert(SR::Apply(local,patch,merged,report,error,two));
        assert(merged.aquatic_skills.elite==900);
        // Omitted/zero guide values cannot erase a prior positive skill.
        guide.skills={};code.terrestrial_skills[0]=0;guide.templateCode=ChatLink::EncodeBuild(code);
        patch=SR::FromPage(guide,tier,false,resolve,match);
        assert(SR::Apply(local,patch,merged,report,error,two)&&merged.terrestrial_skills.heal==100);
        assert(merged.terrestrial_skills.utilities[1]==301&&(*merged.rev_active_palettes)[2][0]==13);
        // Partial or rejected companion pair cannot qualify new wire evidence.
        code.legends[1]=7;guide.templateCode=ChatLink::EncodeBuild(code);
        patch=SR::FromPage(guide,tier,false,resolve,match);
        assert(SR::Apply(local,patch,merged,report,error,two));
        assert(merged.terrestrial_skills.utilities[1]==300&&(*merged.rev_active_palettes)[2][0]==0);
    }
    // Exercise the shared provider-neutral template projection, not the
    // excluded MetaBattle parser. Omission and duplicate-pair protection
    // apply equally to both profession-specific components.
    for(const auto prof:{PROF_RANGER,PROF_REVENANT}){
        Build local;local.profession=ChatLink::ProfessionName(prof);local.source_url=url;
        Page guide;guide.profession=local.profession;guide.sourceUrl=url;
        DecodedBuildLink code{};code.profession=prof;
        if(prof==PROF_RANGER){
            local.pets.terrestrial[0]=4;local.pets.terrestrial[1]=5;
            local.pets.aquatic[0]=6;local.pets.aquatic[1]=7;
            code.pets[0]=8;
        }else{
            local.legend_codes[0]=1;local.legend_codes[1]=2;
            local.legend_codes[2]=3;local.legend_codes[3]=4;
            local.inactive_rev_skills=InactiveLegendPalettes{10,20,30,40,50,60};
            code.legends[0]=5;
            code.inactive_rev_skills[0]=99;
        }
        auto tier=[](uint32_t,uint32_t){return -1;};
        auto none=SR::FromPage(guide,tier);
        assert(SR::Apply(local,none,merged,report,error,two));
        assert(merged.pets.terrestrial[0]==local.pets.terrestrial[0]&&merged.legend_codes[0]==local.legend_codes[0]);
        guide.templateCode=ChatLink::EncodeBuild(code);
        auto supplied=SR::FromPage(guide,tier);
        assert(supplied.verifiedTemplate==guide.templateCode&&supplied.companions[0]&&!supplied.companions[1]);
        assert(SR::Apply(local,supplied,merged,report,error,two));
        if(prof==PROF_RANGER){
            assert(merged.pets.terrestrial[0]==8&&merged.pets.terrestrial[1]==5&&merged.pets.aquatic[0]==6);
            code.pets[0]=5;
        }else{
            assert(merged.legend_codes[0]==5&&merged.legend_codes[1]==2&&merged.legend_codes[2]==3);
            assert(merged.inactive_rev_skills==local.inactive_rev_skills);
            code.legends[1]=2;
            code.inactive_rev_skills[1]=98;code.inactive_rev_skills[2]=97;
            guide.templateCode=ChatLink::EncodeBuild(code);
            auto land=SR::FromPage(guide,tier);
            assert(SR::Apply(local,land,merged,report,error,two));
            assert((merged.inactive_rev_skills==InactiveLegendPalettes{99,98,97,40,50,60}));
            assert(merged.rev_inactive_context&&(*merged.rev_inactive_context)[0]==LegendPaletteContext({5,2,0})&&
                (*merged.rev_inactive_context)[1]==UnknownLegendPaletteContext);
            auto absent=local;absent.inactive_rev_skills.reset();
            assert(SR::Apply(absent,land,merged,report,error,two)&&!merged.inactive_rev_skills);
            code.legends[2]=3;code.legends[3]=4;
            code.inactive_rev_skills[3]=96;code.inactive_rev_skills[4]=95;code.inactive_rev_skills[5]=94;
            guide.templateCode=ChatLink::EncodeBuild(code);
            assert(SR::Apply(absent,SR::FromPage(guide,tier),merged,report,error,two));
            assert((merged.inactive_rev_skills==InactiveLegendPalettes{99,98,97,96,95,94}));
            assert(merged.rev_inactive_context&&(*merged.rev_inactive_context)[1]==LegendPaletteContext({3,4,0}));
            code.legends[0]=2;
        }
        guide.templateCode=ChatLink::EncodeBuild(code);
        assert(SR::Apply(local,SR::FromPage(guide,tier),merged,report,error,two));
        assert(merged.pets.terrestrial[0]==local.pets.terrestrial[0]&&merged.legend_codes[0]==local.legend_codes[0]);
        code.profession=PROF_MESMER;guide.templateCode=ChatLink::EncodeBuild(code);
        auto mismatch=SR::FromPage(guide,tier);
        assert(mismatch.verifiedTemplate.empty()&&!mismatch.companions[0]);
    }
    {
        DecodedBuildLink code{};code.profession=PROF_MESMER;
        const auto link=ChatLink::EncodeBuild(code);
        auto header=sample;header.insert(header.find("<div id=\"gearset\">"),link);
        Page linked;assert(Parse(url,header,linked,error)&&linked.templateCode==link);
        header.insert(header.find("<div id=\"gearset\">"),"[&DQk=]");
        assert(Parse(url,header,linked,error)&&linked.templateCode.empty());
    }
    assert(PositionalIds("1,0,3,4,5")==std::vector<uint32_t>({1,0,3,4,5}));
    assert(PositionalIds("1,,3").empty());
    return 0;
}
