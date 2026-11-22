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
int main() {
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
    Fashion::Look f;
    f.id = "fashion-1"; f.name = "Saved Wardrobe";
    f.wardrobe_link = wardrobe; f.weight = "Heavy";
    Fashion::Associate(f, "Warrior");
    nlohmann::json fashion = {{"version", 1}, {"looks", nlohmann::json::array({Fashion::ToJson(f)})}};
    nlohmann::json builds = {{"builds", nlohmann::json::array({
        {{"id", "build-a"}, {"name", "Build A"}, {"profession", "Warrior"}}
    })}};
    Put(FilePath(src,"build_library.json"),builds.dump());
    Put(FilePath(src,"fashion_library.json"),fashion.dump());
    Put(FilePath(src,"settings.json"),R"({"show_age":true})");
    Put(FilePath(src,"ach_tracked.json"),R"({"pinned":[1,3,5]})");
    const std::string portrait("\x89PNG\r\n\0test", 11);
    Put(FilePath(src,"portraits/a.png"),portrait);
    std::string err;
    const std::string allZip = (base / "full.zip").u8string();
    assert(Export(src, Builds|Fashion|Other, allZip, true, err));
    Package full;
    assert(ReadPackage(allZip, full, err));
    assert(full.zip && full.available == (Builds|Fashion|Other));
    assert(full.buildCount == 1 && full.fashionCount == 1);
    assert(full.files.at("portraits/a.png") == portrait);
    assert(full.files.count("settings.json") == 1);

    // Single-collection export produces a directly importable JSON file.
    const std::string onlyBuilds = (base / "legacy-export.json").u8string();
    assert(Export(src, Builds, onlyBuilds, false, err));
    Package individual;
    assert(ReadPackage(onlyBuilds, individual, err));
    assert(individual.available == Builds);
    const std::string onlyFashion = (base / "fashion_library.json").u8string();
    assert(Export(src, Fashion, onlyFashion, false, err));
    assert(ReadPackage(onlyFashion, individual, err));
    assert(individual.available == Fashion);

    // Merge skips matching IDs, preserves existing values, and adds new entries.
    Put(FilePath(dst,"build_library.json"),R"({"builds":[{"id":"build-a","name":"Local version"},{"id":"build-b","name":"Local B"}]})");
    Put(FilePath(dst,"fashion_library.json"),R"({"version":1,"looks":[]})");
    Put(FilePath(dst,"settings.json"),R"({"show_age":false})");
    Plan plan;
    assert(Prepare(dst, full, Builds|Fashion|Other, false, plan, err));
    assert(plan.buildAdded == 0 && plan.buildSkipped == 1);
    assert(plan.fashionAdded == 1 && plan.fashionSkipped == 0);
    assert(plan.otherSkipped == 1);
    std::string recovery;
    assert(Apply(dst, plan, recovery, err));
    assert(Exists(recovery));
    const auto merged = nlohmann::json::parse(Get(FilePath(dst,"build_library.json")));
    assert(merged["builds"].size() == 2);
    assert(merged["builds"][0]["name"] == "Local version");
    assert(nlohmann::json::parse(Get(FilePath(dst,"fashion_library.json")))["looks"].size() == 1);
    assert(Get(FilePath(dst,"settings.json")) == R"({"show_age":false})");
    assert(Get(FilePath(dst,"portraits/a.png")) == portrait);

    // Replace a selected collection, never touch unchecked collections.
    assert(Prepare(dst, full, Fashion, true, plan, err));
    assert(plan.writeFiles.size() == 1);
    assert(Apply(dst, plan, recovery, err));
    assert(nlohmann::json::parse(Get(FilePath(dst,"build_library.json")))["builds"].size() == 2);

    // Restore "Other" only, preserving builds and fashion.
    assert(Prepare(dst, full, Other, true, plan, err));
    assert(Apply(dst, plan, recovery, err));
    assert(Get(FilePath(dst,"settings.json")) == R"({"show_age":true})");

    // Empty collection files are valid backups and may clear a selected collection.
    Put((base/"empty.json").u8string(), R"({"builds":[]})");
    Package empty;
    assert(ReadPackage((base/"empty.json").u8string(), empty, err));
    assert(Prepare(dst, empty, Builds, true, plan, err));
    assert(Apply(dst, plan, recovery, err));
    assert(nlohmann::json::parse(Get(FilePath(dst,"build_library.json")))["builds"].empty());
    assert(!Prepare(dst, empty, Fashion, true, plan, err));

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
    fs::remove_all(base);
}
