#include "../src/NativeLibraryPortable.h"
#include <cassert>
using namespace AlterEgo;
using namespace AlterEgo::NativeLibrary;
int case_native_portable(int,char**) {
    std::string error;
    Snapshot s;
    Json raw={{"id","a"},{"name","A"},{"profession","Engineer"}};
    assert(PutLoadout(s.loadouts,raw,error));
    s.loadouts.components["armor"].push_back({"unused","Unused",Json::object()});
    DecodedWardrobeLink w{};w.helm_skin=123;
    Fashion::Look look;look.id="f";look.name="Look";look.wardrobe_link=ChatLink::EncodeWardrobe(w);
    s.fashion.push_back(look);s.loadouts.loadouts[0].fashion_id="f";
    auto packet=ExportPortableLibrary(s,true,false);
    auto oldGraph=packet;
    for(auto& part:oldGraph["loadouts"]["components"]["armor"])
        if(part["id"]=="unused")part["data"]["gear"]["HelmAquatic"]={{"stat_id",1130}};
    PortableLibrary rejected;
    assert(!ParsePortableLibrary(oldGraph,rejected,error));
    PortableLibrary in;assert(ParsePortableLibrary(packet,in,error));
    assert(in.loadouts&&!in.fashion&&in.data.fashion.size()==1);
    assert(Composable::Find(in.data.loadouts,"armor","unused"));
    Snapshot current;Fashion::Look different=look;different.name="Local";
    current.fashion.push_back(different);
    Snapshot next;ImportCounts counts;
    assert(MergePortableLibrary(current,in,true,false,false,next,counts,error));
    assert(next.fashion.size()==2&&next.loadouts.loadouts[0].fashion_id!="f");
    assert(Composable::Find(next.loadouts,"armor","unused"));
    assert(counts.loadoutsAdded==1);
    packet["fashion"]=Json::array();assert(!ParsePortableLibrary(packet,in,error));
    Json legacy={{"builds",Json::array({raw})}};
    assert(ParsePortableLibrary(legacy,in,error));
    assert(in.legacy&&in.loadouts);
    assert(MergePortableLibrary(next,in,true,false,false,current,counts,error));
    assert(counts.loadoutsSkipped==1);
    // Native graph merge: the same ID and payload do not prove that two
    // independent named records or incompatible Skills are interchangeable.
    Composable::Document local,remote,merged;
    Json left={{"id","local"},{"name","Local"},{"profession","Engineer"},
        {"skills",{{"utilities",{1,2,3}}}}};
    Json right=left;right["id"]="remote";right["name"]="Remote";right["profession"]="Necromancer";
    assert(PutLoadout(local,left,error));
    assert(PutLoadout(remote,right,error));
    const auto skillId=local.loadouts[0].refs.at("skills");
    auto remoteSkill=remote.loadouts[0].refs.at("skills");
    Composable::Find(remote,"skills",remoteSkill)->id=skillId;
    remote.loadouts[0].refs["skills"]=skillId;
    Composable::Find(remote,"skills",skillId)->name=Composable::Find(local,"skills",skillId)->name;
    // Reserve a suffix that already belongs to another imported component.
    remote.components["skills"].push_back({skillId+":import:1","Independent",Json::object()});
    const auto armorId=local.loadouts[0].refs.at("armor");
    auto remoteArmor=remote.loadouts[0].refs.at("armor");
    Composable::Find(remote,"armor",remoteArmor)->id=armorId;
    remote.loadouts[0].refs["armor"]=armorId;
    Composable::Find(remote,"armor",armorId)->name="Different intentional name";
    size_t added=0,skipped=0;
    assert(Composable::Merge(local,remote,false,merged,added,skipped,error));
    assert(added==1&&skipped==0);
    assert(merged.loadouts[0].refs.at("skills")==skillId);
    assert(merged.loadouts[1].refs.at("skills")==skillId+":import:2");
    assert(Composable::Find(merged,"skills",skillId+":import:1")->name=="Independent");
    assert(merged.loadouts[1].refs.at("armor")!=armorId);
    assert(Composable::Find(merged,"armor",merged.loadouts[1].refs.at("armor"))->name=="Different intentional name");
    // With verified same-profession context and exact identity, reuse is safe.
    auto compatible=remote;
    auto baseId=compatible.loadouts[0].refs.at("base_build");
    Composable::Find(compatible,"base_build",baseId)->data["profession"]="Engineer";
    assert(Composable::Merge(local,compatible,false,merged,added,skipped,error));
    assert(merged.loadouts[1].refs.at("skills")==skillId);
    using AlterEgo::InfusionMode;
    for(auto mode:{InfusionMode::Slots,InfusionMode::Totals,InfusionMode::Conflict}){
        Snapshot original;
        Json build={{"id","mode"},{"name","Mode"},{"profession","Engineer"},
            {"gear",{{"HelmAquatic",{{"item_id",45},{"rune_id",12}}},
                     {"WeaponAquaticA",{{"weapon_type","Harpoon"},{"item_id",50}}}}}};
        if(mode!=InfusionMode::Totals){
            build["gear"]["Helm"]={{"infusion_attributes",Json::array({"Power"})},{"infusion_snapshot",true}};
            build["gear"]["WeaponAquaticA"]["infusion_ids"]=Json::array({4294967295u});
        }
        if(mode!=InfusionMode::Slots){
            build["infusion_summary_present"]=true;build["infusion_ar_total"]=150;
            build["infusion_stat_totals"]={{"Power",90}};
        }
        assert(PutLoadout(original.loadouts,build,error));
        assert(Composable::InfusionPayloadMode(build)==mode);
        auto& l=original.loadouts.loadouts.front();
        std::map<std::string,std::string> refs;
        for(const auto* kind:{"aquatic","infusions"}){refs[kind]=l.refs.at(kind);l.refs.erase(kind);}
        original.loadouts.equipment_sets.push_back({"set","Mode Set",refs});l.equipment_set_id="set";
        assert(Composable::InfusionReferenceMode(original.loadouts,Composable::EffectiveRefs(original.loadouts,l))==mode);
        auto archive=ExportPortableLibrary(original,true,false);
        PortableLibrary parsed;assert(ParsePortableLibrary(archive,parsed,error));
        Json resolved;
        assert(Composable::Resolve(parsed.data.loadouts,parsed.data.loadouts.loadouts.front(),resolved,error)&&resolved==build);
        Json packet;assert(Composable::ExportPortableLoadout(original.loadouts,"mode",packet,error));
        Composable::Document imported;
        assert(Composable::ImportPortableLoadout(imported,packet,"new",error));
        assert(Composable::Resolve(imported,imported.loadouts.front(),resolved,error));
        auto expected=build;expected["id"]="new";assert(resolved==expected);
        assert(Composable::InfusionReferenceMode(imported,Composable::EffectiveRefs(imported,imported.loadouts.front()))==mode);
        Json old={{"builds",Json::array({build})}};
        assert(ParsePortableLibrary(old,parsed,error)&&parsed.legacy);
        assert(Composable::Resolve(parsed.data.loadouts,parsed.data.loadouts.loadouts.front(),resolved,error)&&resolved==build);
    }
    return 0;
}
