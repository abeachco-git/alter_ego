#pragma once
#include "CacheDatabase.h"
#include <array>
#include <limits>

namespace AlterEgo::Cache::Wiki {
using Json = nlohmann::json;
enum class Kind { Metadata, Failures };
inline const char* Name(Kind kind) { return kind==Kind::Metadata ? "metadata" : "failures"; }
inline constexpr std::array<const char*,8> Fields{
    "acquisition","collection","set","vendor","vendor_location","vendor_waypoint","vendor_cost","vendor_cost_keys"
};
inline int64_t SkinId(const std::string& key) {
    size_t used=0;
    auto id=std::stoull(key,&used);
    if (used!=key.size() || std::to_string(id)!=key || !id || id>std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Invalid wiki skin identity");
    return static_cast<int64_t>(id);
}
inline Json Normalize(Kind kind,const Json& input) {
    if(!input.is_object()) throw std::runtime_error("Invalid wiki snapshot");
    Json result=Json::object();
    if(kind==Kind::Metadata) {
        for(const auto& [key,entry]:input.items()) {
            SkinId(key);
            if(!entry.is_object()) throw std::runtime_error("Invalid wiki metadata entry");
            Json row=Json::object();
            for(auto field:Fields) {
                const auto value=entry.value(field,std::string{});
                if(!value.empty()) row[field]=value;
            }
            if(!row.empty()) result[key]=std::move(row);
        }
    } else {
        result["skins"]=Json::object();result["currencies"]=Json::object();
        const bool wrapped=input.contains("skins") || input.contains("currencies");
        const auto skins=wrapped ? input.value("skins",Json::object()) : input;
        const auto currencies=wrapped ? input.value("currencies",Json::object()) : Json::object();
        if(!skins.is_object() || !currencies.is_object()) throw std::runtime_error("Invalid wiki failures");
        for(const auto& [key,value]:skins.items()) {
            SkinId(key);
            if(!value.is_number_integer() || value.get<int64_t>()<0) throw std::runtime_error("Invalid wiki failure time");
            result["skins"][key]=value;
        }
        for(const auto& [key,value]:currencies.items()) {
            if(key.empty() || !value.is_number_integer() || value.get<int64_t>()<0) throw std::runtime_error("Invalid currency failure");
            result["currencies"][key]=value;
        }
    }
    return result;
}
inline bool Present(Database::Connection& db,Kind kind) {
    auto q=db.Prepare("SELECT 1 FROM wiki_cache_state WHERE kind=?");
    q.Bind(1,std::string(Name(kind)));return q.Step();
}
inline void Replace(Database::Connection& db,Kind kind,const Json& data) {
    Database::Transaction tx(db);
    if(kind==Kind::Metadata) {
        db.Exec("DELETE FROM wiki_skin_metadata");
        auto put=db.Prepare("INSERT INTO wiki_skin_metadata VALUES(?,?,?,?,?,?,?,?,?)");
        for(const auto& [key,row]:data.items()) {
            put.Bind(1,SkinId(key));
            for(size_t i=0;i<Fields.size();++i) put.Bind(static_cast<int>(i+2),row.value(Fields[i],std::string{}));
            put.Execute();put.Reset();
        }
    } else {
        db.Exec("DELETE FROM wiki_skin_failures; DELETE FROM wiki_currency_failures");
        auto skin=db.Prepare("INSERT INTO wiki_skin_failures VALUES(?,?)");
        auto currency=db.Prepare("INSERT INTO wiki_currency_failures VALUES(?,?)");
        for(const auto& [key,value]:data.at("skins").items()) {
            skin.Bind(1,SkinId(key));skin.Bind(2,value.get<int64_t>());skin.Execute();skin.Reset();
        }
        for(const auto& [key,value]:data.at("currencies").items()) {
            currency.Bind(1,key);currency.Bind(2,value.get<int64_t>());currency.Execute();currency.Reset();
        }
    }
    auto state=db.Prepare("INSERT OR IGNORE INTO wiki_cache_state VALUES(?)");
    state.Bind(1,std::string(Name(kind)));state.Execute();
    tx.Commit();
}
inline Json Project(Database::Connection& db,Kind kind) {
    Json out=Json::object();
    Database::Transaction tx(db);
    if(kind==Kind::Metadata) {
        auto q=db.Prepare("SELECT * FROM wiki_skin_metadata ORDER BY skin_id");
        while(q.Step()) {
            Json row=Json::object();
            for(size_t i=0;i<Fields.size();++i) {
                auto value=q.Text(static_cast<int>(i+1));
                if(!value.empty()) row[Fields[i]]=std::move(value);
            }
            out[std::to_string(q.Integer(0))]=std::move(row);
        }
    } else {
        out["skins"]=Json::object();out["currencies"]=Json::object();
        auto skin=db.Prepare("SELECT skin_id,failed_at FROM wiki_skin_failures");
        while(skin.Step()) out["skins"][std::to_string(skin.Integer(0))]=skin.Integer(1);
        auto currency=db.Prepare("SELECT currency,failed_at FROM wiki_currency_failures");
        while(currency.Step()) out["currencies"][currency.Text(0)]=currency.Integer(1);
    }
    tx.Commit();return out;
}
inline bool Write(const std::string& root,Kind kind,const Json& input) {
    try {
        auto data=Normalize(kind,input);
        std::lock_guard<std::mutex> lock(Cache::Mutex());
        auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Cache::Initialize(db);Replace(db,kind,data);
        return true;
    } catch(...) {return false;}
}
// Native presence wins, including a deliberately empty snapshot. Never
// overwrite a corrupt/foreign database with legacy input. Only remove a
// rebuildable legacy file after its committed native projection is verified.
inline std::optional<Json> Read(const std::string& root,Kind kind,const std::filesystem::path& legacy) {
    std::lock_guard<std::mutex> lock(Cache::Mutex());
    auto path=Storage::Paths(root).Cache();
    const bool hasLegacy=std::filesystem::exists(legacy);
    if(!hasLegacy && !std::filesystem::exists(path))return std::nullopt;
    std::filesystem::create_directories(path.parent_path());
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
    Cache::Initialize(db);
    if(Present(db,kind))return Project(db,kind);
    if(!hasLegacy)return std::nullopt;
    const auto bytes=Storage::ReadMigrationBytes(legacy);
    const auto data=Normalize(kind,Json::parse(bytes));
    Replace(db,kind,data);
    auto verified=Project(db,kind);
    if(verified!=data)throw std::runtime_error("Wiki migration verification failed");
    std::error_code ec;
    if(Storage::ReadMigrationBytes(legacy)==bytes)std::filesystem::remove(legacy,ec);
    return verified;
}
}
