#pragma once
// Pure provider-neutral guide patch. Missing values are never deletion signals.
// Source HTML adapters currently supply positive values; explicit empty values
// are reserved for positively identified removals, not zero/default templates.
#include "SnowCrowsImport.h"
#include "InfusionTotalsCore.h"
#include "GearEditCore.h"
#include "SkillEditCore.h"
#include <optional>
#include <set>
namespace AlterEgo::SourceRefresh {
template<class T> using Field=std::optional<T>;
struct GearPatch {
    Field<uint32_t> item,stat,rune,sigil,sigil2;
    Field<std::string> statName,type;
};
struct SpecPatch { Field<uint32_t> id; std::array<Field<int>,3> traits; };
struct Patch {
    std::string profession,sourceUrl,warning;
    std::array<SpecPatch,3> specs;
    bool completeSpecs=false;
    std::array<Field<uint32_t>,5> skills;
    std::map<std::string,GearPatch> gear;
    std::set<std::string> removedSlots;
    Field<uint32_t> rune,relic;
    Field<std::vector<SnowCrows::InfusionTotal>> infusions;
};
inline bool Armor(const std::string& s){
    return s=="Helm"||s=="Shoulders"||s=="Coat"||s=="Gloves"||s=="Leggings"||s=="Boots";
}
inline bool LandWeapon(const std::string& s){return s=="WeaponA1"||s=="WeaponA2"||s=="WeaponB1"||s=="WeaponB2";}
inline bool LandGear(const std::string& s){
    return Armor(s)||LandWeapon(s)||s=="Backpack"||s=="Accessory1"||s=="Accessory2"||s=="Amulet"||s=="Ring1"||s=="Ring2";
}
inline std::string WeaponKey(std::string s){
    if(s=="Longbow")return "LongBow";
    if(s=="Shortbow")return "ShortBow";
    return s;
}
// A partial specialization line has no reliable positional identity unless
// all three lines are present or the existing specialization ID matches.
template<class Tier> Patch FromPage(const SnowCrows::Page& page,Tier tier,bool initial=false){
    Patch p;p.profession=page.profession;p.sourceUrl=page.sourceUrl;p.warning=page.importWarning;
    std::set<uint32_t> ids;
    p.completeSpecs=std::all_of(page.specs.begin(),page.specs.end(),[&](const auto& s){return s.id&&ids.insert(s.id).second;});
    for(size_t i=0;i<3;++i){
        const auto& s=page.specs[i];if(!s.id)continue;
        p.specs[i].id=s.id;
        for(size_t j=0;j<s.traits.size();++j){
            const auto id=s.traits[j];if(!id||id>INT32_MAX)continue;
            int t=tier(s.id,id);
            // A complete list is published in tier order. A partial list is not.
            if(t==-1&&(s.traits.size()==3||initial))t=static_cast<int>(j);
            if(t>=0&&t<3&&!p.specs[i].traits[t])p.specs[i].traits[t]=static_cast<int>(id);
            else p.warning+=" A trait tier was unresolved and retained locally.";
        }
    }
    // The parser preserves positions; unknown/zero fields do not clear a slot.
    for(size_t i=0;i<5;++i)if(page.skills[i])p.skills[i]=page.skills[i];
    for(const auto& [slot,g]:page.gear){
        if(!LandGear(slot))continue;
        if(LandWeapon(slot)&&!g.weaponType.empty()&&!EquipmentChoice::WeaponEnvironmentAllowed(slot,g.weaponType=="Harpoon Gun"?"Speargun":g.weaponType)){
            p.warning+=" An unsupported aquatic weapon in a terrestrial guide was ignored.";continue;
        }
        auto& dest=p.gear[slot];
        if(g.itemId)dest.item=g.itemId;
        if(g.statId)dest.stat=g.statId;
        if(!g.statName.empty())dest.statName=g.statName;
        if(!g.weaponType.empty())dest.type=WeaponKey(g.weaponType);
        if(Armor(slot)&&!g.upgrades.empty())dest.rune=g.upgrades[0];
        if(LandWeapon(slot)&&!g.upgrades.empty())dest.sigil=g.upgrades[0];
        if(LandWeapon(slot)&&g.upgrades.size()>1)dest.sigil2=g.upgrades[1];
    }
    if(page.uniformRunePublished&&page.runeId)p.rune=page.runeId;
    if(page.relicId)p.relic=page.relicId;
    if(!page.infusions.empty())p.infusions=page.infusions;
    return p;
}
template<class Build> bool HasLocalInfusions(const Build& b){
    return b.infusion_summary_present||std::any_of(b.gear.begin(),b.gear.end(),[](const auto& e){
        return InfusionSlotEligible(e.first)&&HasSlotInfusionData(e.second);
    });
}
template<class Build> void AdoptInfusions(Build& b,const std::vector<SnowCrows::InfusionTotal>& v){
    ForgetSlotInfusions(b);ForgetAggregateInfusions(b);
    for(const auto& i:v)if(i.itemId&&i.count>0)b.infusion_totals.push_back({i.itemId,i.count,""});
}
enum class ItemCompatibility { Compatible, Incompatible, Unknown };
struct UnknownCompatibility {
    template<class Slot> ItemCompatibility operator()(const Slot&,const GearPatch&)const{return ItemCompatibility::Unknown;}
};
struct Report { std::vector<std::string> notes; bool offerInfusions=false; };
template<class Build,class TwoHanded,class Compatibility=UnknownCompatibility> bool Apply(const Build& before,const Patch& p,Build& result,Report& report,std::string& error,TwoHanded twoHanded,Compatibility compatibility={},bool initial=false){
    report={};error.clear();
    if(before.profession!=p.profession||before.source_url.empty()||before.source_url!=p.sourceUrl){
        error="The guide no longer matches the Loadout profession or source URL.";return false;
    }
    Build b=before;
    b.import_warning=p.warning;
    if(!p.warning.empty())report.notes.push_back(p.warning);
    // Reuse a matching old specialization's traits even when its position
    // changes. An incompatible specialization cannot retain old traits.
    for(size_t i=0;i<3;++i){
        const auto& spec=p.specs[i];if(!spec.id)continue;
        size_t target=i;
        if(!p.completeSpecs&&!initial){
            target=3;for(size_t j=0;j<3;++j)if(before.specializations[j].spec_id==*spec.id)target=j;
            if(target==3){report.notes.push_back("Partial specialization layout: unmatched line retained for manual review.");continue;}
        }
        auto line=before.specializations[target];
        if(line.spec_id!=*spec.id){
            line={};line.spec_id=*spec.id;
            for(const auto& old:before.specializations)if(old.spec_id==*spec.id){line=old;break;}
        }
        for(size_t t=0;t<3;++t)if(spec.traits[t])line.traits[t]=*spec.traits[t];
        b.specializations[target]=line;
    }
    auto& bar=b.terrestrial_skills;
    if(p.skills[0])bar.heal=*p.skills[0];
    if(p.skills[4])bar.elite=*p.skills[4];
    std::array<uint32_t,3> utilities{};
    for(size_t i=0;i<3;++i)utilities[i]=p.skills[i+1].value_or(bar.utilities[i]);
    std::set<uint32_t> unique;bool ambiguous=false;
    for(auto id:utilities)if(id&&!unique.insert(id).second)ambiguous=true;
    if(ambiguous)report.notes.push_back("Utility changes conflict with a retained slot; utility bar left unchanged for review.");
    else std::copy(utilities.begin(),utilities.end(),bar.utilities);
    const bool runePatch=p.rune||std::any_of(p.gear.begin(),p.gear.end(),[](const auto& e){return e.second.rune.has_value();});
    if(runePatch&&(b.rune_id||!b.rune_name.empty())){
        // The legacy global rune is a terrestrial fallback. Keep every
        // recorded per-slot override, and do not materialize an aquatic rune
        // the guide never supplied.
        for(auto& e:b.gear)if(Armor(e.first)&&!e.second.rune_id&&e.second.rune.empty()){
            e.second.rune_id=b.rune_id;e.second.rune=b.rune_name;
        }
        b.rune_id=0;b.rune_name.clear();
    }
    auto upgrade=[](uint32_t& id,std::string& name,const Field<uint32_t>& v){
        if(v&&(id!=*v||!*v)){id=*v;name.clear();}
    };
    for(const auto& [slot,g]:p.gear){
        if(!LandGear(slot))continue;
        if(slot=="WeaponA2"||slot=="WeaponB2"){
            const std::string main=slot=="WeaponA2"?"WeaponA1":"WeaponB1";
            const auto supplied=p.gear.find(main);
            const auto local=b.gear.find(main);
            if((supplied==p.gear.end()||!supplied->second.type)&&local!=b.gear.end()&&twoHanded(local->second.weapon_type)){
                report.notes.push_back(slot+": omitted main-hand type leaves local two-handed weapon intact; offhand proposal skipped.");
                continue;
            }
        }
        auto& s=b.gear[slot];s.slot=slot;
        const bool changedType=g.type&&!s.weapon_type.empty()&&WeaponKey(s.weapon_type)!=*g.type;
        const bool statSame=g.statName&&!s.stat_name.empty()&&NamedStats::SameCanonicalStat(*g.statName,s.stat_name);
        const bool changedStat=(g.stat&&s.stat_id&&*g.stat!=s.stat_id&&!statSame)||
            (g.statName&&!s.stat_name.empty()&&!statSame&&NamedStats::NormalizeKey(*g.statName)!=NamedStats::NormalizeKey(s.stat_name));
        if(g.item&&!*g.item)s.item_id=0; // positively identified removal
        else if(changedType){s.item_id=g.item.value_or(0);}
        else if(changedStat&&s.item_id){
            const auto match=compatibility(s,g);
            if(match==ItemCompatibility::Incompatible){
                s.item_id=g.item.value_or(0);
                report.notes.push_back(slot+": incompatible fixed-stat item replaced or cleared.");
            }else if(match==ItemCompatibility::Unknown){
                report.notes.push_back(slot+": exact item retained; compatibility with new stats is unverified.");
            }
        }else if(g.item&&!s.item_id)s.item_id=*g.item;
        // Guide item IDs are examples, not an instruction to replace a local
        // legendary or other exact item with the same recommended stats/type.
        if(g.type)s.weapon_type=*g.type;
        if(g.stat){if(s.stat_id!=*g.stat&&!g.statName)s.stat_name.clear();s.stat_id=*g.stat;}
        else if(changedStat)s.stat_id=0;
        if(g.statName)s.stat_name=*g.statName;
        upgrade(s.rune_id,s.rune,g.rune);upgrade(s.sigil_id,s.sigil,g.sigil);upgrade(s.sigil2_id,s.sigil2,g.sigil2);
        if(g.type&&LandWeapon(slot)&&!s.weapon_type.empty()&&!twoHanded(s.weapon_type)){
            if(s.sigil2_id||!s.sigil2.empty()||GearEdit::InfusionPositionCount(s)>1)
                report.notes.push_back(slot+": second sigil/infusion incompatible with one-handed weapon; removed in draft.");
            s.sigil2_id=0;s.sigil2.clear();
            if(s.infusion_ids.size()>1)s.infusion_ids.resize(1);
            if(s.infusion_attributes.size()>1)s.infusion_attributes.resize(1);
        }
    }
    if(p.rune)for(auto& e:b.gear)if(Armor(e.first))upgrade(e.second.rune_id,e.second.rune,p.rune);
    if(p.relic)upgrade(b.relic_id,b.relic_name,p.relic);
    for(const auto& slot:p.removedSlots)if(LandGear(slot)&&b.gear.erase(slot))report.notes.push_back(slot+": explicitly removed.");
    for(const char* main:{"WeaponA1","WeaponB1"}){
        const auto supplied=p.gear.find(main);
        if(supplied==p.gear.end()||!supplied->second.type||!twoHanded(*supplied->second.type))continue;
        const std::string off=std::string(main)=="WeaponA1"?"WeaponA2":"WeaponB2";
        if(p.gear.count(off)&&!p.removedSlots.count(off)){
            error="Guide contains conflicting two-handed and offhand equipment.";return false;
        }
        if(b.gear.erase(off))report.notes.push_back(off+": removed because the guide specifies a two-handed main weapon.");
    }
    if(p.infusions){
        if(HasLocalInfusions(before)){
            report.offerInfusions=true;
            report.notes.push_back("Local infusion configuration retained. Guide totals can be adopted explicitly.");
        }else AdoptInfusions(b,*p.infusions);
    }
    if(!initial)report.notes.push_back("Omitted fields, aquatic skills and companion choices are retained. Local aquatic equipment is retained unless you explicitly adopt guide-wide infusion totals.");
    result=std::move(b);return true;
}
}
