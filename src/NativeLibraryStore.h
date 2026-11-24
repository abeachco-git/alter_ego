#pragma once
#include "NativeLibrary.h"
#include <filesystem>
#include <mutex>

namespace AlterEgo::NativeLibrary {

struct Paths {
    explicit Paths(const std::string& root) : base(std::filesystem::u8path(root)) {}
    std::filesystem::path base;
    std::filesystem::path Library() const {return base/"library"/"library.db";}
    std::filesystem::path Cache() const {return base/"cache"/"cache.db";}
    std::filesystem::path Migration() const {return base/"backups"/"migration";}
    std::filesystem::path Recovery() const {return base/"backups"/"recovery";}
};

// A single runtime service owns the cached committed snapshot. Connections
// are short-lived and used only by the calling thread, under this service lock.
// Rendering reads published snapshots, not SQL. Import and edit commands use
// the revision from their draft; a stale command never overwrites newer data.
class Store {
public:
    bool Open(const std::string& directory,std::string& error);
    bool Read(Snapshot& snapshot,std::string& error);
    bool Commit(const Snapshot& before,Snapshot after,std::string& error);
    bool Backup(const std::string& destination,std::string& error);
    void Close();
private:
    std::mutex mutex_;
    std::string directory_;
    Snapshot snapshot_;
    bool loaded_=false;
};

Store& RuntimeStore();

// Only legacy imports/migration call this function: consolidate identical
// newly split records without removing any intentionally retained native IDs.
bool SplitLegacyExact(const Composable::Json& legacy,Composable::Document& output,
                      std::string& error);

} // namespace AlterEgo::NativeLibrary
