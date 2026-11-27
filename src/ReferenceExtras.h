#pragma once
#include <cstdint>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
namespace AlterEgo::ReferenceExtras {
using Json=nlohmann::json;
// A bounded, typed allowlist of optional public fields. Absence stays absent;
// contextual facts are retained separately from unconditional facts.
inline uint32_t Unsigned(const Json& value){
    if(!value.is_number_integer())throw std::runtime_error("Invalid reference number");
    if(value.is_number_unsigned()){auto n=value.get<uint64_t>();if(n>UINT32_MAX)throw std::runtime_error("Reference number overflow");return static_cast<uint32_t>(n);}
    const auto n=value.get<int64_t>();if(n<0||n>UINT32_MAX)throw std::runtime_error("Reference number overflow");return static_cast<uint32_t>(n);
}
inline std::string Text(const Json& value){
    if(!value.is_string()||value.get_ref<const std::string&>().size()>512)throw std::runtime_error("Invalid reference text");
    return value.get<std::string>();
}
inline Json Project(const Json& row,bool skill){
    Json out=Json::object();
    auto copy=[&](const char* key,auto validate){
        auto it=row.find(key);if(it!=row.end()&&!it->is_null())out[key]=validate(*it);
    };
    auto list=[&](const Json& values,auto validate){
        if(!values.is_array()||values.size()>256)throw std::runtime_error("Invalid reference list");
        Json result=Json::array();for(const auto& v:values)result.push_back(validate(v));return result;
    };
    auto facts=[&](const Json& values){return list(values,[](const Json& v){
        if(!v.is_object()||v.dump().size()>16384)throw std::runtime_error("Invalid conditional fact");
        return v;
    });};
    copy("traited_facts",facts);
    if(skill){
        for(const char* k:{"cost","initiative","flip_skill","next_chain","prev_chain","toolbelt_skill"})copy(k,Unsigned);
        for(const char* k:{"slot","weapon_type","attunement","dual_wield"})copy(k,Text);
        for(const char* k:{"categories","flags"})copy(k,[&](const Json& v){return list(v,Text);});
        for(const char* k:{"transform_skills","bundle_skills"})copy(k,[&](const Json& v){return list(v,Unsigned);});
    }else{
        copy("specialization",Unsigned);
        // Embedded trait-triggered records are contextual; never adopt them as
        // authoritative /v2/skills definitions or invent missing exact IDs.
        copy("skills",[&](const Json& v){return list(v,[&](const Json& s){
            if(!s.is_object())throw std::runtime_error("Invalid triggered skill");
            Json x=Json::object();
            for(const char* k:{"id"})if(s.contains(k)&&!s.at(k).is_null())x[k]=Unsigned(s.at(k));
            if(s.contains("name")&&!s.at("name").is_null())x["name"]=Text(s.at("name"));
            for(const char* k:{"description","icon"})if(s.contains(k)&&!s.at(k).is_null()){
                const auto& value=s.at(k);if(!value.is_string()||value.get_ref<const std::string&>().size()>8192)throw std::runtime_error("Invalid triggered skill text");x[k]=value;
            }
            for(const char* k:{"facts","traited_facts"})if(s.contains(k)&&!s.at(k).is_null())x[k]=facts(s.at(k));
            return x;
        });});
    }
    if(out.dump().size()>262144)throw std::runtime_error("Reference extras too large");
    return out;
}
// Item acquisition fields belong to the canonical item record, not Commerce
// membership, account binding or a derived tooltip. Explicit zero and an empty
// restriction array remain distinct from missing/legacy fields.
inline Json ProjectItem(const Json& row){
    Json out=Json::object();
    for(const char* key:{"level","vendor_value"}){
        auto it=row.find(key);if(it!=row.end()&&!it->is_null())out[key]=Unsigned(*it);
    }
    auto restrictions=row.find("restrictions");
    if(restrictions!=row.end()&&!restrictions->is_null()){
        if(!restrictions->is_array()||restrictions->size()>256)throw std::runtime_error("Invalid item restrictions");
        Json list=Json::array();
        for(const auto& value:*restrictions){auto name=Text(value);if(name.empty())throw std::runtime_error("Empty item restriction");list.push_back(std::move(name));}
        out["restrictions"]=std::move(list);
    }
    return out;
}
// The /v2/itemstats response carries names, multipliers and offsets.
// Legacy name-only rows intentionally have no formula projection.
inline double StatNumber(const Json& v){
    if(!v.is_number())throw std::runtime_error("Invalid stat coefficient");
    const double n=v.get<double>();
    if(!std::isfinite(n)||std::abs(n)>1e9)throw std::runtime_error("Stat coefficient out of range");
    return n;
}
inline Json ProjectStatFormulas(const Json& row){
    const auto it=row.find("formulas");
    if(it==row.end()||it->is_null())return nullptr;
    const auto& names=row.at("attributes");
    if(!it->is_array()||!names.is_array()||it->size()!=names.size()||it->size()>128)
        throw std::runtime_error("Invalid stat formula list");
    Json formulas=Json::array();std::set<std::string> seen;
    for(size_t i=0;i<it->size();++i){
        const auto& f=it->at(i);const auto name=Text(f.at("attribute"));
        if(name.empty()||name!=Text(names.at(i))||!seen.insert(name).second)
            throw std::runtime_error("Mismatched stat formula");
        formulas.push_back({{"attribute",name},{"multiplier",StatNumber(f.at("multiplier"))},{"value",StatNumber(f.at("value"))}});
    }
    return formulas;
}
inline Json StatObservation(const Json& raw){
    Json row={{"id",Unsigned(raw.at("id"))},{"name",raw.at("name").get<std::string>()},{"attributes",Json::array()}};
    const auto& attrs=raw.at("attributes");
    if(!attrs.is_array()||attrs.size()>128)throw std::runtime_error("Invalid stat attributes");
    Json formulas=Json::array();bool complete=true;std::set<std::string> seen;
    for(const auto& attr:attrs){
        const auto name=Text(attr.at("attribute"));
        if(name.empty()||!seen.insert(name).second)throw std::runtime_error("Invalid stat attribute");
        row["attributes"].push_back(name);
        const bool multiplier=attr.contains("multiplier")&&!attr.at("multiplier").is_null();
        const bool value=attr.contains("value")&&!attr.at("value").is_null();
        const double m=multiplier?StatNumber(attr.at("multiplier")):0;
        const double v=value?StatNumber(attr.at("value")):0;
        if(multiplier&&value)formulas.push_back({{"attribute",name},{"multiplier",m},{"value",v}});
        else complete=false;
    }
    if(complete){row["formulas"]=std::move(formulas);row["formulas"]=ProjectStatFormulas(row);}
    return row;
}
inline void Merge(Json& row,const Json& extras){for(auto it=extras.begin();it!=extras.end();++it)row[it.key()]=it.value();}
}
