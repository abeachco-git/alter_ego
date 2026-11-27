#include "../src/NativeLibraryPortable.h"
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
    namespace CI=AlterEgo::CharacterIdentity;
    const CI::Evidence creation{"2020-01-01T02:03:04Z","Necromancer","Human"};
    const CI::Provenance source{"A","Original",creation};
    legacy["builds"][0]["source_character"]=source.name;
    legacy["builds"][0]["source_identity"]=CI::EncodeProvenance(source);
    Native::Snapshot expected;
    std::string error;
    assert(C::SplitLegacy(legacy,expected.loadouts,error));
    AlterEgo::DecodedWardrobeLink wardrobe{};
    wardrobe.helm_skin=321;wardrobe.helm_dyes[2]=98;wardrobe.visibility_flags=7;
    AlterEgo::Fashion::Look look;
    look.id="fashion-one";look.name="Look";look.wardrobe_link=AlterEgo::ChatLink::EncodeWardrobe(wardrobe);
    look.characters={"Character 1","Character 2"};
    look.source_character=source.name;look.source_identity=source;
    assert(AlterEgo::Fashion::AssociateVerified(look,source));
    assert(!AlterEgo::Fashion::AssociateVerified(look,{"A","Renamed",creation}));
    assert(AlterEgo::Fashion::AssociateVerified(look,{"B","Original",creation}));
    expected.fashion.push_back(look);
    expected.loadouts.loadouts[0].fashion_id=look.id;
    // Exercise a populated v7 companion migration. Use the old ownership
    // representation via direct relational rows and exact old fingerprint,
    // then check backed-up migration, shared identities and flat round-trip.
    {
        Scratch old;
        auto prior=expected;
        const auto baseId=prior.loadouts.loadouts[0].refs.at("base_build");
        auto* base=C::Find(prior.loadouts,"base_build",baseId);
        base->data["profession"]="Ranger";base->data.erase("pets");
        prior.loadouts.loadouts[0].metadata.erase("source_identity");
        prior.loadouts.loadouts[0].metadata.erase("source_character");
        {
            Native::Repository repo(old.db(),DB::Connection::OpenMode::Create);
            assert(repo.Commit(repo.Read(),prior)==1);
        }
        base->data["pets"]={1,2,3,4};
        {
            DB::Connection db(old.db(),DB::Connection::OpenMode::Existing);
            db.Exec("PRAGMA user_version=7");
            auto q=db.Prepare("UPDATE components SET fingerprint=? WHERE kind='base_build' AND id=?");
            q.Bind(1,Native::ExactFingerprint(base->data));q.Bind(2,baseId);q.Step();
            auto flags=db.Prepare("UPDATE component_payloads SET flags=flags|2 WHERE kind='base_build' AND component_id=?");
            flags.Bind(1,baseId);flags.Step();
            for(int i=0;i<4;++i){
                auto row=db.Prepare("INSERT INTO build_choices VALUES('base_build',?,'pets',?,?)");
                row.Bind(1,baseId);row.Bind(2,static_cast<std::int64_t>(i));row.Bind(3,static_cast<std::int64_t>(i+1));row.Step();
            }
        }
        Json before;assert(C::ToLegacy(prior.loadouts,before,error));
        Native::Repository migrated(old.db(),DB::Connection::OpenMode::Existing);
        const auto actual=migrated.Read();
        assert(actual.revision==2&&actual.loadouts.loadouts[0].refs.count("pets"));
        assert(C::Find(actual.loadouts,"base_build",baseId));
        Json after;assert(C::ToLegacy(actual.loadouts,after,error)&&before==after);
        assert(C::ValidateCurrentCompanionOwnership(actual.loadouts,error));
    }
    // Both companion types retain typed ordering, optional presence, unknown
    // fields and independent identities through native persistence and the
    // complete portable Library. Selective import keeps unused components.
    {
        Scratch companionDb;
        Json input={{"builds",Json::array({
            {{"id","ranger"},{"name","Ranger"},{"profession","Ranger"},
             {"pets",Json::array({4,0,8,2})}},
            {{"id","revenant"},{"name","Revenant"},{"profession","Revenant"},
             {"legend_codes",Json::array({2,1,0,3})}},
            {{"id","omitted"},{"name","Omitted"},{"profession","Ranger"}},
            {{"id","empty"},{"name","Empty"},{"profession","Ranger"},{"pets",Json::array()}}
        })}};
        Native::Snapshot source;
        assert(C::SplitLegacy(input,source.loadouts,error));
        auto* ranger=C::FindLoadout(source.loadouts,"ranger");
        auto* revenant=C::FindLoadout(source.loadouts,"revenant");
        assert(ranger&&revenant);
        const auto petsId=ranger->refs.at("pets"),legendsId=revenant->refs.at("legends");
        auto* pets=C::Find(source.loadouts,"pets",petsId);
        assert(pets);
        pets->data["future_companion"]={{"ordered",Json::array({1,0,3})}};
        auto unused=*pets;unused.id="unused-pets";unused.name="Unused pets";
        unused.data["pets"]=Json::array({8,4,0,2});
        source.loadouts.components["pets"].push_back(unused);
        assert(C::DuplicateLoadout(source.loadouts,"ranger","shared","Shared Ranger",123,error));
        assert(C::FindLoadout(source.loadouts,"shared")->refs.at("pets")==petsId);
        assert(!C::FindLoadout(source.loadouts,"omitted")->refs.count("pets"));
        assert(C::FindLoadout(source.loadouts,"empty")->refs.count("pets"));
        Native::Repository repo(companionDb.db(),DB::Connection::OpenMode::Create);
        assert(repo.Commit(repo.Read(),source)==1);
        const auto stored=repo.Read();
        assert(C::ToJson(stored.loadouts)==C::ToJson(source.loadouts));
        const auto packet=Native::ExportPortableLibrary(stored,true,false);
        Native::PortableLibrary parsed;
        assert(Native::ParsePortableLibrary(packet,parsed,error));
        assert(parsed.loadouts&&!parsed.fashion);
        Native::Snapshot restored;
        Native::ImportCounts counts;
        assert(Native::MergePortableLibrary(Native::Snapshot{},parsed,true,false,true,restored,counts,error));
        assert(C::ToJson(restored.loadouts)==C::ToJson(stored.loadouts));
        assert(C::Find(restored.loadouts,"pets","unused-pets"));
        assert(C::FindLoadout(restored.loadouts,"shared")->refs.at("pets")==petsId);
        assert(C::FindLoadout(restored.loadouts,"revenant")->refs.at("legends")==legendsId);

        auto bad=stored;
        C::FindLoadout(bad.loadouts,"ranger")->refs["pets"]="does-not-exist";
        bool rejected=false;
        try{repo.Commit(stored,bad);}catch(const DB::Error&){rejected=true;}
        assert(rejected&&repo.Read().revision==1);
        auto changed=stored;
        assert(C::Assign(changed.loadouts,"ranger","pets","unused-pets",error));
        assert(repo.Commit(stored,changed)==2);
        bool stale=false;
        try{repo.Commit(stored,source);}catch(const DB::Error&){stale=true;}
        assert(stale);
        const auto final=repo.Read();
        assert(C::FindLoadout(final.loadouts,"ranger")->refs.at("pets")=="unused-pets");
        assert(C::FindLoadout(final.loadouts,"shared")->refs.at("pets")==petsId);
        assert(C::FindLoadout(final.loadouts,"revenant")->refs.at("legends")==legendsId);
    }
    // The external build-link protocol must preserve both pairs and the six
    // inactive utility palette IDs independently of the active skill bars.
    for(const auto profession:{AlterEgo::PROF_RANGER,AlterEgo::PROF_REVENANT}){
        AlterEgo::DecodedBuildLink original{};
        original.profession=profession;
        for(int i=0;i<5;++i){
            original.terrestrial_skills[i]=static_cast<uint16_t>(600+i);
            original.aquatic_skills[i]=static_cast<uint16_t>(900-i);
        }
        if(profession==AlterEgo::PROF_RANGER){
            original.pets[0]=4;original.pets[1]=8;original.pets[2]=0;original.pets[3]=2;
        }else{
            original.legends[0]=2;original.legends[1]=1;original.legends[2]=0;original.legends[3]=3;
            for(int i=0;i<6;++i)original.inactive_rev_skills[i]=static_cast<uint16_t>(i==3?0:4000-i*17);
        }
        const auto encoded=AlterEgo::ChatLink::EncodeBuild(original);
        AlterEgo::DecodedBuildLink decoded{};
        assert(AlterEgo::ChatLink::DecodeBuild(encoded,decoded));
        assert(decoded.profession==original.profession);
        for(int i=0;i<4;++i){
            assert(decoded.pets[i]==original.pets[i]);
            assert(decoded.legends[i]==original.legends[i]);
        }
        for(int i=0;i<5;++i){
            assert(decoded.terrestrial_skills[i]==original.terrestrial_skills[i]);
            assert(decoded.aquatic_skills[i]==original.aquatic_skills[i]);
        }
        for(int i=0;i<6;++i)assert(decoded.inactive_rev_skills[i]==original.inactive_rev_skills[i]);
        assert(AlterEgo::ChatLink::EncodeBuild(decoded)==encoded);
    }
    // A populated v5 library migrates without losing legacy labels, content
    // or revision. The two new typed tables are additive.
    {
        Scratch old;
        Native::Snapshot prior=expected;
        prior.loadouts.loadouts[0].metadata.erase("source_identity");
        prior.fashion[0].source_identity.reset();prior.fashion[0].associations.clear();
        {
            Native::Repository repo(old.db(),DB::Connection::OpenMode::Create);
            assert(repo.Commit(repo.Read(),prior)==1);
        }
        {
            DB::Connection db(old.db(),DB::Connection::OpenMode::Existing);
            db.Exec("DROP TABLE loadout_source_identity;DROP TABLE fashion_character_identity;ALTER TABLE components DROP COLUMN name_origin;PRAGMA user_version=5;");
        }
        {
            Native::Repository migrated(old.db(),DB::Connection::OpenMode::Existing);
            const auto read=migrated.Read();
            assert(read.revision==1&&C::ToJson(read.loadouts)==C::ToJson(prior.loadouts));
            assert(AlterEgo::Fashion::ToJson(read.fashion[0])==AlterEgo::Fashion::ToJson(prior.fashion[0]));
            assert(migrated.Commit(read,expected)==2);
            const auto actual=migrated.Read();
            assert(C::ToJson(actual.loadouts)==C::ToJson(expected.loadouts));
            assert(AlterEgo::Fashion::ToJson(actual.fashion[0])==AlterEgo::Fashion::ToJson(look));
        }
    }
    // v6 names are protected regardless of their spelling. New provenance
    // is persisted independently of exact payload/fingerprint and survives
    // repository reopen, including a name-origin-only revision.
    {
        Scratch old;
        auto prior=expected;
        {
            Native::Repository repo(old.db(),DB::Connection::OpenMode::Create);
            assert(repo.Commit(repo.Read(),prior)==1);
        }
        {
            DB::Connection db(old.db(),DB::Connection::OpenMode::Existing);
            db.Exec("ALTER TABLE components DROP COLUMN name_origin;PRAGMA user_version=6;");
        }
        {
            Native::Repository migrated(old.db(),DB::Connection::OpenMode::Existing);
            auto read=migrated.Read();assert(read.revision==1);
            assert(C::ToJson(read.loadouts)==C::ToJson(prior.loadouts));
            auto after=read;
            after.loadouts.components.at("armor").front().nameOrigin=C::NameOrigin::Manual;
            assert(migrated.Commit(read,after)==2);
        }
        Native::Repository reopened(old.db(),DB::Connection::OpenMode::Existing);
        auto read=reopened.Read();assert(read.revision==2);
        assert(read.loadouts.components.at("armor").front().nameOrigin==C::NameOrigin::Manual);
        assert(read.loadouts.components.at("armor").front().data==prior.loadouts.components.at("armor").front().data);
    }
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
        const auto refs=next.loadouts.equipment_sets.front().refs;
        assert(refs.at("sigils")==next.loadouts.loadouts.front().refs.at("sigils"));
        auto remove=next.loadouts;
        assert(!C::RemoveUnreferenced(remove,"sigils",refs.at("sigils"),error));
        assert(Native::ApplyEquipmentSet(next.loadouts,"first","set1",error));
        assert(next.loadouts.loadouts.front().equipment_set_id=="set1");
        assert(next.loadouts.loadouts.front().refs.size()==2);
        assert(Native::ExactEquipmentSets(next.loadouts,raw).size()==1);
        auto second=next.loadouts.loadouts.front();second.id="second";second.metadata["id"]="second";
        next.loadouts.loadouts.push_back(second);
        assert(C::EquipmentSetUsers(next.loadouts,"set1").size()==2);
        assert(C::EffectiveComponentUsers(next.loadouts,"sigils",refs.at("sigils")).size()==2);
        auto nameOnly=next.loadouts;Json nameDraft;
        assert(C::Resolve(nameOnly,nameOnly.loadouts.front(),nameDraft,error));
        nameDraft["name"]="Linked rename";assert(Native::PutLoadout(nameOnly,nameDraft,error));
        assert(nameOnly.loadouts.front().equipment_set_id=="set1");
        auto invalid=next.loadouts;invalid.loadouts.front().refs["sigils"]=refs.at("sigils");
        assert(!C::Validate(invalid,error));
        invalid=next.loadouts;invalid.equipment_sets.clear();assert(!C::Validate(invalid,error));
        // Source review uses the same projection transaction: private edits
        // fork linked sets, shared consent changes the original, untouched
        // categories and both original owners remain intact until commit.
        {
            auto draftDoc=next.loadouts;Json original;assert(C::Resolve(draftDoc,draftDoc.loadouts.front(),original,error));
            auto staged=original;staged["gear"]["WeaponA1"]["sigil_id"]=987654;
            const auto beforeGraph=C::ToJson(draftDoc);
            auto review=Native::UnifiedChanges(draftDoc,draftDoc.loadouts.front(),original,staged);
            assert(!review.empty()&&C::ToJson(draftDoc)==beforeGraph);
            auto sharedDraft=draftDoc;
            assert(Native::PutUnifiedLoadout(draftDoc,staged,{},error));
            Json first,second;assert(C::Resolve(draftDoc,draftDoc.loadouts[0],first,error)&&C::Resolve(draftDoc,draftDoc.loadouts[1],second,error));
            assert(first["gear"]["WeaponA1"]["sigil_id"]==987654&&second["gear"]["WeaponA1"]["sigil_id"]!=987654);
            assert(draftDoc.loadouts[0].equipment_set_id!=draftDoc.loadouts[1].equipment_set_id);
            assert(Native::PutUnifiedLoadout(sharedDraft,staged,{"sigils"},error));
            assert(C::Resolve(sharedDraft,sharedDraft.loadouts[1],second,error)&&second["gear"]["WeaponA1"]["sigil_id"]==987654);
            assert(sharedDraft.loadouts[0].equipment_set_id==sharedDraft.loadouts[1].equipment_set_id);
        }
        auto privateEdit=next.loadouts;
        Json edited;assert(C::Resolve(privateEdit,privateEdit.loadouts.front(),edited,error));
        edited["gear"]["WeaponA1"]["sigil_id"]=999;
        assert(!Native::PutLoadout(privateEdit,edited,error));
        assert(Native::DetachEquipmentSet(privateEdit,"first",error));
        assert(Native::PutLoadout(privateEdit,edited,error));
        assert(privateEdit.equipment_sets.front().refs.at("sigils")==refs.at("sigils"));
        assert(privateEdit.loadouts.front().refs.at("sigils")!=refs.at("sigils"));
        assert(Native::ApplyEquipmentSet(privateEdit,"first","set1",error));
        assert(privateEdit.loadouts.front().equipment_set_id=="set1");
        assert(Native::SaveSetFromLoadout(privateEdit,"first","copy","Copy",error));
        assert(Native::AssignSetComponent(privateEdit,"set1","sigils","different",error));
        Json one,two;
        assert(C::Resolve(privateEdit,privateEdit.loadouts[0],one,error));
        assert(C::Resolve(privateEdit,privateEdit.loadouts[1],two,error));
        assert(one["gear"]["WeaponA1"]["sigil_id"]!=two["gear"]["WeaponA1"]["sigil_id"]);
        auto shared=next.loadouts;
        assert(Native::AssignSetComponent(shared,"set1","sigils","different",error));
        assert(C::Resolve(shared,shared.loadouts[0],one,error)&&C::Resolve(shared,shared.loadouts[1],two,error));
        assert(one["gear"]==two["gear"]);
        assert(repo.Commit(state,next)==3);
        const auto restored=repo.Read();
        assert(C::ToJson(restored.loadouts)==C::ToJson(next.loadouts));
        assert(repo.Users("sigils",refs.at("sigils")).size()==1);
        auto rejected=restored;rejected.loadouts.equipment_sets.clear();bool blocked=false;
        try{repo.Commit(restored,rejected);}catch(const DB::Error&){blocked=true;}
        assert(blocked&&repo.Read().revision==restored.revision);
        auto changed=restored;assert(Native::AssignSetComponent(changed.loadouts,"set1","sigils","different",error));
        assert(repo.Commit(restored,changed)==4);
        bool stale=false;try{repo.Commit(restored,changed);}catch(const DB::Error&){stale=true;}
        assert(stale&&repo.Read().revision==4);
        auto partial=repo.Read();
        auto mixed=partial;
        assert(Native::AssignSetComponent(mixed.loadouts,"set1","relic","",error));
        assert(Native::AssignSetComponent(mixed.loadouts,"set1","sigils","",error));
        assert(mixed.loadouts.equipment_sets.front().refs.size()==5);
        assert(mixed.loadouts.loadouts.front().refs.at("relic")==refs.at("relic"));
        assert(C::Assign(mixed.loadouts,"first","sigils","identical",error));
        assert(mixed.loadouts.loadouts[1].refs.at("sigils")=="different");
        assert(!C::Assign(mixed.loadouts,"first","armor",refs.at("armor"),error));
        assert(repo.Commit(partial,mixed)==5);
        auto mixedRead=repo.Read();
        assert(C::ToJson(mixedRead.loadouts)==C::ToJson(mixed.loadouts));
        Json mixedPacket;
        assert(C::ExportPortableLoadout(mixedRead.loadouts,"first",mixedPacket,error));
        assert(mixedPacket.at("equipment_set_kinds").size()==5);
        auto importedMixed=mixedRead.loadouts;
        assert(C::ImportPortableLoadout(importedMixed,mixedPacket,"mixed-import",error));
        assert(importedMixed.loadouts.back().refs.count("relic"));
        assert(importedMixed.loadouts.back().refs.count("sigils"));
        const auto beforeInclude=mixedRead;
        assert(Native::AssignSetComponent(mixedRead.loadouts,"set1","sigils","different",error));
        assert(!mixedRead.loadouts.loadouts.front().refs.count("sigils"));
        assert(repo.Commit(beforeInclude,mixedRead)==6);
        // Capture only selected categories, switch sets without dropping the
        // old inherited categories, then edit a newly independent relic.
        auto selected=mixedRead.loadouts;
        assert(Native::PutEquipmentSet(selected,"armor-only","Armor",raw,error,{},{"armor"}));
        assert(C::FindEquipmentSet(selected,"armor-only")->refs.size()==1);
        assert(Native::ApplyEquipmentSet(selected,"first","armor-only",error));
        assert(selected.loadouts.front().refs.count("sigils"));
        assert(selected.loadouts.front().refs.count("relic"));
        assert(!selected.loadouts.front().refs.count("armor"));
        Json draft;assert(C::Resolve(selected,selected.loadouts.front(),draft,error));
        draft["relic_id"]=999;
        assert(Native::PutLoadout(selected,draft,error));
        Json other;assert(C::Resolve(selected,selected.loadouts[1],other,error));
        assert(other["relic_id"]!=999);
        assert(Native::SaveSetFromLoadout(selected,"first","armor-copy","Armor copy",error));
        assert(C::FindEquipmentSet(selected,"armor-copy")->refs.size()==1);
        assert(Native::SaveSetFromLoadout(selected,"first","custom","Armor and relic",error,{"armor","relic"}));
        assert(C::FindEquipmentSet(selected,"custom")->refs.size()==2);
        assert(selected.loadouts.front().refs.count("sigils"));
        assert(!selected.loadouts.front().refs.count("relic"));
        assert(C::Validate(selected,error));
        assert(!Native::AssignSetComponent(selected,"armor-only","armor","",error));
        const auto matches=Native::ExactEquipmentSets(selected,raw);
        assert(std::any_of(matches.begin(),matches.end(),[](const auto& m){return m.id=="armor-only";}));
        auto staged=mixedRead.loadouts;
        const auto unchanged=C::ToJson(staged);
        auto proposedRefs=C::FindEquipmentSet(staged,"set1")->refs;
        proposedRefs.erase("armor");proposedRefs["relic"]=refs.at("relic");
        auto invalidRefs=proposedRefs;invalidRefs["armor"]="does-not-exist";
        assert(!Native::SaveEquipmentSetDraft(staged,"set1","Rejected",invalidRefs,error));
        assert(C::ToJson(staged)==unchanged);
        assert(!Native::SaveEquipmentSetDraft(staged,"set1","Empty",{},error));
        assert(C::ToJson(staged)==unchanged);
        assert(Native::SaveEquipmentSetDraft(staged,"set1","Updated",proposedRefs,error));
        for(const auto& l:staged.loadouts){
            assert(l.refs.at("armor")==refs.at("armor"));
            assert(!l.refs.count("relic"));
        }
        auto newDirect=staged.loadouts.front().refs;
        newDirect["sigils"]="identical"; // inherited conflict must fail.
        const auto beforeInvalid=C::ToJson(staged);
        assert(!Native::SaveLoadoutReferenceDraft(staged,"first","set1",look.id,newDirect,error));
        assert(C::ToJson(staged)==beforeInvalid);
        auto effective=C::EffectiveRefs(staged,staged.loadouts.front());
        effective["sigils"]="identical";
        assert(Native::SaveLoadoutReferenceDraft(staged,"first","",look.id,effective,error));
        assert(staged.loadouts.front().equipment_set_id.empty());
        assert(staged.loadouts.front().refs.at("sigils")=="identical");
        assert(staged.loadouts[1].equipment_set_id=="set1");
        auto beforeDraftCommit=repo.Read();
        auto afterDraftCommit=beforeDraftCommit;afterDraftCommit.loadouts=staged;
        assert(repo.Commit(beforeDraftCommit,afterDraftCommit)==7);
        assert(C::ToJson(repo.Read().loadouts)==C::ToJson(staged));
        Json roundTrip;assert(C::Resolve(restored.loadouts,restored.loadouts.loadouts.front(),roundTrip,error));
        const auto& ids=roundTrip.at("gear").at("Helm").at("infusion_ids");
        assert(ids.size()==3);
        uint32_t infusionId=0;
        assert(C::ReadItemId(ids[0],infusionId)&&infusionId==11);
        assert(C::ReadItemId(ids[1],infusionId)&&infusionId==22);
        assert(C::ReadItemId(ids[2],infusionId)&&infusionId==33);
        assert(C::ReadItemId(Json(uint32_t(0)),infusionId)&&infusionId==0);
        assert(!C::ReadItemId(Json(-1),infusionId));
        assert(!C::ReadItemId(Json(uint64_t(4294967296ULL)),infusionId));
        assert(!C::ReadItemId(Json(1.5),infusionId));
        C::Document portable;
        assert(C::FromJson(C::ToJson(restored.loadouts),portable,error));
        assert(portable.equipment_sets.front().refs==refs);
        assert(portable.loadouts.front().equipment_set_id=="set1");
        Json packet;assert(C::ExportPortableLoadout(portable,"first",packet,error));
        assert(packet.at("equipment_set_name")=="Equipment");
        auto imported=portable;assert(C::ImportPortableLoadout(imported,packet,"imported",error));
        assert(!imported.loadouts.back().equipment_set_id.empty());
        assert(imported.loadouts.back().refs.size()==2);
        assert(Native::ConsolidateExact(portable,"sigils",refs.at("sigils"),"identical",error));
        assert(portable.equipment_sets.front().refs.at("sigils")=="identical");
        C::Document merged;size_t added=0,skipped=0;
        size_t setsAdded=0,setsSkipped=0;
        assert(C::Merge(state.loadouts,portable,false,merged,added,skipped,error,&setsAdded,&setsSkipped));
        assert(setsAdded==1&&setsSkipped==0);
        assert(merged.equipment_sets.size()==1);
        assert(C::Validate(merged,error));
    }
    assert(Native::ExactFingerprint(sigil.data)!=Native::ExactFingerprint(swapped.data));
    return 0;
}
