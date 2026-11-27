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
// Durable local reference; never use this as an API address or portable ID.
struct Ref {
    std::string account;
    Id id=0;
    bool Valid()const{return !account.empty()&&id>0;}
    bool operator==(const Ref& o)const{return account==o.account&&id==o.id;}
    bool operator!=(const Ref& o)const{return !(*this==o);}
    bool operator<(const Ref& o)const{return std::tie(account,id)<std::tie(o.account,o.id);}
};
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
    Ref Reference()const{return {account,binding.id};}
};
inline bool AdmittedBinding(const Bindings& admitted,const std::string& name,const Binding& current){
    const auto it=admitted.find(name);
    return it!=admitted.end()&&it->second==current;
}
inline bool SameAddress(const Address& a,const Address& b){
    return a.account==b.account&&a.name==b.name&&a.binding==b.binding&&a.credentialGeneration==b.credentialGeneration;
}
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
struct RenameNotice {Ref ref;std::string before,after;};
inline std::vector<RenameNotice> VerifiedRenames(const State& prior,const State& next){
    std::vector<RenameNotice> out;
    if(!prior.initialized||!next.initialized||prior.account!=next.account)return out;
    for(const auto& [id,r]:next.records){
        const auto old=prior.records.find(id);
        if(old!=prior.records.end()&&old->second.present&&r.present&&old->second.name!=r.name&&
           old->second.evidence==r.evidence&&next.Current(r.name)==&r)
            out.push_back({{next.account,id},old->second.name,r.name});
    }
    return out;
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
