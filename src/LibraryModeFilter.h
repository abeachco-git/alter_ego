#pragma once
#include <set>
#include <string>
#include <vector>
namespace AlterEgo::LibraryFilter {
inline const std::vector<std::string>& Modes() {
    static const std::vector<std::string> modes{"PvE","WvW","PvP","Raid","Fractal","Other"};
    return modes;
}
inline std::set<std::string> Legacy(int value) {
    if(value<1||value>static_cast<int>(Modes().size()))return {};
    return {Modes()[value-1]};
}
inline void Validate(std::set<std::string>& values) {
    for(auto it=values.begin();it!=values.end();) {
        bool known=false;
        for(const auto& mode:Modes())if(mode==*it){known=true;break;}
        if(!known)it=values.erase(it);else ++it;
    }
}
template<class Mode,class Label>
inline bool Matches(const std::set<std::string>& filters,const std::vector<Mode>& modes,Mode legacy,Label label) {
    if(filters.empty())return true;
    if(modes.empty())return filters.count(label(legacy))!=0;
    for(auto mode:modes)if(filters.count(label(mode)))return true;
    return false;
}
}
