#pragma once
// Identity evidence is account-qualified and independent of the current API name.
// No game-memory access, transport, UI or persistence here.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace AlterEgo::CharacterIdentity {
using Id=int64_t;
inline constexpr size_t MaxRoster=256,MaxRecords=4096,MaxAliases=256;
inline bool Text(const std::string& s,size_t limit=256){
    if(s.empty()||s.size()>limit)return false;
    for(unsigned char c:s)if(c<32||c==127)return false;
    return true;
}
// The authenticated API reports ISO 8601 UTC creation times. Preserve the
// full time; normalize only equivalent trailing fractional zeros. Reject
// unsupported/malformed precision instead of guessing an identity.
inline std::optional<std::string> Creation(const std::string& s){
    if(s.size()<20||s.size()>30||s.back()!='Z'||s[4]!='-'||s[7]!='-'||
       s[10]!='T'||s[13]!=':'||s[16]!=':')return std::nullopt;
    auto number=[&](size_t pos,size_t count){
        int value=0;for(size_t i=pos;i<pos+count;++i){
            if(s[i]<'0'||s[i]>'9')return -1;
            value=value*10+s[i]-'0';
        }return value;
    };
    const int y=number(0,4),m=number(5,2),d=number(8,2),h=number(11,2),
        min=number(14,2),sec=number(17,2);
    constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(y<1||m<1||m>12||d<1||h<0||h>23||min<0||min>59||sec<0||sec>59)
        return std::nullopt;
    const int monthDays=days[m-1]+(m==2&&y%4==0&&(y%100!=0||y%400==0)?1:0);
    if(d>monthDays)return std::nullopt;
    if(s.size()==20)return s;
    if(s[19]!='.'||s.size()<22)return std::nullopt;
    for(size_t i=20;i+1<s.size();++i)if(s[i]<'0'||s[i]>'9')return std::nullopt;
    size_t end=s.size()-1;while(end>20&&s[end-1]=='0')--end;
    return s.substr(0,end==20?19:end)+"Z";
}
struct Evidence {
    std::string created,profession,race;
    bool operator<(const Evidence& o)const{
        return std::tie(created,profession,race)<std::tie(o.created,o.profession,o.race);
    }
    bool operator==(const Evidence& o)const{return !(*this<o)&&!(o<*this);}
};
inline std::optional<Evidence> Normalize(Evidence e){
    const auto created=Creation(e.created);
    static const std::set<std::string> professions={"Guardian","Warrior","Engineer","Ranger","Thief","Elementalist","Mesmer","Necromancer","Revenant"};
    static const std::set<std::string> races={"Asura","Charr","Human","Norn","Sylvari"};
    if(!created||!professions.count(e.profession)||!races.count(e.race))return std::nullopt;
    e.created=*created;return e;
}
struct Record {
    Id id=0;
    Evidence evidence;
    std::string name;
    bool present=false;
    std::set<std::string> names; // historical only: never a current-name lookup
    int64_t bindingRevision=0; // changes only when this identity/address changes
};
struct State {
    std::string account;
    int64_t revision=0;
    Id nextId=1;
    std::map<Id,Record> records;
    bool initialized=false;
    // A retained binding is not necessarily safe to use as a current address.
    std::set<std::string> blocked;
    std::set<Id> pendingAbsence;
    const Record* Bound(const std::string& name)const{
        for(const auto& [id,r]:records){(void)id;if(r.present&&r.name==name)return &r;}
        return nullptr;
    }
    const Record* Current(const std::string& name)const{
        return blocked.count(name)?nullptr:Bound(name);
    }
};
inline bool Valid(const State& s){
    if(!Text(s.account)||s.revision<0||s.nextId<1||s.records.size()>MaxRecords)return false;
    if(s.blocked.size()>MaxRecords+MaxRoster||s.pendingAbsence.size()>MaxRoster)return false;
    std::set<std::string> current;
    for(const auto& name:s.blocked)if(!Text(name))return false;
    for(const auto id:s.pendingAbsence){
        const auto it=s.records.find(id);
        if(it==s.records.end()||!it->second.present||!s.blocked.count(it->second.name))return false;
    }
    for(const auto& [id,r]:s.records){
        const auto e=Normalize(r.evidence);
        if(id<1||id>=s.nextId||r.id!=id||!e||!(*e==r.evidence)||!Text(r.name)||
           r.bindingRevision<0||r.bindingRevision>s.revision||
           r.names.empty()||r.names.size()>MaxAliases)return false;
        for(const auto& n:r.names)if(!Text(n))return false;
        if(r.present&&!current.insert(r.name).second)return false;
    }
    return current.size()<=MaxRoster;
}
struct Observation {std::string name;Evidence evidence;};
enum class Kind { Added,Renamed,Removed };
struct Change {Id id;Kind kind;std::string from,to;};
enum class Status { Ready,Incomplete,Ambiguous,Invalid,Exhausted };
struct Plan {
    Status status=Status::Invalid;
    State next;
    std::vector<Change> changes;
    bool complete=false; // every roster name supplied valid core evidence
    explicit operator bool()const{return status==Status::Ready;}
    bool FullyResolved()const{return status==Status::Ready&&complete&&next.blocked.empty();}

};
// The roster is a verified complete NAME list. Core observations may be
// partial. Only the live owner can establish account, request generation
// and independent evidence freshness. confirmAbsences must never be set
// for a replay/coalesced result or a partial scan. A first complete absence
// blocks the address; a second independently verified complete absence
// can retire it. Never resolve a retired fingerprint by name alone.
inline Plan Reconcile(const State& old,const std::set<std::string>& roster,
                      const std::vector<Observation>& observed,bool confirmAbsences=false){
    auto fail=[](Status status){Plan p;p.status=status;return p;};
    if(!Valid(old)||roster.size()>MaxRoster||observed.size()>MaxRoster)return fail(Status::Invalid);
    for(const auto& n:roster)if(!Text(n))return fail(Status::Invalid);
    std::map<std::string,Evidence> valid;
    std::map<Evidence,std::vector<std::string>> observedByEvidence;
    std::set<std::string> supplied;
    Plan p;p.status=Status::Ready;p.next=old;p.next.initialized=true;
    auto& blocked=p.next.blocked;blocked.clear();
    for(const auto& row:observed){
        if(!roster.count(row.name)||!supplied.insert(row.name).second)return fail(Status::Invalid);
        auto e=Normalize(row.evidence);
        if(!e){blocked.insert(row.name);continue;}
        valid.emplace(row.name,*e);observedByEvidence[*e].push_back(row.name);
    }
    p.complete=valid.size()==roster.size();
    std::map<Evidence,std::vector<Id>> priorByEvidence;
    for(const auto& [id,r]:old.records)priorByEvidence[r.evidence].push_back(id);
    // Proposal zero denotes a new identity. Existing proposals are indexed
    // in both directions so address cycles are evaluated as a single group.
    std::map<std::string,Id> proposals;
    std::map<Id,std::string> destinations;
    for(const auto& n:roster)if(!valid.count(n)&&(!old.Bound(n)||old.blocked.count(n)))blocked.insert(n);
    for(const auto& [e,names]:observedByEvidence){
        const auto prior=priorByEvidence.find(e);
        const bool uniquePrior=prior!=priorByEvidence.end()&&prior->second.size()==1;
        if(names.size()!=1||(prior!=priorByEvidence.end()&&
            (!uniquePrior||!old.records.at(prior->second.front()).present))){
            blocked.insert(names.begin(),names.end());
            if(prior!=priorByEvidence.end())for(auto id:prior->second){
                const auto& r=old.records.at(id);
                if(r.present)blocked.insert(r.name);
                p.next.pendingAbsence.erase(id);
            }
            continue;
        }
        const auto& name=names.front();
        if(prior==priorByEvidence.end()){proposals.emplace(name,0);continue;}
        const auto id=prior->second.front();const auto& r=old.records.at(id);
        p.next.pendingAbsence.erase(id);
        proposals.emplace(name,id);destinations.emplace(id,name);
        // A partial response cannot authorize moving an identity away from
        // a still-listed but unobserved old address.
        if(r.name!=name&&roster.count(r.name)&&!valid.count(r.name)){
            blocked.insert(r.name);blocked.insert(name);
        }
    }
    // A previously unresolved group requires all of its still-listed
    // addresses to be observed before clearing its uncertainty.
    const bool unresolvedOld=std::any_of(old.blocked.begin(),old.blocked.end(),
        [&](const auto& n){return roster.count(n)&&!valid.count(n);});
    if(unresolvedOld)for(const auto& [name,id]:proposals){
        if(old.blocked.count(name)||(id&&old.blocked.count(old.records.at(id).name))){
            blocked.insert(name);if(id)blocked.insert(old.records.at(id).name);
        }
    }
    std::set<Id> retiring;
    for(const auto& [id,r]:old.records)if(r.present&&!destinations.count(id)){
        const bool observedFingerprint=observedByEvidence.count(r.evidence)!=0;
        if(observedFingerprint){p.next.pendingAbsence.erase(id);continue;}
        const bool conflict=!roster.count(r.name)||supplied.count(r.name)!=0||old.blocked.count(r.name);
        if(!conflict)continue;
        if(p.complete&&confirmAbsences&&old.pendingAbsence.count(id)){
            retiring.insert(id);p.next.pendingAbsence.erase(id);
        }else{
            blocked.insert(r.name);
            if(p.complete)p.next.pendingAbsence.insert(id);
        }
    }
    // History bounds limit new allocation, not verification of other
    // characters or renaming an established identity.
    size_t remaining=MaxRecords-old.records.size();
    auto nextId=old.nextId;
    for(const auto& [name,id]:proposals)if(!id&&!blocked.count(name)){
        if(!remaining||nextId==std::numeric_limits<Id>::max())blocked.insert(name);
        else{--remaining;++nextId;}
    }
    // Propagate uncertainty through connected address claims. An occupied
    // destination may only be adopted if its prior owner is also moving or
    // independently confirmed retired.
    bool changed=true;
    while(changed){
        const auto before=blocked.size();
        for(const auto& [name,id]:proposals){
            const auto* occupant=old.Bound(name);
            bool conflict=blocked.count(name)!=0||(id&&blocked.count(old.records.at(id).name));
            if(occupant&&occupant->id!=id&&!retiring.count(occupant->id)){
                const auto move=destinations.find(occupant->id);
                if(move==destinations.end()||move->second==name||blocked.count(move->second))conflict=true;
            }
            if(conflict){blocked.insert(name);if(id)blocked.insert(old.records.at(id).name);}
        }
        changed=blocked.size()!=before;
    }
    for(auto id:retiring){
        auto& r=p.next.records.at(id);r.present=false;
        p.changes.push_back({id,Kind::Removed,r.name,{}});
    }
    for(const auto& [name,id]:proposals){
        if(blocked.count(name)||(id&&blocked.count(old.records.at(id).name)))continue;
        if(!id){
            const auto local=p.next.nextId++;
            p.next.records.emplace(local,Record{local,valid.at(name),name,true,{name}});
            p.changes.push_back({local,Kind::Added,{},name});
        }else{
            auto& r=p.next.records.at(id);
            if(r.name!=name){
                p.changes.push_back({id,Kind::Renamed,r.name,name});
                r.name=name;
                // Keep existing history at its cap. Current name is stored
                // independently; a full history must not prevent a rename.
                if(r.names.size()<MaxAliases)r.names.insert(name);
            }
        }
    }
    // Forget unresolved labels that are neither in the latest roster nor
    // held by a retained binding. Retired records retain their provenance.
    for(auto it=blocked.begin();it!=blocked.end();){
        if(!roster.count(*it)&&!p.next.Bound(*it))it=blocked.erase(it);else ++it;
    }
    if(!p.changes.empty()||blocked!=old.blocked||p.next.pendingAbsence!=old.pendingAbsence){
        if(old.revision==std::numeric_limits<int64_t>::max())return fail(Status::Exhausted);
        ++p.next.revision;
        for(auto& [id,r]:p.next.records){
            const auto prior=old.records.find(id);
            if(prior==old.records.end()||r.present!=prior->second.present||
               r.name!=prior->second.name||
               (r.present&&bool(blocked.count(r.name))!=bool(old.blocked.count(prior->second.name))))
                r.bindingRevision=p.next.revision;
        }
    }
    if(!Valid(p.next))return fail(Status::Invalid);
    return p;
}
} // namespace AlterEgo::CharacterIdentity
