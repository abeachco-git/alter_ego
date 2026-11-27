#pragma once
#include "ItemMetadataCache.h"
#include <algorithm>
#include <chrono>

namespace AlterEgo::PublicCatalog {
using Json = nlohmann::json;
inline constexpr int64_t Week = 7 * 86400, Month = 30 * 86400, Retry = 86400;
// Shared successful-observation lifetime for balance-sensitive public metadata.
inline constexpr int64_t BalanceMetadataAge = Week;
inline int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
inline bool Due(int64_t success,int64_t attempt,int64_t now,bool manual,int64_t interval=Week) {
    return manual || ((!success || now-success>=interval) && (!attempt || now-attempt>=Retry));
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
inline void MarkAttempt(Database::Connection& db,const std::string& dataset,int64_t stamp) {
    auto q=db.Prepare("INSERT INTO reference_freshness VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET saved_at=MAX(reference_freshness.saved_at,excluded.saved_at)");
    q.Bind(1,dataset);q.Bind(2,stamp);q.Execute();
}
inline void Mark(Database::Connection& db,const std::string& dataset,int64_t stamp) {
    auto q=db.Prepare("INSERT INTO reference_freshness VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET saved_at=excluded.saved_at");
    q.Bind(1,dataset);q.Bind(2,stamp);q.Execute();
}
inline std::string StoredKind(Database::Connection& db,uint32_t id) {
    auto q=db.Prepare("SELECT type,details FROM reference_items WHERE id=? AND complete=1");
    q.Bind(1,id);
    if(!q.Step())throw std::runtime_error("Missing durable item observation");
    Json row={{"type",q.Text(0)}};
    if(!q.IsNull(1))row["details"]=Json::parse(q.Text(1));
    return Kind(row);
}
inline void StoreUpgrades(Database::Connection& db,const std::set<uint32_t>& ids,const Json& rows,int64_t stamp,
                          const Cache::ItemRequestStamps* requestStamps=nullptr,Json* accepted=nullptr) {
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
    Cache::StoreItemSnapshot(db,Cache::ItemDataset::Details,rows,requestStamps?Cache::ReferenceSnapshotTime():stamp*1000000,requestStamps,accepted);
    db.Exec("DELETE FROM public_upgrade_items; DELETE FROM public_item_index;");
    auto item=db.Prepare("INSERT INTO public_upgrade_items VALUES(?,?)");
    for(const auto& row:rows){const auto id=Id(row.at("id"));const auto kind=StoredKind(db,id);if(kind.empty())continue;item.Bind(1,id);item.Bind(2,kind);item.Execute();item.Reset();}
    if(db.Scalar("SELECT count(DISTINCT kind) FROM public_upgrade_items")!=5)throw std::runtime_error("Incomplete durable upgrade categories");
    auto index=db.Prepare("INSERT INTO public_item_index VALUES(?)");
    for(auto id:ids){index.Bind(1,id);index.Execute();index.Reset();}
    Mark(db,"public_upgrades",stamp);
    Mark(db,"public_item_discovery",stamp);
}
inline std::set<uint32_t> PlanItems(const std::set<uint32_t>& index,const std::set<uint32_t>& previous,
                                    const std::set<uint32_t>& upgrades,bool discover,bool refresh) {
    std::set<uint32_t> result;
    for(auto id:index)if((discover&&!previous.count(id))||(refresh&&upgrades.count(id)))result.insert(id);
    return result;
}
inline void StoreIncrementalItems(Database::Connection& db,const std::set<uint32_t>& index,
                                  const Json& rows,bool discover,bool refresh,int64_t stamp,
                                  const Cache::ItemRequestStamps* requestStamps=nullptr,Json* accepted=nullptr) {
    std::set<uint32_t> previous,known;
    auto old=db.Prepare("SELECT id FROM public_item_index");
    while(old.Step())previous.insert(static_cast<uint32_t>(old.Integer(0)));
    auto up=db.Prepare("SELECT id FROM public_upgrade_items");
    while(up.Step())known.insert(static_cast<uint32_t>(up.Integer(0)));
    if(previous.empty()||index.empty()||index.size()<previous.size()*9/10)
        throw std::runtime_error("Incomplete public item index");
    if((discover&&stamp<Fresh(db,"public_item_discovery"))||(refresh&&stamp<Fresh(db,"public_upgrades")))
        throw std::runtime_error("Older item refresh");
    ValidateBatch(rows,PlanItems(index,previous,known,discover,refresh));
    if(!rows.empty())Cache::StoreItemSnapshot(db,Cache::ItemDataset::Details,rows,Cache::ReferenceSnapshotTime(),requestStamps,accepted);
    auto put=db.Prepare("INSERT INTO public_upgrade_items VALUES(?,?) ON CONFLICT(id) DO UPDATE SET kind=excluded.kind");
    auto remove=db.Prepare("DELETE FROM public_upgrade_items WHERE id=?");
    for(const auto& row:rows){
        const auto id=Id(row.at("id"));const auto kind=StoredKind(db,id);
        if(kind.empty()){remove.Bind(1,id);remove.Execute();remove.Reset();}
        else{put.Bind(1,id);put.Bind(2,kind);put.Execute();put.Reset();}
    }
    if(discover){
        db.Exec("DELETE FROM public_item_index");
        auto add=db.Prepare("INSERT INTO public_item_index VALUES(?)");
        for(auto id:index){add.Bind(1,id);add.Execute();add.Reset();}
        db.Exec("DELETE FROM public_upgrade_items WHERE id NOT IN (SELECT id FROM public_item_index)");
        Mark(db,"public_item_discovery",stamp);
    }
    if(db.Scalar("SELECT count(DISTINCT kind) FROM public_upgrade_items")!=5)
        throw std::runtime_error("Incomplete upgrade categories");
    if(refresh)Mark(db,"public_upgrades",stamp);
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
struct Pet {
    uint32_t id=0;std::string name,icon;
    std::optional<std::string> description{};
    std::optional<std::vector<uint32_t>> skills{};
};
struct Legend {
    uint8_t code=0;std::string apiId,name,icon;uint32_t swap=0;
    std::optional<uint32_t> heal{},elite{};
    std::optional<std::vector<uint32_t>> utilities{};
};
// API-defined order, not a character's recorded utility order. Unknown and
// explicit empty values remain distinct even when read from an older cache.
inline std::optional<std::vector<uint32_t>> CompanionSkillList(const Json& row,const char* field,bool objects){
    const auto it=row.find(field);
    if(it==row.end()||it->is_null())return std::nullopt;
    if(!it->is_array()||it->size()>64)throw std::runtime_error("Invalid companion skill array");
    std::vector<uint32_t> ids;
    for(const auto& v:*it)ids.push_back(Id(objects?v.at("id"):v));
    return ids;
}
inline std::optional<uint32_t> CompanionSkill(const Json& row,const char* field){
    const auto it=row.find(field);
    if(it==row.end())return std::nullopt;
    if(it->is_null())return uint32_t(0);
    if(it->is_number_integer()&&*it==0)return uint32_t(0);
    return Id(*it);
}
enum class CompanionSkillRole { Pet, Invocation, Heal, Utility, Elite };
struct CompanionSkillReference { uint32_t id=0; CompanionSkillRole role=CompanionSkillRole::Pet; };
inline const char* RoleLabel(CompanionSkillRole role){
    switch(role){
    case CompanionSkillRole::Invocation:return "Invocation";
    case CompanionSkillRole::Heal:return "Heal";
    case CompanionSkillRole::Utility:return "Utility";
    case CompanionSkillRole::Elite:return "Elite";
    default:return "Pet skill";
    }
}
inline std::vector<CompanionSkillReference> CompanionSkills(const Pet& pet){
    std::vector<CompanionSkillReference> out;
    if(pet.skills)for(auto id:*pet.skills)if(id)out.push_back({id,CompanionSkillRole::Pet});
    return out;
}
inline std::vector<CompanionSkillReference> CompanionSkills(const Legend& legend,bool invocation=true){
    std::vector<CompanionSkillReference> out;
    if(invocation&&legend.swap)out.push_back({legend.swap,CompanionSkillRole::Invocation});
    if(legend.heal&&*legend.heal)out.push_back({*legend.heal,CompanionSkillRole::Heal});
    if(legend.utilities)for(auto id:*legend.utilities)if(id)out.push_back({id,CompanionSkillRole::Utility});
    if(legend.elite&&*legend.elite)out.push_back({*legend.elite,CompanionSkillRole::Elite});
    return out;
}
inline std::vector<uint32_t> CompanionSkillIds(const std::vector<CompanionSkillReference>& refs){
    std::vector<uint32_t> ids;for(const auto& ref:refs)ids.push_back(ref.id);return ids;
}
inline std::vector<uint32_t> AssociatedSkills(const Pet& pet){return CompanionSkillIds(CompanionSkills(pet));}
inline std::vector<uint32_t> AssociatedSkills(const Legend& legend){return CompanionSkillIds(CompanionSkills(legend,false));}
// Skill records share the reference request-start watermark (microseconds).
// An ordinary legend refresh can reuse complete recent skill observations;
// manual refresh or stale/missing metadata requires a newer observation.
inline bool ReferenceNeedsRefresh(int64_t stamp,bool complete,int64_t now,bool manual,bool automatic){
    const auto seconds=stamp/1000000;
    return manual||!complete||(automatic&&(seconds<=0||seconds>now||now-seconds>=BalanceMetadataAge));
}
inline bool LegendSkillNeedsRefresh(int64_t stamp,bool complete,int64_t now,bool manual){
    return ReferenceNeedsRefresh(stamp,complete,now,manual,true);
}
inline bool LegendSkillCovered(int64_t baseline,int64_t observed,bool complete){
    return complete&&observed>baseline&&observed>0;
}
inline std::vector<Pet> ParsePets(const Json& rows) {
    if(!rows.is_array()||rows.empty())throw std::runtime_error("Empty pet catalog");
    std::set<uint32_t> seen;std::vector<Pet> out;
    for(const auto& r:rows){
        auto id=Id(r.at("id"));auto name=r.at("name").get<std::string>();
        if(id>255||name.empty()||!seen.insert(id).second)throw std::runtime_error("Invalid pet");
        Pet pet{id,name,r.value("icon",std::string{})};
        const auto description=r.find("description");
        if(description!=r.end()&&!description->is_null()){
            if(!description->is_string()||description->get_ref<const std::string&>().size()>8192)
                throw std::runtime_error("Invalid pet description");
            pet.description=description->get<std::string>();
        }
        pet.skills=CompanionSkillList(r,"skills",true);
        out.push_back(std::move(pet));
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
        Legend legend{static_cast<uint8_t>(code),id,it->second.first,it->second.second,swap};
        legend.heal=CompanionSkill(r,"heal");legend.elite=CompanionSkill(r,"elite");
        legend.utilities=CompanionSkillList(r,"utilities",false);
        out.push_back(std::move(legend));
    }
    return out;
}
inline void StorePets(Database::Connection& db,const std::vector<Pet>& rows,int64_t stamp) {
    if(rows.empty()||stamp<Fresh(db,"public_pets")||
       static_cast<int64_t>(rows.size())<db.Scalar("SELECT count(*) FROM public_pets")*9/10)
        throw std::runtime_error("Incomplete pet catalog");
    db.Exec("DELETE FROM public_pets");
    auto q=db.Prepare("INSERT INTO public_pets(id,name,icon,description,skills_known) VALUES(?,?,?,?,?)");
    auto skill=db.Prepare("INSERT INTO public_pet_skills VALUES(?,?,?)");
    for(const auto& r:rows){
        q.Bind(1,r.id);q.Bind(2,r.name);q.Bind(3,r.icon);
        if(r.description)q.Bind(4,*r.description);else q.BindNull(4);
        q.Bind(5,r.skills.has_value());q.Execute();q.Reset();
        if(r.skills)for(size_t i=0;i<r.skills->size();++i){
            skill.Bind(1,r.id);skill.Bind(2,static_cast<int64_t>(i));skill.Bind(3,(*r.skills)[i]);skill.Execute();skill.Reset();
        }
    }
    Mark(db,"public_pets",stamp);
}
inline void StoreLegends(Database::Connection& db,const std::vector<Legend>& rows,int64_t stamp) {
    if(rows.empty()||stamp<Fresh(db,"public_legends")||
       static_cast<int64_t>(rows.size())<db.Scalar("SELECT count(*) FROM public_legends"))
        throw std::runtime_error("Incomplete legend catalog");
    db.Exec("DELETE FROM public_legends");
    auto q=db.Prepare("INSERT INTO public_legends(code,api_id,swap_skill,name,icon,heal,elite,utilities_known) VALUES(?,?,?,?,?,?,?,?)");
    auto utility=db.Prepare("INSERT INTO public_legend_utilities VALUES(?,?,?)");
    for(const auto& r:rows){
        q.Bind(1,static_cast<int>(r.code));q.Bind(2,r.apiId);q.Bind(3,r.swap);q.Bind(4,r.name);q.Bind(5,r.icon);
        if(r.heal)q.Bind(6,*r.heal);else q.BindNull(6);
        if(r.elite)q.Bind(7,*r.elite);else q.BindNull(7);
        q.Bind(8,r.utilities.has_value());q.Execute();q.Reset();
        if(r.utilities)for(size_t i=0;i<r.utilities->size();++i){
            utility.Bind(1,static_cast<int>(r.code));utility.Bind(2,static_cast<int64_t>(i));
            utility.Bind(3,(*r.utilities)[i]);utility.Execute();utility.Reset();
        }
    }
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
// Resolve immutable catalog identities, never use display names as IDs.
inline const Pet* FindPet(const std::vector<Pet>& rows,uint32_t id){
    for(const auto& row:rows)if(row.id==id)return &row;
    return nullptr;
}
inline const Legend* FindLegend(const std::vector<Legend>& rows,uint32_t code){
    if(code>255||!code)return nullptr;
    const auto canonical=CanonicalLegendCode(static_cast<uint8_t>(code));
    for(const auto& row:rows)if(row.code==canonical)return &row;
    return nullptr;
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
    auto pets=db.Prepare("SELECT id,name,icon,description,skills_known FROM public_pets ORDER BY name,id");
    std::map<uint32_t,std::vector<uint32_t>> petSkills,legendUtilities;
    auto ps=db.Prepare("SELECT pet_id,skill_id FROM public_pet_skills ORDER BY pet_id,ordinal");
    while(ps.Step())petSkills[static_cast<uint32_t>(ps.Integer(0))].push_back(static_cast<uint32_t>(ps.Integer(1)));
    auto ls=db.Prepare("SELECT code,skill_id FROM public_legend_utilities ORDER BY code,ordinal");
    while(ls.Step())legendUtilities[static_cast<uint32_t>(ls.Integer(0))].push_back(static_cast<uint32_t>(ls.Integer(1)));
    while(pets.Step()){
        Pet pet{static_cast<uint32_t>(pets.Integer(0)),pets.Text(1),pets.Text(2)};
        if(!pets.IsNull(3))pet.description=pets.Text(3);
        if(pets.Integer(4))pet.skills=std::move(petSkills[pet.id]);
        out.pets.push_back(std::move(pet));
    }
    auto legends=db.Prepare("SELECT code,api_id,name,icon,swap_skill,heal,elite,utilities_known FROM public_legends ORDER BY code");
    while(legends.Step()){
        Legend legend{static_cast<uint8_t>(legends.Integer(0)),legends.Text(1),legends.Text(2),legends.Text(3),static_cast<uint32_t>(legends.Integer(4))};
        if(!legends.IsNull(5))legend.heal=static_cast<uint32_t>(legends.Integer(5));
        if(!legends.IsNull(6))legend.elite=static_cast<uint32_t>(legends.Integer(6));
        if(legends.Integer(7))legend.utilities=std::move(legendUtilities[legend.code]);
        out.legends.push_back(std::move(legend));
    }
    for(const char* name:{"public_pets","public_legends","public_stats","public_dyes","public_item_discovery","public_skins"})
        out.freshness.emplace(name,std::make_pair(Fresh(db,name),Fresh(db,std::string(name)+"_attempt")));
    return out;
}
inline bool DatasetDue(const Snapshot& s,const std::string& name,int64_t now,bool manual) {
    auto it=s.freshness.find(name);
    const auto interval=name=="public_dyes"?Month:
        (name=="public_pets"||name=="public_legends"||name=="public_stats")?BalanceMetadataAge:Week;
    return it==s.freshness.end()||Due(it->second.first,it->second.second,now,manual,interval);
}
} // namespace AlterEgo::PublicCatalog
