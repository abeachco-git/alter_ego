#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include "CharacterActivity.h"
namespace AlterEgo::CharacterScopes {
enum Scope : uint32_t { Core=1, Builds=2, Tabs=4, Physical=8, Crafting=16 };
using Mask=uint32_t;
inline constexpr Mask All=Core|Builds|Tabs|Physical|Crafting;
inline constexpr Mask Compare=Core|Builds|Tabs|Physical;
inline Mask Permitted(bool characters,bool builds,bool inventories){
    return characters?(Core|Crafting|(builds?Builds|Tabs:0u)|(inventories?Physical:0u)):0u;
}
inline constexpr std::array<Scope,5> Ordered{{Core,Builds,Tabs,Physical,Crafting}};
inline constexpr size_t Index(Scope s){return s==Core?0:s==Builds?1:s==Tabs?2:s==Physical?3:4;}
inline const char* Name(Scope s){namespace N=RefreshEngine::ScopeName;return s==Core?N::Core:s==Builds?N::Builds:s==Tabs?N::Tabs:s==Physical?N::Physical:N::Crafting;}
inline bool SpecSensitive(Scope s){return s==Builds||s==Tabs||s==Physical;}
using Stamps=std::array<int64_t,5>;
inline constexpr Mask Visible(int tab){
    return Core|Crafting|((tab==1||tab==3)?Builds:tab==2?Physical:Tabs|Physical);
}
inline int64_t Oldest(const Stamps& stamps,Mask demand){
    int64_t result=0;bool first=true;
    for(auto s:Ordered)if(demand&s){auto stamp=stamps[Index(s)];if(first||stamp<result)result=stamp;first=false;}
    return result;
}
inline Mask Due(const CharacterActivity::Tracker& tracker,const CharacterActivity::Key& key,
    const Stamps& stamps,Mask demand,int64_t now,bool specSignal){
    Mask result=0;
    for(auto s:Ordered)if((demand&s)&&tracker.Due(key,Name(s),stamps[Index(s)],0,now,specSignal&&SpecSensitive(s)))result|=s;
    return result;
}
inline bool Fresh(const CharacterActivity::Tracker& tracker,const CharacterActivity::Key& key,
    const Stamps& stamps,Mask demand,int64_t now,bool specSignal){
    for(auto s:Ordered)if((demand&s)&&!tracker.Fresh(key,Name(s),stamps[Index(s)],now,specSignal&&SpecSensitive(s)))return false;
    return true;
}
}
