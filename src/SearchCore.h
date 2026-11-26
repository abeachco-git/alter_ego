#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <sstream>
#include <vector>
namespace AlterEgo::Search {
inline std::string Lower(std::string s) {
    for (auto& ch : s) if(ch>='A'&&ch<='Z')ch=static_cast<char>(ch-'A'+'a');
    return s;
}
// One adjacent transposition, insertion, deletion or substitution; never
// fuzzy-match short tokens (they otherwise match almost everything).
inline bool NearWord(const std::string& a, const std::string& b) {
    if (std::any_of(a.begin(),a.end(),[](unsigned char c){return c>=128;}) ||
        std::any_of(b.begin(),b.end(),[](unsigned char c){return c>=128;})) return false;
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
// Opt-in minimum for search-first views; count non-whitespace bytes across
// tokens so a short second word ("valk kr") remains valid.
inline bool HasMinimumQuery(const std::vector<std::string>& query,size_t minimum=3) {
    size_t count=0;
    for(const auto& token:query){count+=token.size();if(count>=minimum)return true;}
    return minimum==0;
}
inline bool Matches(const std::string& text, const std::vector<std::string>& query) {
    const auto hay=Lower(text);
    for (const auto& q:query) {
        if(hay.find(q)!=std::string::npos) continue;
        // Numeric IDs are literal; do not turn a mistyped ID into a different item.
        if(std::any_of(q.begin(),q.end(),[](unsigned char c){return std::isdigit(c)!=0;}))return false;
        bool found=false;
        for(const auto& word:Words(hay)) if(NearWord(q,word)) {found=true;break;}
        if(!found) return false;
    }
    return true;
}

inline bool Numeric(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(),s.end(),[](unsigned char c){return c>='0'&&c<='9';});
}
// IDs are distinct from labels: never match a partial ID.
inline bool MatchesId(const std::string& name,uint32_t id,const std::vector<std::string>& query) {
    const auto exact=std::to_string(id);
    for(const auto& q:query) {
        if(Numeric(q)&&q==exact)continue;
        if(!Matches(name,{q}))return false;
    }
    return true;
}
inline bool MatchesFields(const std::vector<std::string>& query,const std::vector<std::string>& fields) {
    for(const auto& q:query) {
        bool found=false;
        for(const auto& field:fields) if(Matches(field,{q})){found=true;break;}
        if(!found)return false;
    }
    return true;
}
}
