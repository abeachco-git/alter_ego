#pragma once
#include <cstdint>
namespace AlterEgo::SkillEdit {
template<class Ids> bool SpecializationChoiceAllowed(const Ids& ids,int slot,uint32_t next,bool elite) {
    if(slot<0||slot>=3||(elite&&slot!=2))return false;
    if(next)for(int i=0;i<3;++i)if(i!=slot&&ids[i]==next)return false;
    return true;
}
// Only the other slot of the same environment participates in duplicate
// validation. The caller never mirrors or mutates the other pair.
inline bool PairChoiceAllowed(uint32_t other,uint32_t next) {
    return !next||other!=next;
}
template<class Value> bool SetPairChoice(Value& current,uint32_t other,uint32_t next) {
    if(!PairChoiceAllowed(other,next)||current==next)return false;
    current=next;return true;
}
template<class Utilities> bool SetUtility(Utilities& u,int index,uint32_t next) {
    if(index<0||index>=3)return false;
    const auto previous=u[index];
    if(previous==next)return false;
    if(next)for(int i=0;i<3;++i)if(i!=index&&u[i]==next)u[i]=previous;
    u[index]=next;return true;
}
template<class Bar> bool SetSlot(Bar& bar,int slot,uint32_t next) {
    if(slot>=1&&slot<=3)return SetUtility(bar.utilities,slot-1,next);
    if(slot!=0&&slot!=4)return false;
    auto& field=slot==0?bar.heal:bar.elite;
    if(field==next)return false;
    field=next;return true;
}
}
