#include "NativeAccounts.h"
#include "UntrustedInput.h"
#include "AccountCommitGate.h"
#include "NativeInventoryStore.h"
#include "InventoryGroupCore.h"
#include "ArenaNetTransport.h"
#include "BackgroundWorkers.h"
#include "AddonShared.h"
#include "CatalogPresentation.h"
#include "GW2API.h"
#include "AutoRefreshPolicy.h"
#include "CharacterActivity.h"
#include "RefreshUI.h"
#include "Commerce.h"
#include "CommercePriceCore.h"
#include "RefreshProgress.h"
#include "ChatLink.h"
#include <windows.h>
#include <wincred.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <map>
#include <sstream>

namespace AlterEgo::NativeAccounts {
namespace {
using namespace NativeInventory;
struct Account {std::string id,name,label;std::set<std::string> permissions;};
struct View {
    std::vector<Account> accounts;Projection inventory;
    uint64_t credentialEpoch=1;
    std::map<uint32_t,std::vector<size_t>> index;
    std::map<std::tuple<std::string,std::string,std::string>,ScopeState> scopes;
};
std::atomic<uint64_t> inventoryGeneration{0};
uint32_t revealItem=0;
InventoryNavigation navigation;
std::set<std::string> expandedInventoryGroups;
bool groupRevealPending=false;
// Render-thread preferences, persisted in the ordinary settings file.
std::set<uint32_t> walletPins;
std::string root;
std::mutex viewMutex,publishMutex,credentialMutex;
std::atomic<uint64_t> registryGeneration{1};
std::atomic<bool> registryReady{false};
std::shared_ptr<const View> view=std::make_shared<View>();
std::atomic<bool> busy{false};
std::mutex physicalMutex,rosterMutex;
struct PhysicalEntry {uint64_t epoch=0;PhysicalEquipmentResult value;std::chrono::steady_clock::time_point finished{};};
std::map<std::pair<std::string,std::string>,PhysicalEntry> physicalCache;
std::string message;
RefreshUI::State messageState=RefreshUI::State::Ready;
using ScopeKey=std::tuple<std::string,std::string,std::string>;
// Session-only progress overlay; no database reads or inventory index rebuilds on render.
std::map<ScopeKey,ScopeState> liveScopes;
unsigned processedScopes=0;
std::string activeScope;
void BeginScope(const ScopeState& state,const std::string& label){
    std::lock_guard<std::mutex> lock(viewMutex);
    auto current=state;current.status="Refreshing";
    auto prior=view->scopes.find({state.account,state.scope,state.character});
    if(prior!=view->scopes.end())current.success=prior->second.success;
    liveScopes[{state.account,state.scope,state.character}]=std::move(current);
    activeScope=label;
}
void EndScope(ScopeState state){
    std::lock_guard<std::mutex> lock(viewMutex);
    auto key=ScopeKey{state.account,state.scope,state.character};
    // Shared endpoint owners already published the authoritative outcome.
    // Do not let a delayed inventory consumer overlay a newer owner result.
    if(state.scope=="Equipment"||state.scope=="Roster"){
        liveScopes.erase(key);++processedScopes;activeScope.clear();return;
    }
    auto prior=liveScopes.find(key);
    if(state.status!="Ready"&&prior!=liveScopes.end())state.success=prior->second.success;
    liveScopes[key]=std::move(state);++processedScopes;activeScope.clear();
}
void SetActiveScope(const std::string& label){
    std::lock_guard<std::mutex> lock(viewMutex);activeScope=label;
}
void ClearProgress(){
    std::lock_guard<std::mutex> lock(viewMutex);liveScopes.clear();processedScopes=0;activeScope.clear();
}
const std::vector<std::string> recommended={"account","characters","inventories","builds","progression","unlocks","wallet","guilds","tradingpost"};
std::string Target(const std::string& id){return "AlterEgo/GW2/"+id;}
bool ValidGuid(const std::string& s){
    if(s.size()!=36)return false;
    for(size_t i=0;i<s.size();++i)if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}
    else if(!std::isxdigit(static_cast<unsigned char>(s[i])))return false;
    return true;
}
struct Secret {
    std::string value;
    explicit Secret(std::string v):value(std::move(v)){}
    ~Secret(){if(!value.empty())SecureZeroMemory(&value[0],value.size());}
    Secret(const Secret&)=delete;Secret& operator=(const Secret&)=delete;
};
std::string Key(const std::string& id){
    PCREDENTIALA cred=nullptr;
    if(!CredReadA(Target(id).c_str(),CRED_TYPE_GENERIC,0,&cred))return {};
    std::string result(reinterpret_cast<const char*>(cred->CredentialBlob),cred->CredentialBlobSize);
    if(cred->CredentialBlob&&cred->CredentialBlobSize)SecureZeroMemory(cred->CredentialBlob,cred->CredentialBlobSize);
    CredFree(cred);return result;
}
bool SaveKey(const std::string& id,const std::string& key){
    std::string target=Target(id);CREDENTIALA cred{};cred.Type=CRED_TYPE_GENERIC;cred.TargetName=&target[0];
    cred.CredentialBlobSize=static_cast<DWORD>(key.size());cred.CredentialBlob=reinterpret_cast<LPBYTE>(const_cast<char*>(key.data()));
    cred.Persist=CRED_PERSIST_LOCAL_MACHINE;return CredWriteA(&cred,0)!=FALSE;
}
std::filesystem::path AccountPath(){return Storage::Paths(root).Accounts();}
void InitAccounts(Database::Connection& db){
    Database::Migrate(db,Storage::kAccountRegistryApplicationId,Storage::kAccountRegistrySchemaVersion,[](Database::Connection& c,int){
        c.Exec("CREATE TABLE accounts(id TEXT PRIMARY KEY,name TEXT NOT NULL,label TEXT NOT NULL,permissions TEXT NOT NULL) STRICT");
    });
}
std::vector<Account> Accounts(){
    std::vector<Account> out;const auto p=AccountPath();if(!std::filesystem::exists(p))return out;
    Database::Connection db(p.u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
    auto q=db.Prepare("SELECT id,name,label,permissions FROM accounts ORDER BY name");
    while(q.Step()){
        Account a;a.id=q.Text(0);a.name=q.Text(1);a.label=q.Text(2);
        if(!ValidGuid(a.id)||a.name.empty())throw std::runtime_error("Invalid account registry");
        a.permissions=Json::parse(q.Text(3)).get<std::set<std::string>>();out.push_back(std::move(a));
    }
    return out;
}
void Publish(const std::string& msg,bool replaceMessage=true,RefreshUI::State state=RefreshUI::State::Ready){
    std::lock_guard<std::mutex> serial(publishMutex);
    auto next=std::make_shared<View>();next->credentialEpoch=registryGeneration.load();next->accounts=Accounts();next->inventory=Read(root);
    for(size_t i=0;i<next->inventory.rows.size();++i)next->index[next->inventory.rows[i].slot.id].push_back(i);
    // All location consumers share this order; do not reorder row identities.
    for(auto& entry:next->index)std::sort(entry.second.begin(),entry.second.end(),[&](size_t a,size_t b){
        return LocationLess(next->inventory.rows[a].slot,next->inventory.rows[b].slot);
    });
    for(const auto& s:next->inventory.states)next->scopes[{s.account,s.scope,s.character}]=s;
    std::lock_guard<std::mutex> lock(viewMutex);view=std::move(next);++inventoryGeneration;if(replaceMessage){message=msg;messageState=state;}registryReady=true;
}
std::shared_ptr<const View> Snapshot(){std::lock_guard<std::mutex> lock(viewMutex);return view;}
void Message(const std::string& msg,RefreshUI::State state=RefreshUI::State::Error){std::lock_guard<std::mutex> lock(viewMutex);message=msg;messageState=state;}
std::string Status(){std::lock_guard<std::mutex> lock(viewMutex);return message;}
template<class Job> void Launch(Job job){
    if(busy.exchange(true))return;
    ClearProgress();
    Message("Working...",RefreshUI::State::Working);
    Background::Workers().Launch([job=std::move(job)]{
        try{job();}catch(const Background::Cancelled&){Message("Operation cancelled.");}catch(...){Message("Operation failed; previous verified data retained.");}
        ClearProgress();busy=false;
    },[]{Message("Operation cancelled.");ClearProgress();busy=false;});
}
int64_t Now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string Error(int code){
    if(code==429)return "Rate limited; retry later";
    if(code==401||code==403)return "Invalid key or missing permission";
    if(code==0)return "Transport failed or cancelled";
    return "HTTP "+std::to_string(code);
}
Json Fetch(const std::string& path,const std::string& key){
    Background::Checkpoint();
    auto r=ArenaNet::Get(path,key);
    Background::Checkpoint();
    if(r.status_code!=200)throw std::runtime_error(Error(r.status_code));
    return Untrusted::Parse(r.body,Untrusted::PublicApi);
}
std::string Encode(const std::string& s){
    constexpr char hex[]="0123456789ABCDEF";std::string out;
    for(unsigned char c:s){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')out+=static_cast<char>(c);
        else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}
    return out;
}
struct RosterResult {
    ScopeState state;
    std::set<std::string> names;
    bool accepted=false;
};
struct RosterEntry {
    uint64_t epoch=0;
    RosterResult result;
    std::chrono::steady_clock::time_point finished{};
};
std::map<std::string,RosterEntry> rosterCache;
// One owner for endpoint, durable roster, pruning and status.
RosterResult FetchRoster(const LinkedAccount& a){
    using Clock=std::chrono::steady_clock;
    const auto queued=Clock::now();
    RosterResult result;result.state={a.id,"Roster",{},"Roster retrieval failed",0,0};
    std::unique_lock<std::mutex> lock(rosterMutex,std::defer_lock);
    while(!lock.try_lock()){
        if(Background::Workers().Stopping()||!Current(a))return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if(Background::Workers().Stopping()||!Current(a))return result;
    const auto previous=rosterCache.find(a.id);
    if(previous!=rosterCache.end()&&previous->second.epoch==a.generation&&
       previous->second.result.accepted&&previous->second.result.state.status=="Ready"&&
       previous->second.finished>=queued)return previous->second.result;
    result.state.attempted=Now();
    try{
        const auto response=Request(a,"/v2/characters",{"characters"});
        if(!response||!response.data.is_array()){
            if(!a.permissions.count("characters"))result.state.status="Missing permissions";
            throw std::runtime_error("Roster request failed");
        }
        for(const auto& n:response.data){
            const auto name=n.get<std::string>();
            if(name.empty()||!result.names.insert(name).second)throw std::runtime_error("Invalid roster");
        }
        result.state.status="Ready";result.state.success=Now();
    }catch(...){result.names.clear();result.state.success=0;if(result.state.status=="Ready")result.state.status="Roster retrieval failed";}
    try{
        result.accepted=CommitIfCurrent(a,[&]{
            if(result.state.status=="Ready"){
                if(!StoreRoster(root,a.id,result.names,result.state.success,result.state.attempted))return false;
            }else if(!Record(root,result.state))return false;
            Publish("",false);return true;
        });
    }catch(...){result.accepted=false;}
    if(result.accepted&&result.state.status=="Ready")
        rosterCache[a.id]={a.generation,result,Clock::now()};
    else rosterCache.erase(a.id);
    return result;
}
void Link(const std::shared_ptr<Secret>& key,const std::string& label,const std::string& expected){
    Launch([key,label,expected]{
        try{
            const auto token=Fetch("/v2/tokeninfo",key->value);
            const auto identity=Fetch("/v2/account",key->value);
            Account a;a.id=identity.at("id").get<std::string>();a.name=identity.at("name").get<std::string>();a.label=label;
            a.permissions=token.at("permissions").get<std::set<std::string>>();
            if(!ValidGuid(a.id)||a.name.empty()||!a.permissions.count("account"))throw std::runtime_error("Invalid account identity");
            if(!expected.empty()&&expected!=a.id){Message("Key belongs to a different account.");return;}
            for(const auto& old:Accounts())if(old.id==a.id&&expected.empty()){Message("Account already linked. Select Edit to replace its key.");return;}
            // Lock order: credential gate before registry database transaction.
            std::unique_lock<std::mutex> credentialLock(credentialMutex);
            std::filesystem::create_directories(AccountPath().parent_path());
            Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Create);InitAccounts(db);
            Database::Transaction tx(db);
            auto q=db.Prepare("INSERT INTO accounts VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,label=excluded.label,permissions=excluded.permissions");
            q.Bind(1,a.id);q.Bind(2,a.name);q.Bind(3,a.label);q.Bind(4,Json(a.permissions).dump());q.Execute();
            Secret previous(Key(a.id));
            if(!SaveKey(a.id,key->value)){Message("Windows Credential Manager rejected the key.");return;}
            try{tx.Commit();}catch(...){if(previous.value.empty())CredDeleteA(Target(a.id).c_str(),CRED_TYPE_GENERIC,0);else SaveKey(a.id,previous.value);throw;}
            ++registryGeneration;
            Publish("Account linked. Inventory can now be refreshed.",true,RefreshUI::State::Success);
            credentialLock.unlock();
            // Network admission never runs under the credential gate.
            // RefreshRoster rechecks the newly published epoch before committing.
            if(a.permissions.count("characters")){
                for(const auto& linked:Linked())if(linked.id==a.id){
                    std::set<std::string> names;(void)RefreshRoster(linked,names);break;
                }
            }
        }catch(const std::exception& e){
            // Never show arbitrary JSON exception text or HTTP response bodies.
            const std::string s=e.what();
            Message(s.rfind("HTTP ",0)==0||s=="Invalid key or missing permission"||s=="Transport failed or cancelled"||s=="Rate limited; retry later"?s:"Account validation or storage failed.");
        }
    });
}
void Remove(const std::string& id){
    Launch([id]{
        std::lock_guard<std::mutex> credentialLock(credentialMutex);
        Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
        Database::Transaction tx(db);auto q=db.Prepare("DELETE FROM accounts WHERE id=?");q.Bind(1,id);q.Execute();
        tx.Commit();++registryGeneration;
        const bool removed=CredDeleteA(Target(id).c_str(),CRED_TYPE_GENERIC,0)!=FALSE||GetLastError()==ERROR_NOT_FOUND;
        Publish(removed?"Account unlinked. Library unchanged.":"Account unlinked; remove its remaining credential in Windows Credential Manager.",true,removed?RefreshUI::State::Success:RefreshUI::State::Error);
    });
}
bool AutoScope(const std::string& scope){
    using D=AutoRefreshPolicy::Dataset;
    if(scope=="Bank")return AutoRefreshPolicy::Enabled(D::Bank);
    if(scope=="Shared inventory")return AutoRefreshPolicy::Enabled(D::SharedInventory);
    if(scope=="Materials")return AutoRefreshPolicy::Enabled(D::Materials);
    if(scope=="Bags")return AutoRefreshPolicy::Enabled(D::Bags);
    if(scope=="Equipment")return AutoRefreshPolicy::Enabled(D::Equipment);
    if(scope=="Legendary Armory")return AutoRefreshPolicy::Enabled(D::Armory);
    if(scope=="Delivery box")return AutoRefreshPolicy::Enabled(D::Delivery);
    if(scope=="Wallet")return AutoRefreshPolicy::Enabled(D::Wallet);
    if(scope=="Guild bank"||scope=="Guild access")return AutoRefreshPolicy::Enabled(D::Guild);
    if(scope=="Roster")return AutoScope("Bags")||AutoScope("Equipment");
    return false;
}
bool ActivityScope(const std::string& scope){return scope=="Bags"||scope=="Equipment"||scope=="Bank"||scope=="Shared inventory"||scope=="Materials"||scope=="Legendary Armory";}
bool ScopeFresh(const std::string& account,const std::string& scope,const std::string& character,int64_t success){
    if(ActivityScope(scope))return CharacterActivity::Fresh({account,character},scope,success,scope=="Equipment"&&CharacterActivity::SpecSignal().load());
    return success>0&&Now()>=success&&Now()-success<FreshSeconds(scope);
}
bool ScopeDue(const View& v,const std::string& account,const std::string& scope,const std::string& character={}){
    auto it=v.scopes.find({account,scope,character});
    if(!AutoScope(scope))return false;
    {
        auto a=std::find_if(v.accounts.begin(),v.accounts.end(),[&](const Account& a){return a.id==account;});
        if(a==v.accounts.end())return false;
        const bool permitted=scope=="Wallet"?a->permissions.count("wallet"):
            scope=="Guild access"?a->permissions.count("guilds"):
            scope=="Roster"?a->permissions.count("characters"):
            scope=="Delivery box"?a->permissions.count("tradingpost"):
            a->permissions.count("inventories")&&((scope!="Bags"&&scope!="Equipment")||a->permissions.count("characters"))&&(scope!="Legendary Armory"||a->permissions.count("unlocks"));
        if(!permitted)return false;
    }
    const auto success=it==v.scopes.end()?0:it->second.success;
    const auto attempt=it==v.scopes.end()||it->second.status=="Ready"?0:it->second.attempted;
    if(ActivityScope(scope))return CharacterActivity::Due({account,character},scope,success,attempt,scope=="Equipment"&&CharacterActivity::SpecSignal().load());
    return Due(scope,attempt,success,Now());
}
bool Fresh(const View& v,const Row& row){
    const auto& s=row.slot;auto it=v.scopes.find({s.account,s.scope,s.character});
    return it!=v.scopes.end()&&it->second.status=="Ready"&&ScopeFresh(s.account,s.scope,s.character,row.captured);
}
void Refresh(const std::vector<Account>& accounts,uint64_t epoch,const std::string& onlyScope,bool automatic=false,const std::string& onlyCharacter={},const std::set<std::string>& characterFilter={},bool includeWallet=false){
    Launch([accounts,epoch,onlyScope,automatic,onlyCharacter,characterFilter,includeWallet]{
        ApiActivity::ReasonScope reason(automatic?ApiActivity::Reason::Visible:ApiActivity::Reason::Manual);
        const auto previous=Snapshot();
        if(previous->credentialEpoch!=epoch)throw Background::Cancelled{};
        std::set<uint32_t> ids;std::set<std::string> fetchedGuilds;
        bool currencyMetadataNeeded=false;
        auto lastPublish=std::chrono::steady_clock::time_point{};
        // Scope completion is immediate in the tooltip; full projections are
        // coalesced on the worker to avoid rebuilding the item index per request.
        auto publishProgress=[&](bool force=false){
            const auto now=std::chrono::steady_clock::now();
            if(force||now-lastPublish>=std::chrono::seconds(5)){
                Publish("",false);lastPublish=std::chrono::steady_clock::now();
            }
        };
        for(const auto& a:accounts){
            // Account selection and permissions came from the same immutable
            // registry view at admission. Never attach a later credential to it.
            LinkedAccount linked;linked.id=a.id;linked.name=a.name;
            linked.permissions=a.permissions;linked.generation=epoch;
            const auto checkCurrent=[&]{
                Background::Checkpoint();
                if(!Current(linked))throw Background::Cancelled{};
            };
            checkCurrent();
            std::unique_lock<std::mutex> credentialLock(credentialMutex);
            checkCurrent();
            Secret key(Key(a.id));credentialLock.unlock();
            if(key.value.empty()){Message("Stored credential unavailable. Replace it in Account Management.");continue;}
            const auto fetchAccount=[&](const std::string& path){
                checkCurrent();
                return Fetch(path,key.value);
            };
            const auto commit=[&](const auto& write){
                if(!CommitIfCurrent(linked,[&]{write();return true;}))
                    throw Background::Cancelled{};
            };
            auto scope=[&](const std::string& name,const std::string& character,const std::string& path,bool bags){
                if(!MatchesStorageView(onlyScope,characterFilter,name,character))return;
                if(!onlyCharacter.empty()&&character!=onlyCharacter)return;
                if(automatic&&!ScopeDue(*previous,a.id,name,character))return;
                checkCurrent();
                ScopeState state{a.id,name,character,"Ready",Now(),0};
                BeginScope(state,(accounts.size()>1?a.name+" / ":"")+name+(character.empty()?"":" / "+character));
                const bool characterScope=bags||name=="Equipment";
                const bool permitted=name=="Delivery box"?a.permissions.count("tradingpost")!=0:
                    a.permissions.count("inventories")&&(!characterScope||a.permissions.count("characters"))&&
                    (name!="Legendary Armory"||a.permissions.count("unlocks"));
                if(!permitted&&name!="Equipment"){
                    state.status="Missing permissions";commit([&]{if(!Record(root,state))throw Background::Cancelled{};});EndScope(state);publishProgress();return;
                }
                CharacterActivity::CancelOnShutdown attempts({a.id,character});
                CharacterActivity::Ticket ticket{};
                const auto scopeCommit=[&](const auto& write){
                    try{commit(write);}
                    catch(const Background::Cancelled&){
                        if(ticket.serial)CharacterActivity::Cancel({a.id,character},name,ticket);
                        throw;
                    }
                };
                try{
                    if(name=="Equipment"){
                        auto physical=RequestPhysicalEquipment(linked,character,!automatic);
                        checkCurrent();
                        // The physical owner alone records success AND failure.
                        if(!physical.started)throw Background::Cancelled{};
                        state.attempted=physical.started;
                        if(physical.response&&physical.inventoryCommitted){
                            state.success=physical.success;
                            auto slots=ParseEquipment(physical.response.data,a.id,character);
                            for(const auto& slot:slots){ids.insert(slot.id);ids.insert(slot.upgrades.begin(),slot.upgrades.end());ids.insert(slot.infusions.begin(),slot.infusions.end());}
                        }else state.status=physical.response?"Invalid or incomplete response":physical.response.error;
                        EndScope(state);publishProgress();return;
                    }
                    ticket=ActivityScope(name)?attempts.BeginScope(name):CharacterActivity::Ticket{};
                    auto j=fetchAccount(path);std::vector<Slot> slots;
                    if(bags){
                        const auto& b=j.at("bags");if(!b.is_array())throw std::runtime_error("Invalid bags");
                        size_t i=0;for(const auto& bag:b){auto prefix=std::to_string(i++)+"/";if(bag.is_null())continue;
                            auto rows=ParseArray(bag.at("inventory"),a.id,name,character,prefix);
                            slots.insert(slots.end(),rows.begin(),rows.end());}
                    }else if(name=="Equipment")slots=ParseEquipment(j,a.id,character);
                    else if(name=="Legendary Armory")slots=ParseArmory(j,a.id);
                    else if(name=="Delivery box")slots=ParseArray(j.at("items"),a.id,name,{},{},false);
                    else slots=ParseArray(j,a.id,name,character,{},name!="Materials");
                    Background::Checkpoint();state.success=Now();
                    scopeCommit([&]{
                        if(!Record(root,state,&slots))throw Background::Cancelled{};
                        if(ActivityScope(name))CharacterActivity::Success({a.id,character},name,ticket,state.success);
                    });
                    for(const auto& s:slots){ids.insert(s.id);ids.insert(s.upgrades.begin(),s.upgrades.end());ids.insert(s.infusions.begin(),s.infusions.end());}
                }catch(const Background::Cancelled&){
                    if(ticket.serial)CharacterActivity::Cancel({a.id,character},name,ticket);
                    throw;
                }catch(const std::exception& e){
                    Background::Checkpoint();
                    const std::string what=e.what();
                    state.status=what.rfind("HTTP ",0)==0||what=="Invalid key or missing permission"||what=="Transport failed or cancelled"||what=="Rate limited; retry later"?what:"Invalid or incomplete response";
                    if(name=="Equipment")throw;
                    scopeCommit([&]{if(!Record(root,state))throw Background::Cancelled{};});
                }
                EndScope(state);publishProgress();
            };
            if((onlyScope.empty()||onlyScope=="Wallet"||(includeWallet&&onlyScope=="Items"))&&(!automatic||ScopeDue(*previous,a.id,"Wallet"))){
                checkCurrent();
                ScopeState state{a.id,"Wallet","","Ready",Now(),0};
                BeginScope(state,"Wallet");
                try{
                    if(!a.permissions.count("wallet"))throw std::runtime_error("Missing wallet permission");
                    auto rows=ParseWallet(fetchAccount("/v2/account/wallet"));state.success=Now();
                    commit([&]{RecordWallet(root,state,&rows);});currencyMetadataNeeded=true;
                }catch(const Background::Cancelled&){throw;}catch(...){Background::Checkpoint();state.status=a.permissions.count("wallet")?"Wallet request failed":"Missing permissions";commit([&]{RecordWallet(root,state);});}
                EndScope(state);publishProgress();
            }
            if((onlyScope.empty()||onlyScope=="Guild bank")&&(!automatic||ScopeDue(*previous,a.id,"Guild access"))){
                checkCurrent();
                SetActiveScope("Guild access");
                try{
                    if(!a.permissions.count("guilds"))throw std::runtime_error("Missing guilds permission");
                    auto identity=fetchAccount("/v2/account");
                    if(identity.at("id").get<std::string>()!=a.id)throw std::runtime_error("Account mismatch");
                    std::set<std::string> guilds;
                    for(const auto& raw:identity.at("guild_leader")){auto id=raw.get<std::string>();if(!ValidGuid(id))throw std::runtime_error("Invalid guild ID");guilds.insert(id);}
                    commit([&]{StoreGuildAccess(root,a.id,guilds,Now());});
                    for(const auto& id:guilds){
                        checkCurrent();
                        if(fetchedGuilds.count(id))continue;
                        auto old=previous->inventory.guilds.find(id);
                        if(automatic&&old!=previous->inventory.guilds.end()&&!Due("Guild bank",old->second.attempted,old->second.success,Now()))continue;
                        GuildRow g;g.id=id;g.attempted=Now();
                        SetActiveScope("Guild bank / "+(old!=previous->inventory.guilds.end()&&!old->second.name.empty()?old->second.name:id));
                        try{
                            auto meta=Fetch("/v2/guild/"+id,{});g.name=meta.at("name").get<std::string>();g.tag=meta.value("tag",std::string{});
                        }catch(const Background::Cancelled&){throw;}catch(...){Background::Checkpoint();}
                        try{
                            auto stock=ParseGuild(fetchAccount("/v2/guild/"+id+"/stash"),id);
                            g.status="Ready";g.success=Now();commit([&]{RecordGuild(root,g,&stock);});
                            for(const auto& s:stock.slots)ids.insert(s.id);
                            fetchedGuilds.insert(id);
                        }catch(const Background::Cancelled&){throw;}catch(...){Background::Checkpoint();g.status="Guild stash unavailable";commit([&]{RecordGuild(root,g);});}
                        publishProgress(true);
                    }
                }catch(const Background::Cancelled&){throw;}catch(...){Background::Checkpoint();
                    ScopeState state{a.id,"Guild access","",a.permissions.count("guilds")?"Guild access request failed":"Missing permissions",Now(),0};commit([&]{Record(root,state);});
                }
                SetActiveScope("");publishProgress(true);
            }
            scope("Bank",{},"/v2/account/bank",false);
            scope("Shared inventory",{},"/v2/account/inventory",false);
            scope("Materials",{},"/v2/account/materials",false);
            scope("Legendary Armory",{},"/v2/account/legendaryarmory",false);
            scope("Delivery box",{},"/v2/commerce/delivery",false);
            if((onlyScope.empty()||onlyScope=="Items"||onlyScope=="Bags"||onlyScope=="Equipment"||onlyScope=="Roster")&&(!automatic||AutoScope("Roster"))){
                std::set<std::string> names;
                auto cached=previous->inventory.rosters.find(a.id);
                if(!a.permissions.count("characters")&&automatic&&!ScopeDue(*previous,a.id,"Roster"))continue;
                if(a.permissions.count("characters")&&automatic&&onlyScope!="Roster"&&cached!=previous->inventory.rosters.end())names=cached->second;
                else if(a.permissions.count("characters")&&automatic&&!ScopeDue(*previous,a.id,"Roster")){
                    if(cached!=previous->inventory.rosters.end())names=cached->second;
                }else{
                    ScopeState state{a.id,"Roster","","Ready",Now(),0};BeginScope(state,"Roster");
                    auto result=FetchRoster(linked);
                    if(!result.accepted)throw Background::Cancelled{};
                    EndScope(result.state);publishProgress(true);
                    if(result.state.status!="Ready")continue;
                    names=std::move(result.names);
                }
                // Do not relabel later bag/equipment failures as roster failures.
                for(const auto& ch:names){
                    const auto path="/v2/characters/"+Encode(ch);
                    scope("Bags",ch,path+"/inventory",true);
                    scope("Equipment",ch,path+"/equipment?v=2024-04-01T00:00:00.000Z",false);
                }
            }
        }
        // Resolve names in bounded batches. Missing metadata cannot discard verified slots.
        SetActiveScope("Resolving item and currency metadata");
        publishProgress(true);
        auto cached=Read(root);
        for(const auto& [id,meta]:cached.metadata)if(meta.complete&&cached.flags.count(id))ids.erase(id);
        
        if(currencyMetadataNeeded&&std::any_of(cached.wallet.begin(),cached.wallet.end(),[](const WalletRow& w){return w.name.empty()||!w.iconKnown;}))
            try{
                const auto stamp=Cache::ReferenceSnapshotTime();
                auto definitions=Fetch("/v2/currencies?ids=all",{});
                Background::Checkpoint();StoreCurrencies(root,definitions,stamp);
            }catch(const Background::Cancelled&){throw;}catch(...){Background::Checkpoint();}
        std::string batch;unsigned count=0;std::set<uint32_t> expected;
        unsigned metadataDone=0;const auto metadataTotal=ids.size();
        auto flush=[&]{
            if(batch.empty())return;
            SetActiveScope("Item metadata "+std::to_string(metadataDone)+"/"+std::to_string(metadataTotal));
            try{
                const auto stamp=Cache::ReferenceSnapshotTime();
                auto rows=Fetch("/v2/items?ids="+batch,{});
                if(!rows.is_array()||rows.empty())throw std::runtime_error("Invalid item response");
                std::set<uint32_t> seen;
                for(const auto& row:rows){const auto id=ItemId(row.at("id"));
                    if(!expected.count(id)||!seen.insert(id).second)throw std::runtime_error("Unexpected item metadata ID");}
                std::lock_guard<std::mutex> publication(Cache::ItemPublicationMutex());
                Background::Checkpoint();
                auto accepted=StoreNames(root,rows,stamp);GW2API::AdoptPublicItems(accepted);
            }catch(const Background::Cancelled&){throw;}catch(...){Background::Checkpoint();}
            metadataDone+=count;batch.clear();expected.clear();count=0;publishProgress();
        };
        for(auto id:ids){Background::Checkpoint();if(count==100)flush();if(count++)batch+=",";batch+=std::to_string(id);expected.insert(id);}flush();
        Publish("Refresh finished. Check individual storage status for failures or stale data.");
    });
}
std::string Display(const Account& a){return a.label.empty()?a.name:a.label;}
std::string Active(const View& v,const std::string& ch){
    if(ch.empty())return {};std::string result;
    for(const auto& a:v.accounts){auto it=v.inventory.rosters.find(a.id);
        if(it!=v.inventory.rosters.end()&&it->second.count(ch)){if(!result.empty())return {};result=a.id;}}
    return result;
}
}
std::vector<LinkedAccount> Linked(){
    auto v=Snapshot();std::vector<LinkedAccount> out;const auto gen=v->credentialEpoch;
    for(const auto& a:v->accounts){
        LinkedAccount row;row.id=a.id;row.name=a.name;row.label=a.label;row.permissions=a.permissions;row.generation=gen;
        const auto it=v->inventory.rosters.find(a.id);
        if(it!=v->inventory.rosters.end()){row.rosterKnown=true;row.characters=it->second;}
        out.push_back(std::move(row));
    }
    return out;
}
LinkedAccount FindLinked(const std::string& id){
    for(auto& a:Linked())if(a.id==id)return std::move(a);
    return {};
}
bool Ready(){return registryReady.load();}
bool Current(const LinkedAccount& a){
    if(a.id.empty()||a.generation!=registryGeneration.load())return false;
    auto v=Snapshot();
    return v->credentialEpoch==a.generation&&std::any_of(v->accounts.begin(),v->accounts.end(),[&](const Account& b){return b.id==a.id;});
}
bool CommitIfCurrent(const LinkedAccount& a,const std::function<bool()>& commit){
    return AccountCommit::Run(credentialMutex,[&]{return !Background::Workers().Stopping()&&Current(a);},commit);
}
bool CommitIfCurrent(const std::vector<LinkedAccount>& accounts,const std::function<bool()>& commit){
    return AccountCommit::Run(credentialMutex,[&]{
        return !Background::Workers().Stopping()&&!accounts.empty()&&
            std::all_of(accounts.begin(),accounts.end(),[](const auto& a){return Current(a);});
    },commit);
}
bool CommitIfRosterCurrent(const std::vector<LinkedAccount>& accounts,const std::map<std::string,std::set<std::string>>& roster,const std::function<bool()>& commit){
    return CommitIfCurrent(accounts,[&]{
        for(const auto& [account,names]:roster){
            auto current=ReadRoster(root,account);
            if(!current||*current!=names)return false;
        }
        return commit();
    });
}
bool CommitIfCharacterCurrent(const LinkedAccount& account,const std::string& character,const std::function<bool()>& commit){
    return CommitIfCurrent(account,[&]{
        auto names=ReadRoster(root,account.id);
        return (!names||names->count(character))&&commit();
    });
}
Result Request(const LinkedAccount& a,const std::string& path,const std::vector<std::string>& permissions,const std::string& headers){
    if(!Current(a))return {0,{},"Account changed"};
    for(const auto& p:permissions)if(!a.permissions.count(p))return {403,{},"Missing API permission: "+p};
    std::unique_lock<std::mutex> lock(credentialMutex);
    if(!Current(a))return {0,{},"Account changed"};
    Secret key(Key(a.id));lock.unlock();
    if(key.value.empty())return {401,{},"API key unavailable"};
    auto r=ArenaNet::Get(path,key.value,headers);
    if(Background::Workers().Stopping())return {0,{},"Cancelled"};
    if(!Current(a))return {0,{},"Account changed"};
    if(r.status_code!=200)return {r.status_code,{},Error(r.status_code)};
    try{
        auto parsed=Untrusted::Parse(r.body,Untrusted::PublicApi);
        if(Background::Workers().Stopping())return {0,{},"Cancelled"};
        if(!Current(a))return {0,{},"Account changed"};
        return {200,std::move(parsed),{}};
    }catch(...){return {0,{},"Invalid API response"};}
}
Result Request(const std::string& id,const std::string& path,const std::vector<std::string>& permissions){
    for(const auto& a:Linked())if(a.id==id)return Request(a,path,permissions);
    return {401,{},"No linked account"};
}
PhysicalEquipmentResult RequestPhysicalEquipment(const LinkedAccount& a,const std::string& character,bool manual){
    using Clock=std::chrono::steady_clock;
    const auto queued=Clock::now();
    std::unique_lock<std::mutex> lock(physicalMutex,std::defer_lock);
    while(!lock.try_lock()){if(Background::Workers().Stopping())return {{0,{},"Cancelled"},0,0};std::this_thread::sleep_for(std::chrono::milliseconds(25));}
    if(Background::Workers().Stopping()||!Current(a))return {{0,{},"Account changed"},0,0};
    if(!CommitIfCharacterCurrent(a,character,[]{return true;}))return {{0,{},"Character roster changed"},0,0};
    const CharacterActivity::Key k{a.id,character};
    auto& e=physicalCache[k];
    if(e.epoch==a.generation&&e.value.response&&e.value.inventoryCommitted&&
       (e.finished>=queued||(!manual&&Clock::now()-e.finished<std::chrono::seconds(15))))return e.value;
    if(Background::Workers().Stopping())return {{0,{},"Cancelled"},0,0};
    CharacterActivity::CancelOnShutdown attempts(k);
    const auto ticket=attempts.BeginScope("Equipment");
    const auto cancel=[&](const char* reason){
        CharacterActivity::Cancel(k,"Equipment",ticket);
        return PhysicalEquipmentResult{{0,{},reason},0,0};
    };
    auto r=Request(a,"/v2/characters/"+Encode(character)+"/equipment?v=2024-04-01T00:00:00.000Z",{"characters","inventories"});
    PhysicalEquipmentResult result{std::move(r),ticket.started,Now()};
    // Parse before the credential gate, then publish both outcomes while the
    // physical owner is still held. Consumers never record a delayed failure.
    std::vector<Slot> slots;
    bool complete=false;
    if(result.response){
        try{slots=ParseEquipment(result.response.data,a.id,character);complete=true;}catch(...){}
    }
    if(Background::Workers().Stopping())return cancel("Cancelled");
    try{
        const bool accepted=CommitIfCharacterCurrent(a,character,[&]{
            ScopeState state{a.id,"Equipment",character,complete?"Ready":
                (result.response?"Invalid or incomplete response":result.response.error),ticket.started,complete?result.success:0};
            if(!Record(root,state,complete?&slots:nullptr))return false;
            if(complete){
                CharacterActivity::Success(k,"Equipment",ticket,result.success);
                result.inventoryCommitted=true;
            }
            Publish("",false);return true;
        });
        if(!accepted)return cancel("Account or character roster changed");
    }catch(...){
        // Preserve the raw response for the character supplement consumer;
        // only a committed physical projection advances Inventory freshness.
        if(Background::Workers().Stopping())return cancel("Cancelled");
        if(!Current(a))return cancel("Account changed");
    }
    if(result.inventoryCommitted)e={a.generation,result,Clock::now()};
    else physicalCache.erase(k);
    return result;
}
bool RefreshRoster(const LinkedAccount& a,std::set<std::string>& names){
    auto result=FetchRoster(a);
    if(!result.accepted||result.state.status!="Ready")return false;
    names=std::move(result.names);return true;
}
std::vector<Location> Locations(uint32_t id){
    auto v=Snapshot();std::vector<Location> out;
    const auto it=v->index.find(id);if(it==v->index.end())return out;
    for(auto n:it->second){const auto& row=v->inventory.rows[n];
        const auto& s=row.slot;
        for(const auto& a:v->accounts)if(a.id==s.account){
            out.push_back({a.id,s.scope,s.character,s.position,BindingName(s.binding),s.count});break;
        }
    }
    return out;
}
uint64_t InventoryGeneration(){return inventoryGeneration.load();}
int64_t LooseCount(uint32_t id,const std::string& account,const std::string& character,int64_t stat){
    auto v=Snapshot();if(account.empty()||std::none_of(v->accounts.begin(),v->accounts.end(),[&](const Account& a){return a.id==account;}))return 0;
    const auto it=v->index.find(id);if(it==v->index.end())return 0;
    auto flags=v->inventory.flags.find(id);const auto f=flags==v->inventory.flags.end()?ItemFlags{}:flags->second;
    int64_t count=0;
    for(auto i:it->second){const auto& r=v->inventory.rows[i];if(UsableLoose(r.slot,account,character,f,Fresh(*v,r),stat))count+=r.slot.count;}
    return count;
}
void RevealItem(uint32_t id){revealItem=id;}
void RenderItemAvailability(uint32_t id,const std::string& account,const std::string& character,int64_t stat){
    auto v=Snapshot();if(account.empty()){ImGui::TextDisabled("Target account not verified");return;}
    const auto it=v->index.find(id);if(it==v->index.end()){ImGui::TextDisabled("No cached locations (not proof of absence)");return;}
    const auto f=v->inventory.flags.count(id)?v->inventory.flags.at(id):ItemFlags{};
    for(auto i:it->second){
        const auto& r=v->inventory.rows[i];const auto& s=r.slot;
        const auto a=std::find_if(v->accounts.begin(),v->accounts.end(),[&](const Account& a){return a.id==s.account;});
        if(a==v->accounts.end())continue;
        if(v->accounts.size()==1&&s.account!=account)continue;
        std::string label=LocationLabel(s);
        if(v->accounts.size()>1)label=Display(*a)+" / "+label;
        std::string status;
        if(!Fresh(*v,r))status="Stale or failed";
        else if(UsableLoose(s,account,character,f,true,stat))status="In loose stock";
        else if(s.account!=account&&s.binding==Binding::Unbound&&f.known&&!f.accountBound&&!f.soulbound&&s.scope!="Equipment"&&s.scope!="Delivery box")status="Potentially transferable";
        else if(s.scope=="Legendary Armory")status="Armory ownership; check eligibility";
        else if(s.scope=="Equipment")status="Equipped elsewhere / verify reuse";
        else if(s.binding==Binding::Account||s.binding==Binding::Character)status="Restricted or stat mismatch";
        else status="Accessibility unverified";
        ImGui::TextWrapped("%s x%lld - %s",label.c_str(),static_cast<long long>(s.count),status.c_str());
    }
}
void Initialize(const std::string& directory){
    root=directory;registryReady=false;Launch([]{try{Publish("");}catch(...){registryReady=true;throw;}});
}
void Shutdown(){
    // Called only after managed workers join.
    registryReady=false;++registryGeneration;physicalCache.clear();rosterCache.clear();
    std::lock_guard<std::mutex> lock(viewMutex);view=std::make_shared<View>();message.clear();root.clear();
}
bool HasLinkedAccounts(){return !Snapshot()->accounts.empty();}
std::string VerifiedAccountForCharacter(const std::string& character){return Active(*Snapshot(),character);}
namespace {
bool EnabledAction(const char* label,bool enabled,UI::ButtonRole role=UI::ButtonRole::Standard){
    if(!enabled){ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true);ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*.5f);}
    const bool clicked=UI::Button(label,ImVec2(0,0),role)&&enabled;
    if(!enabled){ImGui::PopStyleVar();ImGui::PopItemFlag();}
    return clicked;
}
std::vector<Account> SelectedAccounts(const View& v,const std::string& account){
    std::vector<Account> out;
    for(const auto& a:v.accounts)if(account.empty()||v.accounts.size()==1||a.id==account)out.push_back(a);
    return out;
}
void ItemMenu(uint32_t id,const std::string& name,const std::string& chat,const std::map<std::pair<std::string,std::string>,int64_t>& targets={},bool currency=false){
    if(!ImGui::BeginPopupContextItem("##inventory_item_actions"))return;
    ImGui::TextDisabled("%s",name.c_str());ImGui::Separator();
    CopyNameMenuItem(name);
    if(!currency&&ImGui::MenuItem("Copy Item Chat Link"))CopyToClipboard(chat.empty()?AlterEgo::ChatLink::EncodeItem(id):chat);
    if(ImGui::MenuItem("Open Wiki Page")){
        auto url="https://wiki.guildwars2.com/wiki/Special:Search/"+Encode(name);
        ShellExecuteA(nullptr,"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    }
    if(!targets.empty()){
        ImGui::Separator();
        if(targets.size()==1){
            const auto& [key,count]=*targets.begin();(void)count;
            RenderCharacterLoadMenu(("Load Character - "+key.second).c_str(),key.second,key.first);
        }else if(ImGui::BeginMenu("Load Character")){
            for(const auto& [key,count]:targets){
                ImGui::PushID(key.first.c_str());ImGui::PushID(key.second.c_str());
                RenderCharacterLoadMenu((key.second+" x"+std::to_string(count)).c_str(),key.second,key.first);
                ImGui::PopID();ImGui::PopID();
            }
            ImGui::EndMenu();
        }
    }
    ImGui::EndPopup();
}
void AddFacetChips(UI::FilterChips& chips,const std::string& key,std::set<std::string>& values){
    auto* target=&values;
    for(const auto& value:values)chips.push_back({key+"/"+value,value,[target,value]{target->erase(value);}});
}
std::vector<std::pair<std::string,std::string>> Choices(const std::set<std::string>& values){
    std::vector<std::pair<std::string,std::string>> out;
    for(const auto& value:values)out.emplace_back(value,value);
    return out;
}
bool MatchesFacet(const std::set<std::string>& facet,const std::string& value){return facet.empty()||facet.count(value)!=0;}
}
void RenderOptions(){
    auto v=Snapshot();
    RenderSectionHeader("Account Management",UI::SectionGold);
    ImGui::TextWrapped("Create a GW2 API key with: Account, Characters, Inventories, Builds, Progression, Unlocks, Wallet, Guilds, Trading Post. PvP is not needed.");
    if(RenderActionButton("Create API key"))ShellExecuteA(nullptr,"open","https://account.guildwars2.com/applications",nullptr,nullptr,SW_SHOWNORMAL);
    static char key[256]{},label[128]{};static std::string editing,removing;
    if(!editing.empty())ImGui::TextUnformatted("Editing linked account");
    UI::InputText("Label (optional)##native_label",label,sizeof(label));
    UI::InputText("API key##native_key",key,sizeof(key),ImGuiInputTextFlags_Password);
    if(EnabledAction(editing.empty()?"Validate & link":"Validate & replace key",!busy.load()&&key[0])){
        auto secret=std::make_shared<Secret>(std::string(key));SecureZeroMemory(key,sizeof(key));
        Link(secret,label,editing);editing.clear();label[0]=0;
    }
    if(!editing.empty()){ImGui::SameLine();if(RenderActionButton("Cancel edit")){editing.clear();label[0]=0;SecureZeroMemory(key,sizeof(key));}}
    for(const auto& a:v->accounts){
        ImGui::PushID(a.id.c_str());ImGui::Separator();ImGui::TextUnformatted(Display(a).c_str());
        if(!a.label.empty())ImGui::TextDisabled("%s",a.name.c_str());
        std::string missing;for(const auto& p:recommended)if(!a.permissions.count(p)){if(!missing.empty())missing+=", ";missing+=p;}
        if(missing.empty())ImGui::TextUnformatted("All recommended permissions granted");
        else ImGui::TextWrapped("Missing: %s",missing.c_str());
        if(EnabledAction("Edit",!busy.load())){editing=a.id;snprintf(label,sizeof(label),"%s",a.label.c_str());SecureZeroMemory(key,sizeof(key));}
        ImGui::SameLine();if(EnabledAction("Unlink",!busy.load(),UI::ButtonRole::Destructive)){removing=a.id;ImGui::OpenPopup("Unlink account?");}
        if(ImGui::BeginPopupModal("Unlink account?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextWrapped("Remove the linked account and its stored credential? Saved Library data is unaffected.");
            if(EnabledAction("Unlink account",!busy.load(),UI::ButtonRole::Destructive)){Remove(removing);ImGui::CloseCurrentPopup();}
            ImGui::SameLine();if(RenderActionButton("Cancel"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    std::string status;RefreshUI::State statusState;
    {std::lock_guard<std::mutex> lock(viewMutex);status=message;statusState=messageState;}
    if(busy.load())RefreshUI::Status(RefreshUI::State::Working,"Working...");
    else if(!status.empty()){
        ImGui::PushTextWrapPos(0.0f);
        RefreshUI::Status(statusState,status);
        ImGui::PopTextWrapPos();
    }
    ImGui::TextDisabled("Keys are stored in Windows Credential Manager, not exported with the Library.");
}
std::set<uint32_t> WalletPins(){return walletPins;}
void SetWalletPins(const std::set<uint32_t>& ids){walletPins=ids;}
InventoryNavigation CaptureInventoryNavigation(){return navigation;}
void RestoreInventoryNavigation(const InventoryNavigation& state){navigation=state;revealItem=0;groupRevealPending=state.grouped&&state.item!=0;}
int InventorySubview(){return navigation.subview;}
void SetInventorySubview(int value){if(value>=0&&value<=2)navigation.subview=value;}
bool InventoryGrouped(){return navigation.grouped;}
void SetInventoryGrouped(bool value){
    if(navigation.grouped!=value){navigation.grouped=value;navigation.group.clear();groupRevealPending=value&&navigation.item!=0;}
}
std::set<std::string> InventoryExpandedGroups(){return expandedInventoryGroups;}
void SetInventoryExpandedGroups(const std::set<std::string>& value){
    expandedInventoryGroups.clear();
    for(const auto& key:value)if(!key.empty()&&key.size()<=1024&&expandedInventoryGroups.size()<4096)expandedInventoryGroups.insert(key);
}
std::string RevealAccount(uint32_t id,const std::string& selectedAccount){
    auto v=Snapshot();if(v->accounts.size()<2||selectedAccount.empty())return selectedAccount;
    auto it=v->index.find(id);if(it==v->index.end())return selectedAccount;
    bool selectedHas=false,otherHas=false;
    for(auto n:it->second){
        const auto& owner=v->inventory.rows[n].slot.account;
        if(owner==selectedAccount)selectedHas=true;
        else if(std::any_of(v->accounts.begin(),v->accounts.end(),[&](const Account& a){return a.id==owner;}))otherHas=true;
    }
    return !selectedHas&&otherHas?std::string{}:selectedAccount;
}
static void PollDemand(const std::string& selectedAccount,const std::string& requested,bool includeWallet=false){
    if(!AutoRefreshPolicy::Enabled()||!Ready()||busy.load())return;
    static auto last=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    if(now-last<std::chrono::seconds(5))return;
    auto v=Snapshot();auto active=CharacterActivity::ActiveKey();
    auto selected=selectedAccount;
    if(selected.empty()&&v->accounts.size()>1){selected=active.first;if(selected.empty())return;}
    const auto accounts=SelectedAccounts(*v,selected);
    for(const auto& a:accounts){
        auto run=[&](const std::string& scope,const std::string& ch={}){
            if(!ScopeDue(*v,a.id,scope,ch))return false;
            last=now;Refresh({a},v->credentialEpoch,scope=="Guild access"?"Guild bank":scope,true,ch);return true;
        };
        if(requested=="Wallet"){if(run("Wallet"))return;continue;}
        if(requested=="Guild bank"){if(run("Guild access"))return;continue;}
        // Items now displays cached wallet balances; apply its existing independent policy.
        if(includeWallet&&requested=="Items"&&run("Wallet"))return;
        const bool items=requested=="Items";
        const bool bags=items||requested=="Bags",equipment=items||requested=="Equipment";
        auto roster=v->inventory.rosters.find(a.id);
        if((bags||equipment)&&roster==v->inventory.rosters.end()&&run("Roster"))return;
        if((bags||equipment)&&roster!=v->inventory.rosters.end()){
            if(active.first==a.id&&roster->second.count(active.second)){
                if(bags&&run("Bags",active.second))return;
                if(equipment&&run("Equipment",active.second))return;
            }
        }
        for(const auto* s:{"Bank","Shared inventory","Materials","Legendary Armory","Delivery box"})
            if((items||requested==s)&&run(s))return;
        if((bags||equipment)&&roster!=v->inventory.rosters.end()){
            for(const auto& ch:roster->second){
                if(active.first==a.id&&ch==active.second)continue;
                if(bags&&run("Bags",ch))return;
                if(equipment&&run("Equipment",ch))return;
            }
        }
        if((bags||equipment)&&run("Roster"))return;
    }
    last=now;
}
void PollItemDemand(const std::string& selectedAccount){PollDemand(selectedAccount,"Items");}
void RenderInventory(const std::string& selectedAccount,const std::function<void()>& saveSettings,float& listWidth){
    auto v=Snapshot();
    if(!Ready()){RenderSpinner("Loading linked accounts...");return;}
    if(v->accounts.empty()){ImGui::TextDisabled("Link an account in Nexus > Alter Ego options to browse Inventory.");return;}
    auto& nav=navigation;
    auto& query=nav.query;auto& scope=nav.scope;auto& binding=nav.binding;
    auto& rarity=nav.rarity;auto& type=nav.type;auto& characterFilter=nav.characters;
    auto& subview=nav.subview;auto& sort=nav.sort;auto& freshness=nav.freshness;
    auto& minQuantity=nav.minQuantity;auto& transfer=nav.transfer;
    if(revealItem){
        nav.item=revealItem;nav.currency=false;nav.group.clear();groupRevealPending=nav.grouped;query=std::to_string(revealItem);revealItem=0;
        scope.clear();binding.clear();rarity.clear();type.clear();characterFilter.clear();
        freshness=minQuantity=transfer=0;subview=0;
    }
    const bool multi=v->accounts.size()>1,all=multi&&selectedAccount.empty();
    const auto accounts=SelectedAccounts(*v,selectedAccount);
    const auto allowed=[&](const std::string& id){return std::any_of(accounts.begin(),accounts.end(),[&](const Account& a){return a.id==id;});};
    if(accounts.empty()){ImGui::TextDisabled("Select a linked account to browse.");return;}
    const char* names[]={"Items","Wallet","Guild Bank"};
    RenderSectionHeader(names[subview],UI::SectionGold);
    PollDemand(selectedAccount,subview==1?"Wallet":subview==2?"Guild bank":"Items",subview==0);
    if(subview!=0){
        auto& q=subview==1?nav.walletQuery:nav.guildQuery;
        auto& pinFilter=nav.pinFilter;auto& balanceFilter=nav.balanceFilter;
        if(subview==1){
            UI::SearchToolbar("native_wallet","Search currencies or ID",q,
                [&]{
                    UI::FilterChips chips;
                    if(pinFilter)chips.push_back({"pins","Pinned only",[&]{pinFilter=0;}});
                    if(balanceFilter)chips.push_back({"balance","Hide zero",[&]{balanceFilter=0;}});
                    return chips;
                },
                [&]{
                    const char* pinOptions[]={"All","Pinned only"};
                    const char* balanceOptions[]={"All","Nonzero only"};
                    UI::FilterChoice("Pinned",pinFilter,pinOptions,2);
                    UI::FilterChoice("Balance",balanceFilter,balanceOptions,2);
                },
                [&]{pinFilter=balanceFilter=0;});
        }else{
            // Plain searchable list, no filter popup: same SearchField used by Characters.
            UI::SearchField("guild_search","Search items or ID",q);
        }
        if(subview==1){
            for(const auto& a:accounts){
                if(multi&&all)ImGui::TextUnformatted(Display(a).c_str());
                // Retrieval status belongs exclusively to the shared footer,
                // including per-account failures and stale timestamps in its tooltip.
                ImGui::PushID(a.id.c_str());
                std::vector<const WalletRow*> visible;
                for(const auto& w:v->inventory.wallet)if(w.account==a.id&&Match(w.name,w.id,q)&&(!pinFilter||walletPins.count(w.id))&&(!balanceFilter||w.value>0))visible.push_back(&w);
                std::stable_sort(visible.begin(),visible.end(),[&](const WalletRow* a,const WalletRow* b){
                    const bool fa=walletPins.count(a->id),fb=walletPins.count(b->id);
                    if(fa!=fb)return fa;
                    return a->order==b->order?a->id<b->id:a->order<b->order;
                });
                int rowIndex=0;
                for(const auto* ptr:visible){
                    const auto& w=*ptr;ImGui::PushID(static_cast<int>(w.id));
                    const auto p=ImGui::GetCursorScreenPos();
                    UI::CatalogRowBackground(rowIndex++,p,ImVec2(p.x+ImGui::GetContentRegionAvail().x,p.y+ImGui::GetFrameHeight()));
                    const bool pinned=walletPins.count(w.id)!=0;
                    if(UI::PinButton("##currency_pin",pinned,"Pin currency","Unpin currency")){
                        if(pinned)walletPins.erase(w.id);else walletPins.insert(w.id);
                        if(saveSettings)saveSettings();
                    }
                    ImGui::SameLine();
                    // Reserved currency ID namespace; use the ordinary disk-backed icon queue.
                    const uint32_t iconId=0x40000000u|w.id;
                    const float iconSize=ImGui::GetFrameHeight();
                    if(auto* tex=IconManager::GetIcon(iconId);tex&&tex->Resource)
                        ImGui::Image(tex->Resource,ImVec2(iconSize,iconSize));
                    else{
                        ImGui::Dummy(ImVec2(iconSize,iconSize));
                        if(!w.icon.empty())IconManager::RequestIcon(iconId,w.icon,true);
                    }
                    ImGui::SameLine();ImGui::AlignTextToFramePadding();
                    std::string label=w.name.empty()?"Currency #"+std::to_string(w.id):w.name;
                    if(w.id==1){ImGui::Text("%s:",label.c_str());ImGui::SameLine();RenderCoins(w.value);}
                    else ImGui::Text("%s: %lld",label.c_str(),static_cast<long long>(w.value));
                    if(!w.description.empty()&&ImGui::IsItemHovered())ImGui::SetTooltip("%s",w.description.c_str());
                    ImGui::PopID();
                }
                if(visible.empty())ImGui::TextDisabled("No matching cached currencies.");
                ImGui::PopID();
            }
        }else{
            ImGui::TextWrapped("Guild bank is shared stock, not personal inventory. ArenaNet requires a guild leader's key.");
            std::set<std::string> shown;
            for(const auto& a:accounts){
                const auto st=v->scopes.find({a.id,"Guild access",""});
                if(st==v->scopes.end()||st->second.status!="Ready"||!a.permissions.count("guilds")){ImGui::TextDisabled("Guild access not verified%s%s",multi&&all?" / ":"",multi&&all?Display(a).c_str():"");continue;}
                auto access=v->inventory.guildAccess.find(a.id);
                if(access!=v->inventory.guildAccess.end())shown.insert(access->second.begin(),access->second.end());
            }
            for(const auto& id:shown){
                auto it=v->inventory.guilds.find(id);if(it==v->inventory.guilds.end())continue;
                const auto& g=it->second;ImGui::PushID(id.c_str());
                auto title=(g.name.empty()?id:g.name)+" ["+g.tag+"]";
                if(ImGui::TreeNode((title+"###guild").c_str())){
                    ImGui::TextDisabled("%s%s",g.status.c_str(),Due("Guild bank",0,g.success,Now())?" (stale)":"");
                    ImGui::TextUnformatted("Shared coins:");ImGui::SameLine();RenderCoins(g.coins);
                    std::map<uint32_t,std::pair<const Row*,int64_t>> groups;
                    for(const auto& r:g.rows)if(Match(r.name,r.slot.id,q)){auto& x=groups[r.slot.id];x.first=&r;x.second+=r.slot.count;}
                    for(const auto& [item,x]:groups){
                        ImGui::PushID(static_cast<int>(item));
                        const auto& row=*x.first;
                        const auto name=row.name.empty()?"Item #"+std::to_string(item):row.name;
                        ImGui::TextColored(GetSkinRarityColor(row.rarity),"%s x%lld",name.c_str(),static_cast<long long>(x.second));
                        ItemMenu(item,name,row.chat);ImGui::PopID();
                    }
                    if(groups.empty())ImGui::TextDisabled("No matching cached items.");
                    ImGui::TreePop();
                }ImGui::PopID();
            }
            if(shown.empty())ImGui::TextDisabled("No verified guild bank access.");
        }
        return;
    }
    const UI::SplitPanel split(listWidth,420.f,200.f,240.f);
    ImGui::BeginChild("InventoryList",ImVec2(split.left,0),true);
    const auto previousQuery=query;
    UI::SearchToolbar("native_inventory","Search items, currencies or ID",query,
        [&]{
            UI::FilterChips c;
            AddFacetChips(c,"character",characterFilter);
            if(!scope.empty())c.push_back({"scope",scope,[&]{scope.clear();}});
            AddFacetChips(c,"binding",binding);AddFacetChips(c,"rarity",rarity);
            AddFacetChips(c,"type",type);
            if(freshness)c.push_back({"freshness",freshness==1?"Fresh":"Stale / failed",[&]{freshness=0;}});
            if(minQuantity)c.push_back({"quantity","Min "+std::to_string(minQuantity),[&]{minQuantity=0;}});
            if(transfer)c.push_back({"transfer","Potentially transferable",[&]{transfer=0;}});
            return c;
        },
        [&]{
            std::set<std::string> names=characterFilter;
            for(const auto& a:accounts){
                auto it=v->inventory.rosters.find(a.id);
                if(it!=v->inventory.rosters.end())names.insert(it->second.begin(),it->second.end());
            }
            RenderCharacterMultiFilter("Character",characterFilter,names);
            UI::FilterChoice("Storage",scope,{"Bank","Shared inventory","Materials","Bags","Equipment","Legendary Armory","Delivery box","Wallet"});
            UI::MultiFilterChoice("Binding",binding,Choices({"Unknown","Unbound","Account bound","Character bound"}));
            UI::MultiFilterChoice("Rarity",rarity,{{"Junk","Junk"},{"Basic","Basic"},{"Fine","Fine"},{"Masterwork","Masterwork"},{"Rare","Rare"},{"Exotic","Exotic"},{"Ascended","Ascended"},{"Legendary","Legendary"}});
            std::set<std::string> types=type;types.insert("Currency");
            for(const auto& r:v->inventory.rows)if(allowed(r.slot.account)&&!r.type.empty())types.insert(r.type);
            UI::MultiFilterChoice("Type",type,Choices(types));
            const char* freshOptions[]={"All","Fresh","Stale / failed"};
            UI::FilterChoice("Freshness",freshness,freshOptions,3);
            UI::FilterInt("Min quantity",minQuantity);if(minQuantity<0)minQuantity=0;
            const char* transferOptions[]={"All","Potentially transferable"};
            UI::FilterChoice("Transferability",transfer,transferOptions,2);
        },
        [&]{scope.clear();binding.clear();rarity.clear();type.clear();characterFilter.clear();freshness=minQuantity=transfer=0;});
    if(query!=previousQuery){nav.item=0;nav.currency=false;nav.group.clear();groupRevealPending=false;}
    const char* sorts[]={"Name","Quantity"};ImGui::SetNextItemWidth(120);UI::Combo("##inventory_sort",&sort,sorts,2);
    auto tokens=Skinventory::Search::Words(query);
    // Explicit Find in Inventory navigation can reveal even an unusually short ID.
    const bool canSearch=Skinventory::Search::HasMinimumQuery(tokens)||
        (nav.item!=0&&query==std::to_string(nav.item));
    const bool canBrowse=CanBrowseItems(!characterFilter.empty(),!scope.empty(),
        !binding.empty(),!rarity.empty(),!type.empty(),freshness!=0,minQuantity>0,transfer!=0);
    const bool walletEligible=WalletMatchesFacets(scope,characterFilter,binding,rarity,type,transfer!=0);
    const bool showResults=nav.grouped||canSearch||canBrowse;
    if(!showResults)ImGui::TextWrapped("Enter at least 3 characters or select a filter to browse inventory.");
    if(!canSearch){
        if(showResults&&!tokens.empty())ImGui::TextDisabled("Enter 3 characters to narrow these results.");
        tokens.clear();
    }
    // Cache the filtered aggregate: rebuild only on a new projection or changed controls.
    struct Group{uint32_t id;int64_t count=0;std::string name;std::vector<const Row*> locations;bool currency=false;std::vector<const WalletRow*> balances;};
    const auto walletFresh=[&](const WalletRow& w){auto it=v->scopes.find({w.account,"Wallet",""});return it!=v->scopes.end()&&it->second.status=="Ready"&&ScopeFresh(w.account,"Wallet",{},it->second.success);};
    const auto currencyName=[](const WalletRow& w){return w.name.empty()?"Currency #"+std::to_string(w.id):w.name;};
    const auto quantityText=[](uint32_t id,bool currency,int64_t count){if(currency&&id==1)return std::to_string(count/10000)+"g "+std::to_string(count/100%100)+"s "+std::to_string(count%100)+"c";return std::to_string(count);};
    static std::shared_ptr<const View> previous;static std::string prevKey;static std::vector<Group> groups;
    static uint64_t projectionRevision=0;
    const auto filterKey=Json::array({showResults,selectedAccount,tokens,scope,binding,rarity,type,sort,characterFilter,freshness,minQuantity,transfer,(freshness||transfer)?Now()/10:0}).dump();
    if(previous!=v||prevKey!=filterKey){
        previous=v;prevKey=filterKey;groups.clear();++projectionRevision;std::map<SearchIdentity,size_t> index;
        if(showResults)for(const auto& row:v->inventory.rows){
            const auto& s=row.slot;
            if(!allowed(s.account)||(!scope.empty()&&scope!=s.scope)||!MatchesFacet(binding,BindingName(s.binding))||!MatchesFacet(rarity,row.rarity)||!MatchesFacet(type,row.type)||!Match(row.name,s.id,tokens))continue;
            if(!MatchesFacet(characterFilter,s.character))continue;
            const bool fresh=Fresh(*v,row);
            if((freshness==1&&!fresh)||(freshness==2&&fresh))continue;
            if(transfer){auto f=v->inventory.flags.find(s.id);if(!fresh||s.binding!=Binding::Unbound||f==v->inventory.flags.end()||f->second.accountBound||f->second.soulbound||s.scope=="Equipment"||s.scope=="Delivery box")continue;}
            const SearchIdentity key{false,s.id};auto it=index.find(key);if(it==index.end()){index.emplace(key,groups.size());groups.push_back({s.id,0,row.name,{}});it=index.find(key);}
            auto& group=groups[it->second];group.count+=s.count;group.locations.push_back(&row);
        }
        if(showResults&&walletEligible)for(const auto& w:v->inventory.wallet){
            if(!allowed(w.account)||!WalletMatchesBalance(w.value,walletFresh(w),freshness)||!Match(currencyName(w),w.id,tokens))continue;
            const SearchIdentity key{true,w.id};
            auto it=index.find(key);
            if(it==index.end()){index.emplace(key,groups.size());groups.push_back({w.id,0,currencyName(w),{},true,{}});it=index.find(key);}
            auto& group=groups[it->second];group.count+=w.value;group.balances.push_back(&w);
        }
        groups.erase(std::remove_if(groups.begin(),groups.end(),[&](const Group& g){return g.count<minQuantity;}),groups.end());
        std::sort(groups.begin(),groups.end(),[&](const Group& a,const Group& b){if(sort==1&&a.count!=b.count)return a.count>b.count;return a.name==b.name?SearchIdentity{a.currency,a.id}<SearchIdentity{b.currency,b.id}:a.name<b.name;});
    }
    const auto metaFor=[&](uint32_t id)->const ItemPresentation*{
        const auto it=v->inventory.metadata.find(id);return it==v->inventory.metadata.end()?nullptr:&it->second;
    };
    const auto loadTargets=[&](uint32_t id){
        std::map<std::pair<std::string,std::string>,int64_t> out;
        const auto it=v->index.find(id);if(it!=v->index.end())for(auto idx:it->second){
            const auto& slot=v->inventory.rows[idx].slot;
            if(allowed(slot.account)&&!slot.character.empty())out[{slot.account,slot.character}]+=slot.count;
        }
        return out;
    };
    if(showResults){
        ImGui::Text("%zu matching results",groups.size());ImGui::Separator();
        if(groups.empty())ImGui::TextDisabled("%s",busy.load()?"Refreshing inventory...":"No matching cached items or currencies.");
    }
    auto selected=std::find_if(groups.begin(),groups.end(),[&](const Group& x){return x.id==nav.item&&x.currency==nav.currency;});
    if(selected==groups.end()){nav.item=0;nav.currency=false;nav.group.clear();groupRevealPending=false;}

    // Derive group buckets from the same filtered aggregate, not a second scan
    // or filter implementation. Rebuild only on a projection/login-order change.
    static GroupTree tree;
    static uint64_t treeProjection=0,treeLoginRevision=0,treeRevision=0;
    static std::map<std::pair<size_t,std::string>,int64_t> groupCounts;
    static std::map<size_t,std::string> firstGroup;
    static std::vector<GroupLine> visibleLines;
    static uint64_t visibleRevision=0;
    static std::set<std::string> visibleExpansion;
    static bool visibleForced=false;
    const bool forceExpand=!tokens.empty();
    if(nav.grouped){
        uint64_t loginRevision=0;
        std::map<CharacterActivity::Key,int64_t> logins;
        {
            std::lock_guard<std::mutex> lock(CharacterActivity::Mutex());
            loginRevision=CharacterActivity::State().LoginRevision();
            if(treeProjection!=projectionRevision||treeLoginRevision!=loginRevision)
                logins=CharacterActivity::State().LoginTimes();
        }
        if(treeProjection!=projectionRevision||treeLoginRevision!=loginRevision){
            treeProjection=projectionRevision;treeLoginRevision=loginRevision;++treeRevision;
            tree.Clear();groupCounts.clear();firstGroup.clear();visibleLines.clear();
            std::map<GroupPath,std::vector<GroupLeaf>> buckets;
            for(size_t i=0;i<groups.size();++i){
                const auto& g=groups[i];std::map<GroupPath,int64_t> counts;
                for(const auto* r:g.locations){const auto& s=r->slot;counts[{s.account,s.character,s.scope}]+=s.count;}
                for(const auto* w:g.balances)counts[{w->account,{},"Wallet"}]+=w->value;
                for(const auto& [path,count]:counts)buckets[path].push_back({i,count});
            }
            std::map<std::string,size_t> accountOrder;
            std::map<std::string,std::string> accountLabels;
            for(size_t i=0;i<accounts.size();++i){accountOrder[accounts[i].id]=i;accountLabels[accounts[i].id]=Display(accounts[i]);}
            std::vector<GroupPath> paths;
            for(const auto& [path,leaves]:buckets)paths.push_back(path);
            const auto login=[&](const GroupPath& p){auto it=logins.find({p.account,p.character});return it==logins.end()?int64_t(0):it->second;};
            std::sort(paths.begin(),paths.end(),[&](const GroupPath& a,const GroupPath& b){
                if(a.account!=b.account)return accountOrder.at(a.account)<accountOrder.at(b.account);
                if(a.character.empty()!=b.character.empty())return a.character.empty();
                if(a.character!=b.character){
                    const auto x=login(a),y=login(b);
                    return x!=y?CharacterActivity::LoginBefore(x,y,true):a.character<b.character;
                }
                const auto ar=StorageRank(a.storage),br=StorageRank(b.storage);
                return ar!=br?ar<br:a.storage<b.storage;
            });
            for(const auto& path:paths){
                auto& leaves=buckets.at(path);
                std::sort(leaves.begin(),leaves.end(),[&](const GroupLeaf& a,const GroupLeaf& b){
                    if(sort==1&&a.count!=b.count)return a.count>b.count;
                    const auto& x=groups[a.item];const auto& y=groups[b.item];
                    return x.name!=y.name?x.name<y.name:SearchIdentity{x.currency,x.id}<SearchIdentity{y.currency,y.id};
                });
                const auto key=GroupKey(path);
                for(const auto& leaf:leaves){groupCounts[{leaf.item,key}]=leaf.count;firstGroup.emplace(leaf.item,key);}
                tree.Add(path,accountLabels.at(path.account),all,std::move(leaves));
            }
        }
        if(nav.item){
            const size_t i=static_cast<size_t>(selected-groups.begin());
            if(!groupCounts.count({i,nav.group})){
                const auto it=firstGroup.find(i);nav.group=it==firstGroup.end()?std::string{}:it->second;
            }
            // A search reveals matching branches without changing saved expansion.
            if(groupRevealPending&&!forceExpand&&!nav.group.empty())tree.Expand(nav.group,expandedInventoryGroups);
        }
        if(visibleRevision!=treeRevision||visibleExpansion!=expandedInventoryGroups||visibleForced!=forceExpand){
            visibleRevision=treeRevision;visibleExpansion=expandedInventoryGroups;visibleForced=forceExpand;
            visibleLines=tree.Flatten(expandedInventoryGroups,forceExpand);
        }
    }
    ImGui::BeginChild("InventoryListScroll",ImVec2(0,0),false);
    if(nav.grouped&&groupRevealPending&&nav.item){
        const size_t i=static_cast<size_t>(selected-groups.begin());
        for(size_t n=0;n<visibleLines.size();++n){
            const auto& line=visibleLines[n];
            if(line.leaf&&line.leaf->item==i&&line.node->key==nav.group){UI::CatalogScrollToRow(static_cast<int>(n),UI::CatalogRowHeight());break;}
        }
    }
    groupRevealPending=false;
    const auto renderItem=[&](int rowIndex,const Group& group,int64_t count,const std::string& path){
        const bool on=nav.currency==group.currency&&nav.item==group.id&&(!nav.grouped||nav.group==path);
        const auto select=[&](bool clicked){
            if(clicked&&!on){MarkInventoryNavigation();nav.item=group.id;nav.currency=group.currency;nav.group=path;}
        };
        if(group.currency){
            const auto& w=*group.balances.front();
            ImGui::PushID("currency");ImGui::PushID(static_cast<int>(group.id));
            auto hit=UI::CatalogRow(rowIndex,on,0x40000000u|group.id,w.icon,group.name,"",quantityText(group.id,true,count));
            select(hit.clicked);ItemMenu(group.id,group.name,{}, {},true);
            ImGui::PopID();ImGui::PopID();return;
        }
        const auto& row=*group.locations.front();
        const auto name=group.name.empty()?"Item #"+std::to_string(group.id):group.name;
        const auto* meta=metaFor(group.id);
        ImGui::PushID(static_cast<int>(group.id));
        auto hit=UI::CatalogRow(rowIndex,on,group.id,meta?meta->icon:"",name,row.rarity,std::to_string(count));
        select(hit.clicked);
        const bool popup=ImGui::IsPopupOpen("##inventory_item_actions")||ImGui::IsItemClicked(ImGuiMouseButton_Right);
        ItemMenu(group.id,name,row.chat,popup?loadTargets(group.id):std::map<std::pair<std::string,std::string>,int64_t>{});
        ImGui::PopID();
    };
    if(!nav.grouped){
        UI::CatalogRows(static_cast<int>(groups.size()),[&](int i){renderItem(i,groups[i],groups[i].count,{});});
    }else{
        const auto characters=GW2API::GetCharacters();
        bool expansionChanged=false;
        UI::CatalogRows(static_cast<int>(visibleLines.size()),[&](int i){
            const auto& line=visibleLines[i];const auto& node=*line.node;
            const float indent=static_cast<float>(line.depth)*14.f;
            if(indent>0)ImGui::Indent(indent);
            ImGui::PushID(node.key.c_str());
            if(line.leaf)renderItem(i,groups[line.leaf->item],line.leaf->count,node.key);
            else{
                std::string profession;
                if(!node.character.empty())for(const auto& c:*characters)
                    if(c.account_name==node.account&&c.name==node.character){profession=c.profession;break;}
                std::function<void(ImDrawList*,ImVec2,ImVec2)> decorate;
                if(!profession.empty())decorate=[&](ImDrawList* d,ImVec2 a,ImVec2 b){DrawInventoryCharacterLabel(d,a,b,profession,node.label);};
                const bool open=forceExpand||expandedInventoryGroups.count(node.key)!=0;
                if(UI::CatalogBranchRow(i,open,node.label,decorate)&&!forceExpand){
                    if(open)expandedInventoryGroups.erase(node.key);
                    else if(expandedInventoryGroups.size()<4096)expandedInventoryGroups.insert(node.key);
                    expansionChanged=true;
                }
            }
            ImGui::PopID();
            if(indent>0)ImGui::Unindent(indent);
        });
        if(expansionChanged)saveSettings();
    }
    ImGui::EndChild();ImGui::EndChild();
    if(split.Divider("##inventory_split",listWidth))saveSettings();
    ImGui::BeginChild("InventoryDetailPanel",ImVec2(0,0),true);
    selected=std::find_if(groups.begin(),groups.end(),[&](const Group& x){return x.id==nav.item&&x.currency==nav.currency;});
    if(selected==groups.end()){ImGui::TextWrapped("Select an item or currency to view details.");ImGui::EndChild();return;}
    const auto& group=*selected;
    const auto groupQuantity=nav.grouped?groupCounts.find({static_cast<size_t>(selected-groups.begin()),nav.group}):groupCounts.end();
    if(group.currency){
        const auto& w=*group.balances.front();
        UI::CatalogBanner(group.name,"Currency","");
        ImGui::PushID("currency_details");ImGui::PushID(static_cast<int>(group.id));
        ItemMenu(group.id,group.name,{}, {},true);
        ImGui::TextDisabled("Currency ID: %u",group.id);
        int64_t total=0;
        for(const auto& balance:v->inventory.wallet)if(balance.id==group.id&&allowed(balance.account)&&balance.value>0)total+=balance.value;
        ImGui::Text("Total in account scope:");ImGui::SameLine();
        if(group.id==1)RenderCoins(total);else ImGui::Text("%lld",static_cast<long long>(total));
        if(total!=group.count)ImGui::TextDisabled("Matching filters: %s",quantityText(group.id,true,group.count).c_str());
        if(groupQuantity!=groupCounts.end())ImGui::TextDisabled("Selected group: %s",quantityText(group.id,true,groupQuantity->second).c_str());
        RenderSectionHeader("Locations",UI::SectionGold);
        for(const auto& balance:v->inventory.wallet){
            if(balance.id!=group.id||!allowed(balance.account)||balance.value<=0)continue;
            ImGui::PushID(balance.account.c_str());
            std::string label="Account Wallet";
            if(multi)for(const auto& a:accounts)if(a.id==balance.account){label=Display(a)+" / "+label;break;}
            ImGui::TextUnformatted(label.c_str());ImGui::SameLine();
            if(group.id==1)RenderCoins(balance.value);else ImGui::Text("%lld",static_cast<long long>(balance.value));
            if(!walletFresh(balance))ImGui::TextDisabled("Stale or failed");
            ImGui::Separator();ImGui::PopID();
        }
        if(!w.description.empty()){
            RenderSectionHeader("Currency Information",UI::SectionGold);
            ImGui::TextWrapped("%s",PlainItemDescription(w.description).c_str());
        }
        ImGui::PopID();ImGui::PopID();ImGui::EndChild();return;
    }
    const auto& representative=*group.locations.front();
    const auto name=group.name.empty()?"Item #"+std::to_string(group.id):group.name;
    const auto* meta=metaFor(group.id);
    UI::CatalogBanner(name,representative.rarity+"  /  "+representative.type,representative.rarity);
    ImGui::PushID(static_cast<int>(group.id));ItemMenu(group.id,name,representative.chat,loadTargets(group.id));ImGui::PopID();
    ImGui::TextDisabled("ID: %u",group.id);
    int64_t total=0;const auto locations=v->index.find(group.id);
    if(locations!=v->index.end())for(auto idx:locations->second)if(allowed(v->inventory.rows[idx].slot.account))total+=v->inventory.rows[idx].slot.count;
    ImGui::Text("Total in account scope: %lld",static_cast<long long>(total));
    if(total!=group.count)ImGui::TextDisabled("Matching filters: %lld",static_cast<long long>(group.count));
    if(groupQuantity!=groupCounts.end())ImGui::TextDisabled("Selected group: %lld",static_cast<long long>(groupQuantity->second));
    RenderSectionHeader("Locations",UI::SectionGold);
    if(locations!=v->index.end())for(auto idx:locations->second){
        const auto& row=v->inventory.rows[idx];const auto& slot=row.slot;if(!allowed(slot.account))continue;
        ImGui::PushID(static_cast<int>(idx));
        auto label=LocationLabel(slot);if(multi)for(const auto& a:v->accounts)if(a.id==slot.account){label=Display(a)+" / "+label;break;}
        label+=" x"+std::to_string(slot.count);
        ImGui::Selectable((label+"###location").c_str(),false);
        if(ImGui::BeginPopupContextItem("##location_actions")){
            if(!slot.character.empty()){
                CopyNameMenuItem(slot.character,"Copy Character Name");
                RenderCharacterLoadMenu("Load Character",slot.character,slot.account);
            }
            CopyNameMenuItem(name);
            ImGui::EndPopup();
        }
        ImGui::TextDisabled("%s%s",BindingName(slot.binding),Fresh(*v,row)?"":" | Stale or failed");
        if(!slot.boundTo.empty())ImGui::TextWrapped("Bound to %s",slot.boundTo.c_str());
        if(slot.charges>=0)ImGui::Text("Charges: %lld",static_cast<long long>(slot.charges));
        if(slot.stat>0)ImGui::TextDisabled("Selected stat ID: %lld",static_cast<long long>(slot.stat));
        const bool glyph=slot.scope=="Equipment"&&(slot.position.find("/Pick")!=std::string::npos||slot.position.find("/Axe")!=std::string::npos||slot.position.find("/Sickle")!=std::string::npos);
        if(glyph&&slot.upgrades.empty())ImGui::TextDisabled("Glyph: Unknown (not reported)");
        for(auto id:slot.upgrades){const auto item = GW2API::GetItemInfo(id);ImGui::TextWrapped("%s: %s",glyph?"Glyph":"Upgrade",item&&!item->name.empty()?item->name.c_str():std::to_string(id).c_str());}
        for(auto id:slot.infusions){const auto item = GW2API::GetItemInfo(id);ImGui::TextWrapped("Infusion: %s",item&&!item->name.empty()?item->name.c_str():std::to_string(id).c_str());}
        ImGui::Separator();ImGui::PopID();
    }
    RenderSectionHeader("Trading Post",UI::SectionGold);
    {
        using Skinventory::Commerce;
        const auto status=Commerce::GetItemPriceStatus(group.id);
        const auto now=Now();
        // A selected-item demand is independent of the inventory search and
        // inventory storage refresh. No result-list or per-row price requests.
        static uint32_t priceDemandId=0;
        static auto priceDemandSince=std::chrono::steady_clock::time_point{};
        static auto priceDemandPoll=std::chrono::steady_clock::time_point{};
        static auto lastVisible=std::chrono::steady_clock::time_point{};
        const auto tick=std::chrono::steady_clock::now();
        if(priceDemandId!=group.id||tick-lastVisible>std::chrono::seconds(2)){
            priceDemandId=group.id;priceDemandSince=tick;priceDemandPoll={};
        }
        lastVisible=tick;
        if(!status.pending&&status.retry_at<=now&&
            (!status.price||RefreshEngine::Due(status.price->fetched_at_unix,0,now,Commerce::PriceTTL))&&
            AutoRefreshPolicy::Enabled(AutoRefreshPolicy::Dataset::TradingPrices)&&
            tick-priceDemandSince>=std::chrono::milliseconds(500)&&
            (priceDemandPoll==std::chrono::steady_clock::time_point{}||tick-priceDemandPoll>=std::chrono::seconds(5))){
            Commerce::RequestItemPrices({group.id});priceDemandPoll=tick;
        }
        if(status.price&&status.price->tradeable){
            const auto& p=*status.price;
            ImGui::TextUnformatted("Buy order:");ImGui::SameLine();
            if(p.buy_quantity>0&&p.buy_price>0)RenderCoins(p.buy_price);else ImGui::TextDisabled("No buy orders");
            ImGui::TextUnformatted("Sell listing:");ImGui::SameLine();
            if(p.sell_quantity>0&&p.sell_price>0)RenderCoins(p.sell_price);else ImGui::TextDisabled("No sell listings");
            SellableQuantity all,matching;
            const auto f=v->inventory.flags.find(group.id);
            if(f!=v->inventory.flags.end()){
                const auto add=[&](SellableQuantity& quantity,const Row& row){
                    quantity.Add(row.slot,f->second,Fresh(*v,row),allowed(row.slot.account));
                };
                if(locations!=v->index.end())for(auto idx:locations->second)
                    add(all,v->inventory.rows[idx]);
                // Reuse the exact rows already selected by the local filter
                // projection; do not reimplement its predicates or use its
                // unqualified total (which can include unsellable stock).
                for(const auto* row:group.locations)add(matching,*row);
            }
            ImGui::TextUnformatted("Estimated instant sale after fees:");
            const auto renderEstimate=[&](const char* label,const SellableQuantity& quantity){
                ImGui::BeginGroup();
                if(quantity.count){
                    const auto count=*quantity.count;
                    ImGui::Text("%s (%lld):",label,static_cast<long long>(count));ImGui::SameLine();
                    const auto net=Skinventory::EstimateInstantSale(count,p.buy_price);
                    if(count==0)ImGui::TextDisabled("No verified sellable quantity");
                    else if(net&&p.buy_quantity>0)RenderCoins(*net);
                    else ImGui::TextDisabled("Estimate unavailable");
                }else{
                    ImGui::Text("%s:",label);ImGui::SameLine();ImGui::TextDisabled("Estimate unavailable");
                }
                ImGui::EndGroup();
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Potentially sellable items in fresh storage at the best cached buy price. Bound and unverified stock excluded. Order depth and actual fees may differ.");
            };
            renderEstimate("All locations",all);
            if(all.count!=matching.count)renderEstimate("Matching filters",matching);
        }else if(status.price)ImGui::TextDisabled("Not tradeable");
        else ImGui::TextDisabled("Price not retrieved");
        if(status.pending)ImGui::TextDisabled("Price request queued or refreshing");
        else if(status.retry_at>now)ImGui::TextDisabled("Price refresh failed; retry in %lldm",static_cast<long long>((status.retry_at-now+59)/60));
        else if(status.price&&(now<status.price->fetched_at_unix||now-status.price->fetched_at_unix>=Commerce::PriceTTL))ImGui::TextDisabled("Price stale");
        if(status.pending){ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true);ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*0.5f);}
        if(UI::Button("Refresh price",ImVec2(0,UI::ActionButtonHeight()))&&!status.pending)Commerce::RequestItemPrices({group.id},true);
        if(status.pending){ImGui::PopStyleVar();ImGui::PopItemFlag();}
        RefreshUI::Timestamp(status.price?static_cast<std::time_t>(status.price->fetched_at_unix):0,"Trading Post price");
    }
    RenderSectionHeader("Item Information",UI::SectionGold);
    auto flags=v->inventory.flags.find(group.id);
    if(flags!=v->inventory.flags.end()){
        if(flags->second.accountBound)ImGui::TextUnformatted("Account bound on acquire");
        if(flags->second.soulbound)ImGui::TextUnformatted("Character bound on acquire");
        if(flags->second.bindOnUse)ImGui::TextUnformatted("Binds on use");
    }
    if(meta&&meta->complete){
        const auto& details=meta->details;
        if(details.contains("type")&&details["type"].is_string())ImGui::TextWrapped("Subtype: %s",details["type"].get<std::string>().c_str());
        if(details.contains("weight_class")&&details["weight_class"].is_string())ImGui::TextWrapped("Weight: %s",details["weight_class"].get<std::string>().c_str());
        if(details.contains("infix_upgrade")&&details["infix_upgrade"].is_object()){
            const auto& infix=details["infix_upgrade"];
            if(infix.contains("attributes")&&infix["attributes"].is_array())for(const auto& attr:infix["attributes"])
                if(attr.is_object()&&attr.contains("attribute")&&attr["attribute"].is_string()&&attr.contains("modifier")&&attr["modifier"].is_number_integer())
                    ImGui::Text("+%lld %s",static_cast<long long>(attr["modifier"].get<int64_t>()),attr["attribute"].get<std::string>().c_str());
        }
        if(!meta->description.empty())ImGui::TextWrapped("%s",PlainItemDescription(meta->description).c_str());
    }else ImGui::TextDisabled("Detailed metadata not retrieved. Refresh inventory to populate it.");
    ImGui::EndChild();
}
void RenderInventoryStatusRow(const std::string& selectedAccount){
    auto v=Snapshot();const auto accounts=SelectedAccounts(*v,selectedAccount);
    const auto& nav=navigation;
    const auto scope=nav.subview==1?std::string("Wallet"):nav.subview==2?std::string("Guild bank"):nav.scope.empty()?std::string("Items"):nav.scope;
    const bool itemView=nav.subview==0;
    const bool characterFiltered=itemView&&!nav.characters.empty();
    const bool includeWallet=itemView&&WalletMatchesFacets(nav.scope,nav.characters,nav.binding,nav.rarity,nav.type,nav.transfer!=0);
    const std::set<std::string> noCharacters;
    const auto& characters=characterFiltered?nav.characters:noCharacters;
    bool missing=false,failed=false,stale=false,needsManual=false,retrying=false;
    std::map<ScopeKey,ScopeState> progress;std::string current;unsigned processed=0;
    {std::lock_guard<std::mutex> lock(viewMutex);progress=liveScopes;current=activeScope;processed=processedScopes;}
    std::time_t oldest=0;unsigned count=0;
    std::string detail;
    const auto note=[&](const std::string& name,int64_t success,const std::string& status,const std::string& ttlScope,bool show=true,const std::string& account={},const std::string& ch={}){
        if(show){if(!detail.empty())detail+="\n";detail+=name+": "+status;}
        if(success<=0)missing=true;
        else{
            if(!oldest||success<oldest)oldest=static_cast<std::time_t>(success);
            if(!ScopeFresh(account,ttlScope,ch,success)){stale=true;if(show)detail+=" (stale or awaiting synchronization)";}
            if(show)detail+="; "+RefreshUI::Age(static_cast<std::time_t>(success));
        }
        if((success<=0||!ScopeFresh(account,ttlScope,ch,success))&&!AutoScope(ttlScope))needsManual=true;
        if(status!="Ready"&&status!="Not retrieved"&&status!="Refreshing"){
            const bool transient=status=="Rate limited; retry later"||status=="Transport failed or cancelled"||
                status=="HTTP 429"||status.rfind("HTTP 5",0)==0;
            if(transient&&AutoScope(ttlScope))retrying=true;else failed=true;
        }
        ++count;
    };
    const auto mark=[&](const Account& a,const std::string& dataset,const std::string& ch={},RefreshProgress::Counts* group=nullptr){
        const ScopeKey key{a.id,dataset,ch};
        auto it=v->scopes.find(key);auto live=progress.find(key);
        const auto* s=live!=progress.end()?&live->second:it!=v->scopes.end()?&it->second:nullptr;
        auto name=(accounts.size()>1?Display(a)+" / ":"")+dataset+(ch.empty()?"":" / "+ch);
        const auto status=s?s->status:"Not retrieved";const auto success=s?s->success:0;
        note(name,success,status,dataset,group==nullptr,a.id,ch);
        if(group)group->Add(status=="Ready"&&success>0&&!ScopeFresh(a.id,dataset,ch,success)?"Stale":status,success>0);
        if(!group&&ActivityScope(dataset)){
            namespace A=CharacterActivity;
            A::Inspection inspection;
            A::Eligibility eligible;
            const auto now=Now();
            {
                std::lock_guard<std::mutex> lock(A::Mutex());
                const bool spec=dataset=="Equipment"&&A::SpecSignal().load();
                inspection=A::State().Inspect({a.id,ch},dataset,success,now,spec);
                eligible=A::State().Eligible({a.id,ch},dataset,success,s&&status!="Ready"?s->attempted:0,now,spec);
            }
            detail+="; "+std::string(inspection.active?"active":"not active");
            if(!AutoScope(dataset))detail+="; auto refresh disabled";
            else if(eligible.due)detail+="; refresh due";
            else if(eligible.at>now){
                const auto remaining=eligible.at-now;
                using W=A::WaitReason;
                const char* reason=eligible.reason==W::Retry?"retry cooldown":
                    eligible.reason==W::LoginGrace?"login grace":eligible.reason==W::LogoutGrace?"logout grace":
                    eligible.reason==W::SpecDebounce?"specialization change":eligible.reason==W::Stagger?"staggered refresh":"active interval";
                detail+="; "+std::string(reason)+" "+std::to_string(remaining/60)+"m "+std::to_string(remaining%60)+"s";
            }else if(inspection.fresh&&!inspection.active)detail+="; no unverified observed activity";
        }
    };
    std::set<std::string> guilds;
    for(const auto& a:accounts){
        if(scope=="Wallet"){if(!itemView||includeWallet)mark(a,"Wallet");continue;}
        if(scope=="Items"&&includeWallet)mark(a,"Wallet");
        if(scope=="Guild bank"){
            mark(a,"Guild access");auto access=v->inventory.guildAccess.find(a.id);
            if(access!=v->inventory.guildAccess.end())guilds.insert(access->second.begin(),access->second.end());
            continue;
        }
        for(const auto* dataset:{"Bank","Shared inventory","Materials","Legendary Armory","Delivery box"})
            if(MatchesStorageView(scope,characters,dataset,{}))mark(a,dataset);
        if(scope=="Items"||scope=="Bags"||scope=="Equipment"){
            auto roster=v->inventory.rosters.find(a.id);
            // Roster is a prerequisite, not a bag/equipment retrieval.
            if(roster==v->inventory.rosters.end()){
                mark(a,"Roster");missing=true;continue;
            }
            for(const auto* dataset:{"Bags","Equipment"}){
                if(scope!="Items"&&scope!=dataset)continue;
                RefreshProgress::Counts group;
                for(const auto& ch:roster->second)
                    if(MatchesStorageView(scope,characters,dataset,ch))
                        mark(a,dataset,ch,characterFiltered?nullptr:&group);
                if(!characterFiltered){
                    if(!detail.empty())detail+="\n";
                    detail+=(accounts.size()>1?Display(a)+" / ":"")+dataset+": "+group.Summary();
                }
            }
        }
    }
    for(const auto& id:guilds){
        auto it=v->inventory.guilds.find(id);
        if(it==v->inventory.guilds.end())note(id,0,"Not retrieved","Guild bank");
        else note(it->second.name.empty()?id:it->second.name,it->second.success,it->second.status,"Guild bank");
    }
    // A scoped aggregate is only fully retrieved when every expected dataset
    // has succeeded. Use the oldest successful timestamp, never a failure time.
    if(missing||!count)oldest=0;
    auto state=RefreshUI::State::Ready;
    if(busy.load()){state=RefreshUI::State::Working;detail=std::to_string(processed)+" storage scopes processed"+(current.empty()?"":"\nCurrent: "+current)+"\n\n"+detail;}
    else if(failed)state=RefreshUI::State::Error;
    else if(retrying||((missing||stale||!count)&&needsManual))state=RefreshUI::State::Waiting;
    const char* idleLabel=state==RefreshUI::State::Waiting?(retrying?"Retrying":"Auto disabled"):
        state==RefreshUI::State::Ready&&(missing||stale||!count)?(count?"Scheduled":"No scopes"):nullptr;
    if(!count&&characterFiltered)detail="No verified storage scopes match the selected characters and storage filter.";
    if(characterFiltered&&count)detail+="\n\nTimestamp and manual Refresh cover matching character storage only.";
    if(itemView&&nav.item&&!nav.currency){
        const auto p=Skinventory::Commerce::GetItemPriceStatus(nav.item);const auto now=Now();
        detail+="\n\nTrading Post price (selected item): ";
        if(p.pending)detail+="queued or refreshing";
        else if(p.retry_at>now)detail+="retry cooldown "+std::to_string((p.retry_at-now+59)/60)+"m";
        else if(!p.price)detail+="not retrieved";
        else if(now<p.price->fetched_at_unix||now-p.price->fetched_at_unix>=Skinventory::Commerce::PriceTTL)detail+="stale; refresh due";
        else detail+="fresh; next eligible in "+std::to_string((Skinventory::Commerce::PriceTTL-(now-p.price->fetched_at_unix)+59)/60)+"m";
        if(p.price)detail+="; updated "+RefreshUI::Age(static_cast<std::time_t>(p.price->fetched_at_unix));
        if(!AutoRefreshPolicy::Enabled(AutoRefreshPolicy::Dataset::TradingPrices))detail+="; automatic refresh disabled";
        detail+="\nPrice refresh is independent of inventory storage.";
    }
    if(accounts.empty())detail="Link or select an account.";
    const bool incompatible=(characterFiltered&&scope!="Items"&&scope!="Bags"&&scope!="Equipment")||(itemView&&scope=="Wallet"&&!includeWallet);
    if(RefreshUI::Row("native_inventory_refresh",oldest,"inventory scope",state,detail,busy.load()||accounts.empty()||incompatible,nullptr,idleLabel,true))
        Refresh(accounts,v->credentialEpoch,scope,false,{},characterFiltered?characters:noCharacters,includeWallet);
}

}
