#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
namespace AlterEgo::TooltipCore {
// Pure, display-only helpers. Never infer exact identities or empty slots.
inline const char* FriendlyAttribute(const std::string& attr){
    if(attr=="CritDamage")return "Ferocity";
    if(attr=="ConditionDamage")return "Condition Damage";
    if(attr=="ConditionDuration")return "Expertise";
    if(attr=="BoonDuration")return "Concentration";
    if(attr=="Healing"||attr=="HealingPower")return "Healing Power";
    return attr.c_str();
}
inline std::string EntityName(const std::string& known,const std::string& stored={}){
    return known.empty()?stored:known;
}
// Known public skill/trait fact shapes only. No inferred damage, character
// attributes, trait application, competitive-mode values or unknown facts.
template<class Json> std::vector<std::string> FactLines(const Json& facts){
    std::vector<std::string> out;
    if(!facts.is_array())return out;
    for(const auto& fact:facts){
        if(out.size()>=32)break;
        if(!fact.is_object())continue;
        auto str=[&](const char* k){
            auto it=fact.find(k);
            return it!=fact.end()&&it->is_string()&&it->template get_ref<const std::string&>().size()<=512?
                it->template get<std::string>():std::string{};
        };
        auto num=[&](const char* k){
            auto it=fact.find(k);
            if(it==fact.end()||!it->is_number())return std::string{};
            auto v=it->dump();return v.size()<=24?v:std::string{};
        };
        const auto type=str("type"), text=str("text");
        std::string line;
        auto value=[&](const char* field,const char* unit,const char* fallback){
            const auto v=num(field);
            if(!v.empty())line=(text.empty()?std::string(fallback):text)+": "+v+unit;
        };
        if(type=="Recharge")value("value"," s","Recharge");
        else if(type=="Range"||type=="Radius"||type=="Distance")value("value","",type.c_str());
        else if(type=="Number")value("value","","");
        else if(type=="Percent")value("percent","%","");
        else if(type=="Time")value("duration"," s","Duration");
        else if(type=="Damage"){const auto hits=num("hit_count");if(!hits.empty())line="Hits: "+hits;}
        else if(type=="ComboField"||type=="ComboFinisher"){
            const auto field=str(type=="ComboField"?"field_type":"finisher_type");
            if(!field.empty())line=(text.empty()?(type=="ComboField"?"Combo field":"Combo finisher"):text)+": "+field;
        }else if(type=="Buff"){
            const auto status=str("status"), duration=num("duration"), count=num("apply_count");
            if(!status.empty()){line=status;if(!count.empty())line+=" x"+count;if(!duration.empty())line+=" ("+duration+" s)";}
        }else if(type=="AttributeAdjust"){
            const auto attr=str("target"),v=num("value");
            if(!attr.empty()&&!v.empty())line=std::string(FriendlyAttribute(attr))+": "+v;
        }else if(type=="Unblockable"||type=="StunBreak")line=text.empty()?(type=="Unblockable"?"Unblockable":"Breaks stun"):text;
        if(!line.empty()&&line.front()!=':')out.push_back(std::move(line));
    }
    return out;
}
// Dye remover has its own API value; callers own evidence of channel presence.
inline std::string DyeName(int id,const std::string& known){
    if(id==0)return "Empty";
    if(id==1)return "Dye Remover";
    return known;
}
}
