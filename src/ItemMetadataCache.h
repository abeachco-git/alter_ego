#pragma once
#include "ReferenceMetadataCache.h"
#include "NativeInventoryCore.h"

namespace AlterEgo::Cache {
enum class ItemDataset { Details, Aliases };
using ItemRequestStamps = std::map<uint32_t,int64_t>;
// Hold only across durable commit and live adoption, never across HTTP.
inline std::mutex& ItemPublicationMutex(){static std::mutex mutex;return mutex;}
inline const char* ItemDatasetName(ItemDataset d) {
    return d==ItemDataset::Details?"item_cache":"item_name_cache";
}
// Commerce may supply names for IDs without detail records. It must never
// erase the richer public item projection or replace a verified full name.
inline constexpr const char* kUpsertItemName=R"SQL(
INSERT INTO reference_items(id,name) VALUES(?,?)
ON CONFLICT(id) DO UPDATE SET name=excluded.name
WHERE reference_items.complete=0 AND excluded.name<>''
)SQL";
inline void PutCanonicalItemName(Database::Statement& q,uint32_t id,const std::string& name) {
    if(!id)throw std::runtime_error("Invalid item ID");
    q.Bind(1,id);q.Bind(2,name);q.Execute();q.Reset();
}
inline bool ItemSnapshotExists(Database::Connection& db,ItemDataset d,int64_t* stamp=nullptr) {
    auto q=db.Prepare("SELECT captured_at FROM reference_metadata_snapshots WHERE dataset=?");
    q.Bind(1,std::string(ItemDatasetName(d)));
    if(!q.Step())return false;
    if(stamp)*stamp=q.Integer(0);
    return true;
}
inline void StoreItemSnapshot(Database::Connection& db,ItemDataset d,const ReferenceJson& data,int64_t stamp,
                              const ItemRequestStamps* requestStamps=nullptr,
                              ReferenceJson* accepted=nullptr) {
    if(d==ItemDataset::Details) {
        if(!data.is_array()||data.empty())throw std::runtime_error("Invalid item detail snapshot");
        auto q=db.Prepare(R"SQL(
INSERT INTO reference_items(id,name,icon,rarity,type,description,chat_link,default_skin,details,complete,verified_at)
VALUES(?,?,?,?,?,?,?,?,?,1,?)
ON CONFLICT(id) DO UPDATE SET
 name=COALESCE(NULLIF(excluded.name,''),reference_items.name),
 icon=excluded.icon,rarity=excluded.rarity,type=excluded.type,description=excluded.description,
 chat_link=excluded.chat_link,default_skin=excluded.default_skin,details=excluded.details,complete=1,
 verified_at=excluded.verified_at
WHERE reference_items.complete=0 OR excluded.verified_at>reference_items.verified_at
RETURNING id
)SQL");
        auto flagWatermark=db.Prepare(RecordWatermarkSql);
        auto flags=db.Prepare("INSERT INTO native_item_flags VALUES(?,?,?,?) ON CONFLICT(item_id) DO UPDATE SET account_bound=excluded.account_bound,soulbound=excluded.soulbound,bind_on_use=excluded.bind_on_use");
        std::set<uint32_t> seen;
        for(const auto& row:data) {
            const auto id=ReferenceId(row.at("id"));
            if(!id||!seen.insert(id).second)throw std::runtime_error("Invalid or duplicate item ID");
            q.Bind(1,id);q.Bind(2,row.value("name",std::string()));q.Bind(3,row.value("icon",std::string()));
            q.Bind(4,row.value("rarity",std::string()));q.Bind(5,row.value("type",std::string()));
            q.Bind(6,row.value("description",std::string()));q.Bind(7,row.value("chat_link",std::string()));
            q.Bind(8,row.contains("default_skin")?ReferenceId(row.at("default_skin")):0);
            // Type-specific details are heterogeneous API extensions.
            if(row.contains("details")&&!row.at("details").is_null())q.Bind(9,row.at("details").dump());
            else q.BindNull(9);
            const auto verified=requestStamps?requestStamps->at(id):stamp;
            if(verified<0)throw std::runtime_error("Invalid item request stamp");
            q.Bind(10,verified);
            if(q.Step()){
                if(accepted)accepted->push_back(row);
                if(q.Step())throw std::runtime_error("Unexpected duplicate item result");
            }
            q.Reset();
            // Binding metadata has its own watermark: an older response may
            // fill previously absent flags but cannot regress verified flags.
            if(verified>0&&row.contains("flags")){
                const auto parsed=NativeInventory::ParseFlags(row.at("flags"));
                if(ClaimRecordWatermark(flagWatermark,"native_item_flags",id,verified)){
                    flags.Bind(1,id);flags.Bind(2,int(parsed.accountBound));flags.Bind(3,int(parsed.soulbound));
                    flags.Bind(4,int(parsed.bindOnUse));flags.Execute();flags.Reset();
                }
            }
        }
    } else {
        if(!data.is_object())throw std::runtime_error("Invalid item name snapshot");
        auto item=db.Prepare("INSERT INTO reference_items(id) VALUES(?) ON CONFLICT(id) DO NOTHING");
        auto alias=db.Prepare("INSERT INTO item_name_aliases VALUES(?,?) ON CONFLICT(name) DO UPDATE SET item_id=excluded.item_id");
        for(const auto& [name,v]:data.items()) {
            const auto id=ReferenceId(v);
            if(!id||name.empty())throw std::runtime_error("Invalid item name alias");
            item.Bind(1,id);item.Execute();item.Reset();
            alias.Bind(1,name);alias.Bind(2,id);alias.Execute();alias.Reset();
        }
    }
    auto mark=db.Prepare("INSERT INTO reference_metadata_snapshots VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET captured_at=MAX(reference_metadata_snapshots.captured_at,excluded.captured_at)");
    mark.Bind(1,std::string(ItemDatasetName(d)));mark.Bind(2,stamp);mark.Execute();
    auto fresh=db.Prepare("INSERT INTO reference_freshness VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET saved_at=MAX(reference_freshness.saved_at,excluded.saved_at)");
    fresh.Bind(1,std::string(ItemDatasetName(d)));fresh.Bind(2,stamp/1000000);fresh.Execute();
}
inline ReferenceJson ReadItemRows(Database::Connection& db,ItemDataset d) {
    if(d==ItemDataset::Aliases) {
        ReferenceJson out=ReferenceJson::object();
        // Names can be nonunique. Use the smallest ID as a deterministic
        // fallback; an explicitly recorded alias retains its selected ID.
        auto names=db.Prepare("SELECT name,MIN(id) FROM reference_items WHERE name<>'' GROUP BY name");
        while(names.Step())out[names.Text(0)]=names.Integer(1);
        auto aliases=db.Prepare("SELECT name,item_id FROM item_name_aliases");
        while(aliases.Step())out[aliases.Text(0)]=aliases.Integer(1);
        return out;
    }
    ReferenceJson out=ReferenceJson::array();
    auto q=db.Prepare("SELECT id,name,icon,rarity,type,description,chat_link,default_skin,details FROM reference_items WHERE complete=1 ORDER BY id");
    while(q.Step()) {
        ReferenceJson row={{"id",q.Integer(0)},{"name",q.Text(1)},{"icon",q.Text(2)},{"rarity",q.Text(3)},{"type",q.Text(4)}};
        if(!q.Text(5).empty())row["description"]=q.Text(5);
        if(!q.Text(6).empty())row["chat_link"]=q.Text(6);
        if(q.Integer(7))row["default_skin"]=q.Integer(7);
        if(!q.IsNull(8))row["details"]=ReferenceJson::parse(q.Text(8));
        out.push_back(std::move(row));
    }
    return out;
}
inline bool WriteItems(const std::string& root,ItemDataset d,const ReferenceJson& data,int64_t stamp) {
    if(root.empty()||stamp<0)return false;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        int64_t previous=0;
        if(ItemSnapshotExists(db,d,&previous)&&stamp<previous)return false;
        StoreItemSnapshot(db,d,data,stamp);tx.Commit();return true;
    }catch(...){return false;}
}
// Batches may finish in any order; the row watermark decides the winner.
inline bool WriteItemObservations(const std::string& root,const ReferenceJson& rows,int64_t requestStamp,ReferenceJson& accepted) {
    if(root.empty()||requestStamp<=0||rows.empty())return false;
    try{
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        auto committed=ReferenceJson::array();
        StoreItemSnapshot(db,ItemDataset::Details,rows,requestStamp,nullptr,&committed);
        tx.Commit();accepted=std::move(committed);return true;
    }catch(...){return false;}
}
inline std::optional<ReferenceJson> ReadItems(const std::string& root,ItemDataset d) {
    if(root.empty())return std::nullopt;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        if(std::filesystem::exists(path)) {
            Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
            Initialize(db);Database::Transaction tx(db);
            if(ItemSnapshotExists(db,d)) {
                auto out=ReadItemRows(db,d);tx.Commit();return out;
            }
        }
        const auto legacy=Storage::ResolveCache(root,std::string(ItemDatasetName(d))+".json");
        ReferenceJson data;
        {
            std::ifstream f(legacy);
            if(!f.is_open()) {
                // Commerce may have populated canonical names before the
                // GW2API name projection has ever been persisted.
                if(d==ItemDataset::Aliases&&std::filesystem::exists(path)) {
                    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
                    Initialize(db);Database::Transaction tx(db);
                    auto out=ReadItemRows(db,d);tx.Commit();return out;
                }
                return std::nullopt;
            }
            f>>data;
        }
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        if(!ItemSnapshotExists(db,d))StoreItemSnapshot(db,d,data,0);
        auto out=ReadItemRows(db,d);tx.Commit();
        std::error_code ec;std::filesystem::remove(std::filesystem::u8path(legacy),ec);
        return out;
    }catch(...){return std::nullopt;}
}
} // namespace AlterEgo::Cache
