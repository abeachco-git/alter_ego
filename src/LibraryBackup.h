#pragma once
// Validated, selective Alter Ego backup/restore; no UI dependencies.
// Existing build IDs are the only build merge identity. Fashion uses its IDs.
// Merge preserves existing preferences and adds absent files; Replace overwrites
// only files actually present in selected import collections.
// Nothing is written until all selected entries have passed validation.
#include "BackupZip.h"
#include "FashionLibrary.h"
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
        "ach_tracked.json", "login_times.json", "clears_ui.json"};
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
           name == "fashion_library.json" || IsOtherJson(name) || UserAsset(name);
}
inline unsigned GroupForName(const std::string& name) {
    if (name == "build_library.json") return Builds;
    if (name == "fashion_library.json") return Fashion;
    return IsOtherJson(name) || UserAsset(name) ? static_cast<unsigned>(Other) : 0u;
}
inline bool Exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::u8path(path), ec);
}
inline std::string FilePath(const std::string& dir, const std::string& name) {
    return (std::filesystem::u8path(dir) / std::filesystem::u8path(name)).u8string();
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
    try { j = nlohmann::json::parse(bytes); }
    catch (const std::exception& e) {
        error = name + ": invalid JSON: " + e.what(); return false;
    }
    if (!j.is_object()) { error = name + ": expected a JSON object."; return false; }
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
    if ((selected & Builds) && !append("build_library.json", R"({"builds":[]})")) return false;
    if ((selected & Fashion) && !append("fashion_library.json", R"({"version":1,"looks":[]})")) return false;
    if (selected & Other) {
        for (const auto& name : {"settings.json", "session.json", "char_sort.json",
                                 "ach_tracked.json", "login_times.json", "clears_ui.json"}) {
            if (!append(name, "")) return false;
        }
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
inline bool Export(const std::string& dir, unsigned selected,
                   const std::string& destination, bool asZip, std::string& error) {
    FileMap files;
    if (!Collect(dir, selected, files, error)) return false;
    if (!asZip) {
        if (files.size() != 1 || selected == Other) {
            error = "Individual JSON export requires Builds or Fashion only."; return false;
        }
        const auto& bytes = files.begin()->second;
        std::ofstream out(destination, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
            error = "Could not export JSON file."; return false;
        }
        out.flush();
        return out.good();
    }
    files["manifest.json"] = Manifest(files, selected).dump(2);
    return BackupZip::WriteStored(files, destination, error);
}

struct Package {
    FileMap files;
    unsigned available = 0;
    size_t buildCount = 0;
    size_t fashionCount = 0;
    bool zip = false;
};
inline std::string DetectOtherJsonType(const nlohmann::json& j) {
    // Individual backups may have been manually renamed. Prefer unmistakable
    // structural markers before falling back to the historical filename.
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
        try { j = nlohmann::json::parse(raw); }
        catch (...) { error = "Not a ZIP archive or valid JSON file."; return false; }
        if (!j.is_object()) { error = "JSON import must contain an object."; return false; }
        std::string name;
        if (j.contains("builds") && j["builds"].is_array()) name = "build_library.json";
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
            const auto m = nlohmann::json::parse(package.files.at("manifest.json"));
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
        package.available |= GroupForName(name);
        if (name == "build_library.json") package.buildCount = count;
        if (name == "fashion_library.json") package.fashionCount = count;
    }
    return package.available != 0;
}
struct Plan {
    FileMap writeFiles;
    unsigned selected = 0;
    bool replace = false;
    size_t buildAdded = 0, buildSkipped = 0;
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
    for (const auto& [name, imported] : package.files) {
        if (!(GroupForName(name) & selected)) continue;
        if (name == "build_library.json" || name == "fashion_library.json") {
            std::string existing;
            if (!ReadExisting(dir, name, existing, error)) return false;
            const bool isBuild = name == "build_library.json";
            const char* key = isBuild ? "builds" : "looks";
            nlohmann::json input = nlohmann::json::parse(imported);
            nlohmann::json current;
            if (!existing.empty() && !replace) {
                try { current = nlohmann::json::parse(existing); }
                catch (...) { error = "Existing " + name + " is corrupt; merge stopped."; return false; }
                size_t unused;
                if (!ValidateCollection(name, existing, unused, error)) {
                    error = "Existing " + name + " is invalid; merge stopped. " + error;
                    return false;
                }
            } else current = {{key, nlohmann::json::array()}};
            nlohmann::json output = replace ? std::move(input) : std::move(current);
            size_t added = 0, skipped = 0, similar = 0;
            std::set<std::string> ids;
            std::set<std::string> links;
            if (!replace) {
                for (const auto& item : output[key]) {
                    ids.insert(item.at("id").get<std::string>());
                    if (!isBuild) links.insert(item.at("wardrobe_link").get<std::string>());
                }
                for (const auto& item : input[key]) {
                    const std::string id = item.at("id").get<std::string>();
                    if (ids.count(id)) { ++skipped; continue; }
                    if (!isBuild && links.count(item.at("wardrobe_link").get<std::string>()))
                        ++similar;
                    output[key].push_back(item);
                    ids.insert(id);
                    if (!isBuild) links.insert(item.at("wardrobe_link").get<std::string>());
                    ++added;
                }
            } else added = output[key].size();
            if (isBuild) { plan.buildAdded = added; plan.buildSkipped = skipped; }
            else { plan.fashionAdded = added; plan.fashionSkipped = skipped; plan.fashionSimilar = similar; }
            plan.writeFiles[name] = output.dump(2);
        } else {
            const bool present = Exists(FilePath(dir, name));
            if (!replace && present) { ++plan.otherSkipped; continue; }
            plan.writeFiles[name] = imported;
            ++plan.otherWritten;
        }
    }
    return true;
}

inline bool WriteAtomic(const std::string& path, const std::string& bytes, std::string& error) {
    const auto file = std::filesystem::u8path(path);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) { error = "Could not create backup destination directory."; return false; }
    const std::string tmp = path + ".ae_restore_tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
            error = "Could not stage restored file: " + file.filename().u8string();
            std::filesystem::remove(std::filesystem::u8path(tmp), ec);
            return false;
        }
        out.flush();
        if (!out.good()) { error = "Could not flush restored file."; return false; }
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Could not replace file: " + file.filename().u8string();
        std::filesystem::remove(std::filesystem::u8path(tmp), ec);
        return false;
    }
#else
    std::filesystem::rename(std::filesystem::u8path(tmp), file, ec);
    if (ec) {
        error = "Could not replace file: " + file.filename().u8string();
        std::filesystem::remove(std::filesystem::u8path(tmp), ec);
        return false;
    }
#endif
    return true;
}

inline bool Apply(const std::string& dir, const Plan& plan,
                  std::string& recoveryFile, std::string& error) {
    recoveryFile.clear();
    if (plan.writeFiles.empty()) return true;
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
    nlohmann::json oldFiles = {{"format", "alter_ego_backup"}, {"version", 1},
        {"created", (int64_t)std::time(nullptr)}, {"files", nlohmann::json::array()},
        {"previously_absent", absent}};
    for (const auto& [name, bytes] : before) {
        (void)bytes;
        oldFiles["files"].push_back(name);
    }
    snapshot["manifest.json"] = oldFiles.dump(2);
    const auto backupDir = std::filesystem::u8path(dir) / "backups";
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
    return true;
}
}} // AlterEgo::Backup
