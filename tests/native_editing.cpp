#include "../src/NativeLibraryEditing.h"
#include "../src/ComponentBrowseCore.h"
#include "../src/ComposableSemantics.h"
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
    // Functional utility equivalence does not authorize exact native reuse.
    // Exercise both explicit editing decisions and the unified aggregate Save.
    {
        Composable::Document graph;std::string error;
        auto original=a;
        original["skills"]["heal"]=10;original["skills"]["elite"]=11;
        original["aquatic_skills"]={{"heal",20},{"elite",21},{"utilities",{4,5,6}}};
        auto peer=original;peer["id"]="b";peer["name"]="B";
        auto reordered=original;reordered["id"]="c";reordered["name"]="C";
        reordered["skills"]["utilities"]={3,1,2};
        reordered["aquatic_skills"]["utilities"]={6,4,5};
        assert(Composable::Semantics::SameUtilities(original["skills"],reordered["skills"]));
        assert(Composable::Semantics::SameUtilities(original["aquatic_skills"],reordered["aquatic_skills"]));
        assert(PutLoadout(graph,original,error));
        assert(PutLoadout(graph,peer,error));
        assert(PutLoadout(graph,reordered,error));
        const auto ref=[&](const char* id){return Composable::FindLoadout(graph,id)->refs.at("skills");};
        const auto first=ref("a"),second=ref("c");
        assert(first==ref("b")&&first!=second);
        const auto firstPayload=Composable::Find(graph,"skills",first)->data;
        const auto secondPayload=Composable::Find(graph,"skills",second)->data;
        assert(!ExactContent(firstPayload,secondPayload));
        assert(EditIndex(graph).Matches("skills",firstPayload,"Engineer").size()==1);
        const auto matches=EditIndex(graph).Matches("skills",secondPayload,"Engineer");
        assert(matches.size()==1&&matches.front().id==second);
        const auto before=Composable::ToJson(graph);
        assert(!ConsolidateExact(graph,"skills",first,second,error));
        assert(!ReuseEditedForLoadout(graph,"a","skills",first,second,firstPayload,error));
        assert(Composable::ToJson(graph)==before);
        assert(ReuseEditedForLoadout(graph,"a","skills",first,second,secondPayload,error));
        assert(ref("a")==second&&ref("b")==first&&ref("c")==second);
        Json resolved;
        assert(Composable::Resolve(graph,*Composable::FindLoadout(graph,"a"),resolved,error));
        assert(resolved["skills"]==reordered["skills"]&&resolved["aquatic_skills"]==reordered["aquatic_skills"]);
        assert(PutUnifiedLoadout(graph,original,{},error));
        assert(ref("a")==first&&ref("b")==first&&ref("c")==second);
        assert(Composable::Find(graph,"skills",first)->data==firstPayload);
        assert(Composable::Find(graph,"skills",second)->data==secondPayload);
        assert(Composable::Resolve(graph,*Composable::FindLoadout(graph,"a"),resolved,error));
        assert(resolved["skills"]==original["skills"]&&resolved["aquatic_skills"]==original["aquatic_skills"]);
    }
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
    // Inherited reference replacements retain exact selected IDs, never
    // mutate component payloads, and fork a shared set only once.
    {
        Composable::Document graph;std::string error;
        assert(PutLoadout(graph,a,error));assert(PutLoadout(graph,b,error));
        assert(SaveSetFromLoadout(graph,"a","set","Set",error,{"armor","sigils"}));
        assert(ApplyEquipmentSet(graph,"b","set",error));
        const auto original=*Composable::FindEquipmentSet(graph,"set");
        auto newArmor=*Composable::Find(graph,"armor",original.refs.at("armor"));
        newArmor.id="new-armor";newArmor.name="Other armor";
        newArmor.data["gear"]["Helm"]["stat_id"]=33;
        graph.components["armor"].push_back(newArmor);
        auto newSigils=*Composable::Find(graph,"sigils",original.refs.at("sigils"));
        newSigils.id="new-sigils";newSigils.name="Other sigils";
        graph.components["sigils"].push_back(newSigils); // even exact-equal payload must retain chosen ID
        const auto direct=Composable::FindLoadout(graph,"a")->refs;
        const auto before=Composable::ToJson(graph);
        assert(!SaveParentReferenceDraft(graph,"a","set","",direct,{{"armor","missing"}},{},error));
        assert(Composable::ToJson(graph)==before);
        auto privateGraph=graph;
        assert(SaveParentReferenceDraft(privateGraph,"a","set","",direct,{{"armor","new-armor"},{"sigils","new-sigils"}},{},error));
        assert(privateGraph.equipment_sets.size()==graph.equipment_sets.size()+1);
        const auto* editedOwner=Composable::FindLoadout(privateGraph,"a");
        assert(editedOwner->equipment_set_id!="set");
        assert(editedOwner->refs==direct);
        assert(Composable::EffectiveRefs(privateGraph,*editedOwner).at("armor")=="new-armor");
        assert(Composable::EffectiveRefs(privateGraph,*editedOwner).at("sigils")=="new-sigils");
        assert(Composable::FindEquipmentSet(privateGraph,"set")->refs==original.refs);
        assert(Composable::ToJson(graph)==before);
        auto mixed=graph;
        assert(SaveParentReferenceDraft(mixed,"a","set","",direct,{{"armor","new-armor"},{"sigils","new-sigils"}},{"sigils"},error));
        assert(Composable::FindEquipmentSet(mixed,"set")->refs.at("sigils")=="new-sigils");
        assert(Composable::FindEquipmentSet(mixed,"set")->refs.at("armor")==original.refs.at("armor"));
        assert(Composable::Find(mixed,"armor","new-armor")->data==newArmor.data);
        auto shared=graph;
        assert(SaveParentReferenceDraft(shared,"a","set","",direct,{{"armor","new-armor"}},{"armor"},error));
        assert(shared.equipment_sets.size()==graph.equipment_sets.size());
        assert(Composable::EffectiveRefs(shared,*Composable::FindLoadout(shared,"b")).at("armor")=="new-armor");
        auto single=graph;
        assert(DetachEquipmentSet(single,"b",error));
        assert(SaveParentReferenceDraft(single,"a","set","",direct,{{"armor","new-armor"}},{},error));
        assert(single.equipment_sets.size()==graph.equipment_sets.size());
        assert(Composable::FindEquipmentSet(single,"set")->refs.at("armor")=="new-armor");
    }
    // Name provenance is explicit, not inferred from suffixes. Only pending
    // automatic names are repaired, without touching payload or exact IDs.
    {
        using namespace Composable;
        Document graph;
        const Json relicData={{"relic_id",42}};
        auto autoPart=GeneratedComponent(graph,"relic","auto",relicData);
        assert(autoPart.nameOrigin==NameOrigin::AutomaticPending);
        graph.components["relic"].push_back(autoPart);
        graph.components["relic"].push_back({"legacy","Relic (metadata pending)",relicData});
        graph.components["relic"].push_back({"custom","Keep me",relicData,NameOrigin::Manual});
        assert(!RepairPendingNames(graph,{}));
        const ComponentNameLookup lookup=[](const std::string& kind,uint32_t id){
            return kind=="item"&&id==42?std::string("Verified relic"):std::string{};
        };
        assert(RepairPendingNames(graph,lookup));
        assert(Find(graph,"relic","auto")->name=="Verified relic");
        assert(Find(graph,"relic","auto")->nameOrigin==NameOrigin::Automatic);
        assert(Find(graph,"relic","auto")->data==relicData);
        assert(Find(graph,"relic","legacy")->name=="Relic (metadata pending)");
        assert(Find(graph,"relic","custom")->name=="Keep me");
        assert(!RepairPendingNames(graph,[](const std::string&,uint32_t){return std::string("Changed upstream name");}));
        std::string kind,error;Component imported;
        auto packet=ExportComponent("relic",*Find(graph,"relic","auto"));
        assert(ImportComponent(packet,kind,imported,error));
        assert(imported.nameOrigin==NameOrigin::Automatic);
        packet["name_origin"]="invalid";
        assert(!ImportComponent(packet,kind,imported,error));
        packet.erase("name_origin");
        assert(ImportComponent(packet,kind,imported,error)&&imported.nameOrigin==NameOrigin::Unknown);
        Document restored;
        assert(FromJson(ToJson(graph),restored,error));
        assert(ToJson(graph)==ToJson(restored));
        auto edited=graph;
        Find(edited,"relic","auto")->data["relic_id"]=43;
        RefreshEditedAutomaticNames(graph,edited,lookup);
        assert(Find(edited,"relic","auto")->nameOrigin==NameOrigin::AutomaticPending);
        Find(edited,"relic","custom")->data["relic_id"]=43;
        RefreshEditedAutomaticNames(graph,edited,lookup);
        assert(Find(edited,"relic","custom")->name=="Keep me");
        Document loadout,importedGraph;
        assert(PutLoadout(loadout,a,error));
        const auto armorId=FindLoadout(loadout,"a")->refs.at("armor");
        assert(Find(loadout,"armor",armorId)->nameOrigin==NameOrigin::AutomaticPending);
        Json portable;
        assert(ExportPortableLoadout(loadout,"a",portable,error));
        assert(ImportPortableLoadout(importedGraph,portable,"imported",error));
        const auto importedArmor=FindLoadout(importedGraph,"imported")->refs.at("armor");
        assert(Find(importedGraph,"armor",importedArmor)->nameOrigin==NameOrigin::AutomaticPending);
        portable["components"]["armor"]["name_origin"]=true;
        const auto prior=ToJson(importedGraph);
        assert(!ImportPortableLoadout(importedGraph,portable,"invalid",error));
        assert(ToJson(importedGraph)==prior);
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
