#pragma once
#include "CacheDatabase.h"
#include "ReferenceMetadataCache.h"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

namespace AlterEgo::Cache {
using Json = nlohmann::json;
enum class AchievementDataset { Names, Definitions, Groups };
inline const char* DatasetName(AchievementDataset d) {
    switch(d) {
    case AchievementDataset::Names: return "ach_name_index";
    case AchievementDataset::Definitions: return "ach_defs";
    case AchievementDataset::Groups: return "ach_groups";
    }
    throw std::runtime_error("Unknown achievement dataset");
}
inline uint32_t CatalogId(const std::string& key) {
    size_t n=0; auto id=std::stoull(key,&n);
    if(n!=key.size() || id>UINT32_MAX) throw std::runtime_error("Invalid achievement id");
    return static_cast<uint32_t>(id);
}
// JSON is only the transient adapter for the established in-memory projections
// and legacy files. Every persisted field uses a typed SQL column.
inline void WriteCatalogRows(Database::Connection& db, AchievementDataset d,const Json& data,bool replaceAll=true) {
    if(!data.is_object()) throw std::runtime_error("Invalid catalog snapshot");
    if(d==AchievementDataset::Names) {
        db.Exec("DELETE FROM achievement_names");
        auto q=db.Prepare("INSERT INTO achievement_names VALUES(?,?)");
        for(const auto& [key,v]:data.items()) {
            const auto id=CatalogId(key);const auto name=v.get<std::string>();
            if(!id || name.empty()) throw std::runtime_error("Invalid achievement name");
            q.Bind(1,id);q.Bind(2,name);q.Execute();q.Reset();
        }
    } else if(d==AchievementDataset::Definitions) {
        if(replaceAll) db.Exec("DELETE FROM achievement_definitions");
        auto remove=db.Prepare("DELETE FROM achievement_definitions WHERE id=?");
        auto q=db.Prepare("INSERT INTO achievement_definitions VALUES(?,?,?,?,?,?,?,?)");
        auto flag=db.Prepare("INSERT INTO achievement_definition_flags VALUES(?,?,?)");
        auto pre=db.Prepare("INSERT INTO achievement_prerequisites VALUES(?,?,?)");
        auto bit=db.Prepare("INSERT INTO achievement_bits VALUES(?,?,?,?,?)");
        for(const auto& [key,v]:data.items()) {
            const auto id=CatalogId(key);
            if(id==0 || v.at("id").get<uint32_t>()!=id) throw std::runtime_error("Invalid definition identity");
            if(!replaceAll){remove.Bind(1,id);remove.Execute();remove.Reset();}
            q.Bind(1,id);q.Bind(2,v.value("name",""));q.Bind(3,v.value("description",""));
            q.Bind(4,v.value("requirement",""));q.Bind(5,v.value("icon_url",""));q.Bind(6,v.value("type",""));
            q.Bind(7,v.value("max_count",0));q.Bind(8,v.value("total_ap",0));q.Execute();q.Reset();
            int ordinal=0;
            for(const auto& f:v.value("flags",Json::array())) {
                flag.Bind(1,id);flag.Bind(2,ordinal++);flag.Bind(3,f.get<std::string>());
                flag.Execute();flag.Reset();
            }
            ordinal=0;
            for(const auto& p:v.value("prerequisites",Json::array())) {
                pre.Bind(1,id);pre.Bind(2,ordinal++);pre.Bind(3,p.get<uint32_t>());
                pre.Execute();pre.Reset();
            }
            ordinal=0;
            for(const auto& b:v.value("bits",Json::array())) {
                bit.Bind(1,id);bit.Bind(2,ordinal++);bit.Bind(3,b.value("type",""));
                bit.Bind(4,b.value("item_id",0u));bit.Bind(5,b.value("text",""));
                bit.Execute();bit.Reset();
            }
        }
    } else if(d==AchievementDataset::Groups) {
        if(!data.at("groups").is_array() || !data.at("categories").is_object())
            throw std::runtime_error("Invalid group/category snapshot");
        db.Exec("DELETE FROM achievement_groups; DELETE FROM achievement_categories;");
        auto group=db.Prepare("INSERT INTO achievement_groups VALUES(?,?,?,?,?)");
        auto cat=db.Prepare("INSERT INTO achievement_categories VALUES(?,?,?,?,?)");
        auto gc=db.Prepare("INSERT INTO achievement_group_categories VALUES(?,?,?)");
        auto member=db.Prepare("INSERT INTO achievement_category_members VALUES(?,?,?)");
        int order=0;
        for(const auto& g:data.at("groups")) {
            const auto id=g.at("id").get<std::string>();
            group.Bind(1,id);group.Bind(2,order++);group.Bind(3,g.value("name",""));
            group.Bind(4,g.value("description",""));group.Bind(5,g.value("order",0));group.Execute();group.Reset();
            int ordinal=0;
            for(const auto& cid:g.value("categories",Json::array())) {
                gc.Bind(1,id);gc.Bind(2,ordinal++);gc.Bind(3,cid.get<uint32_t>());gc.Execute();gc.Reset();
            }
        }
        for(const auto& [key,v]:data.at("categories").items()) {
            const auto id=CatalogId(key);
            if(v.at("id").get<uint32_t>()!=id) throw std::runtime_error("Invalid category identity");
            cat.Bind(1,id);cat.Bind(2,v.value("name",""));cat.Bind(3,v.value("description",""));
            cat.Bind(4,v.value("order",0));cat.Bind(5,v.value("icon_url",""));cat.Execute();cat.Reset();
            int ordinal=0;
            for(const auto& aid:v.value("achievements",Json::array())) {
                member.Bind(1,id);member.Bind(2,ordinal++);member.Bind(3,aid.get<uint32_t>());member.Execute();member.Reset();
            }
        }
    }
}
inline Json ReadCatalogRows(Database::Connection& db,AchievementDataset d,int64_t stamp) {
    Json out=Json::object();
    if(d==AchievementDataset::Names) {
        auto q=db.Prepare("SELECT id,name FROM achievement_names");
        while(q.Step()) out[std::to_string(q.Integer(0))]=q.Text(1);
    } else if(d==AchievementDataset::Definitions) {
        auto q=db.Prepare("SELECT id,name,description,requirement,icon_url,type,max_count,total_ap FROM achievement_definitions");
        while(q.Step()) out[std::to_string(q.Integer(0))]={{"id",q.Integer(0)},{"name",q.Text(1)},
            {"description",q.Text(2)},{"requirement",q.Text(3)},{"icon_url",q.Text(4)},{"type",q.Text(5)},
            {"max_count",q.Integer(6)},{"total_ap",q.Integer(7)},{"flags",Json::array()},
            {"prerequisites",Json::array()},{"bits",Json::array()}};
        auto flags=db.Prepare("SELECT id,flag FROM achievement_definition_flags ORDER BY id,ordinal");
        while(flags.Step()) out.at(std::to_string(flags.Integer(0)))["flags"].push_back(flags.Text(1));
        auto pre=db.Prepare("SELECT id,prerequisite FROM achievement_prerequisites ORDER BY id,ordinal");
        while(pre.Step()) out.at(std::to_string(pre.Integer(0)))["prerequisites"].push_back(pre.Integer(1));
        auto bits=db.Prepare("SELECT id,type,item_id,text FROM achievement_bits ORDER BY id,ordinal");
        while(bits.Step()) out.at(std::to_string(bits.Integer(0)))["bits"].push_back(
            {{"type",bits.Text(1)},{"item_id",bits.Integer(2)},{"text",bits.Text(3)}});
    } else if(d==AchievementDataset::Groups) {
        out["fetch_time"]=stamp;out["groups"]=Json::array();out["categories"]=Json::object();
        std::map<std::string,size_t> groupIndex;
        auto groups=db.Prepare("SELECT id,name,description,display_order FROM achievement_groups ORDER BY ordinal");
        while(groups.Step()) {
            groupIndex.emplace(groups.Text(0),out["groups"].size());
            out["groups"].push_back({{"id",groups.Text(0)},{"name",groups.Text(1)},
                {"description",groups.Text(2)},{"order",groups.Integer(3)},{"categories",Json::array()}});
        }
        auto cats=db.Prepare("SELECT id,name,description,display_order,icon_url FROM achievement_categories");
        while(cats.Step()) out["categories"][std::to_string(cats.Integer(0))]={{"id",cats.Integer(0)},
            {"name",cats.Text(1)},{"description",cats.Text(2)},{"order",cats.Integer(3)},
            {"icon_url",cats.Text(4)},{"achievements",Json::array()}};
        auto gc=db.Prepare("SELECT group_id,category_id FROM achievement_group_categories ORDER BY group_id,ordinal");
        while(gc.Step()) out["groups"].at(groupIndex.at(gc.Text(0)))["categories"].push_back(gc.Integer(1));
        auto members=db.Prepare("SELECT category_id,achievement_id FROM achievement_category_members ORDER BY category_id,ordinal");
        while(members.Step()) out["categories"].at(std::to_string(members.Integer(0)))["achievements"].push_back(members.Integer(1));
    }
    return out;
}
inline bool WriteCatalog(const std::string& root,AchievementDataset d,const Json& data,int64_t nameIndexVerifiedAt=0) {
    if(!data.is_object() || data.empty() || nameIndexVerifiedAt<0 ||
       (nameIndexVerifiedAt>0 && d!=AchievementDataset::Names)) return false;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);
        Database::Transaction tx(db);
        const int64_t stamp=d==AchievementDataset::Groups ? data.value("fetch_time",int64_t(0)) :
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        {
            auto q=db.Prepare("SELECT saved_at FROM reference_freshness WHERE dataset=?");
            q.Bind(1,std::string(DatasetName(d)));
            if(q.Step() && q.Integer(0)>stamp) return false;
        }
        WriteCatalogRows(db,d,data);
        auto q=db.Prepare("INSERT INTO reference_freshness VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET saved_at=excluded.saved_at");
        q.Bind(1,std::string(DatasetName(d)));q.Bind(2,stamp);q.Execute();
        if(nameIndexVerifiedAt>0) {
            auto verified=db.Prepare("INSERT INTO reference_freshness VALUES('ach_name_index_verified',?) ON CONFLICT(dataset) DO UPDATE SET saved_at=max(saved_at,excluded.saved_at)");
            verified.Bind(1,nameIndexVerifiedAt);verified.Execute();
        }
        tx.Commit();return true;
    } catch(...) { return false; }
}
// Canonical per-ID observations reuse the request-start watermark table.
inline constexpr const char* AchievementDefinitionWatermark="achievement-definitions";
inline Json StoreAchievementObservations(Database::Connection& db,const Json& rows,int64_t stamp) {
    if(!rows.is_object()||rows.empty()||stamp<=0) throw std::runtime_error("Invalid achievement observations");
    auto watermark=db.Prepare(RecordWatermarkSql);
    auto names=db.Prepare("INSERT INTO achievement_names VALUES(?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name");
    Json accepted=Json::object();
    for(const auto& [key,row]:rows.items()){
        const auto id=CatalogId(key);
        const auto name=row.at("name").get<std::string>();
        if(!id||row.at("id").get<uint32_t>()!=id||name.empty())throw std::runtime_error("Invalid achievement observation");
        if(ClaimRecordWatermark(watermark,AchievementDefinitionWatermark,id,stamp)){
            accepted[key]=row;
            names.Bind(1,id);names.Bind(2,name);names.Execute();names.Reset();
        }
    }
    if(!accepted.empty()){
        WriteCatalogRows(db,AchievementDataset::Definitions,accepted,false);
        auto freshness=db.Prepare("INSERT INTO reference_freshness VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET saved_at=max(saved_at,excluded.saved_at)");
        for(auto dataset:{AchievementDataset::Definitions,AchievementDataset::Names}){
            freshness.Bind(1,std::string(DatasetName(dataset)));freshness.Bind(2,stamp/1000000);freshness.Execute();freshness.Reset();
        }
    }
    return accepted;
}
inline bool WriteAchievementObservations(const std::string& root,const Json& rows,int64_t stamp,Json& accepted){
    if(root.empty())return false;
    try{
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        auto committed=StoreAchievementObservations(db,rows,stamp);
        tx.Commit();accepted=std::move(committed);return true;
    }catch(...){return false;}
}
inline std::map<uint32_t,int64_t> ReadAchievementWatermarks(const std::string& root){
    std::map<uint32_t,int64_t> out;
    std::lock_guard<std::mutex> lock(Mutex());
    const auto path=Storage::Paths(root).Cache();
    if(!std::filesystem::exists(path))return out;
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
    Initialize(db);
    auto q=db.Prepare("SELECT id,request_at FROM reference_record_watermarks WHERE dataset=?");
    q.Bind(1,std::string(AchievementDefinitionWatermark));
    while(q.Step())out.emplace(static_cast<uint32_t>(q.Integer(0)),q.Integer(1));
    return out;
}
inline bool VerifyAchievementIdIndex(const std::string& root,int64_t stamp){
    if(root.empty()||stamp<=0)return false;
    try{
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        auto q=db.Prepare("INSERT INTO reference_freshness VALUES('ach_name_index_verified',?) ON CONFLICT(dataset) DO UPDATE SET saved_at=max(saved_at,excluded.saved_at)");
        q.Bind(1,stamp);q.Execute();tx.Commit();return true;
    }catch(...){return false;}
}
// The ordinary names save time is not proof of a complete ArenaNet ID check.
inline int64_t ReadNameIndexVerification(const std::string& root) {
    std::lock_guard<std::mutex> lock(Mutex());
    const auto path=Storage::Paths(root).Cache();
    if(!std::filesystem::exists(path)) return 0;
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
    Initialize(db);
    return db.Scalar("SELECT coalesce((SELECT saved_at FROM reference_freshness WHERE dataset='ach_name_index_verified'),0)");
}
inline std::optional<Json> ReadCatalog(const std::string& root,AchievementDataset d) {
    {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        if(std::filesystem::exists(path)) {
            Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
            Initialize(db);
            Database::Transaction tx(db);
            auto q=db.Prepare("SELECT saved_at FROM reference_freshness WHERE dataset=?");
            q.Bind(1,std::string(DatasetName(d)));
            if(q.Step()) {
                auto result=ReadCatalogRows(db,d,q.Integer(0));
                tx.Commit();return result;
            }
        }
    }
    const auto legacy=Storage::Paths(root).base/(std::string(DatasetName(d))+".json");
    std::ifstream file(legacy);
    if(!file) return std::nullopt;
    auto data=Json::parse(file);file.close();
    if(!WriteCatalog(root,d,data)) return std::nullopt;
    std::error_code ec;std::filesystem::remove(legacy,ec);
    return data;
}
} // namespace AlterEgo::Cache
