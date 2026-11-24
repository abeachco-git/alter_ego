#include "../src/NativeLibraryEditing.h"
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
    return 0;
}
