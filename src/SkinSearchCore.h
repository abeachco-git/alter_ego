#pragma once
#include "SearchCore.h"
#include <optional>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
namespace Skinventory {
// Normalize category identities for the UI only. Preserve raw API metadata on disk.
inline std::string SkinCategorySubtype(const std::string& type, const std::string& subtype) {
    if (type != "Weapon") return subtype;
    if (subtype == "ShortBow" || subtype == "Shortbow" || subtype == "Short Bow") return "Short Bow";
    if (subtype == "Speargun" || subtype == "Harpoon Gun") return "Harpoon Gun";
    if (subtype == "LongBow" || subtype == "Longbow" || subtype == "Long Bow") return "Longbow";
    if (subtype == "Harpoon" || subtype == "Spear") return "Spear";
    return subtype;
}
}
namespace Skinventory { namespace Search = AlterEgo::Search; }
namespace Skinventory {
using CommerceCandidates=std::unordered_map<uint32_t,std::vector<uint32_t>>;
// Price choices describe the complete candidate set, not just the winner.
inline std::unordered_set<uint32_t> ChangedCommerceCandidates(const CommerceCandidates& before,const CommerceCandidates& after) {
    std::unordered_set<uint32_t> changed;
    for(const auto& [skin,ids]:before) {
        auto it=after.find(skin);
        if(it==after.end()){if(!ids.empty())changed.insert(skin);}
        else if(!std::is_permutation(ids.begin(),ids.end(),it->second.begin(),it->second.end()))changed.insert(skin);
    }
    for(const auto& [skin,ids]:after)if(!ids.empty()&&!before.count(skin))changed.insert(skin);
    return changed;
}
struct PriceInfo {
    uint32_t item_id=0;
    int buy_price=0, sell_price=0, buy_quantity=0, sell_quantity=0;
    bool tradeable=false;
    int64_t fetched_at_unix=0;
};
// Positive, available sell listings always win, regardless of rarity.
// No-listing candidates are retained only for informational buy orders.
inline bool BetterPrice(const PriceInfo& candidate, const PriceInfo& current) {
    if(!candidate.tradeable) return false;
    if(!current.tradeable) return true;
    const bool a=candidate.sell_price>0 && candidate.sell_quantity>0;
    const bool b=current.sell_price>0 && current.sell_quantity>0;
    if(a!=b) return a;
    if(a && candidate.sell_price!=current.sell_price) return candidate.sell_price<current.sell_price;
    if(!a && candidate.buy_price!=current.buy_price) return candidate.buy_price>current.buy_price;
    return candidate.item_id<current.item_id;
}
// All candidates must have an observation before a cheapest choice is
// verified. Its freshness is the oldest candidate, not the winning price.
inline std::optional<PriceInfo> SelectSkinPrice(const std::vector<uint32_t>& candidates,
        const std::unordered_map<uint32_t,PriceInfo>& prices,std::optional<PriceInfo> fallback={}){
    if(candidates.empty())return PriceInfo{};
    PriceInfo best;int64_t oldest=0;
    for(auto id:candidates){
        auto it=prices.find(id);if(it==prices.end())return fallback;
        const auto& p=it->second;
        if(p.fetched_at_unix<=0)return fallback;
        if(!oldest||p.fetched_at_unix<oldest)oldest=p.fetched_at_unix;
        if(BetterPrice(p,best))best=p;
    }
    best.fetched_at_unix=oldest;return best;
}
}
