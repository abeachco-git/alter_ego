#pragma once
// Rebuildable account achievement snapshots. No network or UI dependencies.
#include "Database.h"
#include "NativeLibraryStore.h"
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace AlterEgo::Cache {
constexpr int kApplicationId = 0x41454348; // AECH
constexpr int kSchemaVersion = 1;
struct AchievementProgress {
    uint32_t id = 0;
    int current = 0, max = 0;
    bool done = false;
    int repeated = 0;
    bool unlocked = true;
    std::set<uint32_t> completed_bits;
};
struct AchievementSnapshot {
    int64_t fetchedAt = 0;
    std::map<uint32_t, AchievementProgress> entries;
};
inline void Upgrade(Database::Connection& db, int version) {
    if (version != 1) throw Database::Error(SQLITE_ERROR, "Unknown cache schema");
    db.Exec(R"SQL(
CREATE TABLE achievement_snapshots(
    account TEXT PRIMARY KEY CHECK(length(account)>0),
    fetched_at INTEGER NOT NULL CHECK(fetched_at>=0)
) STRICT;
CREATE TABLE achievement_progress(
    account TEXT NOT NULL REFERENCES achievement_snapshots(account) ON DELETE CASCADE,
    id INTEGER NOT NULL CHECK(id BETWEEN 0 AND 4294967295),
    current_count INTEGER NOT NULL, max_count INTEGER NOT NULL,
    done INTEGER NOT NULL CHECK(done IN (0,1)), repeated INTEGER NOT NULL,
    unlocked INTEGER NOT NULL CHECK(unlocked IN (0,1)),
    PRIMARY KEY(account,id)
) STRICT;
CREATE TABLE achievement_completed_bits(
    account TEXT NOT NULL, achievement_id INTEGER NOT NULL,
    bit INTEGER NOT NULL CHECK(bit BETWEEN 0 AND 4294967295),
    PRIMARY KEY(account,achievement_id,bit),
    FOREIGN KEY(account,achievement_id) REFERENCES achievement_progress(account,id) ON DELETE CASCADE
) STRICT;
)SQL");
}
inline void Initialize(Database::Connection& db) {
    const auto id = db.Scalar("PRAGMA application_id");
    const auto version = db.Scalar("PRAGMA user_version");
    if (id != kApplicationId || version != kSchemaVersion)
        Database::Migrate(db, kApplicationId, kSchemaVersion, Upgrade);
}
// Short-lived connections serialize snapshot operations without owning worker
// threads. SQL stays at the existing disk persistence boundaries.
inline std::mutex& Mutex() { static std::mutex mutex; return mutex; }
inline std::optional<AchievementSnapshot> ReadAchievements(
    const std::string& root, const std::string& account) {
    if (account.empty()) return std::nullopt;
    std::lock_guard<std::mutex> lock(Mutex());
    const auto path = NativeLibrary::Paths(root).Cache();
    if (!std::filesystem::exists(path)) return std::nullopt;
    Database::Connection db(path.u8string(), Database::Connection::OpenMode::Existing);
    Initialize(db);
    Database::Transaction tx(db);
    AchievementSnapshot result;
    {
        auto q = db.Prepare("SELECT fetched_at FROM achievement_snapshots WHERE account=?");
        q.Bind(1, account);
        if (!q.Step()) return std::nullopt;
        result.fetchedAt = q.Integer(0);
    }
    {
        auto q = db.Prepare("SELECT id,current_count,max_count,done,repeated,unlocked FROM achievement_progress WHERE account=? ORDER BY id");
        q.Bind(1, account);
        while (q.Step()) {
            AchievementProgress row;
            row.id = static_cast<uint32_t>(q.Integer(0));
            row.current = static_cast<int>(q.Integer(1));
            row.max = static_cast<int>(q.Integer(2));
            row.done = q.Integer(3) != 0;
            row.repeated = static_cast<int>(q.Integer(4));
            row.unlocked = q.Integer(5) != 0;
            result.entries.emplace(row.id, std::move(row));
        }
    }
    {
        auto q = db.Prepare("SELECT achievement_id,bit FROM achievement_completed_bits WHERE account=? ORDER BY achievement_id,bit");
        q.Bind(1, account);
        while (q.Step())
            result.entries.at(static_cast<uint32_t>(q.Integer(0))).completed_bits.insert(static_cast<uint32_t>(q.Integer(1)));
    }
    tx.Commit();
    return result;
}
// An older fetch must not replace a newer persisted account snapshot.
// Equal timestamps are allowed for incremental progress/optimistic updates.
inline bool WriteAchievements(const std::string& root, const std::string& account,
                              const AchievementSnapshot& snapshot) {
    if (account.empty() || snapshot.fetchedAt < 0) return false;
    std::lock_guard<std::mutex> lock(Mutex());
    const auto path = NativeLibrary::Paths(root).Cache();
    std::filesystem::create_directories(path.parent_path());
    Database::Connection db(path.u8string(), Database::Connection::OpenMode::Create);
    Initialize(db);
    Database::Transaction tx(db);
    {
        auto q = db.Prepare("SELECT fetched_at FROM achievement_snapshots WHERE account=?");
        q.Bind(1, account);
        if (q.Step() && q.Integer(0) > snapshot.fetchedAt) return false;
    }
    {
        auto q = db.Prepare("DELETE FROM achievement_snapshots WHERE account=?");
        q.Bind(1, account); q.Execute();
        auto put = db.Prepare("INSERT INTO achievement_snapshots VALUES(?,?)");
        put.Bind(1, account); put.Bind(2, snapshot.fetchedAt); put.Execute();
    }
    auto row = db.Prepare("INSERT INTO achievement_progress VALUES(?,?,?,?,?,?,?)");
    auto bit = db.Prepare("INSERT INTO achievement_completed_bits VALUES(?,?,?)");
    for (const auto& [id,p] : snapshot.entries) {
        if (id != p.id) throw std::runtime_error("Achievement identity mismatch");
        row.Bind(1, account); row.Bind(2, id); row.Bind(3, p.current);
        row.Bind(4, p.max); row.Bind(5, p.done); row.Bind(6, p.repeated);
        row.Bind(7, p.unlocked); row.Execute(); row.Reset();
        for (const auto b : p.completed_bits) {
            bit.Bind(1, account); bit.Bind(2, id); bit.Bind(3, b);
            bit.Execute(); bit.Reset();
        }
    }
    tx.Commit();
    return true;
}
} // namespace AlterEgo::Cache
