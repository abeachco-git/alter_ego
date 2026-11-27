#include "../src/SkillEditCore.h"
#include "../src/ArtworkRequest.h"
#include <cassert>
#include <nlohmann/json.hpp>
struct TestBar { uint32_t heal=0, utilities[3]={0,0,0}, elite=0; };
int case_skill_edit_core(int,char**) {
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
