#include "../src/NativeLibraryStore.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace Native=AlterEgo::NativeLibrary;
namespace C=AlterEgo::Composable;
namespace DB=AlterEgo::Database;
namespace fs=std::filesystem;
using Json=C::Json;
namespace {
struct Scratch {
    fs::path dir;
    Scratch(){dir=fs::temp_directory_path()/("ae-migration-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));assert(fs::create_directory(dir));}
    ~Scratch(){std::error_code ec;fs::remove_all(dir,ec);}
};
void Write(const fs::path& path,const std::string& bytes) {
    fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);
    out<<bytes;assert(out.good());
}
std::string Read(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
}
int case_native_migration(int,char**) {
    Scratch scratch;const auto root=scratch.dir/"migration";
    Native::Paths paths(root.u8string());std::string error;
    Json first={{"id","a"},{"name","A"},{"profession","Necromancer"},
        {"skills",{{"heal",10},{"utilities",{1,2,3}},{"elite",4}}},
        {"gear",{{"Helm",{{"stat_id",1},{"rune_id",9}}}}}};
    auto second=first;second["id"]="b";second["name"]="B";
    // Identical payload alone is insufficient for legacy Skills consolidation.
    auto unknownA=first;unknownA["id"]="unknown-a";unknownA.erase("profession");
    auto unknownB=unknownA;unknownB["id"]="unknown-b";
    auto other=first;other["id"]="other";other["profession"]="Engineer";
    C::Document isolated;
    assert(Native::SplitLegacyExact(Json{{"builds",Json::array({first,second,unknownA,unknownB,other})}},isolated,error));
    assert(isolated.components.at("skills").size()==4);
    assert(isolated.loadouts[0].refs.at("skills")==isolated.loadouts[1].refs.at("skills"));
    assert(isolated.loadouts[2].refs.at("skills")!=isolated.loadouts[3].refs.at("skills"));
    assert(isolated.loadouts[4].refs.at("skills")!=isolated.loadouts[0].refs.at("skills"));
    Json legacy={{"builds",Json::array({first,second})}};
    AlterEgo::DecodedWardrobeLink w{};w.helm_skin=42;
    Json look={{"id","f"},{"name","Fashion"},{"wardrobe_link",AlterEgo::ChatLink::EncodeWardrobe(w)},
               {"future",Json::array({1,2,3})}};
    Json fashion={{"looks",Json::array({look})}};
    const auto legacyBytes=legacy.dump(2),fashionBytes=fashion.dump(2);
    Write(root/"build_library.json",legacyBytes);Write(root/"fashion_library.json",fashionBytes);
    // Simulate abandoned staging data from an interrupted migration.
    Write(root/"library"/".ae-stage-abandoned"/"library.db","partial");
    Native::Store store;assert(store.Open(root.u8string(),error));
    Native::Snapshot snapshot;assert(store.Read(snapshot,error));
    assert(snapshot.loadouts.loadouts.size()==2&&snapshot.fashion.size()==1);
    assert(snapshot.loadouts.components.at("base_build").size()==1);
    assert(snapshot.loadouts.components.at("skills").size()==1);
    assert(snapshot.fashion.front().extension.at("future")==look.at("future"));
    assert(snapshot.loadouts.loadouts[0].refs==snapshot.loadouts.loadouts[1].refs);
    assert(!fs::exists(root/"build_library.json")&&!fs::exists(root/"fashion_library.json"));
    size_t archived=0;
    for(const auto& entry:fs::directory_iterator(paths.Migration())) {
        assert(Read(entry.path()/"build_library.json")==legacyBytes);
        assert(Read(entry.path()/"fashion_library.json")==fashionBytes);++archived;
    }
    assert(archived==1);
    auto next=snapshot;next.loadouts.loadouts.clear();next.loadouts.components.clear();next.fashion.clear();
    assert(store.Commit(snapshot,next,error));
    const auto backup=paths.Recovery()/"library.db";assert(store.Backup(backup.u8string(),error));
    assert(!store.Backup(backup.u8string(),error));
    store.Close();
    Write(root/"build_library.json",legacyBytes); // initialized empty DB must win
    assert(store.Open(root.u8string(),error));assert(store.Read(snapshot,error));
    assert(snapshot.loadouts.loadouts.empty()&&snapshot.fashion.empty());
    Native::Repository recovery(backup.u8string(),DB::Connection::OpenMode::Existing);
    assert(recovery.Read().loadouts.loadouts.empty());store.Close();

    // Existing v4 graph split aquatic fields across physical and upgrade components.
    const auto v4root=scratch.dir/"v4";Native::Paths v4paths(v4root.u8string());
    fs::create_directories(v4paths.Library().parent_path());
    Native::Snapshot old;assert(Native::SplitLegacyExact(legacy,old.loadouts,error));
    const auto refs=old.loadouts.loadouts.front().refs;
    C::Find(old.loadouts,"trinkets",refs.at("trinkets"))->data["gear"]["HelmAquatic"]={{"stat_id",1},{"future","retain"}};
    C::Find(old.loadouts,"runes",refs.at("runes"))->data["gear"]["HelmAquatic"]={{"rune_id",9}};
    C::Find(old.loadouts,"weapons",refs.at("weapons"))->data["gear"]["WeaponAquaticA"]={{"weapon_type","Trident"},{"stat_id",1}};
    C::Find(old.loadouts,"sigils",refs.at("sigils"))->data["gear"]["WeaponAquaticA"]={{"sigil_id",10},{"sigil2_id",11}};
    C::Find(old.loadouts,"infusions",refs.at("infusions"))->data["gear"]["WeaponAquaticA"]={{"infusion_ids",{42,43}},{"infusion_attributes",{"Power",""}},{"infusion_snapshot",true}};
    old.loadouts.equipment_sets.push_back({"mixed","Mixed",{{"trinkets",refs.at("trinkets")},{"runes",refs.at("runes")}}});
    for(auto& l:old.loadouts.loadouts){l.equipment_set_id="mixed";l.refs.erase("trinkets");l.refs.erase("runes");}
    old.loadouts.components["armor"].push_back({"unused","Unused",Json{{"gear",Json{{"HelmAquatic",Json{{"item_id",99}}}}}}});
    Json beforeWater;assert(C::ToLegacy(old.loadouts,beforeWater,error));
    {
        Native::Repository repo(v4paths.Library().u8string(),DB::Connection::OpenMode::Create);
        old.revision=repo.Commit(repo.Read(),old);
    }
    {DB::Connection db(v4paths.Library().u8string(),DB::Connection::OpenMode::Existing);db.Exec("DROP TABLE loadout_source_identity;DROP TABLE fashion_character_identity;PRAGMA user_version=4");}
    assert(store.Open(v4root.u8string(),error));Native::Snapshot migrated;assert(store.Read(migrated,error));
    Json afterWater;assert(C::ToLegacy(migrated.loadouts,afterWater,error)&&beforeWater==afterWater);
    assert(migrated.revision==old.revision+1);
    assert(migrated.loadouts.loadouts[0].refs.at("aquatic")==migrated.loadouts.loadouts[1].refs.at("aquatic"));
    assert(!migrated.loadouts.equipment_sets.front().refs.count("aquatic"));
    assert(migrated.loadouts.components.at("aquatic").size()==3);
    assert(migrated.loadouts.loadouts[0].refs.at("weapons")==refs.at("weapons"));
    assert(!C::Find(migrated.loadouts,"weapons",refs.at("weapons"))->data.contains("gear"));
    bool backupFound=false;for(const auto& entry:fs::directory_iterator(v4paths.Migration())){
        DB::Connection backup((entry.path()/"library.db").u8string(),DB::Connection::OpenMode::ReadOnly);
        assert(backup.Scalar("PRAGMA user_version")==4);backupFound=true;
    }
    assert(backupFound);store.Close();
    assert(store.Open(v4root.u8string(),error));assert(store.Read(snapshot,error));
    assert(snapshot.revision==migrated.revision);store.Close();
    const auto freshParts=C::Split(beforeWater.at("builds")[0]);
    assert(freshParts.at("aquatic").at("gear").at("HelmAquatic").at("rune_id")==9);
    assert(freshParts.at("aquatic").at("gear").at("WeaponAquaticA").at("infusion_ids")==Json::array({42,43}));
    assert(C::Split(first).at("aquatic").empty());

    const auto corrupt=scratch.dir/"corrupt";Native::Paths corruptPaths(corrupt.u8string());
    Write(corruptPaths.Library(),"");Write(corrupt/"build_library.json",legacyBytes);
    assert(!store.Open(corrupt.u8string(),error));
    assert(Read(corruptPaths.Library()).empty()&&Read(corrupt/"build_library.json")==legacyBytes);
    const auto malformed=scratch.dir/"malformed";Write(malformed/"build_library.json","invalid");
    assert(!store.Open(malformed.u8string(),error));
    assert(!fs::exists(Native::Paths(malformed.u8string()).Library()));
    const auto v3=scratch.dir/"experimental";Write(v3/"loadouts_v3.json","{}");
    assert(!store.Open(v3.u8string(),error));assert(error.find("Experimental v3")!=std::string::npos);
    const auto fresh=scratch.dir/"fresh";
    assert(store.Open(fresh.u8string(),error));assert(store.Read(snapshot,error));
    assert(snapshot.revision==0&&snapshot.loadouts.loadouts.empty());
    assert(!fs::exists(Native::Paths(fresh.u8string()).Cache()));
    return 0;
}
