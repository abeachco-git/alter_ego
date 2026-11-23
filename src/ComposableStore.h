#pragma once
// Storage for the versioned component graph. The old build_library.json is
// retained as an atomic, backwards-compatible mirror (never sole authority).
// A byte-for-byte pre-migration backup is written before v3 first becomes
// authoritative. All load/commit paths validate and round-trip in memory.
#include "ComposableLoadouts.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace AlterEgo { namespace Composable { namespace Store {

constexpr const char* kFile = "loadouts_v3.json";
constexpr const char* kLegacy = "build_library.json";
constexpr const char* kBefore = "build_library.before_loadouts_v3.json";
constexpr const char* kPrevious = "loadouts_v3.previous.json";
constexpr std::uintmax_t kMaxBytes = 64u * 1024u * 1024u;

inline std::filesystem::path Path(const std::string& dir, const char* name) {
    return std::filesystem::u8path(dir) / std::filesystem::u8path(name);
}
inline bool Exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}
inline bool Read(const std::filesystem::path& path, std::string& data,
                 std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return Fail(error, "Cannot open " + path.filename().u8string());
    const auto n = file.tellg();
    if (n < 0 || static_cast<std::uintmax_t>(n) > kMaxBytes)
        return Fail(error, "Invalid or oversized library file.");
    data.resize(static_cast<size_t>(n));
    file.seekg(0);
    if (!data.empty())
        file.read(&data[0], static_cast<std::streamsize>(data.size()));
    if (!file) return Fail(error, "Incomplete library read.");
    return true;
}
inline bool Parse(const std::string& data, Json& value, std::string& error) {
    try { value = Json::parse(data); return true; }
    catch (const std::exception& e) {
        return Fail(error, std::string("Corrupt library JSON: ") + e.what());
    }
}
inline bool Atomic(const std::filesystem::path& target,
                   const std::string& data, bool createOnly,
                   std::string& error) {
    if (data.size() > kMaxBytes) return Fail(error, "Library exceeds size limit.");
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) return Fail(error, "Cannot create library data directory.");
    if (createOnly && Exists(target))
        return Fail(error, "Refusing to overwrite protected backup.");

    static std::atomic<std::uint64_t> sequence{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto tmp = std::filesystem::u8path(
        target.u8string() + ".tmp-" + std::to_string(ticks) + "-" +
        std::to_string(sequence.fetch_add(1)));
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file) return Fail(error, "Cannot create temporary library file.");
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.flush();
        const bool ok = file.good();
        file.close();
        if (!ok || !file) {
            std::filesystem::remove(tmp, ec);
            return Fail(error, "Cannot finish writing temporary library.");
        }
    }

    bool replaced = false;
    if (createOnly) {
        std::filesystem::create_hard_link(tmp, target, ec);
        replaced = !ec;
#ifdef _WIN32
        // A move without REPLACE_EXISTING is an alternative on NTFS volumes
        // where creating hard links is disallowed.
        if (!replaced)
            replaced = MoveFileExW(tmp.c_str(), target.c_str(),
                                   MOVEFILE_WRITE_THROUGH) != 0;
#endif
    } else {
#ifdef _WIN32
        replaced = MoveFileExW(tmp.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::filesystem::rename(tmp, target, ec);
        replaced = !ec;
#endif
    }
    std::filesystem::remove(tmp, ec);
    if (!replaced)
        return Fail(error, "Could not replace library atomically: " +
                           target.filename().u8string());
    return true;
}
inline bool Save(const std::string& dir, const Document& doc,
                 std::string& error);

inline bool LoadV3(const std::string& dir, Document& output, std::string& error) {
    std::string data;
    if (!Read(Path(dir, kFile), data, error)) return false;
    Json json;
    if (!Parse(data, json, error)) return false;
    return FromJson(json, output, error);
}

// Verify that the user's legacy records can be reconstructed *exactly* before
// publishing a new v3 document. The raw legacy backup is never overwritten.
inline bool Open(const std::string& dir, Document& output,
                 bool& migrated, std::string& error) {
    migrated = false;
    error.clear();
    if (Exists(Path(dir, kFile))) {
        Document current;
        if (!LoadV3(dir, current, error)) return false;
        // An older DLL may have written the legacy compatibility file while
        // the user tested another branch. Import only if the legacy file is
        // demonstrably newer than v3; never silently discard either version.
        const auto oldPath = Path(dir, kLegacy);
        if (Exists(oldPath)) {
            std::string raw;
            Json old, projected;
            if (!Read(oldPath, raw, error) || !Parse(raw, old, error) ||
                !ToLegacy(current, projected, error)) return false;
            if (!old.is_object() || !old.contains("builds") ||
                !old.at("builds").is_array())
                return Fail(error, "Legacy mirror is not a valid Build library.");
            if (projected.at("builds") != old.at("builds")) {
                std::error_code ecOld, ecNew;
                const auto modifiedOld = std::filesystem::last_write_time(oldPath, ecOld);
                const auto modifiedNew = std::filesystem::last_write_time(Path(dir, kFile), ecNew);
                if (ecOld || ecNew || modifiedOld <= modifiedNew)
                    return Fail(error,
                        "Divergent Build and Loadout data; refusing automatic overwrite.");
                Document reconciled;
                if (!Reconcile(current, old, reconciled, error) ||
                    !Save(dir, reconciled, error)) return false;
                current = std::move(reconciled);
                migrated = true;
            }
        }
        output = std::move(current);
        return true;
    }

    if (!Exists(Path(dir, kLegacy))) {
        output = Document{};
        return true;
    }

    std::string raw;
    if (!Read(Path(dir, kLegacy), raw, error)) return false;
    Json legacy;
    if (!Parse(raw, legacy, error)) return false;
    Document doc;
    if (!SplitLegacy(legacy, doc, error)) return false;
    Json roundTrip;
    if (!ToLegacy(doc, roundTrip, error) ||
        roundTrip.at("builds") != legacy.at("builds"))
        return Fail(error, "Legacy migration changed existing saved records.");

    const auto backup = Path(dir, kBefore);
    if (Exists(backup)) {
        std::string preserved;
        if (!Read(backup, preserved, error) || preserved != raw)
            return Fail(error, "Previous pre-migration backup differs from legacy.");
    } else if (!Atomic(backup, raw, true, error)) return false;

    const auto data = ToJson(doc).dump(2);
    if (!Atomic(Path(dir, kFile), data, true, error)) return false;
    Document verified;
    if (!LoadV3(dir, verified, error)) return false;
    Json check;
    if (!ToLegacy(verified, check, error) ||
        check.at("builds") != legacy.at("builds"))
        return Fail(error, "Migrated Loadout file failed verification.");
    output = std::move(verified);
    migrated = true;
    return true;
}

// Every Save checks the previous document before replacing it. Keep a
// recovery snapshot, then write the canonical file, then the legacy mirror.
// If mirror write fails, canonical data is still intact and the caller is
// notified; subsequent loads always favor the canonical v3 file.
inline bool Save(const std::string& dir, const Document& doc,
                 std::string& error) {
    if (!Validate(doc, error)) return false;
    Document verified;
    if (!FromJson(ToJson(doc), verified, error)) return false;
    Json oldView;
    if (!ToLegacy(verified, oldView, error)) return false;

    const auto path = Path(dir, kFile);
    if (Exists(path)) {
        Document before;
        if (!LoadV3(dir, before, error))
            return Fail(error, "Refusing to overwrite corrupt v3 library: " + error);
        std::string raw;
        if (!Read(path, raw, error)) return false;
        if (!Atomic(Path(dir, kPrevious), raw, false, error)) return false;
    } else if (Exists(Path(dir, kLegacy))) {
        // A first-time save must go through Open() to preserve the old file.
        return Fail(error, "Must migrate legacy library before saving v3.");
    }
    if (!Atomic(path, ToJson(verified).dump(2), false, error)) return false;
    return Atomic(Path(dir, kLegacy), oldView.dump(2), false, error);
}

} } } // namespace AlterEgo::Composable::Store
