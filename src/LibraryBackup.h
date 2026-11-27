#pragma once
// Validated, selective Alter Ego backup/restore; no UI dependencies.
// Existing build IDs are the only build merge identity. Fashion uses its IDs.
// Merge preserves existing preferences and adds absent files; Replace overwrites
// only files actually present in selected import collections.
// Nothing is written until all selected entries have passed validation.
#include "BackupZip.h"
#include "UntrustedInput.h"
#include "FashionModel.h"
#include "NativeLibraryPortable.h"
#include "ComposableLoadouts.h"
#include "CharacterActivity.h"
#include "CharacterPreferenceStore.h"
#include <nlohmann/json.hpp>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace AlterEgo { namespace Backup {

enum Group : unsigned { Builds = 1, Fashion = 2, Other = 4 };
using FileMap = BackupZip::Entries;

inline bool IsOtherJson(const std::string& name) {
    static const std::set<std::string> names = {
        "settings.json", "session.json", "char_sort.json",
        "ach_tracked.json", "login_times.json", "character_logins.json", "character_loadouts.json", "clears_ui.json"};
    return names.count(name) != 0;
}
inline bool EndsWithIgnoreCase(std::string value, std::string suffix) {
    if (value.size() < suffix.size()) return false;
    return AlterEgo::Fashion::ContainsIgnoreCase(
        value.substr(value.size() - suffix.size()), suffix);
}
inline bool UserAsset(const std::string& name) {
    if (!BackupZip::SafeName(name)) return false;
    for (const auto& dir : {"portraits/", "fonts/"}) {
        if (name.rfind(dir, 0) != 0) continue;
        auto tail = name.substr(std::string(dir).size());
        if (tail.empty() || tail.find('/') != std::string::npos) return false;
        if (std::string(dir) == "portraits/")
            return EndsWithIgnoreCase(tail, ".png") || EndsWithIgnoreCase(tail, ".jpg") ||
                   EndsWithIgnoreCase(tail, ".jpeg");
        return EndsWithIgnoreCase(tail, ".ttf") || EndsWithIgnoreCase(tail, ".otf");
    }
    return false;
}
inline bool SupportedName(const std::string& name) {
    return name == "manifest.json" || name == "build_library.json" ||
           name == "library.json" ||
           name == "fashion_library.json" || IsOtherJson(name) || UserAsset(name);
}
inline unsigned GroupForName(const std::string& name) {
    if (name == "library.json") return Builds | Fashion;
    if (name == "build_library.json") return Builds;
    if (name == "fashion_library.json") return Fashion;
    return IsOtherJson(name) || UserAsset(name) ? static_cast<unsigned>(Other) : 0u;
}
inline bool Exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::u8path(path), ec);
}
inline std::string FilePath(const std::string& dir, const std::string& name) {
    return Storage::Resolve(dir, name);
}
inline bool ReadExisting(const std::string& dir, const std::string& name,
                         std::string& content, std::string& error) {
    if (!Exists(FilePath(dir, name))) { content.clear(); return true; }
    return BackupZip::ReadBytes(FilePath(dir, name), content, error, BackupZip::kMaxEntryBytes);
}
inline nlohmann::json Manifest(const FileMap& files, unsigned mask) {
    nlohmann::json manifest = {
        {"format", "alter_ego_backup"}, {"version", 1},
        {"created", static_cast<int64_t>(std::time(nullptr))},
        {"selected", {{"builds", bool(mask & Builds)},
                      {"fashion", bool(mask & Fashion)}, {"other", bool(mask & Other)}}},
        {"files", nlohmann::json::array()}};
    for (const auto& [name, contents] : files) {
        (void)contents;
        manifest["files"].push_back(name);
    }
    return manifest;
}
inline bool IsJsonName(const std::string& name) {
    return EndsWithIgnoreCase(name, ".json");
}
inline bool ValidateCollection(const std::string& name, const std::string& bytes,
                               size_t& count, std::string& error) {
    count = 0;
    if (!IsJsonName(name)) return UserAsset(name);
    nlohmann::json j;
    try { j = Untrusted::Parse(bytes,Untrusted::Library); }
    catch (const std::exception& e) {
        error = name + ": invalid JSON: " + e.what(); return false;
    }
    if (!j.is_object()) { error = name + ": expected a JSON object."; return false; }
    if(name=="character_loadouts.json"){
        try{count=CharacterLoadout::DecodePortable(j).size();return true;}
        catch(const std::exception& e){error=name+": "+e.what();return false;}
    }
    if(name=="character_logins.json"){
        try{count=CharacterActivity::DecodeLoginHistory(j).size();return true;}
        catch(const std::exception& e){error=name+": "+e.what();return false;}
    }
    if (name == "library.json") {
        NativeLibrary::PortableLibrary data;
        if (!NativeLibrary::ParsePortableLibrary(j,data,error)) return false;
        count=data.data.loadouts.loadouts.size();return true;
    }
    if (name == "build_library.json") {
        if (!j.contains("builds") || !j["builds"].is_array()) {
            error = "Build library requires a builds array."; return false;
        }
        std::set<std::string> ids;
        for (const auto& build : j["builds"]) {
            if (!build.is_object() || !build.contains("id") ||
                !build["id"].is_string() || build["id"].get<std::string>().empty()) {
                error = "Build library contains an entry without a saved ID."; return false;
            }
            if (!ids.insert(build["id"].get<std::string>()).second) {
                error = "Build library contains duplicate IDs."; return false;
            }
            ++count;
        }
    } else if (name == "fashion_library.json") {
        if (!j.contains("looks") || !j["looks"].is_array()) {
            error = "Fashion library requires a looks array."; return false;
        }
        std::set<std::string> ids;
        for (const auto& value : j["looks"]) {
            AlterEgo::Fashion::Look look;
            try {
                if (!AlterEgo::Fashion::FromJson(value, look) || look.name.empty()) {
                    error = "Fashion library contains an invalid appearance."; return false;
                }
            } catch (const std::exception& e) {
                error = std::string("Invalid fashion entry: ") + e.what(); return false;
            }
            if (!ids.insert(look.id).second) {
                error = "Fashion library contains duplicate IDs."; return false;
            }
            ++count;
        }
    }
    return true;
}
inline bool Collect(const std::string& dir, unsigned selected, FileMap& files,
                    std::string& error) {
    files.clear();
    if (!(selected & (Builds | Fashion | Other))) {
        error = "Select at least one collection."; return false;
    }
    auto append = [&](const std::string& name, const std::string& defaultData) -> bool {
        std::string data;
        if (!ReadExisting(dir, name, data, error)) return false;
        if (data.empty()) data = defaultData;
        if (!data.empty()) {
            size_t count = 0;
            if (!ValidateCollection(name, data, count, error)) return false;
            files.emplace(name, std::move(data));
        }
        return true;
    };
    if (selected & (Builds|Fashion)) {
        NativeLibrary::Snapshot state;
        auto& store=NativeLibrary::RuntimeStore();
        if(!store.Open(dir,error)||!store.Read(state,error))return false;
        files["library.json"]=NativeLibrary::ExportPortableLibrary(state,selected&Builds,selected&Fashion).dump(2);
    }
    if (selected & Other) {
        for (const auto& name : {"settings.json", "session.json", "char_sort.json",
                                 "ach_tracked.json", "login_times.json", "clears_ui.json"}) {
            if (!append(name, "")) return false;
        }
        // Generate a portable projection from the flushed authoritative ledger;
        // never include the activity/verification ledger itself in a backup.
        try{
            std::map<CharacterActivity::Key,int64_t> logins;
            std::string ledger,pending;
            if(!ReadExisting(dir,"character_activity.json",ledger,error)||
               !ReadExisting(dir,"character_logins.json",pending,error))return false;
            if(!ledger.empty()){
                CharacterActivity::Tracker tracker;
                tracker.Load(nlohmann::json::parse(ledger));logins=tracker.HistoricalLogins();
            }
            if(!pending.empty())CharacterActivity::MergeLoginHistory(logins,
                CharacterActivity::DecodeLoginHistory(nlohmann::json::parse(pending)));
            if(!logins.empty())files["character_logins.json"]=CharacterActivity::EncodeLoginHistory(logins).dump(2);
        }catch(const std::exception& e){error=std::string("Cannot export login history: ")+e.what();return false;}
        // Export only portable preferences, never the account registry or
        // credentials. Local numeric IDs are not installation-independent.
        try{
            CharacterLoadout::PortableChoices preferences;
            const auto accountDb=Storage::Paths(dir).Accounts();
            if(std::filesystem::exists(accountDb)){
                Database::Connection db(accountDb.u8string(),Database::Connection::OpenMode::ReadOnly);
                const auto version=db.Scalar("PRAGMA user_version");
                if(db.Scalar("PRAGMA application_id")!=Storage::kAccountRegistryApplicationId||
                    version<1||version>Storage::kAccountRegistrySchemaVersion)
                    throw std::runtime_error("Invalid account registry");
                if(version>=6){
                    db.Exec("BEGIN");
                    preferences=CharacterLoadout::ExportPortable(db);
                    db.Exec("COMMIT");
                }
            }
            std::string pending;
            if(!ReadExisting(dir,"character_loadouts.json",pending,error))return false;
            if(!pending.empty()){
                const auto records=CharacterLoadout::DecodePortable(Untrusted::Parse(pending,Untrusted::Library));
                preferences.insert(records.begin(),records.end());
            }
            if(!preferences.empty())files["character_loadouts.json"]=CharacterLoadout::EncodePortable(preferences).dump(2);
        }catch(const std::exception& e){error=std::string("Cannot export character Loadout preferences: ")+e.what();return false;}
        for (const auto& subfolder : {"portraits", "fonts"}) {
            const auto folder = std::filesystem::u8path(dir) / subfolder;
            std::error_code ec;
            if (!std::filesystem::is_directory(folder, ec)) continue;
            for (auto it = std::filesystem::directory_iterator(folder, ec);
                 !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const std::string filename = it->path().filename().u8string();
                const std::string name = std::string(subfolder) + "/" + filename;
                if (!UserAsset(name)) continue;
                std::string data;
                if (!ReadExisting(dir, name, data, error)) return false;
                if (data.empty()) continue;
                files.emplace(name, std::move(data));
                if (files.size() > BackupZip::kMaxEntries) {
                    error = "Too many custom portraits or fonts for one archive."; return false;
                }
            }
            if (ec) { error = "Could not enumerate custom user assets."; return false; }
        }
    }
    return true;
}
// Exports cannot overwrite live files or protected recovery snapshots.
inline bool ExportDestination(const std::string& dir,const std::string& destination,std::string& error) {
    namespace fs=std::filesystem;
    try{
        const auto base=fs::weakly_canonical(fs::u8path(dir));
        const auto target=fs::weakly_canonical(fs::u8path(destination));
        auto equal=[](const fs::path& a,const fs::path& b){
#ifdef _WIN32
            return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;
#else
            return a==b;
#endif
        };
        auto a=base.begin(),b=target.begin();
        while(a!=base.end()&&b!=target.end()&&equal(*a,*b)){++a;++b;}
        if(a==base.end()){
            if(b==target.end()||!equal(*b,"backups"))
                throw std::runtime_error("Choose an export outside the addon data folder or inside backups.");
            ++b;
            if(b==target.end()||equal(*b,"migration")||equal(*b,"recovery")||equal(*b,"cache-quarantine"))
                throw std::runtime_error("Do not overwrite protected recovery or migration backups.");
        }
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
inline bool Export(const std::string& dir, unsigned selected,
                   const std::string& destination, bool asZip, std::string& error) {
    if(!ExportDestination(dir,destination,error))return false;
    FileMap files;
    if (!Collect(dir, selected, files, error)) return false;
    if (!asZip) {
        // Portable JSON contains the selected collections and dependencies.
        if (files.size() != 1 || selected == Other) {
            error = "Individual JSON export requires Builds or Fashion only."; return false;
        }
        const auto& bytes = files.begin()->second;
        return Storage::ReplaceFile(std::filesystem::u8path(destination),bytes,error);
    }
    files["manifest.json"] = Manifest(files, selected).dump(2);
    return BackupZip::WriteStored(files, destination, error);
}

struct Package {
    FileMap files;
    unsigned available = 0;
    size_t buildCount = 0, equipmentSetCount = 0;
    size_t fashionCount = 0;
    bool zip = false;
};
inline std::string DetectOtherJsonType(const nlohmann::json& j) {
    // Individual backups may have been manually renamed. Prefer unmistakable
    // structural markers before falling back to the historical filename.
    if(j.value("format",std::string())=="alter_ego_character_logins")return "character_logins.json";
    if(j.value("format",std::string())=="alter_ego_character_loadouts")return "character_loadouts.json";
    if (j.contains("show_qa_icon") || j.contains("auto_refresh_enabled") ||
        j.contains("font")) return "settings.json";
    if (j.contains("pinned") && j["pinned"].is_array()) return "ach_tracked.json";
    if (j.contains("sort_mode") && j["sort_mode"].is_number_integer()) return "char_sort.json";
    if (j.contains("main_tab") && j["main_tab"].is_number_integer()) return "session.json";
    if (j.contains("expanded") && j["expanded"].is_array()) return "clears_ui.json";
    if (!j.empty() && std::all_of(j.begin(), j.end(),
        [](const nlohmann::json& value) { return value.is_number_integer(); }))
        return "login_times.json";
    return {};
}
inline bool ReadPackage(const std::string& source, Package& package, std::string& error) {
    package = Package{};
    std::string raw;
    if (!BackupZip::ReadBytes(source, raw, error)) return false;
    if (raw.size() >= 4 && raw.compare(0, 4, "PK\x03\x04", 4) == 0) {
        package.zip = true;
        if (!BackupZip::ReadStored(raw, package.files, error)) return false;
    } else {
        nlohmann::json j;
        try { j = Untrusted::Parse(raw,Untrusted::Library); }
        catch (...) { error = "Not a ZIP archive or valid JSON file."; return false; }
        if (!j.is_object()) { error = "JSON import must contain an object."; return false; }
        std::string name;
        if (j.contains("format") && j["format"].is_string() &&
            j["format"] == NativeLibrary::kLibraryFormat)
            name = "library.json";
        else if (j.contains("builds") && j["builds"].is_array()) name = "build_library.json";
        else if (j.contains("looks") && j["looks"].is_array()) name = "fashion_library.json";
        else {
            name = DetectOtherJsonType(j);
            if (name.empty()) name = std::filesystem::u8path(source).filename().u8string();
            if (!IsOtherJson(name)) {
                error = "Unknown JSON structure. Use a build/fashion library or recognized settings JSON."; return false;
            }
        }
        package.files.emplace(name, std::move(raw));
    }
    if (package.files.empty()) { error = "Backup contains no files."; return false; }
    if (package.files.count("manifest.json")) {
        try {
            const auto m = Untrusted::Parse(package.files.at("manifest.json"),Untrusted::Clipboard);
            if (!m.is_object() || m.value("format", std::string()) != "alter_ego_backup" ||
                m.value("version", 0) != 1) {
                error = "Unsupported backup manifest."; return false;
            }
            if (m.contains("files") && m["files"].is_array()) {
                for (const auto& name : m["files"]) {
                    if (!name.is_string() || !package.files.count(name.get<std::string>())) {
                        error = "Backup manifest lists a missing file."; return false;
                    }
                }
            }
        } catch (...) { error = "Invalid backup manifest."; return false; }
        package.files.erase("manifest.json");
    }
    if (package.files.empty()) { error = "Backup contains no restorable collections."; return false; }
    for (const auto& [name, bytes] : package.files) {
        if (!SupportedName(name) || name == "manifest.json" || !GroupForName(name)) {
            error = "Unsupported or unsafe backup file: " + name; return false;
        }
        size_t count = 0;
        if (!ValidateCollection(name, bytes, count, error)) return false;
        if(name=="library.json") {
            NativeLibrary::PortableLibrary data;
            if(!NativeLibrary::ParsePortableLibrary(Untrusted::Parse(bytes,Untrusted::Library),data,error))return false;
            if(data.loadouts){
                package.available|=Builds;package.buildCount=data.data.loadouts.loadouts.size();
                package.equipmentSetCount=data.data.loadouts.equipment_sets.size();
            }
            if(data.fashion){package.available|=Fashion;package.fashionCount=data.data.fashion.size();}
        } else {
            package.available |= GroupForName(name);
            if (name == "build_library.json") package.buildCount = count;
            if (name == "fashion_library.json") package.fashionCount = count;
        }
    }
    if(package.files.count("library.json")&&(package.files.count("build_library.json")||package.files.count("fashion_library.json"))) {
        error="Backup mixes incompatible library representations.";return false;
    }
    return package.available != 0;
}
struct Plan {
    FileMap writeFiles;
    NativeLibrary::Snapshot before, after;
    bool libraryChanged=false;
    unsigned selected = 0;
    bool replace = false;
    size_t buildAdded = 0, buildSkipped = 0, equipmentSetAdded = 0, equipmentSetSkipped = 0;
    size_t fashionAdded = 0, fashionSkipped = 0, fashionSimilar = 0;
    size_t otherWritten = 0, otherSkipped = 0;
};
inline bool Prepare(const std::string& dir, const Package& package,
                    unsigned selected, bool replace, Plan& plan, std::string& error) {
    plan = Plan{};
    plan.selected = selected;
    plan.replace = replace;
    if (!selected || (selected & ~package.available)) {
        error = "Selected collection is not present in this backup."; return false;
    }
    if(selected&(Builds|Fashion)) {
        auto& store=NativeLibrary::RuntimeStore();
        if(!store.Open(dir,error)||!store.Read(plan.before,error))return false;
        NativeLibrary::PortableLibrary input;
        if(package.files.count("library.json")) {
            if(!NativeLibrary::ParsePortableLibrary(Untrusted::Parse(package.files.at("library.json"),Untrusted::Library),input,error))return false;
        } else {
            if(selected&Builds)
                if(!NativeLibrary::ParsePortableLibrary(Untrusted::Parse(package.files.at("build_library.json"),Untrusted::Library),input,error))return false;
            if(selected&Fashion) {
                NativeLibrary::PortableLibrary f;
                if(!NativeLibrary::ParsePortableLibrary(Untrusted::Parse(package.files.at("fashion_library.json"),Untrusted::Library),f,error))return false;
                input.fashion=true;input.data.fashion=std::move(f.data.fashion);
            }
        }
        NativeLibrary::ImportCounts counts;
        if(!NativeLibrary::MergePortableLibrary(plan.before,input,selected&Builds,selected&Fashion,replace,plan.after,counts,error))return false;
        plan.libraryChanged=true;
        plan.buildAdded=counts.loadoutsAdded;plan.buildSkipped=counts.loadoutsSkipped;
        plan.equipmentSetAdded=counts.equipmentSetsAdded;plan.equipmentSetSkipped=counts.equipmentSetsSkipped;
        plan.fashionAdded=counts.fashionAdded;plan.fashionSkipped=counts.fashionSkipped;plan.fashionSimilar=counts.fashionSimilar;
    }
    for(const auto& [name,bytes]:package.files) {
        if(!(selected&Other)||GroupForName(name)!=Other)continue;
        if(name=="character_loadouts.json"){
            try{
                auto preferences=CharacterLoadout::DecodePortable(Untrusted::Parse(bytes,Untrusted::Library));
                std::string pending;if(!ReadExisting(dir,name,pending,error))return false;
                if(!pending.empty()){
                    const auto records=CharacterLoadout::DecodePortable(Untrusted::Parse(pending,Untrusted::Library));
                    preferences.insert(records.begin(),records.end());
                }
                plan.writeFiles[name]=CharacterLoadout::EncodePortable(preferences).dump(2);++plan.otherWritten;
            }catch(const std::exception& e){error=std::string("Cannot import character Loadout preferences: ")+e.what();return false;}
            continue;
        }
        if(name=="character_logins.json"){
            // Preserve even pending history if another restore is applied before
            // its durable activity-ledger write succeeds.
            try{
                auto logins=CharacterActivity::DecodeLoginHistory(Untrusted::Parse(bytes,Untrusted::Library));
                std::string pending;if(!ReadExisting(dir,name,pending,error))return false;
                if(!pending.empty())CharacterActivity::MergeLoginHistory(logins,CharacterActivity::DecodeLoginHistory(nlohmann::json::parse(pending)));
                plan.writeFiles[name]=CharacterActivity::EncodeLoginHistory(logins).dump(2);++plan.otherWritten;
            }catch(const std::exception& e){error=std::string("Cannot import login history: ")+e.what();return false;}
            continue;
        }
        if(!replace&&Exists(FilePath(dir,name))){++plan.otherSkipped;continue;}
        plan.writeFiles[name]=bytes;++plan.otherWritten;
    }
    return true;
}

inline bool WriteAtomic(const std::string& path,const std::string& bytes,std::string& error) {
    return Storage::ReplaceFile(std::filesystem::u8path(path),bytes,error);
}

inline bool Apply(const std::string& dir, const Plan& plan,
                  std::string& recoveryFile, std::string& error) {
    recoveryFile.clear();
    if (plan.writeFiles.empty() && !plan.libraryChanged) return true;
    FileMap before;
    std::set<std::string> absent;
    for (const auto& [name, data] : plan.writeFiles) {
        (void)data;
        std::string current;
        if (!ReadExisting(dir, name, current, error)) return false;
        if (Exists(FilePath(dir, name))) before.emplace(name, std::move(current));
        else absent.insert(name);
    }
    FileMap snapshot = before;
    if(plan.libraryChanged) snapshot["library.json"]=NativeLibrary::ExportPortableLibrary(plan.before,true,true).dump(2);
    nlohmann::json oldFiles = {{"format", "alter_ego_backup"}, {"version", 1},
        {"created", (int64_t)std::time(nullptr)}, {"files", nlohmann::json::array()},
        {"previously_absent", absent}};
    for (const auto& [name, bytes] : snapshot) {
        (void)bytes;
        oldFiles["files"].push_back(name);
    }
    snapshot["manifest.json"] = oldFiles.dump(2);
    const auto backupDir = NativeLibrary::Paths(dir).Recovery();
    std::error_code ec;
    std::filesystem::create_directories(backupDir, ec);
    if (ec) { error = "Could not create recovery backup directory."; return false; }
    const std::string prefix = "alter_ego_pre_restore_" + std::to_string(std::time(nullptr));
    auto dest = backupDir / (prefix + ".zip");
    for (unsigned n = 1; std::filesystem::exists(dest, ec); ++n)
        dest = backupDir / (prefix + "_" + std::to_string(n) + ".zip");
    recoveryFile = dest.u8string();
    if (!BackupZip::WriteStored(snapshot, recoveryFile, error)) {
        recoveryFile.clear(); return false; // never modify data without a snapshot
    }
    if(plan.libraryChanged && !NativeLibrary::RuntimeStore().Backup((dest.u8string()+".db"),error))return false;
    std::vector<std::string> committed;
    for (const auto& [name, bytes] : plan.writeFiles) {
        if (!WriteAtomic(FilePath(dir, name), bytes, error)) {
            bool rollbackFailed = false;
            for (auto it = committed.rbegin(); it != committed.rend(); ++it) {
                if (absent.count(*it)) {
                    std::filesystem::remove(std::filesystem::u8path(FilePath(dir, *it)), ec);
                    if (ec) rollbackFailed = true;
                } else {
                    std::string rollbackError;
                    if (!WriteAtomic(FilePath(dir, *it), before.at(*it), rollbackError))
                        rollbackFailed = true;
                }
            }
            if (rollbackFailed) error += " Automatic rollback failed; original files remain in " + recoveryFile;
            return false;
        }
        committed.push_back(name);
    }
    if(plan.libraryChanged && !NativeLibrary::RuntimeStore().Commit(plan.before,plan.after,error)) {
        for(auto it=committed.rbegin();it!=committed.rend();++it) {
            std::string ignored;
            if(absent.count(*it)){std::filesystem::remove(std::filesystem::u8path(FilePath(dir,*it)),ec);if(ec)error+="; restore original files from recovery ZIP";}
            else if(!WriteAtomic(FilePath(dir,*it),before.at(*it),ignored))error+="; restore original file from recovery ZIP";
        }
        return false;
    }
    return true;
}
}} // AlterEgo::Backup
