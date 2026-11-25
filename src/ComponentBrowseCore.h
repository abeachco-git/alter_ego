#pragma once
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <tuple>

namespace AlterEgo::ComponentBrowse {
inline bool MatchesKind(const std::set<std::string>& kinds,const std::string& kind){
    return kinds.empty()||kinds.count(kind)!=0;
}
struct SortKey {
    std::string name,id;
    int references=0;
};
inline SortKey Key(std::string name,std::string id,int references){
    for(char& c:name)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return {std::move(name),std::move(id),references};
}
// Usage ties keep alphabetical order in either direction. IDs make
// identical display names deterministic without altering saved identities.
inline bool Less(const SortKey& a,const SortKey& b,bool usage,bool ascending){
    if(usage&&a.references!=b.references)return ascending?a.references<b.references:a.references>b.references;
    if(a.name!=b.name)return !usage&&!ascending?a.name>b.name:a.name<b.name;
    return a.id<b.id;
}
}
