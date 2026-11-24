#include "../src/Database.h"
#include "../src/CacheDatabase.h"
#include "../src/AchievementCatalogCache.h"
#include "../src/NativeLibraryStore.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

namespace DB = AlterEgo::Database;
namespace {
struct Scratch {
    std::filesystem::path dir;
    Scratch() {
        dir = std::filesystem::temp_directory_path() /
            ("alterego-db-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(dir))
            throw std::runtime_error("Cannot create test directory");
    }
    ~Scratch() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    std::string path(const char* name) const { return (dir / name).u8string(); }
};
void Upgrade(DB::Connection& db, int version) {
    if (version == 1) {
        db.Exec("CREATE TABLE parent(id INTEGER PRIMARY KEY, name TEXT NOT NULL);"
                "CREATE TABLE child(id INTEGER PRIMARY KEY, parent INTEGER NOT NULL "
                "REFERENCES parent(id));");
    } else if (version == 2) {
        db.Exec("CREATE INDEX child_parent ON child(parent)");
    } else {
        throw std::runtime_error("Unexpected migration");
    }
}
}

int case_database_core(int, char**) {
    Scratch temp;
    constexpr int id = 0x41454c42;
    {
        DB::Connection db(temp.path("library.db"), DB::Connection::OpenMode::Create);
        DB::Migrate(db, id, 1, Upgrade);
        assert(db.Scalar("PRAGMA user_version") == 1);
        {
            DB::Transaction tx(db);
            auto insert = db.Prepare("INSERT INTO parent VALUES(?,?)");
            insert.Bind(1, std::int64_t(1));
            insert.Bind(2, std::string("literal ' ;") + std::string(1, '\0'));
            insert.Execute();
            tx.Commit();
        }
        {
            DB::Transaction tx(db);
            db.Exec("INSERT INTO parent VALUES(2,'rollback')");
        }
        assert(db.Scalar("SELECT count(*) FROM parent") == 1);
        bool rejected = false;
        try { db.Exec("INSERT INTO child VALUES(1,99)"); }
        catch (const DB::Error&) { rejected = true; }
        assert(rejected);
        db.Exec("INSERT INTO child VALUES(1,1)");
        try {
            DB::Migrate(db, id, 2, [](DB::Connection& c, int) {
                c.Exec("CREATE TABLE incomplete(id INTEGER)");
                throw std::runtime_error("interrupted");
            });
            assert(false);
        } catch (const std::runtime_error&) {}
        assert(db.Scalar("PRAGMA user_version") == 1);
        assert(db.Scalar("SELECT count(*) FROM sqlite_schema WHERE name='incomplete'") == 0);
        DB::Migrate(db, id, 2, Upgrade);
        DB::Connection backup(temp.path("backup.db"), DB::Connection::OpenMode::Create);
        db.BackupTo(backup);
        assert(backup.Scalar("PRAGMA user_version") == 2);
        assert(backup.Scalar("SELECT count(*) FROM child") == 1);
    }
    {
        DB::Connection db(temp.path("library.db"), DB::Connection::OpenMode::Existing);
        DB::Migrate(db, id, 2, Upgrade);
        auto read = db.Prepare("SELECT name FROM parent WHERE id=1");
        assert(read.Step());
        assert(read.Text(0) == std::string("literal ' ;") + std::string(1, '\0'));
        assert(!read.Step());
        db.Verify();
    }
    {
        DB::Connection db(temp.path("foreign.db"), DB::Connection::OpenMode::Create);
        db.Exec("CREATE TABLE unrelated(id INTEGER)");
        bool rejected = false;
        try { DB::Migrate(db, id, 1, Upgrade); }
        catch (const DB::Error&) { rejected = true; }
        assert(rejected);
        assert(db.Scalar("SELECT count(*) FROM sqlite_schema WHERE name='unrelated'") == 1);
    }
    {
        namespace Cache = AlterEgo::Cache;
        const auto root = temp.dir.u8string();
        assert(!Cache::ReadAchievements(root, "Account.1234"));
        assert(!std::filesystem::exists(AlterEgo::NativeLibrary::Paths(root).Cache()));
        Cache::AchievementSnapshot first;
        first.fetchedAt = 100;
        for (uint32_t id = 1; id <= 5000; ++id) {
            Cache::AchievementProgress p;
            p.id = id; p.current = 2; p.max = 3; p.completed_bits = {0, 2};
            first.entries.emplace(id, p);
        }
        assert(Cache::WriteAchievements(root, "Account.1234", first));
        auto other = first;
        other.entries.clear();
        assert(Cache::WriteAchievements(root, "Account_1234", other));
        auto read = Cache::ReadAchievements(root, "Account.1234");
        assert(read && read->entries.size() == 5000 && read->fetchedAt == 100);
        assert(read->entries.at(1).completed_bits == std::set<uint32_t>({0,2}));
        assert(Cache::ReadAchievements(root, "Account_1234")->entries.empty());
        assert(!Cache::ReadAchievements(root, "Unknown.0000"));
        first.fetchedAt = 99;
        assert(!Cache::WriteAchievements(root, "Account.1234", first));
        first.fetchedAt = 101;
        first.entries.at(1).id = 2;
        bool rejected = false;
        try { Cache::WriteAchievements(root, "Account.1234", first); }
        catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
        read = Cache::ReadAchievements(root, "Account.1234");
        assert(read && read->fetchedAt == 100 && read->entries.size() == 5000);
        std::filesystem::remove_all(AlterEgo::NativeLibrary::Paths(root).Cache().parent_path());
        assert(!Cache::ReadAchievements(root, "Account.1234"));
        assert(std::filesystem::exists(temp.path("library.db")));
    }
    {
        namespace Storage = AlterEgo::Storage;
        const auto root = temp.dir.u8string();
        const Storage::Paths paths(root);
        const auto legacy = paths.base/"settings.json";
        const std::string original = R"({"show_age":true})";
        Storage::WriteMigrationBytes(legacy,original);
        const auto moved = Storage::Resolve(root,"settings.json");
        assert(moved == (paths.ConfigDirectory()/"settings.json").u8string());
        assert(Storage::ReadMigrationBytes(std::filesystem::u8path(moved)) == original);
        assert(!std::filesystem::exists(legacy));
        Storage::WriteMigrationBytes(std::filesystem::u8path(moved),R"({"show_age":false})");
        Storage::WriteMigrationBytes(legacy,original);
        assert(Storage::Resolve(root,"settings.json") == moved);
        assert(Storage::ReadMigrationBytes(std::filesystem::u8path(moved)) == R"({"show_age":false})");
        assert(Storage::ReadMigrationBytes(legacy) == original);
        bool archived=false;
        for (const auto& entry:std::filesystem::directory_iterator(paths.Migration()))
            if (std::filesystem::exists(entry.path()/"settings.json"))
                archived |= Storage::ReadMigrationBytes(entry.path()/"settings.json")==original;
        assert(archived);
        const auto pinned=Storage::Resolve(root,"ach_tracked.json");
        assert(pinned==(paths.LibraryDirectory()/"ach_tracked.json").u8string());
        Storage::WriteMigrationBytes(std::filesystem::u8path(pinned),R"({"pinned":[7]})");
        std::filesystem::remove_all(paths.CacheDirectory());
        assert(Storage::ReadMigrationBytes(std::filesystem::u8path(pinned))==R"({"pinned":[7]})");
    }
    {
        namespace Cache = AlterEgo::Cache;
        using D=Cache::AchievementDataset;
        using J=Cache::Json;
        const auto root=temp.dir.u8string();
        J defs={{"1",{{"id",1},{"name","Collection"},{"description","d"},{"requirement","r"},
            {"icon_url","url"},{"type","Default"},{"max_count",2},{"total_ap",3},
            {"flags",J::array({"Permanent","Hidden"})},{"prerequisites",J::array({4,5})},
            {"bits",J::array({{{"type","Item"},{"item_id",42},{"text","first"}},{{"type","Text"},{"item_id",0},{"text","second"}}})}}}};
        assert(Cache::WriteCatalog(root,D::Definitions,defs));
        assert(Cache::ReadCatalog(root,D::Definitions)==defs);
        J malformed=defs;malformed["1"]["id"]=2;
        assert(!Cache::WriteCatalog(root,D::Definitions,malformed));
        assert(Cache::ReadCatalog(root,D::Definitions)==defs);
        J groups={{"fetch_time",100},{"groups",J::array({{{"id","g"},{"name","Group"},
            {"description","d"},{"order",0},{"categories",J::array({2,1})}}})},
            {"categories",{{"1",{{"id",1},{"name","Category"},{"description","d"},
                {"order",1},{"icon_url","url"},{"achievements",J::array({9,8,7})}}}}}};
        assert(Cache::WriteCatalog(root,D::Groups,groups));
        assert(Cache::ReadCatalog(root,D::Groups)==groups);
        groups["fetch_time"]=99;assert(!Cache::WriteCatalog(root,D::Groups,groups));
        J names={{"1","Name"},{"2","Other"}};
        AlterEgo::Storage::WriteMigrationBytes(AlterEgo::Storage::Paths(root).base/"ach_name_index.json",names.dump());
        assert(Cache::ReadCatalog(root,D::Names)==names);
        assert(!std::filesystem::exists(AlterEgo::Storage::Paths(root).base/"ach_name_index.json"));
        J waypoints={{"1",{{"-1","[&ABC=]"},{"2","[&DEF=]"}}}};
        assert(Cache::WriteCatalog(root,D::Waypoints,waypoints));
        assert(Cache::ReadCatalog(root,D::Waypoints)==waypoints);
        J heroes={{"123",{{"map_name","Map"},{"expansion","HoT"}}}};
        assert(Cache::WriteCatalog(root,D::HeroChallenges,heroes));
        assert(Cache::ReadCatalog(root,D::HeroChallenges)==heroes);
    }
    return 0;
}
