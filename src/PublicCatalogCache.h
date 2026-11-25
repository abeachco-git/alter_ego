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
struct Snapshot {
    int64_t upgrades=0,upgradeAttempt=0,vault=0,vaultAttempt=0;
    std::set<uint32_t> index,upgradeIds;
    std::vector<Entry> entries;
    std::map<uint32_t,Objective> objectives;
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
    return out;
}
} // namespace AlterEgo::PublicCatalog
