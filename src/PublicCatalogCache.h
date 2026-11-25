#pragma once
#include "ItemMetadataCache.h"
#include <algorithm>
#include <chrono>

namespace AlterEgo::PublicCatalog {
using Json = nlohmann::json;
inline constexpr int64_t Week = 7 * 86400, Retry = 86400;
inline int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
inline bool Due(int64_t success,int64_t attempt,int64_t now,bool manual) {
    return manual || ((!success || now-success>=Week) && (!attempt || now-attempt>=Retry));
}
inline uint32_t Id(const Json& v) {
    if(!v.is_number_integer())throw std::runtime_error("Invalid public ID");
    const auto n=v.get<int64_t>();
    if(n<=0||n>UINT32_MAX)throw std::runtime_error("Invalid public ID");
    return static_cast<uint32_t>(n);
}
inline std::set<uint32_t> Index(const Json& rows) {
    if(!rows.is_array()||rows.empty())throw std::runtime_error("Empty public index");
    std::set<uint32_t> out;
    for(const auto& v:rows)if(!out.insert(Id(v)).second)throw std::runtime_error("Duplicate public ID");
    return out;
}
inline std::string Kind(const Json& row) {
    const auto type=row.at("type").get<std::string>();
    if(type=="Relic")return "Relic";
    if(type!="UpgradeComponent")return {};
    const auto& d=row.at("details");
    const auto sub=d.at("type").get<std::string>();
    if(sub=="Rune"||sub=="Sigil")return sub;
    if(d.contains("infusion_upgrade_flags")) {
        for(const auto& flag:d.at("infusion_upgrade_flags")) {
            if(flag=="Enrichment")return "Enrichment";
            if(flag=="Infusion")return "Infusion";
        }
    }
    return {};
}
inline void ValidateBatch(const Json& rows,const std::set<uint32_t>& expected) {
    if(!rows.is_array())throw std::runtime_error("Invalid item batch");
    std::set<uint32_t> seen;
    for(const auto& row:rows) {
        if(row.at("name").get<std::string>().empty()||!seen.insert(Id(row.at("id"))).second)
            throw std::runtime_error("Invalid item row");
        (void)Kind(row);
    }
    if(seen!=expected)throw std::runtime_error("Incomplete item batch");
}
inline int64_t Fresh(Database::Connection& db,const std::string& dataset) {
    auto q=db.Prepare("SELECT saved_at FROM reference_freshness WHERE dataset=?");
    q.Bind(1,dataset);return q.Step()?q.Integer(0):0;
}
inline void Mark(Database::Connection& db,const std::string& dataset,int64_t stamp) {
    auto q=db.Prepare("INSERT INTO reference_freshness VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET saved_at=excluded.saved_at");
    q.Bind(1,dataset);q.Bind(2,stamp);q.Execute();
}
inline void StoreUpgrades(Database::Connection& db,const std::set<uint32_t>& ids,const Json& rows,int64_t stamp) {
    if(ids.empty()||!rows.is_array()||rows.empty())throw std::runtime_error("Empty upgrade catalog");
    const auto previousCount=db.Scalar("SELECT count(*) FROM public_item_index");
    if(previousCount>0&&static_cast<int64_t>(ids.size())<previousCount*9/10)
        throw std::runtime_error("Unexpectedly truncated item index");
    if(stamp<Fresh(db,"public_upgrades"))throw std::runtime_error("Older upgrade catalog");
    std::set<uint32_t> seen;std::set<std::string> kinds;
    for(const auto& row:rows) {
        auto id=Id(row.at("id"));auto kind=Kind(row);
        if(!ids.count(id)||kind.empty()||!seen.insert(id).second||row.at("name").get<std::string>().empty())
            throw std::runtime_error("Invalid upgrade catalog row");
        kinds.insert(kind);
    }
    if(kinds.size()!=5)throw std::runtime_error("Incomplete upgrade categories");
    Cache::StoreItemSnapshot(db,Cache::ItemDataset::Details,rows,stamp*1000000);
    db.Exec("DELETE FROM public_upgrade_items; DELETE FROM public_item_index;");
    auto item=db.Prepare("INSERT INTO public_upgrade_items VALUES(?,?)");
    for(const auto& row:rows){item.Bind(1,Id(row.at("id")));item.Bind(2,Kind(row));item.Execute();item.Reset();}
    auto index=db.Prepare("INSERT INTO public_item_index VALUES(?)");
    for(auto id:ids){index.Bind(1,id);index.Execute();index.Reset();}
    Mark(db,"public_upgrades",stamp);
}
struct Objective {std::string title,track;int64_t acclaim=0;};
inline std::map<uint32_t,Objective> ParseObjectives(const Json& rows) {
    if(!rows.is_array()||rows.empty())throw std::runtime_error("Empty Vault catalog");
    std::map<uint32_t,Objective> out;
    for(const auto& row:rows) {
        Objective o{row.at("title").get<std::string>(),row.at("track").get<std::string>(),row.at("acclaim").get<int64_t>()};
        if(o.title.empty()||!row.at("acclaim").is_number_integer()||o.acclaim<0||
           !out.emplace(Id(row.at("id")),std::move(o)).second)throw std::runtime_error("Invalid Vault definition");
    }
    return out;
}
inline void StoreObjectives(Database::Connection& db,const std::map<uint32_t,Objective>& rows,int64_t stamp) {
    if(rows.empty()||stamp<Fresh(db,"public_vault"))throw std::runtime_error("Invalid Vault snapshot");
    db.Exec("DELETE FROM public_vault_objectives");
    auto q=db.Prepare("INSERT INTO public_vault_objectives VALUES(?,?,?,?)");
    for(const auto& [id,o]:rows){q.Bind(1,id);q.Bind(2,o.title);q.Bind(3,o.track);q.Bind(4,o.acclaim);q.Execute();q.Reset();}
    Mark(db,"public_vault",stamp);
}
struct Entry {std::string name,bonus,kind,rarity;uint32_t item_id=0;};
inline std::string Summary(const Json& details,const std::string& description) {
    std::string out;
    auto append=[&](const std::string& s){if(s.empty())return;if(!out.empty())out+="; ";out+=s;};
    if(details.is_object()) {
        if(details.contains("bonuses")&&details.at("bonuses").is_array())
            for(const auto& v:details.at("bonuses"))if(v.is_string())append(v.get<std::string>());
        if(details.contains("infix_upgrade")&&details.at("infix_upgrade").is_object()) {
            const auto& infix=details.at("infix_upgrade");
            if(infix.contains("buff")&&infix.at("buff").is_object())append(infix.at("buff").value("description",std::string{}));
            if(infix.contains("attributes")&&infix.at("attributes").is_array())
                for(const auto& a:infix.at("attributes"))if(a.contains("modifier")&&a.at("modifier").is_number_integer())
                    append("+"+std::to_string(a.at("modifier").get<int>())+" "+a.value("attribute",std::string{}));
        }
    }
    if(out.empty())out=description;
    return out;
}
struct Pet {uint32_t id=0;std::string name,icon;};
struct Legend {uint8_t code=0;std::string apiId,name,icon;uint32_t swap=0;};
inline std::vector<Pet> ParsePets(const Json& rows) {
    if(!rows.is_array()||rows.empty())throw std::runtime_error("Empty pet catalog");
    std::set<uint32_t> seen;std::vector<Pet> out;
    for(const auto& r:rows){
        auto id=Id(r.at("id"));auto name=r.at("name").get<std::string>();
        if(id>255||name.empty()||!seen.insert(id).second)throw std::runtime_error("Invalid pet");
        out.push_back({id,name,r.value("icon",std::string{})});
    }
    return out;
}
inline std::vector<Legend> ParseLegends(const Json& rows,const Json& skills) {
    if(!rows.is_array()||rows.empty()||!skills.is_array())throw std::runtime_error("Empty legend catalog");
    std::map<uint32_t,std::pair<std::string,std::string>> names;
    for(const auto& s:skills){
        auto id=Id(s.at("id"));auto name=s.at("name").get<std::string>();
        if(name.empty()||!names.emplace(id,std::make_pair(name,s.value("icon",std::string{}))).second)
            throw std::runtime_error("Invalid legend skill");
    }
    std::set<uint32_t> codes;std::set<std::string> ids;std::vector<Legend> out;
    for(const auto& r:rows){
        auto code=Id(r.at("code"));auto id=r.at("id").get<std::string>();auto swap=Id(r.at("swap"));
        auto it=names.find(swap);
        if(code>255||id.empty()||!codes.insert(code).second||!ids.insert(id).second||it==names.end())
            throw std::runtime_error("Invalid legend identity");
        out.push_back({static_cast<uint8_t>(code),id,it->second.first,it->second.second,swap});
    }
    return out;
}
inline void StorePets(Database::Connection& db,const std::vector<Pet>& rows,int64_t stamp) {
    if(rows.empty()||stamp<Fresh(db,"public_pets")||
       static_cast<int64_t>(rows.size())<db.Scalar("SELECT count(*) FROM public_pets")*9/10)
        throw std::runtime_error("Incomplete pet catalog");
    db.Exec("DELETE FROM public_pets");auto q=db.Prepare("INSERT INTO public_pets VALUES(?,?,?)");
    for(const auto& r:rows){q.Bind(1,r.id);q.Bind(2,r.name);q.Bind(3,r.icon);q.Execute();q.Reset();}
    Mark(db,"public_pets",stamp);
}
inline void StoreLegends(Database::Connection& db,const std::vector<Legend>& rows,int64_t stamp) {
    if(rows.empty()||stamp<Fresh(db,"public_legends")||
       static_cast<int64_t>(rows.size())<db.Scalar("SELECT count(*) FROM public_legends"))
        throw std::runtime_error("Incomplete legend catalog");
    db.Exec("DELETE FROM public_legends");auto q=db.Prepare("INSERT INTO public_legends VALUES(?,?,?,?,?)");
    for(const auto& r:rows){q.Bind(1,static_cast<int>(r.code));q.Bind(2,r.apiId);q.Bind(3,r.swap);q.Bind(4,r.name);q.Bind(5,r.icon);q.Execute();q.Reset();}
    Mark(db,"public_legends",stamp);
}
inline uint8_t LegendCode(const std::vector<Legend>& rows,const std::string& id) {
    for(const auto& row:rows)if(row.apiId==id)return row.code;
    // The original known template protocol codes are preserved offline.
    // Do not derive arbitrary new codes from an ID suffix.
    if(id.size()==7&&id.compare(0,6,"Legend")==0&&id[6]>='1'&&id[6]<='8')
        return static_cast<uint8_t>(id[6]-'0');
    return 0;
}
inline uint8_t CanonicalLegendCode(uint8_t code) {
    // Historical experimental picker wrote 13..19; preserve its meaning on
    // explicit display/edit/export, never rewrite durable library records.
    return code>=13&&code<=19?static_cast<uint8_t>(code-12):code;
}
struct Snapshot {
    int64_t upgrades=0,upgradeAttempt=0,vault=0,vaultAttempt=0;
    std::set<uint32_t> index,upgradeIds;
    std::vector<Entry> entries;
    std::map<uint32_t,Objective> objectives;
    std::vector<Pet> pets;
    std::vector<Legend> legends;
    std::map<std::string,std::pair<int64_t,int64_t>> freshness;
};
inline Snapshot Read(Database::Connection& db) {
    Snapshot out;
    out.upgrades=Fresh(db,"public_upgrades");out.upgradeAttempt=Fresh(db,"public_upgrades_attempt");
    out.vault=Fresh(db,"public_vault");out.vaultAttempt=Fresh(db,"public_vault_attempt");
    auto ids=db.Prepare("SELECT id FROM public_item_index");while(ids.Step())out.index.insert(static_cast<uint32_t>(ids.Integer(0)));
    auto items=db.Prepare("SELECT i.id,i.name,i.description,i.details,u.kind,i.rarity FROM public_upgrade_items u JOIN reference_items i ON i.id=u.id ORDER BY i.name,i.id");
    while(items.Step()){
        const auto id=static_cast<uint32_t>(items.Integer(0));out.upgradeIds.insert(id);
        auto details=items.IsNull(3)?Json::object():Json::parse(items.Text(3));
        out.entries.push_back({items.Text(1),Summary(details,items.Text(2)),items.Text(4),items.Text(5),id});
    }
    auto q=db.Prepare("SELECT id,title,track,acclaim FROM public_vault_objectives");
    while(q.Step())out.objectives.emplace(static_cast<uint32_t>(q.Integer(0)),Objective{q.Text(1),q.Text(2),q.Integer(3)});
    auto pets=db.Prepare("SELECT id,name,icon FROM public_pets ORDER BY name,id");
    while(pets.Step())out.pets.push_back({static_cast<uint32_t>(pets.Integer(0)),pets.Text(1),pets.Text(2)});
    auto legends=db.Prepare("SELECT code,api_id,name,icon,swap_skill FROM public_legends ORDER BY code");
    while(legends.Step())out.legends.push_back({static_cast<uint8_t>(legends.Integer(0)),legends.Text(1),legends.Text(2),legends.Text(3),static_cast<uint32_t>(legends.Integer(4))});
    for(const char* name:{"public_pets","public_legends","public_stats","public_dyes"})
        out.freshness.emplace(name,std::make_pair(Fresh(db,name),Fresh(db,std::string(name)+"_attempt")));
    return out;
}
inline bool DatasetDue(const Snapshot& s,const std::string& name,int64_t now,bool manual) {
    auto it=s.freshness.find(name);
    return it==s.freshness.end()||Due(it->second.first,it->second.second,now,manual);
}
} // namespace AlterEgo::PublicCatalog
