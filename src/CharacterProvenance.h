#pragma once
#include "CharacterIdentityRuntime.h"
#include <nlohmann/json.hpp>

namespace AlterEgo::CharacterIdentity {
// Portable provenance is a historical label plus corroborating evidence,
// not an API address or a globally guaranteed identifier.
struct Provenance {
    std::string account,name;
    Evidence evidence;
    bool Valid()const{
        const auto normalized=Normalize(evidence);
        return Text(account)&&Text(name)&&normalized&&*normalized==evidence;
    }
    bool SameEvidence(const Provenance& o)const{return account==o.account&&evidence==o.evidence;}
    bool operator==(const Provenance& o)const{return SameEvidence(o)&&name==o.name;}
    bool Matches(const Address& address)const{return Valid()&&account==address.account&&evidence==address.binding.evidence;}
};
inline Provenance CaptureProvenance(const Address& address){
    return {address.account,address.name,address.binding.evidence};
}
inline nlohmann::json EncodeProvenance(const Provenance& p){
    return {{"account",p.account},{"character",p.name},{"created",p.evidence.created},
        {"profession",p.evidence.profession},{"race",p.evidence.race}};
}
inline std::optional<Provenance> DecodeProvenance(const nlohmann::json& j){
    try{
        if(!j.is_object()||j.contains("local_id"))return std::nullopt;
        Provenance p{j.at("account").get<std::string>(),j.at("character").get<std::string>(),
            {j.at("created").get<std::string>(),j.at("profession").get<std::string>(),j.at("race").get<std::string>()}};
        if(!p.Valid())return std::nullopt;
        return p;
    }catch(...){return std::nullopt;}
}
inline bool AssociateProvenance(std::vector<Provenance>& rows,const Provenance& p){
    if(!p.Valid()||rows.size()>=MaxRoster)return false;
    if(std::any_of(rows.begin(),rows.end(),[&](const auto& prior){return prior.SameEvidence(p);}))return false;
    rows.push_back(p);return true;
}
inline nlohmann::json EncodeAssociations(const std::vector<Provenance>& rows){
    nlohmann::json out=nlohmann::json::array();
    for(const auto& p:rows)out.push_back(EncodeProvenance(p));
    return out;
}
inline std::optional<std::vector<Provenance>> DecodeAssociations(const nlohmann::json& j){
    if(!j.is_array()||j.size()>MaxRoster)return std::nullopt;
    std::vector<Provenance> out;
    for(const auto& row:j){
        const auto p=DecodeProvenance(row);
        if(!p||!AssociateProvenance(out,*p))return std::nullopt;
    }
    return out;
}
} // namespace AlterEgo::CharacterIdentity
