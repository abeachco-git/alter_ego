#pragma once
#include "CacheDatabase.h"
#include "NativeLibrarySchema.h"
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace AlterEgo::Storage {
struct DatasetDiagnostic {
    std::string label;
    int64_t records=0;
    int64_t refreshedAt=0;
};
struct DatabaseDiagnostic {
    std::string status="Not inspected";
    uintmax_t bytes=0;
    int64_t schema=0;
    bool healthy=false;
};
struct CacheDiagnostics {
    DatabaseDiagnostic library,cache,accounts;
    uintmax_t durableFileBytes=0,backupBytes=0;
    bool durableFilesComplete=true,backupsComplete=true;
    uintmax_t artworkBytes=0;
    bool artworkComplete=true;
    int64_t inspectedAt=0;
    std::vector<DatasetDiagnostic> datasets;
};
inline uintmax_t ArtworkSize(const std::filesystem::path& path,bool& complete) {
    std::error_code ec;
    if(!std::filesystem::exists(path,ec)) {
        if(ec)complete=false;
        return 0;
    }
    uintmax_t bytes=0;
    // Never follow directory symlinks or count a symlink target as artwork.
    std::filesystem::recursive_directory_iterator it(path,
        std::filesystem::directory_options::skip_permission_denied,ec),end;
    if(ec)complete=false;
    while(it!=end&&!ec) {
        auto status=it->symlink_status(ec);
        if(ec){complete=false;break;}
        if(std::filesystem::is_regular_file(status)) {
            const auto size=it->file_size(ec);
            if(ec){complete=false;break;}
            bytes+=size;
        }
        it.increment(ec);
    }
    if(ec)complete=false;
    return bytes;
}
inline DatabaseDiagnostic InspectDatabase(const std::filesystem::path& path,int identity,int version,
                                        bool cache,std::vector<DatasetDiagnostic>& datasets) {
    DatabaseDiagnostic result;
    try {
        if(!std::filesystem::exists(path)){result.status="Not created";return result;}
        result.bytes=std::filesystem::file_size(path);
        // Explicit read-only diagnostics must not create, migrate, recover,
        // or change journaling on either database.
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::ReadOnly);
        result.schema=db.Scalar("PRAGMA user_version");
        if(db.Scalar("PRAGMA application_id")!=identity) {
            result.status="Unexpected application identity";return result;
        }
        if(result.schema!=version) {
            result.status="Schema differs from current version";return result;
        }
        db.Exec("BEGIN");
        auto check=db.Prepare("PRAGMA quick_check");
        if(!check.Step()||check.Text(0)!="ok"||check.Step()) {
            result.status="Quick check failed";return result;
        }
        auto foreign=db.Prepare("PRAGMA foreign_key_check");
        if(foreign.Step()){result.status="Foreign key check failed";return result;}
        if(cache) {
            struct Entry { const char* label; const char* count; const char* freshness; };
            const Entry entries[]={
                {"Achievement progress","SELECT count(*) FROM achievement_progress",nullptr},
                {"Verified achievement progress","SELECT count(*) FROM verified_achievement_progress",nullptr},
                {"Public Vault season","SELECT count(*) FROM public_vault_season",nullptr},
                {"Achievement definitions","SELECT count(*) FROM achievement_definitions","ach_defs"},
                {"Achievement names","SELECT count(*) FROM achievement_names","ach_name_index_verified"},
                {"Achievement categories","SELECT count(*) FROM achievement_categories","ach_groups"},
                {"Skin catalog","SELECT count(*) FROM skins WHERE catalog=1","skin_catalog"},
                {"Fashion skins","SELECT count(*) FROM skins WHERE fashion=1","skin_fashion"},
                {"Owned skin records","SELECT count(*) FROM owned_skins",nullptr},
                {"Commerce mappings","SELECT count(*) FROM commerce_skin_items","commerce_item_map"},
                {"Commerce prices","SELECT count(*) FROM commerce_skin_price_choices","commerce_prices"},
                {"Items","SELECT count(*) FROM reference_items WHERE complete=1","item_cache"},
                {"Item aliases","SELECT count(*) FROM item_name_aliases","item_name_cache"},
                {"Equipment catalog","SELECT count(*) FROM public_upgrade_items","public_upgrades"},
                {"Discovered public items","SELECT count(*) FROM public_item_index","public_item_discovery"},
                {"Verified skin discovery","SELECT count(*) FROM skins WHERE catalog=1","public_skins"},
                {"Public Vault definitions","SELECT count(*) FROM public_vault_objectives","public_vault"},
                {"Ranger pets","SELECT count(*) FROM public_pets","public_pets"},
                {"Revenant legends","SELECT count(*) FROM public_legends","public_legends"},
                {"Skills","SELECT count(*) FROM reference_skills","skill_cache"},
                {"Traits","SELECT count(*) FROM reference_traits","trait_cache"},
                {"Specializations","SELECT count(*) FROM reference_specializations","spec_cache"},
                {"Item stats","SELECT count(*) FROM reference_itemstats","itemstat_cache"},
                {"Dyes","SELECT count(*) FROM reference_dyes","dye_cache"},
                {"Characters","SELECT count(*) FROM character_rows",nullptr},
                {"Native roster characters","SELECT count(*) FROM native_rosters",nullptr},
                {"Inventory scopes","SELECT count(*) FROM native_inventory_scopes",nullptr},
                {"Inventory item locations","SELECT count(*) FROM native_inventory_slots",nullptr},
                {"Inventory sockets","SELECT count(*) FROM native_inventory_sockets",nullptr},
                {"Wallet balances","SELECT count(*) FROM native_wallet",nullptr},
                {"Currency definitions","SELECT count(*) FROM native_currencies",nullptr},
                {"Guild banks","SELECT count(*) FROM native_guilds",nullptr},
                {"Guild bank locations","SELECT count(*) FROM native_guild_slots",nullptr},
                {"Clears entries","SELECT count(*) FROM clears_entries",nullptr},
                {"Vault objectives","SELECT count(*) FROM vault_objectives",nullptr},
                {"Wiki event timer starts","SELECT count(*) FROM wiki_event_starts","wiki_event_timers"},
                {"Wiki acquisition metadata","SELECT count(*) FROM wiki_skin_metadata",nullptr},
                {"Wiki skin failures","SELECT count(*) FROM wiki_skin_failures",nullptr},
                {"Wiki currency failures","SELECT count(*) FROM wiki_currency_failures",nullptr}
            };
            auto fresh=db.Prepare("SELECT saved_at FROM reference_freshness WHERE dataset=?");
            for(const auto& e:entries) {
                DatasetDiagnostic row;
                row.label=e.label;row.records=db.Scalar(e.count);
                if(e.freshness) {
                    fresh.Bind(1,std::string(e.freshness));
                    if(fresh.Step())row.refreshedAt=fresh.Integer(0);
                    fresh.Reset();
                } else if(row.label=="Achievement progress") {
                    row.refreshedAt=db.Scalar("SELECT COALESCE(MAX(fetched_at),0) FROM achievement_snapshots");
                } else if(row.label=="Verified achievement progress") {
                    row.refreshedAt=db.Scalar("SELECT COALESCE(MAX(fetched_at),0) FROM verified_achievement_snapshots");
                } else if(row.label=="Public Vault season") {
                    row.refreshedAt=db.Scalar("SELECT COALESCE(MAX(verified_at),0) FROM public_vault_season");
                } else if(row.label=="Clears entries") {
                    row.refreshedAt=db.Scalar("SELECT COALESCE(MAX(fetch_time),0) FROM clears_snapshots");
                } else if(row.label=="Vault objectives") {
                    row.refreshedAt=db.Scalar("SELECT COALESCE(MAX(fetch_time),0) FROM vault_snapshots");
                } else if(row.label=="Characters") {
                    row.refreshedAt=db.Scalar("SELECT COALESCE(MAX(last_updated),0) FROM character_snapshot");
                } else if(row.label=="Owned skin records") {
                    row.refreshedAt=db.Scalar("SELECT COALESCE(MAX(fetched_at),0) FROM ownership_snapshots");
                }
                datasets.push_back(std::move(row));
            }
        }
        db.Exec("COMMIT");
        result.status="Quick check passed";result.healthy=true;
    }catch(const std::exception& e){result.status=e.what();}
    return result;
}
inline CacheDiagnostics InspectStorage(const std::string& root) {
    CacheDiagnostics out;
    if(root.empty())return out;
    const Paths paths(root);
    out.library=InspectDatabase(paths.Library(),NativeLibrary::kApplicationId,NativeLibrary::kSchemaVersion,false,out.datasets);
    out.accounts=InspectDatabase(paths.Accounts(),kAccountRegistryApplicationId,kAccountRegistrySchemaVersion,false,out.datasets);
    for(const auto& dir:{paths.ConfigDirectory(),paths.LibraryDirectory(),paths.Portraits(),paths.Fonts()})
        out.durableFileBytes+=ArtworkSize(dir,out.durableFilesComplete);
    const auto primaryBytes=out.library.bytes+out.accounts.bytes;
    if(out.durableFileBytes>=primaryBytes)out.durableFileBytes-=primaryBytes;
    else out.durableFilesComplete=false;
    out.backupBytes=ArtworkSize(paths.base/"backups",out.backupsComplete);
    {
        std::lock_guard<std::mutex> lock(Cache::Mutex());
        out.cache=InspectDatabase(paths.Cache(),Cache::kApplicationId,Cache::kSchemaVersion,true,out.datasets);
    }
    out.artworkBytes=ArtworkSize(paths.CacheDirectory()/"textures",out.artworkComplete)+
        ArtworkSize(paths.CacheDirectory()/"wiki_images",out.artworkComplete);
    out.inspectedAt=std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return out;
}
} // namespace AlterEgo::Storage
