#include "../src/NativeInventoryStore.h"
#include "../src/NativeProgressCore.h"
#include "../src/CompletionCache.h"
#include "../src/RefreshProgress.h"
#include <cassert>
#include <filesystem>
#include <chrono>
using namespace AlterEgo;
using namespace AlterEgo::NativeInventory;
int case_native_inventory(int,char**){
    RefreshProgress::Counts scopeProgress;
    scopeProgress.Add("Ready",true);scopeProgress.Add("Refreshing",true);scopeProgress.Add("Missing permissions",true);scopeProgress.Add("Not retrieved",false);
    assert(scopeProgress.total==4&&scopeProgress.ready==1&&scopeProgress.working==1&&scopeProgress.failed==1&&scopeProgress.missing==1);
    assert(RefreshProgress::Character(2,5,"Test","Equipment tabs").find("2/5")!=std::string::npos);
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
    auto equipped=ParseEquipment(Json::parse(R"({"equipment":[{"id":20,"location":"Equipped","slot":"WeaponA1","tabs":[1,2,3],"upgrades":[30],"infusions":[31]},{"id":20,"location":"Armory","tabs":[4]},{"id":21,"location":"EquippedFromLegendaryArmory","tabs":[1]},{"id":22,"slot":"Pick","charges":50}]})"),"account-a","B");
    assert(equipped.size()==3&&equipped[0].count==1&&equipped[1].count==1&&equipped[2].charges==50);
    Record(root,{"account-a","Equipment","B","Ready",103,103},&equipped);
    v=Read(root);bool socket=false;for(const auto& row:v.rows)if(row.slot.id==20&&!row.slot.upgrades.empty())socket=row.slot.upgrades[0]==30&&row.slot.infusions[0]==31;
    assert(socket);
    auto armory=ParseArmory(Json::parse(R"([{"id":21,"count":2}])"),"account-a");
    assert(armory.size()==1&&armory[0].binding==Binding::Account&&armory[0].count==2);
    const auto progress=NativeProgress::ParseAchievements(Json::parse(R"([{"id":1,"current":2,"max":3,"done":false,"bits":[0,70]},{"id":2,"done":true,"repeated":4}])"),500);
    assert(progress.entries.size()==2&&progress.entries.at(1).completed_bits.count(70)&&progress.entries.at(2).repeated==4);
    rejected=false;try{NativeProgress::ParseAchievements(Json::parse(R"([{"id":1,"done":false},{"id":1,"done":true}])"),500);}catch(...){rejected=true;}assert(rejected);
    const auto special=NativeProgress::ParseVault(Json::parse(R"({"objectives":[{"id":9,"title":"Objective","track":"PvE","acclaim":100,"progress_current":1,"progress_complete":2,"claimed":false}]})"),true);
    assert(special.objectives.size()==1&&!special.objectives[0].isDone());
    rejected=false;try{NativeProgress::ParseVault(Json::parse(R"({"objectives":[]})"),false);}catch(...){rejected=true;}assert(rejected);
    auto wallet=ParseWallet(Json::parse(R"([{"id":1,"value":123456},{"id":2,"value":0}])"));
    RecordWallet(root,{"account-a","Wallet","","Ready",200,200},&wallet);
    RecordWallet(root,{"account-a","Wallet","","Failed",201,0});
    StoreCurrencies(root,Json::parse(R"([{"id":1,"name":"Coin","description":"","order":1}])"));
    auto g=ParseGuild(Json::parse(R"([{"upgrade_id":1,"size":2,"coins":500,"inventory":[{"id":10,"count":4},null]}])"),"guild-a");
    GuildRow gr;gr.id="guild-a";gr.name="Guild";gr.status="Ready";gr.attempted=200;gr.success=200;
    RecordGuild(root,gr,&g);StoreGuildAccess(root,"account-a",{"guild-a"},200);StoreGuildAccess(root,"account-b",{"guild-a"},200);
    v=Read(root);assert(v.guilds.size()==1&&v.guilds.at("guild-a").rows.size()==1&&v.guilds.at("guild-a").coins==500);
    assert(v.wallet.size()==2&&v.wallet.front().value>=0);
    assert(!UsableLoose(g.slots.front(),"account-a","B",{},true));
    assert(UsableLoose(rows.front(),"account-a","B",{},true)&&!UsableLoose(rows.front(),"account-a","B",{},false));
    for(const auto* scope:{"Wallet","Bags","Shared inventory","Bank","Equipment","Delivery box","Materials","Guild bank","Guild access"}){
        assert(FreshSeconds(scope)==1800);
        assert(!Due(scope,100,0,1899)&&Due(scope,100,0,1900));
        assert(!Due(scope,100,100,1899)&&Due(scope,100,100,1900));
        assert(Due(scope,0,0,100));
    }
    for(const auto* scope:{"Legendary Armory","Roster"}){
        assert(FreshSeconds(scope)==3600);
        assert(!Due(scope,100,100,3699)&&Due(scope,100,100,3700));
        assert(!Due(scope,100,0,1899)&&Due(scope,100,0,1900));
    }
    assert(Match("Superior Sigil of Force",10,"sigli force"));
    using AlterEgo::Cache::CompletionDataset;
    const auto vaultPeriod=[](int64_t stamp,int id){return Json{{"fetched_at",stamp},{"objectives",Json::array({Json{{"id",id},{"title","Objective"},{"claimed",false}}})}};};
    assert(Cache::WriteVaultPeriods(root,"account-a",Json{{"account","account-a"},{"daily",vaultPeriod(100,1)},{"weekly",vaultPeriod(100,2)}}));
    assert(Cache::WriteVaultPeriods(root,"account-a",Json{{"account","account-a"},{"daily",vaultPeriod(200,3)}}));
    auto snap=Cache::ReadCompletion(root,CompletionDataset::Vault,"account-a");
    assert(snap&&snap->at("daily").at("fetched_at")==200&&snap->at("weekly").at("fetched_at")==100&&!snap->contains("special"));
    assert(Cache::WriteVaultPeriods(root,"account-a",Json{{"account","account-a"},{"daily",vaultPeriod(150,4)},{"special",vaultPeriod(210,5)}}));
    snap=Cache::ReadCompletion(root,CompletionDataset::Vault,"account-a");
    assert(snap&&snap->at("daily").at("objectives").at(0).at("id")==3&&snap->at("special").at("fetched_at")==210);
    assert(!Cache::ReadCompletion(root,CompletionDataset::Vault,"account-b"));
    assert(!Cache::WriteVaultPeriods(root,"account-a",Json{{"account","account-a"},{"weekly",vaultPeriod(0,6)}}));
    assert(Cache::ReadCompletion(root,CompletionDataset::Vault,"account-a")->at("weekly").at("fetched_at")==100);
    std::filesystem::remove_all(root);return 0;
}
