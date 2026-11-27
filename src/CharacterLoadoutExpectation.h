#pragma once
// Character intent is independent of observed build/equipment tab numbers.
// Preferences reference stable Library IDs; never mutate Library contents.
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <utility>

namespace AlterEgo::CharacterLoadout {
using Key=std::pair<std::string,std::string>;
enum class Mode { Automatic, None, Explicit };
struct Choice {
    Mode mode=Mode::Automatic;
    std::string id;
    bool operator==(const Choice& o)const{return mode==o.mode&&id==o.id;}
};
inline bool ValidKey(const Key& k){
    return !k.first.empty()&&!k.second.empty()&&k.first.size()<=256&&k.second.size()<=256;
}
inline bool ValidChoice(const Choice& c){
    return c.mode==Mode::Explicit?!c.id.empty()&&c.id.size()<=1024:c.id.empty();
}
class Preferences {
    std::map<Key,Choice> rows;
public:
    Choice Get(const Key& k)const{auto it=rows.find(k);return it==rows.end()?Choice{}:it->second;}
    bool Set(const Key& k,const Choice& c){
        if(!ValidKey(k)||!ValidChoice(c))return false;
        if(c.mode==Mode::Automatic){rows.erase(k);return true;}
        if(rows.size()>=4096&&!rows.count(k))return false;
        rows[k]=c;return true;
    }
    nlohmann::json Encode()const{
        auto j=nlohmann::json::array();
        for(const auto& [key,c]:rows)j.push_back({{"account",key.first},{"character",key.second},{"mode",c.mode==Mode::None?"none":"explicit"},{"id",c.id}});
        return j;
    }
    bool Decode(const nlohmann::json& j){
        if(!j.is_array()||j.size()>4096)return false;
        Preferences next;
        for(const auto& row:j){
            if(!row.is_object())return false;
            for(const char* field:{"account","character","mode","id"})
                if(!row.contains(field)||!row[field].is_string())return false;
            Key k{row["account"].get<std::string>(),row["character"].get<std::string>()};
            const auto mode=row["mode"].get<std::string>();
            if(mode!="none"&&mode!="explicit")return false;
            Choice c{mode=="none"?Mode::None:Mode::Explicit,row["id"].get<std::string>()};
            if(next.rows.count(k)||!next.Set(k,c))return false;
        }
        rows=std::move(next.rows);return true;
    }
};
template<class Compatible> Choice Resolve(Choice choice,Compatible compatible) {
    if(choice.mode==Mode::Explicit&&!compatible(choice.id))return {};
    return choice;
}
}
