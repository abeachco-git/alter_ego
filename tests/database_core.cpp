#include "../src/PublicCatalogCache.h"
#include "../src/WikiEventCache.h"
#include "../src/WikiMetadataCache.h"
#include "../src/Database.h"
#include "../src/ExternalResourceSafety.h"
#include "../src/CacheDatabase.h"
#include "../src/AchievementCatalogCache.h"
#include "../src/SkinDataCache.h"
#include "../src/CommerceCache.h"
#include "../src/ReferenceMetadataCache.h"
#include "../src/ItemMetadataCache.h"
#include "../src/CacheDiagnostics.h"
#include "../src/CacheMaintenance.h"
#include "../src/CharacterCache.h"
#include "../src/CharacterToolsCore.h"
#include "../src/CompletionCache.h"
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
    {
        namespace U=AlterEgo::Untrusted;
        assert(U::Parse(R"({"name":"O'Brien; --","nested":[{"x":1},{"x":2}]})",U::Clipboard).at("name")=="O'Brien; --");
        const U::Limits small{100,30,10,3};
        for(const std::string bad:{R"({"a":1,"a":2})",R"({"a":{"x":1,"x":2}})",R"([[[[[[0]]]]]])",R"({"a":"01234567890"})"}){
            bool rejected=false;try{U::Parse(bad,small);}catch(...){rejected=true;}assert(rejected);
        }
        bool rejected=false;
        try{U::Parse(std::string("{}")+char(0)+"{}",small);}catch(...){rejected=true;}
        assert(rejected);
        assert(U::Parse(R"({"a":"\u0000"})",small).at("a").get<std::string>().size()==1);
    }
    {
        namespace F=AlterEgo::WikiFreshness;
        namespace W=AlterEgo::Cache::Wiki;
        using namespace AlterEgo;
        const int64_t now=10000000;
        static_assert(F::SpecialRetry==3600);
        assert(F::Due({},now,F::SkinInterval,F::SkinRetry));
        assert(!F::Due({now-F::SkinInterval+1,0},now,F::SkinInterval,F::SkinRetry));
        assert(F::Due({now-F::SkinInterval,0},now,F::SkinInterval,F::SkinRetry));
        assert(!F::Due({0,now-F::SkinRetry+1},now,F::SkinInterval,F::SkinRetry));
        assert(F::Due({0,now-F::SkinRetry},now,F::SkinInterval,F::SkinRetry));
        assert(!F::Due({now-F::SpecialInterval+1,0},now,F::SpecialInterval,F::SpecialRetry));
        assert(F::Due({now-F::SpecialInterval,0},now,F::SpecialInterval,F::SpecialRetry));
        assert(!F::Due({0,now-F::SpecialRetry+1},now,F::SpecialInterval,F::SpecialRetry));
        assert(F::Due({0,now-F::SpecialRetry},now,F::SpecialInterval,F::SpecialRetry));
        assert(!F::ValidSpecialBody("error")&&F::ValidSpecialBody("== Active events ==\n"));
        assert(F::DateMatches(20261101,20261125,20261125));
        assert(!F::DateMatches(20261101,20261124,20261125));
        Scratch scratch;
        DB::Connection db(scratch.path("wiki-freshness.db"),DB::Connection::OpenMode::Create);
        DB::Migrate(db,Cache::kApplicationId,18,Cache::Upgrade);
        W::Replace(db,W::Kind::Metadata,{{"42",{{"vendor","Old"}}}});
        Cache::Initialize(db);
        assert(W::ReadFreshness(db).empty());
        assert(W::Project(db,W::Kind::Metadata).at("42").at("vendor")=="Old");
        W::MarkAttempt(db,42,now);
        auto stamp=W::ReadFreshness(db).at(42);
        assert(stamp.attempt==now&&stamp.success==0);
        bool failed=false;
        try{W::StoreVerified(db,42,nlohmann::json::object(),now);}catch(...){failed=true;}
        assert(failed&&W::ReadFreshness(db).at(42).success==0);
        assert(W::Project(db,W::Kind::Metadata).at("42").at("vendor")=="Old");
        W::StoreVerified(db,42,{{"vendor","New"}},now+1);
        W::MarkAttempt(db,42,now+F::SkinInterval+2);
        stamp=W::ReadFreshness(db).at(42);
        assert(stamp.success==now+1&&stamp.attempt==now+F::SkinInterval+2);
        assert(W::Project(db,W::Kind::Metadata).at("42").at("vendor")=="New");
    }
    {
        namespace W=AlterEgo::WikiEvents;
        Scratch scratch;
        DB::Connection db(scratch.path("wiki-events.db"),DB::Connection::OpenMode::Create);
        AlterEgo::Cache::Initialize(db);
        auto first=W::LoadBest(db);
        assert(first.embedded&&first.verified==W::EmbeddedVerifiedAt&&first.timers==W::Embedded().timers);
        auto older=first;older.verified--;older.embedded=false;
        assert(W::Select(first,older).embedded);
        auto newer=first;newer.verified++;newer.embedded=false;
        newer.timers.at("Kaineng Blackout")[0]=1;
        W::Validate(newer.timers);
        {DB::Transaction tx(db);W::Store(db,newer);tx.Commit();}
        assert(!W::LoadBest(db).embedded&&W::LoadBest(db).timers==newer.timers);
        AlterEgo::PublicCatalog::Mark(db,W::AttemptDataset,newer.verified+10);
        auto cached=W::LoadBest(db);
        assert(cached.verified==newer.verified&&cached.attempt==newer.verified+10);
        assert(!W::Due(cached,newer.verified+29*86400LL,false));
        assert(W::Due(cached,newer.verified+30*86400LL,false));
        assert(W::Due(cached,newer.verified+10,true));
        auto broken=newer;broken.verified++;broken.timers.erase("Kaineng Blackout");
        bool failed=false;try{DB::Transaction tx(db);W::Store(db,broken);tx.Commit();}catch(...){failed=true;}
        assert(failed&&W::Read(db).timers==newer.timers&&W::Read(db).verified==newer.verified);
        failed=false;try{DB::Transaction tx(db);W::Store(db,older);tx.Commit();}catch(...){failed=true;}
        assert(failed&&W::Read(db).timers==newer.timers);
    }
    {
    using namespace AlterEgo::PublicCatalog;
    assert(Due(0,0,100,false));
    assert(!Due(100,100,101,false));
    assert(!Due(0,100,101,false));
    assert(Due(100,100,101,true));
    assert(Due(100,100,100+Week,false));
    assert(!Due(100,100,100+Week,false,Month));
    assert(Due(100,100,100+Month,false,Month));
    assert(Due(100,100,101,true,Month));
    assert((PlanItems({1,2,3},{1,2},{1},true,false)==std::set<uint32_t>{3}));
    assert((PlanItems({1,2,3},{1,2},{1},true,true)==std::set<uint32_t>{1,3}));
    assert((PlanItems({1,2},{1,2},{1},false,true)==std::set<uint32_t>{1}));
    const auto vaultDefs=ParseObjectives(Json::parse(R"([{"id":20,"title":"Perform 3 Combo Skills in Combat","track":"PvE","acclaim":10}])"));
    assert(vaultDefs.at(20).acclaim==10);
    assert(Kind(Json::parse(R"({"type":"UpgradeComponent","details":{"type":"Default","infusion_upgrade_flags":["Infusion"]}})"))=="Infusion");
    assert(Kind(Json::parse(R"({"type":"UpgradeComponent","details":{"type":"Default","infusion_upgrade_flags":["Enrichment"]}})"))=="Enrichment");
    assert(Kind(Json::parse(R"({"type":"Relic"})"))=="Relic");
    bool badBatch=false;
    try{ValidateBatch(Json::parse(R"([{"id":1,"name":"Test","type":"Relic"}])"),{1,2});}catch(...){badBatch=true;}
    assert(badBatch);
    }

    Scratch temp;
    {
        namespace P=AlterEgo::PublicCatalog;
        DB::Connection db(temp.path("public-catalog.db"),DB::Connection::OpenMode::Create);
        AlterEgo::Cache::Initialize(db);
        const auto rows=P::Json::parse(R"([
            {"id":1,"name":"Rune","type":"UpgradeComponent","rarity":"Exotic","details":{"type":"Rune","bonuses":["+25 Power"]}},
            {"id":2,"name":"Sigil","type":"UpgradeComponent","rarity":"Exotic","details":{"type":"Sigil","infix_upgrade":{"buff":{"description":"+5% Damage"}}}},
            {"id":3,"name":"Relic","type":"Relic","rarity":"Exotic","description":"Effect"},
            {"id":4,"name":"Infusion","type":"UpgradeComponent","details":{"type":"Default","infusion_upgrade_flags":["Infusion"]}},
            {"id":5,"name":"Enrichment","type":"UpgradeComponent","details":{"type":"Default","infusion_upgrade_flags":["Enrichment"]}}
        ])");
        {
            DB::Transaction tx(db);
            P::StoreUpgrades(db,{1,2,3,4,5,6},rows,100);
            P::StoreObjectives(db,{{20,{"Combo","PvE",10}}},100);
            tx.Commit();
        }
        const auto pets=P::ParsePets(P::Json::parse(R"([{"id":4,"name":"Pet","icon":"url"}])"));
        const auto legends=P::ParseLegends(P::Json::parse(R"([{"id":"Legend8","code":8,"swap":500}])"),
            P::Json::parse(R"([{"id":500,"name":"Legendary Entity Stance","icon":"url"}])"));
        {DB::Transaction tx(db);P::StorePets(db,pets,100);P::StoreLegends(db,legends,100);tx.Commit();}
        auto saved=P::Read(db);
        assert(saved.pets.size()==1&&saved.pets[0].id==4);
        assert(saved.legends.size()==1&&saved.legends[0].swap==500);
        assert(P::LegendCode(saved.legends,"Legend8")==8);
        assert(P::LegendCode(saved.legends,"Legend9")==0);
        assert(P::CanonicalLegendCode(13)==1&&P::CanonicalLegendCode(19)==7&&P::CanonicalLegendCode(8)==8);
        assert(!P::DatasetDue(saved,"public_pets",101,false));
        assert(saved.upgrades==100&&saved.vault==100&&saved.index.size()==6&&saved.entries.size()==5);
        assert(saved.objectives.at(20).title=="Combo");
        assert(!P::DatasetDue(saved,"public_pets",100+P::Week,false));
        assert(P::DatasetDue(saved,"public_pets",100+P::Month,false));
        {
            DB::Transaction tx(db);
            P::StoreIncrementalItems(db,{1,2,3,4,5,6,7},
                P::Json::parse(R"([{"id":7,"name":"New ordinary item","type":"CraftingMaterial"}])"),true,false,200);
            tx.Commit();
        }
        auto discovered=P::Read(db);
        assert(discovered.index.size()==7&&discovered.upgrades==100);
        assert(discovered.freshness.at("public_item_discovery").first==200);
        assert(db.Scalar("SELECT count(*) FROM reference_items WHERE id=7 AND complete=1")==1);
        {
            DB::Transaction tx(db);
            P::StoreIncrementalItems(db,{1,2,3,4,5,6,7},rows,false,true,300);
            tx.Commit();
        }
        assert(P::Read(db).upgrades==300&&P::Read(db).freshness.at("public_item_discovery").first==200);
        assert(db.Scalar("SELECT count(*) FROM reference_items WHERE complete=1")==6);
        bool failed=false;
        try{
            DB::Transaction tx(db);
            auto partial=rows;partial.erase(0);
            P::StoreUpgrades(db,{1,2,3,4,5,6},partial,200);
            tx.Commit();
        }catch(...){failed=true;}
        assert(failed&&P::Read(db).upgrades==300&&P::Read(db).entries.size()==5);
        failed=false;
        try{DB::Transaction tx(db);P::StoreObjectives(db,{},200);tx.Commit();}catch(...){failed=true;}
        assert(failed&&P::Read(db).objectives.size()==1);
    }
    {
        namespace R=AlterEgo::ExternalResource;
        assert(R::ArtworkUrl("https://wiki.guildwars2.com/images/a/icon.png"));
        assert(R::ArtworkUrl("https://render.guildwars2.com/file/ABC/1.png"));
        assert(!R::ArtworkUrl("https://wiki.guildwars2.com.evil.test/images/a.png"));
        assert(!R::ArtworkUrl("https://wiki.guildwars2.com@evil.test/images/a.png"));
        assert(!R::SafeRedirect("https://wiki.guildwars2.com/api.php","http://wiki.guildwars2.com/api.php"));
        assert(R::SafeRedirect("https://wiki.guildwars2.com/api.php","https://wiki.guildwars2.com/wiki/Page"));
        assert(!R::SafeCacheKey("../escape")&&R::SafeCacheKey("Astral Acclaim"));
        std::string png("\x89PNG\r\n\x1a\n",8);png.resize(33,0);
        png[11]=13;png.replace(12,4,"IHDR");png[19]=32;png[23]=32;
        assert(R::ImageHeader(png));png[16]=1;assert(!R::ImageHeader(png));
        assert(!R::ImageHeader("not an image"));
        assert(!AlterEgo::Untrusted::DisplayText(std::string("visible")+char(0)+"hidden"));
        assert(AlterEgo::Untrusted::DisplayText("one\ntwo",true));
        DB::Connection db(temp.path("sql-security.db"),DB::Connection::OpenMode::Create);
        bool rejected=false;
        try{auto q=db.Prepare("SELECT 1; SELECT 2");}catch(const DB::Error&){rejected=true;}
        assert(rejected);rejected=false;
        try{auto q=db.Prepare("SELECT ?");q.Step();}catch(const DB::Error&){rejected=true;}
        assert(rejected);
        auto q=db.Prepare("SELECT ?");q.BindNull(1);assert(q.Step()&&q.IsNull(0));
    }
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
        const auto verified=Cache::AchievementSnapshotKind::Verified;
        assert(!Cache::ReadAchievements(root,"Account.1234",verified)); // no projection promotion
        auto raw=first;raw.fetchedAt=102;raw.entries.at(1).current=1;
        assert(Cache::WriteAchievements(root,"Account.1234",raw,verified));
        assert(Cache::ReadAchievements(root,"Account.1234",verified)->entries.at(1).current==1);
        assert(Cache::ReadAchievements(root,"Account.1234")->entries.at(1).current==2);
        assert(!Cache::ReadAchievements(root,"Account_1234",verified));
        assert(!Cache::WriteAchievements(root,"Account.1234",first,verified));
        raw.fetchedAt=103;raw.entries.at(1).id=2;
        bool badRaw=false;
        try{Cache::WriteAchievements(root,"Account.1234",raw,verified);}catch(...){badRaw=true;}
        assert(badRaw&&Cache::ReadAchievements(root,"Account.1234",verified)->fetchedAt==102);
        assert(!Cache::ReadVaultSeason(root));
        assert(Cache::WriteVaultSeason(root,{100,1000}));
        assert(!Cache::WriteVaultSeason(root,{101,101}));
        assert(!Cache::WriteVaultSeason(root,{99,2000}));
        auto season=Cache::ReadVaultSeason(root);
        assert(season&&season->verifiedAt==100&&season->end==1000);
        assert(Cache::WriteVaultSeason(root,{1001,2000}));
        assert(Cache::ReadVaultSeason(root)->end==2000);
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
        assert(!Cache::ReadAchievements(root,"Account.1234",verified));
        assert(!Cache::ReadVaultSeason(root));
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
        assert(Cache::ReadNameIndexVerification(root)==0); // legacy save is not verification
        assert(Cache::WriteCatalog(root,D::Names,names,100));
        assert(Cache::ReadNameIndexVerification(root)==100);
        auto more=names;more["3"]="New";
        assert(Cache::WriteCatalog(root,D::Names,more));
        assert(Cache::ReadNameIndexVerification(root)==100);
        auto invalid=more;invalid["1"]="";
        assert(!Cache::WriteCatalog(root,D::Names,invalid,200));
        assert(Cache::ReadCatalog(root,D::Names)==more);
        assert(Cache::ReadNameIndexVerification(root)==100);
        assert(Cache::WriteCatalog(root,D::Names,more,200));
        assert(Cache::ReadNameIndexVerification(root)==200);
        // Retired reference tables are absent after a fresh migration.
        DB::Connection db(AlterEgo::Storage::Paths(root).Cache().u8string(),DB::Connection::OpenMode::Existing);
        assert(db.Scalar("SELECT count(*) FROM sqlite_schema WHERE name IN ('hero_challenges','character_hero_points','achievement_waypoints')")==0);
    }
    {
        // A real v13 database upgrades without losing other reference or character records.
        namespace Cache=AlterEgo::Cache;
        const auto path=temp.dir/"hero-retirement-v13.db";
        DB::Connection db(path.u8string(),DB::Connection::OpenMode::Create);
        DB::Migrate(db,Cache::kApplicationId,13,Cache::Upgrade);
        db.Exec("INSERT INTO character_rows(character_id,account_name,name) VALUES(1,'A.B','Hero');"
                "INSERT INTO character_hero_points VALUES(1,0,'0-29');"
                "INSERT INTO hero_challenges VALUES('0-29','Map','Core');"
                "INSERT INTO achievement_names VALUES(1,'Retain');"
                "INSERT INTO achievement_waypoints VALUES(1,0,'[&ABC=]');"
                "INSERT INTO reference_freshness VALUES('hero_challenges',100),('ach_name_index',100),('ach_waypoints',100),('ach_name_index_verified',200);");
        Cache::Initialize(db);
        assert(db.Scalar("PRAGMA user_version")==Cache::kSchemaVersion);
        assert(db.Scalar("SELECT count(*) FROM sqlite_schema WHERE name IN ('hero_challenges','character_hero_points','achievement_waypoints')")==0);
        assert(db.Scalar("SELECT count(*) FROM reference_freshness WHERE dataset IN ('hero_challenges','ach_waypoints')")==0);
        assert(db.Scalar("SELECT saved_at FROM reference_freshness WHERE dataset='ach_name_index_verified'")==200);
        assert(db.Scalar("SELECT count(*) FROM reference_freshness WHERE dataset='ach_name_index'")==1);
        assert(db.Scalar("SELECT count(*) FROM character_rows WHERE name='Hero'")==1);
        assert(db.Scalar("SELECT count(*) FROM achievement_names WHERE name='Retain'")==1);
    }
    {
        namespace Cache=AlterEgo::Cache;
        using J=Cache::SkinJson;
        using P=Cache::SkinProjection;
        const auto root=temp.dir.u8string();
        const auto absent=temp.dir/"missing.json";
        J catalog=J::array({{{"id",42},{"name","Skin"},{"type","Armor"},{"subtype","Helm"},
            {"weight_class","Heavy"},{"rarity","Rare"},{"icon","icon"},
            {"flags",J::array({"HideIfLocked"})},{"restrictions",J::array({"Human"})}}});
        J fashion=J::array({{{"id",42},{"name","Skin"},{"type","Armor"},{"rarity","Rare"},
            {"dye_slot_count",2},{"dye_slot_materials",J::array({"metal","cloth"})}}});
        assert(Cache::WriteSkins(root,P::Catalog,catalog));
        assert(Cache::WriteSkins(root,P::Fashion,fashion));
        assert(Cache::WriteSkins(root,P::Catalog,catalog));
        auto read=Cache::ReadSkins(root,P::Catalog,absent);
        assert(read && read->at(0).at("subtype")=="Helm");
        assert(read->at(0).at("flags")==catalog.at(0).at("flags"));
        read=Cache::ReadSkins(root,P::Fashion,absent);
        assert(read && read->at(0).at("dye_slot_count")==2);
        assert(read->at(0).at("dye_slot_materials")==fashion.at(0).at("dye_slot_materials"));
        assert(read->at(0).at("weight_class")=="Heavy");
        // A delayed catalog fills its own fields but cannot regress newer common/dye data.
        const auto orderedRoot=(temp.dir/"skin-order-regression").u8string();
        auto older=catalog,newer=fashion,committed=J::array();
        older[0]["name"]="Older";newer[0]["name"]="Newer";newer[0]["icon"]="new-icon";
        Cache::SkinRequestStamps oldStamp{{42,100}},newStamp{{42,200}};
        assert(Cache::WriteSkins(orderedRoot,P::Fashion,newer,&newStamp,&committed));
        assert(Cache::WriteSkins(orderedRoot,P::Catalog,older,&oldStamp,&committed));
        assert(committed.at(0).at("name")=="Newer"&&committed.at(0).at("icon")=="new-icon");
        assert(committed.at(0).at("subtype")=="Helm"&&committed.at(0).at("flags")==older[0]["flags"]);
        assert(committed.at(0).at("dye_slot_count")==2&&committed.at(0).at("fashion")==true&&committed.at(0).at("catalog")==true);
        auto replay=older;replay[0]["flags"]=J::array();replay[0]["icon"]="replay";
        assert(Cache::WriteSkins(orderedRoot,P::Catalog,replay,&oldStamp,&committed));
        assert(committed.at(0).at("flags")==older[0]["flags"]&&committed.at(0).at("icon")=="new-icon");
        auto staleDye=newer;staleDye[0]["dye_slot_count"]=1;staleDye[0]["dye_slot_materials"]=J::array({"leather"});
        assert(Cache::WriteSkins(orderedRoot,P::Fashion,staleDye,&oldStamp,&committed));
        assert(committed.at(0).at("dye_slot_count")==2);
        auto newest=older;newest[0]["name"]="Latest";newest[0]["flags"]=J::array();
        Cache::SkinRequestStamps latest{{42,300}};
        assert(Cache::WriteSkins(orderedRoot,P::Catalog,newest,&latest,&committed));
        assert(committed.at(0).at("name")=="Latest"&&committed.at(0).at("flags").empty()&&committed.at(0).at("dye_slot_count")==2);
        auto invalid=newest;invalid.push_back(newest[0]);
        assert(!Cache::WriteSkins(orderedRoot,P::Catalog,invalid,&latest,&committed));
        assert(Cache::ReadSkins(orderedRoot,P::Fashion,absent)->at(0).at("name")=="Latest");
        Cache::OwnershipSnapshot owned;owned.fetchedAt=100;owned.ids={42,43};
        assert(Cache::WriteOwnership(root,"Account.1234",owned));
        Cache::OwnershipSnapshot empty;empty.fetchedAt=101;
        assert(Cache::WriteOwnership(root,"Account_1234",empty));
        assert(Cache::ReadOwnership(root,"Account.1234")->ids==owned.ids);
        assert(Cache::ReadOwnership(root,"Account_1234")->ids.empty());
        owned.fetchedAt=99;owned.ids.clear();
        assert(!Cache::WriteOwnership(root,"Account.1234",owned));
        assert(Cache::ReadOwnership(root,"Account.1234")->ids.size()==2);
    }
    {
        namespace Cache=AlterEgo::Cache;
        using J=Cache::Json;
        using D=Cache::CommerceDataset;
        const auto root=temp.dir.u8string();
        const auto absent=temp.dir/"absent.json";
        J mapping={{"version",2},{"skins",{{"1",J::array({11,12})},{"2",J::array({11})}}},
            {"names",{{"11","Item"},{"12","Alternate"}}},{"vendors",{{"3",4200}}}};
        assert(Cache::WriteCommerce(root,D::Mapping,mapping));
        assert(Cache::ReadCommerce(root,D::Mapping,absent)==mapping);
        J observation={{"item_id",11},{"ts",100},{"buy",25},{"sell",50},{"buy_q",2},{"sell_q",3},{"tradeable",true}};
        J prices={{"1",observation},{"2",observation}};
        assert(Cache::WriteCommerce(root,D::Prices,prices));
        assert(Cache::ReadCommerce(root,D::Prices,absent)==prices);
        {
            DB::Connection db(AlterEgo::Storage::Paths(root).Cache().u8string(),DB::Connection::OpenMode::Existing);
            assert(db.Scalar("SELECT count(*) FROM commerce_item_prices")==1);
        }
        J stale=observation;stale["ts"]=99;stale["sell"]=1;
        assert(Cache::WriteCommerce(root,D::Prices,J{{"1",stale}}));
        assert(Cache::ReadCommerce(root,D::Prices,absent)==prices);
        // Reordering is not a new candidate set; adding/removing candidates
        // invalidates only affected choices, including formerly empty choices.
        auto reordered=mapping;reordered["skins"]["1"]=J::array({12,11});
        assert(Cache::WriteCommerce(root,D::Mapping,reordered));
        assert(Cache::ReadCommerce(root,D::Prices,absent)==prices);
        auto changed=reordered;changed["skins"]["1"]=J::array({12});
        assert(Cache::WriteCommerce(root,D::Mapping,changed));
        assert(Cache::ReadCommerce(root,D::Prices,absent)==J({{"2",observation}}));
        changed["skins"].erase("2");
        assert(Cache::WriteCommerce(root,D::Mapping,changed));
        assert(Cache::ReadCommerce(root,D::Prices,absent)->empty());
        auto paths=AlterEgo::Storage::Paths(root);
        AlterEgo::Storage::WriteMigrationBytes(paths.base/"item_cache.json","[]");
        auto moved=AlterEgo::Storage::ResolveCache(root,"item_cache.json");
        assert(moved==(paths.CacheDirectory()/"item_cache.json").u8string());
        assert(AlterEgo::Storage::ReadMigrationBytes(std::filesystem::u8path(moved))=="[]");
        assert(!std::filesystem::exists(paths.base/"item_cache.json"));
    }
    {
        namespace Cache=AlterEgo::Cache;
        using J=Cache::ReferenceJson;
        using D=Cache::ReferenceDataset;
        const auto root=temp.dir.u8string();
        J skills=J::array({{{"id",10},{"name","Skill"},{"icon","icon"},{"description","d"},
            {"type","Utility"},{"specialization",12},{"professions",J::array({"Engineer","Guardian"})},
            {"facts",J::array({{{"type","Damage"},{"hit_count",2}},{{"type","Buff"},{"status","Might"}}})}}});
        assert(Cache::WriteReference(root,D::Skills,skills,100));
        assert(Cache::ReadReference(root,D::Skills)==skills);
        auto stale=skills;stale[0]["name"]="Stale";
        assert(!Cache::WriteReference(root,D::Skills,stale,99));
        const auto ordered=(temp.dir/"reference-order-regression").u8string();
        J accepted=J::array();auto newer=skills;newer[0]["name"]="New skill";
        assert(Cache::WriteReferenceObservations(ordered,D::Skills,newer,200,accepted)&&accepted==newer);
        auto older=skills;auto disjoint=skills[0];disjoint["id"]=11;older.push_back(disjoint);
        assert(Cache::WriteReferenceObservations(ordered,D::Skills,older,100,accepted)&&accepted.size()==1&&accepted[0]["id"]==11);
        auto replay=newer;replay[0]["name"]="Replay";
        assert(Cache::WriteReferenceObservations(ordered,D::Skills,replay,200,accepted)&&accepted.empty());
        auto stored=Cache::ReadReference(ordered,D::Skills);
        assert(stored&&stored->size()==2&&stored->at(0)["name"]=="New skill"&&stored->at(1)["id"]==11);
        auto broken=newer;broken[0]["professions"]=J::array({1});
        assert(!Cache::WriteReferenceObservations(ordered,D::Skills,broken,300,accepted));
        assert(Cache::ReadReference(ordered,D::Skills)==stored);
        assert(Cache::WriteReferenceObservations(ordered,D::Skills,newer,300,accepted)&&accepted.size()==1);

        assert(Cache::ReadReference(root,D::Skills)==skills);
        auto malformed=skills;malformed.push_back(skills.at(0));
        assert(!Cache::WriteReference(root,D::Skills,malformed,101));
        assert(Cache::ReadReference(root,D::Skills)==skills);
        J specs=J::array({{{"id",2},{"name","Spec"},{"icon","i"},{"background","b"},{"profession","Engineer"},
            {"profession_icon","p"},{"profession_icon_big","P"},{"elite",true},
            {"minor_traits",J::array({9,8,7})},{"major_traits",J::array({6,5,4})}}});
        assert(Cache::WriteReference(root,D::Specializations,specs,100));
        assert(Cache::ReadReference(root,D::Specializations)==specs);
        J traits=J::array({{{"id",9},{"name","Trait"},{"icon","i"},{"description","d"},
            {"tier",1},{"order",2},{"slot","Major"}}});
        assert(Cache::WriteReference(root,D::Traits,traits,100));
        assert(Cache::ReadReference(root,D::Traits)==traits);
        J stats=J::array({{{"id",1},{"name","Stat"},{"attributes",J::array({"Power","Precision"})}}});
        auto paths=AlterEgo::Storage::Paths(root);
        const auto legacy=paths.base/"itemstat_cache.json";
        AlterEgo::Storage::WriteMigrationBytes(legacy,stats.dump());
        assert(Cache::ReadReference(root,D::ItemStats)==stats);
        assert(!std::filesystem::exists(paths.CacheDirectory()/"itemstat_cache.json"));
        AlterEgo::Storage::WriteMigrationBytes(legacy,"[]");
        assert(Cache::ReadReference(root,D::ItemStats)==stats);
        J dyes=J::array({{{"id",0},{"name","Dye"},{"base",J::array({1,2,3})},
            {"cloth",J::array({4,5,6})},{"leather",J::array({7,8,9})},{"metal",J::array({10,11,12})}}});
        assert(Cache::WriteReference(root,D::Dyes,dyes,100));
        assert(Cache::ReadReference(root,D::Dyes)==dyes);
        auto badDye=dyes;badDye[0]["cloth"][1]=256;
        assert(!Cache::WriteReference(root,D::Dyes,badDye,101));
        assert(Cache::ReadReference(root,D::Dyes)==dyes);
        DB::Connection db(paths.Cache().u8string(),DB::Connection::OpenMode::Existing);
        assert(db.Scalar("PRAGMA user_version")==Cache::kSchemaVersion);
        assert(db.Scalar("SELECT count(*) FROM reference_skills_professions")==2);
    }
    {
        namespace Cache=AlterEgo::Cache;
        using J=Cache::ReferenceJson;
        using D=Cache::ItemDataset;
        const auto root=temp.dir.u8string();
        const auto paths=AlterEgo::Storage::Paths(root);
        J details=J::array({{{"id",11},{"name","Verified item"},{"icon","i"},{"rarity","Rare"},
            {"type","Armor"},{"default_skin",42},{"details",{{"type","Helm"},{"infix_upgrade",{{"id",1}}}}}}});
        assert(Cache::WriteItems(root,D::Details,details,100));
        assert(Cache::ReadItems(root,D::Details)==details);
        J aliases={{"Old item spelling",11},{"Other item",12}};
        AlterEgo::Storage::WriteMigrationBytes(paths.base/"item_name_cache.json",aliases.dump());
        auto names=Cache::ReadItems(root,D::Aliases);
        assert(names&&names->at("Old item spelling")==11&&names->at("Verified item")==11);
        assert(!std::filesystem::exists(paths.CacheDirectory()/"item_name_cache.json"));
        J map={{"version",2},{"skins",{{"42",J::array({11,12})}}},
            {"names",{{"11","Narrow stale name"},{"12","Commerce item"}}},{"vendors",J::object()}};
        assert(Cache::WriteCommerce(root,Cache::CommerceDataset::Mapping,map));
        auto commerce=Cache::ReadCommerce(root,Cache::CommerceDataset::Mapping,paths.base/"absent.json");
        assert(commerce&&commerce->at("names").at("11")=="Verified item");
        assert(commerce->at("names").at("12")=="Commerce item");
        assert(Cache::ReadItems(root,D::Details)==details);
        auto bad=details;bad.push_back(details.at(0));
        assert(!Cache::WriteItems(root,D::Details,bad,101));
        assert(!Cache::WriteItems(root,D::Details,details,99));
        assert(Cache::ReadItems(root,D::Details)==details);
        assert(Cache::WriteItems(root,D::Aliases,J{{"Old item spelling",12}},101));
        assert(!Cache::WriteItems(root,D::Aliases,aliases,100));
        assert(Cache::ReadItems(root,D::Aliases)->at("Old item spelling")==12);
        // Name-only Commerce records must never look like fetched details.
        assert(Cache::ReadItems(root,D::Details)->size()==1);
        const auto orderRoot=(temp.dir/"item-order-regression").u8string();
        // Delayed same-second responses cannot regress a newer record. Older
        // requests for disjoint IDs are still accepted.
        auto accepted=J::array();
        const auto later=Cache::ReferenceSnapshotTime();
        const auto newest=Cache::ReferenceSnapshotTime();
        assert(newest>later);
        auto row=[](uint32_t id,const char* name){return J{{"id",id},{"name",name},{"type","Armor"}};};
        assert(Cache::WriteItemObservations(orderRoot,J::array({row(99,"new")}),newest,accepted));
        assert(accepted.size()==1);
        assert(Cache::WriteItemObservations(orderRoot,J::array({row(99,"old"),row(100,"other")}),later,accepted));
        assert(accepted.size()==1&&accepted.at(0).at("id")==100);
        assert(Cache::WriteItemObservations(orderRoot,J::array({row(99,"equal")}),newest,accepted));
        assert(accepted.empty());
        auto ordered=Cache::ReadItems(orderRoot,D::Details);assert(ordered);
        bool foundNew=false,foundOther=false;
        for(const auto& entry:*ordered){
            if(entry.at("id")==99){assert(entry.at("name")=="new");foundNew=true;}
            if(entry.at("id")==100){assert(entry.at("name")=="other");foundOther=true;}
        }
        assert(foundNew&&foundOther);
        assert(!Cache::WriteItemObservations(orderRoot,J::array({row(99,"invalid"),row(99,"duplicate")}),Cache::ReferenceSnapshotTime(),accepted));
        assert(Cache::ReadItems(orderRoot,D::Details)==ordered);

    }
    {
        namespace Storage=AlterEgo::Storage;
        const auto root=temp.dir.u8string();
        const Storage::Paths paths(root);
        const auto missingRoot=(temp.dir/"missing-root").u8string();
        auto missing=Storage::InspectStorage(missingRoot);
        assert(missing.cache.status=="Not created");
        assert(!std::filesystem::exists(missingRoot));
        std::filesystem::create_directories(paths.CacheDirectory()/"textures");
        Storage::WriteMigrationBytes(paths.CacheDirectory()/"textures"/"test.dat","1234");
        const auto result=Storage::InspectStorage(root);
        assert(result.cache.healthy&&result.cache.schema==AlterEgo::Cache::kSchemaVersion&&result.artworkBytes==4);
        assert(!result.datasets.empty());
        auto items=std::find_if(result.datasets.begin(),result.datasets.end(),
            [](const auto& d){return d.label=="Items";});
        assert(items!=result.datasets.end()&&items->records==1);
        const auto foreign=temp.dir/"foreign.db";
        {
            DB::Connection db(foreign.u8string(),DB::Connection::OpenMode::Create);
            db.Exec("PRAGMA application_id=1234");
        }
        std::vector<Storage::DatasetDiagnostic> rows;
        auto rejected=Storage::InspectDatabase(foreign,AlterEgo::Cache::kApplicationId,AlterEgo::Cache::kSchemaVersion,true,rows);
        assert(!rejected.healthy&&rows.empty());
        DB::Connection readOnly(paths.Cache().u8string(),DB::Connection::OpenMode::ReadOnly);
        bool failed=false;
        try{readOnly.Exec("DELETE FROM reference_items");}catch(const DB::Error&){failed=true;}
        assert(failed);
    }
    {
        namespace Storage=AlterEgo::Storage;
        namespace Cache=AlterEgo::Cache;
        using J=Cache::CharacterRows::Json;
        const auto root=(temp.dir/"maintenance").u8string();
        const Storage::Paths paths(root);
        std::filesystem::create_directories(paths.LibraryDirectory());
        std::filesystem::create_directories(paths.ConfigDirectory());
        std::filesystem::create_directories(paths.CacheDirectory()/"wiki_images");
        Storage::WriteMigrationBytes(paths.Library() ,"durable");
        Storage::WriteMigrationBytes(paths.LibraryDirectory()/"ach_tracked.json","pins");
        Storage::WriteMigrationBytes(paths.ConfigDirectory()/"settings.json","settings");
        Storage::WriteMigrationBytes(paths.CacheDirectory()/"wiki_images"/"wiki_data.json","metadata");
        Storage::WriteMigrationBytes(paths.CacheDirectory()/"wiki_images"/"42.jpg","image");
        Storage::WriteMigrationBytes(paths.CacheDirectory()/"wiki_images"/"wiki_failed.json","failures");
        assert(Cache::WriteCharacters(root,J{{"last_updated",100},{"characters",J::array({{{"name","Hero"},{"account_name","A"}}})}}));
        Cache::AchievementSnapshot progress;progress.fetchedAt=100;
        assert(Cache::WriteAchievements(root,"A",progress));
        using WikiKind=Cache::Wiki::Kind;
        const auto wikiLegacy=paths.CacheDirectory()/"wiki_images"/"legacy_metadata.json";
        const J wikiData={{"42",{{"acquisition","Dungeon"},{"vendor","Vendor"}}}};
        Storage::WriteMigrationBytes(wikiLegacy,wikiData.dump());
        assert(Cache::Wiki::Read(root,WikiKind::Metadata,wikiLegacy)==wikiData);
        assert(!std::filesystem::exists(wikiLegacy));
        Storage::WriteMigrationBytes(wikiLegacy,"{}");
        assert(Cache::Wiki::Read(root,WikiKind::Metadata,wikiLegacy)==wikiData);
        const J wikiFailures={{"skins",{{"42",100}}},{"currencies",{{"Karma",200}}}};
        assert(Cache::Wiki::Write(root,WikiKind::Failures,wikiFailures));
        assert(!Cache::Wiki::Write(root,WikiKind::Metadata,J{{"bad",{{"vendor","Bad"}}}}));
        assert(Cache::Wiki::Read(root,WikiKind::Metadata,wikiLegacy)==wikiData);
        Storage::WriteMigrationBytes(paths.base/"characters.json","stale legacy");
        std::string error;
        assert(Storage::ScheduleMaintenance(root,Storage::CacheOperation::Characters,error));
        assert(!Storage::ScheduleMaintenance(root,Storage::CacheOperation::All,error));
        assert(Cache::ReadCharacters(root).has_value()); // not a live deletion
        auto result=Storage::RunStartupMaintenance(root);
        assert(result.attempted&&result.success&&!Storage::MaintenancePending(root));
        assert(!Cache::ReadCharacters(root).has_value());
        assert(Cache::ReadAchievements(root,"A").has_value());
        assert(!std::filesystem::exists(paths.base/"characters.json"));
        assert(Storage::ScheduleMaintenance(root,Storage::CacheOperation::Artwork,error));
        assert(Storage::RunStartupMaintenance(root).success);
        assert(!std::filesystem::exists(paths.CacheDirectory()/"wiki_images"/"42.jpg"));
        assert(!std::filesystem::exists(paths.CacheDirectory()/"wiki_images"/"wiki_failed.json"));
        assert(Storage::ReadMigrationBytes(paths.CacheDirectory()/"wiki_images"/"wiki_data.json")=="metadata");
        assert(Cache::Wiki::Read(root,WikiKind::Metadata,wikiLegacy)==wikiData);
        assert(Cache::Wiki::Read(root,WikiKind::Failures,paths.base/"missing.json")==
            (J{{"skins",J::object()},{"currencies",J::object()}}));
        // Replace the disposable DB with corrupt bytes: full recovery never
        // opens it or silently discards it.
        std::filesystem::remove(paths.Cache());
        Storage::WriteMigrationBytes(paths.Cache(),"damaged database");
        Storage::WriteMigrationBytes(paths.CacheDirectory()/"cache.db-wal","sidecar");
        assert(Storage::ScheduleMaintenance(root,Storage::CacheOperation::All,error));
        assert(Storage::RunStartupMaintenance(root).success);
        assert(!std::filesystem::exists(paths.CacheDirectory()));
        bool quarantined=false;
        for(const auto& entry:std::filesystem::directory_iterator(paths.CacheQuarantine())) {
            const auto candidate=entry.path()/"cache"/"cache.db";
            if(std::filesystem::exists(candidate)) {
                assert(Storage::ReadMigrationBytes(candidate)=="damaged database");
                assert(Storage::ReadMigrationBytes(candidate.parent_path()/"cache.db-wal")=="sidecar");
                quarantined=true;
            }
        }
        assert(quarantined);
        assert(Storage::ReadMigrationBytes(paths.Library())=="durable");
        assert(Storage::ReadMigrationBytes(paths.LibraryDirectory()/"ach_tracked.json")=="pins");
        assert(Storage::ReadMigrationBytes(paths.ConfigDirectory()/"settings.json")=="settings");
        assert(!Storage::RunStartupMaintenance(root).attempted);
    }
    {
        namespace Cache=AlterEgo::Cache;
        using J=Cache::CharacterRows::Json;
        const auto root=(temp.dir/"characters").u8string();
        const AlterEgo::Storage::Paths paths(root);
        J bt={{"tab",1},{"is_active",true},{"name","Build"},{"profession","Ranger"},
            {"pets",{1,2,3,4}},{"legends",{"a","b","c","d"}},{"weapons",{5,6}},
            {"specializations",J::array({{{"id",1},{"traits",{1,2,3}}},{{"id",2},{"traits",{3,2,1}}},{{"id",3},{"traits",{2,1,3}}}})},
            {"skills",{{"heal",10},{"utilities",{11,12,13}},{"elite",14}}}};
        J eq={{"id",42},{"slot","Helm"},{"tab",2},{"binding","Character"},{"bound_to","Hero"},
            {"upgrades",{3,4}},{"infusions",{9,8,7}},{"dyes",{0,22}},{"attributes",{{"Power",123}}}};
        J tool={{"id",43},{"slot","Pick"},{"tab",0},{"charges",0},{"upgrades",{55}}};
        J lure={{"id",44},{"slot","FishingLure"},{"tab",0},{"charges",17}};
        J ch={{"name","Hero"},{"account_name","A.B"},{"data_refreshed",99},{"crafting_cached",true},
            {"crafting",J::array({{{"discipline","Chef"},{"rating",400},{"active",false}}})},
            {"equipment",J::array({eq,tool,lure})},{"build_tabs",J::array({bt})},{"equipment_tab_names",{{"2","Gear"}}}};
        J other=ch;other["account_name"]="A_B";
        J unknown=ch;unknown.erase("account_name");unknown["build_tabs"][0].erase("pets");unknown["build_tabs"][0].erase("legends");unknown["build_tabs"][0].erase("weapons");
        J data={{"last_updated",100},{"characters",J::array({ch,other,unknown})}};
        std::filesystem::create_directories(paths.base);
        auto legacyData=data;legacyData["characters"][0]["heropoints"]={"0-29","0-10"};
        AlterEgo::Storage::WriteMigrationBytes(paths.base/"characters.json",legacyData.dump());
        auto read=Cache::ReadCharacters(root);assert(read&&read->at("characters").size()==3);
        assert(!read->at("characters")[0].contains("heropoints"));
        assert(read->at("characters")[0]==ch);
        assert(!read->at("characters")[0].contains("core_verified")); // old aggregate is not per-scope evidence
        assert(!read->at("characters")[0]["equipment"][0].contains("charges"));
        assert(read->at("characters")[0]["equipment"][1]["charges"]==0);
        assert(read->at("characters")[0]["equipment"][2]["charges"]==17);
        struct ToolRow {int id;std::string slot;int tab;};
        const std::vector<ToolRow> tools{{1,"Pick",2},{2,"Pick",0},{3,"Axe",3},{4,"Sickle",2}};
        assert(AlterEgo::CharacterTools::Find(tools,"Pick",2)->id==2);
        assert(!AlterEgo::CharacterTools::Find(tools,"Axe",2));
        assert(AlterEgo::CharacterTools::Find(tools,"Sickle",2)->id==4);
        assert(!AlterEgo::CharacterTools::Find(tools,"Helm",2));
        assert(read->at("characters")[1]==other);
        assert(read->at("characters")[2].at("account_name")=="");
        assert(!read->at("characters")[2]["build_tabs"][0].contains("pets"));
        assert(!read->at("characters")[2]["build_tabs"][0].contains("weapons"));
        auto bad=data;bad["characters"].push_back(ch);
        assert(!Cache::WriteCharacters(root,bad));assert(Cache::ReadCharacters(root)==read);
        auto stale=data;stale["last_updated"]=99;
        assert(!Cache::WriteCharacters(root,stale));assert(Cache::ReadCharacters(root)==read);
        auto updated=data;updated["last_updated"]=101;updated["characters"][0]["equipment"][0]["infusions"]={99};
        assert(Cache::WriteCharacters(root,updated));
        assert(Cache::ReadCharacters(root)->at("characters")[0]["equipment"][0]["infusions"]==J::array({99}));
        DB::Connection db(paths.Cache().u8string(),DB::Connection::OpenMode::Existing);
        assert(db.Scalar("SELECT count(*) FROM character_rows")==3);
        const auto beforeIncrement=Cache::ReadCharacters(root);
        auto one=ch;one["data_refreshed"]=102;one["equipment"][0]["infusions"]={77};
        one["core_verified"]=100;one["tabs_verified"]=102;one["physical_verified"]=101;
        one["equipment"][1]["physical_supplement"]=true;
        J change={{"last_updated",102},{"characters",J::array({one})}};
        assert(Cache::HasCharacterSnapshot(root)==std::optional<bool>(true));
        assert(Cache::WriteCharacters(root,change,true));
        auto afterIncrement=Cache::ReadCharacters(root);
        assert(afterIncrement->at("characters")[0]==one);
        assert(!afterIncrement->at("characters")[0].contains("builds_verified"));
        assert(afterIncrement->at("characters")[0]["equipment"][1]["physical_supplement"]==true);
        assert(afterIncrement->at("characters")[1]==beforeIncrement->at("characters")[1]);
        assert(afterIncrement->at("characters")[2]==beforeIncrement->at("characters")[2]);
        assert(!Cache::WriteCharacters(root,J{{"last_updated",103},{"characters",J::array({ch})}},true));
        assert(Cache::ReadCharacters(root)==afterIncrement);
        auto malformed=one;malformed["data_refreshed"]=103;malformed["equipment"][0]["infusions"]="invalid";
        assert(!Cache::WriteCharacters(root,J{{"last_updated",103},{"characters",J::array({malformed})}},true));
        assert(Cache::ReadCharacters(root)==afterIncrement);
        auto invalidScope=one;invalidScope["data_refreshed"]=103;invalidScope["core_verified"]=-1;
        assert(!Cache::WriteCharacters(root,J{{"last_updated",103},{"characters",J::array({invalidScope})}},true));
        assert(Cache::ReadCharacters(root)==afterIncrement);
        auto newChar=one;newChar["name"]="New Hero";
        assert(Cache::WriteCharacters(root,J{{"last_updated",103},{"characters",J::array({newChar})}},true));
        const auto beforeDelete=Cache::ReadCharacters(root);
        assert(beforeDelete->at("characters").size()==4);
        // Abort mid-transaction: neither account may be partially removed.
        db.Exec("CREATE TRIGGER reject_roster_delete BEFORE DELETE ON character_rows WHEN OLD.account_name='A_B' BEGIN SELECT RAISE(ABORT,'test rollback'); END");
        assert(!Cache::DeleteAbsentCharacters(root,{{"A.B",{"New Hero"}},{"A_B",{}}}));
        assert(Cache::ReadCharacters(root)==beforeDelete);
        db.Exec("DROP TRIGGER reject_roster_delete");
        assert(Cache::DeleteAbsentCharacters(root,{{"A.B",{"New Hero"}},{"",{}}}));
        const auto afterDelete=Cache::ReadCharacters(root);
        assert(afterDelete->at("characters").size()==3);
        assert(afterDelete->at("characters")[0]==beforeDelete->at("characters")[1]);
        assert(afterDelete->at("characters")[1]==beforeDelete->at("characters")[2]);
        assert(afterDelete->at("characters")[2]==beforeDelete->at("characters")[3]);
        assert(db.Scalar("SELECT count(*) FROM character_rows WHERE character_id=1")==0);
        assert(db.Scalar("SELECT count(*) FROM character_equipment WHERE character_id=1")==0);
        assert(db.Scalar("SELECT count(*) FROM character_rows WHERE character_id=4")==1);
        assert(Cache::DeleteAbsentCharacters(root,{{"A.B",{"New Hero"}}}));
        assert(Cache::ReadCharacters(root)==afterDelete);
        db.Verify();
    }
    {
        namespace Cache=AlterEgo::Cache;
        using D=Cache::CompletionDataset;
        using J=Cache::CompletionRows::Json;
        const auto root=(temp.dir/"completion").u8string();
        const AlterEgo::Storage::Paths paths(root);
        std::filesystem::create_directories(paths.base);
        J clear={{"id",9125},{"name","Strikes"},{"tier","Weekly"},{"done",false},{"current",1},{"max",3},
            {"bitNames",{"Bears","Old Lion's Court","Other"}},{"bitDone",{1,0,0}}};
        J clears={{"account","A.B"},{"fetch_time",100},{"dailyFractals",J::array()},
            {"dailyBounties",J::array()},{"weeklyWings",J::array()},{"weeklyStrikes",clear}};
        AlterEgo::Storage::WriteMigrationBytes(paths.base/"clears_cache_A_B.json",clears.dump());
        assert(!Cache::ReadCompletion(root,D::Clears,"A_B"));
        assert(!Cache::HasCompletion(root,D::Clears,"A_B"));
        assert(Cache::ReadCompletion(root,D::Clears,"A.B")==clears);
        auto other=clears;other["account"]="A_B";other["weeklyStrikes"]["bitDone"]={0,1,0};
        assert(Cache::WriteCompletion(root,D::Clears,"A_B",other));
        assert(Cache::ReadCompletion(root,D::Clears,"A.B")==clears);
        assert(Cache::ReadCompletion(root,D::Clears,"A_B")==other);
        auto stale=clears;stale["fetch_time"]=99;
        assert(!Cache::WriteCompletion(root,D::Clears,"A.B",stale));
        auto bad=clears;bad["fetch_time"]=101;bad["weeklyStrikes"]["bitDone"]={2};
        assert(!Cache::WriteCompletion(root,D::Clears,"A.B",bad));
        assert(Cache::ReadCompletion(root,D::Clears,"A.B")==clears);
        J period={{"meta_progress_current",1},{"meta_progress_complete",6},{"meta_reward_astral",450},
            {"meta_reward_claimed",false},{"objectives",J::array({{{"id",42},{"title","Weekly boss"},{"track","PvE"},
                {"acclaim",50},{"progress_current",1},{"progress_complete",1},{"claimed",true}}})}};
        J vault={{"fetch_time",200},{"season_end",300},{"daily",period},{"weekly",period},{"special",period}};
        AlterEgo::Storage::WriteMigrationBytes(paths.base/"vault_cache.json",vault.dump());
        assert(!Cache::ReadCompletion(root,D::Vault,"A.B",false));
        vault["account"]="A.B";
        for(const char* section:{"daily","weekly","special"})vault[section]["fetched_at"]=200;
        assert(Cache::ReadCompletion(root,D::Vault,"A.B",true)==vault);
        assert(Cache::HasCompletion(root,D::Vault,"A.B"));
        assert(!Cache::HasCompletion(root,D::Vault,"A_B"));
        auto reset=vault;reset["fetch_time"]=201;reset["weekly"]["fetched_at"]=201;reset["weekly"]["objectives"][0]["claimed"]=false;
        reset["weekly"]["objectives"][0]["progress_current"]=0;
        assert(Cache::WriteCompletion(root,D::Vault,"A.B",reset));
        assert(Cache::ReadCompletion(root,D::Vault,"A.B")==reset);
        assert(!Cache::WriteCompletion(root,D::Vault,"A.B",vault));
        assert(!Cache::WriteCompletion(root,D::Vault,"A_B",vault));
        DB::Connection db(paths.Cache().u8string(),DB::Connection::OpenMode::Existing);db.Verify();
        assert(db.Scalar("SELECT count(*) FROM clears_snapshots")==2);
        // A partial current-day commit must not relabel a failed prior section.
        J partial={{"account","A.B"},{"dailyFractals",J::array({clear})},{"section_stamps",{{"dailyFractals",300}}}};
        assert(Cache::WriteClearsSections(root,"A.B",partial));
        auto merged=Cache::ReadCompletion(root,D::Clears,"A.B");
        assert(merged&&merged->at("section_stamps").at("dailyFractals")==300&&merged->at("section_stamps").at("weeklyStrikes")==100);
        J independent={{"account","A.B"},{"weeklyStrikes",clear},{"section_stamps",{{"weeklyStrikes",250}}}};
        assert(Cache::WriteClearsSections(root,"A.B",independent));
        auto both=Cache::ReadCompletion(root,D::Clears,"A.B");
        assert(both->at("fetch_time")==300&&both->at("section_stamps").at("weeklyStrikes")==250);
        independent["section_stamps"]["weeklyStrikes"]=249;independent["weeklyStrikes"]["name"]="stale";
        assert(Cache::WriteClearsSections(root,"A.B",independent));
        assert(Cache::ReadCompletion(root,D::Clears,"A.B")==both);
        partial["section_stamps"]["dailyFractals"]=301;partial["dailyFractals"]=J::array();
        assert(!Cache::WriteClearsSections(root,"A.B",partial));
        assert(Cache::ReadCompletion(root,D::Clears,"A.B")==both);
        assert(db.Scalar("SELECT count(*) FROM vault_snapshots")==1);
    }
    return 0;
}
