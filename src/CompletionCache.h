#pragma once
// Rebuildable Clears and Wizard's Vault snapshots. Exact account identity is
// authoritative; legacy filename slugs are migration candidates only.
#include "CacheDatabase.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cctype>

namespace AlterEgo::Cache {
enum class CompletionDataset { Clears, Vault };
namespace CompletionRows {
using Json=nlohmann::json;
inline const char* Stem(CompletionDataset d) {
    return d==CompletionDataset::Clears?"clears_cache":"vault_cache";
}
inline const char* MarkerSql(CompletionDataset d) {
    return d==CompletionDataset::Clears?
        "SELECT fetch_time FROM clears_snapshots WHERE account=?":
        "SELECT fetch_time FROM vault_snapshots WHERE account=?";
}
inline bool Exists(Database::Connection& db,CompletionDataset d,const std::string& account,int64_t* stamp=nullptr) {
    auto q=db.Prepare(MarkerSql(d));q.Bind(1,account);
    if(!q.Step())return false;
    if(stamp)*stamp=q.Integer(0);
    return true;
}
inline int64_t Number(const Json& row,const char* key) {
    if(!row.contains(key))return 0;
    const auto& v=row.at(key);
    if(!v.is_number_integer())throw std::runtime_error("Invalid completion number");
    return v.get<int64_t>();
}
inline int64_t Flag(const Json& row,const char* key) {
    if(!row.contains(key))return 0;
    const auto& v=row.at(key);
    if(!v.is_boolean())throw std::runtime_error("Invalid completion flag");
    return v.get<bool>()?1:0;
}
inline std::string Text(const Json& row,const char* key) {
    return row.value(key,std::string());
}
inline Json Array(const Json& row,const char* key) {
    auto it=row.find(key);
    if(it==row.end())return Json::array();
    if(!it->is_array())throw std::runtime_error("Invalid completion array");
    return *it;
}
inline void Store(Database::Connection& db,CompletionDataset d,const std::string& account,const Json& data) {
    if(account.empty()||!data.is_object()||Text(data,"account")!=account)
        throw std::runtime_error("Unverified completion account");
    const auto stamp=Number(data,"fetch_time");
    int64_t previous=0;
    if(stamp<0||(Exists(db,d,account,&previous)&&stamp<previous))
        throw std::runtime_error("Older completion snapshot");
    if(d==CompletionDataset::Clears) {
        auto mark=db.Prepare("INSERT INTO clears_snapshots VALUES(?,?) ON CONFLICT(account) DO UPDATE SET fetch_time=excluded.fetch_time");
        mark.Bind(1,account);mark.Bind(2,stamp);mark.Execute();
        auto remove=db.Prepare("DELETE FROM clears_sections WHERE account=?");
        remove.Bind(1,account);remove.Execute();
        auto section=db.Prepare("INSERT INTO clears_sections(account,section,fetched_at) VALUES(?,?,?)");
        auto entry=db.Prepare("INSERT INTO clears_entries VALUES(?,?,?,?,?,?,?,?,?)");
        auto names=db.Prepare("INSERT INTO clears_bit_names VALUES(?,?,?,?,?)");
        auto done=db.Prepare("INSERT INTO clears_bit_done VALUES(?,?,?,?,?)");
        for(const char* key:{"dailyFractals","dailyBounties","weeklyWings","weeklyStrikes"}) {
            if(!data.contains(key))continue;
            const std::string kind=key;
            const auto rows=kind=="weeklyStrikes"?Json::array({data.at(key)}):Array(data,key);
            const auto sectionStamp=data.contains("section_stamps")?Number(data.at("section_stamps"),key):0;
            if(sectionStamp<0||sectionStamp>stamp)throw std::runtime_error("Invalid Clears section stamp");
            section.Bind(1,account);section.Bind(2,kind);section.Bind(3,sectionStamp);section.Execute();section.Reset();
            int64_t ordinal=0;
            for(const auto& row:rows) {
                if(!row.is_object())throw std::runtime_error("Invalid Clears entry");
                entry.Bind(1,account);entry.Bind(2,kind);entry.Bind(3,ordinal);
                entry.Bind(4,Number(row,"id"));entry.Bind(5,Text(row,"name"));entry.Bind(6,Text(row,"tier"));
                entry.Bind(7,Flag(row,"done"));entry.Bind(8,Number(row,"current"));entry.Bind(9,Number(row,"max"));entry.Execute();entry.Reset();
                int64_t bit=0;
                for(const auto& name:Array(row,"bitNames")) {
                    names.Bind(1,account);names.Bind(2,kind);names.Bind(3,ordinal);names.Bind(4,bit++);names.Bind(5,name.get<std::string>());names.Execute();names.Reset();
                }
                bit=0;
                for(const auto& value:Array(row,"bitDone")) {
                    if(!value.is_number_integer()||(value!=0&&value!=1))throw std::runtime_error("Invalid Clears bit");
                    done.Bind(1,account);done.Bind(2,kind);done.Bind(3,ordinal);done.Bind(4,bit++);done.Bind(5,value.get<int64_t>());done.Execute();done.Reset();
                }
                ++ordinal;
            }
        }
    } else {
        auto mark=db.Prepare("INSERT INTO vault_snapshots VALUES(?,?,?) ON CONFLICT(account) DO UPDATE SET fetch_time=excluded.fetch_time,season_end=excluded.season_end");
        mark.Bind(1,account);mark.Bind(2,stamp);
        if(data.contains("season_end"))mark.Bind(3,Number(data,"season_end"));else mark.BindNull(3);
        mark.Execute();
        auto remove=db.Prepare("DELETE FROM vault_periods WHERE account=?");
        remove.Bind(1,account);remove.Execute();
        auto period=db.Prepare("INSERT INTO vault_periods(account,section,meta_current,meta_complete,meta_astral,meta_claimed,fetched_at) VALUES(?,?,?,?,?,?,?)");
        auto objective=db.Prepare("INSERT INTO vault_objectives VALUES(?,?,?,?,?,?,?,?,?,?)");
        for(const char* key:{"daily","weekly","special"}) {
            if(!data.contains(key))continue;
            const auto& row=data.at(key);
            if(!row.is_object())throw std::runtime_error("Invalid Vault period");
            const std::string kind=key;
            period.Bind(1,account);period.Bind(2,kind);period.Bind(3,Number(row,"meta_progress_current"));
            period.Bind(4,Number(row,"meta_progress_complete"));period.Bind(5,Number(row,"meta_reward_astral"));period.Bind(6,Flag(row,"meta_reward_claimed"));period.Bind(7,row.contains("fetched_at")?Number(row,"fetched_at"):stamp);period.Execute();period.Reset();
            int64_t ordinal=0;
            for(const auto& o:Array(row,"objectives")) {
                if(!o.is_object())throw std::runtime_error("Invalid Vault objective");
                objective.Bind(1,account);objective.Bind(2,kind);objective.Bind(3,ordinal++);
                objective.Bind(4,Number(o,"id"));objective.Bind(5,Text(o,"title"));objective.Bind(6,Text(o,"track"));
                objective.Bind(7,Number(o,"acclaim"));objective.Bind(8,Number(o,"progress_current"));objective.Bind(9,Number(o,"progress_complete"));objective.Bind(10,Flag(o,"claimed"));objective.Execute();objective.Reset();
            }
        }
    }
}
inline std::optional<Json> Read(Database::Connection& db,CompletionDataset d,const std::string& account) {
    int64_t stamp=0;
    if(!Exists(db,d,account,&stamp))return std::nullopt;
    Json out={{"account",account},{"fetch_time",stamp}};
    if(d==CompletionDataset::Clears) {
        auto sections=db.Prepare("SELECT section,fetched_at FROM clears_sections WHERE account=?");sections.Bind(1,account);
        auto entries=db.Prepare("SELECT ordinal,id,name,tier,done,current_count,max_count FROM clears_entries WHERE account=? AND section=? ORDER BY ordinal");
        auto names=db.Prepare("SELECT name FROM clears_bit_names WHERE account=? AND section=? AND entry_ordinal=? ORDER BY ordinal");
        auto done=db.Prepare("SELECT done FROM clears_bit_done WHERE account=? AND section=? AND entry_ordinal=? ORDER BY ordinal");
        while(sections.Step()) {
            auto kind=sections.Text(0);Json rows=Json::array();
            if(sections.Integer(1)>0)out["section_stamps"][kind]=sections.Integer(1);
            entries.Bind(1,account);entries.Bind(2,kind);
            while(entries.Step()) {
                Json row={{"id",entries.Integer(1)},{"name",entries.Text(2)},{"tier",entries.Text(3)},
                    {"done",entries.Integer(4)!=0},{"current",entries.Integer(5)},{"max",entries.Integer(6)},
                    {"bitNames",Json::array()},{"bitDone",Json::array()}};
                names.Bind(1,account);names.Bind(2,kind);names.Bind(3,entries.Integer(0));
                while(names.Step())row["bitNames"].push_back(names.Text(0));
                names.Reset();
                done.Bind(1,account);done.Bind(2,kind);done.Bind(3,entries.Integer(0));
                while(done.Step())row["bitDone"].push_back(done.Integer(0));
                done.Reset();rows.push_back(std::move(row));
            }
            entries.Reset();
            if(kind=="weeklyStrikes") {
                if(rows.size()!=1)throw std::runtime_error("Invalid stored strike section");
                out[kind]=std::move(rows[0]);
            } else out[kind]=std::move(rows);
        }
    } else {
        auto season=db.Prepare("SELECT season_end FROM vault_snapshots WHERE account=?");season.Bind(1,account);
        if(season.Step()&&!season.IsNull(0))out["season_end"]=season.Integer(0);
        auto periods=db.Prepare("SELECT section,meta_current,meta_complete,meta_astral,meta_claimed,fetched_at FROM vault_periods WHERE account=?");periods.Bind(1,account);
        auto objectives=db.Prepare("SELECT id,title,track,acclaim,progress_current,progress_complete,claimed FROM vault_objectives WHERE account=? AND section=? ORDER BY ordinal");
        while(periods.Step()) {
            auto kind=periods.Text(0);
            Json row={{"meta_progress_current",periods.Integer(1)},{"meta_progress_complete",periods.Integer(2)},
                {"meta_reward_astral",periods.Integer(3)},{"meta_reward_claimed",periods.Integer(4)!=0},{"fetched_at",periods.Integer(5)},{"objectives",Json::array()}};
            objectives.Bind(1,account);objectives.Bind(2,kind);
            while(objectives.Step())row["objectives"].push_back({{"id",objectives.Integer(0)},{"title",objectives.Text(1)},
                {"track",objectives.Text(2)},{"acclaim",objectives.Integer(3)},{"progress_current",objectives.Integer(4)},
                {"progress_complete",objectives.Integer(5)},{"claimed",objectives.Integer(6)!=0}});
            objectives.Reset();out[kind]=std::move(row);
        }
    }
    return out;
}
inline std::string LegacyName(CompletionDataset d,const std::string& account) {
    std::string slug;
    for(unsigned char ch:account)slug.push_back(std::isalnum(ch)||ch=='-'||ch=='_'?static_cast<char>(ch):'_');
    return std::string(Stem(d))+"_"+slug+".json";
}
} // namespace CompletionRows
inline bool HasCompletion(const std::string& root,CompletionDataset d,const std::string& account) {
    if(root.empty()||account.empty())return false;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        if(!std::filesystem::exists(path))return false;
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
        Initialize(db);return CompletionRows::Exists(db,d,account);
    }catch(...){return false;}
}
inline bool WriteCompletion(const std::string& root,CompletionDataset d,const std::string& account,const CompletionRows::Json& data) {
    if(root.empty()||account.empty())return false;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        CompletionRows::Store(db,d,account,data);tx.Commit();return true;
    }catch(...){return false;}
}
// Merge verified periods transactionally; omitted sections retain their own
// timestamps and previous reset fingerprints.
// Clears definitions/progress are independently verified; preserve prior reset
// fingerprints for absent or failed sections rather than marking them current.
inline bool WriteClearsSections(const std::string& root,const std::string& account,const CompletionRows::Json& patch) {
    using namespace CompletionRows;
    if(root.empty()||account.empty()||!patch.is_object()||Text(patch,"account")!=account)return false;
    try{
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        auto prior=Read(db,CompletionDataset::Clears,account);
        Json merged=prior.value_or(Json{{"account",account},{"fetch_time",0}});
        int64_t newest=Number(merged,"fetch_time");
        // Convert the legacy aggregate timestamp into section stamps before
        // moving the aggregate forward. Omitted sections keep their old age.
        for(const char* key:{"dailyFractals","dailyBounties","weeklyWings","weeklyStrikes"}){
            if(merged.contains(key)&&(!merged.contains("section_stamps")||!merged["section_stamps"].contains(key)))
                merged["section_stamps"][key]=newest;
            if(!patch.contains(key))continue;
            const auto stamp=Number(patch.at("section_stamps"),key);
            if(stamp<=0)throw std::runtime_error("Unverified Clears section");
            if(merged.contains(key)&&stamp<Number(merged.at("section_stamps"),key))continue;
            const auto& row=patch.at(key);
            if(std::string(key)=="weeklyStrikes"?(!row.is_object()||Number(row,"id")<=0):(!row.is_array()||row.empty()))
                throw std::runtime_error("Empty Clears section");
            merged[key]=row;merged["section_stamps"][key]=stamp;newest=std::max(newest,stamp);
        }
        merged["fetch_time"]=newest;
        Store(db,CompletionDataset::Clears,account,merged);tx.Commit();return true;
    }catch(...){return false;}
}
inline bool WriteVaultPeriods(const std::string& root,const std::string& account,const CompletionRows::Json& patch) {
    using namespace CompletionRows;
    if(root.empty()||account.empty()||!patch.is_object()||Text(patch,"account")!=account)return false;
    try{
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        auto prior=Read(db,CompletionDataset::Vault,account);
        Json merged=prior.value_or(Json{{"account",account},{"fetch_time",0}});
        int64_t newest=Number(merged,"fetch_time");
        const auto oldEnd=Number(merged,"season_end"),patchEnd=Number(patch,"season_end");
        // Advancing a known season must not relabel its expired Special rows.
        const bool seasonAdvanced=oldEnd>0&&patchEnd>oldEnd;
        if(seasonAdvanced&&merged.contains("special")&&Number(merged.at("special"),"fetched_at")<oldEnd)
            merged.erase("special");
        for(const char* key:{"daily","weekly","special"}){
            if(!patch.contains(key))continue;
            const auto& row=patch.at(key);
            auto stamp=Number(row,"fetched_at");
            if(stamp<=0)throw std::runtime_error("Unverified Vault period");
            if(std::string(key)=="special"&&((patchEnd>0&&patchEnd<oldEnd)||(seasonAdvanced&&stamp<oldEnd)))continue;
            if(merged.contains(key)&&stamp<Number(merged.at(key),"fetched_at"))continue;
            merged[key]=row;newest=std::max(newest,stamp);
        }
        if(patch.contains("season_end")&&Number(patch,"season_end")>0)
            merged["season_end"]=std::max(Number(merged,"season_end"),Number(patch,"season_end"));
        merged["fetch_time"]=newest;
        Store(db,CompletionDataset::Vault,account,merged);tx.Commit();return true;
    }catch(...){return false;}
}
inline std::optional<CompletionRows::Json> ReadCompletion(const std::string& root,CompletionDataset d,const std::string& account,bool singleAccount=false) {
    using namespace CompletionRows;
    if(root.empty()||account.empty())return std::nullopt;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        if(std::filesystem::exists(path)) {
            Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
            Initialize(db);Database::Transaction tx(db);auto out=Read(db,d,account);tx.Commit();if(out)return out;
        }
        std::string legacy=Storage::ResolveCache(root,LegacyName(d,account));
        Json data;
        bool verified=false;
        // A slug collision must never establish identity; the embedded account
        // is mandatory for a scoped file. An old unscoped file may be imported
        // only once H&S confirms there is exactly one configured account.
        {std::ifstream in(legacy);if(in){in>>data;verified=data.is_object()&&data.contains("account")&&Text(data,"account")==account;}}
        if(!verified&&singleAccount) {
            legacy=Storage::ResolveCache(root,std::string(Stem(d))+".json");
            std::ifstream in(legacy);
            if(in){in>>data;verified=data.is_object()&&(!data.contains("account")||Text(data,"account")==account);}
        }
        if(!verified)return std::nullopt;
        data["account"]=account;
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        Store(db,d,account,data);auto out=Read(db,d,account);tx.Commit();
        std::error_code ec;std::filesystem::remove(std::filesystem::u8path(legacy),ec);
        return out;
    }catch(...){return std::nullopt;}
}
} // namespace AlterEgo::Cache
