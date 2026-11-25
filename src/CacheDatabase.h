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
constexpr int kSchemaVersion = 11;
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
    if (version == 11) {
        db.Exec(R"SQL(
CREATE TABLE native_wallet(account TEXT NOT NULL,currency_id INTEGER NOT NULL,value INTEGER NOT NULL CHECK(value>=0),PRIMARY KEY(account,currency_id)) STRICT;
CREATE TABLE native_currencies(id INTEGER PRIMARY KEY,name TEXT NOT NULL,description TEXT NOT NULL,sort_order INTEGER NOT NULL) STRICT;
CREATE TABLE native_guild_access(account TEXT NOT NULL,guild_id TEXT NOT NULL,PRIMARY KEY(account,guild_id)) STRICT;
CREATE TABLE native_guilds(guild_id TEXT PRIMARY KEY,name TEXT NOT NULL,tag TEXT NOT NULL,status TEXT NOT NULL,attempted INTEGER NOT NULL,success INTEGER NOT NULL) STRICT;
CREATE TABLE native_guild_slots(guild_id TEXT NOT NULL REFERENCES native_guilds ON DELETE CASCADE,position TEXT NOT NULL,item_id INTEGER NOT NULL,quantity INTEGER NOT NULL CHECK(quantity>0),PRIMARY KEY(guild_id,position)) STRICT;
CREATE TABLE native_guild_coins(guild_id TEXT NOT NULL REFERENCES native_guilds ON DELETE CASCADE,section TEXT NOT NULL,value INTEGER NOT NULL CHECK(value>=0),PRIMARY KEY(guild_id,section)) STRICT;
CREATE TABLE native_item_flags(item_id INTEGER PRIMARY KEY,account_bound INTEGER NOT NULL,soulbound INTEGER NOT NULL,bind_on_use INTEGER NOT NULL) STRICT;
)SQL");
        return;
    }
    if (version == 10) {
        db.Exec(R"SQL(
CREATE TABLE native_roster_state(account TEXT PRIMARY KEY, fetched_at INTEGER NOT NULL) STRICT;
CREATE TABLE native_rosters(account TEXT NOT NULL REFERENCES native_roster_state(account) ON DELETE CASCADE,character TEXT NOT NULL,PRIMARY KEY(account,character)) STRICT;
CREATE TABLE native_inventory_scopes(account TEXT NOT NULL,scope TEXT NOT NULL,character TEXT NOT NULL,status TEXT NOT NULL,attempted INTEGER NOT NULL,success INTEGER NOT NULL,PRIMARY KEY(account,scope,character)) STRICT;
CREATE TABLE native_inventory_slots(account TEXT NOT NULL,scope TEXT NOT NULL,character TEXT NOT NULL,position TEXT NOT NULL,item_id INTEGER NOT NULL CHECK(item_id BETWEEN 1 AND 4294967295),quantity INTEGER NOT NULL CHECK(quantity>0),binding INTEGER NOT NULL CHECK(binding BETWEEN 0 AND 3),bound_to TEXT NOT NULL,stat_id INTEGER NOT NULL,charges INTEGER NOT NULL,PRIMARY KEY(account,scope,character,position),FOREIGN KEY(account,scope,character) REFERENCES native_inventory_scopes ON DELETE CASCADE) STRICT;
CREATE TABLE native_inventory_sockets(account TEXT NOT NULL,scope TEXT NOT NULL,character TEXT NOT NULL,position TEXT NOT NULL,kind INTEGER NOT NULL CHECK(kind IN (0,1)),ordinal INTEGER NOT NULL,item_id INTEGER NOT NULL,PRIMARY KEY(account,scope,character,position,kind,ordinal),FOREIGN KEY(account,scope,character,position) REFERENCES native_inventory_slots ON DELETE CASCADE) STRICT;
CREATE INDEX native_inventory_item_lookup ON native_inventory_slots(item_id,account);
)SQL");
        return;
    }
    if (version == 9) {
        db.Exec(R"SQL(
CREATE TABLE wiki_cache_state(kind TEXT PRIMARY KEY CHECK(kind IN ('metadata','failures'))) STRICT;
CREATE TABLE wiki_skin_metadata(skin_id INTEGER PRIMARY KEY CHECK(skin_id BETWEEN 1 AND 4294967295),
    acquisition TEXT NOT NULL, collection TEXT NOT NULL, set_name TEXT NOT NULL,
    vendor_name TEXT NOT NULL, vendor_location TEXT NOT NULL, vendor_waypoint TEXT NOT NULL,
    vendor_cost TEXT NOT NULL, vendor_cost_keys TEXT NOT NULL) STRICT;
CREATE TABLE wiki_skin_failures(skin_id INTEGER PRIMARY KEY CHECK(skin_id BETWEEN 1 AND 4294967295),
    failed_at INTEGER NOT NULL CHECK(failed_at>=0)) STRICT;
CREATE TABLE wiki_currency_failures(currency TEXT PRIMARY KEY CHECK(length(currency)>0),
    failed_at INTEGER NOT NULL CHECK(failed_at>=0)) STRICT;
)SQL");
        return;
    }
    if (version == 8) {
        db.Exec(R"SQL(
CREATE TABLE clears_snapshots(account TEXT PRIMARY KEY CHECK(length(account)>0), fetch_time INTEGER NOT NULL CHECK(fetch_time>=0)) STRICT;
CREATE TABLE clears_sections(account TEXT NOT NULL REFERENCES clears_snapshots ON DELETE CASCADE, section TEXT NOT NULL CHECK(section IN ('dailyFractals','dailyBounties','weeklyWings','weeklyStrikes')), PRIMARY KEY(account,section)) STRICT;
CREATE TABLE clears_entries(account TEXT NOT NULL, section TEXT NOT NULL, ordinal INTEGER NOT NULL, id INTEGER NOT NULL, name TEXT NOT NULL, tier TEXT NOT NULL, done INTEGER NOT NULL, current_count INTEGER NOT NULL, max_count INTEGER NOT NULL, PRIMARY KEY(account,section,ordinal), FOREIGN KEY(account,section) REFERENCES clears_sections ON DELETE CASCADE) STRICT;
CREATE TABLE clears_bit_names(account TEXT NOT NULL, section TEXT NOT NULL, entry_ordinal INTEGER NOT NULL, ordinal INTEGER NOT NULL, name TEXT NOT NULL, PRIMARY KEY(account,section,entry_ordinal,ordinal), FOREIGN KEY(account,section,entry_ordinal) REFERENCES clears_entries ON DELETE CASCADE) STRICT;
CREATE TABLE clears_bit_done(account TEXT NOT NULL, section TEXT NOT NULL, entry_ordinal INTEGER NOT NULL, ordinal INTEGER NOT NULL, done INTEGER NOT NULL CHECK(done IN (0,1)), PRIMARY KEY(account,section,entry_ordinal,ordinal), FOREIGN KEY(account,section,entry_ordinal) REFERENCES clears_entries ON DELETE CASCADE) STRICT;
CREATE TABLE vault_snapshots(account TEXT PRIMARY KEY CHECK(length(account)>0), fetch_time INTEGER NOT NULL CHECK(fetch_time>=0), season_end INTEGER) STRICT;
CREATE TABLE vault_periods(account TEXT NOT NULL REFERENCES vault_snapshots ON DELETE CASCADE, section TEXT NOT NULL CHECK(section IN ('daily','weekly','special')), meta_current INTEGER NOT NULL, meta_complete INTEGER NOT NULL, meta_astral INTEGER NOT NULL, meta_claimed INTEGER NOT NULL, PRIMARY KEY(account,section)) STRICT;
CREATE TABLE vault_objectives(account TEXT NOT NULL, section TEXT NOT NULL, ordinal INTEGER NOT NULL, id INTEGER NOT NULL, title TEXT NOT NULL, track TEXT NOT NULL, acclaim INTEGER NOT NULL, progress_current INTEGER NOT NULL, progress_complete INTEGER NOT NULL, claimed INTEGER NOT NULL, PRIMARY KEY(account,section,ordinal), FOREIGN KEY(account,section) REFERENCES vault_periods ON DELETE CASCADE) STRICT;
)SQL");
        return;
    }
    if (version == 7) {
        db.Exec(R"SQL(
CREATE TABLE character_snapshot(singleton INTEGER PRIMARY KEY CHECK(singleton=1), last_updated INTEGER NOT NULL CHECK(last_updated>=0)) STRICT;
CREATE TABLE character_rows(character_id INTEGER PRIMARY KEY, account_name TEXT NOT NULL,
    name TEXT NOT NULL CHECK(length(name)>0),
    race TEXT,
    gender TEXT,
    profession TEXT,
    level INTEGER,
    age INTEGER,
    created TEXT,
    last_modified TEXT,
    data_refreshed INTEGER,
    deaths INTEGER,
    crafting_cached INTEGER,
    active_build_tab INTEGER,
    active_equipment_tab INTEGER, UNIQUE(account_name,name)) STRICT;
CREATE TABLE character_crafting(character_id INTEGER NOT NULL REFERENCES character_rows ON DELETE CASCADE, ordinal INTEGER NOT NULL, discipline TEXT, rating INTEGER, active INTEGER, PRIMARY KEY(character_id,ordinal)) STRICT;
CREATE TABLE character_equipment(character_id INTEGER NOT NULL REFERENCES character_rows ON DELETE CASCADE, ordinal INTEGER NOT NULL, item_id INTEGER,
    slot TEXT,
    skin INTEGER,
    tab INTEGER,
    stat_id INTEGER,
    binding TEXT,
    bound_to TEXT, PRIMARY KEY(character_id,ordinal)) STRICT;
CREATE TABLE character_equipment_values(character_id INTEGER NOT NULL, equipment_ordinal INTEGER NOT NULL, kind TEXT NOT NULL CHECK(kind IN ('upgrades','infusions','dyes')), ordinal INTEGER NOT NULL, value INTEGER NOT NULL, PRIMARY KEY(character_id,equipment_ordinal,kind,ordinal), FOREIGN KEY(character_id,equipment_ordinal) REFERENCES character_equipment ON DELETE CASCADE) STRICT;
CREATE TABLE character_equipment_attributes(character_id INTEGER NOT NULL, equipment_ordinal INTEGER NOT NULL, attribute TEXT NOT NULL, value INTEGER NOT NULL, PRIMARY KEY(character_id,equipment_ordinal,attribute), FOREIGN KEY(character_id,equipment_ordinal) REFERENCES character_equipment ON DELETE CASCADE) STRICT;
CREATE TABLE character_equipment_tab_names(character_id INTEGER NOT NULL REFERENCES character_rows ON DELETE CASCADE, tab INTEGER NOT NULL, name TEXT NOT NULL, PRIMARY KEY(character_id,tab)) STRICT;
CREATE TABLE character_build_tabs(character_id INTEGER NOT NULL REFERENCES character_rows ON DELETE CASCADE, ordinal INTEGER NOT NULL, tab INTEGER,
    is_active INTEGER,
    name TEXT,
    profession TEXT,
    spec0_id INTEGER,
    spec0_trait0 INTEGER,
    spec0_trait1 INTEGER,
    spec0_trait2 INTEGER,
    spec1_id INTEGER,
    spec1_trait0 INTEGER,
    spec1_trait1 INTEGER,
    spec1_trait2 INTEGER,
    spec2_id INTEGER,
    spec2_trait0 INTEGER,
    spec2_trait1 INTEGER,
    spec2_trait2 INTEGER,
    bar0_heal INTEGER,
    bar0_utility0 INTEGER,
    bar0_utility1 INTEGER,
    bar0_utility2 INTEGER,
    bar0_elite INTEGER,
    bar1_heal INTEGER,
    bar1_utility0 INTEGER,
    bar1_utility1 INTEGER,
    bar1_utility2 INTEGER,
    bar1_elite INTEGER,
    pet0 INTEGER,
    pet1 INTEGER,
    pet2 INTEGER,
    pet3 INTEGER,
    legend0 TEXT,
    legend1 TEXT,
    legend2 TEXT,
    legend3 TEXT,
    weapons_known INTEGER, PRIMARY KEY(character_id,ordinal)) STRICT;
CREATE TABLE character_build_weapons(character_id INTEGER NOT NULL, build_ordinal INTEGER NOT NULL, ordinal INTEGER NOT NULL, weapon_id INTEGER NOT NULL, PRIMARY KEY(character_id,build_ordinal,ordinal), FOREIGN KEY(character_id,build_ordinal) REFERENCES character_build_tabs ON DELETE CASCADE) STRICT;
CREATE TABLE character_hero_points(character_id INTEGER NOT NULL REFERENCES character_rows ON DELETE CASCADE, ordinal INTEGER NOT NULL, hero_point TEXT NOT NULL, PRIMARY KEY(character_id,ordinal)) STRICT;
)SQL");
        return;
    }
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
