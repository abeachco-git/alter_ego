#pragma once
#include <cstdint>
#include <optional>
#include <array>
#include <algorithm>
#include <limits>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include "ChatLink.h"
namespace AlterEgo::SkillEdit {
enum class SlotEvidence { Unrecorded, Empty, Recorded, Unresolved };
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
    return evidence==SlotEvidence::Unresolved?"Unresolved selection":evidence==SlotEvidence::Empty?"Empty":"Not recorded";
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
// Keep the typed skill bar authoritative. Raw wire evidence only describes
// the original slot; explicit replacement retires it even for zero -> zero.
inline DisplaySlot WithPaletteEvidence(DisplaySlot slot,const std::optional<RevenantActivePaletteEvidence>& evidence,bool aquatic,size_t index){
    if(!evidence||index>=5||slot.id)return slot;
    const auto& row=(*evidence)[(aquatic?5:0)+index];
    if(!row[1])slot.evidence=row[0]?SlotEvidence::Unresolved:SlotEvidence::Empty;
    return slot;
}
inline std::array<DisplaySlot,5> WithPaletteBar(std::array<DisplaySlot,5> slots,
    const std::optional<RevenantActivePaletteEvidence>& evidence,bool aquatic){
    for(size_t i=0;i<slots.size();++i)slots[i]=WithPaletteEvidence(slots[i],evidence,aquatic,i);
    return slots;
}
template<class Json> std::optional<RevenantActivePaletteEvidence> ReadPaletteEvidence(const Json& data){
    if(!data.is_object()||!data.contains("rev_active_palettes"))return std::nullopt;
    const auto& rows=data.at("rev_active_palettes");
    if(!rows.is_array()||rows.size()!=10)return std::nullopt;
    RevenantActivePaletteEvidence result{};
    for(size_t i=0;i<10;++i){
        if(!rows[i].is_array()||rows[i].size()!=5)return std::nullopt;
        for(size_t j=0;j<5;++j){
            const auto slot=JsonDisplaySlot(rows[i][j]);
            if(slot.evidence==SlotEvidence::Unrecorded)return std::nullopt;
            result[i][j]=slot.id;
        }
    }
    return ValidRevenantActivePaletteEvidence(result)?std::optional<RevenantActivePaletteEvidence>(result):std::nullopt;
}
inline bool RetirePaletteEdits(std::optional<RevenantActivePaletteEvidence>& evidence,bool aquatic,
    const std::array<DisplaySlot,5>& before,const std::array<DisplaySlot,5>& after,uint8_t explicitSlots=0){
    if(!evidence)return false;
    bool changed=false;
    for(size_t i=0;i<5;++i)if(before[i].id!=after[i].id||(explicitSlots&(1u<<i))){
        auto& row=(*evidence)[(aquatic?5:0)+i];
        if(row!=std::array<uint32_t,5>{}){row={};changed=true;}
    }
    return changed;
}
template<class Bar> bool SetSlotWithPaletteEvidence(Bar& bar,
    std::optional<RevenantActivePaletteEvidence>& evidence,bool aquatic,int slot,uint32_t next);
// A present but unrepresentable selection is neither empty nor omitted.
struct CompanionProjection {
    std::array<uint32_t,4> values{};
    uint8_t recorded=0, unresolved=0;
    bool Known(std::size_t i)const{return (recorded&(1u<<i))!=0;}
    bool Unresolved(std::size_t i)const{return (unresolved&(1u<<i))!=0;}
    void Set(std::size_t i,uint32_t id){
        if(i>=values.size())return;
        values[i]=id;recorded|=static_cast<uint8_t>(1u<<i);
        unresolved&=static_cast<uint8_t>(~(1u<<i));
    }
};
template<class Json> CompanionProjection JsonCompanionSlots(const Json& array,uint32_t maximum) {
    CompanionProjection result;
    if(array.is_array())for(std::size_t i=0;i<4&&i<array.size();++i){
        if(array[i].is_null())continue; // explicit gap, not a verified empty choice
        const auto slot=JsonDisplaySlot(array[i],maximum);
        if(slot.evidence==SlotEvidence::Unrecorded)result.unresolved|=static_cast<uint8_t>(1u<<i);
        else result.Set(i,slot.id);
    }
    return result;
}
template<class Json> uint8_t RecordedCompanionSlots(const Json& array,uint32_t maximum) {
    return JsonCompanionSlots(array,maximum).recorded;
}
// Keep trailing and unrepresentable source values until explicitly replaced.
template<class Json> void SetCompanionPayloadSlot(Json& payload,std::size_t i,uint32_t id) {
    if(!payload.is_array()||i>=4)return;
    while(payload.size()<=i)payload.push_back(nullptr);
    payload[i]=id;
}
template<class Json> Json OverlayCompanionSlots(const Json& original,const std::array<uint32_t,4>& values,uint32_t maximum) {
    Json result=original.is_array()?original:Json::array();
    const auto prior=JsonCompanionSlots(result,maximum);
    for(std::size_t i=0;i<values.size();++i)
        if(values[i]!=prior.values[i])SetCompanionPayloadSlot(result,i,values[i]);
    return result;
}
// Existing typed character-cache legend columns store strings. An internal
// control-prefixed marker (invalid as an API identifier) distinguishes a
// newly observed explicit null from old empty-string defaults without
// migrating the rebuildable cache. It is never exposed in the Library.
inline constexpr const char* CachedEmptyLegend="\x01" "AE_EMPTY_LEGEND";
struct RetrievedLegendSlot {std::string identifier;bool explicitlyEmpty=false;};
template<class Json> RetrievedLegendSlot ReadRetrievedLegendSlot(const Json& value,bool cached){
    if(value.is_null())return {{},!cached}; // API documents null as empty, cache null is missing
    if(!value.is_string())return {};
    const auto name=value.template get<std::string>();
    if(cached&&name==CachedEmptyLegend)return {{},true};
    if(name.size()>256||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))return {};
    return {name,false};
}
inline std::string EncodeCachedLegendSlot(const std::string& identifier,bool explicitlyEmpty){
    return explicitlyEmpty&&identifier.empty()?std::string(CachedEmptyLegend):identifier;
}
// Build a sparse payload from retrieved evidence. Unknown interior
// slots remain null; nonempty unmapped native legend identifiers are preserved
// verbatim as unresolved strings, not manufactured numeric protocol codes.
template<class Json,class LegendCode> Json CaptureCompanionPayload(
    const std::array<uint32_t,4>& pets,const std::array<std::string,4>& legends,
    bool legend,LegendCode code,uint8_t legendEmpty=0){
    Json result=Json::array();
    for(size_t i=0;i<4;++i){
        if(legend){
            if(legends[i].empty()){
                if(legendEmpty&(1u<<i))SetCompanionPayloadSlot(result,i,0);
                continue;
            }
            const auto id=code(legends[i]);
            if(id)SetCompanionPayloadSlot(result,i,id);
            else{
                while(result.size()<=i)result.push_back(nullptr);
                result[i]=legends[i];
            }
        }else if(pets[i])SetCompanionPayloadSlot(result,i,pets[i]);
    }
    return result;
}
// The GW2 wire format has no missing-slot marker. Reject a partially
// recorded pair or unresolved selection instead of manufacturing an empty.
// Entirely unrecorded environments retain the existing optional wire default.
inline bool CompanionPairsRepresentable(const CompanionProjection& slots){
    if(slots.unresolved)return false;
    for(size_t env=0;env<2;++env){
        const auto pair=(slots.recorded>>(2*env))&3u;
        if(pair==1||pair==2)return false;
    }
    return true;
}
inline const char* CompanionMissingLabel(const CompanionProjection& slots,std::size_t i) {
    return slots.Unresolved(i)?"Unresolved selection":slots.Known(i)?"Empty":"Not recorded";
}
// A duplicate is verified incompatible; different positive identities alone
// cannot verify specialization or habitat eligibility.
enum class PairContext { Unknown, Incompatible };
template<class Canonical> PairContext CompanionPairContext(const CompanionProjection& slots,std::size_t env,Canonical canonical) {
    if(env>=2)return PairContext::Unknown;
    const auto a=slots.values[env*2],b=slots.values[env*2+1];
    if(a&&b&&canonical(a)==canonical(b))return PairContext::Incompatible;
    return PairContext::Unknown;
}
// Specialization compatibility is independent of habitat and stance order.
enum class ContextVerdict { Compatible, Incompatible, Unknown };
// Canonical native wire codes, not guessed from display names or API suffixes.
// Documented build template codes and profession specialization mechanics.
inline const char* LegendRequiredElite(uint32_t canonicalCode) {
    switch(canonicalCode){
    case 1:return "Herald";
    case 5:return "Renegade";
    case 7:return "Vindicator";
    case 8:return "Conduit";
    default:return nullptr;
    }
}
// Empty string is a verified non-elite specialization; nullopt is unknown.
using EliteContext=std::optional<std::string>;
inline ContextVerdict LegendSpecialization(uint32_t canonicalCode,const EliteContext& context) {
    if(canonicalCode==2||canonicalCode==3||canonicalCode==4||canonicalCode==6)
        return ContextVerdict::Compatible; // specialization dimension only
    const auto* required=LegendRequiredElite(canonicalCode);
    if(!required||!context)return ContextVerdict::Unknown;
    return *context==required?ContextVerdict::Compatible:ContextVerdict::Incompatible;
}
inline bool LegendChoiceAllowed(uint32_t canonicalCode,const std::vector<EliteContext>& contexts) {
    return std::none_of(contexts.begin(),contexts.end(),[&](const auto& context){
        return LegendSpecialization(canonicalCode,context)==ContextVerdict::Incompatible;
    });
}
inline ContextVerdict SkillSpecialization(uint32_t required,std::optional<uint32_t> selected) {
    if(!required||!selected)return ContextVerdict::Unknown;
    return required==*selected?ContextVerdict::Compatible:ContextVerdict::Incompatible;
}
// A core or elite conclusion requires all three known specialization records
// and valid placement. An imported earlier elite or duplicate is anomalous;
// do not invent a compatible context or reject an existing choice.
template<class Lookup> EliteContext ResolveEliteContext(const std::array<uint32_t,3>& ids,Lookup lookup){
    if(!ids[0]||!ids[1]||!ids[2]||ids[0]==ids[1]||ids[0]==ids[2]||ids[1]==ids[2])return std::nullopt;
    for(size_t i=0;i<ids.size();++i){
        auto spec=lookup(ids[i]);
        if(!spec||(i<2&&!spec->empty()))return std::nullopt;
        if(i==2)return spec;
    }
    return std::nullopt;
}
template<class Json> std::array<uint32_t,3> ContextSpecializations(const Json& raw){
    std::array<uint32_t,3> ids{};
    if(!raw.is_object()||!raw.contains("specializations"))return ids;
    const auto& specs=raw.at("specializations");
    if(!specs.is_array()||specs.size()!=3)return ids;
    for(size_t i=0;i<ids.size();++i)if(specs[i].is_object()&&specs[i].contains("id"))
        ids[i]=JsonDisplaySlot(specs[i].at("id")).id;
    return ids;
}
template<class Json> uint32_t ThirdSpecialization(const Json& raw) {
    if(!raw.is_object()||!raw.contains("specializations"))return 0;
    const auto& specs=raw.at("specializations");
    if(!specs.is_array()||specs.size()<3||!specs[2].is_object()||!specs[2].contains("id"))return 0;
    return JsonDisplaySlot(specs[2].at("id")).id;
}
inline std::string LegendContextWarning(uint32_t canonicalCode,const EliteContext& context) {
    if(LegendSpecialization(canonicalCode,context)!=ContextVerdict::Incompatible)return {};
    return std::string("Legend requires ")+LegendRequiredElite(canonicalCode);
}
template<class Json> std::vector<std::string> RevenantContextWarnings(const Json& raw,
    const EliteContext& elite,std::optional<uint32_t> selectedElite,
    const std::function<uint32_t(uint32_t)>& canonical,
    const std::function<std::optional<uint32_t>(uint32_t)>& requiredElite,
    const std::function<std::optional<bool>(uint32_t)>& noUnderwater) {
    std::vector<std::string> out;
    if(!raw.is_object()||!raw.contains("profession")||raw.at("profession")!="Revenant")return out;
    auto label=[](bool aquatic){return aquatic?"Aquatic":"Terrestrial";};
    if(raw.contains("legend_codes")){
        const auto slots=JsonCompanionSlots(raw.at("legend_codes"),255);
        for(int i=0;i<4;++i){
            const auto warning=LegendContextWarning(canonical(slots.values[i]),elite);
            if(!warning.empty())out.push_back(std::string(label(i>=2))+" legend "+std::to_string(i%2+1)+": "+warning);
        }
    }
    for(int env=0;env<2;++env){
        const char* key=env?"aquatic_skills":"skills";
        if(!raw.contains(key))continue;
        const auto slots=JsonDisplayBar(raw.at(key));
        for(std::size_t i=0;i<slots.size();++i)if(slots[i].id){
            const auto id=slots[i].id;
            const auto prefix=std::string(label(env!=0))+" "+SlotLabel(i)+": ";
            const auto spec=requiredElite(id);
            if(spec&&SkillSpecialization(*spec,selectedElite)==ContextVerdict::Incompatible)
                out.push_back(prefix+"requires a different elite specialization");
            if(env&&noUnderwater(id).value_or(false))
                out.push_back(prefix+"skill is marked NoUnderwater");
        }
    }
    return out;
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
template<class Bar> bool SetSlotWithPaletteEvidence(Bar& bar,
    std::optional<RevenantActivePaletteEvidence>& evidence,bool aquatic,int slot,uint32_t next){
    if(slot<0||slot>=5)return false;
    const auto before=DisplayBar(bar);
    const bool changed=SetSlot(bar,slot,next);
    const bool retired=RetirePaletteEdits(evidence,aquatic,before,DisplayBar(bar),
        static_cast<uint8_t>(1u<<slot));
    return changed||retired;
}
}
