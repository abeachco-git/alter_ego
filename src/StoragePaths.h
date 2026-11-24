#pragma once
#include <filesystem>
#include <fstream>
#include <chrono>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

namespace AlterEgo::Storage {
struct Paths {
    explicit Paths(const std::string& root) : base(std::filesystem::u8path(root)) {}
    std::filesystem::path base;
    std::filesystem::path ConfigDirectory() const { return base/"config"; }
    std::filesystem::path LibraryDirectory() const { return base/"library"; }
    std::filesystem::path CacheDirectory() const { return base/"cache"; }
    std::filesystem::path Library() const { return LibraryDirectory()/"library.db"; }
    std::filesystem::path Cache() const { return CacheDirectory()/"cache.db"; }
    std::filesystem::path Migration() const { return base/"backups"/"migration"; }
    std::filesystem::path Recovery() const { return base/"backups"/"recovery"; }
    std::filesystem::path Portraits() const { return base/"portraits"; }
    std::filesystem::path Fonts() const { return base/"fonts"; }
};
inline bool ConfigName(const std::string& name) {
    return name=="settings.json" || name=="session.json" ||
           name=="char_sort.json" || name=="clears_ui.json";
}
inline bool DurableName(const std::string& name) {
    return name=="ach_tracked.json" || name=="login_times.json";
}
inline std::filesystem::path Destination(const Paths& paths, const std::string& name) {
    if (ConfigName(name)) return paths.ConfigDirectory()/name;
    if (DurableName(name)) return paths.LibraryDirectory()/name;
    return paths.base/std::filesystem::u8path(name);
}
inline std::string ReadMigrationBytes(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if (!f) throw std::runtime_error("Cannot read migration source");
    const auto n=f.tellg();
    if (n<0 || n>64*1024*1024) throw std::runtime_error("Invalid migration size");
    std::string bytes(static_cast<size_t>(n),'\0');
    f.seekg(0);
    if (!bytes.empty()) f.read(&bytes[0],static_cast<std::streamsize>(bytes.size()));
    if (!f) throw std::runtime_error("Incomplete migration source");
    return bytes;
}
inline void WriteMigrationBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    if (!f || !f.write(bytes.data(),static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot stage migration file");
    f.flush(); f.close();
    if (!f || ReadMigrationBytes(path)!=bytes)
        throw std::runtime_error("Migration verification failed");
}
// Existing canonical files always win. A valid legacy document is archived
// and published without replacing a destination. If staging fails, keep the
// old path active for this attempt instead of silently discarding preferences.
// Only these six explicitly classified filenames are migrated.
inline std::string Resolve(const std::string& root, const std::string& name) {
    const Paths paths(root);
    const auto dest=Destination(paths,name);
    if (!ConfigName(name) && !DurableName(name)) return dest.u8string();
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    const auto old=paths.base/name;
    std::error_code ec;
    if (std::filesystem::exists(dest,ec)) return dest.u8string();
    if (ec) return old.u8string();
    const bool legacy=std::filesystem::exists(old,ec);
    if (ec) return old.u8string();
    try {
        std::filesystem::create_directories(dest.parent_path());
        if (!legacy) return dest.u8string();
        const auto bytes=ReadMigrationBytes(old);
        const auto parsed=nlohmann::json::parse(bytes,nullptr,false);
        if (!parsed.is_object()) return old.u8string();
        // Unique archive and separate staging inode; truncating active JSON
        // must never change its migration backup.
        const auto parent=paths.Migration();
        std::filesystem::create_directories(parent);
        std::filesystem::path archive;
        for (unsigned n=0;n<100;++n) {
            auto candidate=parent/("config-"+std::to_string(
                std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(n));
            if (std::filesystem::create_directory(candidate)) { archive=candidate; break; }
        }
        if (archive.empty()) throw std::runtime_error("Cannot allocate migration directory");
        WriteMigrationBytes(archive/name,bytes);
        const auto stage=archive/(name+".stage");
        WriteMigrationBytes(stage,bytes);
        if (ReadMigrationBytes(old)!=bytes) throw std::runtime_error("Migration source changed");
        std::filesystem::create_hard_link(stage,dest,ec);
#ifdef _WIN32
        // Windows rename fails when a destination already exists.
        if (ec) { ec.clear(); std::filesystem::rename(stage,dest,ec); }
#endif
        if (ec) {
            if (std::filesystem::exists(dest)) return dest.u8string();
            throw std::runtime_error("Cannot publish migration file");
        }
        std::filesystem::remove(stage,ec);
        if (ReadMigrationBytes(dest)==bytes && ReadMigrationBytes(archive/name)==bytes &&
            ReadMigrationBytes(old)==bytes)
            std::filesystem::remove(old,ec);
        return dest.u8string();
    } catch (...) {
        ec.clear();
        if (std::filesystem::exists(dest,ec)) return dest.u8string();
        return old.u8string();
    }
}
} // namespace AlterEgo::Storage
