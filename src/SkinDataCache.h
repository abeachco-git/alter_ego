#pragma once
#include "CacheDatabase.h"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

namespace AlterEgo::Cache {
enum class SkinProjection { Catalog, Fashion };
using SkinJson=nlohmann::json;
inline std::string SkinDataset(SkinProjection p) {
    return p==SkinProjection::Catalog?"skin_catalog":"skin_fashion";
}
inline std::string RootFromLegacySkinDirectory(const std::string& dir) {
    return std::filesystem::u8path(dir).parent_path().u8string();
}
// Common metadata is stored once. Catalog and Fashion own their distinct
// completeness flags and child fields. Missing values from a narrower
// projection never erase known details from the other projection.
inline bool WriteSkins(const std::string& root,SkinProjection projection,const SkinJson& rows) {
    if(root.empty() || !rows.is_array() || rows.empty()) return false;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);
        Database::Transaction tx(db);
        const bool catalog=projection==SkinProjection::Catalog;
        auto upsert=db.Prepare(R"SQL(
INSERT INTO skins(id,name,type,subtype,weight_class,rarity,icon,dye_slot_count,catalog,fashion)
VALUES(?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(id) DO UPDATE SET
 name=COALESCE(NULLIF(excluded.name,''),skins.name),
 type=COALESCE(NULLIF(excluded.type,''),skins.type),
 subtype=COALESCE(NULLIF(excluded.subtype,''),skins.subtype),
 weight_class=COALESCE(NULLIF(excluded.weight_class,''),skins.weight_class),
 rarity=COALESCE(NULLIF(excluded.rarity,''),skins.rarity),
 icon=COALESCE(NULLIF(excluded.icon,''),skins.icon),
 dye_slot_count=COALESCE(excluded.dye_slot_count,skins.dye_slot_count),
 catalog=MAX(skins.catalog,excluded.catalog),
 fashion=MAX(skins.fashion,excluded.fashion)
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
        for(const auto& r:rows) {
            const auto id=r.at("id").get<uint32_t>();
            if(!id) throw std::runtime_error("Invalid skin identity");
            upsert.Bind(1,id);upsert.Bind(2,r.value("name",""));upsert.Bind(3,r.value("type",""));
            upsert.Bind(4,r.value("subtype",""));upsert.Bind(5,r.value("weight_class",""));
            upsert.Bind(6,r.value("rarity",""));upsert.Bind(7,r.value("icon",""));
            const int dyes=r.value("dye_slot_count",-1);
            if(dyes>=0) upsert.Bind(8,dyes);else upsert.BindNull(8);
            upsert.Bind(9,catalog);upsert.Bind(10,!catalog);upsert.Execute();upsert.Reset();
            if(catalog) {
                children(id,r,"flags",flagsDelete,flagsInsert);
                children(id,r,"restrictions",restrictionsDelete,restrictionsInsert);
            } else if(dyes>=0) {
                children(id,r,"dye_slot_materials",dyesDelete,dyesInsert);
            }
        }
        auto fresh=db.Prepare("INSERT INTO reference_freshness VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET saved_at=excluded.saved_at");
        fresh.Bind(1,SkinDataset(projection));
        fresh.Bind(2,std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        fresh.Execute();tx.Commit();return true;
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
    return rows;
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
