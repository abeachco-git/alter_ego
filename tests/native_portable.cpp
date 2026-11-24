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
    return 0;
}
