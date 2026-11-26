#pragma once
#include "GearEditCore.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
namespace AlterEgo::GearEdit {
using GearJson=nlohmann::json;
// Preserve unknown slot fields on explicit mode changes. The standalone
// Infusions component owns only terrestrial infusion fields, not aquatic gear.
inline void ClearComponentInfusionSlots(GearJson& data) {
    if(!data.contains("gear")||!data["gear"].is_object())return;
    auto& gear=data["gear"];
    for(auto it=gear.begin();it!=gear.end();){
        bool removed=false;
        if(it.value().is_object())for(const char* key:{"infusion_ids","infusion_attributes","infusion_snapshot","infusion"})
            removed=it.value().erase(key)>0||removed;
        if(removed&&it.value().empty())it=gear.erase(it);else ++it;
    }
    if(gear.empty())data.erase("gear");
}
inline void ClearComponentInfusionTotals(GearJson& data) {
    for(const char* key:{"infusion_totals","infusion_summary_present","infusion_ar_total","infusion_stat_totals"})data.erase(key);
}
inline bool ComponentOwnsField(const std::string& kind,const std::string& slot,CopyField field){
    const bool water=kind=="aquatic"&&EquipmentChoice::IsAquaticGearSlot(slot);
    const bool armor=slot=="Helm"||slot=="Shoulders"||slot=="Coat"||slot=="Gloves"||slot=="Leggings"||slot=="Boots";
    const bool weapon=slot=="WeaponA1"||slot=="WeaponA2"||slot=="WeaponB1"||slot=="WeaponB2";
    const bool trinket=slot=="Backpack"||slot=="Accessory1"||slot=="Accessory2"||slot=="Amulet"||slot=="Ring1"||slot=="Ring2";
    if(field==CopyField::Stats)return water||(kind=="armor"&&armor)||(kind=="trinkets"&&trinket)||(kind=="weapons"&&weapon);
    if(field==CopyField::Upgrade)return water||(kind=="runes"&&armor)||(kind=="sigils"&&weapon);
    if(field==CopyField::Infusions)return (water||(kind=="infusions"&&(armor||weapon||trinket)))&&slot!="Amulet"&&slot!="Relic";
    return field==CopyField::Relic&&kind=="relic";
}
// Accept only known, typed fields. Never reinterpret an unfamiliar imported
// representation as an empty value or silently overwrite it on paste.
template<class Slot> bool ReadComponentField(const GearJson& part,CopyField field,
    bool armor,Slot& out,bool requirePresent=true){
    if(!part.is_object())return false;
    bool any=false;
    auto id=[&](const char* key,uint32_t& dest){
        if(!part.contains(key))return true;
        any=true;const auto& v=part.at(key);
        if(!v.is_number_integer()||v<0||v>std::numeric_limits<uint32_t>::max())return false;
        dest=v.get<uint32_t>();return true;
    };
    auto str=[&](const char* key,std::string& dest){
        if(!part.contains(key))return true;
        any=true;const auto& v=part.at(key);if(!v.is_string())return false;
        dest=v.get<std::string>();return true;
    };
    bool ok=false;
    if(field==CopyField::Stats)ok=id("stat_id",out.stat_id)&&str("stat_name",out.stat_name);
    if(field==CopyField::Upgrade){
        if(armor)ok=id("rune_id",out.rune_id)&&str("rune",out.rune);
        else ok=id("sigil_id",out.sigil_id)&&str("sigil",out.sigil)&&id("sigil2_id",out.sigil2_id)&&str("sigil2",out.sigil2);
    }
    if(field==CopyField::Infusions){
        ok=true;
        if(part.contains("infusion_ids")){
            any=true;const auto& a=part.at("infusion_ids");
            if(!a.is_array())return false;
            for(const auto& v:a){
                if(!v.is_number_integer()||v<0||v>std::numeric_limits<uint32_t>::max())return false;
                out.infusion_ids.push_back(v.get<uint32_t>());
            }
        }
        if(part.contains("infusion_attributes")){
            any=true;const auto& a=part.at("infusion_attributes");
            if(!a.is_array())return false;
            for(const auto& v:a){if(!v.is_string())return false;out.infusion_attributes.push_back(v.get<std::string>());}
        }
        if(part.contains("infusion_snapshot")){
            any=true;if(!part.at("infusion_snapshot").is_boolean())return false;
            out.infusion_snapshot=part.at("infusion_snapshot").get<bool>();
        }
        if(!str("infusion",out.infusion))return false;
        if(requirePresent&&!HasInfusionRequirements(out))return false;
        if(requirePresent&&!out.infusion.empty()&&out.infusion_ids.empty()&&out.infusion_attributes.empty()&&!out.infusion_snapshot)return false;
    }
    return ok&&(!requirePresent||any);
}
template<class Slot> void WriteComponentField(GearJson& part,CopyField field,bool armor,const Slot& value){
    auto id=[&](const char* key,uint32_t v){if(v)part[key]=v;else part.erase(key);};
    auto str=[&](const char* key,const std::string& v){if(!v.empty())part[key]=v;else part.erase(key);};
    if(field==CopyField::Stats){id("stat_id",value.stat_id);str("stat_name",value.stat_name);}
    if(field==CopyField::Upgrade){
        if(armor){id("rune_id",value.rune_id);str("rune",value.rune);}
        else {id("sigil_id",value.sigil_id);str("sigil",value.sigil);id("sigil2_id",value.sigil2_id);str("sigil2",value.sigil2);}
    }
    if(field==CopyField::Infusions){
        part["infusion_ids"]=value.infusion_ids;part["infusion_attributes"]=value.infusion_attributes;
        part["infusion_snapshot"]=value.infusion_snapshot;str("infusion",value.infusion);
    }
}
template<class Slot> bool ApplyComponentField(GearJson& dest,CopyField field,bool armor,
    const Slot& source,int capacity,bool capacityVerified=true){
    Slot value{};
    if(!ReadComponentField(dest,field,armor,value,false))return false;
    if(field==CopyField::Stats)CopyStats(value,source);
    else if(field==CopyField::Upgrade){if(armor)CopyRune(value,source);else CopySigils(value,source,capacity);}
    else if(field==CopyField::Infusions){if(capacity<=0||!CopyInfusions(value,source,capacity,capacityVerified))return false;}
    else return false;
    WriteComponentField(dest,field,armor,value);return true;
}
}
