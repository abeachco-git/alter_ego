#pragma once
// Rebuildable account achievement snapshots. No network or UI dependencies.
#include "Database.h"
#include "StoragePaths.h"
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace AlterEgo::Cache {
constexpr int kApplicationId = 0x41454348; // AECH
constexpr int kSchemaVersion = 6;
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
    if (version == 6) {
        db.Exec(R"SQL(
CREATE TABLE reference_items(id INTEGER PRIMARY KEY CHECK(id BETWEEN 1 AND 4294967295),
    name TEXT NOT NULL DEFAULT '', icon TEXT NOT NULL DEFAULT '', rarity TEXT NOT NULL DEFAULT '',
    type TEXT NOT NULL DEFAULT '', description TEXT NOT NULL DEFAULT '', chat_link TEXT NOT NULL DEFAULT '',
    default_skin INTEGER NOT NULL DEFAULT 0, details TEXT,
    complete INTEGER NOT NULL DEFAULT 0 CHECK(complete IN (0,1))) STRICT;
CREATE INDEX reference_items_name ON reference_items(name);
CREATE TABLE item_name_aliases(name TEXT PRIMARY KEY CHECK(length(name)>0),
    item_id INTEGER NOT NULL REFERENCES reference_items(id)) STRICT;
CREATE TABLE commerce_item_name_ids(item_id INTEGER PRIMARY KEY REFERENCES reference_items(id)) STRICT;
INSERT INTO reference_items(id,name) SELECT item_id,name FROM commerce_item_names WHERE item_id>0;
INSERT INTO commerce_item_name_ids SELECT item_id FROM commerce_item_names WHERE item_id>0;
DROP TABLE commerce_item_names;
)SQL");
        return;
    }
    if (version == 5) {
        db.Exec(R"SQL(
CREATE TABLE reference_metadata_snapshots(dataset TEXT PRIMARY KEY, captured_at INTEGER NOT NULL CHECK(captured_at>=0)) STRICT;
CREATE TABLE reference_specializations(id INTEGER PRIMARY KEY CHECK(id BETWEEN 0 AND 4294967295),
    "name" TEXT NOT NULL,
    "icon" TEXT NOT NULL,
    "background" TEXT NOT NULL,
    "profession" TEXT NOT NULL,
    "profession_icon" TEXT NOT NULL,
    "profession_icon_big" TEXT NOT NULL,
    "elite" INTEGER NOT NULL CHECK("elite" IN (0,1))) STRICT;
CREATE TABLE reference_specializations_minor_traits(id INTEGER NOT NULL REFERENCES reference_specializations(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, value INTEGER NOT NULL, PRIMARY KEY(id,ordinal)) STRICT;
CREATE TABLE reference_specializations_major_traits(id INTEGER NOT NULL REFERENCES reference_specializations(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, value INTEGER NOT NULL, PRIMARY KEY(id,ordinal)) STRICT;
CREATE TABLE reference_traits(id INTEGER PRIMARY KEY CHECK(id BETWEEN 0 AND 4294967295),
    "name" TEXT NOT NULL,
    "icon" TEXT NOT NULL,
    "description" TEXT NOT NULL,
    "tier" INTEGER NOT NULL,
    "order" INTEGER NOT NULL,
    "slot" TEXT NOT NULL,
    "facts" TEXT) STRICT;
CREATE TABLE reference_skills(id INTEGER PRIMARY KEY CHECK(id BETWEEN 0 AND 4294967295),
    "name" TEXT NOT NULL,
    "icon" TEXT NOT NULL,
    "description" TEXT NOT NULL,
    "type" TEXT NOT NULL,
    "specialization" INTEGER NOT NULL,
    "facts" TEXT) STRICT;
CREATE TABLE reference_skills_professions(id INTEGER NOT NULL REFERENCES reference_skills(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, value TEXT NOT NULL, PRIMARY KEY(id,ordinal)) STRICT;
CREATE TABLE reference_itemstats(id INTEGER PRIMARY KEY CHECK(id BETWEEN 0 AND 4294967295),
    "name" TEXT NOT NULL) STRICT;
CREATE TABLE reference_itemstats_attributes(id INTEGER NOT NULL REFERENCES reference_itemstats(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, value TEXT NOT NULL, PRIMARY KEY(id,ordinal)) STRICT;
CREATE TABLE reference_dyes(id INTEGER PRIMARY KEY CHECK(id BETWEEN 0 AND 4294967295),
    "name" TEXT NOT NULL,
    base_r INTEGER NOT NULL,
    base_g INTEGER NOT NULL,
    base_b INTEGER NOT NULL,
    cloth_r INTEGER NOT NULL,
    cloth_g INTEGER NOT NULL,
    cloth_b INTEGER NOT NULL,
    leather_r INTEGER NOT NULL,
    leather_g INTEGER NOT NULL,
    leather_b INTEGER NOT NULL,
    metal_r INTEGER NOT NULL,
    metal_g INTEGER NOT NULL,
    metal_b INTEGER NOT NULL) STRICT;
)SQL");
        return;
    }
    if (version == 4) {
        db.Exec(R"SQL(
CREATE TABLE commerce_item_names(item_id INTEGER PRIMARY KEY, name TEXT NOT NULL) STRICT;
CREATE TABLE commerce_skin_items(skin_id INTEGER NOT NULL, item_id INTEGER NOT NULL,
    ordinal INTEGER NOT NULL, PRIMARY KEY(skin_id,item_id), UNIQUE(skin_id,ordinal)) STRICT;
CREATE INDEX commerce_item_skins ON commerce_skin_items(item_id);
CREATE TABLE commerce_vendor_prices(skin_id INTEGER PRIMARY KEY, copper INTEGER NOT NULL) STRICT;
CREATE TABLE commerce_item_prices(item_id INTEGER NOT NULL, fetched_at INTEGER NOT NULL,
    buy INTEGER NOT NULL, sell INTEGER NOT NULL, buy_quantity INTEGER NOT NULL,
    sell_quantity INTEGER NOT NULL, tradeable INTEGER NOT NULL CHECK(tradeable IN (0,1)),
    PRIMARY KEY(item_id,fetched_at)) STRICT;
CREATE TABLE commerce_skin_price_choices(skin_id INTEGER PRIMARY KEY, item_id INTEGER,
    fetched_at INTEGER NOT NULL, FOREIGN KEY(item_id,fetched_at)
    REFERENCES commerce_item_prices(item_id,fetched_at)) STRICT;
)SQL");
        return;
    }
    if (version == 3) {
        db.Exec(R"SQL(
CREATE TABLE skins(id INTEGER PRIMARY KEY, name TEXT NOT NULL, type TEXT NOT NULL,
    subtype TEXT NOT NULL DEFAULT '', weight_class TEXT NOT NULL, rarity TEXT NOT NULL,
    icon TEXT NOT NULL, dye_slot_count INTEGER,
    catalog INTEGER NOT NULL DEFAULT 0 CHECK(catalog IN (0,1)),
    fashion INTEGER NOT NULL DEFAULT 0 CHECK(fashion IN (0,1))) STRICT;
CREATE INDEX skin_categories ON skins(type,subtype,weight_class);
CREATE TABLE skin_flags(skin_id INTEGER NOT NULL REFERENCES skins(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, value TEXT NOT NULL, PRIMARY KEY(skin_id,ordinal)) STRICT;
CREATE TABLE skin_restrictions(skin_id INTEGER NOT NULL REFERENCES skins(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, value TEXT NOT NULL, PRIMARY KEY(skin_id,ordinal)) STRICT;
CREATE TABLE skin_dye_materials(skin_id INTEGER NOT NULL REFERENCES skins(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, value TEXT NOT NULL, PRIMARY KEY(skin_id,ordinal)) STRICT;
CREATE TABLE ownership_snapshots(account TEXT PRIMARY KEY CHECK(length(account)>0),
    fetched_at INTEGER NOT NULL CHECK(fetched_at>=0)) STRICT;
CREATE TABLE owned_skins(account TEXT NOT NULL REFERENCES ownership_snapshots(account) ON DELETE CASCADE,
    skin_id INTEGER NOT NULL CHECK(skin_id BETWEEN 0 AND 4294967295),
    PRIMARY KEY(account,skin_id)) STRICT;
)SQL");
        return;
    }
    if (version == 2) {
        db.Exec(R"SQL(
CREATE TABLE reference_freshness(dataset TEXT PRIMARY KEY, saved_at INTEGER NOT NULL) STRICT;
CREATE TABLE achievement_names(id INTEGER PRIMARY KEY, name TEXT NOT NULL) STRICT;
CREATE INDEX achievement_name_search ON achievement_names(name COLLATE NOCASE);
CREATE TABLE achievement_waypoints(achievement_id INTEGER NOT NULL, bit INTEGER NOT NULL,
    code TEXT NOT NULL, PRIMARY KEY(achievement_id,bit)) STRICT;
CREATE TABLE hero_challenges(id TEXT PRIMARY KEY, map_name TEXT NOT NULL, expansion TEXT NOT NULL) STRICT;
CREATE TABLE achievement_definitions(id INTEGER PRIMARY KEY, name TEXT NOT NULL,
    description TEXT NOT NULL, requirement TEXT NOT NULL, icon_url TEXT NOT NULL,
    type TEXT NOT NULL, max_count INTEGER NOT NULL, total_ap INTEGER NOT NULL) STRICT;
CREATE TABLE achievement_definition_flags(id INTEGER NOT NULL REFERENCES achievement_definitions(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, flag TEXT NOT NULL, PRIMARY KEY(id,ordinal)) STRICT;
CREATE TABLE achievement_prerequisites(id INTEGER NOT NULL REFERENCES achievement_definitions(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, prerequisite INTEGER NOT NULL, PRIMARY KEY(id,ordinal)) STRICT;
CREATE TABLE achievement_bits(id INTEGER NOT NULL REFERENCES achievement_definitions(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, type TEXT NOT NULL, item_id INTEGER NOT NULL,
    text TEXT NOT NULL, PRIMARY KEY(id,ordinal)) STRICT;
CREATE TABLE achievement_groups(id TEXT PRIMARY KEY, ordinal INTEGER NOT NULL,
    name TEXT NOT NULL, description TEXT NOT NULL, display_order INTEGER NOT NULL) STRICT;
CREATE TABLE achievement_categories(id INTEGER PRIMARY KEY, name TEXT NOT NULL, description TEXT NOT NULL,
    display_order INTEGER NOT NULL, icon_url TEXT NOT NULL) STRICT;
CREATE TABLE achievement_group_categories(group_id TEXT NOT NULL REFERENCES achievement_groups(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, category_id INTEGER NOT NULL, PRIMARY KEY(group_id,ordinal)) STRICT;
CREATE TABLE achievement_category_members(category_id INTEGER NOT NULL REFERENCES achievement_categories(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL, achievement_id INTEGER NOT NULL, PRIMARY KEY(category_id,ordinal)) STRICT;
CREATE INDEX achievement_member_lookup ON achievement_category_members(achievement_id);
)SQL");
        return;
    }
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
    const auto path = Storage::Paths(root).Cache();
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
    const auto path = Storage::Paths(root).Cache();
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
