#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <utility>

namespace AlterEgo::EntityMenu {
// Name and native link are independent evidence. The role is presentation,
// not a guessed name or an invented link.
struct Entity {
    std::string role,name,link;
    bool Visible() const { return !name.empty()||!link.empty(); }
    std::string Label() const { return name.empty()?role:name; }
    std::string Key() const { return link.empty()?"name:"+role+":"+name:"link:"+link; }
};
inline void AppendUnique(std::vector<Entity>& rows,Entity entity){
    if(!entity.Visible())return;
    const auto same=[&](const Entity& row){
        return !entity.link.empty()?row.link==entity.link:
            row.link.empty()&&row.role==entity.role&&row.name==entity.name;
    };
    auto found=std::find_if(rows.begin(),rows.end(),same);
    if(found==rows.end())rows.push_back(std::move(entity));
    else if(found->name.empty()&&!entity.name.empty())found->name=std::move(entity.name);
}
}
