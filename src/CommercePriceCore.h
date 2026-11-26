#pragma once
#include "SkinSearchCore.h"
#include "UntrustedInput.h"
#include <limits>
#include <stdexcept>
namespace Skinventory {
// Used both before claiming IDs and after acquisition of the shared broker.
// Select from the input batch, never the initially empty output vector.
inline std::vector<uint32_t> SelectPriceRequests(const std::vector<uint32_t>& candidates,
    const std::unordered_map<uint32_t,PriceInfo>& cached,bool manual,int64_t now,int64_t ttl){
    std::vector<uint32_t> needed;needed.reserve(candidates.size());
    for(auto id:candidates){
        if(!id)continue;
        const auto it=cached.find(id);
        if(manual||it==cached.end()||it->second.fetched_at_unix<=0||
           it->second.fetched_at_unix>now||now-it->second.fetched_at_unix>=ttl)
            needed.push_back(id);
    }
    return needed;
}
inline int CommercePriceField(const nlohmann::json& j,const char* key){
    const auto& v=j.at(key);if(!v.is_number_integer())throw std::runtime_error("Invalid commerce integer");
    const auto n=v.get<int64_t>();
    if(n<0||n>std::numeric_limits<int>::max())throw std::runtime_error("Invalid commerce range");
    return static_cast<int>(n);
}
// 200 covers all requested IDs; 206 covers a strict nonempty subset;
// 404 must explicitly report invalid requested IDs. Fail closed otherwise.
inline std::unordered_map<uint32_t,PriceInfo> ParseCommercePrices(int status,const std::string& body,
        const std::vector<uint32_t>& ids,int64_t stamp){
    std::unordered_map<uint32_t,PriceInfo> out;
    if(ids.empty()||stamp<=0||(status!=200&&status!=206&&status!=404))return out;
    const std::unordered_set<uint32_t> expected(ids.begin(),ids.end());
    try{
        auto j=AlterEgo::Untrusted::Parse(body,AlterEgo::Untrusted::PublicApi);
        if(status==404){
            if(!j.is_object()||j.value("text",std::string())!="all ids provided are invalid")return {};
        }else{
            if(!j.is_array())return {};
            for(const auto& e:j){
                const auto& raw=e.at("id");if(!raw.is_number_integer())return {};
                const auto n=raw.get<int64_t>();if(n<=0||n>4294967295LL)return {};
                const auto id=static_cast<uint32_t>(n);if(!expected.count(id)||out.count(id))return {};
                PriceInfo p;p.item_id=id;p.tradeable=true;p.fetched_at_unix=stamp;
                p.buy_price=CommercePriceField(e.at("buys"),"unit_price");p.buy_quantity=CommercePriceField(e.at("buys"),"quantity");
                p.sell_price=CommercePriceField(e.at("sells"),"unit_price");p.sell_quantity=CommercePriceField(e.at("sells"),"quantity");
                out.emplace(id,p);
            }
        }
        if(status==200&&out.size()!=expected.size())return {};
        if(status==206&&(out.empty()||out.size()>=expected.size()))return {};
        if(status!=200)for(auto id:ids)if(!out.count(id)){PriceInfo p;p.item_id=id;p.fetched_at_unix=stamp;out.emplace(id,p);}
        return out;
    }catch(...){return {};}
}
// Summary quote only; round each fee upward and reject overflow.
inline std::optional<int64_t> EstimateInstantSale(int64_t count,int buy){
    if(count<=0||buy<=0||count>std::numeric_limits<int64_t>::max()/buy)return std::nullopt;
    const int64_t gross=count*buy;
    const auto fee=[gross](int percent){return (gross/100)*percent+((gross%100)*percent+99)/100;};
    return std::max<int64_t>(0,gross-fee(5)-fee(10));
}
}
