#pragma once
#include "ReferenceMetadataCache.h"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

namespace AlterEgo::Cache {
enum class SkinProjection { Catalog, Fashion };
using SkinJson=nlohmann::json;
using SkinRequestStamps=std::map<uint32_t,int64_t>;
// Serialize durable row merges and both live projections; never held during HTTP.
inline std::mutex& SkinPublicationMutex(){static std::mutex m;return m;}
inline SkinJson SkinObservation(const SkinJson& raw){
    const auto id=ReferenceId(raw.at("id"));
    const auto name=raw.at("name").get<std::string>();
    if(!id||name.empty())throw std::runtime_error("Invalid skin observation");
    SkinJson row={{"id",id},{"name",name},{"type",raw.at("type").get<std::string>()},
                  {"rarity",raw.value("rarity",std::string{})},{"icon",raw.value("icon",std::string{})}};
    if(row.at("type").get<std::string>().empty())throw std::runtime_error("Missing skin type");
    for(const char* key:{"flags","restrictions"}){
        if(raw.contains(key))row[key]=raw.at(key).get<std::vector<std::string>>();
        else row[key]=SkinJson::array();
    }
    if(raw.contains("details")&&raw.at("details").is_object()){
        const auto& d=raw.at("details");
        row["subtype"]=d.value("type",std::string{});
        row["weight_class"]=d.value("weight_class",std::string{});
        if(d.contains("dye_slots")&&d.at("dye_slots").is_object()&&d.at("dye_slots").contains("default")){
            const auto& slots=d.at("dye_slots").at("default");
            if(!slots.is_array()||slots.size()>4)throw std::runtime_error("Invalid dye slots");
            int count=0;auto materials=SkinJson::array();
            for(const auto& slot:slots){
                if(slot.is_null())materials.push_back("");
                else {++count;materials.push_back(slot.value("material",std::string("cloth")));}
            }
            row["dye_slot_count"]=count;row["dye_slot_materials"]=std::move(materials);
        }
    }
    return row;
}
// Return canonical values, including fields owned by the other projection.
inline SkinJson CommittedSkin(Database::Connection& db,uint32_t id){
    auto q=db.Prepare("SELECT name,type,subtype,weight_class,rarity,icon,dye_slot_count,catalog,fashion FROM skins WHERE id=?");
    q.Bind(1,id);if(!q.Step())throw std::runtime_error("Missing committed skin");
    SkinJson row={{"id",id},{"name",q.Text(0)},{"type",q.Text(1)},{"subtype",q.Text(2)},
        {"weight_class",q.Text(3)},{"rarity",q.Text(4)},{"icon",q.Text(5)},
        {"catalog",q.Integer(7)!=0},{"fashion",q.Integer(8)!=0}};
    if(!q.IsNull(6))row["dye_slot_count"]=q.Integer(6);
    auto children=[&](const char* sql,const char* key){
        auto c=db.Prepare(sql);c.Bind(1,id);row[key]=SkinJson::array();
        while(c.Step())row[key].push_back(c.Text(0));
    };
    if(row.at("catalog").get<bool>()){
        children("SELECT value FROM skin_flags WHERE skin_id=? ORDER BY ordinal","flags");
        children("SELECT value FROM skin_restrictions WHERE skin_id=? ORDER BY ordinal","restrictions");
    }
    if(row.contains("dye_slot_count"))
        children("SELECT value FROM skin_dye_materials WHERE skin_id=? ORDER BY ordinal","dye_slot_materials");
    return row;
}
inline std::string SkinDataset(SkinProjection p) {
    return p==SkinProjection::Catalog?"skin_catalog":"skin_fashion";
}
inline std::string RootFromLegacySkinDirectory(const std::string& dir) {
    return std::filesystem::u8path(dir).parent_path().u8string();
}
// Common metadata is stored once. Catalog and Fashion own their distinct
// completeness flags and child fields. Missing values from a narrower
// projection never erase known details from the other projection.
inline bool WriteSkins(const std::string& root,SkinProjection projection,const SkinJson& rows,
                       const SkinRequestStamps* stamps=nullptr,SkinJson* committed=nullptr) {
    if(root.empty() || !rows.is_array() || rows.empty()) return false;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);
        Database::Transaction tx(db);
        const bool catalog=projection==SkinProjection::Catalog;
        auto old=db.Prepare("SELECT common_verified_at,catalog_verified_at,dye_verified_at FROM skins WHERE id=?");
        auto upsert=db.Prepare(R"SQL(
INSERT INTO skins(id,name,type,subtype,weight_class,rarity,icon,dye_slot_count,catalog,fashion,
                  common_verified_at,catalog_verified_at,dye_verified_at)
VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(id) DO UPDATE SET
 name=CASE WHEN excluded.name<>'' AND (excluded.common_verified_at>skins.common_verified_at OR skins.name='' OR skins.common_verified_at=0) THEN excluded.name ELSE skins.name END,
 type=CASE WHEN excluded.type<>'' AND (excluded.common_verified_at>skins.common_verified_at OR skins.type='' OR skins.common_verified_at=0) THEN excluded.type ELSE skins.type END,
 subtype=CASE WHEN excluded.subtype<>'' AND (excluded.common_verified_at>skins.common_verified_at OR skins.subtype='' OR skins.common_verified_at=0) THEN excluded.subtype ELSE skins.subtype END,
 weight_class=CASE WHEN excluded.weight_class<>'' AND (excluded.common_verified_at>skins.common_verified_at OR skins.weight_class='' OR skins.common_verified_at=0) THEN excluded.weight_class ELSE skins.weight_class END,
 rarity=CASE WHEN excluded.rarity<>'' AND (excluded.common_verified_at>skins.common_verified_at OR skins.rarity='' OR skins.common_verified_at=0) THEN excluded.rarity ELSE skins.rarity END,
 icon=CASE WHEN excluded.icon<>'' AND (excluded.common_verified_at>skins.common_verified_at OR skins.icon='' OR skins.common_verified_at=0) THEN excluded.icon ELSE skins.icon END,
 dye_slot_count=CASE WHEN excluded.dye_slot_count IS NOT NULL AND (excluded.dye_verified_at>skins.dye_verified_at OR skins.dye_slot_count IS NULL OR skins.dye_verified_at=0) THEN excluded.dye_slot_count ELSE skins.dye_slot_count END,
 catalog=MAX(skins.catalog,excluded.catalog),
 fashion=MAX(skins.fashion,excluded.fashion),
 common_verified_at=MAX(skins.common_verified_at,excluded.common_verified_at),
 catalog_verified_at=MAX(skins.catalog_verified_at,excluded.catalog_verified_at),
 dye_verified_at=MAX(skins.dye_verified_at,excluded.dye_verified_at)
)SQL");
        auto flagsDelete=db.Prepare("DELETE FROM skin_flags WHERE skin_id=?");
        auto flagsInsert=db.Prepare("INSERT INTO skin_flags VALUES(?,?,?)");
        auto restrictionsDelete=db.Prepare("DELETE FROM skin_restrictions WHERE skin_id=?");
        auto restrictionsInsert=db.Prepare("INSERT INTO skin_restrictions VALUES(?,?,?)");
        auto dyesDelete=db.Prepare("DELETE FROM skin_dye_materials WHERE skin_id=?");
        auto dyesInsert=db.Prepare("INSERT INTO skin_dye_materials VALUES(?,?,?)");
        auto children=[](uint32_t id,const SkinJson& row,const char* key,
                         Database::Statement& del,Database::Statement& put) {
            if(!row.contains(key)) return;
            const auto& values=row.at(key);
            if(!values.is_array()) throw std::runtime_error("Invalid skin child list");
            del.Bind(1,id);del.Execute();del.Reset();
            int n=0;
            for(const auto& value:values) {
                put.Bind(1,id);put.Bind(2,n++);put.Bind(3,value.get<std::string>());
                put.Execute();put.Reset();
            }
        };
        auto merged=SkinJson::array();std::set<uint32_t> seen;
        for(const auto& r:rows) {
            const auto id=ReferenceId(r.at("id"));
            if(!id||!seen.insert(id).second)throw std::runtime_error("Invalid or duplicate skin identity");
            const int64_t stamp=stamps?stamps->at(id):0;
            if(stamps&&stamp<=0)throw std::runtime_error("Invalid skin request stamp");
            old.Bind(1,id);
            const bool exists=old.Step();
            const int64_t catalogStamp=exists?old.Integer(1):0,dyeStamp=exists?old.Integer(2):0;
            if(exists)(void)old.Step();
            old.Reset();
            const bool catalogFields=catalog&&(!exists||catalogStamp==0||stamp>catalogStamp);
            const bool knownDyes=r.contains("dye_slot_count")&&r.at("dye_slot_count").get<int>()>=0;
            const bool dyeFields=knownDyes&&(!exists||dyeStamp==0||stamp>dyeStamp);
            upsert.Bind(1,id);upsert.Bind(2,r.value("name",""));upsert.Bind(3,r.value("type",""));
            upsert.Bind(4,r.value("subtype",""));upsert.Bind(5,r.value("weight_class",""));
            upsert.Bind(6,r.value("rarity",""));upsert.Bind(7,r.value("icon",""));
            const int dyes=r.value("dye_slot_count",-1);
            if(dyes>=0&&dyes<=4&&dyeFields)upsert.Bind(8,dyes);else upsert.BindNull(8);
            if(dyes>4||dyes< -1)throw std::runtime_error("Invalid skin dye count");
            upsert.Bind(9,catalog);upsert.Bind(10,!catalog);
            upsert.Bind(11,stamp);upsert.Bind(12,catalogFields?stamp:0);upsert.Bind(13,dyeFields?stamp:0);
            upsert.Execute();upsert.Reset();
            if(catalogFields) {
                children(id,r,"flags",flagsDelete,flagsInsert);
                children(id,r,"restrictions",restrictionsDelete,restrictionsInsert);
            }
            if(dyeFields)children(id,r,"dye_slot_materials",dyesDelete,dyesInsert);
            if(committed)merged.push_back(CommittedSkin(db,id));
        }
        auto fresh=db.Prepare("INSERT INTO reference_freshness VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET saved_at=excluded.saved_at");
        fresh.Bind(1,SkinDataset(projection));
        fresh.Bind(2,std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        fresh.Execute();tx.Commit();if(committed)*committed=std::move(merged);return true;
    } catch(...) { return false; }
}
inline std::optional<SkinJson> ReadSkins(const std::string& root,SkinProjection projection,
                                        const std::filesystem::path& legacy) {
    if(root.empty()) return std::nullopt;
    {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        if(std::filesystem::exists(path)) {
            Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
            Initialize(db);
            Database::Transaction tx(db);
            auto fresh=db.Prepare("SELECT saved_at FROM reference_freshness WHERE dataset=?");
            fresh.Bind(1,SkinDataset(projection));
            if(fresh.Step()) {
                const bool catalog=projection==SkinProjection::Catalog;
                auto q=db.Prepare(catalog?
                    "SELECT id,name,type,subtype,weight_class,rarity,icon,dye_slot_count FROM skins WHERE catalog=1":
                    "SELECT id,name,type,subtype,weight_class,rarity,icon,dye_slot_count FROM skins WHERE fashion=1");
                SkinJson out=SkinJson::array();
                std::map<int64_t,size_t> indexes;
                while(q.Step()) {
                    indexes.emplace(q.Integer(0),out.size());
                    SkinJson row={{"id",q.Integer(0)},{"name",q.Text(1)},{"type",q.Text(2)},
                        {"subtype",q.Text(3)},{"weight_class",q.Text(4)},{"rarity",q.Text(5)},{"icon",q.Text(6)}};
                    if(!q.IsNull(7)) row["dye_slot_count"]=q.Integer(7);
                    out.push_back(std::move(row));
                }
                auto children=[&](const char* sql,const char* key) {
                    auto child=db.Prepare(sql);
                    while(child.Step()) {
                        auto it=indexes.find(child.Integer(0));
                        if(it!=indexes.end()) out.at(it->second)[key].push_back(child.Text(1));
                    }
                };
                if(catalog) {
                    children("SELECT skin_id,value FROM skin_flags ORDER BY skin_id,ordinal","flags");
                    children("SELECT skin_id,value FROM skin_restrictions ORDER BY skin_id,ordinal","restrictions");
                } else children("SELECT skin_id,value FROM skin_dye_materials ORDER BY skin_id,ordinal","dye_slot_materials");
                tx.Commit();return out;
            }
        }
    }
    std::ifstream file(legacy);
    if(!file) return std::nullopt;
    auto rows=SkinJson::parse(file);file.close();
    if(!WriteSkins(root,projection,rows)) return std::nullopt;
    std::error_code ec;std::filesystem::remove(legacy,ec);
    return ReadSkins(root,projection,legacy);
}
struct OwnershipSnapshot {
    int64_t fetchedAt=0;
    std::set<uint32_t> ids;
};
inline bool WriteOwnership(const std::string& root,const std::string& account,const OwnershipSnapshot& snapshot) {
    if(root.empty() || account.empty() || snapshot.fetchedAt<0) return false;
    std::lock_guard<std::mutex> lock(Mutex());
    const auto path=Storage::Paths(root).Cache();
    std::filesystem::create_directories(path.parent_path());
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
    Initialize(db);
    Database::Transaction tx(db);
    {
        auto q=db.Prepare("SELECT fetched_at FROM ownership_snapshots WHERE account=?");
        q.Bind(1,account);if(q.Step()&&q.Integer(0)>snapshot.fetchedAt)return false;
    }
    auto del=db.Prepare("DELETE FROM ownership_snapshots WHERE account=?");
    del.Bind(1,account);del.Execute();
    auto put=db.Prepare("INSERT INTO ownership_snapshots VALUES(?,?)");
    put.Bind(1,account);put.Bind(2,snapshot.fetchedAt);put.Execute();
    auto row=db.Prepare("INSERT INTO owned_skins VALUES(?,?)");
    for(auto id:snapshot.ids) {row.Bind(1,account);row.Bind(2,id);row.Execute();row.Reset();}
    tx.Commit();return true;
}
inline std::optional<OwnershipSnapshot> ReadOwnership(const std::string& root,const std::string& account) {
    if(root.empty() || account.empty())return std::nullopt;
    std::lock_guard<std::mutex> lock(Mutex());
    const auto path=Storage::Paths(root).Cache();
    if(!std::filesystem::exists(path))return std::nullopt;
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
    Initialize(db);
    Database::Transaction tx(db);
    OwnershipSnapshot snapshot;
    auto q=db.Prepare("SELECT fetched_at FROM ownership_snapshots WHERE account=?");
    q.Bind(1,account);if(!q.Step())return std::nullopt;
    snapshot.fetchedAt=q.Integer(0);
    auto rows=db.Prepare("SELECT skin_id FROM owned_skins WHERE account=?");
    rows.Bind(1,account);
    while(rows.Step())snapshot.ids.insert(static_cast<uint32_t>(rows.Integer(0)));
    tx.Commit();return snapshot;
}
} // namespace AlterEgo::Cache
