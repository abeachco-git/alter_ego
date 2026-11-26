#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace AlterEgo::RecordSelection {
template<class Rows,class Key,class KeyOf>
inline int IndexOf(const Rows& rows,const Key& key,KeyOf keyOf){
    for(std::size_t i=0;i<rows.size();++i)if(keyOf(rows[i])==key)return static_cast<int>(i);
    return -1;
}
template<class Rows>
inline int ById(const Rows& rows,const std::string& id){
    if(id.empty())return -1;
    return IndexOf(rows,id,[](const auto& row)->const std::string&{return row.id;});
}
struct RevisionTarget{
    std::string id,name;uint64_t revision=0;
    bool Current(uint64_t current)const{return !id.empty()&&revision==current;}
};
}
