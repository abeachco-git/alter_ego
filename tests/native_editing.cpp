#include "../src/NativeLibraryEditing.h"
#include "../src/ComponentBrowseCore.h"
#include <cassert>
using namespace AlterEgo;
using namespace AlterEgo::NativeLibrary;
using Json=Composable::Json;

int case_native_editing(int,char**) {
    Composable::Document doc;
    std::string err;
    Json a={{"id","a"},{"name","A"},{"profession","Engineer"},
        {"skills",{{"utilities",{1,2,3}}}},
        {"gear",{{"WeaponA1",{{"id",100},{"sigil_id",1},{"sigil2_id",2}}},
                 {"Helm",{{"stat_id",1},{"unknown",42}}}}}};
    Json b=a;b["id"]="b";b["name"]="B";
    assert(PutLoadout(doc,a,err));
    assert(PutLoadout(doc,b,err));
    const auto armor=doc.loadouts[0].refs.at("armor");
    const auto skills=doc.loadouts[0].refs.at("skills");
    assert(doc.loadouts[1].refs.at("armor")==armor);
    assert(doc.loadouts[1].refs.at("skills")==skills);
    auto edited=a;edited["gear"]["Helm"]["stat_id"]=2;
    assert(PutLoadout(doc,edited,err));
    assert(doc.loadouts[0].refs.at("armor")!=armor);
    assert(doc.loadouts[1].refs.at("armor")==armor);
    assert(doc.loadouts[0].refs.at("skills")==skills);
    edited["gear"]["Helm"]["stat_id"]=3;
    const auto privateId=doc.loadouts[0].refs.at("armor");
    assert(PutLoadout(doc,edited,err));
    assert(doc.loadouts[0].refs.at("armor")==privateId);
    assert(PutLoadout(doc,a,err));
    assert(doc.loadouts[0].refs.at("armor")==armor);
    assert(Composable::Find(doc,"armor",privateId)); // no automatic cleanup
    auto swapped=a;swapped["gear"]["WeaponA1"]["sigil_id"]=2;
    swapped["gear"]["WeaponA1"]["sigil2_id"]=1;
    assert(PutLoadout(doc,swapped,err));
    assert(doc.loadouts[0].refs.at("sigils")!=doc.loadouts[1].refs.at("sigils"));
    Json stored={{"gear",{{"Helm",{{"stat_id",1},{"extension",42}}}}}};
    Json baseline={{"gear",{{"Helm",{{"stat_id",1},{"item_id",0}}}}}};
    Json draft=baseline;draft["gear"]["Helm"]["stat_id"]=2;
    auto patched=PatchDraft(stored,baseline,draft);
    assert(!patched["gear"]["Helm"].contains("item_id"));
    assert(patched["gear"]["Helm"]["extension"]==42);
    assert(patched["gear"]["Helm"]["stat_id"]==2);
    auto duplicate=*Composable::Find(doc,"armor",armor);
    duplicate.id="armor:duplicate";duplicate.name="Explicit duplicate";
    doc.components["armor"].push_back(duplicate);
    doc.loadouts[1].refs["armor"]=duplicate.id;
    assert(!ConsolidateExact(doc,"armor",privateId,armor,err));
    assert(ConsolidateExact(doc,"armor",duplicate.id,armor,err));
    assert(!Composable::Find(doc,"armor",duplicate.id));
    assert(doc.loadouts[1].refs.at("armor")==armor);
    auto changedArmor=Composable::Find(doc,"armor",privateId)->data;
    assert(ReuseEditedForLoadout(doc,"a","armor",armor,privateId,changedArmor,err));
    assert(doc.loadouts[0].refs.at("armor")==privateId);
    assert(doc.loadouts[1].refs.at("armor")==armor);
    assert(!ReuseEditedForLoadout(doc,"a","armor",armor,privateId,changedArmor,err));
    assert(!ReuseEditedForLoadout(doc,"a","armor",privateId,armor,changedArmor,err));
    const auto baseId=doc.loadouts[0].refs.at("base_build");
    auto wrongBase=Composable::Find(doc,"base_build",baseId)->data;
    wrongBase["profession"]="Necromancer";
    assert(!Composable::CopyEditedForLoadout(doc,"a","base_build","new-base","New Base",wrongBase,err,baseId));
    assert(doc.loadouts[0].refs.at("base_build")==baseId);
    assert(VerifiedSkillsProfession(doc,skills)=="Engineer");
    // Aggregate modification must not bypass private/shared Base Build guards.
    auto wrongLoadout=a;wrongLoadout["profession"]="Necromancer";
    const auto before=doc.loadouts[0].refs;
    assert(!PutLoadout(doc,wrongLoadout,err));
    assert(doc.loadouts[0].refs==before);
    assert(Composable::ProfessionOf(doc,doc.loadouts[0])=="Engineer");
    assert(VerifiedSkillsProfession(doc,skills)=="Engineer");
    wrongLoadout["id"]="new-profession";
    assert(PutLoadout(doc,wrongLoadout,err));
    assert(doc.loadouts.back().refs.at("skills")!=skills);
    assert(VerifiedSkillsProfession(doc,"missing").empty());
    // A unified private edit forks only the linked shared set and changed
    // categories; subsequent edits in that same fork cannot mutate the old set.
    {
        Composable::Document graph;std::string error;
        assert(PutLoadout(graph,a,error));assert(PutLoadout(graph,b,error));
        assert(SaveSetFromLoadout(graph,"a","set","Set",error,{"armor","sigils"}));
        assert(ApplyEquipmentSet(graph,"b","set",error));
        const auto oldSet=*Composable::FindEquipmentSet(graph,"set");
        Json edit=a;edit["gear"]["Helm"]["stat_id"]=9;edit["gear"]["WeaponA1"]["sigil_id"]=7;edit["name"]="Edited";
        const auto changes=UnifiedChanges(graph,*Composable::FindLoadout(graph,"a"),a,edit);
        assert(changes.size()==2);
        for(const auto& change:changes)assert(change.setUsers==2&&change.loadouts==2);
        assert(PutUnifiedLoadout(graph,edit,{},error));
        assert(Composable::FindLoadout(graph,"a")->equipment_set_id!="set");
        assert(Composable::FindLoadout(graph,"b")->equipment_set_id=="set");
        assert(Composable::FindEquipmentSet(graph,"set")->refs==oldSet.refs);
        Json resolved;assert(Composable::Resolve(graph,*Composable::FindLoadout(graph,"b"),resolved,error));
        assert(resolved==b);
        assert(Composable::Resolve(graph,*Composable::FindLoadout(graph,"a"),resolved,error));assert(resolved==edit);
        // Reverting a private change reuses an exact existing identity.
        edit["gear"]["Helm"]["stat_id"]=1;
        assert(PutUnifiedLoadout(graph,edit,{},error));
        assert(Composable::EffectiveRefs(graph,*Composable::FindLoadout(graph,"a")).at("armor")==oldSet.refs.at("armor"));
        edit["gear"]["Helm"]["stat_id"]=8;
        assert(PutUnifiedLoadout(graph,edit,{"armor"},error));
        assert(Composable::Resolve(graph,*Composable::FindLoadout(graph,"b"),resolved,error));
        assert(resolved["gear"]["Helm"]["stat_id"]==8&&resolved["gear"]["Helm"]["unknown"]==42);
        const auto saved=Composable::ToJson(graph);
        edit["profession"]="Necromancer";
        assert(!PutUnifiedLoadout(graph,edit,{},error));assert(Composable::ToJson(graph)==saved);
    }
    // Standalone creation reuses exact components and never modifies a Loadout.
    {
        const auto before=Composable::ToJson(doc);
        assert(!CreateEquipmentSet(doc,"manual","",{{"armor",armor}},err));
        assert(!CreateEquipmentSet(doc,"manual","Set",{},err));
        assert(!CreateEquipmentSet(doc,"manual","Set",{{"skills",skills}},err));
        assert(!CreateEquipmentSet(doc,"manual","Set",{{"armor","missing"}},err));
        assert(Composable::ToJson(doc)==before);
        assert(CreateEquipmentSet(doc,"manual","Set",{{"armor",armor}},err));
        assert(Composable::FindEquipmentSet(doc,"manual")->refs.at("armor")==armor);
        assert(Composable::ToJson(doc)["loadouts"]==before["loadouts"]);
        assert(!CreateEquipmentSet(doc,"manual","Duplicate",{{"armor",armor}},err));
    }
    // The grouped browser uses empty facets as All and stable sort keys
    // independent of category ordering or component content.
    {
        namespace B=AlterEgo::ComponentBrowse;
        assert(B::MatchesKind({},"base_build")&&B::MatchesKind({},"relic"));
        assert(B::MatchesKind({"armor","runes"},"runes"));
        assert(!B::MatchesKind({"armor","runes"},"skills"));
        const auto alpha=B::Key("alpha","a",5),beta=B::Key("Beta","b",1),
            same=B::Key("ALPHA","z",5);
        assert(B::Less(alpha,beta,false,true)&&B::Less(beta,alpha,false,false));
        assert(B::Less(beta,alpha,true,true)&&B::Less(alpha,beta,true,false));
        assert(B::Less(alpha,same,true,true)&&B::Less(alpha,same,true,false));
        assert(!B::Less(alpha,alpha,false,true)&&!B::Less(alpha,alpha,true,false));
    }
    return 0;
}
