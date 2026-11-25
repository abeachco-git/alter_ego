#pragma once
#include <string>
#include <vector>
namespace AlterEgo::CharacterTools {
inline bool Gathering(const std::string& slot) {
    return slot=="Pick"||slot=="Axe"||slot=="Sickle";
}
inline bool Utility(const std::string& slot) {
    return Gathering(slot)||slot=="FishingRod"||slot=="FishingBait"||slot=="FishingLure"||
        slot=="PowerCore"||slot=="SensoryArray"||slot=="ServiceChip";
}
// Character-wide rows from the supplemental endpoint take priority. Legacy
// snapshots can contain template-scoped rows; only use the actual active tab.
template<class Item>
inline const Item* Find(const std::vector<Item>& items,const std::string& slot,int activeTab) {
    if(!Utility(slot))return nullptr;
    const Item* active=nullptr;
    for(const auto& item:items) {
        if(item.slot!=slot||!item.id)continue;
        if(item.tab==0)return &item;
        if(item.tab==activeTab&&!active)active=&item;
    }
    return active;
}
}
