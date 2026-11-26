#pragma once
// Shared, UI-independent infusion totals and minimum-only comparisons.
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace AlterEgo {
struct InfusionSummary {
    bool present = false;
    bool complete = true; // False if public metadata is unavailable.
    int agonyResistance = 0;
    std::map<std::string, int> stats;
};
// The existing typed fields encode the two modes without a schema rewrite.
 // A mixed legacy projection must be resolved explicitly, never silently
 // prefer one side or compare both. Empty recorded slots are meaningful.
enum class InfusionMode { Slots, Totals, Conflict };
enum class InfusionConfiguration { LandA, LandB, WaterA, WaterB };
inline bool InfusionSlotEligible(const std::string& slot) {
    return slot!="Amulet"&&slot!="Relic";
}
inline const char* InfusionConfigurationLabel(InfusionConfiguration c) {
    switch(c){
    case InfusionConfiguration::LandA:return "Terrestrial Set I";
    case InfusionConfiguration::LandB:return "Terrestrial Set II";
    case InfusionConfiguration::WaterA:return "Aquatic Set I";
    case InfusionConfiguration::WaterB:return "Aquatic Set II";
    }
    return "Unknown configuration";
}
inline bool InfusionSlotInConfiguration(const std::string& slot,InfusionConfiguration c) {
    const bool water=c==InfusionConfiguration::WaterA||c==InfusionConfiguration::WaterB;
    if(slot=="Helm")return !water;
    if(slot=="HelmAquatic"||slot=="Breather")return water;
    if(slot=="WeaponA1"||slot=="WeaponA2")return c==InfusionConfiguration::LandA;
    if(slot=="WeaponB1"||slot=="WeaponB2")return c==InfusionConfiguration::LandB;
    if(slot=="WeaponAquaticA"||slot=="AquaticA")return c==InfusionConfiguration::WaterA;
    if(slot=="WeaponAquaticB"||slot=="AquaticB")return c==InfusionConfiguration::WaterB;
    return slot=="Shoulders"||slot=="Coat"||slot=="Gloves"||slot=="Leggings"||slot=="Boots"||
        slot=="Backpack"||slot=="Accessory1"||slot=="Accessory2"||slot=="Ring1"||slot=="Ring2";
}
template<class HasSlot> std::vector<InfusionConfiguration> InfusionConfigurations(HasSlot has) {
    using C=InfusionConfiguration;
    const bool a=has("WeaponA1")||has("WeaponA2"),b=has("WeaponB1")||has("WeaponB2");
    const bool wa=has("WeaponAquaticA")||has("AquaticA"),wb=has("WeaponAquaticB")||has("AquaticB");
    std::vector<C> configs;
    if(a||!b)configs.push_back(C::LandA);
    if(b)configs.push_back(C::LandB);
    if(wa||(!wb&&(has("HelmAquatic")||has("Breather"))))configs.push_back(C::WaterA);
    if(wb)configs.push_back(C::WaterB);
    return configs;
}
template<class Slot> bool HasSlotInfusionData(const Slot& s) {
    return s.infusion_snapshot || !s.infusion.empty() ||
        std::any_of(s.infusion_ids.begin(),s.infusion_ids.end(),[](auto id){return id!=0;}) ||
        std::any_of(s.infusion_attributes.begin(),s.infusion_attributes.end(),[](const auto& a){return !a.empty();});
}
template<class Build> InfusionMode GetInfusionMode(const Build& b) {
    const bool totals=b.infusion_summary_present || !b.infusion_totals.empty();
    if(!totals)return InfusionMode::Slots;
    for(const auto& entry:b.gear)if(InfusionSlotEligible(entry.first)&&HasSlotInfusionData(entry.second))return InfusionMode::Conflict;
    return InfusionMode::Totals;
}
template<class Build> void ForgetAggregateInfusions(Build& b) {
    b.infusion_summary_present=false;b.infusion_ar_total=0;
    b.infusion_stat_totals.clear();b.infusion_totals.clear();
}
template<class Build> void ForgetSlotInfusions(Build& b) {
    for(auto& entry:b.gear){
        if(!InfusionSlotEligible(entry.first))continue;
        auto& s=entry.second;s.infusion_ids.clear();s.infusion_attributes.clear();
        s.infusion_snapshot=false;s.infusion.clear();
    }
}
template<class Build> bool SetNumericInfusionTotals(Build& b,const InfusionSummary& total) {
    if(!total.complete)return false;
    ForgetSlotInfusions(b);ForgetAggregateInfusions(b);
    b.infusion_summary_present=true;b.infusion_ar_total=total.agonyResistance;
    b.infusion_stat_totals=total.stats;return true;
}
// Restore only infusion fields, not gear identities, stats or other edits.
// A removed gear slot is not recreated by toggling an unsaved mode.
template<class Build> void RestoreSlotInfusions(Build& b,const Build& previous) {
    ForgetSlotInfusions(b);ForgetAggregateInfusions(b);
    for(auto& entry:b.gear) {
        if(!InfusionSlotEligible(entry.first))continue;
        const auto old=previous.gear.find(entry.first);
        if(old==previous.gear.end()||old->second.item_id!=entry.second.item_id||
            old->second.weapon_type!=entry.second.weapon_type)continue;
        auto& s=entry.second;const auto& v=old->second;
        s.infusion_ids=v.infusion_ids;s.infusion_attributes=v.infusion_attributes;
        s.infusion_snapshot=v.infusion_snapshot;s.infusion=v.infusion;
    }
}
inline int AgonyRequirement(int savedTotal) {
    return std::min(150, std::max(0, savedTotal));
}
// GW2 item buffs commonly read "+9 Agony Resistance"; fall back to the
// standard "Agony Infusion +9" item name. Never infer a value from an item ID.
inline int ParseAgonyResistance(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t at = lower.find("agony resistance");
    if (at != std::string::npos) {
        size_t end = at;
        while (end > 0 && std::isspace(static_cast<unsigned char>(lower[end - 1]))) --end;
        size_t begin = end;
        while (begin > 0 && std::isdigit(static_cast<unsigned char>(lower[begin - 1]))) --begin;
        if (begin > 0 && lower[begin - 1] == '+' && begin < end)
            return std::min(1000, std::stoi(lower.substr(begin, end - begin)));
    }
    at = lower.find("agony infusion +");
    if (at != std::string::npos) {
        at += std::string("agony infusion +").size();
        size_t end = at;
        while (end < lower.size() && std::isdigit(static_cast<unsigned char>(lower[end]))) ++end;
        if (end > at) return std::min(1000, std::stoi(lower.substr(at, end - at)));
    }
    return 0;
}
struct InfusionShortfall {
    std::string attribute; // Agony Resistance or canonical attribute name.
    int required = 0;
    int current = 0;
};
inline std::vector<InfusionShortfall> CompareInfusionTotals(
    const InfusionSummary& saved, const InfusionSummary& live) {
    std::vector<InfusionShortfall> issues;
    if (!saved.present || !saved.complete || !live.complete)
        return issues; // Unknown is not a confirmed shortage.
    const int ar = AgonyRequirement(saved.agonyResistance);
    if (ar > live.agonyResistance)
        issues.push_back({"Agony Resistance", ar, live.agonyResistance});
    for (const auto& [attr, total] : saved.stats) {
        if (total <= 0) continue;
        auto it = live.stats.find(attr);
        int actual = it == live.stats.end() ? 0 : it->second;
        if (actual < total) issues.push_back({attr, total, actual});
    }
    return issues;
}
} // namespace AlterEgo
