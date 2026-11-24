#include "../src/NativeLibraryEditing.h"
#include <cassert>
#include <chrono>
#include <filesystem>

namespace Native=AlterEgo::NativeLibrary;
namespace C=AlterEgo::Composable;
namespace DB=AlterEgo::Database;
using Json=C::Json;
namespace {
struct Scratch {
    std::filesystem::path dir;
    Scratch() {
        dir=std::filesystem::temp_directory_path()/("ae-native-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        if(!std::filesystem::create_directory(dir))throw std::runtime_error("test directory");
    }
    ~Scratch(){std::error_code ec;std::filesystem::remove_all(dir,ec);}
    std::string db() const {return (dir/"library.db").u8string();}
};
}
int case_native_library(int,char**) {
    Scratch tmp;
    Json legacy={{"builds",Json::array({
        {{"id","first"},{"name","First"},{"notes","note"},{"game_modes",{"PvE","WvW"}},
         {"profession","Necromancer"},{"specializations",Json::array({
            {{"id",50},{"traits",{1,2,3}},{"future",Json::array()}}})},
         {"pets",Json::array()},{"legend_codes",{0,0,0,0}},
         {"skills",{{"heal",12},{"utilities",{3,2,1}},{"elite",6}}},
         {"aquatic_skills",Json::object()},
         {"gear",{{"Helm",{{"item_id",50},{"stat_id",0},{"stat_name",""},
                           {"rune_id",4},{"infusion_ids",{11,22,33}},
                           {"infusion_attributes",{"Power","","Precision"}},
                           {"infusion_snapshot",true},{"future",nullptr}}},
                   {"WeaponA1",{{"item_id",21},{"weapon_type","Greatsword"},
                               {"sigil_id",31},{"sigil2_id",32}}},
                   {"Ring1",{{"stat_id",5}}}}},
         {"rune_id",4},{"relic_id",8},{"infusion_ar_total",18},
         {"infusion_summary_present",true},{"infusion_stat_totals",{{"Power",5}}},
         {"infusion_totals",Json::array({{{"item_id",11},{"count",1},{"attribute","Power"}}})},
         {"extension",{{"unknown",true}}}}})}};
    Native::Snapshot expected;
    std::string error;
    assert(C::SplitLegacy(legacy,expected.loadouts,error));
    AlterEgo::DecodedWardrobeLink wardrobe{};
    wardrobe.helm_skin=321;wardrobe.helm_dyes[2]=98;wardrobe.visibility_flags=7;
    AlterEgo::Fashion::Look look;
    look.id="fashion-one";look.name="Look";look.wardrobe_link=AlterEgo::ChatLink::EncodeWardrobe(wardrobe);
    look.characters={"Character 1","Character 2"};expected.fashion.push_back(look);
    expected.loadouts.loadouts[0].fashion_id=look.id;
    // Intentional duplicate and distinct physical placement are retained.
    auto sigil=expected.loadouts.components.at("sigils").front();
    sigil.id="identical";sigil.name="Intentional copy";
    expected.loadouts.components["sigils"].push_back(sigil);
    auto swapped=sigil;swapped.id="different";swapped.name="Swapped";
    std::swap(swapped.data["gear"]["WeaponA1"]["sigil_id"],
              swapped.data["gear"]["WeaponA1"]["sigil2_id"]);
    expected.loadouts.components["sigils"].push_back(swapped);
    {
        // Constant hash forces a collision without relying on chance.
        Native::Repository repo(tmp.db(),DB::Connection::OpenMode::Create,
            [](const Json&){return std::string("collision");});
        auto empty=repo.Read();assert(empty.revision==0);
        assert(repo.Commit(empty,expected)==1);
        auto read=repo.Read();
        Json resolved;assert(C::ToLegacy(read.loadouts,resolved,error));
        assert(resolved.at("builds")==legacy.at("builds"));
        assert(read.loadouts.loadouts.front().fashion_id==look.id);
        assert(AlterEgo::Fashion::ToJson(read.fashion.front())==AlterEgo::Fashion::ToJson(look));
        assert(read.loadouts.components.at("sigils").size()==3);
        auto exact=repo.ExactMatches("sigils",sigil.data);
        assert(exact.size()==2&&exact[0].users==1&&exact[1].users==0);
        assert(repo.Users("sigils",sigil.id).empty());
        assert(repo.Users("sigils","sigils:first")==std::vector<std::string>{"first"});
        assert(repo.Commit(read,read)==1);
        auto update=read;
        update.loadouts.loadouts.front().metadata["name"]="Renamed";
        assert(repo.Commit(read,update)==2);
        bool stale=false;
        try{repo.Commit(read,update);}catch(const DB::Error&){stale=true;}
        assert(stale);
        auto invalid=repo.Read();auto original=invalid;
        invalid.loadouts.loadouts.front().refs["armor"]="nonexistent";
        bool rejected=false;
        try{repo.Commit(original,invalid);}catch(const DB::Error&){rejected=true;}
        assert(rejected&&repo.Read().revision==2);
    }
    {
        Native::Repository repo(tmp.db(),DB::Connection::OpenMode::Existing,
            [](const Json&){return std::string("collision");});
        auto state=repo.Read();assert(state.revision==2);
        assert(state.loadouts.loadouts.front().metadata.at("name")=="Renamed");
        assert(state.loadouts.components.at("sigils").size()==3);
        auto next=state;
        Json raw;assert(C::Resolve(next.loadouts,next.loadouts.loadouts.front(),raw,error));
        assert(Native::PutEquipmentSet(next.loadouts,"set1","Equipment",raw,error));
        assert(next.loadouts.equipment_sets.front().refs.size()==7);
        const auto& refs=next.loadouts.equipment_sets.front().refs;
        assert(refs.at("sigils")==next.loadouts.loadouts.front().refs.at("sigils"));
        auto remove=next.loadouts;
        assert(!C::RemoveUnreferenced(remove,"sigils",refs.at("sigils"),error));
        assert(Native::ApplyEquipmentSet(next.loadouts,"first","set1",error));
        assert(repo.Commit(state,next)==3);
        const auto restored=repo.Read();
        assert(C::ToJson(restored.loadouts)==C::ToJson(next.loadouts));
        assert(repo.Users("sigils",refs.at("sigils")).size()==2);
        C::Document portable;
        assert(C::FromJson(C::ToJson(restored.loadouts),portable,error));
        assert(portable.equipment_sets.front().refs==refs);
        assert(Native::ConsolidateExact(portable,"sigils",refs.at("sigils"),"identical",error));
        assert(portable.equipment_sets.front().refs.at("sigils")=="identical");
        C::Document merged;size_t added=0,skipped=0;
        assert(C::Merge(state.loadouts,portable,false,merged,added,skipped,error));
        assert(merged.equipment_sets.size()==1);
        assert(C::Validate(merged,error));
    }
    assert(Native::ExactFingerprint(sigil.data)!=Native::ExactFingerprint(swapped.data));
    return 0;
}
