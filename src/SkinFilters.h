#pragma once
#include "SkinCache.h"
#include "SkinSearchCore.h"
#include <set>
#include <unordered_set>
#include <map>
#include <tuple>
namespace Skinventory {
using Choices = std::set<std::string>;
inline bool Allows(const Choices& s,const std::string& v) {return s.empty()||s.count(v)!=0;}
struct SkinFilters {
    Choices category,weight,slot,weapon,rarity;
    bool operator==(const SkinFilters& o) const {
        return std::tie(category,weight,slot,weapon,rarity)==std::tie(o.category,o.weight,o.slot,o.weapon,o.rarity);
    }
    bool operator!=(const SkinFilters& o) const {return !(*this==o);}
    bool Matches(const SkinInfo& s) const {
        if (!Allows(category,s.type)||!Allows(rarity,s.rarity)) return false;
        // With no explicit category, subtype filters imply their respective
        // categories. With explicit mixed categories, constrain each branch.
        const bool armor=!weight.empty()||!slot.empty(), weapons=!weapon.empty();
        if(category.empty()&&(armor||weapons)&&
           !((armor&&s.type=="Armor")||(weapons&&s.type=="Weapon"))) return false;
        if(s.type=="Armor") return Allows(weight,s.weight_class)&&Allows(slot,s.subtype);
        if(s.type=="Weapon") return Allows(weapon,SkinCategorySubtype(s.type,s.subtype));
        return true;
    }
    void Prune() {
        if(!Allows(category,"Armor")) {weight.clear();slot.clear();}
        if(!Allows(category,"Weapon")) weapon.clear();
    }
};
inline bool EligibleSkin(const SkinInfo& s) {
    return s.id!=0&&!s.name.empty()&&(s.type=="Armor"||s.type=="Weapon"||s.type=="Back"||s.type=="Gathering");
}
struct SkinCounts {int owned=0,total=0; void Add(bool have){++total;if(have)++owned;}};
struct FilterResult {
    SkinCounts scope;
    int searchMatches=0;
    std::vector<std::pair<uint32_t,bool>> displayed;
};
inline FilterResult FilterSkins(const std::vector<SkinInfo>& rows,const std::unordered_set<uint32_t>& owned,
                         const SkinFilters& filters,const std::vector<std::string>& words,
                         bool hasOwnership,bool showOwned,bool showUnowned) {
    FilterResult out;
    std::unordered_set<uint32_t> seen;
    for(const auto& s:rows) {
        if(!EligibleSkin(s)||!filters.Matches(s)||!seen.insert(s.id).second) continue;
        const bool have=hasOwnership&&owned.count(s.id)!=0;
        out.scope.Add(have);
        if(!Search::Matches(s.name,words))continue;
        ++out.searchMatches;
        if(hasOwnership&&((have&&!showOwned)||(!have&&!showUnowned)))continue;
        out.displayed.emplace_back(s.id,have);
    }
    return out;
}
struct SkinFacets {
    std::map<std::string,SkinCounts> category,weight,slot,weapon,rarity;
};
inline SkinFacets CountFacets(const std::vector<SkinInfo>& rows,const std::unordered_set<uint32_t>& owned,bool hasOwnership) {
    SkinFacets out;std::unordered_set<uint32_t> seen;
    for(const auto& s:rows) {
        if(!EligibleSkin(s)||!seen.insert(s.id).second)continue;
        bool have=hasOwnership&&owned.count(s.id);
        out.category[s.type].Add(have);out.rarity[s.rarity].Add(have);
        if(s.type=="Armor"){out.weight[s.weight_class].Add(have);out.slot[s.subtype].Add(have);}
        if(s.type=="Weapon")out.weapon[SkinCategorySubtype(s.type,s.subtype)].Add(have);
    }
    return out;
}
}
