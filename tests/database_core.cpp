#include "../src/Database.h"
#include "../src/CacheDatabase.h"
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
    return 0;
}
