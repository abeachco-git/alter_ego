#pragma once
#include <string>
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
// Dye remover has its own API value; callers own evidence of channel presence.
inline std::string DyeName(int id,const std::string& known){
    if(id==0)return "Empty";
    if(id==1)return "Dye Remover";
    return known;
}
}
