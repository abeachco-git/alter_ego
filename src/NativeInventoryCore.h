#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <set>
#include <map>
#include "SkinSearchCore.h"

namespace AlterEgo::NativeInventory {
using Json=nlohmann::json;
enum class Binding { Unknown, Unbound, Account, Character };
enum class Access { Unknown, Available, Transferable, Restricted };
struct Slot {
    std::string account, scope, character, position, boundTo;
    uint32_t id=0;
    int64_t count=0;
    Binding binding=Binding::Unknown;
    int64_t stat=-1, charges=-1;
    std::vector<uint32_t> upgrades, infusions;
};
inline uint32_t ItemId(const Json& j) {
    if(!j.is_number_integer()) throw std::runtime_error("Invalid item ID");
    const auto n=j.get<int64_t>();
    if(n<=0||n>4294967295LL)throw std::runtime_error("Invalid item ID");
    return static_cast<uint32_t>(n);
}
inline Binding ParseBinding(const Json& j,bool reportsBinding) {
    if(j.contains("binding")) {
        if(!j.at("binding").is_string())return Binding::Unknown;
        const auto b=j.at("binding").get<std::string>();
        if(b=="Account")return Binding::Account;
        if(b=="Character")return Binding::Character;
        return Binding::Unknown;
    }
    return reportsBinding?Binding::Unbound:Binding::Unknown;
}
inline Access Accessibility(const Slot& s,const std::string& account,const std::string& character,
                            bool transferVerified) {
    if(account.empty()||s.account.empty()||s.binding==Binding::Unknown)return Access::Unknown;
    if(s.binding==Binding::Character) {
        if(s.account!=account)return Access::Restricted;
        if(s.boundTo.empty()||character.empty())return Access::Unknown;
        return s.boundTo==character?Access::Available:Access::Restricted;
    }
    if(s.scope=="Delivery box"||s.scope=="Equipment")return Access::Unknown;
    if(s.account==account)return Access::Available;
    if(s.binding==Binding::Account)return Access::Restricted;
    return transferVerified?Access::Transferable:Access::Unknown;
}
inline const char* BindingName(Binding b) {
    switch(b){case Binding::Unbound:return "Unbound";case Binding::Account:return "Account bound";
    case Binding::Character:return "Character bound";default:return "Unknown";}
}
inline Slot ParseSlot(const Json& row,const std::string& account,const std::string& scope,
                     const std::string& character,const std::string& position,bool reportsBinding) {
    if(!row.is_object()||account.empty()||position.empty())throw std::runtime_error("Invalid inventory slot");
    Slot s; s.account=account;s.scope=scope;s.character=character;s.position=position;
    s.id=ItemId(row.at("id"));
    const auto& count=row.at("count");
    if(!count.is_number_integer()||(s.count=count.get<int64_t>())<0)
        throw std::runtime_error("Invalid quantity");
    s.binding=ParseBinding(row,reportsBinding);
    s.boundTo=row.value("bound_to",std::string{});
    if(row.contains("stats")&&!row.at("stats").is_null())s.stat=ItemId(row.at("stats").at("id"));
    if(row.contains("charges"))s.charges=row.at("charges").get<int64_t>();
    for(const auto* kind:{"upgrades","infusions"})if(row.contains(kind)){
        const auto& values=row.at(kind);
        if(!values.is_array())throw std::runtime_error("Invalid socket list");
        auto& dest=std::string(kind)=="upgrades"?s.upgrades:s.infusions;
        for(const auto& id:values)dest.push_back(ItemId(id));
    }
    return s;
}
inline std::vector<Slot> ParseArray(const Json& j,const std::string& account,const std::string& scope,
                                  const std::string& character={},const std::string& prefix={},
                                  bool reportsBinding=true) {
    if(!j.is_array())throw std::runtime_error("Inventory response is not an array");
    std::vector<Slot> out;size_t i=0;
    for(const auto& row:j){const auto pos=prefix+std::to_string(i++);
        if(row.is_null())continue;
        auto s=ParseSlot(row,account,scope,character,pos,reportsBinding);
        if(s.count)out.push_back(std::move(s));
    }
    return out;
}
// The character equipment endpoint provides one physical item per record,
// with a list of template references. Legendary references are not stock.
inline std::vector<Slot> ParseEquipment(const Json& j,const std::string& account,const std::string& character){
    const auto& equipment=j.at("equipment");
    if(!equipment.is_array())throw std::runtime_error("Invalid equipment response");
    std::vector<Slot> out;size_t i=0;
    for(const auto& raw:equipment){
        if(!raw.is_object())throw std::runtime_error("Invalid equipment row");
        const auto location=raw.value("location",std::string{});
        if(location=="LegendaryArmory"||location=="EquippedFromLegendaryArmory"){++i;continue;}
        const auto slot=raw.value("slot",std::string{});
        const bool tool=slot=="Sickle"||slot=="Axe"||slot=="Pick"||slot=="FishingRod"||slot=="FishingLure"||slot=="FishingBait";
        // Some character-wide tools omit the location field. For ordinary
        // gear an unknown location cannot establish physical ownership.
        if(location!="Equipped"&&location!="Armory"&&!(tool&&location.empty()))
            throw std::runtime_error("Unknown physical equipment location");
        auto row=raw;row["count"]=1;
        const auto pos=std::to_string(i++)+"/"+(slot.empty()?location:slot);
        auto parsed=ParseSlot(row,account,"Equipment",character,pos,true);
        out.push_back(std::move(parsed));
    }
    return out;
}
inline std::vector<Slot> ParseArmory(const Json& j,const std::string& account){
    auto rows=ParseArray(j,account,"Legendary Armory",{},{},false);
    std::set<uint32_t> seen;
    for(auto& row:rows){if(!seen.insert(row.id).second)throw std::runtime_error("Duplicate armory ID");row.binding=Binding::Account;}
    return rows;
}
struct CurrencyBalance {uint32_t id=0;int64_t value=0;};
inline int64_t Nonnegative(const Json& j){
    if(!j.is_number_integer())throw std::runtime_error("Invalid quantity");
    auto n=j.get<int64_t>();if(n<0)throw std::runtime_error("Negative quantity");return n;
}
inline std::vector<CurrencyBalance> ParseWallet(const Json& j){
    if(!j.is_array())throw std::runtime_error("Invalid wallet");
    std::vector<CurrencyBalance> out;std::set<uint32_t> seen;
    for(const auto& r:j){auto id=ItemId(r.at("id"));if(!seen.insert(id).second)throw std::runtime_error("Duplicate currency");out.push_back({id,Nonnegative(r.at("value"))});}
    return out;
}
struct GuildStock {std::vector<Slot> slots;std::map<std::string,int64_t> coins;};
inline GuildStock ParseGuild(const Json& j,const std::string& guid){
    if(!j.is_array())throw std::runtime_error("Invalid guild stash");
    GuildStock out;std::set<std::string> sections;
    for(const auto& r:j){
        auto section=std::to_string(ItemId(r.at("upgrade_id")));
        if(!sections.insert(section).second)throw std::runtime_error("Duplicate stash section");
        const auto& inv=r.at("inventory");
        if(!inv.is_array()||Nonnegative(r.at("size"))!=static_cast<int64_t>(inv.size()))throw std::runtime_error("Incomplete stash");
        auto slots=ParseArray(inv,guid,"Guild bank",{},section+"/",false);
        out.slots.insert(out.slots.end(),slots.begin(),slots.end());
        out.coins[section]=Nonnegative(r.at("coins"));
    }
    return out;
}
inline int64_t FreshSeconds(const std::string& scope){
    if(scope=="Roster")return 86400;
    return 1800;
}
inline bool Due(const std::string& scope,int64_t attempted,int64_t success,int64_t now){
    // Failed attempts back off 15 minutes; manual refresh bypasses Due.
    if(attempted>0&&attempted>=success&&now>=attempted&&now-attempted<900)return false;
    return success<=0||now<success||now-success>=FreshSeconds(scope);
}
struct ItemFlags {bool known=false,accountBound=false,soulbound=false,bindOnUse=false;};
inline ItemFlags ParseFlags(const Json& flags){
    if(!flags.is_array())throw std::runtime_error("Invalid flags");
    ItemFlags f;f.known=true;
    for(const auto& v:flags){auto s=v.get<std::string>();
        if(s=="AccountBound")f.accountBound=true;
        if(s=="SoulbindOnAcquire"||s=="SoulBindOnAcquire")f.soulbound=true;
        if(s=="AccountBindOnUse"||s=="SoulBindOnUse"||s=="SoulbindOnUse")f.bindOnUse=true;
    }return f;
}
inline bool UsableLoose(const Slot& s,const std::string& account,const std::string& character,
                       const ItemFlags& f,bool fresh,int64_t requestedStat=-1){
    if(!fresh||account.empty()||s.account!=account)return false;
    if(s.scope!="Bank"&&s.scope!="Shared inventory"&&s.scope!="Bags"&&s.scope!="Materials")return false;
    if(requestedStat>0&&requestedStat!=s.stat)return false;
    if(s.binding==Binding::Character)return !character.empty()&&s.boundTo==character;
    if(s.binding==Binding::Unknown)return f.known&&!f.soulbound&&s.scope=="Materials";
    if(f.known&&f.soulbound&&s.binding==Binding::Unbound)return false;
    return true;
}
inline std::string Lower(std::string s){for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
// Character is one facet, regardless of the number of selected names.
// Account scope is a navigation context, not an additional item filter.
inline bool CanBrowseItems(bool character, bool storage, bool binding, bool rarity,
                           bool type, bool freshness, bool quantity, bool transfer) {
    return character&&(storage||binding||rarity||type||freshness||quantity||transfer);
}
inline bool Match(const std::string& name,uint32_t id,const std::vector<std::string>& words){
    return Skinventory::Search::Matches(name+" "+std::to_string(id),words);
}
inline bool Match(const std::string& name,uint32_t id,const std::string& query){
    return Match(name,id,Skinventory::Search::Words(query));
}
} // namespace AlterEgo::NativeInventory
