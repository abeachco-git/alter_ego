#pragma once
#include <initializer_list>
#include <string>
namespace AlterEgo::GroupBrowse {
inline std::string PathKey(std::initializer_list<std::string> parts) {
    std::string key;
    for(const auto& part:parts)key+=std::to_string(part.size())+":"+part;
    return key;
}
template<class Set>
bool Expanded(const Set& overrides,const std::string& key,bool defaultOpen,bool search=false) {
    return search || (defaultOpen != (overrides.count(key)!=0));
}
template<class Set>
bool Toggle(Set& overrides,const std::string& key,bool search=false) {
    if(search)return false;
    if(overrides.erase(key))return true;
    if(overrides.size()>=4096)return false;
    overrides.insert(key);return true;
}
template<class Set>
bool Reveal(Set& overrides,const std::string& key,bool defaultOpen) {
    if(defaultOpen)return overrides.erase(key)!=0;
    if(overrides.count(key)||overrides.size()>=4096)return false;
    overrides.insert(key);return true;
}
}
