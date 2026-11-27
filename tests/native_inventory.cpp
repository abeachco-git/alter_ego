#include "../src/CharacterIdentityStore.h"
#include "../src/CharacterIdentityCache.h"
#include "../src/NativeInventoryStore.h"
#include "../src/InventoryGroupCore.h"
#include "../src/CharacterOrderCore.h"
#include "../src/NativeProgressCore.h"
#include "../src/CompletionCache.h"
#include "../src/RefreshProgress.h"
#include "../src/RefreshEngine.h"
#include "../src/CommercePriceCore.h"
#include <cassert>
#include <filesystem>
#include <chrono>
using namespace AlterEgo;
using namespace AlterEgo::NativeInventory;
int case_native_inventory(int,char**){
    // Durable local character identity: rename, same-name reuse, swaps,
    // account isolation, complete observation, v1 migration and rollback.
    {
        namespace CI=AlterEgo::CharacterIdentity;
        const auto root=std::filesystem::temp_directory_path()/("ae-identity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        const auto path=(root/"accounts.db").string();
        const CI::Evidence a{"2015-02-03T04:05:06Z","Necromancer","Asura"},
            b{"2020-01-02T03:04:05Z","Engineer","Human"},
            c{"2026-01-02T03:04:05Z","Necromancer","Asura"};
        CI::Id alice=0,bob=0;
        {
            Database::Connection db(path,Database::Connection::OpenMode::Create);
            Database::Migrate(db,Storage::kAccountRegistryApplicationId,1,[](Database::Connection& conn,int){
                conn.Exec("CREATE TABLE accounts(id TEXT PRIMARY KEY,name TEXT NOT NULL,label TEXT NOT NULL,permissions TEXT NOT NULL) STRICT");
                conn.Exec("INSERT INTO accounts VALUES('A','Example.1234','','[]')");
            });
            CI::InitializeAccountRegistry(db);
            assert(db.Scalar("PRAGMA user_version")==5&&db.Scalar("SELECT count(*) FROM accounts")==1);
            auto initial=CI::Commit(db,"A",0,{"Alice","Bob"},{{"Alice",a},{"Bob",b}});
            assert(initial&&*initial&&initial->changes.size()==2);
            alice=initial->next.Current("Alice")->id;bob=initial->next.Current("Bob")->id;
            assert(alice!=bob);
            auto unchanged=CI::Commit(db,"A",1,{"Alice","Bob"},{{"Alice",a},{"Bob",b}});
            assert(unchanged&&*unchanged&&unchanged->changes.empty()&&unchanged->next.revision==1);
            assert(CI::Read(db,"A").revision==1);
            assert(CI::Commit(db,"B",0,{"Alice"},{{"Alice",a}}));
            assert(!CI::Commit(db,"A",0,{"Bob"},{{"Bob",b}})); // obsolete revision
            auto apply=[&](const std::set<std::string>& roster,const std::vector<CI::Observation>& observed,bool confirm=false){
                auto p=CI::Commit(db,"A",CI::Read(db,"A").revision,roster,observed,confirm);
                assert(p&&*p);return *p;
            };
            auto incomplete=apply({"Alice","Bob"},{{"Alice",a}});
            assert(!incomplete.complete&&incomplete.next.revision==1&&incomplete.next.Current("Bob")->id==bob);
            auto malformed=apply({"Alice","Bob"},{{"Alice",a},{"Bob",{b.created,"Unknown",b.race}}});
            assert(!malformed.complete&&malformed.next.Current("Alice")->id==alice&&!malformed.next.Current("Bob"));
            auto collision=apply({"Alice","Bob","Other"},{{"Alice",a},{"Bob",a},{"Other",c}});
            assert(collision.complete&&!collision.next.Current("Alice")&&!collision.next.Current("Bob")&&collision.next.Current("Other"));
            auto partialSwap=apply({"Alice","Bob","Other"},{{"Alice",b}});
            assert(!partialSwap.next.Current("Alice")&&!partialSwap.next.Current("Bob")&&partialSwap.next.Current("Other"));
            auto swap=apply({"Alice","Bob","Other"},{{"Alice",b},{"Bob",a},{"Other",c}});
            assert(swap.FullyResolved()&&swap.next.Current("Bob")->id==alice&&swap.next.Current("Alice")->id==bob);
            assert(swap.next.records.at(alice).names.count("Alice"));
            auto rename=apply({"Alice","Renamed","Other"},{{"Renamed",a}});
            assert(!rename.complete&&rename.next.Current("Renamed")->id==alice&&!rename.next.Current("Bob"));
            const CI::Evidence d{"2026-01-03T03:04:05Z","Necromancer","Asura"};
            auto pending=apply({"Alice","Renamed","Other"},{{"Alice",d},{"Renamed",a},{"Other",c}});
            assert(!pending.next.Current("Alice")&&pending.next.pendingAbsence.count(bob));
            // A repeated/coalesced observation is not independent confirmation.
            auto replay=apply({"Alice","Renamed","Other"},{{"Alice",d},{"Renamed",a},{"Other",c}});
            assert(!replay.next.Current("Alice")&&replay.next.revision==pending.next.revision);
            auto reuse=apply({"Alice","Renamed","Other"},{{"Alice",d},{"Renamed",a},{"Other",c}},true);
            assert(reuse.FullyResolved()&&reuse.next.Current("Alice")->id!=bob&&!reuse.next.records.at(bob).present);
            auto retired=apply({"Alice","Renamed","Other","Old"},{{"Alice",d},{"Renamed",a},{"Other",c},{"Old",b}});
            assert(!retired.next.Current("Old")&&retired.next.Current("Renamed")->id==alice);
            assert(CI::Read(db,"B").revision==1);
            db.Exec("DELETE FROM accounts WHERE id='A'");
        }
        {
            Database::Connection db(path,Database::Connection::OpenMode::Existing);
            CI::InitializeAccountRegistry(db);
            auto s=CI::Read(db,"A");assert(s.initialized&&s.Current("Renamed")->id==alice&&s.blocked.count("Old"));
            assert(!s.records.at(bob).present&&s.records.at(alice).names.size()==3);
            assert(CI::Read(db,"B").Current("Alice"));
            const auto d=s.Current("Alice")->evidence;
            const auto revision=s.revision;
            // Force a real transaction failure after the intermediate name
            // claims were freed; rollback restores all identity rows.
            db.Exec("CREATE TRIGGER reject_identity BEFORE UPDATE ON character_identities WHEN NEW.name='Rejected' BEGIN SELECT RAISE(ABORT,'fixture'); END");
            bool failed=false;try{(void)CI::Commit(db,"A",revision,{"Alice","Rejected","Other"},{{"Alice",d},{"Rejected",a},{"Other",c}});}catch(...){failed=true;}
            assert(failed&&CI::Read(db,"A").revision==revision&&CI::Read(db,"A").Current("Renamed")->id==alice);
            db.Exec("DROP TRIGGER reject_identity");
            auto firstEmpty=CI::Commit(db,"A",revision,{},{});
            assert(firstEmpty&&*firstEmpty&&firstEmpty->next.pendingAbsence.size()==3&&!firstEmpty->next.Current("Renamed"));
            auto empty=CI::Commit(db,"A",firstEmpty->next.revision,{},{},true);
            assert(empty&&empty->changes.size()==3&&CI::Read(db,"A").Current("Renamed")==nullptr);
            auto initialEmpty=CI::Commit(db,"empty",0,{},{});
            assert(initialEmpty&&initialEmpty->next.initialized&&CI::Read(db,"empty").initialized);
            auto sameEmpty=CI::Commit(db,"empty",0,{},{});
            assert(sameEmpty&&sameEmpty->next.revision==0);
        }
        assert(CI::Creation("2015-02-03T04:05:06.000Z")==CI::Creation(a.created));
        assert(CI::Creation("2024-02-29T00:00:00Z")&&!CI::Creation("2023-02-29T00:00:00Z"));
        assert(!CI::Creation("2015-02-03")&&!CI::Creation("2015-02-03T24:00:00Z"));
        std::filesystem::remove_all(root);
    }
    // Address fences and interrupted cache adoption use independent durable revisions.
    {
        namespace CI=AlterEgo::CharacterIdentity;
        const CI::Evidence a{"2015-02-03T04:05:06Z","Necromancer","Asura"},
            b{"2020-01-02T03:04:05Z","Engineer","Human"};
        CI::State initial;initial.account="A";
        auto first=CI::Reconcile(initial,{"Alice","Bob"},{{"Alice",a},{"Bob",b}});
        assert(first.FullyResolved());
        const auto alice=CI::Capture(first.next,"Alice",10),bob=CI::Capture(first.next,"Bob",10);
        assert(alice&&bob&&CI::Matches(first.next,*alice,10)&&!CI::Matches(first.next,*alice,11));
        auto rename=CI::Reconcile(first.next,{"Renamed","Bob"},{{"Renamed",a}});
        assert(rename&&rename.next.Current("Renamed")&&!CI::Matches(rename.next,*alice,10)&&CI::Matches(rename.next,*bob,10));
        auto back=CI::Reconcile(rename.next,{"Alice","Bob"},{{"Alice",a}});
        assert(back&&back.next.Current("Alice")->id==alice->binding.id&&!CI::Matches(back.next,*alice,10)&&CI::Matches(back.next,*bob,10));
        const auto changed=CI::ChangedAddresses(CI::CurrentBindings(first.next),CI::CurrentBindings(back.next));
        assert(changed==std::set<std::string>{"Alice"});
        const auto root=std::filesystem::temp_directory_path()/("ae-identity-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        const auto path=(root/"cache.db").string();
        {
            Database::Connection db(path,Database::Connection::OpenMode::Create);Cache::Initialize(db);
            db.Exec("INSERT INTO character_rows(character_id,account_name,name,created,profession,race,core_verified) VALUES(1,'A','Alice','2015-02-03T04:05:06Z','Necromancer','Asura',10),(2,'B','Alice','2020-01-02T03:04:05Z','Engineer','Human',10);");
            db.Exec("INSERT INTO native_roster_state VALUES('A',10); INSERT INTO native_rosters VALUES('A','Alice'),('A','Unretrieved');");
            const auto historical=CI::ReadHistoricalCore(db,"A");
            assert(historical.names.size()==2&&historical.names.count("Unretrieved")&&historical.observations.size()==1);
            assert(historical.observations.front().evidence==a);
            db.Exec("INSERT INTO native_inventory_scopes VALUES('A','Bags','Alice','Ready',10,10),('A','Bank','','Ready',10,10),('B','Bags','Alice','Ready',10,10);");
            auto adopted=CI::AdoptCache(db,first.next);
            assert(adopted&&adopted->count("Alice")&&db.Scalar("SELECT count(*) FROM character_rows WHERE account_name='A'")==0);
            assert(db.Scalar("SELECT count(*) FROM native_inventory_scopes")==2);
            db.Exec("INSERT INTO character_rows(character_id,account_name,name) VALUES(3,'A','Alice'),(4,'A','Bob');");
            db.Exec("INSERT INTO native_inventory_scopes VALUES('A','Bags','Alice','Ready',20,20),('A','Equipment','Bob','Ready',20,20);");
            db.Exec("CREATE TRIGGER fail_adoption BEFORE DELETE ON character_rows WHEN OLD.name='Alice' BEGIN SELECT RAISE(ABORT,'fixture'); END");
            bool failed=false;try{(void)CI::AdoptCache(db,rename.next);}catch(...){failed=true;}
            assert(failed&&db.Scalar("SELECT count(*) FROM character_rows WHERE account_name='A'")==2);
            assert(db.Scalar("SELECT revision FROM cache_character_identity_state WHERE account='A'")==first.next.revision);
            db.Exec("DROP TRIGGER fail_adoption");
        }
        {
            Database::Connection db(path,Database::Connection::OpenMode::Existing);Cache::Initialize(db);
            // Skip the intermediate mapping entirely, simulating an interrupted
            // adoption followed by a second rename. Durable per-binding revisions
            // still detect the cycle and preserve Bob's unrelated data.
            auto recovered=CI::AdoptCache(db,back.next);
            assert(recovered&&*recovered==std::set<std::string>{"Alice"});
            assert(db.Scalar("SELECT count(*) FROM character_rows WHERE account_name='A' AND name='Bob'")==1);
            assert(db.Scalar("SELECT count(*) FROM native_inventory_scopes WHERE account='A' AND scope='Equipment'")==1);
            auto replay=CI::AdoptCache(db,back.next);assert(replay&&replay->empty());
            assert(!CI::AdoptCache(db,first.next));
            assert(db.Scalar("SELECT count(*) FROM character_rows WHERE account_name='B'")==1);
        }
        std::filesystem::remove_all(root);
    }
    // Distinct group keys, hierarchy, selected leaf reveal and transient search expansion.
    {
        GroupTree tree;
        const GroupPath bank{"a","","Bank"},bags{"a","Hero","Bags"},equipment{"a","Hero","Equipment"},wallet{"b","","Wallet"};
        assert(GroupKey({"a","b:c","d"})!=GroupKey({"a:b","c","d"}));
        std::set<std::string> overrides;
        using namespace AlterEgo::GroupBrowse;
        assert(Expanded(overrides,"guild",false,true)&&overrides.empty());
        assert(!Toggle(overrides,"guild",true)&&overrides.empty());
        assert(Toggle(overrides,"guild")&&Expanded(overrides,"guild",false));
        assert(!Expanded(overrides,"guild",true));
        assert(Reveal(overrides,"guild",true)&&Expanded(overrides,"guild",true));
        assert(PathKey({"a","b:c","d"})==GroupKey({"a","b:c","d"}));
        assert(StorageRank("Bank")<StorageRank("Bags")&&StorageRank("Equipment")<StorageRank("Bags"));
        tree.Add(bank,"Account A",true,{{0,20},{1,1}});
        tree.Add(equipment,"Account A",true,{{2,1}});
        tree.Add(bags,"Account A",true,{{0,3}});
        tree.Add(wallet,"Account B",true,{{3,5}});
        std::set<std::string> expanded;
        auto collapsed=tree.Flatten(expanded);assert(collapsed.size()==2&&collapsed[0].node->label=="Account A");
        auto full=tree.Flatten(expanded,true);assert(full.size()==12&&expanded.empty());
        assert(tree.Expand(GroupKey(bags),expanded)&&expanded.size()==3);
        auto shown=tree.Flatten(expanded);
        assert(shown.size()==7&&shown[5].leaf&&shown[5].leaf->item==0&&shown[5].leaf->count==3);
        assert(!tree.Expand("unknown",expanded)&&expanded.size()==3);
        tree.Clear();tree.Add(bank,"Account A",false,{{0,20}});
        auto single=tree.Flatten({},true);assert(single.size()==2&&single[0].node->label=="Bank"&&single[1].depth==1);
    }
    {
        namespace O=AlterEgo::CharacterOrder;
        const std::vector<O::Entry> rows={
            {{"a","Zed"},"Guardian","2020",80,10,200},
            {{"a","Alpha"},"Necromancer","2022",60,2,100},
            {{"a","Beta"},"","",0,-1,0},
            {{"b","Aardvark"},"Engineer","2019",80,1,300}};
        const std::vector<std::string> accounts={"a","b"},custom={"Beta","Zed","Alpha"};
        auto order=[&](int mode,bool asc){return O::Indices(rows,mode,asc,custom,accounts);};
        assert((order(O::Custom,true)==std::vector<int>{2,0,1,3}));
        assert((order(O::Name,true)==std::vector<int>{1,2,0,3}));
        assert((order(O::Name,false)==std::vector<int>{0,2,1,3}));
        assert((order(O::Profession,false)==std::vector<int>{1,0,2,3}));
        assert((order(O::Level,true)==std::vector<int>{0,1,2,3}));
        assert((order(O::Level,false)==std::vector<int>{1,0,2,3}));
        assert((order(O::Age,false)==std::vector<int>{1,0,2,3}));
        assert((order(O::Birthday,true)==std::vector<int>{1,0,2,3}));
        assert((order(O::LastLogin,true)==std::vector<int>{0,1,2,3}));
        assert((order(O::LastLogin,false)==std::vector<int>{1,0,2,3}));
        assert((O::Indices(rows,O::Name,true,custom,{"b","a"})==std::vector<int>{3,1,2,0}));
        // A filtered subset must preserve its relative order, including roster-only names.
        const std::vector<O::Entry> subset={rows[0],rows[2]};
        assert((O::Indices(subset,O::Custom,true,custom,accounts)==std::vector<int>{1,0}));
        assert((O::Indices(subset,O::Name,false,custom,accounts)==std::vector<int>{0,1}));
    }
    RefreshProgress::Counts scopeProgress;
    scopeProgress.Add("Ready",true);scopeProgress.Add("Refreshing",true);scopeProgress.Add("Missing permissions",true);scopeProgress.Add("Not retrieved",false);
    assert(scopeProgress.total==4&&scopeProgress.ready==1&&scopeProgress.working==1&&scopeProgress.failed==1&&scopeProgress.missing==1);
    assert(RefreshProgress::Character(2,5,"Test","Equipment tabs").find("2/5")!=std::string::npos);
    const auto root=(std::filesystem::temp_directory_path()/("ae-native-inventory-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    auto rows=ParseArray(Json::parse(R"([null,{"id":10,"count":2},{"id":11,"count":1,"binding":"Account"},{"id":12,"count":1,"binding":"Character","bound_to":"A"}])"),"account-a","Bank");
    assert(rows.size()==3&&rows[0].position=="1"&&rows[0].binding==Binding::Unbound);
    assert(LocationLabel(rows[0])=="Bank / Tab 1 / Slot 2");
    // Sparse positions and multiple stacks of one item survive cache persistence.
    Json sparse=Json::array();
    for(int i=0;i<301;++i)sparse.push_back(nullptr);
    for(auto index:{0,9,29,30,120,122,140,270,300})
        sparse[index]=Json{{"id",10},{"count",index==120?3:1}};
    auto stacks=ParseArray(sparse,"location-test","Bank");
    assert(stacks.size()==9&&LocationLabel(stacks[2])=="Bank / Tab 1 / Slot 30");
    assert(LocationLabel(stacks[3])=="Bank / Tab 2 / Slot 1");
    assert(LocationLabel(stacks[4])=="Bank / Tab 5 / Slot 1");
    assert(LocationLabel(stacks[7])=="Bank / Tab 10 / Slot 1");
    assert(LocationLabel(stacks[8])=="Bank / Tab 11 / Slot 1");
    const auto locationRoot=root+"-positions";
    Record(locationRoot,{"location-test","Bank","","Ready",100,100},&stacks);
    auto cached=Read(locationRoot).rows;
    std::sort(cached.begin(),cached.end(),[](const Row& a,const Row& b){return LocationLess(a.slot,b.slot);});
    assert(cached.size()==stacks.size());
    for(size_t i=0;i<stacks.size();++i){
        assert(cached[i].slot.position==stacks[i].position&&cached[i].slot.count==stacks[i].count);
        assert(LocationLabel(cached[i].slot)==LocationLabel(stacks[i]));
    }
    std::filesystem::remove_all(locationRoot);
    auto bag=ParseArray(Json::parse(R"([null,{"id":10,"count":2}])"),"account-a","Bags","A","2/");
    assert(LocationLabel(bag[0])=="Bags / A / Bag 3 / Slot 2");
    auto laterBag=bag[0];laterBag.position="10/0";assert(LocationLess(bag[0],laterBag));
    laterBag.position="2/10";assert(LocationLess(bag[0],laterBag));
    auto shared=rows[0];shared.scope="Shared inventory";shared.position="7";
    assert(LocationLabel(shared)=="Shared inventory / Slot 8");
    auto malformed=rows[0];malformed.position="30/0";
    assert(LocationLabel(malformed)=="Bank"&&LocationLess(rows[0],malformed));
    for(auto bad:{"","-1","4294967295","99999999999999","1x"," 1"}){
        malformed.position=bad;assert(LocationLabel(malformed)=="Bank");
    }
    malformed=bag[0];malformed.position="2/";assert(LocationLabel(malformed)=="Bags / A");
    auto equipment=bag[0];equipment.scope="Equipment";equipment.position="3/WeaponA1";
    assert(LocationLabel(equipment)=="Equipment / A / WeaponA1");
    auto materialLocation=rows[0];materialLocation.scope="Materials";
    assert(LocationLabel(materialLocation)=="Materials");
    auto armoryLocation=rows[0];armoryLocation.scope="Legendary Armory";
    assert(LocationLabel(armoryLocation)=="Legendary Armory (unlocked count)");
    assert(Accessibility(rows[0],"account-b","B",false)==Access::Unknown);
    assert(Accessibility(rows[0],"account-b","B",true)==Access::Transferable);
    assert(Accessibility(rows[1],"account-b","B",true)==Access::Restricted);
    assert(Accessibility(rows[2],"account-a","B",true)==Access::Restricted);
    assert(Accessibility(rows[2],"account-a","A",true)==Access::Available);
    assert(Match("Superior Sigil of Force",10,"sigil force")&&Match("",10,"10"));
    Record(root,{"account-a","Bank","","Ready",100,100},&rows);
    StoreNames(root,Json::array({Json{{"id",10},{"name","Item"},{"icon","https://example.test/icon.png"},{"description","Test description"},{"details",{{"type","Focus"}}},{"flags",Json::array()}}}));
    auto presentation=Read(root).metadata.at(10);assert(presentation.complete&&presentation.icon=="https://example.test/icon.png"&&presentation.description=="Test description"&&presentation.details.at("type")=="Focus");
    // The Native Inventory producer shares item row ownership with direct and Commerce writers.
    const auto newer=Cache::ReferenceSnapshotTime(),older=newer-1;
    auto observations=Json::array({Json{{"id",10},{"name","New item"},{"flags",Json::array({"AccountBound"})}}});
    assert(StoreNames(root,observations,newer).size()==1);
    auto delayed=observations;delayed[0]["name"]="Old item";delayed[0]["flags"]=Json::array();
    auto accepted=Json::array();
    assert(Cache::WriteItemObservations(root,delayed,older,accepted)&&accepted.empty());
    auto itemRows=Cache::ReadItems(root,Cache::ItemDataset::Details);
    assert(itemRows&&itemRows->at(0).at("name")=="New item"&&Read(root).flags.at(10).accountBound);
    auto latest=delayed;latest[0]["name"]="Latest item";
    assert(Cache::WriteItemObservations(root,latest,Cache::ReferenceSnapshotTime(),accepted)&&accepted.size()==1);
    assert(!Read(root).flags.at(10).accountBound);
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
    // Late success AND failure cannot recreate a pruned character scope.
    assert(!Record(root,{"account-a","Bags","A","Ready",103,103},&bags));
    assert(!Record(root,{"account-a","Bags","A","HTTP 503",104,0}));
    assert(!StoreRoster(root,"account-a",{"A","B"},101,100));
    v=Read(root);assert(v.rows.size()==1&&v.rosters.at("account-a")==std::set<std::string>{"B"});
    bool rosterFresh=false;for(const auto& st:v.states)if(st.account=="account-a"&&st.scope=="Roster")rosterFresh=st.status=="Ready"&&st.attempted==102&&st.success==102;
    assert(rosterFresh);
    assert(StoreRoster(root,"account-empty",{},103,102));
    assert(ReadRoster(root,"account-empty").has_value()&&ReadRoster(root,"account-empty")->empty());
    assert(!ReadRoster(root,"unknown-account"));
    assert(!Record(root,{"account-empty","Equipment","Gone","Failed",104,0}));
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
    assert(WalletMatchesFacets("",{},{},{},{},false));
    assert(WalletMatchesFacets("Wallet",{},{},{},{"Currency","Armor"},false));
    assert(!WalletMatchesFacets("Bank",{},{},{},{},false));
    assert(!WalletMatchesFacets("",{"A"},{},{},{},false));
    assert(!WalletMatchesFacets("",{},{"Unknown"},{},{},false));
    assert(!WalletMatchesFacets("",{},{},{"Basic"},{},false));
    assert(!WalletMatchesFacets("",{},{},{},{"Armor"},false));
    assert(!WalletMatchesFacets("",{},{},{},{},true));
    assert(WalletMatchesBalance(1,true,1)&&WalletMatchesBalance(1,false,2));
    assert(!WalletMatchesBalance(0,true,0)&&!WalletMatchesBalance(1,false,1)&&!WalletMatchesBalance(1,true,2));
    assert((SearchIdentity{false,10}!=SearchIdentity{true,10}));
    auto wallet=ParseWallet(Json::parse(R"([{"id":1,"value":123456},{"id":2,"value":0}])"));
    RecordWallet(root,{"account-a","Wallet","","Ready",200,200},&wallet);
    RecordWallet(root,{"account-a","Wallet","","Failed",201,0});
    const auto currencyStamp=Cache::ReferenceSnapshotTime();
    StoreCurrencies(root,Json::parse(R"([{"id":1,"name":"Coin","description":"","order":1,"icon":"https://render.guildwars2.com/file/test/1.png"}])"),currencyStamp);
    StoreCurrencies(root,Json::parse(R"([{"id":1,"name":"Stale coin","order":2,"icon":"old"}])"),currencyStamp-1);
    auto g=ParseGuild(Json::parse(R"([{"upgrade_id":1,"size":2,"coins":500,"inventory":[{"id":10,"count":4},null]}])"),"guild-a");
    GuildRow gr;gr.id="guild-a";gr.name="Guild";gr.status="Ready";gr.attempted=200;gr.success=200;
    RecordGuild(root,gr,&g);StoreGuildAccess(root,"account-a",{"guild-a"},200);StoreGuildAccess(root,"account-b",{"guild-a"},200);
    v=Read(root);assert(v.guilds.size()==1&&v.guilds.at("guild-a").rows.size()==1&&v.guilds.at("guild-a").coins==500);
    assert(v.wallet.size()==2&&v.wallet.front().value>=0);
    for(const auto& row:v.wallet){
        if(row.id==1)assert(row.name=="Coin"&&row.iconKnown&&row.icon=="https://render.guildwars2.com/file/test/1.png");
        if(row.id==2)assert(!row.iconKnown&&row.icon.empty());
    }
    assert(!UsableLoose(g.slots.front(),"account-a","B",{},true));
    assert(UsableLoose(rows.front(),"account-a","B",{},true)&&!UsableLoose(rows.front(),"account-a","B",{},false));
    for(const auto* scope:{"Wallet","Bags","Shared inventory","Bank","Equipment","Delivery box","Materials","Guild bank","Guild access","Legendary Armory"}){
        assert(FreshSeconds(scope)==1800);
        assert(!Due(scope,100,0,999)&&Due(scope,100,0,1000));
        assert(!Due(scope,100,100,1899)&&Due(scope,100,100,1900));
        assert(Due(scope,0,0,100));
        // The Inventory time-based path consumes the same canonical retry result.
        for(int64_t success:{int64_t(0),int64_t(100),int64_t(101),int64_t(1101)})
            for(int64_t attempt:{int64_t(0),int64_t(100),int64_t(101),int64_t(1101)})
                assert(Due(scope,attempt,success,1000)==AlterEgo::RefreshEngine::Due(success,attempt,1000,FreshSeconds(scope)));
    }
    for(const auto* scope:{"Roster"}){
        assert(FreshSeconds(scope)==86400);
        assert(!Due(scope,100,100,86499)&&Due(scope,100,100,86500));
        assert(!Due(scope,100,0,999)&&Due(scope,100,0,1000));
    }
    using AlterEgo::RefreshEngine::InventoryPermitted;
    assert(InventoryPermitted({"wallet"},"Wallet")&&!InventoryPermitted({"wallet"},"Bank"));
    assert(InventoryPermitted({"inventories"},"Bank")&&!InventoryPermitted({"inventories"},"Bags"));
    assert(InventoryPermitted({"inventories","characters"},"Bags")&&!InventoryPermitted({"inventories","characters"},"Legendary Armory"));
    assert(InventoryPermitted({"inventories","unlocks"},"Legendary Armory"));
    assert(InventoryPermitted({"guilds"},"Guild access")&&!InventoryPermitted({"guilds"},"Roster"));
    assert(InventoryPermitted({"characters"},"Roster")&&InventoryPermitted({"tradingpost"},"Delivery box"));
    assert(Match("Superior Sigil of Force",10,"sigli force"));
    using Skinventory::Search::Words;
    using Skinventory::Search::HasMinimumQuery;
    assert(!HasMinimumQuery(Words(""))&&!HasMinimumQuery(Words("  a b ")));
    assert(HasMinimumQuery(Words("val"))&&HasMinimumQuery(Words("valk kr")));
    assert(!CanBrowseItems(false,false,false,false,false,false,false,false));
    // Every nonempty combination of real facets enables browsing, including
    // Bags alone, Character alone, and rarity/type without a character.
    for(unsigned mask=0;mask<256;++mask){
        assert(CanBrowseItems(mask&1,mask&2,mask&4,mask&8,mask&16,mask&32,mask&64,mask&128)==(mask!=0));
    }
    assert(CanBrowseItems(true,true,false,false,false,false,false,false));
    assert(CanBrowseItems(true,false,true,false,false,false,false,false));
    assert(CanBrowseItems(true,false,false,true,false,false,false,false));
    assert(CanBrowseItems(true,false,false,false,true,false,false,false));
    assert(CanBrowseItems(true,false,false,false,false,true,false,false));
    assert(CanBrowseItems(true,false,false,false,false,false,true,false));
    assert(CanBrowseItems(true,false,false,false,false,false,false,true));
    const std::set<std::string> twoCharacters{"A","B"};
    assert(MatchesStorageView("Bags",{"A"},"Bags","A"));
    assert(!MatchesStorageView("Bags",{"A"},"Bags","B"));
    assert(!MatchesStorageView("Bags",{"A"},"Equipment","A"));
    assert(!MatchesStorageView("Items",{"A"},"Bank",""));
    assert(MatchesStorageView("Items",twoCharacters,"Equipment","B"));
    assert(MatchesStorageView("Items",{},"Bank",""));
    assert(MatchesStorageView("Bags",{},"Bags","B"));
    assert(MatchesStorageView("Items",twoCharacters,"Bags","A"));
    assert(MatchesStorageView("Items",twoCharacters,"Equipment","B"));
    assert(!MatchesStorageView("Items",twoCharacters,"Bags","C"));
    assert(!MatchesStorageView("Bank",twoCharacters,"Bank",""));
    assert(MatchesStorageView("",{},"Bank",""));
    assert(CanBrowseItems(!twoCharacters.empty(),false,false,false,false,false,false,false));
    assert(Match("Valkyrie Krait Star",12345,"valk kr"));
    assert(Match("Valkyrie Krait Star",12345,"KR VALK"));
    assert(!Match("Valkyrie Krait Star",12345,"valk sword"));
    assert(!Match("Valkyrie Krait Star",12345,"12346"));
    assert(Match("Valkyrie Krait Star",12345,"12345"));
    assert(!Match("Valkyrie Krait Star",123456,"12345"));
    assert(Match("Valkyrie Krait Star",12345,"12345 valk"));
    assert(Match("Version 12345",99,"12345"));
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
    // Persist outgoing A after selection moves to B, then a newer season;
    // late Special patches from the old season cannot resurrect stale data.
    const Json outgoing{{"account","account-a"},{"season_end",300},{"special",vaultPeriod(250,7)}};
    assert(Cache::WriteVaultPeriods(root,"account-b",Json{{"account","account-b"},{"daily",vaultPeriod(260,8)}}));
    assert(Cache::WriteVaultPeriods(root,"account-a",outgoing));
    assert(Cache::ReadCompletion(root,CompletionDataset::Vault,"account-a")->at("special").at("fetched_at")==250);
    assert(Cache::WriteVaultPeriods(root,"account-a",Json{{"account","account-a"},{"season_end",600}}));
    assert(!Cache::ReadCompletion(root,CompletionDataset::Vault,"account-a")->contains("special"));
    assert(Cache::WriteVaultPeriods(root,"account-a",Json{{"account","account-a"},{"season_end",300},{"special",vaultPeriod(320,9)}}));
    assert(!Cache::ReadCompletion(root,CompletionDataset::Vault,"account-a")->contains("special"));
    assert(Cache::WriteVaultPeriods(root,"account-a",Json{{"account","account-a"},{"season_end",600},{"special",vaultPeriod(330,10)}}));
    snap=Cache::ReadCompletion(root,CompletionDataset::Vault,"account-a");
    assert(snap&&snap->at("special").at("fetched_at")==330&&snap->at("weekly").at("fetched_at")==100);
    assert(Cache::ReadCompletion(root,CompletionDataset::Vault,"account-b")->at("daily").at("fetched_at")==260);
    std::filesystem::remove_all(root);{
        using namespace AlterEgo::NativeInventory;
        Slot slot;slot.account="a";slot.scope="Materials";slot.binding=Binding::Unknown;
        ItemFlags flags;flags.known=true;
        assert(PotentiallySellable(slot,flags,true));
        assert(!PotentiallySellable(slot,flags,false));
        slot.scope="Bank";assert(!PotentiallySellable(slot,flags,true));
        slot.binding=Binding::Unbound;assert(PotentiallySellable(slot,flags,true));
        slot.binding=Binding::Account;assert(!PotentiallySellable(slot,flags,true));
        slot.binding=Binding::Unbound;flags.accountBound=true;assert(!PotentiallySellable(slot,flags,true));
    }
    {
        // User's Mystic Coin example: 1 in a character bag and 24 in
        // material storage. The character facet selects only the bag.
        Slot bag;bag.id=19976;bag.account="a";bag.scope="Bags";bag.character="Nul Sul";
        bag.binding=Binding::Unbound;bag.count=1;
        Slot materials=bag;materials.scope="Materials";materials.character.clear();
        materials.binding=Binding::Unknown;materials.count=24;
        ItemFlags flags;flags.known=true;
        SellableQuantity all,matching;
        for(const auto& slot:{bag,materials}){
            all.Add(slot,flags,true,true);
            if(MatchesStorageView("",{"Nul Sul"},slot.scope,slot.character))
                matching.Add(slot,flags,true,true);
        }
        assert(all.count==25&&matching.count==1);
        assert(Skinventory::EstimateInstantSale(*all.count,20537)==436410);
        assert(Skinventory::EstimateInstantSale(*matching.count,20537)==17456);
        // Stale, restricted, unknown non-material binding, excluded account,
        // and unverified metadata must contribute to neither quantity.
        all.Add(bag,flags,false,true);
        all.Add(bag,flags,true,false);
        auto restricted=bag;restricted.binding=Binding::Account;
        all.Add(restricted,flags,true,true);
        restricted.binding=Binding::Character;all.Add(restricted,flags,true,true);
        restricted.binding=Binding::Unknown;all.Add(restricted,flags,true,true);
        auto unknown=flags;unknown.known=false;all.Add(bag,unknown,true,true);
        auto bound=flags;bound.accountBound=true;all.Add(bag,bound,true,true);
        assert(all.count==25);
        SellableQuantity empty;empty.Add(bag,flags,false,true);assert(empty.count==0);
        SellableQuantity same;same.Add(bag,flags,true,true);same.Add(materials,flags,true,true);
        assert(same.count==all.count);
        SellableQuantity overflow;auto huge=bag;huge.count=std::numeric_limits<int64_t>::max();
        overflow.Add(huge,flags,true,true);overflow.Add(bag,flags,true,true);
        assert(!overflow.count);overflow.Add(bag,flags,true,true);assert(!overflow.count);
    }
    return 0;
}
