#pragma once
#include "CharacterIdentityRuntime.h"
#include <nlohmann/json.hpp>

namespace AlterEgo::CharacterIdentity {
// UI intent, distinct from an address-fenced request. A verified selection
// follows its identity; a portable selection contains evidence but no local ID.
// Explicitly selected historical snapshots may be previewed but not persisted
// or silently rebound by name.
struct Selection {
    Ref ref;
    Evidence evidence;
    std::string name;
    bool preview=false;
    bool Empty()const{return name.empty();}
    bool Portable()const{return Text(ref.account)&&Text(name)&&Normalize(evidence).has_value();}
    bool SameIdentity(const Selection& o)const{
        if(Empty()||o.Empty())return Empty()&&o.Empty();
        if(ref.Valid()||o.ref.Valid())return ref==o.ref&&evidence==o.evidence;
        return ref.account==o.ref.account&&name==o.name&&evidence==o.evidence;
    }
    bool Accept(const Address& address){
        if(address.account!=ref.account||!(address.binding.evidence==evidence)||
           (ref.Valid()&&address.Reference()!=ref))return false;
        ref=address.Reference();name=address.name;preview=false;return true;
    }
    bool Matches(const Address& address)const{
        return ref.Valid()&&ref==address.Reference()&&evidence==address.binding.evidence;
    }
    bool MatchesPreview(const std::string& account,const std::string& label,const Evidence& observed)const{
        return preview&&!ref.Valid()&&ref.account==account&&name==label&&evidence==observed;
    }
};
inline nlohmann::json EncodeSelection(const Selection& selection,bool portable=false){
    if(selection.preview||!selection.Portable())return nullptr;
    nlohmann::json j={{"account",selection.ref.account},{"character",selection.name},
        {"created",selection.evidence.created},{"profession",selection.evidence.profession},{"race",selection.evidence.race}};
    if(!portable&&selection.ref.Valid())j["local_id"]=selection.ref.id;
    return j;
}
inline std::optional<Selection> DecodeSelection(const nlohmann::json& j){
    try{
        Selection s{{j.at("account").get<std::string>(),j.value("local_id",Id(0))},
            {j.at("created").get<std::string>(),j.at("profession").get<std::string>(),j.at("race").get<std::string>()},
            j.at("character").get<std::string>(),false};
        if(s.ref.id<0||!s.Portable())return std::nullopt;
        const auto e=Normalize(s.evidence);if(!e||!(*e==s.evidence))return std::nullopt;
        return s;
    }catch(...){return std::nullopt;}
}
} // namespace AlterEgo::CharacterIdentity
