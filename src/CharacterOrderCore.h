#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <numeric>
#include <string>
#include <utility>
#include <vector>
namespace AlterEgo::CharacterOrder {
// Persisted sort values; never reorder these.
enum Mode { Custom=0, Name, Profession, Level, Age, Birthday, LastLogin };
using Key=std::pair<std::string,std::string>;
struct Entry {
    Key key;
    std::string profession,created;
    int level=0,birthday=-1;
    int64_t login=0;
};
inline std::vector<int> Indices(const std::vector<Entry>& rows,int mode,bool asc,
    const std::vector<std::string>& custom,const std::vector<std::string>& accounts){
    std::vector<int> order(rows.size());std::iota(order.begin(),order.end(),0);
    std::map<std::string,size_t> customRank,accountRank;
    for(size_t i=0;i<custom.size();++i)customRank.emplace(custom[i],i);
    for(size_t i=0;i<accounts.size();++i)accountRank.emplace(accounts[i],i);
    auto rank=[](const auto& ranks,const std::string& name){auto it=ranks.find(name);return it==ranks.end()?ranks.size():it->second;};
    auto before=[&](const auto& a,const auto& b){return asc?a<b:b<a;};
    std::stable_sort(order.begin(),order.end(),[&](int x,int y){
        const auto& a=rows[x];const auto& b=rows[y];
        if(!accounts.empty()){
            const auto ar=rank(accountRank,a.key.first),br=rank(accountRank,b.key.first);
            if(ar!=br)return ar<br;
        }
        const auto& an=a.key.second;const auto& bn=b.key.second;
        // Missing metadata sorts after known metadata in either direction.
        auto unknown=[&](bool ak,bool bk){return ak!=bk? (ak?1:-1):0;};
        switch(mode){
        case Name:return before(an,bn);
        case Profession:
            if(auto k=unknown(!a.profession.empty(),!b.profession.empty()))return k>0;
            return a.profession!=b.profession?before(a.profession,b.profession):an<bn;
        case Level:
            if(auto k=unknown(a.level>0,b.level>0))return k>0;
            return a.level!=b.level?before(b.level,a.level):an<bn;
        case Age:
            if(auto k=unknown(!a.created.empty(),!b.created.empty()))return k>0;
            return before(a.created,b.created);
        case Birthday:
            if(auto k=unknown(a.birthday>=0,b.birthday>=0))return k>0;
            return a.birthday!=b.birthday?before(a.birthday,b.birthday):an<bn;
        case LastLogin:
            if(auto k=unknown(a.login>0,b.login>0))return k>0;
            return before(b.login,a.login);
        case Custom:
        default:return rank(customRank,an)<rank(customRank,bn);
        }
    });
    return order;
}
inline std::map<Key,size_t> Ranks(const std::vector<Key>& ordered){
    std::map<Key,size_t> ranks;
    for(size_t i=0;i<ordered.size();++i)ranks.emplace(ordered[i],i);
    return ranks;
}
}
