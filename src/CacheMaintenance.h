#pragma once
// Startup-only cache maintenance. No live invalidation, API calls or durable
// database writes. Archive known cache sources before ordinary cache loading.
#include "CacheDatabase.h"
#include <array>
#include <algorithm>

namespace AlterEgo::Storage {
enum class CacheOperation { All, Artwork, Characters, AccountProgress, AchievementCatalog, PublicCatalog };
inline const char* OperationName(CacheOperation op) {
    switch(op) {
    case CacheOperation::All: return "all";
    case CacheOperation::Artwork: return "artwork";
    case CacheOperation::Characters: return "characters";
    case CacheOperation::AccountProgress: return "account-progress";
    case CacheOperation::AchievementCatalog: return "achievement-catalog";
    case CacheOperation::PublicCatalog: return "public-catalog";
    }
    throw std::runtime_error("Unknown cache operation");
}
inline constexpr std::array<CacheOperation,6> kCacheOperations={
    CacheOperation::All,CacheOperation::Artwork,CacheOperation::Characters,
    CacheOperation::AccountProgress,CacheOperation::AchievementCatalog,CacheOperation::PublicCatalog
};
inline std::optional<CacheOperation> ParseCacheOperation(const std::string& name) {
    for(auto op:kCacheOperations)if(name==OperationName(op))return op;
    return std::nullopt;
}
inline bool MaintenancePending(const std::string& root) {
    if(root.empty())return false;
    std::error_code ec;
    return std::filesystem::exists(Paths(root).CacheMaintenanceRequest(),ec);
}
// Reject redirected ancestors before moving a classified path. The root itself
// may be installed via a link; resolve it once and confine operations below it.
inline void CheckMaintenancePath(const std::filesystem::path& base,const std::filesystem::path& relative) {
    namespace fs=std::filesystem;
    auto current=base;
    for(const auto& part:relative) {
        if(part==".."||part=="."||part.empty()||part.has_root_path())
            throw std::runtime_error("Invalid maintenance path");
        current/=part;
        const auto status=fs::symlink_status(current);
        if(fs::is_symlink(status))throw std::runtime_error("Redirected cache path; no maintenance performed there");
        if(!fs::exists(status))break;
        if(fs::canonical(current)!=current)throw std::runtime_error("Redirected cache path");
    }
}
inline bool ScheduleMaintenance(const std::string& root,CacheOperation op,std::string& error) {
    namespace fs=std::filesystem;
    try {
        if(root.empty())throw std::runtime_error("Missing data directory");
        const Paths paths(root);
        fs::create_directories(paths.base);
        const auto base=fs::canonical(paths.base);
        CheckMaintenancePath(base,"config");
        fs::create_directories(paths.ConfigDirectory());
        CheckMaintenancePath(base,"config/cache-maintenance-request");
        // A directory is an exclusive reservation; don't overwrite an existing
        // request. A crash before writing a valid operation fails closed.
        if(!fs::create_directory(paths.CacheMaintenanceRequest()))
            throw std::runtime_error("A maintenance request already exists");
        try {
            WriteMigrationBytes(paths.CacheMaintenanceRequest()/"operation",OperationName(op));
        }catch(...) {
            // Only remove the marker created by this operation, never config/.
            std::error_code ec;
            fs::remove(paths.CacheMaintenanceRequest()/"operation",ec);
            fs::remove(paths.CacheMaintenanceRequest(),ec);
            throw;
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
struct MaintenanceResult { bool attempted=false,success=false; std::string message; };

namespace MaintenanceDetail {
namespace fs=std::filesystem;
struct Archive {
    fs::path base,destination;
    size_t moved=0;
    void Move(const fs::path& relative) {
        CheckMaintenancePath(base,relative);
        const auto src=base/relative;
        if(!fs::exists(src))return;
        const auto dest=destination/relative;
        fs::create_directories(dest.parent_path());
        fs::rename(src,dest);
        ++moved;
    }
    template<class Match> void Files(const fs::path& relative,Match match) {
        if(!relative.empty())CheckMaintenancePath(base,relative);
        const auto dir=base/relative;
        if(!fs::exists(dir))return;
        std::vector<fs::path> names;
        for(const auto& entry:fs::directory_iterator(dir)) {
            if(match(entry.path().filename().u8string()))names.push_back(entry.path().filename());
        }
        for(const auto& name:names)Move(relative/name);
    }
};
inline bool JsonPrefix(const std::string& name,const std::string& prefix) {
    return name.size()>prefix.size()+5 && name.compare(0,prefix.size(),prefix)==0 &&
        name.compare(name.size()-5,5,".json")==0;
}
inline void Legacy(Archive& a,CacheOperation op) {
    const bool all=op==CacheOperation::All;
    auto both=[&](const char* name){a.Move(name);a.Move(fs::path("cache")/name);};
    if(all||op==CacheOperation::Characters)both("characters.json");
    if(all||op==CacheOperation::AccountProgress) {
        for(const char* name:{"clears_cache.json","vault_cache.json","owned_skins.json"})both(name);
        auto account=[](const std::string& name){
            return JsonPrefix(name,"ach_progress_")||JsonPrefix(name,"clears_cache_")||
                JsonPrefix(name,"vault_cache_")||JsonPrefix(name,"owned_skins_");
        };
        a.Files({},account);a.Files("cache",account);
        a.Move("Skinventory/owned_skins.json");
        a.Files("Skinventory",[](const std::string& name){return JsonPrefix(name,"owned_skins_");});
    }
    if(all||op==CacheOperation::AchievementCatalog)
        for(const char* name:{"ach_name_index.json","ach_waypoints.json","hero_challenges.json",
                             "ach_defs.json","ach_groups.json"})both(name);
    if(all||op==CacheOperation::PublicCatalog) {
        for(const char* name:{"spec_cache.json","trait_cache.json","skill_cache.json","itemstat_cache.json",
                             "dye_cache.json","item_cache.json","item_name_cache.json","skin_cache.json",
                             "wiki_special_event.json"})both(name);
        for(const char* name:{"Skinventory/skin_cache.json","Skinventory/item_map_v2.json","Skinventory/price_cache_v2.json",
                             "Skinventory/item_map.json","Skinventory/price_cache.json"})a.Move(name);
    }
    if(all) {a.Move("icons");a.Move("Skinventory/wiki_cache");}
}
inline void Artwork(Archive& a) {
    a.Move("cache/textures");a.Move("icons");
    auto image=[](const std::string& name){
        const auto dot=name.find_last_of('.');
        if(dot==std::string::npos)return false;
        const auto ext=name.substr(dot);
        return ext==".png"||ext==".jpg"||ext==".jpeg"||name=="wiki_failed.json";
    };
    a.Files("cache/wiki_images",image);
    a.Files("Skinventory/wiki_cache",image);
}
inline void InvalidateDatabase(const fs::path& path,CacheOperation op) {
    if(!fs::exists(path))return;
    std::lock_guard<std::mutex> lock(Cache::Mutex());
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
    // Do not migrate or edit a foreign, damaged or unsupported database.
    const auto version=db.Scalar("PRAGMA user_version");
    if(db.Scalar("PRAGMA application_id")!=Cache::kApplicationId ||
       (version!=Cache::kSchemaVersion && version!=17 && version!=16 && version!=15 && version!=14 && version!=13 && version!=8))
        throw std::runtime_error("Unsupported cache database; use Rebuild all cache to quarantine it");
    // Upgrade known older schemas before selective operations.
    Cache::Initialize(db);
    Database::Transaction tx(db);
    switch(op) {
    case CacheOperation::Artwork:
        db.Exec("DELETE FROM wiki_skin_failures; DELETE FROM wiki_currency_failures; INSERT OR IGNORE INTO wiki_cache_state VALUES('failures');");break;
    case CacheOperation::Characters:
        db.Exec("DELETE FROM character_rows; DELETE FROM character_snapshot;");break;
    case CacheOperation::AccountProgress:
        db.Exec("DELETE FROM achievement_snapshots; DELETE FROM ownership_snapshots; DELETE FROM clears_snapshots; DELETE FROM vault_snapshots;");break;
    case CacheOperation::AchievementCatalog:
        db.Exec("DELETE FROM achievement_names;"
                "DELETE FROM achievement_definitions; DELETE FROM achievement_groups; DELETE FROM achievement_categories;"
                "DELETE FROM reference_freshness WHERE dataset IN ('ach_name_index','ach_name_index_verified','ach_defs','ach_groups');");break;
    case CacheOperation::PublicCatalog:
        // Item identities are shared by metadata and Commerce. Invalidate them
        // together; account ownership remains independent of catalog records.
        db.Exec("DELETE FROM wiki_event_starts; DELETE FROM wiki_event_snapshot; DELETE FROM reference_freshness WHERE dataset IN ('wiki_event_timers','wiki_event_timers_attempt');"
                "DELETE FROM public_item_index; DELETE FROM public_upgrade_items; DELETE FROM public_vault_objectives; DELETE FROM public_pets; DELETE FROM public_legends;"
                "DELETE FROM reference_freshness WHERE dataset IN ('public_upgrades','public_upgrades_attempt','public_vault','public_vault_attempt','public_pets','public_pets_attempt','public_legends','public_legends_attempt','public_stats','public_stats_attempt','public_dyes','public_dyes_attempt','public_item_discovery','public_item_discovery_attempt','public_skins','public_skins_attempt');"
                "DELETE FROM commerce_skin_price_choices; DELETE FROM commerce_item_prices;"
                "DELETE FROM commerce_skin_items; DELETE FROM commerce_item_name_ids; DELETE FROM commerce_vendor_prices;"
                "DELETE FROM item_name_aliases; DELETE FROM reference_items; DELETE FROM skins;"
                "DELETE FROM reference_specializations; DELETE FROM reference_traits; DELETE FROM reference_skills;"
                "DELETE FROM reference_itemstats; DELETE FROM reference_dyes; DELETE FROM reference_metadata_snapshots;"
                "DELETE FROM reference_freshness WHERE dataset IN ('spec_cache','trait_cache','skill_cache','itemstat_cache','dye_cache',"
                "'item_cache','item_name_cache','skin_catalog','skin_fashion','commerce_item_map','commerce_prices');");break;
    default: throw std::runtime_error("Invalid selective operation");
    }
    db.Verify();tx.Commit();
}
} // namespace MaintenanceDetail

// Invoke once before any subsystem opens the cache or starts a worker. The
// request is consumed before mutation; partial failures are reported, not
// silently retried against data rebuilt later in the same session.
inline MaintenanceResult RunStartupMaintenance(const std::string& root) {
    namespace fs=std::filesystem;
    MaintenanceResult result;
    if(!MaintenancePending(root))return result;
    result.attempted=true;
    try {
        const Paths paths(root);
        const auto base=fs::canonical(paths.base);
        CheckMaintenancePath(base,"config/cache-maintenance-request/operation");
        const auto op=ParseCacheOperation(ReadMigrationBytes(paths.CacheMaintenanceRequest()/"operation"));
        if(!op)throw std::runtime_error("Invalid maintenance request");
        CheckMaintenancePath(base,"backups/cache-quarantine");
        fs::create_directories(paths.CacheQuarantine());
        fs::path destination;
        for(unsigned n=0;n<100;++n) {
            const auto candidate=paths.CacheQuarantine()/("cache-"+std::to_string(
                std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(n));
            if(fs::create_directory(candidate)){destination=candidate;break;}
        }
        if(destination.empty())throw std::runtime_error("Cannot allocate cache quarantine");
        fs::rename(paths.CacheMaintenanceRequest(),destination/"request");
        MaintenanceDetail::Archive archive{base,destination};
        try {
            if(*op==CacheOperation::All) {
                // Atomic rename of the canonical cache, including any SQLite
                // rollback/WAL sidecars. Never open a corrupt DB to clear it.
                archive.Move("cache");
                MaintenanceDetail::Legacy(archive,*op);
            } else if(*op==CacheOperation::Artwork) {
                MaintenanceDetail::Artwork(archive);
                CheckMaintenancePath(base,"cache/cache.db");
                MaintenanceDetail::InvalidateDatabase(paths.Cache(),*op);
            } else {
                MaintenanceDetail::Legacy(archive,*op);
                CheckMaintenancePath(base,"cache/cache.db");
                MaintenanceDetail::InvalidateDatabase(paths.Cache(),*op);
            }
            result.success=true;
            result.message="Cache maintenance completed. Normal fetches will rebuild missing data. ";
        }catch(const std::exception& e) {
            result.message=std::string("Cache maintenance incomplete: ")+e.what()+". Previously moved files were preserved. ";
        }
        result.message+="Quarantine: "+destination.u8string();
        try{WriteMigrationBytes(destination/"result.txt",result.message);}catch(...){}
    }catch(const std::exception& e){result.message=std::string("Cache maintenance not completed: ")+e.what();}
    return result;
}
} // namespace AlterEgo::Storage
