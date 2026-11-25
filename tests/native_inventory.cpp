#include "../src/NativeInventoryStore.h"
#include <cassert>
#include <filesystem>
#include <chrono>
using namespace AlterEgo;
using namespace AlterEgo::NativeInventory;
int case_native_inventory(int,char**){
    const auto root=(std::filesystem::temp_directory_path()/("ae-native-inventory-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    auto rows=ParseArray(Json::parse(R"([null,{"id":10,"count":2},{"id":11,"count":1,"binding":"Account"},{"id":12,"count":1,"binding":"Character","bound_to":"A"}])"),"account-a","Bank");
    assert(rows.size()==3&&rows[0].position=="1"&&rows[0].binding==Binding::Unbound);
    assert(Accessibility(rows[0],"account-b","B",false)==Access::Unknown);
    assert(Accessibility(rows[0],"account-b","B",true)==Access::Transferable);
    assert(Accessibility(rows[1],"account-b","B",true)==Access::Restricted);
    assert(Accessibility(rows[2],"account-a","B",true)==Access::Restricted);
    assert(Accessibility(rows[2],"account-a","A",true)==Access::Available);
    assert(Match("Superior Sigil of Force",10,"sigil force")&&Match("",10,"10"));
    Record(root,{"account-a","Bank","","Ready",100,100},&rows);
    auto other=ParseArray(Json::parse(R"([{"id":10,"count":3}])"),"account-b","Bank");
    Record(root,{"account-b","Bank","","Ready",100,100},&other);
    Record(root,{"account-a","Bank","","HTTP 503",101,0});
    auto v=Read(root);assert(v.rows.size()==4&&v.states.size()==2&&v.states[0].success==100);
    bool rejected=false;try{Record(root,{"account-a","Bank","","Ready",102,102},&other);}catch(...){rejected=true;}
    assert(rejected&&Read(root).rows.size()==4);
    std::vector<Slot> empty;
    Record(root,{"account-a","Bank","","Ready",103,103},&empty);
    assert(Read(root).rows.size()==1);
    Record(root,{"account-a","Bank","","Ready",102,102},&rows);
    assert(Read(root).rows.size()==1);
    rejected=false;try{ParseArray(Json::parse(R"([{"id":10}])"),"account-a","Bank");}catch(...){rejected=true;}
    assert(rejected);
    auto material=ParseArray(Json::parse(R"([{"id":10,"count":1}])"),"account-a","Materials",{}, {},false);
    assert(material[0].binding==Binding::Unknown);
    StoreRoster(root,"account-a",{"A","B"},100);StoreRoster(root,"account-b",{"C"},100);
    auto bags=ParseArray(Json::parse(R"([{"id":10,"count":2}])"),"account-a","Bags","A");
    Record(root,{"account-a","Bags","A","Ready",101,101},&bags);
    StoreRoster(root,"account-a",{"B"},102);
    v=Read(root);assert(v.rows.size()==1&&v.rosters.at("account-a").count("B")&&v.rosters.at("account-b").count("C"));
    std::filesystem::remove_all(root);return 0;
}
