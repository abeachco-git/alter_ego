#pragma once
#include "CacheDatabase.h"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

namespace AlterEgo::Cache {
using ReferenceJson = nlohmann::json;
enum class ReferenceDataset { Specializations, Traits, Skills, ItemStats, Dyes };

inline const char* ReferenceName(ReferenceDataset d) {
    switch(d) {
    case ReferenceDataset::Specializations: return "spec_cache";
    case ReferenceDataset::Traits: return "trait_cache";
    case ReferenceDataset::Skills: return "skill_cache";
    case ReferenceDataset::ItemStats: return "itemstat_cache";
    case ReferenceDataset::Dyes: return "dye_cache";
    }
    throw std::runtime_error("Unknown reference dataset");
}
inline int64_t ReferenceSnapshotTime() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
inline uint32_t ReferenceId(const ReferenceJson& value) {
    if(!value.is_number_integer()) throw std::runtime_error("Invalid metadata ID");
    const auto id=value.get<int64_t>();
    if(id<0||id>UINT32_MAX) throw std::runtime_error("Invalid metadata ID");
    return static_cast<uint32_t>(id);
}
inline int64_t ReferenceInteger(const ReferenceJson& row,const char* key) {
    if(!row.contains(key))return 0;
    const auto& value=row.at(key);
    if(!value.is_number_integer())throw std::runtime_error("Invalid metadata integer");
    const auto n=value.get<int64_t>();
    if(n<INT32_MIN||n>UINT32_MAX)throw std::runtime_error("Metadata integer out of range");
    return n;
}
inline int64_t ReferenceChannel(const ReferenceJson& row,const char* key,size_t i) {
    if(!row.contains(key)) return 0;
    const auto& rgb=row.at(key);
    if(!rgb.is_array()||rgb.size()!=3||!rgb.at(i).is_number_integer())
        throw std::runtime_error("Invalid dye RGB");
    const auto n=rgb.at(i).get<int64_t>();
    if(n<0||n>255)throw std::runtime_error("Invalid dye channel");
    return n;
}
inline void WriteReferenceRows(Database::Connection& db,ReferenceDataset d,const ReferenceJson& rows) {
    if(!rows.is_array()||rows.empty())throw std::runtime_error("Empty metadata snapshot");
    // These are full projections of cumulative in-memory maps. Replacing a
    // single dataset atomically prevents mixing rows from different captures.
    // Tooltip facts are the only opaque fields: their API-defined shape is
    // heterogeneous and the existing tooltip adapter consumes them verbatim.
    switch(d) {
    case ReferenceDataset::Specializations: {
        db.Exec("DELETE FROM reference_specializations");
        auto q=db.Prepare("INSERT INTO reference_specializations VALUES(?,?,?,?,?,?,?,?)");
        auto minor_traits=db.Prepare("INSERT INTO reference_specializations_minor_traits VALUES(?,?,?)");
        auto major_traits=db.Prepare("INSERT INTO reference_specializations_major_traits VALUES(?,?,?)");
        for(const auto& row:rows) {
            if(!row.is_object())throw std::runtime_error("Invalid metadata row");
            const auto id=ReferenceId(row.at("id"));
            q.Bind(1,id);
            q.Bind(2,row.value("name",std::string()));
            q.Bind(3,row.value("icon",std::string()));
            q.Bind(4,row.value("background",std::string()));
            q.Bind(5,row.value("profession",std::string()));
            q.Bind(6,row.value("profession_icon",std::string()));
            q.Bind(7,row.value("profession_icon_big",std::string()));
            q.Bind(8,row.value("elite",false));
            q.Execute();q.Reset();
            if(row.contains("minor_traits")) {
                const auto& values=row.at("minor_traits");
                if(!values.is_array())throw std::runtime_error("Invalid metadata child list");
                int ordinal=0;
                for(const auto& value:values) {
                    minor_traits.Bind(1,id);minor_traits.Bind(2,ordinal++);minor_traits.Bind(3,ReferenceId(value));
                    minor_traits.Execute();minor_traits.Reset();
                }
            }
            if(row.contains("major_traits")) {
                const auto& values=row.at("major_traits");
                if(!values.is_array())throw std::runtime_error("Invalid metadata child list");
                int ordinal=0;
                for(const auto& value:values) {
                    major_traits.Bind(1,id);major_traits.Bind(2,ordinal++);major_traits.Bind(3,ReferenceId(value));
                    major_traits.Execute();major_traits.Reset();
                }
            }
        }
        if(db.Scalar("SELECT count(*) FROM reference_specializations")==0)throw std::runtime_error("No usable metadata rows");
        break;
    }
    case ReferenceDataset::Traits: {
        db.Exec("DELETE FROM reference_traits");
        auto q=db.Prepare("INSERT INTO reference_traits VALUES(?,?,?,?,?,?,?,?)");
        for(const auto& row:rows) {
            if(!row.is_object())throw std::runtime_error("Invalid metadata row");
            const auto id=ReferenceId(row.at("id"));
            q.Bind(1,id);
            q.Bind(2,row.value("name",std::string()));
            q.Bind(3,row.value("icon",std::string()));
            q.Bind(4,row.value("description",std::string()));
            q.Bind(5,ReferenceInteger(row,"tier"));
            q.Bind(6,ReferenceInteger(row,"order"));
            q.Bind(7,row.value("slot",std::string()));
            if(row.contains("facts")&&!row.at("facts").is_null())q.Bind(8,row.at("facts").dump());else q.BindNull(8);
            q.Execute();q.Reset();
        }
        if(db.Scalar("SELECT count(*) FROM reference_traits")==0)throw std::runtime_error("No usable metadata rows");
        break;
    }
    case ReferenceDataset::Skills: {
        db.Exec("DELETE FROM reference_skills");
        auto q=db.Prepare("INSERT INTO reference_skills VALUES(?,?,?,?,?,?,?)");
        auto professions=db.Prepare("INSERT INTO reference_skills_professions VALUES(?,?,?)");
        for(const auto& row:rows) {
            if(!row.is_object())throw std::runtime_error("Invalid metadata row");
            // Older cache rows without profession metadata must refetch on demand.
            if(!row.contains("professions"))continue;
            const auto id=ReferenceId(row.at("id"));
            q.Bind(1,id);
            q.Bind(2,row.value("name",std::string()));
            q.Bind(3,row.value("icon",std::string()));
            q.Bind(4,row.value("description",std::string()));
            q.Bind(5,row.value("type",std::string()));
            q.Bind(6,ReferenceInteger(row,"specialization"));
            if(row.contains("facts")&&!row.at("facts").is_null())q.Bind(7,row.at("facts").dump());else q.BindNull(7);
            q.Execute();q.Reset();
            if(row.contains("professions")) {
                const auto& values=row.at("professions");
                if(!values.is_array())throw std::runtime_error("Invalid metadata child list");
                int ordinal=0;
                for(const auto& value:values) {
                    professions.Bind(1,id);professions.Bind(2,ordinal++);professions.Bind(3,value.get<std::string>());
                    professions.Execute();professions.Reset();
                }
            }
        }
        if(db.Scalar("SELECT count(*) FROM reference_skills")==0)throw std::runtime_error("No usable metadata rows");
        break;
    }
    case ReferenceDataset::ItemStats: {
        db.Exec("DELETE FROM reference_itemstats");
        auto q=db.Prepare("INSERT INTO reference_itemstats VALUES(?,?)");
        auto attributes=db.Prepare("INSERT INTO reference_itemstats_attributes VALUES(?,?,?)");
        for(const auto& row:rows) {
            if(!row.is_object())throw std::runtime_error("Invalid metadata row");
            const auto id=ReferenceId(row.at("id"));
            q.Bind(1,id);
            q.Bind(2,row.value("name",std::string()));
            q.Execute();q.Reset();
            if(row.contains("attributes")) {
                const auto& values=row.at("attributes");
                if(!values.is_array())throw std::runtime_error("Invalid metadata child list");
                int ordinal=0;
                for(const auto& value:values) {
                    attributes.Bind(1,id);attributes.Bind(2,ordinal++);attributes.Bind(3,value.get<std::string>());
                    attributes.Execute();attributes.Reset();
                }
            }
        }
        if(db.Scalar("SELECT count(*) FROM reference_itemstats")==0)throw std::runtime_error("No usable metadata rows");
        break;
    }
    case ReferenceDataset::Dyes: {
        db.Exec("DELETE FROM reference_dyes");
        auto q=db.Prepare("INSERT INTO reference_dyes VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        for(const auto& row:rows) {
            if(!row.is_object())throw std::runtime_error("Invalid metadata row");
            const auto id=ReferenceId(row.at("id"));
            q.Bind(1,id);
            q.Bind(2,row.value("name",std::string()));
            q.Bind(3,ReferenceChannel(row,"base",0));
            q.Bind(4,ReferenceChannel(row,"base",1));
            q.Bind(5,ReferenceChannel(row,"base",2));
            q.Bind(6,ReferenceChannel(row,"cloth",0));
            q.Bind(7,ReferenceChannel(row,"cloth",1));
            q.Bind(8,ReferenceChannel(row,"cloth",2));
            q.Bind(9,ReferenceChannel(row,"leather",0));
            q.Bind(10,ReferenceChannel(row,"leather",1));
            q.Bind(11,ReferenceChannel(row,"leather",2));
            q.Bind(12,ReferenceChannel(row,"metal",0));
            q.Bind(13,ReferenceChannel(row,"metal",1));
            q.Bind(14,ReferenceChannel(row,"metal",2));
            q.Execute();q.Reset();
        }
        if(db.Scalar("SELECT count(*) FROM reference_dyes")==0)throw std::runtime_error("No usable metadata rows");
        break;
    }
    }
}
inline ReferenceJson ReadReferenceRows(Database::Connection& db,ReferenceDataset d) {
    ReferenceJson out=ReferenceJson::array();
    switch(d) {
    case ReferenceDataset::Specializations: {
        auto q=db.Prepare("SELECT * FROM reference_specializations ORDER BY id");
        std::map<int64_t,size_t> indexes;
        while(q.Step()) {
            ReferenceJson row={{"id",q.Integer(0)}};
            indexes.emplace(q.Integer(0),out.size());
            row["name"]=q.Text(1);
            row["icon"]=q.Text(2);
            row["background"]=q.Text(3);
            row["profession"]=q.Text(4);
            row["profession_icon"]=q.Text(5);
            row["profession_icon_big"]=q.Text(6);
            row["elite"]=q.Integer(7)!=0;
            row["minor_traits"]=ReferenceJson::array();
            row["major_traits"]=ReferenceJson::array();
            out.push_back(std::move(row));
        }
        {
            auto child=db.Prepare("SELECT id,value FROM reference_specializations_minor_traits ORDER BY id,ordinal");
            while(child.Step())out.at(indexes.at(child.Integer(0)))["minor_traits"].push_back(child.Integer(1));
        }
        {
            auto child=db.Prepare("SELECT id,value FROM reference_specializations_major_traits ORDER BY id,ordinal");
            while(child.Step())out.at(indexes.at(child.Integer(0)))["major_traits"].push_back(child.Integer(1));
        }
        break;
    }
    case ReferenceDataset::Traits: {
        auto q=db.Prepare("SELECT * FROM reference_traits ORDER BY id");
        while(q.Step()) {
            ReferenceJson row={{"id",q.Integer(0)}};
            row["name"]=q.Text(1);
            row["icon"]=q.Text(2);
            row["description"]=q.Text(3);
            row["tier"]=q.Integer(4);
            row["order"]=q.Integer(5);
            row["slot"]=q.Text(6);
            if(!q.IsNull(7))row["facts"]=ReferenceJson::parse(q.Text(7));
            out.push_back(std::move(row));
        }
        break;
    }
    case ReferenceDataset::Skills: {
        auto q=db.Prepare("SELECT * FROM reference_skills ORDER BY id");
        std::map<int64_t,size_t> indexes;
        while(q.Step()) {
            ReferenceJson row={{"id",q.Integer(0)}};
            indexes.emplace(q.Integer(0),out.size());
            row["name"]=q.Text(1);
            row["icon"]=q.Text(2);
            row["description"]=q.Text(3);
            row["type"]=q.Text(4);
            row["specialization"]=q.Integer(5);
            if(!q.IsNull(6))row["facts"]=ReferenceJson::parse(q.Text(6));
            row["professions"]=ReferenceJson::array();
            out.push_back(std::move(row));
        }
        {
            auto child=db.Prepare("SELECT id,value FROM reference_skills_professions ORDER BY id,ordinal");
            while(child.Step())out.at(indexes.at(child.Integer(0)))["professions"].push_back(child.Text(1));
        }
        break;
    }
    case ReferenceDataset::ItemStats: {
        auto q=db.Prepare("SELECT * FROM reference_itemstats ORDER BY id");
        std::map<int64_t,size_t> indexes;
        while(q.Step()) {
            ReferenceJson row={{"id",q.Integer(0)}};
            indexes.emplace(q.Integer(0),out.size());
            row["name"]=q.Text(1);
            row["attributes"]=ReferenceJson::array();
            out.push_back(std::move(row));
        }
        {
            auto child=db.Prepare("SELECT id,value FROM reference_itemstats_attributes ORDER BY id,ordinal");
            while(child.Step())out.at(indexes.at(child.Integer(0)))["attributes"].push_back(child.Text(1));
        }
        break;
    }
    case ReferenceDataset::Dyes: {
        auto q=db.Prepare("SELECT * FROM reference_dyes ORDER BY id");
        while(q.Step()) {
            ReferenceJson row={{"id",q.Integer(0)}};
            row["name"]=q.Text(1);
            row["base"]=ReferenceJson::array({q.Integer(2),q.Integer(3),q.Integer(4)});
            row["cloth"]=ReferenceJson::array({q.Integer(5),q.Integer(6),q.Integer(7)});
            row["leather"]=ReferenceJson::array({q.Integer(8),q.Integer(9),q.Integer(10)});
            row["metal"]=ReferenceJson::array({q.Integer(11),q.Integer(12),q.Integer(13)});
            out.push_back(std::move(row));
        }
        break;
    }
    }
    return out;
}
inline bool HasReferenceSnapshot(Database::Connection& db,ReferenceDataset d,int64_t* captured=nullptr) {
    auto q=db.Prepare("SELECT captured_at FROM reference_metadata_snapshots WHERE dataset=?");
    q.Bind(1,std::string(ReferenceName(d)));
    if(!q.Step())return false;
    if(captured)*captured=q.Integer(0);
    return true;
}
inline void StoreReferenceSnapshot(Database::Connection& db,ReferenceDataset d,
                                    const ReferenceJson& rows,int64_t captured) {
    WriteReferenceRows(db,d,rows);
    auto q=db.Prepare("INSERT INTO reference_metadata_snapshots VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET captured_at=excluded.captured_at");
    q.Bind(1,std::string(ReferenceName(d)));q.Bind(2,captured);q.Execute();
    auto fresh=db.Prepare("INSERT INTO reference_freshness VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET saved_at=excluded.saved_at");
    fresh.Bind(1,std::string(ReferenceName(d)));fresh.Bind(2,captured/1000000);fresh.Execute();
}
inline bool WriteReference(const std::string& root,ReferenceDataset d,
                           const ReferenceJson& rows,int64_t captured) {
    if(root.empty()||captured<0)return false;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);
        Database::Transaction tx(db);
        int64_t previous=0;
        if(HasReferenceSnapshot(db,d,&previous)&&captured<previous)return false;
        StoreReferenceSnapshot(db,d,rows,captured);
        tx.Commit();return true;
    } catch(...) {return false;}
}
// Native snapshots win, even if a legacy file remains. A malformed or
// interrupted migration keeps the original source. Do not fall back from a
// damaged/foreign database to stale JSON or overwrite it.
inline std::optional<ReferenceJson> ReadReference(const std::string& root,ReferenceDataset d) {
    if(root.empty())return std::nullopt;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        const auto mode=Database::Connection::OpenMode::Existing;
        if(std::filesystem::exists(path)) {
            Database::Connection db(path.u8string(),mode);
            Initialize(db);
            Database::Transaction tx(db);
            if(HasReferenceSnapshot(db,d)) {
                auto out=ReadReferenceRows(db,d);
                tx.Commit();return out;
            }
        }
        const auto legacy=Storage::ResolveCache(root,std::string(ReferenceName(d))+".json");
        ReferenceJson rows;
        {
            std::ifstream f(legacy);
            if(!f.is_open())return std::nullopt;
            f>>rows;
        }
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);
        Database::Transaction tx(db);
        if(!HasReferenceSnapshot(db,d))StoreReferenceSnapshot(db,d,rows,0);
        auto out=ReadReferenceRows(db,d);
        tx.Commit();
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(legacy),ec);
        return out;
    } catch(...) {return std::nullopt;}
}
} // namespace AlterEgo::Cache
