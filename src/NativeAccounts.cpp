#include "NativeAccounts.h"
#include "NativeInventoryStore.h"
#include "ArenaNetTransport.h"
#include "BackgroundWorkers.h"
#include "AddonShared.h"
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
struct View {std::vector<Account> accounts;Projection inventory;};
std::string root;
std::mutex viewMutex;
std::shared_ptr<const View> view=std::make_shared<View>();
std::atomic<bool> busy{false};
std::string message;
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
std::filesystem::path AccountPath(){return Storage::Paths(root).ConfigDirectory()/"accounts.db";}
void InitAccounts(Database::Connection& db){
    Database::Migrate(db,0x41454143,1,[](Database::Connection& c,int){
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
void Publish(const std::string& msg){
    auto next=std::make_shared<View>();next->accounts=Accounts();next->inventory=Read(root);
    std::lock_guard<std::mutex> lock(viewMutex);view=std::move(next);message=msg;
}
std::shared_ptr<const View> Snapshot(){std::lock_guard<std::mutex> lock(viewMutex);return view;}
void Message(const std::string& msg){std::lock_guard<std::mutex> lock(viewMutex);message=msg;}
std::string Status(){std::lock_guard<std::mutex> lock(viewMutex);return message;}
template<class Job> void Launch(Job job){
    if(busy.exchange(true))return;
    Message("Working...");
    Background::Workers().Launch([job=std::move(job)]{
        try{job();}catch(...){Message("Operation failed; previous verified data retained.");}
        busy=false;
    },[]{Message("Operation cancelled.");busy=false;});
}
int64_t Now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string Error(int code){
    if(code==429)return "Rate limited; retry later";
    if(code==401||code==403)return "Invalid key or missing permission";
    if(code==0)return "Transport failed or cancelled";
    return "HTTP "+std::to_string(code);
}
Json Fetch(const std::string& path,const std::string& key){
    auto r=ArenaNet::Get(path,key);
    if(r.status_code!=200)throw std::runtime_error(Error(r.status_code));
    return Json::parse(r.body);
}
std::string Encode(const std::string& s){
    constexpr char hex[]="0123456789ABCDEF";std::string out;
    for(unsigned char c:s){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')out+=static_cast<char>(c);
        else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}
    return out;
}
std::set<std::string> Roster(const Account& a,const std::string& key){
    auto j=Fetch("/v2/characters",key);if(!j.is_array())throw std::runtime_error("Invalid roster");
    std::set<std::string> names;for(const auto& n:j){auto name=n.get<std::string>();if(name.empty()||!names.insert(name).second)throw std::runtime_error("Invalid roster");}
    StoreRoster(root,a.id,names,Now());return names;
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
            std::filesystem::create_directories(AccountPath().parent_path());
            Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Create);InitAccounts(db);
            Database::Transaction tx(db);
            auto q=db.Prepare("INSERT INTO accounts VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,label=excluded.label,permissions=excluded.permissions");
            q.Bind(1,a.id);q.Bind(2,a.name);q.Bind(3,a.label);q.Bind(4,Json(a.permissions).dump());q.Execute();
            Secret previous(Key(a.id));
            if(!SaveKey(a.id,key->value)){Message("Windows Credential Manager rejected the key.");return;}
            try{tx.Commit();}catch(...){if(previous.value.empty())CredDeleteA(Target(a.id).c_str(),CRED_TYPE_GENERIC,0);else SaveKey(a.id,previous.value);throw;}
            if(a.permissions.count("characters")){try{Roster(a,key->value);}catch(...){}}
            Publish("Account linked. Inventory can now be refreshed.");
        }catch(const std::exception& e){
            // Never show arbitrary JSON exception text or HTTP response bodies.
            const std::string s=e.what();
            Message(s.rfind("HTTP ",0)==0||s=="Invalid key or missing permission"||s=="Transport failed or cancelled"||s=="Rate limited; retry later"?s:"Account validation or storage failed.");
        }
    });
}
void Remove(const std::string& id){
    Launch([id]{
        Database::Connection db(AccountPath().u8string(),Database::Connection::OpenMode::Existing);InitAccounts(db);
        Database::Transaction tx(db);auto q=db.Prepare("DELETE FROM accounts WHERE id=?");q.Bind(1,id);q.Execute();
        tx.Commit();
        const bool removed=CredDeleteA(Target(id).c_str(),CRED_TYPE_GENERIC,0)!=FALSE||GetLastError()==ERROR_NOT_FOUND;
        Publish(removed?"Account unlinked. Library unchanged.":"Account unlinked; remove its remaining credential in Windows Credential Manager.");
    });
}
void Refresh(const std::vector<Account>& accounts,const std::string& onlyScope){
    Launch([accounts,onlyScope]{
        std::set<uint32_t> ids;
        for(const auto& a:accounts){
            Background::Checkpoint();Secret key(Key(a.id));
            if(key.value.empty()){Message("Stored credential unavailable. Replace it in Account Management.");continue;}
            auto scope=[&](const std::string& name,const std::string& character,const std::string& path,bool bags){
                if(!onlyScope.empty()&&onlyScope!=name)return;
                Background::Checkpoint();
                ScopeState state{a.id,name,character,"Ready",Now(),0};
                const bool characterScope=bags||name=="Equipment";
                const bool permitted=name=="Delivery box"?a.permissions.count("tradingpost")!=0:
                    a.permissions.count("inventories")&&(!characterScope||a.permissions.count("characters"))&&
                    (name!="Legendary Armory"||a.permissions.count("unlocks"));
                if(!permitted){
                    state.status="Missing permissions";Record(root,state);return;
                }
                try{
                    auto j=Fetch(path,key.value);std::vector<Slot> slots;
                    if(bags){
                        const auto& b=j.at("bags");if(!b.is_array())throw std::runtime_error("Invalid bags");
                        size_t i=0;for(const auto& bag:b){auto prefix=std::to_string(i++)+"/";if(bag.is_null())continue;
                            auto rows=ParseArray(bag.at("inventory"),a.id,name,character,prefix);
                            slots.insert(slots.end(),rows.begin(),rows.end());}
                    }else if(name=="Equipment")slots=ParseEquipment(j,a.id,character);
                    else if(name=="Legendary Armory")slots=ParseArmory(j,a.id);
                    else if(name=="Delivery box")slots=ParseArray(j.at("items"),a.id,name,{},{},false);
                    else slots=ParseArray(j,a.id,name,character,{},name!="Materials");
                    state.success=Now();Record(root,state,&slots);
                    for(const auto& s:slots)ids.insert(s.id);
                }catch(const std::exception& e){
                    const std::string what=e.what();
                    state.status=what.rfind("HTTP ",0)==0||what=="Invalid key or missing permission"||what=="Transport failed or cancelled"||what=="Rate limited; retry later"?what:"Invalid or incomplete response";
                    Record(root,state);
                }
            };
            scope("Bank",{},"/v2/account/bank",false);
            scope("Shared inventory",{},"/v2/account/inventory",false);
            scope("Materials",{},"/v2/account/materials",false);
            scope("Legendary Armory",{},"/v2/account/legendaryarmory",false);
            scope("Delivery box",{},"/v2/commerce/delivery",false);
            if(onlyScope.empty()||onlyScope=="Bags"||onlyScope=="Equipment"){
                if(!a.permissions.count("characters")){
                    ScopeState failed{a.id,"Roster",{},"Missing permissions",Now(),0};Record(root,failed);
                }else try{
                    for(const auto& ch:Roster(a,key.value)){
                        const auto path="/v2/characters/"+Encode(ch);
                        scope("Bags",ch,path+"/inventory",true);
                        scope("Equipment",ch,path+"/equipment?v=latest",false);
                    }
                }catch(...){ScopeState failed{a.id,"Roster",{},"Roster retrieval failed",Now(),0};Record(root,failed);}
            }
        }
        // Resolve names in bounded batches. Missing metadata cannot discard verified slots.
        auto cached=Read(root);
        for(const auto& row:cached.rows)if(!row.name.empty())ids.erase(row.slot.id);
        std::string batch;unsigned count=0;
        auto flush=[&]{if(batch.empty())return;try{StoreNames(root,Fetch("/v2/items?ids="+batch,{}));}catch(...){}batch.clear();count=0;};
        for(auto id:ids){Background::Checkpoint();if(count==100)flush();if(count++)batch+=",";batch+=std::to_string(id);}flush();
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
void Initialize(const std::string& directory){
    root=directory;Launch([]{Publish("");});
}
void Shutdown(){std::lock_guard<std::mutex> lock(viewMutex);view=std::make_shared<View>();message.clear();root.clear();}
bool HasLinkedAccounts(){return !Snapshot()->accounts.empty();}
std::string VerifiedAccountForCharacter(const std::string& character){return Active(*Snapshot(),character);}
void RenderOptions(){
    auto v=Snapshot();
    RenderSectionHeader("Native Account Management",UI::SectionGold);
    ImGui::TextWrapped("Create a GW2 API key with: Account, Characters, Inventories, Builds, Progression, Unlocks, Wallet, Guilds, Trading Post. PvP is not needed.");
    if(RenderActionButton("Create API key"))ShellExecuteA(nullptr,"open","https://account.guildwars2.com/applications",nullptr,nullptr,SW_SHOWNORMAL);
    static char key[256]{},label[128]{};static std::string editing,removing;
    if(!editing.empty())ImGui::TextUnformatted("Editing linked account");
    ImGui::InputText("Label (optional)##native_label",label,sizeof(label));
    ImGui::InputText("API key##native_key",key,sizeof(key),ImGuiInputTextFlags_Password);
    if(RenderActionButton(editing.empty()?"Validate & link":"Validate & replace key")&&!busy.load()&&key[0]){
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
        if(RenderActionButton("Edit")&&!busy.load()){editing=a.id;snprintf(label,sizeof(label),"%s",a.label.c_str());SecureZeroMemory(key,sizeof(key));}
        ImGui::SameLine();if(RenderActionButton("Unlink")&&!busy.load()){removing=a.id;ImGui::OpenPopup("Unlink account?");}
        if(ImGui::BeginPopupModal("Unlink account?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextWrapped("Remove the linked account and its stored credential? Saved Library data is unaffected.");
            if(RenderActionButton("Unlink account")&&!busy.load()){Remove(removing);ImGui::CloseCurrentPopup();}
            ImGui::SameLine();if(RenderActionButton("Cancel"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    const auto status=Status();if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
    ImGui::TextDisabled("Keys are stored in Windows Credential Manager, not exported with the Library.");
}
void RenderInventory(const std::string& activeCharacter){
    auto v=Snapshot();if(v->accounts.empty()){ImGui::TextWrapped("Link an account in Nexus > Alter Ego options to browse native inventory.");return;}
    static std::string query,selected,scope,binding;
    static bool all=false,initialized=false;
    const bool multi=v->accounts.size()>1;
    if(!multi){selected=v->accounts.front().id;all=false;initialized=false;}
    else{
        const auto active=Active(*v,activeCharacter);
        if(!initialized){selected=active;all=false;initialized=true;}
        if(!all&&std::none_of(v->accounts.begin(),v->accounts.end(),[&](const Account& a){return a.id==selected;}))selected=active;
        std::string title=all?"All accounts":"Select account";
        for(const auto& a:v->accounts)if(a.id==selected&&!all)title=Display(a);
        if(RenderThemedCombo("##native_inventory_account",title.c_str())){
            if(ImGui::Selectable("All accounts",all))all=true;
            for(const auto& a:v->accounts)if(ImGui::Selectable(Display(a).c_str(),!all&&a.id==selected)){selected=a.id;all=false;}
            ImGui::EndCombo();
        }
    }
    std::vector<Account> accounts;for(const auto& a:v->accounts)if(all||a.id==selected)accounts.push_back(a);
    const auto allowed=[&](const std::string& id){return std::any_of(accounts.begin(),accounts.end(),[&](const Account& a){return a.id==id;});};
    bool refresh=false;
    UI::SearchToolbar("native_inventory","Search item or ID",query,
        [&]{UI::FilterChips c;if(!scope.empty())c.push_back({"scope",scope,[&]{scope.clear();}});
            if(!binding.empty())c.push_back({"binding",binding,[&]{binding.clear();}});return c;},
        [&]{if(ImGui::BeginCombo("Storage",scope.empty()?"All":scope.c_str())){
                for(const auto* s:{"","Bank","Shared inventory","Materials","Bags","Equipment","Legendary Armory","Delivery box"})
                    if(ImGui::Selectable(*s?s:"All",scope==s))scope=s;
                ImGui::EndCombo();}
            if(ImGui::BeginCombo("Binding",binding.empty()?"All":binding.c_str())){
                for(const auto* s:{"","Unknown","Unbound","Account bound","Character bound"})
                    if(ImGui::Selectable(*s?s:"All",binding==s))binding=s;
                ImGui::EndCombo();}},
        [&]{scope.clear();binding.clear();},"Refresh",&refresh);
    if(refresh&&!busy.load()&&!accounts.empty())Refresh(accounts,scope);
    const auto status=Status();if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
    if(accounts.empty()){ImGui::TextUnformatted("Select an account to browse.");return;}
    if(ImGui::TreeNode("Storage status")){
        for(const auto& a:accounts){
            for(const auto* storage:{"Bank","Shared inventory","Materials","Bags","Equipment","Legendary Armory","Delivery box"}){
                bool found=false;
                for(const auto& state:v->inventory.states)if(state.account==a.id&&state.scope==storage){
                    found=true;std::string name=(multi&&all?Display(a)+" / ":"")+state.scope;
                    if(!state.character.empty())name+=" / "+state.character;
                    ImGui::Text("%s: %s%s",name.c_str(),state.status.c_str(),state.success&&Now()-state.success>900?" (stale)":"");
                }
                if(!found)ImGui::Text("%s%s: Not retrieved",multi&&all?(Display(a)+" / ").c_str():"",storage);
            }
        }
        ImGui::TreePop();
    }
    // Cache the filtered aggregate: rebuild only on a new projection or changed controls.
    struct Group{uint32_t id;int64_t count=0;std::string name;std::vector<const Row*> locations;};
    static std::shared_ptr<const View> previous;static std::string prevKey;static std::vector<Group> groups;
    const std::string filterKey=selected+"\n"+(all?"all":"one")+"\n"+query+"\n"+scope+"\n"+binding;
    if(previous!=v||prevKey!=filterKey){
        previous=v;prevKey=filterKey;groups.clear();std::map<uint32_t,size_t> index;
        for(const auto& row:v->inventory.rows){
            const auto& s=row.slot;
            if(!allowed(s.account)||(!scope.empty()&&scope!=s.scope)||(!binding.empty()&&binding!=BindingName(s.binding))||!Match(row.name,s.id,query))continue;
            auto it=index.find(s.id);if(it==index.end()){index.emplace(s.id,groups.size());groups.push_back({s.id,0,row.name,{}});it=index.find(s.id);}
            auto& group=groups[it->second];group.count+=s.count;group.locations.push_back(&row);
        }
        std::sort(groups.begin(),groups.end(),[](const Group& a,const Group& b){return a.name==b.name?a.id<b.id:a.name<b.name;});
    }
    ImGui::Text("%zu matching items",groups.size());ImGui::Separator();
    for(const auto& group:groups){
        ImGui::PushID(static_cast<int>(group.id));
        const std::string name=group.name.empty()?"Item #"+std::to_string(group.id):group.name;
        const std::string header=name+" x"+std::to_string(group.count)+"###item";
        const bool opened=ImGui::TreeNode(header.c_str());
        if(ImGui::BeginPopupContextItem()){
            if(ImGui::MenuItem("Copy item name"))CopyToClipboard(name);
            if(!group.locations.front()->chat.empty()&&ImGui::MenuItem("Copy chat link"))CopyToClipboard(group.locations.front()->chat);
            if(ImGui::MenuItem("Open wiki")){const auto url="https://wiki.guildwars2.com/wiki/Special:Search/"+Encode(name);ShellExecuteA(nullptr,"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
            ImGui::EndPopup();
        }
        if(opened){
            for(const auto* row:group.locations){
                const auto& s=row->slot;std::string location=s.scope;
                if(!s.character.empty())location+=" / "+s.character;
                if(s.scope=="Equipment"){const auto slash=s.position.find('/');if(slash!=std::string::npos)location+=" / "+s.position.substr(slash+1);}
                if(s.scope=="Legendary Armory")location+=" (unlocked count)";
                if(s.scope=="Delivery box")location+=" (not collected)";
                if(multi&&all)for(const auto& a:v->accounts)if(a.id==s.account){location=Display(a)+" / "+location;break;}
                ImGui::Text("%s x%lld | %s%s",location.c_str(),static_cast<long long>(s.count),BindingName(s.binding),Now()-row->captured>900?" | Stale":"");
                if(!s.boundTo.empty())ImGui::TextDisabled("Bound to %s",s.boundTo.c_str());
                if(s.charges>=0)ImGui::TextDisabled("Charges: %lld",static_cast<long long>(s.charges));
                if(s.scope=="Equipment"&&(s.position.find("/Pick")!=std::string::npos||s.position.find("/Axe")!=std::string::npos||s.position.find("/Sickle")!=std::string::npos)&&s.upgrades.empty())
                    ImGui::TextDisabled("Installed glyph: unknown (not reported)");
                for(auto id:s.upgrades)ImGui::TextDisabled("Upgrade #%u",id);
                for(auto id:s.infusions)ImGui::TextDisabled("Infusion #%u",id);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}
}
