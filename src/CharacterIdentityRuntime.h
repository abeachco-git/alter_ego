#pragma once
#include "CharacterIdentityCore.h"
namespace AlterEgo::CharacterIdentity {
// A stable local identity is not an API address. The binding revision
// fences rename cycles, replacements and unresolved transitions without
// invalidating unrelated character requests.
struct Binding {
    Id id=0;
    Evidence evidence;
    int64_t revision=0;
    bool operator==(const Binding& other)const{
        return id==other.id&&evidence==other.evidence&&revision==other.revision;
    }
    bool operator!=(const Binding& other)const{return !(*this==other);}
};
using Bindings=std::map<std::string,Binding>;
// Visible demand may admit an unknown gameplay name, but never create an
// indefinite new polling loop. A new observed name or resolved->unresolved
// transition rearms discovery. All times are monotonic seconds.
class DiscoveryGate {
    std::string name_;
    int attempts_=0;
    bool resolved_=false,hasAttempt_=false;
    int64_t last_=0;
public:
    bool Admit(const std::string& name,bool resolved,int64_t now){
        if(name_!=name||resolved_!=resolved){
            name_=name;resolved_=resolved;attempts_=0;
        }
        if(name.empty()||resolved||attempts_>=3)return false;
        const int64_t wait=attempts_==0?30:attempts_==1?120:600;
        if(hasAttempt_&&(now<last_||now-last_<wait))return false;
        ++attempts_;last_=now;hasAttempt_=true;return true;
    }
};
inline Bindings CurrentBindings(const State& state){
    Bindings result;
    if(!state.initialized)return result;
    for(const auto& [id,r]:state.records)
        if(r.present&&!state.blocked.count(r.name))
            result.emplace(r.name,Binding{id,r.evidence,r.bindingRevision});
    return result;
}
struct Address {
    std::string account,name;
    Binding binding;
    uint64_t credentialGeneration=0;
};
inline std::optional<Address> Capture(const State& state,const std::string& name,uint64_t generation){
    if(!state.initialized)return std::nullopt;
    const auto* r=state.Current(name);
    if(!r)return std::nullopt;
    return Address{state.account,name,{r->id,r->evidence,r->bindingRevision},generation};
}
inline bool Matches(const State& state,const Address& address,uint64_t generation){
    if(state.account!=address.account||generation!=address.credentialGeneration)return false;
    const auto current=Capture(state,address.name,generation);
    return current&&current->binding==address.binding;
}
inline std::set<std::string> ChangedAddresses(const Bindings& prior,const Bindings& next){
    std::set<std::string> result;
    for(const auto& [name,binding]:prior){
        const auto it=next.find(name);
        if(it==next.end()||it->second!=binding)result.insert(name);
    }
    for(const auto& [name,binding]:next){
        const auto it=prior.find(name);
        if(it==prior.end()||it->second!=binding)result.insert(name);
    }
    return result;
}
} // namespace AlterEgo::CharacterIdentity
