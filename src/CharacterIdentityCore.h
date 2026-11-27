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
};
struct State {
    std::string account;
    int64_t revision=0;
    Id nextId=1;
    std::map<Id,Record> records;
    const Record* Current(const std::string& name)const{
        for(const auto& [id,r]:records){(void)id;if(r.present&&r.name==name)return &r;}
        return nullptr;
    }
};
inline bool Valid(const State& s){
    if(!Text(s.account)||s.revision<0||s.nextId<1||s.records.size()>MaxRecords)return false;
    std::set<std::string> current;std::set<Evidence> fingerprints;
    for(const auto& [id,r]:s.records){
        const auto e=Normalize(r.evidence);
        if(id<1||id>=s.nextId||r.id!=id||!e||!(*e==r.evidence)||!Text(r.name)||
           r.names.empty()||r.names.size()>MaxAliases||!r.names.count(r.name)||
           !fingerprints.insert(r.evidence).second)return false;
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
    explicit operator bool()const{return status==Status::Ready;}
};
// The caller must supply one complete, authenticated core observation for
// EVERY exact name in one verified roster, from the same account/generation.
// No mutation on partial evidence, collision or historical fingerprint reuse.
// Simultaneous name swaps are resolved by immutable ID, never sequential renames.
inline Plan Reconcile(const State& old,const std::set<std::string>& roster,
                      const std::vector<Observation>& observed){
    auto fail=[](Status status){Plan p;p.status=status;return p;};
    if(!Valid(old)||roster.size()>MaxRoster||observed.size()>MaxRoster)return fail(Status::Invalid);
    if(roster.size()!=observed.size())return fail(Status::Incomplete);
    std::map<Evidence,std::string> current;
    std::set<std::string> names;
    for(const auto& row:observed){
        auto e=Normalize(row.evidence);
        if(!Text(row.name)||!e||!roster.count(row.name)||!names.insert(row.name).second)
            return fail(Status::Incomplete);
        if(!current.emplace(*e,row.name).second)return fail(Status::Ambiguous);
    }
    if(names!=roster)return fail(Status::Incomplete);
    Plan p;p.status=Status::Ready;p.next=old;
    std::map<Evidence,Id> byEvidence;
    for(const auto& [id,r]:old.records)byEvidence.emplace(r.evidence,id);
    std::set<Id> matched;
    for(const auto& [e,name]:current){
        const auto prior=byEvidence.find(e);
        if(prior==byEvidence.end()){
            if(p.next.records.size()>=MaxRecords||p.next.nextId==std::numeric_limits<Id>::max())
                return fail(Status::Exhausted);
            const auto id=p.next.nextId++;
            p.next.records.emplace(id,Record{id,e,name,true,{name}});
            p.changes.push_back({id,Kind::Added,{},name});matched.insert(id);
        }else{
            auto& r=p.next.records.at(prior->second);
            // A previously confirmed absence might be restoration or a rare
            // fingerprint collision. Do not silently inherit retired intent.
            if(!r.present)return fail(Status::Ambiguous);
            matched.insert(r.id);
            if(r.name!=name){
                if(!r.names.count(name)&&r.names.size()>=MaxAliases)return fail(Status::Exhausted);
                p.changes.push_back({r.id,Kind::Renamed,r.name,name});
                r.name=name;r.names.insert(name);
            }
        }
    }
    for(auto& [id,r]:p.next.records)if(r.present&&!matched.count(id)){
        p.changes.push_back({id,Kind::Removed,r.name,{}});r.present=false;
    }
    // A verification without a changed binding is not a mapping change.
    if(!p.changes.empty()){
        if(old.revision==std::numeric_limits<int64_t>::max())return fail(Status::Exhausted);
        ++p.next.revision;
    }
    if(!Valid(p.next))return fail(Status::Invalid);
    return p;
}
} // namespace AlterEgo::CharacterIdentity
