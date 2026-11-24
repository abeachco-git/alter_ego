#pragma once
#include <string>
#include <map>
#include <set>
#include <utility>
namespace AlterEgo::CharacterRequest {
struct ReplyGate {
    std::string account, endpoint;
    bool pending = false;
    void Arm(std::string a, std::string e) {
        account=std::move(a); endpoint=std::move(e); pending=true;
    }
    void Clear() { pending=false; }
    bool Claim(const std::string& a,const std::string& e,bool accountEcho,bool singleAccount) {
        if(!pending || endpoint!=e) return false;
        if(accountEcho) {
            if((!account.empty() && a!=account) || (account.empty() && !singleAccount))return false;
        } else if(!singleAccount)return false;
        pending=false;return true;
    }
};
using Roster=std::map<std::string,std::set<std::string>>;
inline bool VerifiedAbsent(const Roster& r,const std::string& a,const std::string& n) {
    const auto it=r.find(a);
    return !a.empty() && it!=r.end() && !it->second.count(n);
}
}
