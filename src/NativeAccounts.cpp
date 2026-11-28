#include "CharacterIdentityStore.h"
#include "CharacterIdentityCache.h"
#include "CharacterBatchCore.h"
#include "NativeAccounts.h"
#include "UntrustedInput.h"
#include "AccountCommitGate.h"
#include "NativeInventoryStore.h"
#include "PublicCurrencyOwner.h"
#include "InventoryGroupCore.h"
#include "CharacterOrderCore.h"
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
    std::map<std::string,CharacterIdentity::Bindings> bindings;
    std::map<CharacterIdentity::Ref,std::set<std::string>> aliases;
    struct IdentitySummary {CharacterIdentity::Evidence evidence;std::string name;bool present;};
    std::map<CharacterIdentity::Ref,IdentitySummary> identityRecords;
    std::map<uint32_t,std::vector<size_t>> index;
    std::map<std::tuple<std::string,std::string,std::string>,ScopeState> scopes;
};
std::atomic<uint64_t> inventoryGeneration{0};
uint32_t revealItem=0;
InventoryNavigation navigation;
std::set<std::string> expandedInventoryGroups,guildExpandedGroups;
bool groupRevealPending=false;
// Render-thread preferences, persisted in the ordinary settings file.
std::set<uint32_t> walletPins;
std::string root;
std::mutex viewMutex,publishMutex,credentialMutex;
std::vector<CharacterIdentity::RenameNotice> renameNotices; // viewMutex; consumed outside locks on render thread
std::atomic<uint64_t> registryGeneration{1};
std::atomic<bool> registryReady{false};
std::shared_ptr<const View> view=std::make_shared<View>();
std::shared_ptr<const CharacterLoadout::Projection> loadoutPreferences=std::make_shared<CharacterLoadout::Projection>();
std::shared_ptr<const PetOwnership::Projection> petOwnership=std::make_shared<PetOwnership::Projection>();
std::atomic<uint64_t> loadoutPreferenceRevision{0};
std::shared_ptr<const CharacterOrder::OrderProjection> customOrder=std::make_shared<CharacterOrder::OrderProjection>();
std::atomic<uint64_t> customOrderRevision{0};
std::atomic<bool> busy{false};
std::mutex physicalMutex,rosterMutex;
struct PhysicalEntry {uint64_t epoch=0;PhysicalEquipmentResult value;std::chrono::steady_clock::time_point finished{};CharacterIdentity::Binding binding;};
std::map<std::pair<std::string,std::string>,PhysicalEntry> physicalCache,bagCache;
// Identity mutations, admission checks and view publication use credentialMutex.
// Never hold it across HTTP or broker acquisition.
std::map<std::string,CharacterIdentity::State> identityStates;
std::set<std::string> identityRosterBlocked;
struct IdentityProof {CharacterIdentity::Binding binding;uint64_t generation=0;std::chrono::steady_clock::time_point retrieved;};
std::map<std::pair<std::string,std::string>,IdentityProof> identityProofs;
struct AbsenceConfirmation {std::set<CharacterIdentity::Id> pending;std::chrono::steady_clock::time_point since;};
std::map<std::string,AbsenceConfirmation> identityConfirmation;
std::map<std::string,std::chrono::steady_clock::time_point> identityRecoveryAttempts;
std::string message;
RefreshUI::State messageState=RefreshUI::State::Ready;
using ScopeKey=std::tuple<std::string,std::string,std::string>;
// Session-only progress overlay; no database reads or inventory index rebuilds on render.
std::map<ScopeKey,ScopeState> liveScopes;
unsigned processedScopes=0;
std::string activeScope;
std::optional<ScopeKey> activeScopeKey,pendingScopeKey;
uint64_t progressEpoch=0;
// Separate observation lock, never held across broker admission, transport,
// database work or credential gates. Both endpoint consumers report here.
using BrokerKey=std::pair<uint64_t,ScopeKey>;
std::mutex brokerStatusMutex;
RefreshStatus::WorkPhases<BrokerKey> brokerPhases;
class BrokerWork {
    BrokerKey key;
    RefreshStatus::Phase phase=RefreshStatus::Phase::Queued;
public:
    BrokerWork(uint64_t epoch,ScopeKey scope):key{epoch,std::move(scope)}{
        std::lock_guard<std::mutex> lock(brokerStatusMutex);brokerPhases.Queue(key);
    }
    BrokerWork(const BrokerWork&)=delete;
    BrokerWork& operator=(const BrokerWork&)=delete;
    void Start(){
        std::lock_guard<std::mutex> lock(brokerStatusMutex);brokerPhases.Start(key);phase=RefreshStatus::Phase::Working;
    }
    ~BrokerWork(){std::lock_guard<std::mutex> lock(brokerStatusMutex);brokerPhases.Finish(key,phase);}
};
void BeginScope(const ScopeState& state,const std::string& label){
    std::lock_guard<std::mutex> lock(viewMutex);
    auto current=state;current.status="Refreshing";
    auto prior=view->scopes.find({state.account,state.scope,state.character});
    if(prior!=view->scopes.end())current.success=prior->second.success;
    liveScopes[{state.account,state.scope,state.character}]=std::move(current);
    activeScope=label;activeScopeKey=ScopeKey{state.account,state.scope,state.character};
    if(pendingScopeKey==activeScopeKey)pendingScopeKey.reset();
}
void EndScope(ScopeState state){
    std::lock_guard<std::mutex> lock(viewMutex);
    auto key=ScopeKey{state.account,state.scope,state.character};
    // Shared endpoint owners already published the authoritative outcome.
    // Do not let a delayed inventory consumer overlay a newer owner result.
    if(state.scope=="Equipment"||state.scope=="Bags"||state.scope=="Roster"){
        liveScopes.erase(key);++processedScopes;activeScope.clear();activeScopeKey.reset();return;
    }
    auto prior=liveScopes.find(key);
    if(state.status!="Ready"&&prior!=liveScopes.end())state.success=prior->second.success;
    liveScopes[key]=std::move(state);++processedScopes;activeScope.clear();activeScopeKey.reset();
}
void SetActiveScope(const std::string& label,std::optional<ScopeKey> key={}){
    std::lock_guard<std::mutex> lock(viewMutex);activeScope=label;activeScopeKey=std::move(key);
    if(pendingScopeKey==activeScopeKey)pendingScopeKey.reset();
}
void ClearProgress(){
    std::lock_guard<std::mutex> lock(viewMutex);liveScopes.clear();processedScopes=0;activeScope.clear();activeScopeKey.reset();pendingScopeKey.reset();progressEpoch=0;
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
void InitAccounts(Database::Connection& db){CharacterIdentity::InitializeAccountRegistry(db);}
std::shared_ptr<const CharacterLoadout::Projection> PreferenceSnapshot(){
    std::lock_guard<std::mutex> lock(viewMutex);return loadoutPreferences;
}
void PublishPreferences(std::shared_ptr<const CharacterLoadout::Projection> next){
    std::lock_guard<std::mutex> lock(viewMutex);loadoutPreferences=std::move(next);++loadoutPreferenceRevision;
}
// Caller holds the credential gate; no transport or callbacks. Only
// evidence-qualified pending records are eligible for automatic adoption.
std::shared_ptr<const CharacterOrder::OrderProjection> OrderSnapshot(){
    std::lock_guard<std::mutex> lock(viewMutex);return customOrder;
}
void PublishOrder(std::shared_ptr<const CharacterOrder::OrderProjection> next){
    std::lock_guard<std::mutex> lock(viewMutex);customOrder=std::move(next);++customOrderRevision;
}
std::shared_ptr<const PetOwnership::Projection> PetSnapshot(){
    std::lock_guard<std::mutex> lock(viewMutex);return petOwnership;
}
void PublishPets(std::shared_ptr<const PetOwnership::Projection> next){
    std::lock_guard<std::mutex> lock(viewMutex);petOwnership=std::move(next);
}
void RefreshCharacterPreferences(){
    if(!std::filesystem::exists(AccountPath()))return;
    Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
    Database::Transaction tx(db);
    for(const auto& [account,state]:identityStates)
        if(!identityRosterBlocked.count(account)){CharacterLoadout::AdoptPortable(db,state);CharacterOrder::AdoptRanks(db,state);}
    auto order=std::make_shared<CharacterOrder::OrderProjection>(CharacterOrder::ReadOrder(db));
    auto next=std::make_shared<CharacterLoadout::Projection>(CharacterLoadout::ReadProjection(db));
    auto pets=std::make_shared<PetOwnership::Projection>(PetOwnership::Read(db));
    tx.Commit();PublishPreferences(std::move(next));PublishOrder(std::move(order));PublishPets(std::move(pets));
}
// Credential gate is already held. Bootstrap only once, using exact GUID
// and independently verified cached core data. Never replay old cache
// evidence over an initialized durable registry.
CharacterIdentity::State BootstrapIdentity(Database::Connection& db,const std::string& account){
    auto state=CharacterIdentity::Read(db,account);
    if(state.initialized)return state;
    const auto historical=CharacterIdentity::ReadHistoricalCore(root,account);
    if(historical.names.empty())return state;
    const auto plan=CharacterIdentity::Commit(db,account,state.revision,historical.names,historical.observations);
    if(!plan||!*plan)throw std::runtime_error("Could not preserve historical character identity");
    return plan->next;
}
// Caller holds credentialMutex. Cache adoption is independently replayable.
void ReindexView(View& next){
    next.index.clear();next.scopes.clear();
    for(size_t i=0;i<next.inventory.rows.size();++i)next.index[next.inventory.rows[i].slot.id].push_back(i);
    for(auto& entry:next.index)std::sort(entry.second.begin(),entry.second.end(),[&](size_t a,size_t b){
        return LocationLess(next.inventory.rows[a].slot,next.inventory.rows[b].slot);
    });
    for(const auto& s:next.inventory.states)next.scopes[{s.account,s.scope,s.character}]=s;
}
void FilterIdentityView(View& next){
    const auto safe=[&](const std::string& account,const std::string& name){
        if(name.empty())return true;
        const auto it=next.bindings.find(account);
        return it!=next.bindings.end()&&it->second.count(name);
    };
    auto& rows=next.inventory.rows;
    rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const auto& r){
        return (r.slot.scope=="Bags"||r.slot.scope=="Equipment")&&!safe(r.slot.account,r.slot.character);
    }),rows.end());
    auto& states=next.inventory.states;
    states.erase(std::remove_if(states.begin(),states.end(),[&](const auto& s){
        return (s.scope=="Bags"||s.scope=="Equipment")&&!safe(s.account,s.character);
    }),states.end());
    ReindexView(next);
}
void InvalidateIdentityProjection(const std::string& account,const std::set<std::string>& affected,bool all=false){
    GW2API::DiscardIdentityCache(account,affected,all);
    for(auto it=identityProofs.begin();it!=identityProofs.end();){
        if(it->first.first==account&&(all||affected.count(it->first.second)))it=identityProofs.erase(it);
        else ++it;
    }
    std::lock_guard<std::mutex> lock(viewMutex);
    // Fail closed even if the subsequent disk reread fails.
    if(all||!affected.empty()){
        auto next=std::make_shared<View>(*view);
        auto binding=next->bindings.find(account);
        if(binding!=next->bindings.end()){
            if(all)next->bindings.erase(binding);
            else for(const auto& name:affected)binding->second.erase(name);
        }
        FilterIdentityView(*next);view=std::move(next);++inventoryGeneration;
    }
    for(auto it=liveScopes.begin();it!=liveScopes.end();){
        if(std::get<0>(it->first)==account&&!std::get<2>(it->first).empty()&&
           (all||affected.count(std::get<2>(it->first))))it=liveScopes.erase(it);
        else ++it;
    }
}
bool AdoptIdentity(const CharacterIdentity::State& state,
    const std::optional<CharacterIdentity::RosterEvidence>& roster=std::nullopt){
    const auto account=state.account;
    identityRosterBlocked.insert(account);
    try{
        const auto affected=CharacterIdentity::AdoptCache(root,state,roster);
        if(!affected)throw std::runtime_error("Character identity cache recovery failed");
        InvalidateIdentityProjection(account,*affected);
        {
            std::lock_guard<std::mutex> lock(CharacterActivity::Mutex());
            CharacterActivity::State().FenceBindings(account,CharacterIdentity::CurrentBindings(state));
            CharacterActivity::State().AdoptPortableHistory(state);
        }
        // A successful cache adoption, not a raw roster change, authorizes
        // a single live rename notification. Startup has no previous state.
        const auto previous=identityStates.find(account);
        if(previous!=identityStates.end()&&registryReady.load()){
            const auto changes=CharacterIdentity::VerifiedRenames(previous->second,state);
            std::lock_guard<std::mutex> lock(viewMutex);
            for(const auto& change:changes){
                const auto queued=std::find_if(renameNotices.begin(),renameNotices.end(),[&](const auto& n){return n.ref==change.ref;});
                if(queued!=renameNotices.end())queued->after=change.after;
                else if(renameNotices.size()<CharacterIdentity::MaxRoster)renameNotices.push_back(change);
            }
        }
        identityStates[account]=state;
        identityRosterBlocked.erase(account);
        // Preferences never make authenticated cache adoption fail.
        try{
            const auto pending=PreferenceSnapshot();
            const auto order=OrderSnapshot();
            if(std::any_of(pending->pending.begin(),pending->pending.end(),[&](const auto& p){return p.account==account&&p.HasEvidence();})||
               std::any_of(order->pending.begin(),order->pending.end(),[&](const auto& p){return p.account==account;}))
                RefreshCharacterPreferences();
        }catch(...){}
        return true;
    }catch(...){
        InvalidateIdentityProjection(account,{},true);
        return false;
    }
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
    for(const auto& [account,state]:identityStates)
        if(!identityRosterBlocked.count(account)){
            next->bindings.emplace(account,CharacterIdentity::CurrentBindings(state));
            for(const auto& [id,r]:state.records){
                const CharacterIdentity::Ref ref{account,id};
                next->identityRecords.emplace(ref,View::IdentitySummary{r.evidence,r.name,r.present});
                if(state.Current(r.name)==&r){
                    auto names=r.names;names.insert(r.name);next->aliases.emplace(ref,std::move(names));
                }
            }
        }
    // Preserve the raw roster for native Fast Load. Filter only cache rows.
    FilterIdentityView(*next);
    std::lock_guard<std::mutex> lock(viewMutex);view=std::move(next);++inventoryGeneration;if(replaceMessage){message=msg;messageState=state;}registryReady=true;
}
std::shared_ptr<const View> Snapshot(){std::lock_guard<std::mutex> lock(viewMutex);return view;}
GroupPath IdentityGroupPath(const View& v,const std::string& account,const std::string& name,const std::string& scope){
    GroupPath path{account,name,scope};
    if(name.empty())return path;
    const auto a=v.bindings.find(account);
    if(a!=v.bindings.end()){
        const auto b=a->second.find(name);
        if(b!=a->second.end())path.identity=GroupIdentity(b->second);
    }
    return path;
}
std::string CharacterFilterLabel(const View& v,const CharacterIdentity::Selection& selection){
    std::string label=selection.name;
    if(v.accounts.size()>1)for(const auto& a:v.accounts)if(a.id==selection.ref.account){
        label+=" ("+(a.label.empty()?a.name:a.label)+")";break;
    }
    return label;
}
void Message(const std::string& msg,RefreshUI::State state=RefreshUI::State::Error){std::lock_guard<std::mutex> lock(viewMutex);message=msg;messageState=state;}
std::string Status(){std::lock_guard<std::mutex> lock(viewMutex);return message;}
template<class Job> void Launch(Job job,std::optional<ScopeKey> pending={},uint64_t epoch=0){
    if(busy.exchange(true))return;
    ClearProgress();
    {std::lock_guard<std::mutex> lock(viewMutex);pendingScopeKey=std::move(pending);progressEpoch=epoch;}
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
    bool full=false;
};
struct RosterEntry {
    uint64_t epoch=0;
    RosterResult result;
    std::chrono::steady_clock::time_point finished{};
};
std::map<std::string,RosterEntry> rosterCache;
// One owner for endpoint, durable roster, pruning and status.
RosterResult FetchRoster(const LinkedAccount& a,bool verifyAll=false,bool recovery=false){
    using Clock=std::chrono::steady_clock;
    const auto queued=Clock::now();
    RosterResult result;result.state={a.id,"Roster",{},"Roster retrieval failed",0,0};
    BrokerWork observed(a.generation,ScopeKey{a.id,"Roster",{}});
    std::unique_lock<std::mutex> lock(rosterMutex,std::defer_lock);
    while(!lock.try_lock()){
        if(Background::Workers().Stopping()||!Current(a))return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if(Background::Workers().Stopping()||!Current(a))return result;
    const auto previous=rosterCache.find(a.id);
    if(previous!=rosterCache.end()&&previous->second.epoch==a.generation&&
       previous->second.result.accepted&&previous->second.result.state.status=="Ready"&&
       (previous->second.finished>=queued||
        (recovery&&Clock::now()-previous->second.finished<std::chrono::seconds(30)))&&
       (!verifyAll||previous->second.result.full))return previous->second.result;
    observed.Start();
    result.state.attempted=Now();
    CharacterIdentity::State before;
    try{
        if(!CommitIfCurrent(a,[&]{
            Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
            before=BootstrapIdentity(db,a.id);return true;
        }))return result;
        const auto response=Request(a,"/v2/characters",{"characters"},"Cache-Control: no-cache\r\n");
        if(!response||!response.data.is_array()){
            if(!a.permissions.count("characters"))result.state.status="Missing permissions";
            throw std::runtime_error("Roster request failed");
        }
        for(const auto& n:response.data){
            const auto name=n.get<std::string>();
            if(!CharacterIdentity::Text(name)||!result.names.insert(name).second||result.names.size()>CharacterIdentity::MaxRoster)
                throw std::runtime_error("Invalid roster");
        }
        result.state.status="Ready";result.state.success=Now();
    }catch(...){result.names.clear();result.state.success=0;if(result.state.status=="Ready")result.state.status="Roster retrieval failed";}
    try{
        if(result.state.status=="Ready"){
            std::vector<CharacterIdentity::Observation> evidence;
            struct CoreObservation {Json data;int64_t stamp;Clock::time_point retrieved;};
            std::map<std::string,CoreObservation> coreResponses;
            std::set<std::string> attempted;
            auto readCore=[&](const std::string& name){
                if(!attempted.insert(name).second)return;
                Background::Checkpoint();
                const auto response=Request(a,"/v2/characters/"+Encode(name)+"/core",{"characters"},"Cache-Control: no-cache\r\n");
                if(!response&&!response.malformed&&response.status!=404)return;
                std::optional<CharacterIdentity::Evidence> observed;
                try{
                    if(response&&response.data.is_object()&&response.data.at("name").get<std::string>()==name)
                        observed=CharacterIdentity::Normalize({response.data.at("created").get<std::string>(),
                            response.data.at("profession").get<std::string>(),response.data.at("race").get<std::string>()});
                }catch(...){}
                evidence.push_back({name,observed.value_or(CharacterIdentity::Evidence{})});
                if(observed)coreResponses.emplace(name,CoreObservation{response.data,Now(),Clock::now()});
            };
            // The name-only roster remains the sole presence/absence authority.
            // Bulk summaries supply identity evidence, never new roster names.
            auto readCores=[&](const std::vector<std::string>& names){
                for(size_t i=0;i<names.size();i+=CharacterBatch::Limit){
                    Background::Checkpoint();
                    const auto end=std::min(i+CharacterBatch::Limit,names.size());
                    std::vector<std::string> batch;
                    for(size_t j=i;j<end;++j)if(!attempted.count(names[j]))batch.push_back(names[j]);
                    if(batch.size()>1){
                        const auto response=Request(a,CharacterBatch::Path(batch,Encode),{"characters"},"Cache-Control: no-cache\r\n",true);
                        const auto received=Clock::now();const auto stamp=Now();
                        if(!response&&!CharacterBatch::NarrowFallback(response.status)&&!CharacterBatch::SplitRequired(response.status)){
                            // A transport failure is not evidence of absence;
                            // do not fan out to individual core requests.
                            attempted.insert(batch.begin(),batch.end());continue;
                        }
                        if(response){
                            auto indexed=CharacterBatch::Index(batch,response.data);
                            if(indexed)for(auto& [name,row]:*indexed){
                                const auto observed=CharacterBatch::Evidence(row);
                                if(!observed)continue; // targeted core preserves existing conflict/failure semantics
                                attempted.insert(name);
                                evidence.push_back({name,*observed});
                                coreResponses.emplace(name,CoreObservation{std::move(row),stamp,received});
                            }
                        }
                    }
                    for(const auto& name:batch)readCore(name);
                }
            };
            std::vector<std::string> initial;
            for(const auto& name:result.names)if(verifyAll||!before.Current(name))initial.push_back(name);
            readCores(initial);
            auto plan=CharacterIdentity::Reconcile(before,result.names,evidence);
            const bool missing=std::any_of(before.records.begin(),before.records.end(),[&](const auto& entry){
                const auto& old=entry.second;
                if(!old.present)return false;
                const auto next=plan.next.records.find(old.id);
                return !plan||next==plan.next.records.end()||
                    (next->second.present&&!result.names.count(next->second.name));
            });
            // Expand only for unresolved ownership or retirement.
            if(missing||!before.pendingAbsence.empty()||(plan&&!plan.next.blocked.empty()))
                readCores(std::vector<std::string>(result.names.begin(),result.names.end()));
            result.full=evidence.size()==result.names.size()&&
                std::all_of(evidence.begin(),evidence.end(),[](const auto& row){return CharacterIdentity::Normalize(row.evidence).has_value();});
            result.accepted=CommitIfCurrent(a,[&]{
                Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
                const auto now=Clock::now();
                const auto prior=identityConfirmation.find(a.id);
                // Full scans must be separated by the existing propagation
                // grace. Restart/replay cannot confirm a retirement.
                const bool confirm=result.full&&prior!=identityConfirmation.end()&&
                    prior->second.pending==before.pendingAbsence&&
                    now-prior->second.since>=std::chrono::seconds(RefreshEngine::LogoutGraceSeconds);
                const CharacterIdentity::RosterEvidence verified{result.names,result.state.attempted,result.state.success};
                const auto updated=CharacterIdentity::Commit(db,a.id,before.revision,result.names,evidence,confirm,verified);
                if(!updated||!*updated)return false;
                if(!AdoptIdentity(updated->next,verified)){
                    Publish("",false);return false;
                }
                if(updated->next.pendingAbsence.empty())identityConfirmation.erase(a.id);
                else if(result.full&&(prior==identityConfirmation.end()||prior->second.pending!=updated->next.pendingAbsence))
                    identityConfirmation[a.id]={updated->next.pendingAbsence,now};
                for(const auto& row:evidence){
                    const auto e=CharacterIdentity::Normalize(row.evidence);
                    const auto* bound=updated->next.Current(row.name);
                    if(e&&bound&&bound->evidence==*e){
                        const auto core=coreResponses.find(row.name);
                        if(core!=coreResponses.end()){
                            identityProofs[{a.id,row.name}]={{bound->id,bound->evidence,bound->bindingRevision},a.generation,core->second.retrieved};
                            (void)GW2API::PublishDiscoveredCore(a.id,row.name,core->second.data,core->second.stamp);
                        }
                    }
                }
                Publish("",false);return true;
            });
        }else result.accepted=CommitIfCurrent(a,[&]{
            if(!Record(root,result.state))return false;Publish("",false);return true;
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
            try{
                const auto state=BootstrapIdentity(db,a.id);
                if(state.initialized)(void)AdoptIdentity(state,CharacterIdentity::ReadVerifiedRoster(db,a.id));
            }catch(...){identityRosterBlocked.insert(a.id);InvalidateIdentityProjection(a.id,{},true);}
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
        identityStates.erase(id);identityRosterBlocked.erase(id);identityConfirmation.erase(id);
        {
            std::lock_guard<std::mutex> lock(CharacterActivity::Mutex());
            CharacterActivity::State().FenceBindings(id,{});
        }
        for(auto it=identityProofs.begin();it!=identityProofs.end();)
            if(it->first.first==id)it=identityProofs.erase(it);else ++it;
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
bool ActivityScope(const std::string& scope){return RefreshEngine::ActivityInventoryScope(scope);}
bool ScopeFresh(const std::string& account,const std::string& scope,const std::string& character,int64_t success){
    if(ActivityScope(scope))return CharacterActivity::Fresh({account,character},scope,success,scope=="Equipment"&&CharacterActivity::SpecSignal().load());
    return RefreshEngine::TimedFresh(success,Now(),FreshSeconds(scope));
}
bool ScopePermitted(const Account& a,const std::string& scope){
    return RefreshEngine::InventoryPermitted(a.permissions,scope);
}
// Same eligibility and access evidence for automatic admission and footer
// projection. The inspection is read-only; only admission owns dispatch.
RefreshStatus::Scope InspectStorageScope(const std::string& account,const std::string& scope,
    const std::string& character,const ScopeState* state,bool permitted,int64_t now,
    RefreshStatus::Phase phase=RefreshStatus::Phase::Idle){
    namespace R=RefreshStatus;
    R::Scope result;
    const auto status=state?state->status:"Not retrieved";
    result.success=state?state->success:0;result.attempt=state?state->attempted:0;
    result.automatic=AutoScope(scope);result.access=permitted?R::Access::Allowed:R::Access::Denied;
    result.phase=phase;
    const auto unresolved=state&&status!="Ready"?state->attempted:0;
    if(ActivityScope(scope)){
        namespace A=CharacterActivity;
        std::lock_guard<std::mutex> lock(A::Mutex());
        const auto inspection=A::State().Inspect({account,character},scope,result.success,now,
            scope=="Equipment"&&A::SpecSignal().load(),unresolved);
        result.fresh=inspection.fresh;result.eligibility=inspection.eligibility;
        result.attempt=std::max(result.attempt,inspection.attempt);
        result.error=inspection.error;
    }else{
        result.fresh=RefreshEngine::TimedFresh(result.success,now,FreshSeconds(scope));
        if(RefreshEngine::SharedAccountStorage(scope)){
            // Wallet/Delivery can change independently of observed gameplay;
            // keep timed demand offline, but share active admission and age.
            namespace A=CharacterActivity;
            int64_t login=0;
            {std::lock_guard<std::mutex> lock(A::Mutex());
                if(A::State().ActiveKey().first==account)login=A::State().LastLogin({account,{}});}
            result.eligibility=RefreshEngine::ActiveEligibility(result.success,unresolved,now,login,
                FreshSeconds(scope),RefreshEngine::Offset(account+"//"+scope));
        }else result.eligibility=RefreshEngine::TimedEligibility(result.success,unresolved,now,FreshSeconds(scope));
    }
    if(status!="Ready"&&status!="Not retrieved"&&status!="Refreshing"){
        result.error=status;result.failed=true;
    }
    if(!result.error.empty())result.failed=true;
    return result;
}
bool ScopeDue(const View& v,const std::string& account,const std::string& scope,const std::string& character={}){
    auto it=v.scopes.find({account,scope,character});
    if(!AutoScope(scope))return false;
    if(!character.empty()&&(scope=="Bags"||scope=="Equipment")){
        const auto binding=v.bindings.find(account);
        if(binding==v.bindings.end()||!binding->second.count(character))return false;
    }
    const auto a=std::find_if(v.accounts.begin(),v.accounts.end(),[&](const Account& row){return row.id==account;});
    if(a==v.accounts.end()||!ScopePermitted(*a,scope))return false;
    return InspectStorageScope(account,scope,character,it==v.scopes.end()?nullptr:&it->second,true,Now()).eligibility.due;
}
// This is the only account-storage planner for the UI trigger and worker admission.
std::set<std::string> AccountStoragePlan(const View& v,const Account& a,const std::string& requested,bool includeWallet,int64_t now){
    std::vector<RefreshEngine::AccountStorageCandidate> rows;
    for(const auto* name:RefreshEngine::AccountStorageScopes){
        const std::string scope=name;
        const bool seed=requested==scope||(requested=="Items"&&(scope!="Wallet"||includeWallet));
        const auto it=v.scopes.find({a.id,scope,{}});
        auto inspected=InspectStorageScope(a.id,scope,{},it==v.scopes.end()?nullptr:&it->second,ScopePermitted(a,scope),now);
        rows.push_back({scope,inspected.automatic&&inspected.access==RefreshStatus::Access::Allowed,
            seed,inspected.success>0&&inspected.success<=now,inspected.eligibility});
    }
    return RefreshEngine::PlanAccountStorage(rows,now);
}
bool Fresh(const View& v,const Row& row){
    const auto& s=row.slot;auto it=v.scopes.find({s.account,s.scope,s.character});
    return it!=v.scopes.end()&&it->second.status=="Ready"&&ScopeFresh(s.account,s.scope,s.character,row.captured);
}
void Refresh(const std::vector<Account>& accounts,uint64_t epoch,const std::string& onlyScope,bool automatic=false,const std::string& onlyCharacter={},const CharacterFilter& characterFilter={},bool includeWallet=false,bool accountBatch=false){
    std::optional<ScopeKey> pending;
    const bool singleKnown=onlyScope=="Wallet"||onlyScope=="Bank"||onlyScope=="Shared inventory"||
        onlyScope=="Materials"||onlyScope=="Legendary Armory"||onlyScope=="Delivery box"||onlyScope=="Roster"||onlyScope=="Guild bank";
    if(!accountBatch&&accounts.size()==1&&(singleKnown||(automatic&&!onlyCharacter.empty()&&(onlyScope=="Bags"||onlyScope=="Equipment"))))
        pending=ScopeKey{accounts.front().id,onlyScope=="Guild bank"?"Guild access":onlyScope,onlyCharacter};
    const auto admitted=Snapshot();
    Launch([accounts,epoch,onlyScope,automatic,onlyCharacter,characterFilter,includeWallet,accountBatch,admitted]{
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
                std::lock_guard<std::mutex> gate(credentialMutex);
                Publish("",false);lastPublish=std::chrono::steady_clock::now();
            }
        };
        for(const auto& a:accounts){
            const auto accountPlan=accountBatch?AccountStoragePlan(*previous,a,onlyScope,includeWallet,Now()):std::set<std::string>{};
            if(accountBatch&&accountPlan.empty())continue;
            // Account selection and permissions came from the same immutable
            // registry view at admission. Never attach a later credential to it.
            LinkedAccount linked;linked.id=a.id;linked.name=a.name;
            linked.permissions=a.permissions;linked.generation=epoch;
            const auto admittedBindings=admitted->bindings.find(a.id);
            if(admittedBindings!=admitted->bindings.end())linked.bindings=admittedBindings->second;
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
                if(accountBatch){
                    if(!character.empty()||!accountPlan.count(name))return;
                }else{
                    if(!MatchesStorageView(onlyScope,{},name,character)||!characterFilter.Matches(a.id,character))return;
                    if(!onlyCharacter.empty()&&character!=onlyCharacter)return;
                    if(automatic&&!ScopeDue(*previous,a.id,name,character))return;
                }
                checkCurrent();
                const bool characterScope=bags||name=="Equipment";
                const auto address=characterScope?EnsureCharacterAddress(linked,character):std::optional<CharacterIdentity::Address>{};
                if(characterScope&&(!address||!characterFilter.Accepts(*address)))return;
                ScopeState state{a.id,name,character,"Ready",Now(),0};
                BeginScope(state,(accounts.size()>1?a.name+" / ":"")+name+(character.empty()?"":" / "+character));
                const bool permitted=ScopePermitted(a,name);
                if(!permitted&&!characterScope){
                    state.status="Missing permissions";
                    const auto write=[&]{if(!Record(root,state))return false;EndScope(state);return true;};
                    if(address){if(!CommitIfCharacterCurrent(linked,*address,write))return;}
                    else commit([&]{if(!write())throw Background::Cancelled{};});
                    publishProgress();return;
                }
                CharacterActivity::CancelOnShutdown attempts({a.id,character});
                CharacterActivity::Ticket ticket{};
                const auto scopeCommit=[&](const auto& write){
                    try{
                        if(address){
                            if(!CommitIfCharacterCurrent(linked,*address,[&]{write();return true;}))
                                throw Background::Cancelled{};
                        }else commit(write);
                    }
                    catch(const Background::Cancelled&){
                        if(ticket.serial)CharacterActivity::Cancel({a.id,character},name,ticket);
                        throw;
                    }
                };
                try{
                    if(characterScope){
                        auto physical=bags?RequestCharacterBags(linked,*address,!automatic):RequestPhysicalEquipment(linked,*address,!automatic);
                        checkCurrent();
                        // The physical owner alone records success AND failure.
                        if(!physical.started)throw Background::Cancelled{};
                        state.attempted=physical.started;
                        if(physical.response&&physical.inventoryCommitted){
                            state.success=physical.success;
                            auto slots=bags?ParseBags(physical.response.data,a.id,character):ParseEquipment(physical.response.data,a.id,character);
                            for(const auto& slot:slots){ids.insert(slot.id);ids.insert(slot.upgrades.begin(),slot.upgrades.end());ids.insert(slot.infusions.begin(),slot.infusions.end());}
                        }else state.status=physical.response?"Invalid or incomplete response":physical.response.error;
                        if(!CommitIfCharacterCurrent(linked,*address,[&]{EndScope(state);return true;}))
                            throw Background::Cancelled{};
                        publishProgress();return;
                    }
                    if(ActivityScope(name)){
                        if(address){
                            if(!CommitIfCharacterCurrent(linked,*address,[&]{ticket=attempts.BeginScope(name);return true;}))
                                throw Background::Cancelled{};
                        }else ticket=attempts.BeginScope(name);
                    }
                    auto j=fetchAccount(path);std::vector<Slot> slots;
                    if(name=="Legendary Armory")slots=ParseArmory(j,a.id);
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
                    state.status=what.rfind("HTTP ",0)==0||what=="Invalid key or missing permission"||what=="Transport failed or cancelled"||what=="Rate limited; retry later"||what=="Character identity not verified"?what:"Invalid or incomplete response";
                    if(characterScope)throw;
                    scopeCommit([&]{if(!Record(root,state))throw Background::Cancelled{};});
                }
                if(address){
                    if(!CommitIfCharacterCurrent(linked,*address,[&]{EndScope(state);return true;}))
                        throw Background::Cancelled{};
                }else EndScope(state);
                publishProgress();
            };
            if(accountBatch?accountPlan.count("Wallet")!=0:
                (onlyScope.empty()||onlyScope=="Wallet"||(includeWallet&&onlyScope=="Items"))&&(!automatic||ScopeDue(*previous,a.id,"Wallet"))){
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
            if(!accountBatch&&(onlyScope.empty()||onlyScope=="Guild bank")&&(!automatic||ScopeDue(*previous,a.id,"Guild access"))){
                checkCurrent();
                SetActiveScope("Guild access",ScopeKey{a.id,"Guild access",{}});
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
                        SetActiveScope("Guild bank / "+(old!=previous->inventory.guilds.end()&&!old->second.name.empty()?old->second.name:id),ScopeKey{id,"Guild bank",{}});
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
            if(!accountBatch&&(onlyScope.empty()||onlyScope=="Items"||onlyScope=="Bags"||onlyScope=="Equipment"||onlyScope=="Roster")&&(!automatic||AutoScope("Roster"))){
                std::set<std::string> names;
                auto cached=previous->inventory.rosters.find(a.id);
                if(!a.permissions.count("characters")&&automatic&&!ScopeDue(*previous,a.id,"Roster"))continue;
                if(a.permissions.count("characters")&&automatic&&onlyScope!="Roster"&&cached!=previous->inventory.rosters.end())names=cached->second;
                else if(a.permissions.count("characters")&&automatic&&!ScopeDue(*previous,a.id,"Roster")){
                    if(cached!=previous->inventory.rosters.end())names=cached->second;
                }else{
                    ScopeState state{a.id,"Roster","","Ready",Now(),0};BeginScope(state,"Roster");
                    auto result=FetchRoster(linked,automatic||onlyScope=="Roster");
                    if(!result.accepted)throw Background::Cancelled{};
                    EndScope(result.state);publishProgress(true);
                    if(result.state.status!="Ready")continue;
                    names=std::move(result.names);
                    if(onlyCharacter.empty()&&characterFilter.empty())linked.bindings=FindLinked(a.id).bindings;
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
        
        if(currencyMetadataNeeded){
            std::set<uint32_t> currencies;
            for(const auto& w:cached.wallet)if(w.name.empty()||!w.iconKnown)currencies.insert(w.id);
            if(!currencies.empty())try{
                RefreshCurrencyDefinitions(root,currencies);
            }catch(const Background::Cancelled&){throw;}catch(...){Background::Checkpoint();}
        }
        std::vector<uint32_t> batch;
        size_t metadataDone=0;const auto metadataTotal=ids.size();
        auto flush=[&]{
            if(batch.empty())return;
            SetActiveScope("Item metadata "+std::to_string(metadataDone)+"/"+std::to_string(metadataTotal));
            try{GW2API::EnrichInventoryItems(batch,!automatic);}
            catch(const Background::Cancelled&){throw;}catch(...){Background::Checkpoint();}
            metadataDone+=batch.size();batch.clear();publishProgress();
        };
        for(auto id:ids){Background::Checkpoint();if(batch.size()==RefreshEngine::PublicBulkIdLimit)flush();batch.push_back(id);}flush();
        {std::lock_guard<std::mutex> gate(credentialMutex);
        Publish("Refresh finished. Check individual storage status for failures or stale data.");}
    },std::move(pending),epoch);
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
        const auto bindings=v->bindings.find(a.id);
        if(bindings!=v->bindings.end())row.bindings=bindings->second;
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
bool HasCharacterBinding(const std::string& account,const std::string& name){
    const auto v=Snapshot();const auto it=v->bindings.find(account);
    return it!=v->bindings.end()&&it->second.count(name);
}
std::optional<CharacterIdentity::Address> ResolveCharacterAddress(const std::string& account,const std::string& name){
    const auto v=Snapshot();const auto it=v->bindings.find(account);
    if(it==v->bindings.end())return std::nullopt;
    const auto roster=v->inventory.rosters.find(account);
    if(roster==v->inventory.rosters.end()||!roster->second.count(name))return std::nullopt;
    const auto binding=it->second.find(name);
    if(binding==it->second.end())return std::nullopt;
    return CharacterIdentity::Address{account,name,binding->second,v->credentialEpoch};
}
std::optional<CharacterIdentity::Address> ResolveCharacterAddress(const CharacterIdentity::Ref& ref){
    const auto v=Snapshot();const auto it=v->bindings.find(ref.account);
    if(!ref.Valid()||it==v->bindings.end())return std::nullopt;
    const auto roster=v->inventory.rosters.find(ref.account);
    if(roster==v->inventory.rosters.end())return std::nullopt;
    for(const auto& [name,b]:it->second)if(b.id==ref.id&&roster->second.count(name))
        return CharacterIdentity::Address{ref.account,name,b,v->credentialEpoch};
    return std::nullopt;
}
std::optional<CharacterIdentity::Address> ResolveObservedCharacter(const std::string& name){
    const auto v=Snapshot();const auto account=Active(*v,name);
    if(account.empty())return std::nullopt;
    const auto it=v->bindings.find(account);
    if(it==v->bindings.end())return std::nullopt;
    const auto b=it->second.find(name);
    if(b==it->second.end())return std::nullopt;
    return CharacterIdentity::Address{account,name,b->second,v->credentialEpoch};
}
std::optional<CharacterIdentity::Address> ResolveCharacterEvidence(const std::string& account,const CharacterIdentity::Evidence& evidence){
    const auto v=Snapshot();const View::IdentitySummary* match=nullptr;CharacterIdentity::Id id=0;
    for(const auto& [ref,r]:v->identityRecords)if(ref.account==account&&r.evidence==evidence){
        if(match)return std::nullopt;match=&r;id=ref.id;
    }
    if(!match||!match->present)return std::nullopt;
    const auto bindings=v->bindings.find(account);
    const auto roster=v->inventory.rosters.find(account);
    if(bindings==v->bindings.end()||roster==v->inventory.rosters.end()||!roster->second.count(match->name))return std::nullopt;
    const auto b=bindings->second.find(match->name);
    if(b==bindings->second.end()||b->second.id!=id||!(b->second.evidence==evidence))return std::nullopt;
    return CharacterIdentity::Address{account,match->name,b->second,v->credentialEpoch};
}
bool CharacterReferenceRetired(const CharacterIdentity::Ref& ref,const CharacterIdentity::Evidence& evidence){
    const auto v=Snapshot();const auto it=v->identityRecords.find(ref);
    return it!=v->identityRecords.end()&&it->second.evidence==evidence&&!it->second.present;
}
bool CharacterAccountLinked(const std::string& account){
    const auto v=Snapshot();
    return std::any_of(v->accounts.begin(),v->accounts.end(),[&](const auto& a){return a.id==account;});
}
void AdoptPendingLoginHistory(){
    std::lock_guard<std::mutex> gate(credentialMutex);
    std::lock_guard<std::mutex> activity(CharacterActivity::Mutex());
    for(const auto& [account,state]:identityStates)
        if(!identityRosterBlocked.count(account))CharacterActivity::State().AdoptPortableHistory(state);
}
CharacterOrder::RanksByRef CustomOrderRanks(){return OrderSnapshot()->ranks;}
uint64_t CustomOrderRevision(){return customOrderRevision.load();}
bool StageCustomOrder(const CharacterOrder::PortableRanks& records){
    if(records.empty())return true;
    try{
        std::lock_guard<std::mutex> gate(credentialMutex);
        if(root.empty())return false;
        std::filesystem::create_directories(AccountPath().parent_path());
        Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Create);InitAccounts(db);
        Database::Transaction tx(db);CharacterOrder::StageRanks(db,records);
        for(const auto& [account,state]:identityStates)
            if(!identityRosterBlocked.count(account))CharacterOrder::AdoptRanks(db,state);
        auto next=std::make_shared<CharacterOrder::OrderProjection>(CharacterOrder::ReadOrder(db));
        tx.Commit();PublishOrder(std::move(next));return true;
    }catch(...){return false;}
}
bool SaveCustomOrder(const std::string& account,const std::vector<CharacterIdentity::Address>& ordered){
    try{
        std::lock_guard<std::mutex> gate(credentialMutex);
        const auto state=identityStates.find(account);
        if(state==identityStates.end()||identityRosterBlocked.count(account)||ordered.empty())return false;
        const auto epoch=registryGeneration.load();
        std::vector<CharacterIdentity::Ref> refs;
        for(const auto& address:ordered){
            if(address.account!=account||!CharacterIdentity::Matches(state->second,address,epoch))return false;
            refs.push_back(address.Reference());
        }
        Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
        Database::Transaction tx(db);if(!CharacterOrder::Reorder(db,account,refs))return false;
        auto next=std::make_shared<CharacterOrder::OrderProjection>(CharacterOrder::ReadOrder(db));
        tx.Commit();PublishOrder(std::move(next));return true;
    }catch(...){return false;}
}
PetOwnership::Collection CharacterPets(const CharacterIdentity::Ref& ref){
    const auto snapshot=PetSnapshot();const auto found=snapshot->find(ref);
    return found==snapshot->end()?PetOwnership::Collection{}:found->second;
}
bool SetCharacterPetOwnership(const CharacterIdentity::Address& address,uint32_t pet,PetOwnership::Manual manual){
    try{return CommitIfCharacterCurrent(FindLinked(address.account),address,[&]{
        Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
        Database::Transaction tx(db);
        if(!PetOwnership::SetManual(db,address.Reference(),pet,manual))return false;
        auto next=std::make_shared<PetOwnership::Projection>(PetOwnership::Read(db));
        tx.Commit();PublishPets(std::move(next));return true;
    });}catch(...){return false;}
}
bool RecordCommittedPets(const CharacterIdentity::Address& address,const std::set<uint32_t>& pets){
    if(address.binding.evidence.profession!="Ranger"||pets.empty())return true;
    const auto existing=CharacterPets(address.Reference());
    if(std::all_of(pets.begin(),pets.end(),[&](uint32_t id){return PetOwnership::Get(existing,id).observed;}))return true;
    try{
        Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
        Database::Transaction tx(db);
        if(!PetOwnership::Observe(db,address.Reference(),pets))return false;
        auto next=std::make_shared<PetOwnership::Projection>(PetOwnership::Read(db));
        tx.Commit();PublishPets(std::move(next));return true;
    }catch(...){return false;}
}
CharacterLoadout::Choice ExpectedLoadout(const CharacterIdentity::Ref& ref){return PreferenceSnapshot()->Get(ref);}
uint64_t LoadoutPreferenceRevision(){return loadoutPreferenceRevision.load();}
bool StageLoadoutPreferences(const CharacterLoadout::PortableChoices& records){
    if(records.empty())return true;
    try{
        std::lock_guard<std::mutex> gate(credentialMutex);
        if(root.empty())return false;
        std::filesystem::create_directories(AccountPath().parent_path());
        Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Create);InitAccounts(db);
        Database::Transaction tx(db);CharacterLoadout::Stage(db,records);
        for(const auto& [account,state]:identityStates)
            if(!identityRosterBlocked.count(account))CharacterLoadout::AdoptPortable(db,state);
        auto next=std::make_shared<CharacterLoadout::Projection>(CharacterLoadout::ReadProjection(db));
        tx.Commit();PublishPreferences(std::move(next));return true;
    }catch(...){return false;}
}
bool SetExpectedLoadout(const CharacterIdentity::Address& address,const CharacterLoadout::Choice& choice){
    try{return CommitIfCharacterCurrent(FindLinked(address.account),address,[&]{
        Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
        Database::Transaction tx(db);
        if(!CharacterLoadout::SaveChoice(db,address.Reference(),choice))return false;
        auto next=std::make_shared<CharacterLoadout::Projection>(CharacterLoadout::ReadProjection(db));
        tx.Commit();PublishPreferences(std::move(next));return true;
    });}catch(...){return false;}
}
CharacterLoadout::PortableChoices RecoverableLoadoutPreferences(const CharacterIdentity::Address& address){
    CharacterLoadout::PortableChoices out;
    const auto current=ResolveCharacterAddress(address.Reference());
    if(!current||current->binding!=address.binding||current->name!=address.name)return out;
    const auto v=Snapshot();const auto aliases=v->aliases.find(address.Reference());
    const auto preferences=PreferenceSnapshot();
    for(const auto& record:preferences->pending)if(record.account==address.account){
        if(record.HasEvidence()?record.evidence==address.binding.evidence:
           (record.name==address.name||(aliases!=v->aliases.end()&&aliases->second.count(record.name))))
            out.insert(record);
    }
    return out;
}
bool RecoverLoadoutPreference(const CharacterIdentity::Address& address,const CharacterLoadout::PortableChoice& record){
    try{return CommitIfCharacterCurrent(FindLinked(address.account),address,[&]{
        Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
        Database::Transaction tx(db);
        const auto it=identityStates.find(address.account);
        if(it==identityStates.end()||!CharacterLoadout::Recover(db,it->second,address.Reference(),record))return false;
        auto next=std::make_shared<CharacterLoadout::Projection>(CharacterLoadout::ReadProjection(db));
        tx.Commit();PublishPreferences(std::move(next));return true;
    });}catch(...){return false;}
}
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
bool CommitIfCharacterCurrent(const LinkedAccount& a,const CharacterIdentity::Address& address,const std::function<bool()>& commit){
    return CommitIfCurrent(a,[&]{
        const auto it=identityStates.find(a.id);
        if(identityRosterBlocked.count(a.id)||it==identityStates.end()||
           !CharacterIdentity::Matches(it->second,address,a.generation))return false;
        const auto names=ReadRoster(root,a.id);
        return names&&names->count(address.name)&&commit();
    });
}
std::optional<CharacterIdentity::Address> EnsureCharacterAddress(const LinkedAccount& a,const std::string& name){
    std::optional<CharacterIdentity::Address> address;
    const auto admitted=a.bindings.find(name);
    auto capture=[&]{
        address.reset();
        return CommitIfCurrent(a,[&]{
            const auto it=identityStates.find(a.id);
            if(identityRosterBlocked.count(a.id)||it==identityStates.end())return false;
            const auto names=ReadRoster(root,a.id);
            if(!names||!names->count(name))return false;
            address=CharacterIdentity::Capture(it->second,name,a.generation);
            return address.has_value();
        });
    };
    try{
        // Discovery can establish a future target but must never authorize
        // work admitted without an exact binding. Whole-roster callers take
        // a new admission snapshot after their verified roster operation.
        if(admitted==a.bindings.end())return std::nullopt;
        if(!capture()||!CharacterIdentity::AdmittedBinding(a.bindings,name,address->binding))return std::nullopt;
        return address;
    }catch(...){return std::nullopt;}
}
// The old address must become unusable before recovery discovers a new
// owner. Failed transport by itself is not conflicting identity evidence.
static bool IsolateCharacterAddress(const LinkedAccount& a,const CharacterIdentity::Address& address,
                                    const CharacterIdentity::Evidence& observed){
    try{return CommitIfCharacterCurrent(a,address,[&]{
        const auto names=ReadRoster(root,a.id);
        if(!names)return false;
        Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
        const auto& state=identityStates.at(a.id);
        const auto updated=CharacterIdentity::Commit(db,a.id,state.revision,*names,{{address.name,observed}});
        if(!updated||!*updated)return false;
        const bool adopted=AdoptIdentity(updated->next,CharacterIdentity::ReadVerifiedRoster(db,a.id));
        Publish("",false);return adopted;
    });}catch(...){return false;}
}
bool RecordIdentityObservation(const LinkedAccount& a,const CharacterIdentity::Address& address,
    const std::string& created,const std::string& profession,const std::string& race,
    std::chrono::steady_clock::time_point retrieved){
    if(retrieved==std::chrono::steady_clock::time_point{})retrieved=std::chrono::steady_clock::now();
    const auto evidence=CharacterIdentity::Normalize({created,profession,race});
    if(evidence&&*evidence==address.binding.evidence)
        return CommitIfCharacterCurrent(a,address,[&]{
            identityProofs[{a.id,address.name}]={address.binding,a.generation,retrieved};return true;
        });
    if(IsolateCharacterAddress(a,address,evidence.value_or(CharacterIdentity::Evidence{})))
        (void)FetchRoster(a,true,false);
    return false;
}
void RecoverCharacterAddress(const LinkedAccount& a,const CharacterIdentity::Address& address){
    // Preserve the existing recovery cooldown, but a verified 404 must
    // immediately fence the stale address even when recovery is cooling down.
    try{
        if(!IsolateCharacterAddress(a,address,{}))return;
        const bool due=CommitIfCurrent(a,[&]{
            const auto now=std::chrono::steady_clock::now();
            auto& last=identityRecoveryAttempts[a.id];
            if(now-last<std::chrono::seconds(120))return false;
            last=now;return true;
        });
        if(due)(void)FetchRoster(a,false,true);
    }catch(...){}
}
bool VerifyCharacterAddress(const LinkedAccount& a,const CharacterIdentity::Address& address){
    try{
        bool recent=false;
        if(!CommitIfCharacterCurrent(a,address,[&]{
            const auto it=identityProofs.find({a.id,address.name});
            recent=it!=identityProofs.end()&&it->second.generation==a.generation&&
                it->second.binding==address.binding&&
                std::chrono::steady_clock::now()-it->second.retrieved<std::chrono::seconds(30);
            return true;
        }))return false;
        if(recent)return true;
        const auto response=Request(a,"/v2/characters/"+Encode(address.name)+"/core",{"characters"},"Cache-Control: no-cache\r\n");
        if(response.status==404){RecoverCharacterAddress(a,address);return false;}
        if(response.malformed)return RecordIdentityObservation(a,address,{},{},{});
        if(!response)return false;
        try{
            if(!response.data.is_object()||response.data.at("name").get<std::string>()!=address.name)
                return RecordIdentityObservation(a,address,{},{},{});
            return RecordIdentityObservation(a,address,response.data.at("created").get<std::string>(),
                response.data.at("profession").get<std::string>(),response.data.at("race").get<std::string>());
        }catch(...){return RecordIdentityObservation(a,address,{},{},{});}
    }catch(...){return false;}
}
Result Request(const LinkedAccount& a,const std::string& path,const std::vector<std::string>& permissions,const std::string& headers,bool allowPartial){
    if(!Current(a))return {0,{},"Account changed"};
    for(const auto& p:permissions)if(!a.permissions.count(p))return {403,{},"Missing API permission: "+p};
    std::unique_lock<std::mutex> lock(credentialMutex);
    if(!Current(a))return {0,{},"Account changed"};
    Secret key(Key(a.id));lock.unlock();
    if(key.value.empty())return {401,{},"API key unavailable"};
    auto r=ArenaNet::Get(path,key.value,headers);
    if(Background::Workers().Stopping())return {0,{},"Cancelled"};
    if(!Current(a))return {0,{},"Account changed"};
    if(!CharacterBatch::AcceptStatus(r.status_code,allowPartial))return {r.status_code,{},Error(r.status_code)};
    try{
        auto parsed=Untrusted::Parse(r.body,Untrusted::PublicApi);
        if(Background::Workers().Stopping())return {0,{},"Cancelled"};
        if(!Current(a))return {0,{},"Account changed"};
        return {200,std::move(parsed),{}};
    }catch(...){return {0,{},"Invalid API response",true};}
}
Result Request(const std::string& id,const std::string& path,const std::vector<std::string>& permissions){
    for(const auto& a:Linked())if(a.id==id)return Request(a,path,permissions);
    return {401,{},"No linked account"};
}
// Shared storage publication: caller holds physicalMutex. Both targeted
// and combined responses go through the identical parsers and durable owner.
static PhysicalEquipmentResult PublishCharacterStorage(const LinkedAccount& a,
    const CharacterIdentity::Address& address,const std::string& scope,Result response,
    const CharacterActivity::Ticket& ticket,int64_t success,bool publish,
    std::set<uint32_t>* itemIds=nullptr,bool recordFailure=true){
    const CharacterActivity::Key k{a.id,address.name};
    auto& cache=scope=="Bags"?bagCache:physicalCache;
    PhysicalEquipmentResult result{std::move(response),ticket.started,success};
    std::vector<Slot> slots;bool complete=false;
    if(result.response)try{
        slots=scope=="Bags"?ParseBags(result.response.data,a.id,address.name):
            ParseEquipment(result.response.data,a.id,address.name);complete=true;
    }catch(...){}
    const auto cancel=[&](const char* reason){
        CharacterActivity::Cancel(k,scope,ticket);
        return PhysicalEquipmentResult{{0,{},reason},0,0};
    };
    if(Background::Workers().Stopping())return cancel("Cancelled");
    if(!complete&&!recordFailure){
        CharacterActivity::Cancel(k,scope,ticket);
        return result; // missing combined fields may be repaired by a targeted request
    }
    try{
        const bool accepted=CommitIfCharacterCurrent(a,address,[&]{
            ScopeState state{a.id,scope,address.name,complete?"Ready":
                (result.response?"Invalid or incomplete response":result.response.error),ticket.started,complete?result.success:0};
            if(!Record(root,state,complete?&slots:nullptr))return false;
            if(complete){
                CharacterActivity::Success(k,scope,ticket,result.success);
                result.inventoryCommitted=true;
                if(itemIds)for(const auto& slot:slots){
                    itemIds->insert(slot.id);itemIds->insert(slot.upgrades.begin(),slot.upgrades.end());
                    itemIds->insert(slot.infusions.begin(),slot.infusions.end());
                }
            }else{
                std::lock_guard<std::mutex> lock(CharacterActivity::Mutex());
                CharacterActivity::State().Failure(k,scope,ticket,state.status);
            }
            if(publish)Publish("",false);
            return true;
        });
        if(!accepted)return cancel("Account or character roster changed");
    }catch(...){
        if(Background::Workers().Stopping())return cancel("Cancelled");
        if(!CommitIfCharacterCurrent(a,address,[]{return true;}))return cancel("Account or character changed");
    }
    if(result.inventoryCommitted)cache[k]={a.generation,result,std::chrono::steady_clock::now(),address.binding};
    else cache.erase(k);
    return result;
}
static PhysicalEquipmentResult RequestCharacterStorage(const LinkedAccount& a,const CharacterIdentity::Address& address,bool manual,bool bags){
    const std::string scope=bags?"Bags":"Equipment";
    const auto& character=address.name;
    using Clock=std::chrono::steady_clock;
    const auto queued=Clock::now();
    BrokerWork observed(a.generation,ScopeKey{a.id,scope,character});
    std::unique_lock<std::mutex> lock(physicalMutex,std::defer_lock);
    while(!lock.try_lock()){if(Background::Workers().Stopping())return {{0,{},"Cancelled"},0,0};std::this_thread::sleep_for(std::chrono::milliseconds(25));}
    if(Background::Workers().Stopping()||!Current(a))return {{0,{},"Account changed"},0,0};
    if(!CommitIfCharacterCurrent(a,address,[]{return true;}))return {{0,{},"Character roster changed"},0,0};
    const CharacterActivity::Key k{a.id,character};
    auto& cache=bags?bagCache:physicalCache;
    const auto e=cache.find(k);
    if(e!=cache.end()&&e->second.epoch==a.generation&&e->second.binding==address.binding&&
       e->second.value.response&&e->second.value.inventoryCommitted&&
       (e->second.finished>=queued||(!manual&&Clock::now()-e->second.finished<std::chrono::seconds(15))))return e->second.value;
    if(Background::Workers().Stopping())return {{0,{},"Cancelled"},0,0};
    CharacterActivity::CancelOnShutdown attempts(k);
    observed.Start();
    CharacterActivity::Ticket ticket;
    if(!CommitIfCharacterCurrent(a,address,[&]{ticket=attempts.BeginScope(scope);return true;}))
        return {{0,{},"Character identity changed"},0,0};
    auto r=VerifyCharacterAddress(a,address)?
        Request(a,"/v2/characters/"+Encode(character)+(bags?"/inventory":"/equipment?v=2024-04-01T00:00:00.000Z"),{"characters","inventories"}):
        Result{0,{},"Character identity not verified"};
    return PublishCharacterStorage(a,address,scope,std::move(r),ticket,Now(),true);
}
PhysicalEquipmentResult RequestPhysicalEquipment(const LinkedAccount& a,const CharacterIdentity::Address& address,bool manual){
    return RequestCharacterStorage(a,address,manual,false);
}
PhysicalEquipmentResult RequestCharacterBags(const LinkedAccount& a,const CharacterIdentity::Address& address,bool manual){
    return RequestCharacterStorage(a,address,manual,true);
}
CharacterSummaryResult RequestCharacterSummaries(const LinkedAccount& a,const std::vector<CharacterIdentity::Address>& addresses){
    CharacterSummaryResult out;
    using Clock=std::chrono::steady_clock;
    struct Admission {
        CharacterIdentity::Address address;
        CharacterActivity::Ticket bags,physical;
        bool keepBags=false,keepPhysical=false;
        std::unique_ptr<BrokerWork> bagWork,physicalWork;
        ~Admission(){
            const CharacterActivity::Key key{address.account,address.name};
            if(!keepBags&&bags.serial)CharacterActivity::Cancel(key,"Bags",bags);
            if(!keepPhysical&&physical.serial)CharacterActivity::Cancel(key,"Equipment",physical);
        }
    };
    std::map<std::string,std::unique_ptr<Admission>> admitted;
    for(const auto& address:addresses){
        if(address.account!=a.id||address.credentialGeneration!=a.generation)continue;
        auto row=std::make_unique<Admission>();row->address=address;
        row->bagWork=std::make_unique<BrokerWork>(a.generation,ScopeKey{a.id,"Bags",address.name});
        row->physicalWork=std::make_unique<BrokerWork>(a.generation,ScopeKey{a.id,"Equipment",address.name});
        admitted.emplace(address.name,std::move(row));
    }
    std::unique_lock<std::mutex> lock(physicalMutex,std::defer_lock);
    while(!lock.try_lock()){if(Background::Workers().Stopping())return out;std::this_thread::sleep_for(std::chrono::milliseconds(25));}
    if(!Current(a)||Background::Workers().Stopping())return out;
    std::vector<std::string> names;
    for(auto& [name,row]:admitted){
        if(!CommitIfCharacterCurrent(a,row->address,[&]{
            const CharacterActivity::Key key{a.id,name};
            row->bags=CharacterActivity::Begin(key,"Bags");
            row->physical=CharacterActivity::Begin(key,"Equipment");return true;
        }))continue;
        row->bagWork->Start();row->physicalWork->Start();names.push_back(name);
    }
    const auto path=CharacterBatch::Path(names,Encode);
    if(path.empty())return out;
    auto response=Request(a,path,{"characters","builds","inventories"},"Cache-Control: no-cache\r\n",true);
    out.status=response.status;out.error=response.error;out.stamp=Now();out.received=Clock::now();
    if(Background::Workers().Stopping())return out;
    bool changed=false;
    if(!response){
        // Do not turn a failed summary into a burst of narrow requests. A
        // rejected size/unsupported permission path may still be repaired.
        if(!CharacterBatch::SplitRequired(out.status)&&!CharacterBatch::NarrowFallback(out.status))
            for(const auto& name:names){
                auto& row=*admitted.at(name);
                auto bags=PublishCharacterStorage(a,row.address,"Bags",response,row.bags,out.stamp,false);
                auto physical=PublishCharacterStorage(a,row.address,"Equipment",response,row.physical,out.stamp,false);
                row.keepBags=bags.started>0;row.keepPhysical=physical.started>0;changed=true;
            }
    }else{
        auto indexed=CharacterBatch::Index(names,std::move(response.data));
        if(!indexed){out.status=0;out.error="Invalid combined character identity";return out;}
        for(auto& [name,data]:*indexed){
            auto& row=*admitted.at(name);
            const auto evidence=CharacterBatch::Evidence(data);
            if(!evidence||!(*evidence==row.address.binding.evidence))continue;
            // Exact matching evidence only; conflicts use ordinary core
            // reconciliation outside the storage lock.
            if(!RecordIdentityObservation(a,row.address,evidence->created,evidence->profession,evidence->race,out.received))continue;
            // Only the relevant endpoint-shaped payload lives in each cache.
            auto bags=PublishCharacterStorage(a,row.address,"Bags",
                {200,Json{{"bags",data.value("bags",Json{})}},{}},row.bags,out.stamp,false,&out.itemIds,false);
            auto physical=PublishCharacterStorage(a,row.address,"Equipment",
                {200,Json{{"equipment",data.value("equipment",Json{})}},{}},row.physical,out.stamp,false,&out.itemIds,false);
            if(bags.inventoryCommitted){out.bags.insert(name);row.keepBags=true;changed=true;}
            if(physical.inventoryCommitted){out.physical.emplace(name,std::move(physical));row.keepPhysical=true;changed=true;}
            out.rows.emplace(name,std::move(data));
        }
    }
    if(changed)CommitIfCurrent(a,[&]{Publish("",false);return true;});
    return out;
}
bool RefreshRoster(const LinkedAccount& a,std::set<std::string>& names,bool verifyAll){
    auto result=FetchRoster(a,verifyAll);
    if(!result.accepted||result.state.status!="Ready")return false;
    names=std::move(result.names);return true;
}
std::vector<CharacterIdentity::RenameNotice> TakeRenameNotices(){
    std::lock_guard<std::mutex> lock(viewMutex);
    std::vector<CharacterIdentity::RenameNotice> out;out.swap(renameNotices);return out;
}
void PollObservedIdentity(const std::string& name,bool inventory){
    using D=AutoRefreshPolicy::Dataset;
    static CharacterIdentity::DiscoveryGate discovery;
    using Clock=std::chrono::steady_clock;
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count();
    if(!CharacterIdentity::Text(name)){
        (void)discovery.Admit({},false,now);return;
    }
    if(!Ready()||busy.load()||
        !(inventory?(AutoRefreshPolicy::Enabled(D::Bags)||AutoRefreshPolicy::Enabled(D::Equipment)):
                    AutoRefreshPolicy::Enabled(D::Characters)))return;
    auto accounts=Linked();
    if(ResolveObservedCharacter(name)){(void)discovery.Admit(name,true,now);return;}
    accounts.erase(std::remove_if(accounts.begin(),accounts.end(),[](const auto& a){return !a.permissions.count("characters");}),accounts.end());
    if(accounts.empty()||!discovery.Admit(name,false,now))return;
    // Called only from visible demand. The gameplay observer stays local.
    Launch([accounts]{
        ApiActivity::ReasonScope reason(ApiActivity::Reason::Visible);
        bool complete=true;
        for(const auto& account:accounts){
            Background::Checkpoint();const auto result=FetchRoster(account,false,true);
            if(!result.accepted||result.state.status!="Ready")complete=false;
        }
        Message(complete?"Character identity checked.":"Character identity check incomplete; refresh again.",
            complete?RefreshUI::State::Ready:RefreshUI::State::Error);
    });
}
// Keep the published index immutable. Order only the requested presentation
// subset, with numeric stack order retained inside each storage group.
static std::vector<size_t> OrderedLocationRows(const View& v,const std::vector<size_t>& indices){
    std::set<CharacterOrder::Key> keys;
    for(auto i:indices){const auto& s=v.inventory.rows[i].slot;if(!s.character.empty())keys.emplace(s.account,s.character);}
    const auto ranks=CharacterOrder::Ranks(OrderedCharacterKeys(keys));
    std::map<std::string,size_t> accounts;
    for(const auto& id:OrderedCharacterAccounts())accounts.emplace(id,accounts.size());
    for(const auto& a:v.accounts)accounts.emplace(a.id,accounts.size());
    auto rank=[&](const std::string& id){auto it=accounts.find(id);return it==accounts.end()?accounts.size():it->second;};
    auto out=indices;
    std::stable_sort(out.begin(),out.end(),[&](size_t i,size_t j){
        const auto& a=v.inventory.rows[i].slot;const auto& b=v.inventory.rows[j].slot;
        if(a.account!=b.account){auto ar=rank(a.account),br=rank(b.account);return ar!=br?ar<br:a.account<b.account;}
        if(a.character.empty()!=b.character.empty())return a.character.empty();
        if(a.character!=b.character)return ranks.at({a.account,a.character})<ranks.at({b.account,b.character});
        return LocationLess(a,b);
    });
    return out;
}
std::vector<Location> Locations(uint32_t id){
    auto v=Snapshot();std::vector<Location> out;
    const auto it=v->index.find(id);if(it==v->index.end())return out;
    // Raw snapshot accessor can also be called off the render thread.
    // Only UI presentation reads the character-sort preferences.
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
    for(auto i:OrderedLocationRows(*v,it->second)){
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
        if(!s.character.empty()){
            ImGui::PushID(static_cast<int>(i));
            UI::EntityContext("##item_location_character",[&]{
                RenderCharacterIdentityMenuActions(s.character,s.account,true,"Copy Character Name");
            });
            ImGui::PopID();
        }
    }
}
void Initialize(const std::string& directory){
    root=directory;registryReady=false;
    // Startup recovery runs before render callbacks or authenticated workers.
    std::lock_guard<std::mutex> gate(credentialMutex);
    try{
        for(const auto& account:Accounts()){
            try{
                Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
                const auto state=BootstrapIdentity(db,account.id);
                if(state.initialized){
                    const auto historical=GW2API::GetCharacters();
                    if(AdoptIdentity(state,CharacterIdentity::ReadVerifiedRoster(db,account.id))){
                        // First adoption invalidates unbound dependent scopes.
                        // Retain matching historical core fields and their
                        // original verification stamp, never invent freshness.
                        for(const auto& ch:*historical){
                            if(ch.account_name!=account.id||ch.scope_stamps[CharacterScopes::Index(CharacterScopes::Core)]<=0)continue;
                            const auto* binding=state.Current(ch.name);
                            const auto evidence=CharacterIdentity::Normalize({ch.created,ch.profession,ch.race});
                            if(!binding||!evidence||!(binding->evidence==*evidence))continue;
                            const Json core={{"name",ch.name},{"created",ch.created},{"profession",ch.profession},
                                {"race",ch.race},{"gender",ch.gender},{"level",ch.level},{"age",ch.age},
                                {"deaths",ch.deaths},{"last_modified",ch.last_modified}};
                            (void)GW2API::PublishDiscoveredCore(account.id,ch.name,core,
                                ch.scope_stamps[CharacterScopes::Index(CharacterScopes::Core)]);
                        }
                    }
                }
                else{identityRosterBlocked.insert(account.id);InvalidateIdentityProjection(account.id,{},true);}
            }catch(...){identityRosterBlocked.insert(account.id);InvalidateIdentityProjection(account.id,{},true);}
        }
        try{RefreshCharacterPreferences();}catch(...){}
        Publish("");
    }catch(...){
        GW2API::DiscardIdentityCache("",{},true);registryReady=false;
    }
}
void Shutdown(){
    // Called only after managed workers join.
    registryReady=false;++registryGeneration;physicalCache.clear();bagCache.clear();rosterCache.clear();
    identityStates.clear();identityRosterBlocked.clear();identityProofs.clear();identityConfirmation.clear();identityRecoveryAttempts.clear();
    std::lock_guard<std::mutex> lock(viewMutex);view=std::make_shared<View>();loadoutPreferences=std::make_shared<CharacterLoadout::Projection>();++loadoutPreferenceRevision;petOwnership=std::make_shared<PetOwnership::Projection>();customOrder=std::make_shared<CharacterOrder::OrderProjection>();++customOrderRevision;renameNotices.clear();message.clear();root.clear();
}
bool HasLinkedAccounts(){return !Snapshot()->accounts.empty();}
std::string VerifiedAccountForCharacter(const std::string& character){return Active(*Snapshot(),character);}
CharacterAccessInspection InspectCharacterAccess(const std::string& account,const std::string& character){
    const auto v=Snapshot();CharacterAccessInspection out;
    for(const auto& a:v->accounts)if(a.id==account){
        out.linked=true;out.verified=Active(*v,character)==account;
        out.characters=a.permissions.count("characters")!=0;out.builds=a.permissions.count("builds")!=0;
        out.inventories=a.permissions.count("inventories")!=0;out.generation=v->credentialEpoch;
        if(v->accounts.size()>1)out.label=Display(a);
        break;
    }
    return out;
}
namespace {
bool EnabledAction(const char* label,bool enabled,UI::ButtonRole role=UI::ButtonRole::Standard){
    return UI::AvailableActionButton(label,role,enabled,"Operation unavailable or already in progress.");
}
std::vector<Account> SelectedAccounts(const View& v,const std::string& account){
    std::vector<Account> out;
    for(const auto& a:v.accounts)if(account.empty()||v.accounts.size()==1||a.id==account)out.push_back(a);
    return out;
}
void ItemMenu(uint32_t id,const std::string& name,const std::string& chat,const std::map<std::pair<std::string,std::string>,int64_t>& targets={},bool currency=false){
    if(!ImGui::BeginPopupContextItem("##inventory_item_actions"))return;
    UI::MenuSectionHeading(name.c_str());ImGui::Separator();
    CopyNameMenuItem(name);
    if(!currency){if(!chat.empty())CopyNativeLinkMenuItem(chat,"Copy Item Chat Link");else CopyEntityChatLink(AlterEgo::LINK_ITEM,id,"Copy Item Chat Link");}
    ReferenceWikiSearchAction(name);
    if(FastLoadActionsEnabled()&&!targets.empty()){
        ImGui::Separator();
        if(targets.size()==1){
            const auto& [key,count]=*targets.begin();(void)count;
            RenderCharacterLoadMenu(("Load Character - "+key.second).c_str(),key.second,key.first);
        }else if(UI::BeginActionMenu("Load Character",UI::MenuIcon::Open)){
            std::set<CharacterOrder::Key> keys;
            for(const auto& target:targets)keys.insert(target.first);
            for(const auto& key:OrderedCharacterKeys(keys)){
                const auto count=targets.at(key);
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
    // Keep credentials only for the current form; do not overwrite a typed
    // credential or label by opening a different account's edit.
    if(!editing.empty()||key[0]||label[0]){
        UI::SameLineIfActionFits("Cancel edit");
        const bool cancel=UI::AvailableActionButton("Cancel edit",UI::ButtonRole::Standard,!busy.load(),"Validation in progress.");
        if(UI::DiscardRequested("Discard account input?",cancel,key[0]!=0||label[0]!=0,busy.load(),
            "Discard the current label and any entered API key?")){
            editing.clear();label[0]=0;SecureZeroMemory(key,sizeof(key));
        }
    }
    for(const auto& a:v->accounts){
        ImGui::PushID(a.id.c_str());ImGui::Separator();ImGui::TextUnformatted(Display(a).c_str());
        if(!a.label.empty())ImGui::TextDisabled("%s",a.name.c_str());
        std::string missing;for(const auto& p:recommended)if(!a.permissions.count(p)){if(!missing.empty())missing+=", ";missing+=p;}
        if(missing.empty())ImGui::TextUnformatted("All recommended permissions granted");
        else ImGui::TextWrapped("Missing: %s",missing.c_str());
        const bool canEdit=!busy.load()&&(editing==a.id||(!key[0]&&!label[0]&&editing.empty()));
        if(UI::AvailableActionButton("Edit",UI::ButtonRole::Standard,canEdit,"Finish or cancel the current account form first.")&&editing!=a.id){
            editing=a.id;snprintf(label,sizeof(label),"%s",a.label.c_str());SecureZeroMemory(key,sizeof(key));
        }
        ImGui::SameLine();if(EnabledAction("Unlink",!busy.load(),UI::ButtonRole::Destructive)){removing=a.id;ImGui::OpenPopup("Unlink account?");}
        if(UI::BeginConfirmationModal("Unlink account?","Unlink account")){
            ImGui::TextWrapped("Remove %s and its stored credential? Saved Library data is unaffected.",Display(a).c_str());
            const bool valid=removing==a.id;
            const auto action=UI::ConfirmCancel("unlink",valid,"Unlink account","Account changed; review again.",nullptr,busy.load());
            if(action==UI::EditAction::Save){Remove(removing);removing.clear();ImGui::CloseCurrentPopup();}
            else if(action==UI::EditAction::Cancel||UI::EditorEscape()){removing.clear();ImGui::CloseCurrentPopup();}
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
std::set<std::string> GuildExpandedGroups(){return guildExpandedGroups;}
void SetGuildExpandedGroups(const std::set<std::string>& value){
    guildExpandedGroups.clear();
    for(const auto& key:value)if(!key.empty()&&key.size()<=1024&&guildExpandedGroups.size()<4096)guildExpandedGroups.insert(key);
}
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
        const auto runAccount=[&]{
            if(AccountStoragePlan(*v,a,requested,includeWallet,Now()).empty())return false;
            last=now;Refresh({a},v->credentialEpoch,requested,true,{}, {},includeWallet,true);return true;
        };
        if(requested=="Wallet"){if(runAccount())return;continue;}
        if(requested=="Guild bank"){if(run("Guild access"))return;continue;}
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
        if((items||RefreshEngine::SharedAccountStorage(requested))&&runAccount())return;
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
            const char* guildSorts[]={"Name","Quantity"};
            ImGui::SetNextItemWidth(120.f);UI::Combo("##guild_sort",&nav.guildSort,guildSorts,2);
        }
        const auto searchWords=AlterEgo::Search::Words(q);
        if(subview==1){
            for(const auto& a:accounts){
                if(multi&&all)ImGui::TextUnformatted(Display(a).c_str());
                // Retrieval status belongs exclusively to the shared footer,
                // including per-account failures and stale timestamps in its tooltip.
                ImGui::PushID(a.id.c_str());
                std::vector<const WalletRow*> visible;
                for(const auto& w:v->inventory.wallet)if(w.account==a.id&&Match(w.name,w.id,searchWords)&&(!pinFilter||walletPins.count(w.id))&&(!balanceFilter||w.value>0))visible.push_back(&w);
                std::stable_sort(visible.begin(),visible.end(),[&](const WalletRow* a,const WalletRow* b){
                    const bool fa=walletPins.count(a->id),fb=walletPins.count(b->id);
                    if(fa!=fb)return fa;
                    return a->order==b->order?a->id<b->id:a->order<b->order;
                });
                const float pitch=UI::WalletRowHeight();
                UI::CatalogLines(static_cast<int>(visible.size()),pitch,0.f,[&](int rowIndex){
                    const auto& w=*visible[rowIndex];ImGui::PushID(static_cast<int>(w.id));
                    const auto p=ImGui::GetCursorScreenPos();
                    UI::CatalogRowBackground(rowIndex,p,ImVec2(p.x+ImGui::GetContentRegionAvail().x,p.y+pitch));
                    const bool pinned=walletPins.count(w.id)!=0;
                    if(UI::PinButton("##currency_pin",pinned,"Pin currency","Unpin currency",pitch)){
                        if(pinned)walletPins.erase(w.id);else walletPins.insert(w.id);
                        if(saveSettings)saveSettings();
                    }
                    ImGui::SameLine();
                    const std::string label=w.name.empty()?"Currency #"+std::to_string(w.id):w.name;
                    const auto balance=std::to_string(w.value);
                    const float amountWidth=w.id==1?CoinAmountWidth(w.value):ImGui::CalcTextSize(balance.c_str()).x;
                    UI::WalletCatalogLabel(0x40000000u|w.id,w.icon,label,w.description,amountWidth);
                    ItemMenu(w.id,label,{}, {},true);
                    ImGui::SameLine(0,4.f);ImGui::AlignTextToFramePadding();
                    if(w.id==1)RenderCoins(w.value);
                    else ImGui::TextUnformatted(balance.c_str());
                    ImGui::PopID();
                });
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
            bool anyGuild=false;
            for(const auto& id:shown){
                auto it=v->inventory.guilds.find(id);if(it==v->inventory.guilds.end())continue;
                const auto& g=it->second;
                std::map<uint32_t,std::pair<const Row*,int64_t>> groups;
                for(const auto& r:g.rows)if(Match(r.name,r.slot.id,searchWords)){
                    auto& x=groups[r.slot.id];x.first=&r;x.second+=r.slot.count;
                }
                if(!searchWords.empty()&&groups.empty())continue;
                anyGuild=true;
                ImGui::PushID(id.c_str());
                const auto title=(g.name.empty()?id:g.name)+" ["+g.tag+"]";
                const bool forced=!searchWords.empty();
                const bool open=GroupBrowse::Expanded(guildExpandedGroups,id,false,forced);
                if(UI::CatalogBranchRow(0,open,title)&&GroupBrowse::Toggle(guildExpandedGroups,id,forced)){
                    if(saveSettings)saveSettings();
                }
                if(open){
                    ImGui::Indent(14.f);
                    ImGui::TextDisabled("%s%s",g.status.c_str(),Due("Guild bank",0,g.success,Now())?" (stale)":"");
                    ImGui::TextUnformatted("Shared coins:");ImGui::SameLine();RenderCoins(g.coins);
                    std::vector<uint32_t> ordered;
                    for(const auto& [item,x]:groups)ordered.push_back(item);
                    std::sort(ordered.begin(),ordered.end(),[&](uint32_t a,uint32_t b){
                        const auto& x=groups.at(a);const auto& y=groups.at(b);
                        if(nav.guildSort==1&&x.second!=y.second)return x.second>y.second;
                        const auto xn=x.first->name.empty()?"Item #"+std::to_string(a):x.first->name;
                        const auto yn=y.first->name.empty()?"Item #"+std::to_string(b):y.first->name;
                        return xn!=yn?xn<yn:a<b;
                    });
                    UI::CatalogRows(static_cast<int>(ordered.size()),[&](int i){
                        const auto item=ordered[i];const auto& x=groups.at(item);const auto& row=*x.first;
                        const auto name=row.name.empty()?"Item #"+std::to_string(item):row.name;
                        const auto meta=v->inventory.metadata.find(item);
                        const auto current=GW2API::GetItemInfo(item);
                        ImGui::PushID(static_cast<int>(item));
                        UI::CatalogRow(i,false,item,current?current->icon_url:meta==v->inventory.metadata.end()?"":meta->second.icon,name,row.rarity,std::to_string(x.second),false,-1,[&]{if(!row.name.empty())UI::DetailTooltip(name.c_str(),meta==v->inventory.metadata.end()?nullptr:PlainItemDescription(meta->second.description).c_str(),GetSkinRarityColor(row.rarity));});
                        // Shared guild stock has no personal character-load targets.
                        ItemMenu(item,name,row.chat);ImGui::PopID();
                    });
                    if(groups.empty())ImGui::TextDisabled("No matching cached items.");
                    ImGui::Unindent(14.f);
                }
                ImGui::PopID();
            }
            if(!shown.empty()&&!anyGuild&&!searchWords.empty())ImGui::TextDisabled("No matching cached guild items.");
            if(shown.empty())ImGui::TextDisabled("No verified guild bank access.");
        }
        return;
    }
    const UI::SplitPanel split(listWidth,420.f,200.f,240.f);
    AlterEgo::UI::BeginChild("InventoryList",ImVec2(split.left,0),true);
    ProjectCharacters(characterFilter,v->bindings,v->credentialEpoch);
    const auto previousQuery=query;
    UI::SearchToolbar("native_inventory","Search items, currencies or ID",query,
        [&]{
            UI::FilterChips c;
            for(const auto& [ref,selection]:characterFilter)
                c.push_back({"character/"+ref.account+"/"+std::to_string(ref.id),CharacterFilterLabel(*v,selection),
                    [ref]{navigation.characters.erase(ref);}});
            if(!scope.empty())c.push_back({"scope",scope,[&]{scope.clear();}});
            AddFacetChips(c,"binding",binding);AddFacetChips(c,"rarity",rarity);
            AddFacetChips(c,"type",type);
            if(freshness)c.push_back({"freshness",freshness==1?"Fresh":"Stale / failed",[&]{freshness=0;}});
            if(minQuantity)c.push_back({"quantity","Min "+std::to_string(minQuantity),[&]{minQuantity=0;}});
            if(transfer)c.push_back({"transfer","Potentially transferable",[&]{transfer=0;}});
            return c;
        },
        [&]{
            std::vector<CharacterIdentity::Address> candidates;
            for(const auto& a:accounts){
                const auto it=v->bindings.find(a.id);
                if(it!=v->bindings.end())for(const auto& [name,b]:it->second)
                    candidates.push_back({a.id,name,b,v->credentialEpoch});
            }
            RenderCharacterMultiFilter("Character",characterFilter,candidates);
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
    // Preserve typed selection while it still matches the rebuilt projection.
    if(query!=previousQuery)groupRevealPending=false;
    const char* sorts[]={"Name","Quantity"};ImGui::SetNextItemWidth(120);UI::Combo("##inventory_sort",&sort,sorts,2);
    auto tokens=Skinventory::Search::Words(query);
    // Explicit Find in Inventory navigation can reveal even an unusually short ID.
    const bool canSearch=Skinventory::Search::HasMinimumQuery(tokens)||
        (nav.item!=0&&query==std::to_string(nav.item));
    const auto selectedCharacters=ProjectCharacters(characterFilter,v->bindings,v->credentialEpoch);
    const bool canBrowse=CanBrowseItems(selectedCharacters.restricted,!scope.empty(),
        !binding.empty(),!rarity.empty(),!type.empty(),freshness!=0,minQuantity>0,transfer!=0);
    const bool walletEligible=WalletMatchesSelection(scope,selectedCharacters.restricted,binding,rarity,type,transfer!=0);
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
    Json selectionKey=Json::array();
    for(const auto& [ref,selection]:characterFilter)selectionKey.push_back(CharacterIdentity::EncodeSelection(selection));
    const auto filterKey=Json::array({showResults,selectedAccount,tokens,scope,binding,rarity,type,sort,selectionKey,freshness,minQuantity,transfer,(freshness||transfer)?Now()/10:0}).dump();
    if(previous!=v||prevKey!=filterKey){
        previous=v;prevKey=filterKey;groups.clear();++projectionRevision;std::map<SearchIdentity,size_t> index;
        if(showResults)for(const auto& row:v->inventory.rows){
            const auto& s=row.slot;
            if(!allowed(s.account)||(!scope.empty()&&scope!=s.scope)||!MatchesFacet(binding,BindingName(s.binding))||!MatchesFacet(rarity,row.rarity)||!MatchesFacet(type,row.type)||!Match(row.name,s.id,tokens))continue;
            if(!selectedCharacters.Matches(s.account,s.character))continue;
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
    // A temporarily missing identity group is not permission to select another owner.
    if(selected==groups.end()&&nav.group.rfind("identity:",0)!=0){nav.item=0;nav.currency=false;nav.group.clear();groupRevealPending=false;}

    // Derive group buckets from the same filtered aggregate, not a second scan
    // or filter implementation. Rebuild only on a projection/character-order change.
    static GroupTree tree;
    static uint64_t treeProjection=0,treeRevision=0;
    static std::vector<CharacterOrder::Key> treeCharacterOrder;
    static std::vector<std::string> treeAccountOrder;
    static uint64_t candidateProjection=0;
    static std::set<CharacterOrder::Key> candidateCharacters;
    static std::map<std::pair<size_t,std::string>,int64_t> groupCounts;
    static std::map<size_t,std::string> firstGroup;
    static std::vector<GroupLine> visibleLines;
    static uint64_t visibleRevision=0;
    static std::set<std::string> visibleExpansion;
    static bool visibleForced=false;
    const bool forceExpand=!tokens.empty();
    if(nav.grouped){
        if(candidateProjection!=projectionRevision){
            candidateProjection=projectionRevision;candidateCharacters.clear();
            for(const auto& g:groups)for(const auto* r:g.locations)
                if(!r->slot.character.empty())candidateCharacters.emplace(r->slot.account,r->slot.character);
        }
        const auto characterOrder=OrderedCharacterKeys(candidateCharacters);
        const auto accountOrderKeys=OrderedCharacterAccounts();
        if(treeProjection!=projectionRevision||treeCharacterOrder!=characterOrder||treeAccountOrder!=accountOrderKeys){
            treeProjection=projectionRevision;treeCharacterOrder=characterOrder;treeAccountOrder=accountOrderKeys;++treeRevision;
            tree.Clear();groupCounts.clear();firstGroup.clear();visibleLines.clear();
            std::map<GroupPath,std::vector<GroupLeaf>> buckets;
            for(size_t i=0;i<groups.size();++i){
                const auto& g=groups[i];std::map<GroupPath,int64_t> counts;
                for(const auto* r:g.locations){const auto& s=r->slot;counts[IdentityGroupPath(*v,s.account,s.character,s.scope)]+=s.count;}
                for(const auto* w:g.balances)counts[{w->account,{},"Wallet"}]+=w->value;
                for(const auto& [path,count]:counts)buckets[path].push_back({i,count});
            }
            std::map<std::string,size_t> accountOrder;
            std::map<std::string,std::string> accountLabels;
            for(const auto& id:accountOrderKeys)accountOrder.emplace(id,accountOrder.size());
            for(const auto& a:accounts){accountOrder.emplace(a.id,accountOrder.size());accountLabels[a.id]=Display(a);}
            const auto characterRank=CharacterOrder::Ranks(characterOrder);
            std::vector<GroupPath> paths;
            for(const auto& [path,leaves]:buckets)paths.push_back(path);
            std::sort(paths.begin(),paths.end(),[&](const GroupPath& a,const GroupPath& b){
                if(a.account!=b.account)return accountOrder.at(a.account)<accountOrder.at(b.account);
                if(a.character.empty()!=b.character.empty())return a.character.empty();
                if(a.character!=b.character){
                    return characterRank.at({a.account,a.character})<characterRank.at({b.account,b.character});
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
        if(nav.item&&selected!=groups.end()){
            const size_t i=static_cast<size_t>(selected-groups.begin());
            if(!groupCounts.count({i,nav.group})&&nav.group.rfind("identity:",0)!=0){
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
    AlterEgo::UI::BeginChild("InventoryListScroll",ImVec2(0,0),false);
    if(nav.grouped&&groupRevealPending&&nav.item&&selected!=groups.end()){
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
            auto hit=UI::CatalogRow(rowIndex,on,0x40000000u|group.id,w.icon,group.name,"",quantityText(group.id,true,count),false,-1,[&]{if(!w.name.empty())UI::DetailTooltip(w.name.c_str(),PlainItemDescription(w.description).c_str());});
            select(hit.clicked);ItemMenu(group.id,group.name,{}, {},true);
            ImGui::PopID();ImGui::PopID();return;
        }
        const auto& row=*group.locations.front();
        const auto name=group.name.empty()?"Item #"+std::to_string(group.id):group.name;
        const auto* meta=metaFor(group.id);
        const auto current=GW2API::GetItemInfo(group.id);
        ImGui::PushID(static_cast<int>(group.id));
        auto hit=UI::CatalogRow(rowIndex,on,group.id,current?current->icon_url:meta?meta->icon:"",name,row.rarity,std::to_string(count),false,-1,[&]{if(!group.name.empty())UI::DetailTooltip(name.c_str(),meta?PlainItemDescription(meta->description).c_str():nullptr,GetSkinRarityColor(row.rarity));});
        select(hit.clicked);
        const bool popup=ImGui::IsPopupOpen("##inventory_item_actions")||ImGui::IsItemClicked(ImGuiMouseButton_Right);
        std::map<std::pair<std::string,std::string>,int64_t> targets;
        if(popup&&FastLoadActionsEnabled()){
            if(!nav.grouped)targets=loadTargets(group.id);
            else for(const auto* location:group.locations){
                const auto& slot=location->slot;
                if(!slot.character.empty()&&GroupKey(IdentityGroupPath(*v,slot.account,slot.character,slot.scope))==path)
                    targets[{slot.account,slot.character}]+=slot.count;
            }
        }
        ItemMenu(group.id,name,row.chat,targets);
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
                const bool open=GroupBrowse::Expanded(expandedInventoryGroups,node.key,false,forceExpand);
                if(UI::CatalogBranchRow(i,open,node.label,decorate)&&GroupBrowse::Toggle(expandedInventoryGroups,node.key,forceExpand))expansionChanged=true;
                if(!node.character.empty())UI::EntityContext("##inventory_character_group",[&]{
                    RenderCharacterIdentityMenuActions(node.character,node.account,true,"Copy Character Name");
                });
            }
            ImGui::PopID();
            if(indent>0)ImGui::Unindent(indent);
        });
        if(expansionChanged)saveSettings();
    }
    ImGui::EndChild();ImGui::EndChild();
    if(split.Divider("##inventory_split",listWidth))saveSettings();
    AlterEgo::UI::BeginChild("InventoryDetailPanel",ImVec2(0,0),true);
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
    const auto definition=GW2API::GetItemInfo(group.id);
    const auto name=definition&&!definition->name.empty()?definition->name:group.name.empty()?"Item #"+std::to_string(group.id):group.name;
    const auto* meta=metaFor(group.id);
    const auto& itemRarity=definition?definition->rarity:representative.rarity;
    const auto& itemType=definition?definition->type:representative.type;
    UI::CatalogBanner(name,itemRarity+"  /  "+itemType,itemRarity);
    std::vector<uint32_t> definitionDemand{group.id},statDemand;
    ImGui::PushID(static_cast<int>(group.id));ItemMenu(group.id,name,representative.chat,loadTargets(group.id));ImGui::PopID();
    ImGui::TextDisabled("ID: %u",group.id);
    int64_t total=0;const auto locations=v->index.find(group.id);
    if(locations!=v->index.end())for(auto idx:locations->second)if(allowed(v->inventory.rows[idx].slot.account))total+=v->inventory.rows[idx].slot.count;
    ImGui::Text("Total in account scope: %lld",static_cast<long long>(total));
    if(total!=group.count)ImGui::TextDisabled("Matching filters: %lld",static_cast<long long>(group.count));
    if(groupQuantity!=groupCounts.end())ImGui::TextDisabled("Selected group: %lld",static_cast<long long>(groupQuantity->second));
    RenderSectionHeader("Locations",UI::SectionGold);
    if(locations!=v->index.end())for(auto idx:OrderedLocationRows(*v,locations->second)){
        const auto& row=v->inventory.rows[idx];const auto& slot=row.slot;if(!allowed(slot.account))continue;
        ImGui::PushID(static_cast<int>(idx));
        auto label=LocationLabel(slot);if(multi)for(const auto& a:v->accounts)if(a.id==slot.account){label=Display(a)+" / "+label;break;}
        label+=" x"+std::to_string(slot.count);
        ImGui::Selectable((label+"###location").c_str(),false);
        if(ImGui::BeginPopupContextItem("##location_actions")){
            if(!slot.character.empty()){
                const auto heading=UI::MenuObjectHeading("Character",slot.character);
                UI::MenuSection characterSection("character",heading.c_str());
                RenderCharacterIdentityMenuActions(slot.character,slot.account);
            }
            {
                const auto heading=UI::MenuObjectHeading("Item",name);
                UI::MenuSection itemSection("item",slot.character.empty()?nullptr:heading.c_str(),!slot.character.empty());
                CopyNameMenuItem(name);
                if(!row.chat.empty())CopyNativeLinkMenuItem(row.chat);
                else CopyEntityChatLink(AlterEgo::LINK_ITEM,group.id);
            }
            ImGui::EndPopup();
        }
        ImGui::TextDisabled("%s%s",BindingName(slot.binding),Fresh(*v,row)?"":" | Stale or failed");
        if(!slot.boundTo.empty())ImGui::TextWrapped("Bound to %s",slot.boundTo.c_str());
        if(slot.charges>=0)ImGui::Text("Remaining charges: %lld",static_cast<long long>(slot.charges));
        if(slot.stat>0&&slot.stat<=UINT32_MAX){
            const auto id=static_cast<uint32_t>(slot.stat);statDemand.push_back(id);
            const auto stat=GW2API::GetItemStatInfo(id);
            if(stat&&!stat->name.empty())ImGui::TextWrapped("Selected stat: %s",stat->name.c_str());
            else ImGui::TextDisabled("Selected stat ID: %u",id);
            if(ImGui::IsItemHovered()&&stat){
                ImGui::BeginTooltip();RenderSelectedItemStatDetails(group.id,id);ImGui::EndTooltip();
            }
        }
        const bool glyph=slot.scope=="Equipment"&&(slot.position.find("/Pick")!=std::string::npos||slot.position.find("/Axe")!=std::string::npos||slot.position.find("/Sickle")!=std::string::npos);
        if(glyph&&slot.upgrades.empty())ImGui::TextDisabled("Glyph: Unknown (not reported)");
        const auto socket=[&](const char* label,uint32_t id,int index){
            if(!id)return;
            definitionDemand.push_back(id);const auto item=GW2API::GetItemInfo(id);
            const auto name=item&&!item->name.empty()?item->name:std::to_string(id);
            ImGui::PushID(label);ImGui::PushID(index);
            ImGui::TextWrapped("%s: %s",label,name.c_str());
            if(item&&ImGui::IsItemHovered())RenderInstalledUpgradeTooltip(*item);
            UI::EntityContext("##inventory_socket",[&]{CopyEntityMenu(item?item->name:std::string{},LINK_ITEM,id);});
            ImGui::PopID();ImGui::PopID();
        };
        for(size_t i=0;i<slot.upgrades.size();++i)socket(glyph?"Glyph":"Upgrade",slot.upgrades[i],static_cast<int>(i));
        for(size_t i=0;i<slot.infusions.size();++i)socket("Infusion",slot.infusions[i],static_cast<int>(i));
        ImGui::Separator();ImGui::PopID();
    }
    // Only the selected item and its displayed sockets demand public definitions.
    // Do not refresh authenticated storage or list/filter results.
    std::sort(definitionDemand.begin(),definitionDemand.end());
    definitionDemand.erase(std::unique(definitionDemand.begin(),definitionDemand.end()),definitionDemand.end());
    GW2API::FetchItemDetailsAsync(definitionDemand);
    if(!statDemand.empty())GW2API::FetchItemStatDetailsAsync(statDemand);
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
            RenderItemMarketQuote(p,group.id,false,false);
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
                if(ImGui::IsItemHovered())UI::Hint("Potentially sellable items in fresh storage at the best cached buy price. Bound and unverified stock excluded. Order depth and actual fees may differ.");
            };
            renderEstimate("All locations",all);
            if(all.count!=matching.count)renderEstimate("Matching filters",matching);
        }else if(status.price)ImGui::TextDisabled("Not tradeable");
        else ImGui::TextDisabled("Price not retrieved");
        if(status.pending||status.retry_at>now||(status.price&&(now<status.price->fetched_at_unix||now-status.price->fetched_at_unix>=Commerce::PriceTTL)))
            ImGui::TextDisabled("%s",Skinventory::DescribePriceStatus(status.price,status.pending,status.retry_at,now,Commerce::PriceTTL).c_str());
        if(status.pending){ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true);ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*0.5f);}
        if(UI::Button("Refresh price",ImVec2(0,UI::ActionButtonHeight()))&&!status.pending)Commerce::RequestItemPrices({group.id},true);
        if(status.pending){UI::DisabledHint("Price request queued or refreshing.");ImGui::PopStyleVar();ImGui::PopItemFlag();}
        RefreshUI::Timestamp(status.price?static_cast<std::time_t>(status.price->fetched_at_unix):0,"Trading Post price");
    }
    RenderSectionHeader("Item Information",UI::SectionGold);
    auto flags=v->inventory.flags.find(group.id);
    if(flags!=v->inventory.flags.end()){
        if(flags->second.accountBound)ImGui::TextUnformatted("Account bound on acquire");
        if(flags->second.soulbound)ImGui::TextUnformatted("Character bound on acquire");
        if(flags->second.bindOnUse)ImGui::TextUnformatted("Binds on use");
    }
    if(definition){
        RenderItemDefinitionDetails(*definition,TooltipCore::ItemAll,false);
        RenderItemVendorValue(*definition);
    }
    else if(meta&&meta->complete){
        // Retain a verified snapshot fallback without making it a competing owner.
        ItemInfo prior;prior.type=itemType;prior.rarity=itemRarity;prior.description=meta->description;prior.details=meta->details;
        RenderItemDefinitionDetails(prior,TooltipCore::ItemAll,false);
    }else ImGui::TextDisabled("Detailed metadata not retrieved.");
    ImGui::EndChild();
}
void RenderInventoryStatusRow(const std::string& selectedAccount){
    auto v=Snapshot();const auto accounts=SelectedAccounts(*v,selectedAccount);
    const auto& nav=navigation;
    const auto scope=nav.subview==1?std::string("Wallet"):nav.subview==2?std::string("Guild bank"):nav.scope.empty()?std::string("Items"):nav.scope;
    const bool itemView=nav.subview==0;
    const bool characterFiltered=itemView&&!nav.characters.empty();
    const bool includeWallet=itemView&&WalletMatchesSelection(nav.scope,characterFiltered,nav.binding,nav.rarity,nav.type,nav.transfer!=0);
    auto selections=nav.characters;
    const auto characters=characterFiltered?ProjectCharacters(selections,v->bindings,v->credentialEpoch):CharacterFilter{};
    const auto matches=[&](const std::string& account,const std::string& dataset,const std::string& name){
        return MatchesStorageView(scope,{},dataset,name)&&characters.Matches(account,name);
    };
    namespace R=RefreshStatus;
    const auto now=Now();
    const auto activeAccount=CharacterActivity::ActiveKey().first;
    const auto autoAccount=selectedAccount.empty()&&v->accounts.size()>1?activeAccount:selectedAccount;
    const bool allAuto=!(selectedAccount.empty()&&v->accounts.size()>1);
    R::Aggregate aggregate;
    std::map<ScopeKey,ScopeState> progress;std::string current;unsigned processed=0;
    std::optional<ScopeKey> active,pending;
    {std::lock_guard<std::mutex> lock(viewMutex);if(progressEpoch==v->credentialEpoch){
        progress=liveScopes;current=activeScope;processed=processedScopes;
        active=activeScopeKey;pending=pendingScopeKey;}}
    RefreshStatus::WorkPhases<BrokerKey> sharedWork;
    {std::lock_guard<std::mutex> lock(brokerStatusMutex);sharedWork=brokerPhases;}
    const bool operationBusy=busy.load();
    const auto phaseFor=[&](const ScopeKey& key){
        const auto shared=sharedWork.Get({v->credentialEpoch,key});
        if(shared!=R::Phase::Idle)return shared;
        return active&&*active==key?R::Phase::Working:pending&&*pending==key?R::Phase::Queued:R::Phase::Idle;
    };
    std::string detail;
    const auto mark=[&](const Account& a,const std::string& dataset,const std::string& ch={},RefreshProgress::Counts* group=nullptr){
        const ScopeKey key{a.id,dataset,ch};
        auto it=v->scopes.find(key);auto live=progress.find(key);
        const auto* state=live!=progress.end()?&live->second:it!=v->scopes.end()?&it->second:nullptr;
        auto inspected=InspectStorageScope(a.id,dataset,ch,state,ScopePermitted(a,dataset),now,phaseFor(key));
        inspected.demandAllowed=allAuto||(!autoAccount.empty()&&a.id==autoAccount);
        aggregate.Add(inspected,now);
        const auto kind=R::Project(inspected,now);
        if(group){
            const char* label=kind==R::Kind::Denied?"Permission denied":kind==R::Kind::Working?"Refreshing":
                kind==R::Kind::Queued?"Queued":inspected.failed?"Failed":inspected.success<=0?"Not retrieved":
                inspected.fresh?"Ready":"Stale";
            group->Add(label,inspected.success>0);
        }else{
            if(!detail.empty())detail+="\n";
            detail+=(accounts.size()>1?Display(a)+" / ":"")+dataset+(ch.empty()?"":" / "+ch)+": "+R::Describe(inspected,now);
        }
        return inspected;
    };
    std::map<std::string,R::Scope> guilds;
    for(const auto& a:accounts){
        if(scope=="Wallet"){if(!itemView||includeWallet)mark(a,"Wallet");continue;}
        if(scope=="Items"&&includeWallet)mark(a,"Wallet");
        if(scope=="Guild bank"){
            const auto accessStatus=mark(a,"Guild access");auto access=v->inventory.guildAccess.find(a.id);
            if(access!=v->inventory.guildAccess.end()&&access->second.empty()&&accessStatus.success>0)
                detail+="; no accessible guilds in retained roster";
            if(access!=v->inventory.guildAccess.end())for(const auto& id:access->second){
                auto it=guilds.find(id);
                if(it==guilds.end()||(it->second.access!=R::Access::Allowed&&accessStatus.access==R::Access::Allowed)||
                   (accessStatus.access==R::Access::Allowed&&accessStatus.demandAllowed&&
                   (!it->second.demandAllowed||accessStatus.eligibility.due||
                    (accessStatus.eligibility.at&&accessStatus.eligibility.at<it->second.eligibility.at))))
                    guilds[id]=accessStatus;
            }
            continue;
        }
        for(const auto* dataset:{"Bank","Shared inventory","Materials","Legendary Armory","Delivery box"})
            if(matches(a.id,dataset,{}))mark(a,dataset);
        if(scope=="Items"||scope=="Bags"||scope=="Equipment"){
            auto roster=v->inventory.rosters.find(a.id);
            if(roster==v->inventory.rosters.end()){
                mark(a,"Roster");aggregate.missing=true;continue;
            }
            for(const auto* dataset:{"Bags","Equipment"}){
                if(scope!="Items"&&scope!=dataset)continue;
                RefreshProgress::Counts group;
                R::Aggregate groupCoverage;
                if(characterFiltered){
                    std::set<CharacterOrder::Key> keys;
                    for(const auto& ch:roster->second)if(matches(a.id,dataset,ch))keys.emplace(a.id,ch);
                    for(const auto& key:OrderedCharacterKeys(keys))mark(a,dataset,key.second);
                }else{
                    for(const auto& ch:roster->second)
                        if(matches(a.id,dataset,ch))groupCoverage.Add(mark(a,dataset,ch,&group),now);
                }
                if(!characterFiltered){
                    if(!detail.empty())detail+="\n";
                    detail+=(accounts.size()>1?Display(a)+" / ":"")+dataset+": "+group.Summary()+R::OldestGroupRetrieval(groupCoverage,now);
                    if(roster->second.empty())detail+=" (verified empty roster)";
                }
            }
        }
    }
    for(const auto& [id,access]:guilds){
        auto it=v->inventory.guilds.find(id);
        ScopeState state{id,"Guild bank",{},"Not retrieved",0,0};
        std::string name=id;
        if(it!=v->inventory.guilds.end()){
            state.status=it->second.status;state.success=it->second.success;state.attempted=it->second.attempted;
            if(!it->second.name.empty())name=it->second.name;
        }
        auto inspected=InspectStorageScope(id,"Guild bank",{},&state,access.access==R::Access::Allowed,now,
            phaseFor(ScopeKey{id,"Guild bank",{}}));
        inspected.demandAllowed=access.demandAllowed;
        // Guild access is the admission gate for a subsequent stash retrieval.
        if(inspected.eligibility.due&&!access.eligibility.due){
            inspected.eligibility=access.eligibility;
            if(inspected.eligibility.reason==R::WaitReason::Current)inspected.eligibility.reason=R::WaitReason::ActiveInterval;
        }
        aggregate.Add(inspected,now);
        detail+="\n"+name+": "+R::Describe(inspected,now);
    }
    if(!guilds.empty())detail+="\nGuild access controls automatic stash retrieval.";
    auto kind=accounts.empty()?R::Kind::NoAPI:aggregate.Status();
    if(kind==R::Kind::Working||kind==R::Kind::Queued){
        if(operationBusy&&((active&&phaseFor(*active)!=R::Phase::Idle)||(pending&&phaseFor(*pending)!=R::Phase::Idle)))
            detail=std::to_string(processed)+" storage scopes processed"+(current.empty()?"":"\nCurrent operation: "+current)+"\n\n"+detail;
        else detail="Matching shared endpoint work is pending or active.\n\n"+detail;
    }else if(operationBusy)detail+="\nA storage/account operation is in progress; no matching active scope confirmed.";
    if(!aggregate.count&&characterFiltered)detail="No verified storage scopes match the selected characters and storage filter.";
    else if(!aggregate.count&&!accounts.empty())detail+="\nNo storage scopes in the current selection.";
    if(characterFiltered&&aggregate.count)detail+="\n\nTimestamp and manual Refresh cover matching character storage only.";
    if(itemView&&nav.item&&!nav.currency){
        const auto p=Skinventory::Commerce::GetItemPriceStatus(nav.item);
        detail+="\n\nTrading Post price (selected item): ";
        detail+=Skinventory::DescribePriceStatus(p.price,p.pending,p.retry_at,now,Skinventory::Commerce::PriceTTL);
        if(p.price)detail+="; updated "+RefreshUI::Age(static_cast<std::time_t>(p.price->fetched_at_unix),now);
        if(!AutoRefreshPolicy::Enabled(AutoRefreshPolicy::Dataset::TradingPrices))detail+="; automatic refresh disabled";
        detail+="\nPrice refresh is independent of inventory storage.";
    }
    if(accounts.empty())detail="Link or select an account.";
    const bool incompatible=(characterFiltered&&(characters.addresses.empty()||(scope!="Items"&&scope!="Bags"&&scope!="Equipment")))||(itemView&&scope=="Wallet"&&!includeWallet);
    const auto label=R::Label(kind,now,aggregate.retryAt);
    const char* disabledReason=accounts.empty()?"Link or select an account.":incompatible?"The current storage and character filters are incompatible.":
        operationBusy?"A storage or account operation is already in progress.":nullptr;
    if(RefreshUI::Row("native_inventory_refresh",aggregate,"inventory scope",
        RefreshUI::StatusState(kind),detail,operationBusy||accounts.empty()||incompatible,nullptr,label.c_str(),true,now,disabledReason))
        Refresh(accounts,v->credentialEpoch,scope,false,{},characters,includeWallet);
}

}
