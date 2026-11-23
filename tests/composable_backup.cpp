#include "../src/LibraryBackup.h"
#include "../src/ComposableStore.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

using namespace AlterEgo;
using namespace AlterEgo::Backup;
namespace fs = std::filesystem;
using Json = nlohmann::json;

static void Put(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out);
    out << bytes;
    out.close();
    assert(out.good());
}
static std::string Get(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    assert(in);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

int main() {
    const fs::path base = fs::temp_directory_path() / "ae_composable_backup_test";
    fs::remove_all(base);
    const fs::path source = base / "source", destination = base / "dest";
    fs::create_directories(source);
    fs::create_directories(destination);
    std::string error;

    const Json original = Json::parse(R"({"builds":[
       {"id":"one","name":"Import","profession":"Engineer",
        "skills":{"heal":9,"utilities":[1,2,3],"elite":4},
        "gear":{"WeaponA1":{"stat_id":1130,"sigil_id":1},
                "Helm":{"stat_id":1130,"rune_id":333}}},
       {"id":"two","name":"Second","profession":"Necromancer"}
    ]})");
    Composable::Document imported;
    assert(Composable::SplitLegacy(original, imported, error));
    assert(Composable::LinkFashion(imported,"one","fashion-reference",error));
    Put(source / "loadouts_v3.json", Composable::ToJson(imported).dump(2));
    Put(source / "build_library.json", original.dump(2));

    const std::string bundle = (base / "full.zip").u8string();
    assert(Export(source.u8string(), Builds, bundle, true, error));
    Package package;
    assert(ReadPackage(bundle, package, error));
    assert(package.files.count("loadouts_v3.json"));
    assert(package.files.count("build_library.json"));
    assert(package.buildCount == 2);

    // A standalone JSON export still preserves the old build-library format.
    assert(Export(source.u8string(), Builds,
                  (base / "standalone.json").u8string(), false, error));
    Package standalone;
    assert(ReadPackage((base / "standalone.json").u8string(), standalone, error));
    assert(standalone.files.count("build_library.json"));
    assert(!standalone.files.count("loadouts_v3.json"));

    // Merge a new graph with a conflicting component ID. Existing objects are
    // not overwritten or linked to an imported component with different data.
    Json localLegacy = Json::parse(R"({"builds":[
       {"id":"one","name":"Local version","profession":"Warrior"},
       {"id":"three","name":"Local third","profession":"Engineer"}
    ]})");
    Composable::Document local;
    assert(Composable::SplitLegacy(localLegacy, local, error));
    Put(destination / "loadouts_v3.json", Composable::ToJson(local).dump(2));
    Put(destination / "build_library.json", localLegacy.dump(2));

    Plan plan;
    assert(Prepare(destination.u8string(), package, Builds, false, plan, error));
    assert(plan.buildAdded == 1 && plan.buildSkipped == 1);
    assert(plan.writeFiles.count("loadouts_v3.json"));
    assert(plan.writeFiles.count("build_library.json"));
    std::string recovery;
    assert(Apply(destination.u8string(), plan, recovery, error));
    assert(fs::exists(fs::u8path(recovery)));

    Composable::Document combined;
    assert(Composable::FromJson(
        Json::parse(Get(destination / "loadouts_v3.json")), combined, error));
    assert(combined.loadouts.size() == 3);
    assert(Composable::FindLoadout(combined,"one")->metadata["name"] == "Local version");
    assert(Composable::FindLoadout(combined,"two")->metadata["name"] == "Second");
    assert(Composable::FindLoadout(combined,"three")->metadata["name"] == "Local third");

    // Replace recovers all refs, even optional Fashion, without requiring
    // the actual Fashion library to be present.
    assert(Prepare(destination.u8string(), package, Builds, true, plan, error));
    assert(Apply(destination.u8string(), plan, recovery, error));
    assert(Composable::FromJson(
        Json::parse(Get(destination / "loadouts_v3.json")), combined, error));
    assert(combined.loadouts.size() == 2);
    assert(Composable::FindLoadout(combined,"one")->fashion_id == "fashion-reference");

    // Legacy JSON imports are upgraded to v3, not written behind its back.
    const Json older = Json::parse(R"({"builds":[
       {"id":"old","name":"Old format","profession":"Mesmer"}
    ]})");
    Put(base / "old.json", older.dump(2));
    assert(ReadPackage((base / "old.json").u8string(), standalone, error));
    assert(Prepare(destination.u8string(), standalone, Builds, false, plan, error));
    assert(plan.buildAdded == 1);
    assert(Apply(destination.u8string(), plan, recovery, error));
    assert(Composable::FromJson(
        Json::parse(Get(destination / "loadouts_v3.json")), combined, error));
    assert(combined.loadouts.size() == 3);
    assert(Composable::FindLoadout(combined,"old") != nullptr);
    Json legacyMirror;
    assert(Composable::ToLegacy(combined, legacyMirror, error));
    assert(legacyMirror["builds"] ==
           Json::parse(Get(destination / "build_library.json"))["builds"]);

    // Reject a package whose monolithic and component payloads disagree.
    BackupZip::Entries invalidFiles = package.files;
    Json altered = Json::parse(invalidFiles["build_library.json"]);
    altered["builds"][0]["name"] = "Conflicts with v3";
    invalidFiles["build_library.json"] = altered.dump(2);
    const auto badZip = (base / "bad.zip").u8string();
    assert(BackupZip::WriteStored(invalidFiles,badZip,error));
    Package invalidPackage;
    assert(ReadPackage(badZip,invalidPackage,error));
    assert(!Prepare(destination.u8string(),invalidPackage,Builds,true,plan,error));

    fs::remove_all(base);
    return 0;
}
