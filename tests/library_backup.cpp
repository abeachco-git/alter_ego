#include "../src/LibraryBackup.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
using namespace AlterEgo;
using namespace AlterEgo::Backup;
namespace fs = std::filesystem;

static void Put(const std::string& path, const std::string& bytes) {
    fs::create_directories(fs::u8path(path).parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    assert(file && file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())));
}
static std::string Get(const std::string& path) {
    std::string text, error;
    assert(BackupZip::ReadBytes(path, text, error));
    return text;
}
int case_library_backup(int, char**) {
    fs::path base = fs::temp_directory_path() / "alter_ego_backup_regression";
    fs::remove_all(base);
    fs::create_directories(base / "source");
    fs::create_directories(base / "target");
    std::string src = (base / "source").u8string();
    std::string dst = (base / "target").u8string();
    DecodedWardrobeLink w{};
    w.helm_skin = 111;
    w.helm_dyes[0] = 1;
    w.chest_dyes[2] = 642;
    const std::string wardrobe = ChatLink::EncodeWardrobe(w);
    AlterEgo::Fashion::Look f;
    f.id = "fashion-1"; f.name = "Saved Wardrobe";
    f.wardrobe_link = wardrobe; f.weight = "Heavy";
    AlterEgo::Fashion::Associate(f, "Warrior");
    const CharacterIdentity::Provenance sourceIdentity{"account-a","Hero",{"2015-02-03T04:05:06Z","Warrior","Human"}};
    f.source_character=sourceIdentity.name;f.source_identity=sourceIdentity;
    assert(AlterEgo::Fashion::AssociateVerified(f,sourceIdentity));
    nlohmann::json fashion = {{"version", 1}, {"looks", nlohmann::json::array({AlterEgo::Fashion::ToJson(f)})}};
    nlohmann::json builds = {{"builds", nlohmann::json::array({
        {{"id", "build-a"}, {"name", "Build A"}, {"profession", "Warrior"},
         {"source_character",sourceIdentity.name},{"source_identity",CharacterIdentity::EncodeProvenance(sourceIdentity)}}
    })}};
    Put(FilePath(src,"build_library.json"),builds.dump());
    Put(FilePath(src,"fashion_library.json"),fashion.dump());
    Put(FilePath(src,"settings.json"),R"({"show_age":true})");
    Put(FilePath(src,"session.json"),R"({"main_tab":0,"selected_character_ref":{"account":"A","local_id":42,"name":"Alice"},"compare_character_ref":{"account":"B","local_id":3}})");
    Put(FilePath(src,"ach_tracked.json"),R"({"pinned":[1,3,5]})");
    CharacterActivity::Tracker history;
    const CharacterActivity::Key hero{"account-a","Hero"};
    history.Observe(hero.first,hero.second,34,true,2000);
    history.Begin(hero,"Bags",2010);
    Put(FilePath(src,"character_activity.json"),history.Save().dump());
    const CharacterIdentity::Evidence creation{"2015-02-03T04:05:06Z","Warrior","Human"};
    const CharacterLoadout::PortableChoice preference{hero.first,hero.second,creation,{CharacterLoadout::Mode::Explicit,"build-a"}};
    {
        const auto path=Storage::Paths(src).Accounts();
        fs::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        CharacterIdentity::InitializeAccountRegistry(db);
        const auto registry=CharacterIdentity::Commit(db,hero.first,0,{hero.second},{{hero.second,creation}});
        assert(registry&&*registry);
        Database::Transaction tx(db);
        assert(CharacterLoadout::SaveChoice(db,{hero.first,registry->next.Current(hero.second)->id},preference.choice));
        tx.Commit();
    }
    const std::string portrait("\x89PNG\r\n\0test", 11);
    Put(FilePath(src,"portraits/a.png"),portrait);
    std::string err;
    const std::string allZip = (base / "full.zip").u8string();
    assert(Export(src, Builds|AlterEgo::Backup::Fashion|Other, allZip, true, err));
    Package full;
    assert(ReadPackage(allZip, full, err));
    assert(full.zip && full.available == (Builds|AlterEgo::Backup::Fashion|Other));
    assert(full.buildCount == 1 && full.fashionCount == 1);
    assert(full.files.at("portraits/a.png") == portrait);
    assert(full.files.count("settings.json") == 1);
    const auto localSettings=R"({"inventory_expanded_groups":["identity:local-id-and-evidence","shared-group"],"show_age":true})";
    const auto sanitized=nlohmann::json::parse(PortableSettings(localSettings));
    assert(sanitized.at("inventory_expanded_groups")==nlohmann::json::array({"shared-group"}));
    assert(sanitized.at("show_age")==true);
    const auto session=nlohmann::json::parse(full.files.at("session.json"));
    assert(!session["selected_character_ref"].contains("local_id")&&!session["compare_character_ref"].contains("local_id"));
    assert(!full.files.count("character_activity.json"));
    assert(!full.files.count("accounts.db"));
    const auto choices=CharacterLoadout::DecodePortable(nlohmann::json::parse(full.files.at("character_loadouts.json")));
    assert(choices.size()==1&&choices.count(preference));
    assert(!nlohmann::json::parse(full.files.at("character_loadouts.json")).at("entries")[0].contains("local_id"));
    assert(!Export(src,Builds,Storage::Paths(src).Library().u8string(),false,err));
    assert(!Export(src,Builds,(Storage::Paths(src).ConfigDirectory()/"settings.json").u8string(),false,err));
    assert(!Export(src,Builds,(Storage::Paths(src).Recovery()/"original.json").u8string(),false,err));
    const auto normal=(Storage::Paths(src).base/"backups"/"portable.zip").u8string();
    assert(Export(src,Builds,normal,true,err));
    Package portableBackup;assert(ReadPackage(normal,portableBackup,err));
    const auto atomicPath=base/"atomic.json";
    assert(Storage::ReplaceFile(atomicPath,"old",err));
    assert(Storage::ReplaceFile(atomicPath,"new",err));
    assert(Get(atomicPath.u8string())=="new");
    assert(!Storage::ReplaceFile(base/"source","bad",err));
    assert(fs::is_directory(base/"source"));
    const auto portable=nlohmann::json::parse(full.files.at("character_logins.json"));
    assert(CharacterActivity::DecodePortableLogins(portable).at({hero,{}})==2000);
    assert(!portable.at("entries")[0].contains("scopes"));
    assert(!portable.at("entries")[0].contains("last_seen"));

    // Single-collection export produces a directly importable JSON file.
    const std::string onlyBuilds = (base / "legacy-export.json").u8string();
    assert(Export(src, Builds, onlyBuilds, false, err));
    Package individual;
    assert(ReadPackage(onlyBuilds, individual, err));
    assert(individual.available == Builds);
    const std::string onlyFashion = (base / "fashion_library.json").u8string();
    assert(Export(src, AlterEgo::Backup::Fashion, onlyFashion, false, err));
    assert(ReadPackage(onlyFashion, individual, err));
    assert(individual.available == AlterEgo::Backup::Fashion);

    // Merge skips matching IDs, preserves existing values, and adds new entries.
    Put(FilePath(dst,"build_library.json"),R"({"builds":[{"id":"build-a","name":"Local version"},{"id":"build-b","name":"Local B"}]})");
    Put(FilePath(dst,"fashion_library.json"),R"({"version":1,"looks":[]})");
    Put(FilePath(dst,"settings.json"),R"({"show_age":false})");
    Plan plan;
    assert(Prepare(dst, full, Builds|AlterEgo::Backup::Fashion|Other, false, plan, err));
    assert(plan.buildAdded == 0 && plan.buildSkipped == 1);
    assert(plan.fashionAdded == 1 && plan.fashionSkipped == 0);
    assert(plan.otherSkipped == 1);
    std::string recovery;
    assert(Apply(dst, plan, recovery, err));
    assert(Exists(recovery));
    NativeLibrary::Snapshot native;
    assert(NativeLibrary::RuntimeStore().Read(native,err));
    nlohmann::json merged;assert(Composable::ToLegacy(native.loadouts,merged,err));
    assert(merged["builds"].size() == 2);
    assert(merged["builds"][0]["name"] == "Local version");
    assert(native.fashion.size()==1);
    assert(native.fashion[0].source_identity&&*native.fashion[0].source_identity==sourceIdentity);
    assert(native.fashion[0].associations.size()==1&&native.fashion[0].associations[0]==sourceIdentity);
    // Merge must preserve the existing local Loadout, including its absent
    // provenance. The replacement test below verifies incoming provenance.
    assert(!Composable::FindLoadout(native.loadouts,"build-a")->metadata.contains("source_identity"));
    assert(Get(FilePath(dst,"settings.json")) == R"({"show_age":false})");
    assert(Get(FilePath(dst,"portraits/a.png")) == portrait);
    assert(!Exists(FilePath(dst,"character_activity.json")));
    assert(CharacterLoadout::DecodePortable(nlohmann::json::parse(Get(FilePath(dst,"character_loadouts.json")))).count(preference));
    assert(CharacterActivity::DecodePortableLogins(nlohmann::json::parse(Get(FilePath(dst,"character_logins.json")))).at({hero,{}})==2000);
    Put(FilePath(dst,"character_logins.json"),CharacterActivity::EncodeLoginHistory({{hero,3000}}).dump());

    // Replace a selected collection, never touch unchecked collections.
    assert(Prepare(dst, full, AlterEgo::Backup::Fashion, true, plan, err));
    assert(plan.libraryChanged&&plan.writeFiles.empty());
    assert(Apply(dst, plan, recovery, err));
    assert(NativeLibrary::RuntimeStore().Read(native,err)&&native.loadouts.loadouts.size()==2);

    // Restore "Other" only, preserving builds and fashion.
    assert(Prepare(dst, full, Other, true, plan, err));
    assert(Apply(dst, plan, recovery, err));
    assert(Get(FilePath(dst,"settings.json")) == R"({"show_age":true})");
    assert(CharacterActivity::DecodePortableLogins(nlohmann::json::parse(Get(FilePath(dst,"character_logins.json")))).at({hero,{}})==3000);

    assert(Prepare(dst,full,Builds,true,plan,err));
    assert(Apply(dst,plan,recovery,err));
    assert(NativeLibrary::RuntimeStore().Read(native,err)&&native.loadouts.loadouts.size()==1);
    assert(Composable::FindLoadout(native.loadouts,"build-a")->metadata.at("source_identity")==CharacterIdentity::EncodeProvenance(sourceIdentity));

    // Empty collection files are valid backups and may clear a selected collection.
    Put((base/"empty.json").u8string(), R"({"builds":[]})");
    Package empty;
    assert(ReadPackage((base/"empty.json").u8string(), empty, err));
    assert(Prepare(dst, empty, Builds, true, plan, err));
    assert(Apply(dst, plan, recovery, err));
    assert(NativeLibrary::RuntimeStore().Read(native,err)&&native.loadouts.loadouts.empty());
    assert(!Exists(FilePath(dst,"build_library.json")));
    assert(!Prepare(dst, empty, AlterEgo::Backup::Fashion, true, plan, err));

    // Corrupt CRC and archive traversal must fail before touching any files.
    std::string bytes = Get(allZip);
    bytes[36] ^= 1;
    Put((base/"broken.zip").u8string(),bytes);
    Package invalid;
    assert(!ReadPackage((base/"broken.zip").u8string(),invalid,err));
    BackupZip::Entries evil{{"../settings.json","{}"}};
    assert(!BackupZip::WriteStored(evil,(base/"evil.zip").u8string(),err));

    // A manually named settings JSON can be restored individually.
    const std::string settings = (base/"settings.json").u8string();
    Put(settings, R"({"font":{"px":18}})");
    assert(ReadPackage(settings, individual, err));
    assert(individual.available == Other);
    // Manually renamed JSON still resolves to the correct settings collection.
    const std::string renamed = (base/"my_settings_backup.json").u8string();
    Put(renamed, R"({"show_qa_icon":false,"auto_refresh_enabled":false})");
    assert(ReadPackage(renamed, individual, err));
    assert(individual.available == Other);
    assert(individual.files.count("settings.json") == 1);
    NativeLibrary::RuntimeStore().Close();
    fs::remove_all(base);
    return 0;
}
