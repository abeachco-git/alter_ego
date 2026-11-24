#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <sstream>
#include <vector>
namespace Skinventory::Search {
inline std::string Lower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}
// One adjacent transposition, insertion, deletion or substitution; never
// fuzzy-match short tokens (they otherwise match almost everything).
inline bool NearWord(const std::string& a, const std::string& b) {
    if (a.size() < 5 || b.size() < 4 || a.size() > b.size()+1 || b.size() > a.size()+1) return false;
    if (a.size() == b.size()) {
        size_t i=0; while (i<a.size() && a[i]==b[i]) ++i;
        if (i==a.size()) return true;
        if (a.substr(i+1)==b.substr(i+1)) return true;
        return i+1<a.size() && a[i]==b[i+1] && a[i+1]==b[i] && a.substr(i+2)==b.substr(i+2);
    }
    const auto& x=a.size()<b.size()?a:b;
    const auto& y=a.size()<b.size()?b:a;
    size_t i=0; while(i<x.size() && x[i]==y[i]) ++i;
    return x.substr(i)==y.substr(i+1);
}
inline std::vector<std::string> Words(const std::string& s) {
    std::istringstream in(Lower(s)); std::vector<std::string> out;
    for (std::string token; in>>token;) out.push_back(token);
    return out;
}
inline bool Matches(const std::string& text, const std::vector<std::string>& query) {
    const auto hay=Lower(text);
    for (const auto& q:query) {
        if(hay.find(q)!=std::string::npos) continue;
        bool found=false;
        for(const auto& word:Words(hay)) if(NearWord(q,word)) {found=true;break;}
        if(!found) return false;
    }
    return true;
}
}
namespace Skinventory {
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
}
