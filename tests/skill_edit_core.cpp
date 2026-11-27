#include "../src/SkillEditCore.h"
#include "../src/ChatLink.h"
#include "../src/ArtworkRequest.h"
#include "../src/CatalogRowCore.h"
#include <cassert>
#include <nlohmann/json.hpp>
struct TestBar { uint32_t heal=0, utilities[3]={0,0,0}, elite=0; };
struct TestLinkBuild {
    std::string profession="Ranger";
    struct Spec { uint32_t spec_id=0;int traits[3]={}; } specializations[3];
    TestBar terrestrial_skills,aquatic_skills;
    struct Pets { uint32_t terrestrial[2]={},aquatic[2]={}; } pets;
    std::vector<uint32_t> weapons;
};
int case_skill_edit_core(int,char**) {
    {
        using namespace AlterEgo;
        TestLinkBuild b;
        b.specializations[0].spec_id=255;b.specializations[0].traits[1]=3;
        b.terrestrial_skills.heal=101;b.aquatic_skills.utilities[1]=102;
        b.pets.terrestrial[0]=255;b.pets.aquatic[1]=9;b.weapons={65535};
        auto trait=[](uint32_t,int,int v)->std::optional<int>{return v;};
        auto palette=[](uint32_t id)->uint32_t{return id==101?65535:id==102?22:0;};
        auto legend=[](int)->std::optional<uint32_t>{return 0;};
        auto project=[&]{return ProjectDerivedBuildLink(b,trait,palette,legend);};
        auto link=project();
        assert(link&&link->profession==PROF_RANGER&&link->specs[0].spec_id==255&&link->specs[0].traits[1]==3);
        assert(link->terrestrial_skills[0]==65535&&link->aquatic_skills[2]==22&&link->pets[0]==255&&link->pets[3]==9&&link->weapons[0]==65535);
        b.pets.aquatic[1]=256;assert(!project());b.pets.aquatic[1]=9;
        b.specializations[0].spec_id=256;assert(!project());b.specializations[0].spec_id=255;
        b.specializations[0].traits[1]=4;assert(!project());b.specializations[0].traits[1]=3;
        b.aquatic_skills.elite=103;assert(!project());b.aquatic_skills.elite=0;
        b.weapons={65536};assert(!project());b.weapons.assign(256,1);assert(!project());b.weapons.clear();
        auto unresolvedTrait=[](uint32_t,int,int)->std::optional<int>{return std::nullopt;};
        assert(!ProjectDerivedBuildLink(b,unresolvedTrait,palette,legend));
        b.profession="Revenant";
        auto unresolvedLegend=[](int i)->std::optional<uint32_t>{return i==2?std::nullopt:std::optional<uint32_t>(13);};
        assert(!ProjectDerivedBuildLink(b,trait,palette,unresolvedLegend));
        auto oversizedLegend=[](int)->std::optional<uint32_t>{return 256;};
        assert(!ProjectDerivedBuildLink(b,trait,palette,oversizedLegend));
        auto knownLegend=[](int i)->std::optional<uint32_t>{return static_cast<uint32_t>(13+i);};
        InactiveLegendPalettes raw={1,0,65535,4,5,6};
        auto rev=ProjectDerivedBuildLink(b,trait,palette,knownLegend,raw);
        assert(rev&&rev->legends[3]==16&&rev->inactive_rev_skills[2]==65535);
        b.profession="Unknown";assert(!project());
    }
    using namespace AlterEgo::SkillEdit;
    TestBar land{1,{2,3,4},5},water{6,{7,8,9},10};
    assert(SetSlot(land,2,4));
    assert(land.utilities[1]==4&&land.utilities[2]==3);
    assert(water.utilities[1]==8&&water.utilities[2]==9);
    assert(SetSlot(water,0,0)&&land.heal==1);
    assert(SetSlot(water,4,11)&&land.elite==5);
    assert(!SetSlot(water,4,11)&&!SetSlot(water,5,1));
    assert(SetSlot(land,1,0)&&land.utilities[2]==3);
    nlohmann::json u={2,3,4,99};
    assert(SetUtility(u,0,4));
    assert(u==nlohmann::json({4,3,2,99}));
    assert(!SetUtility(u,0,4));
    assert(SetUtility(u,1,0)&&u==nlohmann::json({4,0,2,99}));
    uint32_t pets[4]={1,2,3,4};
    assert(!SetPairChoice(pets[0],pets[1],2));
    assert(SetPairChoice(pets[0],pets[1],3));
    assert(pets[2]==3&&pets[3]==4);
    assert(SetPairChoice(pets[2],pets[3],0)&&pets[0]==3);
    uint32_t specs[3]={1,2,0};
    assert(!SpecializationChoiceAllowed(specs,0,3,true));
    assert(!SpecializationChoiceAllowed(specs,2,2,false));
    assert(SpecializationChoiceAllowed(specs,2,3,true));
    assert(SpecializationChoiceAllowed(specs,0,0,false));
    using J=nlohmann::json;
    assert(AlterEgo::UI::CatalogRowPitch(14,30)==36);
    assert(AlterEgo::UI::CatalogRowPitch(20)==26);
    assert(AlterEgo::UI::CatalogGridColumns(127,40,4)==2);
    assert(AlterEgo::UI::CatalogGridColumns(128,40,4)==3);
    assert(AlterEgo::UI::CatalogWikiSearchUrl("Juvenile A & B/#")==
        "https://wiki.guildwars2.com/wiki/Special:Search?search=Juvenile%20A%20%26%20B%2F%23");
    assert(AlterEgo::UI::CatalogWikiSearchUrl("").empty());
    {
        const auto bar=DisplayBar(TestBar{0,{2,0,4},0},1u|16u);
        assert(bar[0].evidence==SlotEvidence::Empty&&bar[1].id==2);
        assert(bar[2].evidence==SlotEvidence::Unrecorded&&bar[4].evidence==SlotEvidence::Empty);
        const auto partial=JsonDisplayBar(J{{"heal",0},{"utilities",J::array({17,-1})}});
        assert(partial[0].evidence==SlotEvidence::Empty&&partial[1].id==17);
        assert(partial[2].evidence==SlotEvidence::Unrecorded&&partial[3].evidence==SlotEvidence::Unrecorded);
        assert(JsonDisplaySlot(J(256),255).evidence==SlotEvidence::Unrecorded);
        assert(RecordedCompanionSlots(J::array({0,3,-1,256}),255)==3);
        assert(RecordedCompanionSlots(J::array({1,0}),255)==3);
        assert(RecordedCompanionSlots(J::object(),255)==0);
        assert(IconColumns(214,40,4,5)==4&&IconColumns(216,40,4,5)==5);
        assert(IconColumns(0,40,4,5)==1&&IconColumns(800,40,4,5)==5);
    }

    assert(!NoUnderwaterFlag(J::object()).has_value());
    assert(!NoUnderwaterFlag(J{{"flags",J::array({"NoUnderwater",4})}}).has_value());
    assert(NoUnderwaterFlag(J{{"flags",J::array()}})==false);
    assert(NoUnderwaterFlag(J{{"flags",J::array({"NoUnderwater","GroundTargeted"})}})==true);
    assert(EnvironmentChoiceAllowed(false,true));
    assert(!EnvironmentChoiceAllowed(true,true));
    assert(EnvironmentChoiceAllowed(true,false)&&EnvironmentChoiceAllowed(true,std::nullopt));
    using namespace AlterEgo::ArtworkRequest;
    const auto skillKey=SkillKey(28085),legendKey=SkillKey(28085),petKey=PetKey(4);
    assert(skillKey==5028085&&legendKey==skillKey&&petKey==9400004);
    assert(!SkillKey(0)&&!SkillKey(UINT32_MAX)&&!PetKey(0)&&!PetKey(256));
    int texture=1,lookups=0,requests=0;uint32_t requestedKey=0;bool requestedPriority=false;
    uint32_t cachedKey=skillKey;
    auto lookup=[&](uint32_t key)->int*{++lookups;return key==cachedKey?&texture:nullptr;};
    auto request=[&](uint32_t key,const std::string& url,bool priority){
        assert(url=="artwork");++requests;requestedKey=key;requestedPriority=priority;
    };
    assert(Resolve(skillKey,"",false,lookup,request)==&texture);
    assert(Resolve(legendKey,"artwork",true,lookup,request)==&texture&&requests==0);
    assert(Resolve(petKey,"",false,lookup,request)==nullptr&&requests==0);
    assert(Resolve(petKey,"artwork",true,lookup,request)==nullptr&&requests==1&&requestedKey==petKey&&requestedPriority);
    cachedKey=petKey;
    assert(Resolve(petKey,"",false,lookup,request)==&texture&&requests==1);
    assert(Resolve(skillKey,"artwork",false,lookup,request)==nullptr&&requests==2&&requestedKey==skillKey&&!requestedPriority);
    const int priorLookups=lookups;
    assert(Resolve(0,"artwork",true,lookup,request)==nullptr&&lookups==priorLookups&&requests==2);
    return 0;
}
