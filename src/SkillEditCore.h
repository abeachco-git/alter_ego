#pragma once
#include <cstdint>
namespace AlterEgo::SkillEdit {
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
