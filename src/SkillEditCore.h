#pragma once
#include <cstdint>
#include <optional>
#include <array>
#include <algorithm>
#include <limits>
#include <cstddef>
namespace AlterEgo::SkillEdit {
enum class SlotEvidence { Unrecorded, Empty, Recorded };
struct DisplaySlot {
    uint32_t id=0;
    SlotEvidence evidence=SlotEvidence::Unrecorded;
};
inline DisplaySlot SlotDisplay(uint32_t id,bool emptyKnown=false) {
    return {id,id?SlotEvidence::Recorded:emptyKnown?SlotEvidence::Empty:SlotEvidence::Unrecorded};
}
inline const char* SlotRole(std::size_t index) {
    return index==0?"Heal":index==4?"Elite":"Utility";
}
inline const char* SlotLabel(std::size_t index) {
    static constexpr const char* labels[]={"Heal","Utility 1","Utility 2","Utility 3","Elite"};
    return index<5?labels[index]:"Skill";
}
inline const char* MissingSlotLabel(SlotEvidence evidence) {
    return evidence==SlotEvidence::Empty?"Empty":"Not recorded";
}
template<class Bar> std::array<DisplaySlot,5> DisplayBar(const Bar& bar,uint8_t emptyMask=0) {
    const std::array<uint32_t,5> ids={bar.heal,bar.utilities[0],bar.utilities[1],bar.utilities[2],bar.elite};
    std::array<DisplaySlot,5> result{};
    for(std::size_t i=0;i<ids.size();++i)result[i]=SlotDisplay(ids[i],(emptyMask&(1u<<i))!=0);
    return result;
}
template<class Json> DisplaySlot JsonDisplaySlot(const Json& value,uint32_t maximum=std::numeric_limits<uint32_t>::max()) {
    if(value.is_number_unsigned()){
        const auto n=value.template get<uint64_t>();
        if(n<=maximum)return SlotDisplay(static_cast<uint32_t>(n),true);
    }else if(value.is_number_integer()){
        const auto n=value.template get<int64_t>();
        if(n>=0&&static_cast<uint64_t>(n)<=maximum)return SlotDisplay(static_cast<uint32_t>(n),true);
    }
    return {};
}
template<class Json> std::array<DisplaySlot,5> JsonDisplayBar(const Json& bar) {
    std::array<DisplaySlot,5> slots{};
    if(!bar.is_object())return slots;
    if(bar.contains("heal"))slots[0]=JsonDisplaySlot(bar.at("heal"));
    if(bar.contains("elite"))slots[4]=JsonDisplaySlot(bar.at("elite"));
    if(bar.contains("utilities")&&bar.at("utilities").is_array())
        for(std::size_t i=0;i<3&&i<bar.at("utilities").size();++i)
            slots[i+1]=JsonDisplaySlot(bar.at("utilities")[i]);
    return slots;
}
template<class Json> uint8_t RecordedCompanionSlots(const Json& array,uint32_t maximum) {
    uint8_t mask=0;
    if(array.is_array())for(std::size_t i=0;i<4&&i<array.size();++i)
        if(JsonDisplaySlot(array[i],maximum).evidence!=SlotEvidence::Unrecorded)mask|=static_cast<uint8_t>(1u<<i);
    return mask;
}
// One width policy for read-only bars, editable bars and catalog strips.
inline int IconColumns(float available,float size,float spacing,int maximum) {
    if(maximum<1||size<=0||spacing<0||available<=0)return 1;
    const float pitch=size+spacing;
    if(available+spacing>=pitch*maximum)return maximum;
    return std::max(1,static_cast<int>((available+spacing)/pitch));
}

template<class Json> std::optional<bool> NoUnderwaterFlag(const Json& row) {
    if(!row.is_object()||!row.contains("flags")||!row.at("flags").is_array())return std::nullopt;
    bool restricted=false;
    for(const auto& flag:row.at("flags")){
        if(!flag.is_string())return std::nullopt;
        if(flag=="NoUnderwater")restricted=true;
    }
    return restricted;
}
inline bool EnvironmentChoiceAllowed(bool aquatic,std::optional<bool> noUnderwater) {
    return !aquatic||!noUnderwater.value_or(false);
}
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
