#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <cstdint>
#include <limits>
#include <optional>
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
// Definition-only facts. No instance ownership, binding, selected stats or
// inferred effective effects. Consumers choose the parts appropriate to context.
enum ItemDefinitionPart : unsigned {
    ItemType=1u, ItemCombat=2u, ItemAttributes=4u, ItemEffects=8u,
    ItemDescription=16u, ItemAll=31u
};
struct ItemDefinitionLine { unsigned part; std::string text; };
inline const char* WeaponTypeLabel(const std::string& name){
    if(name=="Harpoon")return "Spear";
    if(name=="Speargun")return "Harpoon Gun";
    return name.c_str();
}
template<class Json> std::vector<ItemDefinitionLine> ItemDefinitionLines(
    const std::string& type,const std::string& rarity,const std::string& description,
    const Json& details,unsigned parts=ItemAll){
    std::vector<ItemDefinitionLine> out;
    auto add=[&](unsigned part,std::string line){if((parts&part)&&!line.empty())out.push_back({part,std::move(line)});};
    auto str=[](const Json& j,const char* key){
        if(!j.is_object())return std::string{};
        auto it=j.find(key);return it!=j.end()&&it->is_string()?it->template get<std::string>():std::string{};
    };
    auto integer=[](const Json& j,const char* key)->std::optional<int64_t>{
        if(!j.is_object())return std::nullopt;
        auto it=j.find(key);if(it==j.end())return std::nullopt;
        if(it->is_number_unsigned()){
            const auto v=it->template get<uint64_t>();
            if(v>static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))return std::nullopt;
            return static_cast<int64_t>(v);
        }
        if(it->is_number_integer())return it->template get<int64_t>();
        return std::nullopt;
    };
    auto subtype=str(details,"type"),weight=str(details,"weight_class");
    if(subtype.empty())subtype=type;
    if(!subtype.empty())subtype=WeaponTypeLabel(subtype);
    add(ItemType,weight.empty()?subtype:subtype.empty()?weight:weight+" "+subtype);
    const auto defense=integer(details,"defense"),lo=integer(details,"min_power"),hi=integer(details,"max_power");
    if(defense&&*defense>0)add(ItemCombat,"Defense: "+std::to_string(*defense));
    if(lo&&hi&&*lo>=0&&*hi>=*lo)add(ItemCombat,"Weapon Strength: "+std::to_string(*lo)+" - "+std::to_string(*hi));
    const bool legendaryRelic=type=="Relic"&&rarity=="Legendary";
    if(details.is_object()){
        auto infix=details.find("infix_upgrade");
        if(infix!=details.end()&&infix->is_object()){
            auto attrs=infix->find("attributes");
            if((parts&ItemAttributes)&&attrs!=infix->end()&&attrs->is_array()){
                bool first=true;
                for(const auto& attr:*attrs){
                    const auto name=str(attr,"attribute");const auto mod=integer(attr,"modifier");
                    if(name.empty()||!mod||!*mod)continue;
                    if(first){add(ItemAttributes,"Fixed definition attributes");first=false;}
                    add(ItemAttributes,(*mod>0?"+":"")+std::to_string(*mod)+" "+FriendlyAttribute(name));
                }
            }
            auto buff=infix->find("buff");
            if(!legendaryRelic&&buff!=infix->end())add(ItemEffects,str(*buff,"description"));
        }
        auto bonuses=details.find("bonuses");
        if(!legendaryRelic&&(parts&ItemEffects)&&bonuses!=details.end()&&bonuses->is_array()){
            size_t index=0;
            for(const auto& bonus:*bonuses){
                ++index;if(bonus.is_string()&&!bonus.template get_ref<const std::string&>().empty())
                    add(ItemEffects,"("+std::to_string(index)+"): "+bonus.template get<std::string>());
            }
        }
    }
    if(legendaryRelic)add(ItemDescription,"The API does not expose which relic effect this legendary relic is emulating.");
    else add(ItemDescription,description);
    return out;
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
// Resource labels must be qualified by the actual reported profession.
// Warrior/Druid use the same cost field; never call all costs Revenant energy.
template<class Json> std::vector<std::string> MetadataLines(const Json& extras,const std::vector<std::string>& professions={}){
    std::vector<std::string> out;
    if(!extras.is_object())return out;
    auto number=[&](const char* key,const char* label){
        auto it=extras.find(key);if(it!=extras.end()&&it->is_number_unsigned())out.push_back(std::string(label)+": "+it->dump());
    };
    number("cost",professions.size()==1&&professions.front()=="Revenant"?"Energy cost":"Resource cost (API)");
    number("initiative","Initiative cost");
    for(const char* key:{"weapon_type","attunement","dual_wield"}){
        auto it=extras.find(key);if(it==extras.end()||!it->is_string())continue;
        auto s=it->template get<std::string>();
        if(s.empty()||s=="None")continue;
        out.push_back(std::string(key==std::string("weapon_type")?"Weapon":key==std::string("attunement")?"Attunement":"Off-hand")+": "+s);
    }
    auto categories=extras.find("categories");
    if(categories!=extras.end()&&categories->is_array()){
        std::string line;
        for(const auto& v:*categories)if(v.is_string()&&!v.template get<std::string>().empty()){
            if(line.size()>240)break;
            if(!line.empty())line+=", ";
            line+=v.template get<std::string>();
        }
        if(!line.empty())out.push_back("Categories: "+line);
    }
    auto conditional=extras.find("traited_facts");
    if(conditional!=extras.end()&&conditional->is_array()&&!conditional->empty())
        out.push_back("Additional trait-dependent facts available");
    return out;
}
// Dye remover has its own API value; callers own evidence of channel presence.
inline std::string DyeName(int id,const std::string& known){
    if(id==0)return "Empty";
    if(id==1)return "Dye Remover";
    return known;
}
}
