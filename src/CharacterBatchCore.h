#pragma once
#include "CharacterIdentityCore.h"
#include "CharacterScopes.h"
#include <nlohmann/json.hpp>
#include <map>
#include <optional>
#include <vector>

namespace AlterEgo::CharacterBatch {
using Json=nlohmann::json;
// Combined characters are much larger than public item records. Bound each
// request conservatively, independently of the public 200-ID API limit.
inline constexpr size_t Limit=20;
inline constexpr const char* Schema="2024-04-01T00:00:00.000Z";
template<class Encode>
inline std::string Path(const std::vector<std::string>& names,Encode encode){
    if(names.empty()||names.size()>Limit)return {};
    std::set<std::string> unique;
    std::string path="/v2/characters?ids=";
    for(const auto& name:names){
        if(!CharacterIdentity::Text(name)||!unique.insert(name).second)return {};
        if(unique.size()>1)path+=",";
        path+=encode(name);
    }
    return path+"&v="+Schema;
}
inline std::optional<std::map<std::string,Json>> Index(const std::vector<std::string>& names,Json rows){
    if(!rows.is_array()||names.empty()||names.size()>Limit)return std::nullopt;
    const std::set<std::string> expected(names.begin(),names.end());
    if(expected.size()!=names.size()||rows.size()>expected.size())return std::nullopt;
    std::map<std::string,Json> out;
    try{
        for(auto& row:rows){
            const auto name=row.at("name").get<std::string>();
            if(!expected.count(name)||out.count(name))return std::nullopt;
            out.emplace(name,std::move(row));
        }
    }catch(...){return std::nullopt;}
    return out; // absent names require independent targeted retrieval
}
inline std::optional<CharacterIdentity::Evidence> Evidence(const Json& row){
    try{return CharacterIdentity::Normalize({row.at("created").get<std::string>(),
        row.at("profession").get<std::string>(),row.at("race").get<std::string>()});}
    catch(...){return std::nullopt;}
}
inline bool SplitRequired(int status){return status==413||status==414;}
inline bool NarrowFallback(int status){return status==403||status==404||status==501;}
inline bool AcceptStatus(int status,bool bulk){return status==200||(bulk&&status==206);}
inline const Json* Field(const Json& row,CharacterScopes::Scope scope){
    using namespace CharacterScopes;
    if(scope==Core)return &row;
    if(scope==Physical)return nullptr; // shared storage broker validates physical rows
    try{
        if(scope==Crafting){const auto& a=row.at("crafting");return a.is_array()?&a:nullptr;}
        if(scope!=Builds&&scope!=Tabs)return nullptr;
        const auto& a=row.at(scope==Builds?"build_tabs":"equipment_tabs");
        const auto& count=row.at(scope==Builds?"build_tabs_unlocked":"equipment_tabs_unlocked");
        const auto& active=row.at(scope==Builds?"active_build_tab":"active_equipment_tab");
        if(!a.is_array()||a.empty()||!count.is_number_integer()||!active.is_number_integer()||
           count.get<int64_t>()!=static_cast<int64_t>(a.size())||active.get<int64_t>()<=0)return nullptr;
        std::set<int64_t> seen;unsigned activeCount=0;
        for(const auto& tab:a){
            const auto& id=tab.at("tab");const auto& selected=tab.at("is_active");
            if(!id.is_number_integer()||id.get<int64_t>()<=0||!seen.insert(id.get<int64_t>()).second||
               !selected.is_boolean())return nullptr;
            if(selected.get<bool>()){
                if(id.get<int64_t>()!=active.get<int64_t>())return nullptr;
                ++activeCount;
            }
        }
        return activeCount==1?&a:nullptr;
    }catch(...){return nullptr;}
}
} // namespace AlterEgo::CharacterBatch
