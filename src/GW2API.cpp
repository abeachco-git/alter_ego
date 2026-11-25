#include "AccountIdentity.h"
#include "CallbackLifetime.h"
#include "HttpSafety.h"
#include "BackgroundWorkers.h"
#include "GW2API.h"
#include "CharacterRequest.h"
#include "SkinDataCache.h"
#include "ReferenceMetadataCache.h"
#include "ItemMetadataCache.h"
#include "PublicCatalog.h"
#include "CharacterCache.h"
#include "AutoRefreshPolicy.h"
#include "RefreshEngine.h"
#include "CharacterActivity.h"
#include "NativeAccounts.h"
#include "CharacterToolsCore.h"
#include "RefreshProgress.h"
#include "OwnedSkins.h"
#include "HttpClient.h"
#include "ChatLink.h"
#include "BuildGroupOrder.h"
#include "NativeLibraryStore.h"
#include "NativeLibraryEditing.h"
#include "LibraryBackup.h"

#include <windows.h>
#include <wininet.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <cctype>
#include <unordered_set>
#include <set>
#include <atomic>
#include <chrono>
#include <memory>
#include <climits>

using json = nlohmann::json;

namespace AlterEgo {

    // --- Single-shot backoff retry when H&S returns HOARD_STATUS_BUSY ---
    // (proxy queue full / per-addon rate limit). We record a future time and an
    // action; PollRetries() (driven once per frame from AddonRender) fires it
    // exactly once rather than spinning. Action 1 = re-request character list,
    // 2 = re-issue the current per-character fetch phase.
    namespace {
        std::atomic<uint64_t> g_characterDataRevision{1};
        std::atomic<uint64_t> g_buildLibraryRevision{1};
        // Read-only presentation projection of the committed native library.
        Composable::Document g_loadoutDocument;
        bool g_loadoutDocumentLoaded = false;
        std::mutex g_loadoutPersistenceMutex;
        std::mutex g_characterSaveMutex;

        // A manual character refresh also refreshes crafting; a debounced
        // automatic equipment/spec refresh reuses persisted crafting details.
        // Access only while s_mutex is held.
        CharacterRequest::Roster g_roster;
        bool g_rosterReady=false;
        std::shared_ptr<const std::vector<Character>> g_charProjection;
        uint64_t g_projectionRevision=0;
    }

    // Strip HTML-like tags from GW2 API descriptions
    static std::string StripHtmlTags(const std::string& input) {
        std::string result;
        result.reserve(input.size());
        bool in_tag = false;
        for (size_t i = 0; i < input.size(); i++) {
            if (input[i] == '<') {
                if (i + 3 < input.size() && input[i+1] == 'b' && input[i+2] == 'r' && input[i+3] == '>') {
                    result += '\n';
                    i += 3;
                    continue;
                }
                in_tag = true;
            } else if (input[i] == '>') {
                in_tag = false;
            } else if (!in_tag) {
                result += input[i];
            }
        }
        return result;
    }

    // URL-encode helper
    static std::string UrlEncode(const std::string& name) {
        std::string encoded;
        for (char c : name) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded += c;
            } else {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
                encoded += hex;
            }
        }
        return encoded;
    }

    // Static member initialization
    AddonAPI_t* GW2API::s_api = nullptr;
    FetchStatus GW2API::s_fetch_status = FetchStatus::Idle;
    std::string GW2API::s_fetch_message;
    std::string GW2API::s_active_character;
    std::vector<Character> GW2API::s_characters;
    bool GW2API::s_has_character_data = false;
    time_t GW2API::s_last_updated = 0;
    std::vector<std::string> GW2API::s_pending_char_names;
    int GW2API::s_chars_fetched = 0;
    int GW2API::s_chars_total = 0;
    Character GW2API::s_current_char;
    std::unordered_map<uint32_t, std::shared_ptr<const ItemInfo>> GW2API::s_item_cache;
    std::unordered_map<uint32_t, std::shared_ptr<const SkinInfo>> GW2API::s_skin_cache;
    std::unordered_map<uint32_t, std::shared_ptr<const SpecializationInfo>> GW2API::s_spec_cache;
    std::unordered_map<uint32_t, std::shared_ptr<const TraitInfo>> GW2API::s_trait_cache;
    std::unordered_map<uint32_t, std::shared_ptr<const SkillInfo>> GW2API::s_skill_cache;
    std::unordered_map<int, std::shared_ptr<const DyeColor>> GW2API::s_dye_cache;
    std::unordered_map<uint32_t, std::shared_ptr<const ItemStatInfo>> GW2API::s_itemstat_cache;
    std::unordered_map<std::string, std::shared_ptr<const ProfessionInfo>> GW2API::s_profession_cache;
    std::unordered_map<std::string, std::shared_ptr<const std::map<std::string, ProfessionWeaponData>>> GW2API::s_profession_weapons;
    std::vector<SavedBuild> GW2API::s_saved_builds;
    std::unordered_map<std::string, uint32_t> GW2API::s_item_name_id_cache;
    bool GW2API::s_item_name_cache_dirty = false;
    std::unordered_map<std::string, std::unordered_map<uint16_t, uint32_t>> GW2API::s_palette_to_skill;
    std::unordered_map<std::string, std::unordered_map<uint32_t, uint16_t>> GW2API::s_skill_to_palette;
    std::unordered_set<std::string> GW2API::s_palette_fetching;

    std::mutex GW2API::s_mutex;

    // --- Nexus integration ---

    void GW2API::Initialize(AddonAPI_t* api) {
        s_api = api;
    }

    void GW2API::Shutdown() {
        s_api = nullptr;
    }

    // Helper: get the DLL directory
    static std::string GetDllDir() {
        char dllPath[MAX_PATH];
        HMODULE hModule = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)GetDllDir, &hModule)) {
            if (GetModuleFileNameA(hModule, dllPath, MAX_PATH)) {
                std::string path(dllPath);
                size_t lastSlash = path.find_last_of("\\/");
                if (lastSlash != std::string::npos) {
                    return path.substr(0, lastSlash);
                }
            }
        }
        return "";
    }

    std::string GW2API::GetDataDirectory() {
        std::string dir = GetDllDir();
        if (!dir.empty()) {
            std::replace(dir.begin(), dir.end(), '\\', '/');
        }
        return dir + "/AlterEgo";
    }

    bool GW2API::EnsureDataDirectory() {
        std::string dir = GetDataDirectory();
        try {
            std::filesystem::create_directories(dir);
            return true;
        } catch (...) {
            return false;
        }
    }

    // =========================================================================
    // Native account identity and retrieval
    // =========================================================================
    AccountStatus GW2API::GetAccountStatus(){
        if(!NativeAccounts::Ready())return AccountStatus::Unknown;
        return NativeAccounts::HasLinkedAccounts()?AccountStatus::Ready:AccountStatus::Unavailable;
    }
    std::vector<AccountInfo> GW2API::GetAccounts(){
        std::vector<AccountInfo> out;
        for(const auto& a:NativeAccounts::Linked()){
            AccountInfo row;row.name=a.id;row.label=a.label;row.display_name=a.label.empty()?a.name:a.label;
            row.validated=true;row.characters.assign(a.characters.begin(),a.characters.end());out.push_back(std::move(row));
        }
        return out;
    }
    uint32_t GW2API::GetAccountCount(){return static_cast<uint32_t>(NativeAccounts::Linked().size());}
    bool GW2API::IsMultiAccount(){return GetAccountCount()>1;}
    bool GW2API::HasAccountsData(){return NativeAccounts::Ready()&&GetAccountCount()>0;}
    std::string GW2API::GetCurrentAccountName(){
        std::string ch;{std::lock_guard<std::mutex> lock(s_mutex);ch=s_active_character;}
        return NativeAccounts::VerifiedAccountForCharacter(ch);
    }
    void GW2API::SetCurrentAccountFromCharacter(const std::string& name){std::lock_guard<std::mutex> lock(s_mutex);s_active_character=name;}
    std::string GW2API::GetAccountForCharacter(const std::string& name){return NativeAccounts::VerifiedAccountForCharacter(name);}
    std::vector<std::string> GW2API::GetAllCharacterNamesFromAccounts(const std::string& filter){
        std::vector<std::string> out;
        for(const auto& a:NativeAccounts::Linked())if(filter.empty()||a.id==filter)
            out.insert(out.end(),a.characters.begin(),a.characters.end());
        return out;
    }
    void GW2API::QuerySkinUnlocks(const std::vector<uint32_t>&){ /* OwnedSkins owns the full account snapshot. */ }
    bool GW2API::HasSkinUnlockData(uint32_t){return Skinventory::OwnedSkins::HasDataForAccount(GetCurrentAccountName());}
    bool GW2API::IsSkinUnlocked(uint32_t id){return Skinventory::OwnedSkins::IsOwnedForAccount(GetCurrentAccountName(),id);}
    void GW2API::MarkSkinUnlocked(uint32_t){ /* The shared alert handler calls OwnedSkins::MarkOwned. */ }
    void GW2API::QueryItemLocation(uint32_t){ /* Inventory locations are local, not network queries. */ }
    const ItemLocationResult* GW2API::GetItemLocation(uint32_t id){
        thread_local ItemLocationResult result;result={};result.item_id=id;
        const auto locations=NativeAccounts::Locations(id);if(locations.empty())return nullptr;
        for(const auto& x:locations){
            ItemLocationEntry e;e.location=x.character.empty()?x.scope:x.character;
            e.sublocation=x.scope+" / "+x.binding;e.account_name=x.account;
            e.count=static_cast<int>(std::min<int64_t>(x.count,INT_MAX));result.total_count+=e.count;
            result.locations.push_back(std::move(e));
        }
        return &result;
    }
    void GW2API::RequestCharacterList(){RequestNativeCharacters({},true,true,{});}
    void GW2API::RequestCharacterRefresh(const std::string& priority){RequestNativeCharacters({},false,true,priority);}
    void GW2API::RequestCharacterRefreshSelected(const std::vector<std::string>& names,bool crafting){
        if(!names.empty())RequestNativeCharacters(names,false,crafting,{});
    }
    void GW2API::RequestCharacterActivityRefresh(const std::string& account,const std::string& name,CharacterScopes::Mask demand,bool manual){
        if(name.empty()||!demand)return;
        std::string owner=account;
        if(manual){
            // Legacy character rows may contain an account display name or
            // no account at all. Only explicit manual retrieval can discover
            // their ownership; never broaden an automatic request.
            for(const auto& linked:NativeAccounts::Linked())
                if(!owner.empty()&&linked.name==owner){owner=linked.id;break;}
            if(owner.empty())owner=NativeAccounts::VerifiedAccountForCharacter(name);
        }
        if(!owner.empty()||manual)RequestNativeCharacters({name},false,true,{},owner,demand,manual);
    }
    // Safe helpers: get value from JSON, return default if null or missing
    static int SafeInt(const json& j, const char* key, int def = 0) {
        if (!j.contains(key) || j[key].is_null()) return def;
        return j[key].get<int>();
    }
    static uint32_t SafeUint(const json& j, const char* key, uint32_t def = 0) {
        if (!j.contains(key) || j[key].is_null()) return def;
        return j[key].get<uint32_t>();
    }
    static bool SafeBool(const json& j, const char* key, bool def = false) {
        if (!j.contains(key) || j[key].is_null()) return def;
        return j[key].get<bool>();
    }
    static std::string SafeStr(const json& j, const char* key, const std::string& def = "") {
        if (!j.contains(key) || j[key].is_null()) return def;
        return j[key].get<std::string>();
    }


    void GW2API::RequestNativeCharacters(const std::vector<std::string>& selected,bool listOnly,bool refreshCrafting,const std::string& priority,const std::string& accountOnly,CharacterScopes::Mask demand,bool manual){
        auto accounts=NativeAccounts::Linked();
        if(!accountOnly.empty())accounts.erase(std::remove_if(accounts.begin(),accounts.end(),[&](const auto& a){return a.id!=accountOnly;}),accounts.end());
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(s_fetch_status==FetchStatus::InProgress)return;
            if(accounts.empty()){s_fetch_status=FetchStatus::Error;s_fetch_message="Link an account in Nexus options";return;}
            s_fetch_status=FetchStatus::InProgress;s_fetch_message="Checking character roster...";
            s_current_char={};g_rosterReady=false;s_chars_fetched=0;s_chars_total=0;
        }
        Background::Workers().Launch([accounts,selected,listOnly,refreshCrafting,priority,accountOnly,demand,manual]{
            ApiActivity::ReasonScope reason(manual?ApiActivity::Reason::Manual:ApiActivity::Reason::Visible);
            auto fail=[](const std::string& message){std::lock_guard<std::mutex> lock(s_mutex);s_fetch_status=FetchStatus::Error;s_fetch_message=message;s_current_char={};};
            std::map<std::string,std::set<std::string>> roster;
            std::vector<std::pair<NativeAccounts::LinkedAccount,std::string>> targets;
            bool partial=false;
            // A manual roster fetch verifies every configured account; selective
            // refresh reuses known ownership and never falls back to a random key.
            size_t rosterOrdinal=0;
            for(const auto& a:accounts){
                Background::Checkpoint();++rosterOrdinal;std::set<std::string> names=a.characters;
                if(listOnly||selected.empty()||!a.rosterKnown){
                    if(!manual){partial=true;continue;} // automatic demand never discovers unrelated rosters
                    {std::lock_guard<std::mutex> lock(s_mutex);s_fetch_message="Checking roster "+std::to_string(rosterOrdinal)+"/"+std::to_string(accounts.size())+(accounts.size()>1?" - "+a.name:"");}
                    if(!NativeAccounts::RefreshRoster(a,names)){partial=true;continue;}
                    roster[a.id]=names;
                }
                for(const auto& n:names)if(selected.empty()||std::find(selected.begin(),selected.end(),n)!=selected.end())
                    targets.emplace_back(a,n);
            }
            std::map<std::string,int> counts;for(const auto& x:targets)++counts[x.second];
            targets.erase(std::remove_if(targets.begin(),targets.end(),[&](const auto& x){if(counts[x.second]>1){partial=true;return true;}return false;}),targets.end());
            if(!selected.empty())for(const auto& n:selected)if(!counts.count(n))partial=true;
            if(!priority.empty())std::stable_partition(targets.begin(),targets.end(),[&](const auto& x){return x.second==priority;});
            if(std::any_of(accounts.begin(),accounts.end(),[](const auto& a){return !NativeAccounts::Current(a);})){fail("Account configuration changed; refresh again.");return;}
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_pending_char_names.clear();for(const auto& x:targets)s_pending_char_names.push_back(x.second);
                g_roster=std::move(roster);g_rosterReady=!partial&&!g_roster.empty();
                s_chars_total=static_cast<int>(targets.size());
            }
            if(listOnly){
                {std::lock_guard<std::mutex> lock(s_mutex);s_fetch_status=partial?FetchStatus::Error:FetchStatus::Success;s_fetch_message=partial?"Some rosters could not be verified":"Character list received";}
                // Reconcile on the owned worker, not on the render thread.
                ReconcileCharactersWithRoster();return;
            }
            size_t ordinal=0;
            for(const auto& [a,name]:targets){
                Background::Checkpoint();++ordinal;
                if(!NativeAccounts::Current(a)){partial=true;break;}
                {
                    std::lock_guard<std::mutex> lock(s_mutex);s_current_char.name=name;
                    s_fetch_message=RefreshProgress::Character(ordinal,targets.size(),name,"Starting refresh");
                }
                namespace S=CharacterScopes;
                const CharacterActivity::Key key{a.id,name};
                std::optional<Character> old;
                {std::lock_guard<std::mutex> lock(s_mutex);
                    for(const auto& existing:s_characters)
                        if(existing.name==name&&(existing.account_name==a.id||existing.account_name==a.name||existing.account_name.empty())){
                            old=existing;if(existing.account_name==a.id)break;
                        }
                }
                Character ch=old.value_or(Character{});
                const bool needsIdentity=!old||old->account_name!=a.id;
                ch.name=name;ch.account_name=a.id;
                auto requested=demand;
                if(needsIdentity)requested|=S::Core;
                if(!refreshCrafting&&ch.crafting_cached)requested&=~S::Crafting;
                if(!manual){
                    std::lock_guard<std::mutex> lock(CharacterActivity::Mutex());
                    requested=S::Due(CharacterActivity::State(),key,ch.scope_stamps,requested,
                        CharacterActivity::Now(),CharacterActivity::SpecSignal().load());
                }
                if(!requested)continue;
                struct Verified { S::Scope scope; CharacterActivity::Ticket ticket; int64_t stamp; bool specVerified; };
                std::vector<Verified> verified;
                CharacterActivity::CancelOnShutdown attempts(key);
                const std::string path="/v2/characters/"+UrlEncode(name);
                const auto stage=[&](const std::string& label){
                    std::lock_guard<std::mutex> lock(s_mutex);
                    s_fetch_message=RefreshProgress::Character(ordinal,targets.size(),name,label);
                };
                std::string scopeError;
                const auto require=[&](const std::string& endpoint,const std::vector<std::string>& perms,const char* label){
                    stage(label);auto r=NativeAccounts::Request(a,endpoint,perms);
                    if(!r){scopeError=r.error;throw std::runtime_error("Character endpoint failed");}return std::move(r.data);
                };
                bool identityVerified=!needsIdentity;
                for(auto scope:S::Ordered){
                    if(!(requested&scope))continue;
                    Background::Checkpoint();
                    if(!NativeAccounts::Current(a)){partial=true;break;}
                    if(scope!=S::Core&&!identityVerified){partial=true;continue;}
                    const auto ticket=attempts.BeginScope(S::Name(scope));
                    bool specVerified=true;
                    scopeError.clear();
                    try{
                        // Stage each scope against the current candidate. A
                        // malformed response cannot erase its previous fields.
                        Character next=ch;
                        switch(scope){
                        case S::Core:{
                            auto core=require(path+"/core",{"characters"},"Character details");
                            if(!core.is_object()||core.value("name",std::string{})!=name||
                                !core.contains("profession")||!core.contains("race")||!core.contains("gender")||
                                !core.contains("level")||!core.contains("age")||!core.contains("created")||!core.contains("deaths"))
                                throw std::runtime_error("Invalid character core");
                            auto c=ParseCharacterJson(core);
                            if(c.profession.empty()||c.race.empty()||c.gender.empty()||c.created.empty()||c.level<1||c.level>80)
                                throw std::runtime_error("Incomplete character core");
                            next.race=c.race;next.gender=c.gender;next.profession=c.profession;
                            next.level=c.level;next.age=c.age;next.created=c.created;next.deaths=c.deaths;
                            next.last_modified=SafeStr(core,"last_modified");
                            break;
                        }
                        case S::Builds:{
                            const auto builds=require(path+"/buildtabs?tabs=all&v=2021-07-15T13:00:00.000Z",{"characters","builds"},"Build tabs");
                            if(!builds.is_array()||builds.empty())throw std::runtime_error("Invalid build tabs");
                            next.build_tabs.clear();std::set<int> ids;int active=0;
                            for(const auto& row:builds){
                                if(!row.at("build").is_object())throw std::runtime_error("Invalid build body");
                                auto b=ParseBuildTemplate(row);
                                if(b.tab<=0||!ids.insert(b.tab).second)throw std::runtime_error("Invalid build tab");
                                if(b.is_active){if(active)throw std::runtime_error("Multiple active build tabs");active=b.tab;}
                                next.build_tabs.push_back(std::move(b));
                            }
                            if(!active)throw std::runtime_error("Missing active build tab");
                            next.active_build_tab=active;
                            if(ticket.active&&ticket.specChanged){
                                for(const auto& b:next.build_tabs)if(b.is_active){
                                    std::lock_guard<std::mutex> lock(CharacterActivity::Mutex());
                                    specVerified=CharacterActivity::State().SpecMatches(key,b.specializations[2].spec_id);break;
                                }
                            }
                            break;
                        }
                        case S::Tabs:{
                            const auto eq=require(path+"/equipmenttabs?tabs=all&v=2024-04-01T00:00:00.000Z",{"characters","builds"},"Equipment tabs");
                            if(!eq.is_array()||eq.empty())throw std::runtime_error("Invalid equipment tabs");
                            next.equipment.clear();next.equipment_tab_names.clear();std::set<int> tabs;int active=0;
                            for(const auto& tab:eq){
                                const auto id=SafeInt(tab,"tab");
                                if(id<=0||!tabs.insert(id).second||!tab.at("equipment").is_array())throw std::runtime_error("Invalid equipment tab");
                                next.equipment_tab_names[id]=SafeStr(tab,"name");
                                if(SafeBool(tab,"is_active")){if(active)throw std::runtime_error("Multiple active equipment tabs");active=id;}
                                for(const auto& raw:tab.at("equipment")){
                                    auto item=ParseEquipmentItem(raw);if(!item.id)throw std::runtime_error("Invalid equipment ID");
                                    if(CharacterTools::Utility(item.slot))continue; // physical endpoint owns character-wide tools
                                    item.tab=id;next.equipment.push_back(std::move(item));
                                }
                            }
                            if(!active)throw std::runtime_error("Missing active equipment tab");
                            next.active_equipment_tab=active;
                            // Preserve independently retrieved physical fields.
                            // Legacy utilities were never tab-specific.
                            for(auto item:ch.equipment){
                                if(CharacterTools::Utility(item.slot)){item.tab=0;item.physical_supplement=true;}
                                else if(!item.physical_supplement){
                                    if(item.slot!="Relic"||ch.scope_stamps[S::Index(S::Tabs)]>0)continue;
                                    // Legacy composite relic provenance was unknown.
                                    item.physical_supplement=true;
                                }
                                if(item.tab&&!tabs.count(item.tab))continue;
                                if(std::none_of(next.equipment.begin(),next.equipment.end(),[&](const EquipmentItem& e){return e.tab==item.tab&&e.slot==item.slot;}))
                                    next.equipment.push_back(std::move(item));
                            }
                            break;
                        }
                        case S::Physical:{
                            stage("Physical equipment");
                            auto physical=NativeAccounts::RequestPhysicalEquipment(a,name,manual);
                            if(!physical.response){scopeError=physical.response.error;throw std::runtime_error("Physical equipment endpoint failed");}
                            const auto& supplement=physical.response.data;
                            if(!supplement.at("equipment").is_array())throw std::runtime_error("Invalid supplemental equipment");
                            next.equipment.erase(std::remove_if(next.equipment.begin(),next.equipment.end(),[](const EquipmentItem& e){
                                return e.physical_supplement||CharacterTools::Utility(e.slot);
                            }),next.equipment.end());
                            for(const auto& raw:supplement.at("equipment")){
                                const auto slot=SafeStr(raw,"slot");if(slot!="Relic"&&!CharacterTools::Utility(slot))continue;
                                auto item=ParseEquipmentItem(raw);if(!item.id)throw std::runtime_error("Invalid supplemental equipment ID");
                                item.physical_supplement=true;
                                const auto add=[&](int tab){auto copy=item;copy.tab=tab;
                                    if(std::none_of(next.equipment.begin(),next.equipment.end(),[&](const EquipmentItem& e){return e.tab==tab&&e.slot==slot;}))
                                        next.equipment.push_back(std::move(copy));};
                                if(CharacterTools::Utility(slot)){add(0);continue;}
                                if(raw.contains("tabs")&&raw.at("tabs").is_array()&&!raw.at("tabs").empty()){
                                    for(const auto& t:raw.at("tabs"))if(!t.is_null()){
                                        const int id=t.get<int>();
                                        if(id>0)add(id); // retain associations even when tabs have not been retrieved yet
                                    }
                                }else add(0);
                            }
                            // A reused physical response must not advance this
                            // scope past the actual retrieval or cover a later logout.
                            const auto stamp=physical.success;
                            if(stamp<=0||stamp<ch.scope_stamps[S::Index(scope)])throw std::runtime_error("Invalid physical verification");
                            next.scope_stamps[S::Index(scope)]=stamp;
                            auto actual=ticket;actual.verificationStarted=physical.started;
                            verified.push_back({scope,actual,stamp,true});
                            ch=std::move(next);continue;
                        }
                        case S::Crafting:{
                            auto craft=require(path+"/crafting",{"characters"},"Crafting");
                            if(craft.is_object())craft=craft.at("crafting").get<json>();
                            if(!craft.is_array())throw std::runtime_error("Invalid crafting");
                            for(const auto& row:craft)if(!row.is_object()||!row.contains("discipline")||!row.contains("rating")||!row.contains("active"))
                                throw std::runtime_error("Incomplete crafting row");
                            auto c=ParseCharacterJson(json{{"crafting",craft}});
                            next.crafting=std::move(c.crafting);next.crafting_levels=std::move(c.crafting_levels);
                            next.crafting_active=std::move(c.crafting_active);next.crafting_cached=true;break;
                        }
                        }
                        const auto stamp=CharacterActivity::Now();
                        next.scope_stamps[S::Index(scope)]=stamp;
                        verified.push_back({scope,ticket,stamp,specVerified});
                        ch=std::move(next);
                        if(scope==S::Core)identityVerified=true;
                    }catch(const Background::Cancelled&){throw;}catch(...){
                        partial=true;
                        if(!Background::Workers().Stopping()){
                            std::lock_guard<std::mutex> lock(CharacterActivity::Mutex());
                            CharacterActivity::State().Failure(key,S::Name(scope),ticket,
                                scopeError.empty()?"Invalid or incomplete endpoint response":scopeError);
                        }
                    }
                }
                Background::Checkpoint();
                if(verified.empty()||!identityVerified||!NativeAccounts::Current(a)){partial=true;continue;}
                ch.data_refreshed=std::time(nullptr);
                stage("Saving verified scopes");
                if(!SaveCharacterData(a.id,name,&ch,a.name,[&]{return NativeAccounts::Current(a);})){ 
                    partial=true;
                    std::lock_guard<std::mutex> lock(CharacterActivity::Mutex());
                    for(const auto& v:verified)CharacterActivity::State().Failure(key,S::Name(v.scope),v.ticket,"Could not save verified data");
                    continue;
                }
                for(const auto& v:verified)CharacterActivity::Success(key,S::Name(v.scope),v.ticket,v.stamp,v.specVerified);
                {std::lock_guard<std::mutex> lock(s_mutex);++s_chars_fetched;}
            }
            {
                std::lock_guard<std::mutex> lock(s_mutex);s_current_char={};
                s_fetch_status=partial?FetchStatus::Error:FetchStatus::Success;
                s_fetch_message=std::to_string(s_chars_fetched)+"/"+std::to_string(s_chars_total)+(partial?" characters refreshed; failed data retained":" characters refreshed");
            }
            if(!partial&&!Background::Workers().Stopping())ReconcileCharactersWithRoster();
        },[]{std::lock_guard<std::mutex> lock(s_mutex);s_fetch_status=FetchStatus::Error;s_fetch_message="Character refresh cancelled";s_current_char={};});
    }

    // --- Parse helpers ---

    EquipmentItem GW2API::ParseEquipmentItem(const json& item_json) {
        EquipmentItem eq;
        eq.id = SafeUint(item_json, "id");
        eq.slot = SafeStr(item_json, "slot");
        eq.skin = SafeUint(item_json, "skin");
        eq.binding = SafeStr(item_json, "binding");
        eq.bound_to = SafeStr(item_json, "bound_to");
        eq.binding_known = item_json.contains("binding");
        eq.bound_to_known = item_json.contains("bound_to");
        if(item_json.contains("charges")&&item_json["charges"].is_number_integer()){
            const auto value=item_json["charges"].get<int64_t>();if(value>=0)eq.charges=value;
        }

        if (item_json.contains("upgrades") && item_json["upgrades"].is_array()) {
            for (const auto& u : item_json["upgrades"])
                eq.upgrades.push_back(u.get<uint32_t>());
        }
        if (item_json.contains("infusions") && item_json["infusions"].is_array()) {
            for (const auto& inf : item_json["infusions"])
                eq.infusions.push_back(inf.get<uint32_t>());
        }
        if (item_json.contains("dyes") && item_json["dyes"].is_array()) {
            for (const auto& d : item_json["dyes"])
                eq.dyes.push_back(d.is_null() ? 0 : d.get<int>());
        }

        if (item_json.contains("tabs") && item_json["tabs"].is_array() && !item_json["tabs"].empty()) {
            eq.tab = item_json["tabs"][0].get<int>();
        } else {
            eq.tab = item_json.value("tab", 0);
        }

        // Parse stats (selected stat set for legendary/ascended items)
        if (item_json.contains("stats") && item_json["stats"].is_object()) {
            const auto& stats = item_json["stats"];
            eq.stat_id = SafeUint(stats, "id");
            if (stats.contains("attributes") && stats["attributes"].is_object()) {
                for (auto it = stats["attributes"].begin(); it != stats["attributes"].end(); ++it) {
                    eq.attributes[it.key()] = it.value().get<int>();
                }
            }
        }

        return eq;
    }

    BuildTemplate GW2API::ParseBuildTemplate(const json& tab_json) {
        BuildTemplate bt;
        bt.tab = SafeInt(tab_json, "tab");
        bt.is_active = SafeBool(tab_json, "is_active");

        if (tab_json.contains("build") && !tab_json["build"].is_null()) {
            const auto& build = tab_json["build"];
            bt.name = SafeStr(build, "name");
            bt.profession = SafeStr(build, "profession");

            if (build.contains("specializations") && build["specializations"].is_array()) {
                int idx = 0;
                for (const auto& spec : build["specializations"]) {
                    if (idx >= 3) break;
                    if (spec.is_null()) { idx++; continue; }
                    bt.specializations[idx].spec_id = SafeUint(spec, "id");
                    if (spec.contains("traits") && spec["traits"].is_array()) {
                        int ti = 0;
                        for (const auto& t : spec["traits"]) {
                            if (ti >= 3) break;
                            bt.specializations[idx].traits[ti] = t.is_null() ? 0 : t.get<int>();
                            ti++;
                        }
                    }
                    idx++;
                }
            }

            auto parseSkillBar = [](const json& skills_json) -> SkillBar {
                SkillBar sb;
                if (skills_json.is_null()) return sb;
                sb.heal = SafeUint(skills_json, "heal");
                if (skills_json.contains("utilities") && skills_json["utilities"].is_array()) {
                    int i = 0;
                    for (const auto& u : skills_json["utilities"]) {
                        if (i >= 3) break;
                        sb.utilities[i] = u.is_null() ? 0 : u.get<uint32_t>();
                        i++;
                    }
                }
                sb.elite = SafeUint(skills_json, "elite");
                return sb;
            };

            if (build.contains("skills"))
                bt.terrestrial_skills = parseSkillBar(build["skills"]);
            if (build.contains("aquatic_skills"))
                bt.aquatic_skills = parseSkillBar(build["aquatic_skills"]);

            if (build.contains("pets") && build["pets"].is_object()) {
                bt.pets_known = true;
                const auto& pets = build["pets"];
                if (pets.contains("terrestrial") && pets["terrestrial"].is_array()) {
                    int i = 0;
                    for (const auto& p : pets["terrestrial"]) {
                        if (i >= 2) break;
                        bt.pets.terrestrial[i] = p.is_null() ? 0 : p.get<uint32_t>();
                        i++;
                    }
                }
                if (pets.contains("aquatic") && pets["aquatic"].is_array()) {
                    int i = 0;
                    for (const auto& p : pets["aquatic"]) {
                        if (i >= 2) break;
                        bt.pets.aquatic[i] = p.is_null() ? 0 : p.get<uint32_t>();
                        i++;
                    }
                }
            }

            if (build.contains("legends") && build["legends"].is_array()) {
                bt.legends_known = true;
                int i = 0;
                for (const auto& leg : build["legends"]) {
                    if (i >= 2) break;
                    bt.legends.terrestrial[i] = leg.is_null() ? "" : leg.get<std::string>();
                    i++;
                }
            }
            if (build.contains("aquatic_legends") && build["aquatic_legends"].is_array()) {
                bt.legends_known = true;
                int i = 0;
                for (const auto& leg : build["aquatic_legends"]) {
                    if (i >= 2) break;
                    bt.legends.aquatic[i] = leg.is_null() ? "" : leg.get<std::string>();
                    i++;
                }
            }
        }

        // The API may omit weapon selections entirely; do not turn that into verified empty data.
        if (tab_json.contains("build") && tab_json["build"].is_object()) {
            const auto& build=tab_json["build"];
            if (build.contains("weapons") && build["weapons"].is_array()) {
                const auto& weapons=build["weapons"];
                bt.weapons_known=std::all_of(weapons.begin(),weapons.end(),[](const auto& weapon){
                    return weapon.is_number_integer()&&weapon.template get<int64_t>()>=0&&
                           weapon.template get<int64_t>()<=UINT32_MAX;
                });
                if(bt.weapons_known)bt.weapons=weapons.get<std::vector<uint32_t>>();
            }
        }
        return bt;
    }

    Character GW2API::ParseCharacterJson(const json& cj) {
        Character ch;
        ch.name = cj.value("name", "");
        ch.race = cj.value("race", "");
        ch.gender = cj.value("gender", "");
        ch.profession = cj.value("profession", "");
        ch.level = cj.value("level", 0);
        ch.age = cj.value("age", 0);
        ch.created = cj.value("created", "");
        ch.deaths = cj.value("deaths", 0);
        ch.active_build_tab = cj.value("active_build_tab", 0);
        ch.active_equipment_tab = cj.value("active_equipment_tab", 0);

        if (cj.contains("crafting") && cj["crafting"].is_array()) {
            ch.crafting_cached = true;
            std::vector<std::tuple<std::string, int, bool>> allCrafts;
            for (const auto& craft : cj["crafting"]) {
                int rating = craft.value("rating", 0);
                if (rating >= 1) {
                    allCrafts.emplace_back(
                        craft.value("discipline", ""),
                        rating,
                        craft.value("active", false));
                }
            }
            std::sort(allCrafts.begin(), allCrafts.end(),
                [](const auto& a, const auto& b) {
                    return std::get<2>(a) > std::get<2>(b);
                });
            for (const auto& [disc, lvl, active] : allCrafts) {
                ch.crafting.push_back(disc);
                ch.crafting_levels.push_back(lvl);
                ch.crafting_active.push_back(active);
            }
        }

        if (cj.contains("equipment") && cj["equipment"].is_array()) {
            for (const auto& eq_json : cj["equipment"]) {
                ch.equipment.push_back(ParseEquipmentItem(eq_json));
            }
        }

        if (cj.contains("build_tabs") && cj["build_tabs"].is_array()) {
            for (const auto& tab : cj["build_tabs"]) {
                ch.build_tabs.push_back(ParseBuildTemplate(tab));
            }
        }

        return ch;
    }

    // =========================================================================
    // Query helpers
    // =========================================================================

    FetchStatus GW2API::GetFetchStatus() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_fetch_status;
    }

    std::string GW2API::GetFetchStatusMessage() {
        std::lock_guard<std::mutex> lock(s_mutex);return s_fetch_message;
    }

    std::string GW2API::GetCurrentFetchCharName() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_fetch_status == FetchStatus::InProgress)
            return s_current_char.name;
        return "";
    }

    bool GW2API::HasCharacterData() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_has_character_data;
    }

    time_t GW2API::GetLastUpdated() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_last_updated;
    }

    uint64_t GW2API::GetCharacterDataRevision() {
        return g_characterDataRevision.load();
    }

    uint64_t GW2API::GetBuildLibraryRevision() {
        return g_buildLibraryRevision.load();
    }

    std::shared_ptr<const std::vector<Character>> GW2API::GetCharacters() {
        std::lock_guard<std::mutex> lock(s_mutex);
        const auto revision=g_characterDataRevision.load();
        if(!g_charProjection || g_projectionRevision!=revision) {
            g_charProjection=std::make_shared<const std::vector<Character>>(s_characters);
            g_projectionRevision=revision;
        }
        return g_charProjection;
    }

    std::optional<Character> GW2API::GetCharacter(const std::string& name) {
        std::lock_guard<std::mutex> lock(s_mutex);
        const Character* match=nullptr;
        for(const auto& c:s_characters) {
            if(c.name!=name)continue;
            if(!match || (match->account_name.empty() && !c.account_name.empty()))match=&c;
            else if(!c.account_name.empty() && !match->account_name.empty() && match->account_name!=c.account_name)
                return std::nullopt;
        }
        return match?std::optional<Character>(*match):std::nullopt;
    }

    std::vector<std::string> GW2API::GetPendingCharNames() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_pending_char_names;
    }

    void GW2API::ReconcileCharactersWithRoster() {
        bool changed = false, failed = false;
        {
            // Same lock order as snapshot persistence. Keep publication and
            // deletion serialized; do not call Nexus while either lock is held.
            std::lock_guard<std::mutex> saveLock(g_characterSaveMutex);
            std::lock_guard<std::mutex> lock(s_mutex);
            if(!g_rosterReady || s_fetch_status!=FetchStatus::Success)return;
            g_rosterReady=false;
            const auto absent=[](const Character& c) {
                return CharacterRequest::VerifiedAbsent(g_roster,c.account_name,c.name);
            };
            if(std::none_of(s_characters.begin(),s_characters.end(),absent))return;
            // Commit first. Failure retains the previous in-memory and disk
            // snapshots. A future successful roster fetch can retry.
            if(!Cache::DeleteAbsentCharacters(GetDataDirectory(),g_roster))failed=true;
            else {
                s_characters.erase(std::remove_if(s_characters.begin(),s_characters.end(),absent),
                    s_characters.end());
                s_has_character_data = !s_characters.empty();
                g_characterDataRevision.fetch_add(1);
                changed=true;
            }
        }
        if (s_api && failed) s_api->Log(LOGL_WARNING,"AlterEgo",
            "Character roster reconciliation could not be persisted; retaining the previous cache.");
        if (s_api && changed) s_api->Log(LOGL_INFO,"AlterEgo",
            "Reconciled character cache with verified roster (removed deleted characters).");
    }

    // =========================================================================
    // Cache accessors
    // =========================================================================

    std::shared_ptr<const ItemInfo> GW2API::GetItemInfo(uint32_t item_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_item_cache.find(item_id);
        return (it != s_item_cache.end()) ? it->second : nullptr;
    }

    uint32_t GW2API::FindItemIdByName(const std::string& name) {
        if (name.empty()) return 0;
        std::lock_guard<std::mutex> lock(s_mutex);
        // The validated catalog wins over legacy seed aliases. If duplicate
        // public names exist, keep a known exact alias only if it is a candidate.
        const auto catalog=PublicCatalog::Get();
        uint32_t match=0;bool ambiguous=false;
        const auto alias=s_item_name_id_cache.find(name);
        for(const auto& e:catalog->entries)if(e.name==name) {
            if(alias!=s_item_name_id_cache.end()&&alias->second==e.item_id)return e.item_id;
            if(match&&match!=e.item_id)ambiguous=true;
            match=e.item_id;
        }
        if(match)return ambiguous?0:match;
        // Check full item cache first
        for (const auto& [id, row] : s_item_cache) {
                const auto& info=*row;
            if (info.name == name) return id;
        }
        // Fallback to persistent name→ID cache
        auto it = s_item_name_id_cache.find(name);
        if (it != s_item_name_id_cache.end()) return it->second;
        return 0;
    }

    std::vector<std::pair<std::string, uint32_t>> GW2API::SearchKnownInfusions(
        const std::string& query, size_t maxResults, bool enrichment) {
        std::vector<std::pair<std::string, uint32_t>> results;
        if (query.empty() || maxResults == 0) return results;
        auto lower = [](std::string str) {
            std::transform(str.begin(), str.end(), str.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return str;
        };
        const std::string needle = lower(query);
        const auto catalog=PublicCatalog::Get();
        if(catalog->upgrades) {
            for(const auto& e:catalog->entries)if(e.kind==(enrichment?"Enrichment":"Infusion")&&
                (lower(e.name).find(needle)!=std::string::npos||std::to_string(e.item_id).find(needle)!=std::string::npos))
                results.emplace_back(e.name,e.item_id);
            if(results.size()>maxResults)results.resize(maxResults);
            return results;
        }
        std::lock_guard<std::mutex> lock(s_mutex);
        for (const auto& [name, id] : s_item_name_id_cache) {
            const std::string folded = lower(name);
            // Do not treat arbitrary item names as eligible infusions.
            if (folded.find(enrichment?"enrichment":"infusion") == std::string::npos)
                continue;
            if (folded.find(needle) != std::string::npos ||
                std::to_string(id).find(needle) != std::string::npos)
                results.emplace_back(name, id);
        }
        std::sort(results.begin(), results.end(),
            [](const auto& a, const auto& b) {
                if (a.first != b.first) return a.first < b.first;
                return a.second < b.second;
            });
        if (results.size() > maxResults) results.resize(maxResults);
        return results;
    }

    void GW2API::CacheItemNameId(const std::string& name, uint32_t id) {
        if (name.empty() || id == 0) return;
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_item_name_id_cache.find(name);
        if (it == s_item_name_id_cache.end() || it->second != id) {
            s_item_name_id_cache[name] = id;
            s_item_name_cache_dirty = true;
        }
    }

    bool GW2API::LoadItemNameCache() {
        auto snapshot=Cache::ReadItems(GetDataDirectory(),Cache::ItemDataset::Aliases);
        if(!snapshot)return false;
        std::lock_guard<std::mutex> lock(s_mutex);
        for(const auto& [name,id]:snapshot->items())
            s_item_name_id_cache.emplace(name,id.get<uint32_t>());
        return true;
    }

    bool GW2API::SaveItemNameCache() {
        // Serialize saves without holding the API mutex during SQL. A failed
        // write or a concurrent change must leave the dirty flag set.
        static std::mutex saveMutex;
        std::lock_guard<std::mutex> persist(saveMutex);
        std::unordered_map<std::string,uint32_t> snapshot;
        int64_t captured=0;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(!s_item_name_cache_dirty)return true;
            snapshot=s_item_name_id_cache;
            captured=Cache::ReferenceSnapshotTime();
        }
        json data=json::object();
        for(const auto& [name,id]:snapshot)data[name]=id;
        if(!Cache::WriteItems(GetDataDirectory(),Cache::ItemDataset::Aliases,data,captured))
            return false;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(s_item_name_id_cache==snapshot)s_item_name_cache_dirty=false;
        }
        return true;
    }

    std::shared_ptr<const SkinInfo> GW2API::GetSkinInfo(uint32_t skin_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_skin_cache.find(skin_id);
        return (it != s_skin_cache.end()) ? it->second : nullptr;
    }

    std::shared_ptr<const SpecializationInfo> GW2API::GetSpecInfo(uint32_t spec_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_spec_cache.find(spec_id);
        return (it != s_spec_cache.end()) ? it->second : nullptr;
    }

    std::shared_ptr<const TraitInfo> GW2API::GetTraitInfo(uint32_t trait_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_trait_cache.find(trait_id);
        return (it != s_trait_cache.end()) ? it->second : nullptr;
    }

    std::shared_ptr<const SkillInfo> GW2API::GetSkillInfo(uint32_t skill_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_skill_cache.find(skill_id);
        return (it != s_skill_cache.end()) ? it->second : nullptr;
    }

    std::shared_ptr<const DyeColor> GW2API::GetDyeColor(int color_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_dye_cache.find(color_id);
        return (it != s_dye_cache.end()) ? it->second : nullptr;
    }

    std::shared_ptr<const ItemStatInfo> GW2API::GetItemStatInfo(uint32_t stat_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_itemstat_cache.find(stat_id);
        return (it != s_itemstat_cache.end()) ? it->second : nullptr;
    }

    std::unordered_map<uint32_t, std::shared_ptr<const ItemStatInfo>> GW2API::GetAllItemStats() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_itemstat_cache;
    }

    void GW2API::FetchAllItemStatsAsync(bool manual) {
        bool missing;{std::lock_guard<std::mutex> lock(s_mutex);missing=s_itemstat_cache.empty();}
        if(!PublicCatalog::BeginDataset("public_stats",manual,missing))return;
        const auto root=GetDataDirectory();
        Background::Workers().Launch([root,manual]{
            ApiActivity::ReasonScope reason(manual?ApiActivity::Reason::Manual:ApiActivity::Reason::Background);
            PublicCatalog::Attempt(root,"public_stats");
            auto raw=PublicCatalog::Fetch("/v2/itemstats?ids=all");
            if(!raw.is_array()||raw.size()<50)throw std::runtime_error("Incomplete item stats");
            std::map<uint32_t,ItemStatInfo> staged;
            for(const auto& r:raw){
                ItemStatInfo info;info.id=Cache::ReferenceId(r.at("id"));info.name=r.at("name").get<std::string>();
                if(info.name.empty())continue; // nonselectable unnamed stat records
                for(const auto& a:r.at("attributes"))info.attributes.push_back(a.at("attribute").get<std::string>());
                if(!staged.emplace(info.id,std::move(info)).second)throw std::runtime_error("Duplicate item stat");
            }
            if(staged.size()<50)throw std::runtime_error("Incomplete named item stats");
            json rows=json::array();
            for(const auto& [id,info]:staged)rows.push_back({{"id",id},{"name",info.name},{"attributes",info.attributes}});
            Background::Checkpoint();
            PublicCatalog::WithDb(root,[&](auto& db){
                Database::Transaction tx(db);
                // Retain historical stat identities still used by saved gear.
                auto previous=Cache::ReadReferenceRows(db,Cache::ReferenceDataset::ItemStats);
                for(const auto& r:previous)if(!staged.count(Cache::ReferenceId(r.at("id"))))rows.push_back(r);
                Cache::StoreReferenceSnapshot(db,Cache::ReferenceDataset::ItemStats,rows,Cache::ReferenceSnapshotTime());
                PublicCatalog::Mark(db,"public_stats",PublicCatalog::Now());tx.Commit();PublicCatalog::Publish(db);
            });
            {std::lock_guard<std::mutex> lock(s_mutex);for(auto& [id,info]:staged)s_itemstat_cache[id]=std::make_shared<const ItemStatInfo>(std::move(info));}
            PublicCatalog::EndDataset("public_stats",true);
        },[]{PublicCatalog::EndDataset("public_stats",false);});
    }

    std::shared_ptr<const ProfessionInfo> GW2API::GetProfessionInfo(const std::string& profession) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_profession_cache.find(profession);
        return (it != s_profession_cache.end()) ? it->second : nullptr;
    }

    std::shared_ptr<const std::map<std::string, ProfessionWeaponData>> GW2API::GetProfessionWeapons(const std::string& profession) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_profession_weapons.find(profession);
        return (it != s_profession_weapons.end()) ? it->second : nullptr;
    }

    void GW2API::FetchProfessionInfoAsync(const std::string& profession) {
        // This function is called every frame by several views. Without an
        // in-flight guard, a slow first response can create hundreds of
        // simultaneous detached workers for the same profession.
        static const std::set<std::string> validProfessions = {
            "Guardian", "Warrior", "Engineer", "Ranger", "Thief",
            "Elementalist", "Mesmer", "Necromancer", "Revenant"
        };
        static std::unordered_set<std::string> inFlight;
        if (!validProfessions.count(profession)) return;

        const std::string prof = profession; // own a stable copy before dispatch
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_profession_cache.find(prof) != s_profession_cache.end() ||
                !inFlight.insert(prof).second) return;
        }

        try {
            AlterEgo::Background::Workers().Launch([prof]() {
                try {
                    const std::string url = "https://api.guildwars2.com/v2/professions/" + prof;
                    const std::string response = HttpGet(url);
                    if (!response.empty()) {
                        json j = json::parse(response);
                        ProfessionInfo info;
                        info.name = j.value("name", prof);
                        info.icon_url = j.value("icon", "");
                        info.icon_big_url = j.value("icon_big", "");
                        if (j.contains("specializations") && j["specializations"].is_array())
                            for (const auto& spec : j["specializations"])
                                if (spec.is_number_unsigned())
                                    info.specialization_ids.push_back(spec.get<uint32_t>());

                        const auto specIds = info.specialization_ids;
                        {
                            std::lock_guard<std::mutex> lock(s_mutex);
                            s_profession_cache[prof]=std::make_shared<const ProfessionInfo>(std::move(info));
                        }
                        if (!specIds.empty()) FetchSpecDetailsAsync(specIds);
                        if (s_api) {
                            const std::string msg = "Loaded profession info for " + prof;
                            s_api->Log(LOGL_INFO, "AlterEgo", msg.c_str());
                        }
                    }
                } catch (const std::exception& e) {
                    if (s_api) s_api->Log(LOGL_WARNING, "AlterEgo",
                        (std::string("Profession metadata request failed: ") + e.what()).c_str());
                } catch (...) {}
                // Permit retry after failures; the cache prevents new requests
                // after a successful response. Always clear on worker exit.
                std::lock_guard<std::mutex> lock(s_mutex);
                inFlight.erase(prof);
            }, [prof]() { std::lock_guard<std::mutex> lock(s_mutex); inFlight.erase(prof); });
        } catch (...) {
            std::lock_guard<std::mutex> lock(s_mutex);
            inFlight.erase(prof);
        }
    }

    // =========================================================================
    // Public endpoint fetching (direct HTTP, no auth needed)
    // =========================================================================

    std::string GW2API::HttpGet(const std::string& url) {
        return Skinventory::HttpClient::Get(url);
    }

    // --- Batch fetch: items ---

    void GW2API::AdoptPublicItems(const nlohmann::json& rows) {
        std::lock_guard<std::mutex> lock(s_mutex);
        for(const auto& item:rows) {
            ItemInfo info;info.id=item.at("id").get<uint32_t>();
            info.name=item.value("name",std::string{});
            info.icon_url=item.value("icon",std::string{});
            info.rarity=item.value("rarity",std::string{});
            info.type=item.value("type",std::string{});
            info.chat_link=item.value("chat_link",std::string{});
            info.default_skin=item.value("default_skin",0u);
            info.description=StripHtmlTags(item.value("description",std::string{}));
            if(item.contains("details"))info.details=item.at("details");
            s_item_cache[info.id]=std::make_shared<const ItemInfo>(std::move(info));
        }
    }

    void GW2API::FetchItemDetails(const std::vector<uint32_t>& item_ids, bool forceRefetch) {
        ApiActivity::ReasonScope reason(ApiActivity::Reason::Manual);
        std::vector<uint32_t> missing;
        {std::lock_guard<std::mutex> lock(s_mutex);for(auto id:item_ids)if(id&&(forceRefetch||!s_item_cache.count(id)))missing.push_back(id);}
        if(missing.empty())return;
        auto claim=RefreshEngine::ClaimIds("items",missing,forceRefetch);
        if(!claim->ids.empty())FetchItemDetailsClaimed(*claim,forceRefetch);
        claim.reset();
        RefreshEngine::WaitForIds("items",missing,[]{return Background::Workers().Stopping();});
    }

    void GW2API::FetchItemDetailsClaimed(RefreshEngine::IdBatch& claim, bool forceRefetch) {
        const auto& item_ids=claim.ids;
        if (item_ids.empty()) return;

        std::vector<uint32_t> to_fetch;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (uint32_t id : item_ids) {
                if (id == 0) continue;
                if (forceRefetch || s_item_cache.find(id) == s_item_cache.end())
                    to_fetch.push_back(id);
            }
        }
        if (to_fetch.empty()) return;

        std::sort(to_fetch.begin(), to_fetch.end());
        to_fetch.erase(std::unique(to_fetch.begin(), to_fetch.end()), to_fetch.end());

        const size_t BATCH_SIZE = 200;
        for (size_t i = 0; i < to_fetch.size(); i += BATCH_SIZE) {
            if (AlterEgo::Background::Workers().Stopping()) return;
            std::string ids_param;
            size_t end = std::min(i + BATCH_SIZE, to_fetch.size());
            for (size_t j = i; j < end; j++) {
                if (!ids_param.empty()) ids_param += ",";
                ids_param += std::to_string(to_fetch[j]);
            }

            std::string url = "https://api.guildwars2.com/v2/items?ids=" + ids_param;
            std::string response = HttpGet(url);
            if (Background::Workers().Stopping()) return;
            claim.MarkAttempted(to_fetch.begin()+i,to_fetch.begin()+end);
            if (response.empty()) continue;

            try {
                json j = json::parse(response);
                if (j.is_array()) {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    for (const auto& item : j) {
                        if (!item.contains("id")) continue;
                        ItemInfo info;
                        info.id = item["id"].get<uint32_t>();
                        info.name = item.value("name", "");
                        if(info.name.empty()||!std::binary_search(to_fetch.begin()+i,to_fetch.begin()+end,info.id))continue;
                        info.icon_url = item.value("icon", "");
                        info.rarity = item.value("rarity", "");
                        info.type = item.value("type", "");
                        info.chat_link = item.value("chat_link", "");
                        info.default_skin = item.value("default_skin", 0u);
                        if (item.contains("description"))
                            info.description = StripHtmlTags(item["description"].get<std::string>());
                        if (item.contains("details"))
                            info.details = item["details"];
                        // Also populate persistent name→ID cache
                        if (!info.name.empty()) {
                            s_item_name_id_cache[info.name] = info.id;
                            s_item_name_cache_dirty = true;
                        }
                        s_item_cache[info.id]=std::make_shared<const ItemInfo>(info);
                    }
                }
            } catch (...) {}

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        SaveItemCache();
    }

    void GW2API::FetchSkinDetails(RefreshEngine::IdBatch& claim) {
        const auto& skin_ids=claim.ids;
        if (skin_ids.empty()) return;

        std::vector<uint32_t> to_fetch;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (uint32_t id : skin_ids) {
                // Re-fetch legacy cached armor entries missing the new weight metadata.
                // Otherwise a user updating from older Alter Ego builds could never
                // determine Light/Medium/Heavy for imported wardrobe links.
                if (id == 0) continue;
                auto found = s_skin_cache.find(id);
                if (found == s_skin_cache.end() ||
                    (found->second->type == "Armor" && found->second->weight_class.empty()))
                    to_fetch.push_back(id);
            }
        }
        if (to_fetch.empty()) return;

        std::sort(to_fetch.begin(), to_fetch.end());
        to_fetch.erase(std::unique(to_fetch.begin(), to_fetch.end()), to_fetch.end());

        const size_t BATCH_SIZE = 200;
        for (size_t i = 0; i < to_fetch.size(); i += BATCH_SIZE) {
            if (AlterEgo::Background::Workers().Stopping()) return;
            std::string ids_param;
            size_t end = std::min(i + BATCH_SIZE, to_fetch.size());
            for (size_t j = i; j < end; j++) {
                if (!ids_param.empty()) ids_param += ",";
                ids_param += std::to_string(to_fetch[j]);
            }

            std::string url = "https://api.guildwars2.com/v2/skins?ids=" + ids_param;
            std::string response = HttpGet(url);
            if (Background::Workers().Stopping()) return;
            claim.MarkAttempted(to_fetch.begin()+i,to_fetch.begin()+end);
            if (response.empty()) continue;

            try {
                json j = json::parse(response);
                if (j.is_array()) {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    for (const auto& skin : j) {
                        if (!skin.contains("id")) continue;
                        SkinInfo info;
                        info.id = skin["id"].get<uint32_t>();
                        info.name = skin.value("name", "");
                        if(info.name.empty()||!std::binary_search(to_fetch.begin()+i,to_fetch.begin()+end,info.id))continue;
                        info.icon_url = skin.value("icon", "");
                        info.type = skin.value("type", "");
                        info.rarity = skin.value("rarity", "");
                        if (skin.contains("details") && skin["details"].is_object())
                            info.weight_class = skin["details"].value("weight_class", std::string());
                        // Extract dye slot count and per-channel material from details.dye_slots.default
                        // Null entries = unused channels, only count non-null
                        if (skin.contains("details") && skin["details"].contains("dye_slots") &&
                            skin["details"]["dye_slots"].contains("default") &&
                            skin["details"]["dye_slots"]["default"].is_array()) {
                            int count = 0;
                            for (const auto& ds : skin["details"]["dye_slots"]["default"]) {
                                if (!ds.is_null()) {
                                    count++;
                                    info.dye_slot_materials.push_back(
                                        ds.value("material", "cloth"));
                                } else {
                                    info.dye_slot_materials.push_back("");
                                }
                            }
                            info.dye_slot_count = count;
                        }
                        s_skin_cache[info.id]=std::make_shared<const SkinInfo>(info);
                    }
                }
            } catch (...) {}

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        SaveSkinCache();
    }

    // --- Async wrappers ---

    void GW2API::FetchItemDetailsAsync(const std::vector<uint32_t>& item_ids, bool forceRefetch) {
        std::vector<uint32_t> missing;
        {std::lock_guard<std::mutex> lock(s_mutex);for(auto id:item_ids)if(id&&(forceRefetch||!s_item_cache.count(id)))missing.push_back(id);}
        auto claim=RefreshEngine::ClaimIds("items",missing,forceRefetch);
        if(claim->ids.empty())return;
        AlterEgo::Background::Workers().Launch([claim, forceRefetch]() { ApiActivity::ReasonScope reason(forceRefetch?ApiActivity::Reason::Manual:ApiActivity::Reason::Visible);FetchItemDetailsClaimed(*claim, forceRefetch); });
    }

    void GW2API::FetchSkinDetailsAsync(const std::vector<uint32_t>& skin_ids) {
        std::vector<uint32_t> missing;
        {std::lock_guard<std::mutex> lock(s_mutex);for(auto id:skin_ids){auto it=s_skin_cache.find(id);
            if(id&&(it==s_skin_cache.end()||(it->second->type=="Armor"&&it->second->weight_class.empty())))missing.push_back(id);}}
        auto claim=RefreshEngine::ClaimIds("skins",missing);
        if(claim->ids.empty())return;
        AlterEgo::Background::Workers().Launch([claim]() { ApiActivity::ReasonScope reason(ApiActivity::Reason::Visible);FetchSkinDetails(*claim); });
    }

    void GW2API::FetchSpecDetailsAsync(const std::vector<uint32_t>& spec_ids) {
        std::vector<uint32_t> missing;
        {std::lock_guard<std::mutex> lock(s_mutex);for(auto id:spec_ids)if(id&&!s_spec_cache.count(id))missing.push_back(id);}
        auto claim=RefreshEngine::ClaimIds("specializations",missing);
        if(claim->ids.empty())return;
        auto ids=claim->ids;
        AlterEgo::Background::Workers().Launch([ids,claim]() {
            ApiActivity::ReasonScope reason(ApiActivity::Reason::Visible);
            if (ids.empty()) return;
            std::vector<uint32_t> to_fetch;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                for (uint32_t id : ids) {
                    if (id != 0 && s_spec_cache.find(id) == s_spec_cache.end())
                        to_fetch.push_back(id);
                }
            }
            if (to_fetch.empty()) return;

            std::sort(to_fetch.begin(), to_fetch.end());
            to_fetch.erase(std::unique(to_fetch.begin(), to_fetch.end()), to_fetch.end());

            std::string ids_param;
            for (size_t i = 0; i < to_fetch.size(); i++) {
                if (i > 0) ids_param += ",";
                ids_param += std::to_string(to_fetch[i]);
            }

            std::string url = "https://api.guildwars2.com/v2/specializations?ids=" + ids_param;
            std::string response = HttpGet(url);
            if (Background::Workers().Stopping()) return;
            claim->MarkAttempted(to_fetch.begin(),to_fetch.end());
            if (response.empty()) return;

            try {
                json j = json::parse(response);
                if (j.is_array()) {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    for (const auto& spec : j) {
                        if (!spec.contains("id")) continue;
                        SpecializationInfo info;
                        info.id = spec["id"].get<uint32_t>();
                        info.name = spec.value("name", "");
                        if(info.name.empty()||!std::binary_search(to_fetch.begin(),to_fetch.end(),info.id))continue;
                        info.icon_url = spec.value("icon", "");
                        info.background_url = spec.value("background", "");
                        info.profession = spec.value("profession", "");
                        info.profession_icon_url = spec.value("profession_icon", "");
                        info.profession_icon_big_url = spec.value("profession_icon_big", "");
                        info.elite = spec.value("elite", false);
                        if (spec.contains("minor_traits")) {
                            for (const auto& t : spec["minor_traits"])
                                info.minor_traits.push_back(t.get<uint32_t>());
                        }
                        if (spec.contains("major_traits")) {
                            for (const auto& t : spec["major_traits"])
                                info.major_traits.push_back(t.get<uint32_t>());
                        }
                        s_spec_cache[info.id]=std::make_shared<const SpecializationInfo>(info);
                    }
                }
            } catch (...) {}

            SaveSpecCache();

            // Chain-fetch all trait details for loaded specs
            std::vector<uint32_t> all_trait_ids;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                for (uint32_t sid : to_fetch) {
                    auto it = s_spec_cache.find(sid);
                    if (it == s_spec_cache.end()) continue;
                    for (uint32_t tid : it->second->minor_traits) all_trait_ids.push_back(tid);
                    for (uint32_t tid : it->second->major_traits) all_trait_ids.push_back(tid);
                }
            }
            if (!all_trait_ids.empty()) {
                FetchTraitDetailsAsync(all_trait_ids);
            }
        });
    }

    void GW2API::FetchTraitDetails(RefreshEngine::IdBatch& claim) {
        const auto& trait_ids=claim.ids;
        if (trait_ids.empty()) return;
        std::vector<uint32_t> to_fetch;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (uint32_t id : trait_ids) {
                if (id != 0 && s_trait_cache.find(id) == s_trait_cache.end())
                    to_fetch.push_back(id);
            }
        }
        if (to_fetch.empty()) return;

        std::sort(to_fetch.begin(), to_fetch.end());
        to_fetch.erase(std::unique(to_fetch.begin(), to_fetch.end()), to_fetch.end());

        const size_t BATCH_SIZE = 200;
        for (size_t i = 0; i < to_fetch.size(); i += BATCH_SIZE) {
            if (AlterEgo::Background::Workers().Stopping()) return;
            std::string ids_param;
            size_t end = std::min(i + BATCH_SIZE, to_fetch.size());
            for (size_t j = i; j < end; j++) {
                if (!ids_param.empty()) ids_param += ",";
                ids_param += std::to_string(to_fetch[j]);
            }

            std::string response = HttpGet("https://api.guildwars2.com/v2/traits?ids=" + ids_param);
            if (Background::Workers().Stopping()) return;
            claim.MarkAttempted(to_fetch.begin()+i,to_fetch.begin()+end);
            if (response.empty()) continue;

            try {
                json j = json::parse(response);
                if (j.is_array()) {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    for (const auto& trait : j) {
                        if (!trait.contains("id")) continue;
                        TraitInfo info;
                        info.id = trait["id"].get<uint32_t>();
                        info.name = trait.value("name", "");
                        if(info.name.empty()||!std::binary_search(to_fetch.begin()+i,to_fetch.begin()+end,info.id))continue;
                        info.icon_url = trait.value("icon", "");
                        if (trait.contains("description"))
                            info.description = StripHtmlTags(trait["description"].get<std::string>());
                        info.tier = trait.value("tier", 0);
                        info.order = trait.value("order", 0);
                        info.slot = trait.value("slot", "");
                        if (trait.contains("facts"))
                            info.facts = trait["facts"];
                        s_trait_cache[info.id]=std::make_shared<const TraitInfo>(info);
                    }
                }
            } catch (...) {}

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        SaveTraitCache();
    }

    void GW2API::FetchTraitDetailsAsync(const std::vector<uint32_t>& trait_ids) {
        std::vector<uint32_t> missing;
        {std::lock_guard<std::mutex> lock(s_mutex);for(auto id:trait_ids)if(id&&!s_trait_cache.count(id))missing.push_back(id);}
        auto claim=RefreshEngine::ClaimIds("traits",missing);
        if(claim->ids.empty())return;
        AlterEgo::Background::Workers().Launch([claim]() { ApiActivity::ReasonScope reason(ApiActivity::Reason::Visible);FetchTraitDetails(*claim); });
    }

    void GW2API::FetchSkillDetailsAsync(const std::vector<uint32_t>& skill_ids) {
        std::vector<uint32_t> missing;
        {std::lock_guard<std::mutex> lock(s_mutex);for(auto id:skill_ids)if(id&&!s_skill_cache.count(id))missing.push_back(id);}
        auto claim=RefreshEngine::ClaimIds("skills",missing);
        if(claim->ids.empty())return;
        auto ids=claim->ids;
        AlterEgo::Background::Workers().Launch([ids,claim]() {
            ApiActivity::ReasonScope reason(ApiActivity::Reason::Visible);
            if (ids.empty()) return;
            std::vector<uint32_t> to_fetch;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                for (uint32_t id : ids) {
                    if (id != 0 && s_skill_cache.find(id) == s_skill_cache.end())
                        to_fetch.push_back(id);
                }
            }
            if (to_fetch.empty()) return;

            std::sort(to_fetch.begin(), to_fetch.end());
            to_fetch.erase(std::unique(to_fetch.begin(), to_fetch.end()), to_fetch.end());

            const size_t BATCH_SIZE = 200;
            for (size_t i = 0; i < to_fetch.size(); i += BATCH_SIZE) {
            if (AlterEgo::Background::Workers().Stopping()) return;
                std::string ids_param;
                size_t end = std::min(i + BATCH_SIZE, to_fetch.size());
                for (size_t j = i; j < end; j++) {
                    if (!ids_param.empty()) ids_param += ",";
                    ids_param += std::to_string(to_fetch[j]);
                }

                std::string response = HttpGet("https://api.guildwars2.com/v2/skills?ids=" + ids_param);
            if (Background::Workers().Stopping()) return;
            claim->MarkAttempted(to_fetch.begin()+i,to_fetch.begin()+end);
                if (response.empty()) continue;

                try {
                    json j = json::parse(response);
                    if (j.is_array()) {
                        std::lock_guard<std::mutex> lock(s_mutex);
                        for (const auto& skill : j) {
                            if (!skill.contains("id")) continue;
                            SkillInfo info;
                            info.id = skill["id"].get<uint32_t>();
                            info.name = skill.value("name", "");
                            if(info.name.empty()||!std::binary_search(to_fetch.begin()+i,to_fetch.begin()+end,info.id))continue;
                            info.icon_url = skill.value("icon", "");
                            if (skill.contains("description"))
                                info.description = StripHtmlTags(skill["description"].get<std::string>());
                            info.type = skill.value("type", "");
                            info.specialization = skill.value("specialization", 0u);
                            if (skill.contains("professions") && skill["professions"].is_array())
                                for (const auto& p : skill["professions"])
                                    if (p.is_string()) info.professions.push_back(p.get<std::string>());
                            if (skill.contains("facts"))
                                info.facts = skill["facts"];
                            s_skill_cache[info.id]=std::make_shared<const SkillInfo>(info);
                        }
                    }
                } catch (...) {}

                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            SaveSkillCache();
        });
    }

    void GW2API::FetchItemStatDetailsAsync(const std::vector<uint32_t>& stat_ids) {
        std::vector<uint32_t> missing;
        {std::lock_guard<std::mutex> lock(s_mutex);for(auto id:stat_ids)if(id&&!s_itemstat_cache.count(id))missing.push_back(id);}
        auto claim=RefreshEngine::ClaimIds("itemstats",missing);
        if(claim->ids.empty())return;
        auto ids=claim->ids;
        AlterEgo::Background::Workers().Launch([ids,claim]() {
            ApiActivity::ReasonScope reason(ApiActivity::Reason::Visible);
            if (ids.empty()) return;
            std::vector<uint32_t> to_fetch;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                for (uint32_t id : ids) {
                    if (id != 0 && s_itemstat_cache.find(id) == s_itemstat_cache.end())
                        to_fetch.push_back(id);
                }
            }
            if (to_fetch.empty()) return;

            std::sort(to_fetch.begin(), to_fetch.end());
            to_fetch.erase(std::unique(to_fetch.begin(), to_fetch.end()), to_fetch.end());

            const size_t BATCH_SIZE = 200;
            for (size_t i = 0; i < to_fetch.size(); i += BATCH_SIZE) {
            if (AlterEgo::Background::Workers().Stopping()) return;
                std::string ids_param;
                size_t end = std::min(i + BATCH_SIZE, to_fetch.size());
                for (size_t j = i; j < end; j++) {
                    if (!ids_param.empty()) ids_param += ",";
                    ids_param += std::to_string(to_fetch[j]);
                }

                std::string response = HttpGet("https://api.guildwars2.com/v2/itemstats?ids=" + ids_param);
            if (Background::Workers().Stopping()) return;
            claim->MarkAttempted(to_fetch.begin()+i,to_fetch.begin()+end);
                if (response.empty()) continue;

                try {
                    json j = json::parse(response);
                    if (j.is_array()) {
                        std::lock_guard<std::mutex> lock(s_mutex);
                        for (const auto& stat : j) {
                            if (!stat.contains("id")) continue;
                            ItemStatInfo info;
                            info.id = stat["id"].get<uint32_t>();
                            info.name = stat.value("name", "");
                            if(info.name.empty()||!std::binary_search(to_fetch.begin()+i,to_fetch.begin()+end,info.id))continue;
                            if (stat.contains("attributes") && stat["attributes"].is_array()) {
                                for (const auto& attr : stat["attributes"]) {
                                    info.attributes.push_back(attr.value("attribute", ""));
                                }
                            }
                            s_itemstat_cache[info.id]=std::make_shared<const ItemStatInfo>(info);
                        }
                    }
                } catch (...) {}

                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            SaveItemStatCache();
        });
    }

    void GW2API::FetchDyeColorsAsync(bool manual) {
        bool missing;{std::lock_guard<std::mutex> lock(s_mutex);missing=s_dye_cache.empty();}
        if(!PublicCatalog::BeginDataset("public_dyes",manual,missing))return;
        const auto root=GetDataDirectory();
        Background::Workers().Launch([root,manual]{
            ApiActivity::ReasonScope reason(manual?ApiActivity::Reason::Manual:ApiActivity::Reason::Background);
            PublicCatalog::Attempt(root,"public_dyes");
            auto raw=PublicCatalog::Fetch("/v2/colors?ids=all");
            if(!raw.is_array()||raw.size()<100)throw std::runtime_error("Incomplete dye catalog");
            std::map<int,DyeColor> staged;json rows=json::array();
            for(const auto& r:raw){
                DyeColor dc;dc.id=r.at("id").get<int>();dc.name=r.at("name").get<std::string>();
                if(dc.id<0||dc.name.empty())throw std::runtime_error("Invalid dye");
                auto rgb=[&](const json& a,int& x,int& y,int& z){
                    if(!a.is_array()||a.size()!=3)throw std::runtime_error("Invalid dye RGB");
                    json wrapper={{"rgb",a}};
                    x=static_cast<int>(Cache::ReferenceChannel(wrapper,"rgb",0));
                    y=static_cast<int>(Cache::ReferenceChannel(wrapper,"rgb",1));
                    z=static_cast<int>(Cache::ReferenceChannel(wrapper,"rgb",2));
                };
                rgb(r.at("base_rgb"),dc.r,dc.g,dc.b);
                auto mat=[&](const char* key,int& x,int& y,int& z){
                    if(r.contains(key)&&r.at(key).contains("rgb"))rgb(r.at(key).at("rgb"),x,y,z);
                    else{x=dc.r;y=dc.g;z=dc.b;}
                };
                mat("cloth",dc.cloth_r,dc.cloth_g,dc.cloth_b);
                mat("leather",dc.leather_r,dc.leather_g,dc.leather_b);
                mat("metal",dc.metal_r,dc.metal_g,dc.metal_b);
                if(!staged.emplace(dc.id,dc).second)throw std::runtime_error("Duplicate dye");
                rows.push_back({{"id",dc.id},{"name",dc.name},{"base",{dc.r,dc.g,dc.b}},
                    {"cloth",{dc.cloth_r,dc.cloth_g,dc.cloth_b}},{"leather",{dc.leather_r,dc.leather_g,dc.leather_b}},
                    {"metal",{dc.metal_r,dc.metal_g,dc.metal_b}}});
            }
            Background::Checkpoint();
            PublicCatalog::WithDb(root,[&](auto& db){
                Database::Transaction tx(db);
                const json previous=Cache::ReadReferenceRows(db,Cache::ReferenceDataset::Dyes);
                if(rows.size()<previous.size()*9/10)throw std::runtime_error("Truncated dye catalog");
                for(const auto& r:previous)if(!staged.count(r.at("id").get<int>()))rows.push_back(r);
                Cache::StoreReferenceSnapshot(db,Cache::ReferenceDataset::Dyes,rows,Cache::ReferenceSnapshotTime());
                PublicCatalog::Mark(db,"public_dyes",PublicCatalog::Now());tx.Commit();PublicCatalog::Publish(db);
            });
            {std::lock_guard<std::mutex> lock(s_mutex);for(auto& [id,dc]:staged)s_dye_cache[id]=std::make_shared<const DyeColor>(std::move(dc));}
            PublicCatalog::EndDataset("public_dyes",true);
        },[]{PublicCatalog::EndDataset("public_dyes",false);});
    }

    // =========================================================================
    // Persistence
    // =========================================================================

    bool GW2API::SaveCharacterData(const std::string& account,const std::string& name,
        const Character* replacement,const std::string& legacyAccount,const std::function<bool()>& current) {
        EnsureDataDirectory();
        // Serialize snapshot capture and persistence in the same order. No Nexus calls under this lock.
        std::lock_guard<std::mutex> saveLock(g_characterSaveMutex);
        const auto native=Cache::HasCharacterSnapshot(GetDataDirectory());
        if(!native)return false;
        bool incremental=!name.empty() && *native;
        if(current&&!current())return false;
        std::vector<Character> candidate;
        time_t stamp=0;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            candidate=s_characters;stamp=s_last_updated;
        }
        if(replacement){
            const auto before=candidate.size();
            candidate.erase(std::remove_if(candidate.begin(),candidate.end(),[&](const Character& c){
                return c.name==name&&c.account_name!=account&&(c.account_name.empty()||c.account_name==legacyAccount);
            }),candidate.end());
            if(before!=candidate.size())incremental=false;
            auto it=std::find_if(candidate.begin(),candidate.end(),[&](const Character& c){return c.name==name&&c.account_name==account;});
            if(it==candidate.end())candidate.push_back(*replacement);else *it=*replacement;
            stamp=std::max(stamp,replacement->data_refreshed);
        }
        json j;
        {
            j["last_updated"] = (int64_t)stamp;
            json chars_arr = json::array();
            for (const auto& ch : candidate) {
                if(incremental && (ch.name!=name || ch.account_name!=account))continue;
                json cj;
                cj["name"] = ch.name;
                cj["race"] = ch.race;
                cj["gender"] = ch.gender;
                cj["profession"] = ch.profession;
                cj["level"] = ch.level;
                cj["age"] = ch.age;
                cj["created"] = ch.created;
                cj["last_modified"] = ch.last_modified;
                cj["data_refreshed"] = (int64_t)ch.data_refreshed;
                static constexpr const char* stamps[]={"core_verified","builds_verified","tabs_verified","physical_verified","crafting_verified"};
                for(size_t i=0;i<ch.scope_stamps.size();++i)cj[stamps[i]]=ch.scope_stamps[i];
                cj["deaths"] = ch.deaths;
                cj["crafting_cached"] = ch.crafting_cached;
                if (!ch.crafting.empty()) {
                    json craft_arr = json::array();
                    for (size_t ci = 0; ci < ch.crafting.size(); ci++) {
                        json craft;
                        craft["discipline"] = ch.crafting[ci];
                        craft["rating"] = (ci < ch.crafting_levels.size()) ? ch.crafting_levels[ci] : 0;
                        craft["active"] = (ci < ch.crafting_active.size()) ? ch.crafting_active[ci] : true;
                        craft_arr.push_back(craft);
                    }
                    cj["crafting"] = craft_arr;
                }
                cj["active_build_tab"] = ch.active_build_tab;
                cj["active_equipment_tab"] = ch.active_equipment_tab;

                json eq_arr = json::array();
                for (const auto& eq : ch.equipment) {
                    json ej;
                    ej["id"] = eq.id;
                    ej["slot"] = eq.slot;
                    ej["skin"] = eq.skin;
                    ej["tab"] = eq.tab;
                    if(eq.binding_known||!eq.binding.empty())ej["binding"]=eq.binding;
                    if(eq.bound_to_known||!eq.bound_to.empty())ej["bound_to"]=eq.bound_to;
                    if(eq.charges>=0)ej["charges"]=eq.charges;
                    ej["physical_supplement"]=eq.physical_supplement;
                    if (!eq.upgrades.empty()) ej["upgrades"] = eq.upgrades;
                    if (!eq.infusions.empty()) ej["infusions"] = eq.infusions;
                    if (!eq.dyes.empty()) ej["dyes"] = eq.dyes;
                    if (eq.stat_id != 0) ej["stat_id"] = eq.stat_id;
                    if (!eq.attributes.empty()) {
                        json attrs;
                        for (const auto& [k, v] : eq.attributes) attrs[k] = v;
                        ej["attributes"] = attrs;
                    }
                    eq_arr.push_back(ej);
                }
                cj["equipment"] = eq_arr;

                if (!ch.equipment_tab_names.empty()) {
                    json etn;
                    for (const auto& [tab, name] : ch.equipment_tab_names) {
                        etn[std::to_string(tab)] = name;
                    }
                    cj["equipment_tab_names"] = etn;
                }

                json bt_arr = json::array();
                for (const auto& bt : ch.build_tabs) {
                    json bj;
                    bj["tab"] = bt.tab;
                    bj["is_active"] = bt.is_active;
                    bj["name"] = bt.name;
                    bj["profession"] = bt.profession;

                    json specs = json::array();
                    for (int i = 0; i < 3; i++) {
                        json sj;
                        sj["id"] = bt.specializations[i].spec_id;
                        sj["traits"] = {bt.specializations[i].traits[0],
                                        bt.specializations[i].traits[1],
                                        bt.specializations[i].traits[2]};
                        specs.push_back(sj);
                    }
                    bj["specializations"] = specs;

                    auto skillBarToJson = [](const SkillBar& sb) -> json {
                        json sj;
                        sj["heal"] = sb.heal;
                        sj["utilities"] = {sb.utilities[0], sb.utilities[1], sb.utilities[2]};
                        sj["elite"] = sb.elite;
                        return sj;
                    };
                    bj["skills"] = skillBarToJson(bt.terrestrial_skills);
                    bj["aquatic_skills"] = skillBarToJson(bt.aquatic_skills);
                    if(bt.pets_known)bj["pets"]={bt.pets.terrestrial[0],bt.pets.terrestrial[1],bt.pets.aquatic[0],bt.pets.aquatic[1]};
                    if(bt.legends_known)bj["legends"]={bt.legends.terrestrial[0],bt.legends.terrestrial[1],bt.legends.aquatic[0],bt.legends.aquatic[1]};
                    if(bt.weapons_known||!bt.weapons.empty())bj["weapons"]=bt.weapons;

                    bt_arr.push_back(bj);
                }
                cj["build_tabs"] = bt_arr;

                if (!ch.account_name.empty()) {
                    cj["account_name"] = ch.account_name;
                }

                chars_arr.push_back(cj);
            }
            j["characters"] = chars_arr;
        }

        if(incremental && j["characters"].size()!=1)return false;
        if(current&&!current())return false;
        if(!Cache::WriteCharacters(GetDataDirectory(),j,incremental))return false;
        if(current&&!current())return false;
        if(replacement){
            std::lock_guard<std::mutex> lock(s_mutex);
            s_characters=std::move(candidate);s_has_character_data=!s_characters.empty();
            s_last_updated=stamp;++g_characterDataRevision;
        }
        return true;
    }

    bool GW2API::LoadCharacterData() {
        try {
            auto loaded=Cache::ReadCharacters(GetDataDirectory());
            if(!loaded)return false;
            const auto& j=*loaded;

            std::lock_guard<std::mutex> lock(s_mutex);
            if (!j.contains("characters") || !j["characters"].is_array())
                return false;
            std::vector<Character> restored;
            const int64_t loadedAt = j.value("last_updated", (int64_t)0);
            {
                for (const auto& cj : j["characters"]) {
                    Character ch;
                    ch.name = cj.value("name", "");
                    ch.race = cj.value("race", "");
                    ch.gender = cj.value("gender", "");
                    ch.profession = cj.value("profession", "");
                    ch.level = cj.value("level", 0);
                    ch.age = cj.value("age", 0);
                    ch.created = cj.value("created", "");
                    ch.last_modified = cj.value("last_modified", "");
                    ch.data_refreshed = (time_t)cj.value("data_refreshed", (int64_t)0);
                    static constexpr const char* stamps[]={"core_verified","builds_verified","tabs_verified","physical_verified","crafting_verified"};
                    for(size_t i=0;i<ch.scope_stamps.size();++i)ch.scope_stamps[i]=cj.value(stamps[i],int64_t(0));
                    ch.deaths = cj.value("deaths", 0);
                    // Older character caches contain crafting arrays but no
                    // explicit fetched marker. Keep them valid automatically.
                    ch.crafting_cached = cj.value("crafting_cached", cj.contains("crafting"));
                    if (cj.contains("crafting") && cj["crafting"].is_array()) {
                        for (const auto& craft : cj["crafting"]) {
                            ch.crafting.push_back(craft.value("discipline", ""));
                            ch.crafting_levels.push_back(craft.value("rating", 0));
                            ch.crafting_active.push_back(craft.value("active", true));
                        }
                    }
                    ch.active_build_tab = cj.value("active_build_tab", 0);
                    ch.active_equipment_tab = cj.value("active_equipment_tab", 0);

                    if (cj.contains("equipment_tab_names") && cj["equipment_tab_names"].is_object()) {
                        for (auto it = cj["equipment_tab_names"].begin(); it != cj["equipment_tab_names"].end(); ++it) {
                            ch.equipment_tab_names[std::stoi(it.key())] = it.value().get<std::string>();
                        }
                    }

                    if (cj.contains("equipment") && cj["equipment"].is_array()) {
                        for (const auto& ej : cj["equipment"]) {
                            EquipmentItem eq;
                            eq.id = ej.value("id", (uint32_t)0);
                            eq.slot = ej.value("slot", "");
                            eq.skin = ej.value("skin", (uint32_t)0);
                            eq.tab = ej.value("tab", 0);
                            eq.binding_known=ej.contains("binding");
                            eq.bound_to_known=ej.contains("bound_to");
                            eq.binding=ej.value("binding",std::string());
                            eq.bound_to=ej.value("bound_to",std::string());
                            eq.charges=ej.value("charges",int64_t(-1));
                            eq.physical_supplement=ej.value("physical_supplement",false);
                            if (ej.contains("upgrades")) {
                                for (const auto& u : ej["upgrades"])
                                    eq.upgrades.push_back(u.get<uint32_t>());
                            }
                            if (ej.contains("infusions")) {
                                for (const auto& inf : ej["infusions"])
                                    eq.infusions.push_back(inf.get<uint32_t>());
                            }
                            if (ej.contains("dyes")) {
                                for (const auto& d : ej["dyes"])
                                    eq.dyes.push_back(d.get<int>());
                            }
                            eq.stat_id = ej.value("stat_id", (uint32_t)0);
                            if (ej.contains("attributes") && ej["attributes"].is_object()) {
                                for (auto it = ej["attributes"].begin(); it != ej["attributes"].end(); ++it) {
                                    eq.attributes[it.key()] = it.value().get<int>();
                                }
                            }
                            ch.equipment.push_back(eq);
                        }
                    }

                    if (cj.contains("build_tabs") && cj["build_tabs"].is_array()) {
                        for (const auto& bj : cj["build_tabs"]) {
                            BuildTemplate bt;
                            bt.tab = bj.value("tab", 0);
                            bt.is_active = bj.value("is_active", false);
                            bt.name = bj.value("name", "");
                            bt.profession = bj.value("profession", "");

                            if (bj.contains("specializations") && bj["specializations"].is_array()) {
                                int idx = 0;
                                for (const auto& sj : bj["specializations"]) {
                                    if (idx >= 3) break;
                                    bt.specializations[idx].spec_id = sj.value("id", (uint32_t)0);
                                    if (sj.contains("traits") && sj["traits"].is_array()) {
                                        int ti = 0;
                                        for (const auto& t : sj["traits"]) {
                                            if (ti >= 3) break;
                                            bt.specializations[idx].traits[ti] = t.get<int>();
                                            ti++;
                                        }
                                    }
                                    idx++;
                                }
                            }

                            auto parseSkillBarJson = [](const json& sj) -> SkillBar {
                                SkillBar sb;
                                sb.heal = sj.value("heal", (uint32_t)0);
                                if (sj.contains("utilities") && sj["utilities"].is_array()) {
                                    int i = 0;
                                    for (const auto& u : sj["utilities"]) {
                                        if (i >= 3) break;
                                        sb.utilities[i] = u.get<uint32_t>();
                                        i++;
                                    }
                                }
                                sb.elite = sj.value("elite", (uint32_t)0);
                                return sb;
                            };

                            if (bj.contains("skills"))
                                bt.terrestrial_skills = parseSkillBarJson(bj["skills"]);
                            if (bj.contains("aquatic_skills"))
                                bt.aquatic_skills = parseSkillBarJson(bj["aquatic_skills"]);

                            if(bj.contains("pets")&&bj["pets"].is_array()&&bj["pets"].size()==4) {
                                bt.pets_known=true;
                                for(int i=0;i<2;++i){bt.pets.terrestrial[i]=bj["pets"][i].get<uint32_t>();bt.pets.aquatic[i]=bj["pets"][i+2].get<uint32_t>();}
                            }
                            if(bj.contains("legends")&&bj["legends"].is_array()&&bj["legends"].size()==4) {
                                bt.legends_known=true;
                                for(int i=0;i<2;++i){bt.legends.terrestrial[i]=bj["legends"][i].get<std::string>();bt.legends.aquatic[i]=bj["legends"][i+2].get<std::string>();}
                            }
                            if(bj.contains("weapons")&&bj["weapons"].is_array()) {
                                bt.weapons_known=true;bt.weapons=bj["weapons"].get<std::vector<uint32_t>>();
                            }
                            ch.build_tabs.push_back(bt);
                        }
                    }

                    ch.account_name = cj.value("account_name", "");

                    if (!ch.name.empty()) restored.push_back(std::move(ch));
                }
            }

            // Do not mutate the previous working cache until the entire file
            // parses successfully. Also reconstruct account ownership before
            // H&S sends a fresh accounts response.
            s_characters = std::move(restored);
            s_has_character_data = !s_characters.empty();
            s_last_updated = loadedAt;
            g_characterDataRevision.fetch_add(1);
            return true;
        } catch (...) {
            return false;
        }
    }

    // =========================================================================
    // Build Library
    // =========================================================================

    static const char* GameModeToStr(GameMode m) {
        switch (m) {
            case GameMode::PvE:     return "PvE";
            case GameMode::WvW:     return "WvW";
            case GameMode::PvP:     return "PvP";
            case GameMode::Raid:    return "Raid";
            case GameMode::Fractal: return "Fractal";
            default:                return "Other";
        }
    }

    static GameMode StrToGameMode(const std::string& s) {
        if (s == "PvE")     return GameMode::PvE;
        if (s == "WvW")     return GameMode::WvW;
        if (s == "PvP")     return GameMode::PvP;
        if (s == "Raid")    return GameMode::Raid;
        if (s == "Fractal") return GameMode::Fractal;
        return GameMode::Other;
    }

    const std::vector<SavedBuild>& GW2API::GetSavedBuilds() {
        return s_saved_builds;
    }

    static json SerializeSavedBuild(const SavedBuild& b);
    static bool DeserializeSavedBuild(const json& j,SavedBuild& b);
    static std::string LookupComponentDisplayName(const std::string&,uint32_t);

    int GW2API::AddSavedBuild(SavedBuild build,const std::string& setId,const std::string& newSetName,const std::set<std::string>& kinds) {
        int position=-1;
        const bool ok=CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            if(build.id.empty()) {
                const auto prefix=std::to_string(std::time(nullptr))+"_";
                unsigned n=static_cast<unsigned>(doc.loadouts.size());
                do {build.id=prefix+std::to_string(n++);}while(Composable::FindLoadout(doc,build.id));
            }
            if(Composable::FindLoadout(doc,build.id))return false;
            if(!build.created)build.created=std::time(nullptr);
            position=static_cast<int>(doc.loadouts.size());
            for(int i=position-1;i>=0;--i)if(Composable::ProfessionOf(doc,doc.loadouts[i])==build.profession){position=i+1;break;}
            if(!NativeLibrary::PutLoadout(doc,SerializeSavedBuild(build),error,LookupComponentDisplayName))return false;
            if(!setId.empty()) {
                const auto matches=NativeLibrary::ExactEquipmentSets(doc,SerializeSavedBuild(build));
                if(std::none_of(matches.begin(),matches.end(),[&](const auto& m){return m.id==setId;}))
                    return Composable::Fail(error,"Selected equipment set no longer matches the capture.");
                if(!NativeLibrary::ApplyEquipmentSet(doc,build.id,setId,error))return false;
            } else if(!newSetName.empty()) {
                std::string id="equipment-set:"+build.id;
                for(unsigned n=1;Composable::FindEquipmentSet(doc,id);++n)id="equipment-set:"+build.id+":"+std::to_string(n);
                if(!NativeLibrary::SaveSetFromLoadout(doc,build.id,id,newSetName,error,kinds))return false;
            }
            auto item=std::move(doc.loadouts.back());doc.loadouts.pop_back();
            doc.loadouts.insert(doc.loadouts.begin()+position,std::move(item));
            return true;
        });
        return ok?position:-1;
    }
    bool GW2API::RefreshSavedBuildFromSource(const std::string& id,const SavedBuild& updated) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            auto* l=Composable::FindLoadout(doc,id);json raw;SavedBuild old;
            if(!l||!Composable::Resolve(doc,*l,raw,error)||!DeserializeSavedBuild(raw,old)||
               old.source_url.empty()||old.source_url!=updated.source_url)return false;
            auto merged=updated;merged.id=old.id;merged.name=old.name;merged.notes=old.notes;
            merged.game_mode=old.game_mode;merged.game_modes=old.game_modes;
            merged.created=old.created;merged.source_character=old.source_character;
            if(old.infusion_summary_present){merged.infusion_summary_present=true;merged.infusion_ar_total=old.infusion_ar_total;merged.infusion_stat_totals=old.infusion_stat_totals;}
            return NativeLibrary::PutLoadout(doc,NativeLibrary::PatchDraft(raw,SerializeSavedBuild(old),SerializeSavedBuild(merged)),error,LookupComponentDisplayName);
        });
    }
    bool GW2API::RemoveSavedBuild(const std::string& id) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error){return Composable::RemoveLoadout(doc,id,error);});
    }
    bool GW2API::UpdateSavedBuild(const std::string& id,const std::string& name,const std::string& notes) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string&){
            auto* l=Composable::FindLoadout(doc,id);if(!l)return false;
            l->metadata["name"]=name;l->metadata["notes"]=notes;return true;
        });
    }

    std::string GW2API::EncodeSavedBuildToChatLink(const SavedBuild& b) {
        auto profCode = [](const std::string& p) -> uint8_t {
            if (p == "Guardian")     return 1;
            if (p == "Warrior")      return 2;
            if (p == "Engineer")     return 3;
            if (p == "Ranger")       return 4;
            if (p == "Thief")        return 5;
            if (p == "Elementalist") return 6;
            if (p == "Mesmer")       return 7;
            if (p == "Necromancer")  return 8;
            if (p == "Revenant")     return 9;
            return 0;
        };
        DecodedBuildLink link{};
        link.profession = profCode(b.profession);
        for (int i = 0; i < 3; i++) {
            link.specs[i].spec_id = (uint8_t)b.specializations[i].spec_id;
            const auto si = GetSpecInfo(b.specializations[i].spec_id);
            for (int t = 0; t < 3; t++) {
                int traitId = b.specializations[i].traits[t];
                uint8_t choice = 0;
                if (traitId > 0 && si && si->major_traits.size() >= 9) {
                    for (int r = 0; r < 3; r++)
                        if ((int)si->major_traits[t * 3 + r] == traitId) { choice = (uint8_t)(r + 1); break; }
                } else if (traitId < 0) {
                    choice = (uint8_t)(-traitId); // unresolved placeholder already holds the choice
                }
                link.specs[i].traits[t] = choice;
            }
        }
        auto toPal = [&](uint32_t skill_id) -> uint16_t {
            return skill_id ? GetPaletteIdFromSkill(b.profession, skill_id) : 0;
        };
        link.terrestrial_skills[0] = toPal(b.terrestrial_skills.heal);
        link.terrestrial_skills[1] = toPal(b.terrestrial_skills.utilities[0]);
        link.terrestrial_skills[2] = toPal(b.terrestrial_skills.utilities[1]);
        link.terrestrial_skills[3] = toPal(b.terrestrial_skills.utilities[2]);
        link.terrestrial_skills[4] = toPal(b.terrestrial_skills.elite);
        link.aquatic_skills[0] = toPal(b.aquatic_skills.heal);
        link.aquatic_skills[1] = toPal(b.aquatic_skills.utilities[0]);
        link.aquatic_skills[2] = toPal(b.aquatic_skills.utilities[1]);
        link.aquatic_skills[3] = toPal(b.aquatic_skills.utilities[2]);
        link.aquatic_skills[4] = toPal(b.aquatic_skills.elite);
        if (b.profession == "Ranger") {
            link.pets[0] = (uint8_t)b.pets.terrestrial[0];
            link.pets[1] = (uint8_t)b.pets.terrestrial[1];
            link.pets[2] = (uint8_t)b.pets.aquatic[0];
            link.pets[3] = (uint8_t)b.pets.aquatic[1];
        }
        if (b.profession == "Revenant") {
            for (int i = 0; i < 4; i++) link.legends[i] = b.legend_codes[i];
        }
        for (auto w : b.weapons) link.weapons.push_back((uint16_t)w);
        return ChatLink::EncodeBuild(link);
    }

    std::vector<uint32_t> GW2API::GetProfessionSpecIds(const std::string& profession) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_profession_cache.find(profession);
        return it != s_profession_cache.end() ? it->second->specialization_ids
                                              : std::vector<uint32_t>{};
    }

    std::vector<uint32_t> GW2API::GetProfessionSkillIds(const std::string& profession) {
        std::lock_guard<std::mutex> lock(s_mutex);
        std::vector<uint32_t> out;
        auto it = s_palette_to_skill.find(profession);
        if (it == s_palette_to_skill.end()) return out;
        out.reserve(it->second.size());
        for (const auto& kv : it->second) out.push_back(kv.second);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    std::vector<PetInfo> GW2API::GetPets() {
        auto catalog=PublicCatalog::Get();std::vector<PetInfo> out;
        for(const auto& p:catalog->pets)out.push_back({p.id,p.name,p.icon});
        return out;
    }

    void GW2API::FetchPetsAsync() {
        PublicCatalog::RefreshPets(GetDataDirectory(),false,PublicCatalog::Get()->pets.empty());
    }

    uint32_t GW2API::GetLegendSwapSkill(uint8_t code) {
        auto catalog=PublicCatalog::Get();
        code=PublicCatalog::CanonicalLegendCode(code);
        for(const auto& l:catalog->legends)if(l.code==code)return l.swap;
        return 0;
    }

    void GW2API::FetchLegendsAsync() {
        PublicCatalog::RefreshLegends(GetDataDirectory(),false,PublicCatalog::Get()->legends.empty());
    }



    SavedBuild GW2API::CreateBlankBuild(const std::string& profession, GameMode mode) {
        SavedBuild b;
        b.profession = profession;
        b.game_mode = mode;
        b.game_modes = {mode};
        b.name = profession + " Build";
        b.created = time(nullptr);
        return b;
    }

    bool GW2API::ReplaceSavedBuildDefinition(const std::string& id,const SavedBuild& def) {
        SavedBuild draft=def;draft.id=id;return SaveEditedLoadout(draft);
    }
    bool GW2API::SetSavedBuildGameMode(const std::string& id,GameMode mode) {
        return SetSavedBuildGameModes(id,{mode});
    }
    bool GW2API::SetSavedBuildGameModes(const std::string& id,const std::vector<GameMode>& modes) {
        if(modes.empty())return false;
        return CommitComposableEdit([&](Composable::Document& doc,std::string&){
            auto* l=Composable::FindLoadout(doc,id);if(!l)return false;
            json values=json::array();
            for(auto mode:modes)if(std::find(values.begin(),values.end(),json(GameModeToStr(mode)))==values.end())values.push_back(GameModeToStr(mode));
            l->metadata["game_modes"]=values;l->metadata["game_mode"]=values.front();return true;
        });
    }
    bool GW2API::ReorderSavedBuild(int fromIdx,int toIdx) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string&){
            int n=static_cast<int>(doc.loadouts.size());
            if(fromIdx<0||fromIdx>=n||toIdx<0||toIdx>n)return false;
            auto item=std::move(doc.loadouts[fromIdx]);doc.loadouts.erase(doc.loadouts.begin()+fromIdx);
            doc.loadouts.insert(doc.loadouts.begin()+(fromIdx<toIdx?toIdx-1:toIdx),std::move(item));return true;
        });
    }
    struct OrderedLoadout {std::string id,profession;};
    static bool ReorderGraph(Composable::Document& doc,const std::function<bool(std::vector<OrderedLoadout>&)>& operation) {
        std::vector<OrderedLoadout> order;
        for(const auto& l:doc.loadouts)order.push_back({l.id,Composable::ProfessionOf(doc,l)});
        if(!operation(order))return false;
        std::vector<Composable::Loadout> next;
        for(const auto& l:order)next.push_back(*Composable::FindLoadout(doc,l.id));
        doc.loadouts=std::move(next);return true;
    }
    bool GW2API::ReorderSavedBuildGroup(const std::string& from,const std::string& to,bool after) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string&){return ReorderGraph(doc,[&](auto& order){return BuildGroupOrder::MoveGroup(order,from,to,after);});});
    }
    bool GW2API::ReorderSavedBuildInGroup(const std::string& from,const std::string& to,bool after) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string&){return ReorderGraph(doc,[&](auto& order){return BuildGroupOrder::MoveWithinGroup(order,from,to,after);});});
    }

    // Serialize a single SavedBuild to JSON. Used by SaveBuildLibrary and
    // ExportBuildLibraryToFile so the on-disk format stays in lockstep.
    static json SerializeSavedBuild(const SavedBuild& b) {
        json bj;
        bj["id"] = b.id;
        bj["name"] = b.name;
        if (!b.source_character.empty()) bj["source_character"] = b.source_character;
        if (!b.source_url.empty()) bj["source_url"] = b.source_url;
        bj["chat_link"] = b.chat_link;
        if (!b.ae2_code.empty()) bj["ae2_code"] = b.ae2_code;
        bj["profession"] = b.profession;
        bj["game_mode"] = GameModeToStr(b.game_mode); // Legacy compatibility
        bj["game_modes"] = json::array();
        for (auto mode : (b.game_modes.empty() ? std::vector<GameMode>{b.game_mode} : b.game_modes))
            bj["game_modes"].push_back(GameModeToStr(mode));
        bj["notes"] = b.notes;
        if (!b.import_warning.empty()) bj["import_warning"] = b.import_warning;
        bj["created"] = (int64_t)b.created;

        json specs = json::array();
        for (int i = 0; i < 3; i++) {
            json sj;
            sj["id"] = b.specializations[i].spec_id;
            sj["traits"] = {b.specializations[i].traits[0],
                            b.specializations[i].traits[1],
                            b.specializations[i].traits[2]};
            specs.push_back(sj);
        }
        bj["specializations"] = specs;

        auto skillBarToJson = [](const SkillBar& sb) -> json {
            json sj;
            sj["heal"] = sb.heal;
            sj["utilities"] = {sb.utilities[0], sb.utilities[1], sb.utilities[2]};
            sj["elite"] = sb.elite;
            return sj;
        };
        bj["skills"] = skillBarToJson(b.terrestrial_skills);
        bj["aquatic_skills"] = skillBarToJson(b.aquatic_skills);

        bj["pets"] = {b.pets.terrestrial[0], b.pets.terrestrial[1],
                      b.pets.aquatic[0], b.pets.aquatic[1]};
        bj["legend_codes"] = {b.legend_codes[0], b.legend_codes[1],
                              b.legend_codes[2], b.legend_codes[3]};
        if (!b.weapons.empty()) bj["weapons"] = b.weapons;

        if (!b.gear.empty()) {
            json gearJ = json::object();
            for (const auto& [slot, gs] : b.gear) {
                json gsj;
                if (gs.item_id != 0) gsj["item_id"] = gs.item_id;
                if (gs.infusion_snapshot) gsj["infusion_snapshot"] = true;
                if (!gs.infusion_ids.empty()) gsj["infusion_ids"] = gs.infusion_ids;
                if (!gs.infusion_attributes.empty())
                    gsj["infusion_attributes"] = gs.infusion_attributes;
                gsj["stat_id"] = gs.stat_id;
                gsj["stat_name"] = gs.stat_name;
                if (!gs.rune.empty()) gsj["rune"] = gs.rune;
                if (gs.rune_id != 0) gsj["rune_id"] = gs.rune_id;
                if (!gs.sigil.empty()) gsj["sigil"] = gs.sigil;
                if (gs.sigil_id != 0) gsj["sigil_id"] = gs.sigil_id;
                if (!gs.sigil2.empty()) gsj["sigil2"] = gs.sigil2;
                if (gs.sigil2_id != 0) gsj["sigil2_id"] = gs.sigil2_id;
                if (!gs.infusion.empty()) gsj["infusion"] = gs.infusion;
                if (!gs.weapon_type.empty()) gsj["weapon_type"] = gs.weapon_type;
                gearJ[slot] = gsj;
            }
            bj["gear"] = gearJ;
        }
        if (!b.infusion_totals.empty()) {
            json requirements = json::array();
            for (const auto& inf : b.infusion_totals)
                requirements.push_back({{"item_id", inf.item_id}, {"count", inf.count},
                                        {"attribute", inf.attribute}});
            bj["infusion_totals"] = requirements;
        }
        if (b.infusion_summary_present) {
            bj["infusion_summary_present"] = true;
            bj["infusion_ar_total"] = b.infusion_ar_total;
            bj["infusion_stat_totals"] = b.infusion_stat_totals;
        }
        if (!b.rune_name.empty()) bj["rune_name"] = b.rune_name;
        if (b.rune_id != 0) bj["rune_id"] = b.rune_id;
        if (!b.relic_name.empty()) bj["relic_name"] = b.relic_name;
        if (b.relic_id != 0) bj["relic_id"] = b.relic_id;
        return bj;
    }

    static std::string LookupComponentDisplayName(const std::string& category, uint32_t id) {
        if (category == "spec") {
            if (const auto info = GW2API::GetSpecInfo(id)) return info->name;
            GW2API::FetchSpecDetailsAsync({id});
        } else if (category == "skill") {
            if (const auto info = GW2API::GetSkillInfo(id)) return info->name;
            GW2API::FetchSkillDetailsAsync({id});
        } else if (category == "stat") {
            if (const auto info = GW2API::GetItemStatInfo(id)) return info->name;
        } else {
            if (const auto info = GW2API::GetItemInfo(id)) return info->name;
            GW2API::FetchItemDetailsAsync({id});
        }
        return {};
    }

    // Parse one build entry. Returns false if the entry is missing required fields.
    static bool DeserializeSavedBuild(const json& bj, SavedBuild& b) {
        if (!bj.is_object()) return false;
        b.id = bj.value("id", "");
        b.name = bj.value("name", "");
        b.source_character = bj.value("source_character", "");
        b.source_url = bj.value("source_url", "");
        b.chat_link = bj.value("chat_link", "");
        b.ae2_code = bj.value("ae2_code", "");
        b.profession = bj.value("profession", "");
        b.game_mode = StrToGameMode(bj.value("game_mode", "PvE"));
        if (bj.contains("game_modes") && bj["game_modes"].is_array()) {
            for (const auto& entry : bj["game_modes"]) {
                if (!entry.is_string()) continue;
                auto mode = StrToGameMode(entry.get<std::string>());
                if (std::find(b.game_modes.begin(), b.game_modes.end(), mode) == b.game_modes.end())
                    b.game_modes.push_back(mode);
            }
        }
        if (b.game_modes.empty()) b.game_modes.push_back(b.game_mode);
        b.game_mode = b.game_modes.front();
        b.notes = bj.value("notes", "");
        b.import_warning = bj.value("import_warning", "");
        b.created = bj.value("created", (int64_t)0);

        if (bj.contains("specializations") && bj["specializations"].is_array()) {
            int idx = 0;
            for (const auto& sj : bj["specializations"]) {
                if (idx >= 3) break;
                b.specializations[idx].spec_id = sj.value("id", (uint32_t)0);
                if (sj.contains("traits") && sj["traits"].is_array()) {
                    int ti = 0;
                    for (const auto& t : sj["traits"]) {
                        if (ti >= 3) break;
                        b.specializations[idx].traits[ti] = t.get<int>();
                        ti++;
                    }
                }
                idx++;
            }
        }

        auto parseSkillBarJson = [](const json& sj) -> SkillBar {
            SkillBar sb;
            sb.heal = sj.value("heal", (uint32_t)0);
            if (sj.contains("utilities") && sj["utilities"].is_array()) {
                int i = 0;
                for (const auto& u : sj["utilities"]) {
                    if (i >= 3) break;
                    sb.utilities[i] = u.get<uint32_t>();
                    i++;
                }
            }
            sb.elite = sj.value("elite", (uint32_t)0);
            return sb;
        };

        if (bj.contains("skills"))
            b.terrestrial_skills = parseSkillBarJson(bj["skills"]);
        if (bj.contains("aquatic_skills"))
            b.aquatic_skills = parseSkillBarJson(bj["aquatic_skills"]);

        if (bj.contains("pets") && bj["pets"].is_array() && bj["pets"].size() >= 4) {
            b.pets.terrestrial[0] = bj["pets"][0].get<uint32_t>();
            b.pets.terrestrial[1] = bj["pets"][1].get<uint32_t>();
            b.pets.aquatic[0] = bj["pets"][2].get<uint32_t>();
            b.pets.aquatic[1] = bj["pets"][3].get<uint32_t>();
        }
        if (bj.contains("legend_codes") && bj["legend_codes"].is_array() && bj["legend_codes"].size() >= 4) {
            for (int i = 0; i < 4; i++)
                b.legend_codes[i] = bj["legend_codes"][i].get<uint8_t>();
        }
        if (bj.contains("weapons") && bj["weapons"].is_array()) {
            for (const auto& w : bj["weapons"])
                b.weapons.push_back(w.get<uint32_t>());
        }

        if (bj.contains("gear") && bj["gear"].is_object()) {
            for (auto it = bj["gear"].begin(); it != bj["gear"].end(); ++it) {
                BuildGearSlot gs;
                gs.slot = it.key();
                gs.item_id = it.value().value("item_id", (uint32_t)0);
                gs.infusion_snapshot = it.value().value("infusion_snapshot", false);
                if (it.value().contains("infusion_ids") && it.value()["infusion_ids"].is_array()) {
                    for (const auto& infusion : it.value()["infusion_ids"]) {
                        uint32_t id=0;
                        if(Composable::ReadItemId(infusion,id))gs.infusion_ids.push_back(id);
                        else return false; // Never silently drop a slot or shift its attributes.
                    }
                }
                if (it.value().contains("infusion_attributes") &&
                    it.value()["infusion_attributes"].is_array()) {
                    for (const auto& attr : it.value()["infusion_attributes"])
                        gs.infusion_attributes.push_back(attr.is_string() ? attr.get<std::string>() : "");
                    // Never trust mismatched lengths in hand-edited or older JSON.
                    gs.infusion_attributes.resize(gs.infusion_ids.size());
                }
                gs.stat_id = it.value().value("stat_id", (uint32_t)0);
                gs.stat_name = it.value().value("stat_name", "");
                gs.rune = it.value().value("rune", "");
                gs.rune_id = it.value().value("rune_id", (uint32_t)0);
                gs.sigil = it.value().value("sigil", "");
                gs.sigil_id = it.value().value("sigil_id", (uint32_t)0);
                gs.sigil2 = it.value().value("sigil2", "");
                gs.sigil2_id = it.value().value("sigil2_id", (uint32_t)0);
                gs.infusion = it.value().value("infusion", "");
                gs.weapon_type = it.value().value("weapon_type", "");
                b.gear[gs.slot] = gs;
            }
        }
        if (bj.contains("infusion_totals") && bj["infusion_totals"].is_array()) {
            for (const auto& inf : bj["infusion_totals"]) {
                if (!inf.is_object()) continue;
                BuildInfusionTotal requirement;
                requirement.item_id = inf.value("item_id", (uint32_t)0);
                requirement.count = inf.value("count", 0);
                requirement.attribute = inf.value("attribute", "");
                if (requirement.item_id != 0 && requirement.count > 0 && requirement.count <= 25)
                    b.infusion_totals.push_back(std::move(requirement));
            }
        }
        b.infusion_summary_present = bj.value("infusion_summary_present", false);
        if (b.infusion_summary_present) {
            b.infusion_ar_total = std::max(0, bj.value("infusion_ar_total", 0));
            if (bj.contains("infusion_stat_totals") && bj["infusion_stat_totals"].is_object()) {
                for (auto it = bj["infusion_stat_totals"].begin();
                     it != bj["infusion_stat_totals"].end(); ++it)
                    if (it.value().is_number_integer())
                        b.infusion_stat_totals[it.key()] = std::max(0, it.value().get<int>());
            }
        }
        b.rune_name = bj.value("rune_name", "");
        b.rune_id = bj.value("rune_id", (uint32_t)0);
        b.relic_name = bj.value("relic_name", "");
        b.relic_id = bj.value("relic_id", (uint32_t)0);
        return true;
    }

    bool GW2API::LoadBuildLibrary() {
        std::lock_guard<std::mutex> persist(g_loadoutPersistenceMutex);
        NativeLibrary::Snapshot state;
        std::string error;
        auto& store=NativeLibrary::RuntimeStore();
        if(!store.Open(GetDataDirectory(),error)||!store.Read(state,error)) {
            if(s_api){s_api->Log(LOGL_WARNING,"AlterEgo",error.c_str());s_api->GUI_SendAlert(error.c_str());}
            return false;
        }
        auto loaded=std::move(state.loadouts);

        json legacy;
        if (!Composable::ToLegacy(loaded, legacy, error)) return false;
        std::vector<SavedBuild> parsed;
        try {
            for (const auto& entry : legacy.at("builds")) {
                SavedBuild saved;
                if (!DeserializeSavedBuild(entry, saved))
                    return false; // Do not silently discard incomplete entries.
                parsed.push_back(std::move(saved));
            }
        } catch (...) { return false; }
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_saved_builds = std::move(parsed);
            g_loadoutDocument = std::move(loaded);
            g_loadoutDocumentLoaded = true;
            g_buildLibraryRevision.fetch_add(1);
        }
        return true;
    }

    // Apply native commands atomically; publish only after the durable commit.
    bool GW2API::CommitComposableEdit(
        const std::function<bool(Composable::Document&, std::string&)>& edit) {
        std::lock_guard<std::mutex> persist(g_loadoutPersistenceMutex);
        NativeLibrary::Snapshot snapshot;
        std::string error;
        auto& store=NativeLibrary::RuntimeStore();
        if(!store.Read(snapshot,error))return false;
        auto& before=snapshot.loadouts;
        Composable::Document next=before;
        if (!edit(next, error) || !Composable::Validate(next, error))
            return false;

        // The game chat link is derived from specialization/skill selection.
        // AE2 embeds both skills and gear; invalidate it whenever a component
        // changes, instead of leaving an authoritative-looking stale code.
        std::set<std::string> affected;
        for (auto& loadout : next.loadouts) {
            auto* previous = Composable::FindLoadout(before, loadout.id);
            if (!previous) { affected.insert(loadout.id); continue; }
            bool changed = false, buildChanged = false;
            const auto oldRefs=Composable::EffectiveRefs(before,*previous);
            const auto newRefs=Composable::EffectiveRefs(next,loadout);
            for (const auto& kind : Composable::Kinds()) {
                auto oldRef = oldRefs.find(kind);
                auto newRef = newRefs.find(kind);
                if(oldRef==oldRefs.end()&&newRef==newRefs.end())continue;
                if (oldRef == oldRefs.end() || newRef == newRefs.end()) {
                    changed = true;
                    continue;
                }
                const auto* oldPart = Composable::Find(before, kind, oldRef->second);
                const auto* newPart = Composable::Find(next, kind, newRef->second);
                // Reassignment/copy of an identical component does not
                // invalidate cached codes or require palette re-encoding.
                // Only a semantic payload change does.
                if (!oldPart || !newPart || !NativeLibrary::ExactContent(oldPart->data,newPart->data)) {
                    changed = true;
                    if (kind == "base_build" || kind == "skills") buildChanged = true;
                }
            }
            if(previous->equipment_set_id!=loadout.equipment_set_id||previous->fashion_id!=loadout.fashion_id||oldRefs!=newRefs)affected.insert(loadout.id);
            if (changed) loadout.metadata.erase("ae2_code");
            if (changed || !NativeLibrary::ExactContent(previous->metadata,loadout.metadata)) affected.insert(loadout.id);
            if (buildChanged) {
                json recomposed;
                if (!Composable::Resolve(next, loadout, recomposed, error)) return false;
                SavedBuild saved;
                if (!DeserializeSavedBuild(recomposed, saved)) return false;
                const auto link = GW2API::EncodeSavedBuildToChatLink(saved);
                if (link.empty()) loadout.metadata.erase("chat_link");
                else loadout.metadata["chat_link"] = link;
            }
        }
        std::map<std::string,SavedBuild> cached;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for(const auto& b:s_saved_builds)cached.emplace(b.id,b);
        }
        std::vector<SavedBuild> projected;projected.reserve(next.loadouts.size());
        try {
            for(const auto& l:next.loadouts) {
                auto existing=cached.find(l.id);
                if(!affected.count(l.id)&&existing!=cached.end()) {
                    projected.push_back(std::move(existing->second));continue;
                }
                json entry;SavedBuild record;
                if(!Composable::Resolve(next,l,entry,error)||!DeserializeSavedBuild(entry,record))return false;
                projected.push_back(std::move(record));
            }
        } catch(...) {return false;}
        auto proposed=snapshot;proposed.loadouts=next;
        if (!store.Commit(snapshot,std::move(proposed),error)) {
            if(s_api)s_api->Log(LOGL_WARNING,"AlterEgo",error.c_str());
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(GW2API::s_mutex);
            GW2API::s_saved_builds = std::move(projected);
            g_loadoutDocument = std::move(next);
            g_loadoutDocumentLoaded = true;
            g_buildLibraryRevision.fetch_add(1);
        }
        return true;
    }

    bool GW2API::SaveEditedLoadout(const SavedBuild& draft, uint64_t expectedRevision) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            if(expectedRevision && expectedRevision!=g_buildLibraryRevision.load())return Composable::Fail(error,"Library changed while editing.");
            auto* l=Composable::FindLoadout(doc,draft.id);json raw;SavedBuild old;
            if(!l||!Composable::Resolve(doc,*l,raw,error)||!DeserializeSavedBuild(raw,old))return false;
            return NativeLibrary::PutLoadout(doc,NativeLibrary::PatchDraft(raw,SerializeSavedBuild(old),SerializeSavedBuild(draft)),error,LookupComponentDisplayName);
        });
    }

    bool GW2API::RebaseLoadoutDraft(SavedBuild& draft,const std::string& oldSet,const std::string& oldFashion,
        const std::map<std::string,std::string>& oldRefs,const std::string& newSet,const std::string& newFashion,
        const std::map<std::string,std::string>& newRefs,uint64_t revision,std::string& error){
        json snapshot;Composable::Document doc;
        if(revision!=GetBuildLibraryRevision()||!GetComposableLoadouts(snapshot)||
           revision!=GetBuildLibraryRevision()||!Composable::FromJson(snapshot,doc,error))
            return Composable::Fail(error,"Library changed or could not be loaded.");
        if(!NativeLibrary::SaveLoadoutReferenceDraft(doc,draft.id,oldSet,oldFashion,oldRefs,error))return false;
        auto* l=Composable::FindLoadout(doc,draft.id);
        json before;SavedBuild baseline;
        if(!l||!Composable::Resolve(doc,*l,before,error)||!DeserializeSavedBuild(before,baseline))return false;
        const auto previousRefs=Composable::EffectiveRefs(doc,*l);
        const auto edited=NativeLibrary::PatchDraft(before,SerializeSavedBuild(baseline),SerializeSavedBuild(draft));
        if(!NativeLibrary::SaveLoadoutReferenceDraft(doc,draft.id,newSet,newFashion,newRefs,error))return false;
        l=Composable::FindLoadout(doc,draft.id);
        json replacement;if(!l||!Composable::Resolve(doc,*l,replacement,error))return false;
        const auto afterRefs=Composable::EffectiveRefs(doc,*l);
        const auto oldParts=Composable::Split(before),editedParts=Composable::Split(edited);
        for(const auto& [kind,data]:editedParts){
            const auto old=previousRefs.find(kind),now=afterRefs.find(kind);
            const auto oldId=old==previousRefs.end()?std::string{}:old->second;
            const auto newId=now==afterRefs.end()?std::string{}:now->second;
            if(oldId!=newId&&!NativeLibrary::ExactContent(oldParts.at(kind),data))
                return Composable::Fail(error,"Cannot replace "+kind+" while it has unsaved content edits. Undo those edits or save first.");
        }
        SavedBuild rebased;
        if(!DeserializeSavedBuild(NativeLibrary::PatchDraft(replacement,before,edited),rebased))
            return Composable::Fail(error,"Selected components cannot be projected.");
        draft=std::move(rebased);return true;
    }

    bool GW2API::SaveUnifiedLoadout(const SavedBuild& draft,const std::string& setId,const std::string& fashionId,
        const std::map<std::string,std::string>& refs,const std::set<std::string>& sharedKinds,uint64_t revision){
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            if(revision!=g_buildLibraryRevision.load())return Composable::Fail(error,"Library changed while editing.");
            if(!NativeLibrary::SaveLoadoutReferenceDraft(doc,draft.id,setId,fashionId,refs,error))return false;
            auto* l=Composable::FindLoadout(doc,draft.id);
            json raw;SavedBuild baseline;
            if(!l||!Composable::Resolve(doc,*l,raw,error)||!DeserializeSavedBuild(raw,baseline))return false;
            return NativeLibrary::PutUnifiedLoadout(doc,NativeLibrary::PatchDraft(raw,SerializeSavedBuild(baseline),SerializeSavedBuild(draft)),
                sharedKinds,error,LookupComponentDisplayName);
        });
    }

    bool GW2API::GetComposableLoadouts(nlohmann::json& snapshot) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!g_loadoutDocumentLoaded) return false;
        snapshot = Composable::ToJson(g_loadoutDocument);
        return true;
    }

    bool GW2API::AssignLoadoutComponent(const std::string& loadoutId,
                                        const std::string& kind,
                                        const std::string& componentId) {
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            return Composable::Assign(doc, loadoutId, kind, componentId, error);
        });
    }

    bool GW2API::CopyLoadoutComponent(const std::string& loadoutId,
                                      const std::string& kind,
                                      const std::string& newComponentName) {
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            static std::atomic<uint64_t> counter{0};
            const std::string prefix = kind + ":" + loadoutId + ":copy:";
            const std::string id = prefix + std::to_string(std::time(nullptr)) +
                ":" + std::to_string(counter.fetch_add(1));
            return Composable::CopyForLoadout(doc, loadoutId, kind, id,
                                               newComponentName, error);
        });
    }

    bool GW2API::CopyEditedLoadoutComponent(const std::string& loadoutId,
                                            const std::string& kind,
                                            const std::string& sourceComponentId,
                                            const std::string& name,
                                            const nlohmann::json& data,uint64_t expectedRevision) {
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            if(expectedRevision&&expectedRevision!=g_buildLibraryRevision.load())return Composable::Fail(error,"Library changed while editing.");
            static std::atomic<uint64_t> counter{0};
            std::string id;
            do {
                id = kind + ":" + loadoutId + ":private:" +
                    std::to_string(std::time(nullptr)) + ":" +
                    std::to_string(counter.fetch_add(1));
            } while (Composable::Find(doc, kind, id));
            return Composable::CopyEditedForLoadout(doc, loadoutId, kind,
                                                      id, name, data, error,
                                                      sourceComponentId);
        });
    }

    bool GW2API::ReuseEditedLoadoutComponent(const std::string& loadoutId,
        const std::string& kind,const std::string& sourceId,
        const std::string& candidateId,const nlohmann::json& draft,
        uint64_t expectedRevision) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error) {
            if(expectedRevision!=g_buildLibraryRevision.load())
                return Composable::Fail(error,"Library changed since reuse was selected.");
            return NativeLibrary::ReuseEditedForLoadout(doc,loadoutId,kind,sourceId,
                candidateId,draft,error);
        });
    }

    bool GW2API::EditSharedLoadoutComponent(const std::string& kind,
                                             const std::string& componentId,
                                             const nlohmann::json& data) {
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            return Composable::UpdateShared(doc, kind, componentId, data, error);
        });
    }

    bool GW2API::EditAndRenameLoadoutComponent(const std::string& kind,
                                               const std::string& componentId,
                                               const std::string& name,
                                               const nlohmann::json& data,uint64_t expectedRevision) {
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            if(expectedRevision&&expectedRevision!=g_buildLibraryRevision.load())return Composable::Fail(error,"Library changed while editing.");
            return Composable::UpdateSharedNamed(doc, kind, componentId, name, data, error);
        });
    }

    bool GW2API::SetLoadoutFashion(const std::string& loadoutId,
                                   const std::string& fashionId) {
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            return Composable::LinkFashion(doc, loadoutId, fashionId, error);
        });
    }


    bool GW2API::ConsolidateLoadoutComponent(const std::string& kind,const std::string& source,const std::string& survivor,uint64_t expectedRevision) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            if(expectedRevision!=g_buildLibraryRevision.load())return Composable::Fail(error,"Library changed since confirmation.");
            return NativeLibrary::ConsolidateExact(doc,kind,source,survivor,error);
        });
    }

    bool GW2API::CreateLoadoutComponent(const std::string& kind,
                                         const std::string& name,
                                         const nlohmann::json& data,
                                         std::string* savedId, bool reuseExact, const std::string& profession) {
        if (Composable::ComponentNameKey(name).empty() || !data.is_object() || !Composable::IsKind(kind))
            return false;
        std::string resultId;
        const bool ok = CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            if (reuseExact)
                if (const auto matches=NativeLibrary::EditIndex(doc).Matches(kind,data,profession);!matches.empty()) {
                    resultId = matches.front().id;
                    return true;
                }
            static std::atomic<uint64_t> counter{0};
            std::string id;
            do {
                id = kind + ":created:" + std::to_string(std::time(nullptr)) +
                     ":" + std::to_string(counter.fetch_add(1));
            } while (Composable::Find(doc, kind, id));
            doc.components[kind].push_back({id, Composable::UniqueComponentName(doc, kind, name), data});
            resultId = id;
            return Composable::Validate(doc, error);
        });
        if (ok && savedId) *savedId = resultId;
        return ok;
    }

    bool GW2API::DuplicateLoadoutComponent(const std::string& kind,
                                            const std::string& componentId,
                                            const std::string& name) {
        if (!Composable::IsKind(kind) || name.empty()) return false;
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            const auto* original = Composable::Find(doc, kind, componentId);
            if (!original) return Composable::Fail(error, "Component not found.");
            Composable::Component copy = *original;
            static std::atomic<uint64_t> counter{0};
            do {
                copy.id = kind + ":copy:" + std::to_string(std::time(nullptr)) +
                          ":" + std::to_string(counter.fetch_add(1));
            } while (Composable::Find(doc, kind, copy.id));
            copy.name = Composable::UniqueComponentName(doc, kind, name);
            doc.components[kind].push_back(std::move(copy));
            return true;
        });
    }

    bool GW2API::RenameLoadoutComponent(const std::string& kind,
                                         const std::string& componentId,
                                         const std::string& name) {
        if (!Composable::IsKind(kind) || name.empty()) return false;
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            auto* component = Composable::Find(doc, kind, componentId);
            if (!component) return Composable::Fail(error, "Component not found.");
            if (Composable::ComponentNameKey(name).empty() ||
                (Composable::ComponentNameKey(name) != Composable::ComponentNameKey(component->name) &&
                 !Composable::ComponentNameAvailable(doc, kind, name, componentId)))
                return Composable::Fail(error, "A component with this name already exists.");
            component->name = name;
            return true;
        });
    }

    bool GW2API::DeleteUnusedLoadoutComponent(const std::string& kind,
                                               const std::string& componentId) {
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            return Composable::RemoveUnreferenced(doc, kind, componentId, error);
        });
    }


    std::string GW2API::SuggestLibraryComponentName(const std::string& kind,
                                                      const nlohmann::json& data) {
        return Composable::SuggestComponentName(kind, data, LookupComponentDisplayName);
    }

    std::string GW2API::SuggestCapturedComponentName(const SavedBuild& capture,
                                                      const std::string& kind) {
        const auto fields = Composable::Split(SerializeSavedBuild(capture));
        auto selected = fields.find(kind);
        if (selected == fields.end()) return "Component";
        const auto proposed = SuggestLibraryComponentName(kind, selected->second);
        nlohmann::json snapshot;
        Composable::Document doc;
        std::string error;
        if (!GetComposableLoadouts(snapshot) || !Composable::FromJson(snapshot, doc, error))
            return proposed;
        if (const auto matches=NativeLibrary::EditIndex(doc).Matches(kind,selected->second,capture.profession);!matches.empty())
            return matches.front().name;
        return Composable::UniqueComponentName(doc, kind, proposed);
    }

    bool GW2API::SaveCapturedLoadoutComponent(const SavedBuild& capture,
                                               const std::string& kind,
                                               const std::string& name,
                                               std::string* savedId, bool reuseExact) {
        if (!Composable::IsKind(kind) || name.empty()) return false;
        const auto fields = Composable::Split(SerializeSavedBuild(capture));
        auto selected = fields.find(kind);
        if (selected == fields.end()) return false;
        return CreateLoadoutComponent(kind, name, selected->second, savedId, reuseExact, capture.profession);
    }

    bool GW2API::SaveCapturedEquipmentSet(const SavedBuild& capture,const std::string& name,std::string* savedId,const std::set<std::string>& kinds) {
        if(capture.gear.empty()||Composable::ComponentNameKey(name).empty())return false;
        std::string id;
        const bool ok=CommitComposableEdit([&](Composable::Document& doc,std::string& error) {
            const std::string prefix="equipment-set:"+std::to_string(std::time(nullptr))+":";
            unsigned n=0;
            do{id=prefix+std::to_string(n++);}while(Composable::FindEquipmentSet(doc,id));
            return NativeLibrary::PutEquipmentSet(doc,id,name,SerializeSavedBuild(capture),error,LookupComponentDisplayName,kinds);
        });
        if(ok&&savedId)*savedId=id;
        return ok;
    }
    bool GW2API::ApplyEquipmentSet(const std::string& loadoutId,const std::string& setId) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error) {
            return NativeLibrary::ApplyEquipmentSet(doc,loadoutId,setId,error);
        });
    }
    bool GW2API::DetachEquipmentSet(const std::string& loadoutId) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            return NativeLibrary::DetachEquipmentSet(doc,loadoutId,error);
        });
    }
    bool GW2API::SaveLoadoutAsEquipmentSet(const std::string& loadoutId,const std::string& name,std::string* savedId,const std::set<std::string>& kinds) {
        std::string id;
        const bool ok=CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            const std::string prefix="equipment-set:"+loadoutId+":";
            unsigned n=0;do{id=prefix+std::to_string(n++);}while(Composable::FindEquipmentSet(doc,id));
            return NativeLibrary::SaveSetFromLoadout(doc,loadoutId,id,name,error,kinds);
        });
        if(ok&&savedId)*savedId=id;
        return ok;
    }
    bool GW2API::DuplicateEquipmentSet(const std::string& setId,const std::string& name,std::string* savedId) {
        std::string id;
        const bool ok=CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            const auto* source=Composable::FindEquipmentSet(doc,setId);
            if(!source||Composable::ComponentNameKey(name).empty())return Composable::Fail(error,"Invalid equipment set.");
            auto copy=*source;
            unsigned n=0;do{id=setId+":copy:"+std::to_string(n++);}while(Composable::FindEquipmentSet(doc,id));
            copy.id=id;copy.name=name;doc.equipment_sets.push_back(std::move(copy));return true;
        });
        if(ok&&savedId)*savedId=id;
        return ok;
    }
    bool GW2API::SaveEquipmentSetDraft(const std::string& id,const std::string& name,const std::map<std::string,std::string>& refs,uint64_t revision) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            if(revision!=g_buildLibraryRevision.load())return Composable::Fail(error,"Library changed while editing.");
            return NativeLibrary::SaveEquipmentSetDraft(doc,id,name,refs,error);
        });
    }
    bool GW2API::SaveLoadoutReferenceDraft(const std::string& id,const std::string& setId,const std::string& fashionId,const std::map<std::string,std::string>& refs,uint64_t revision) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            if(revision!=g_buildLibraryRevision.load())return Composable::Fail(error,"Library changed while editing.");
            return NativeLibrary::SaveLoadoutReferenceDraft(doc,id,setId,fashionId,refs,error);
        });
    }
    bool GW2API::AssignEquipmentSetComponent(const std::string& setId,const std::string& kind,const std::string& componentId,uint64_t revision) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string& error){
            if(revision!=g_buildLibraryRevision.load())return Composable::Fail(error,"Library changed; reopen set editor.");
            return NativeLibrary::AssignSetComponent(doc,setId,kind,componentId,error);
        });
    }
    std::vector<std::pair<std::string,std::string>> GW2API::FindCapturedEquipmentSets(const SavedBuild& capture) {
        json raw;Composable::Document doc;std::string error;
        std::vector<std::pair<std::string,std::string>> result;
        if(GetComposableLoadouts(raw)&&Composable::FromJson(raw,doc,error))
            for(const auto& match:NativeLibrary::ExactEquipmentSets(doc,SerializeSavedBuild(capture)))
                result.emplace_back(match.id,match.name);
        return result;
    }
    bool GW2API::RenameEquipmentSet(const std::string& setId,const std::string& name) {
        if(Composable::ComponentNameKey(name).empty())return false;
        return CommitComposableEdit([&](Composable::Document& doc,std::string&) {
            auto* set=Composable::FindEquipmentSet(doc,setId);
            if(!set)return false;
            set->name=name;return true;
        });
    }
    bool GW2API::RemoveEquipmentSet(const std::string& setId) {
        return CommitComposableEdit([&](Composable::Document& doc,std::string&) {
            auto it=std::find_if(doc.equipment_sets.begin(),doc.equipment_sets.end(),
                [&](const auto& set){return set.id==setId;});
            if(it==doc.equipment_sets.end()||!Composable::EquipmentSetUsers(doc,setId).empty())return false;
            doc.equipment_sets.erase(it);return true;
        });
    }

    bool GW2API::ReplaceCapturedLoadoutComponent(const SavedBuild& capture,
                                                  const std::string& kind,
                                                  const std::string& componentId) {
        if (!Composable::IsKind(kind) || componentId.empty()) return false;
        const auto parts = Composable::Split(SerializeSavedBuild(capture));
        const auto selected = parts.find(kind);
        if (selected == parts.end()) return false;
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            const auto* existing = Composable::Find(doc, kind, componentId);
            if (!existing) return Composable::Fail(error, "Component not found.");
            if (!Composable::CaptureProfessionCompatible(doc, kind, componentId,
                    capture.profession, error)) return false;
            return Composable::UpdateShared(doc, kind, componentId,
                Composable::ReplaceCapturedFieldsPreservingExtensions(
                    existing->data, selected->second), error);
        });
    }

    bool GW2API::ImportPortableLoadout(const nlohmann::json& packet) {
        return CommitComposableEdit([&](Composable::Document& doc, std::string& error) {
            static std::atomic<uint64_t> counter{0};
            std::string id;
            do {
                id = "portable:" + std::to_string(std::time(nullptr)) + ":" +
                    std::to_string(counter.fetch_add(1));
            } while (Composable::FindLoadout(doc, id));
            if(!Composable::ImportPortableLoadout(doc,packet,id,error))return false;
            auto* imported=Composable::FindLoadout(doc,id);
            if(imported)imported->fashion_id.clear(); // Portable single-loadout links do not carry Fashion data.
            return true;
        });
    }

    bool GW2API::ExportBuildLibraryToFile(const std::string& path) {
        std::string error;return Backup::Export(GetDataDirectory(),Backup::Builds,path,false,error);
    }
    bool GW2API::ImportBuildLibraryFromFile(const std::string& path,bool replaceAll,
                                            int& addedOut,int& skippedOut,std::string& errOut) {
        addedOut=skippedOut=0;
        Backup::Package p;Backup::Plan plan;std::string recovery;
        if(!Backup::ReadPackage(path,p,errOut)||!Backup::Prepare(GetDataDirectory(),p,Backup::Builds,replaceAll,plan,errOut)||
           !Backup::Apply(GetDataDirectory(),plan,recovery,errOut))return false;
        addedOut=static_cast<int>(plan.buildAdded);skippedOut=static_cast<int>(plan.buildSkipped);
        return LoadBuildLibrary();
    }

    // =========================================================================
    // Reference data cache persistence
    //
    // Items, skins, specs, traits, skills, itemstats and dye colors are public
    // GW2 API data that changes rarely. We persist these caches to disk so
    // equipment, builds and skinventory render instantly on startup without
    // re-hitting the API, even if the API is rate-limited or unavailable.
    // Saves happen at the end of each fetch batch; loads happen at startup.
    // =========================================================================

    bool GW2API::SaveItemCache() {
        EnsureDataDirectory();
        int64_t captured=0;
        json arr = json::array();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            captured=Cache::ReferenceSnapshotTime();
            for (const auto& [id, row] : s_item_cache) {
                const auto& info=*row;
                json j;
                j["id"] = info.id;
                j["name"] = info.name;
                j["icon"] = info.icon_url;
                j["rarity"] = info.rarity;
                j["type"] = info.type;
                if (!info.description.empty()) j["description"] = info.description;
                if (!info.chat_link.empty()) j["chat_link"] = info.chat_link;
                if (info.default_skin) j["default_skin"] = info.default_skin;
                if (!info.details.is_null()) j["details"] = info.details;
                arr.push_back(std::move(j));
            }
        }
        return Cache::WriteItems(GetDataDirectory(),Cache::ItemDataset::Details,arr,captured);
    }

    bool GW2API::SaveSkinCache() {
        EnsureDataDirectory();
        json arr;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            arr = json::array();
            for (const auto& [id, row] : s_skin_cache) {
                const auto& info=*row;
                json j;
                j["id"] = info.id;
                j["name"] = info.name;
                j["icon"] = info.icon_url;
                j["type"] = info.type;
                j["rarity"] = info.rarity;
                if (!info.weight_class.empty()) j["weight_class"] = info.weight_class;
                if (info.dye_slot_count >= 0) j["dye_slot_count"] = info.dye_slot_count;
                if (!info.dye_slot_materials.empty()) j["dye_slot_materials"] = info.dye_slot_materials;
                arr.push_back(std::move(j));
            }
        }
        return Cache::WriteSkins(GetDataDirectory(),Cache::SkinProjection::Fashion,arr);
    }

    bool GW2API::SaveSpecCache() {
        EnsureDataDirectory();
        int64_t capturedAt = 0;
        json arr;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            capturedAt = Cache::ReferenceSnapshotTime();
            arr = json::array();
            for (const auto& [id, row] : s_spec_cache) {
                const auto& info=*row;
                json j;
                j["id"] = info.id;
                j["name"] = info.name;
                j["icon"] = info.icon_url;
                j["background"] = info.background_url;
                j["profession"] = info.profession;
                j["profession_icon"] = info.profession_icon_url;
                j["profession_icon_big"] = info.profession_icon_big_url;
                j["elite"] = info.elite;
                j["minor_traits"] = info.minor_traits;
                j["major_traits"] = info.major_traits;
                arr.push_back(std::move(j));
            }
        }
        return Cache::WriteReference(GetDataDirectory(),Cache::ReferenceDataset::Specializations,arr,capturedAt);
    }

    bool GW2API::SaveTraitCache() {
        EnsureDataDirectory();
        int64_t capturedAt = 0;
        json arr;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            capturedAt = Cache::ReferenceSnapshotTime();
            arr = json::array();
            for (const auto& [id, row] : s_trait_cache) {
                const auto& info=*row;
                json j;
                j["id"] = info.id;
                j["name"] = info.name;
                j["icon"] = info.icon_url;
                if (!info.description.empty()) j["description"] = info.description;
                j["tier"] = info.tier;
                j["order"] = info.order;
                j["slot"] = info.slot;
                if (!info.facts.is_null()) j["facts"] = info.facts;
                arr.push_back(std::move(j));
            }
        }
        return Cache::WriteReference(GetDataDirectory(),Cache::ReferenceDataset::Traits,arr,capturedAt);
    }

    bool GW2API::SaveSkillCache() {
        EnsureDataDirectory();
        int64_t capturedAt = 0;
        json arr;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            capturedAt = Cache::ReferenceSnapshotTime();
            arr = json::array();
            for (const auto& [id, row] : s_skill_cache) {
                const auto& info=*row;
                json j;
                j["id"] = info.id;
                j["name"] = info.name;
                j["icon"] = info.icon_url;
                if (!info.description.empty()) j["description"] = info.description;
                j["type"] = info.type;
                j["specialization"] = info.specialization;
                j["professions"] = info.professions;
                if (!info.facts.is_null()) j["facts"] = info.facts;
                arr.push_back(std::move(j));
            }
        }
        return Cache::WriteReference(GetDataDirectory(),Cache::ReferenceDataset::Skills,arr,capturedAt);
    }

    bool GW2API::SaveItemStatCache() {
        EnsureDataDirectory();
        int64_t capturedAt = 0;
        json arr;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            capturedAt = Cache::ReferenceSnapshotTime();
            arr = json::array();
            for (const auto& [id, row] : s_itemstat_cache) {
                const auto& info=*row;
                json j;
                j["id"] = info.id;
                j["name"] = info.name;
                j["attributes"] = info.attributes;
                arr.push_back(std::move(j));
            }
        }
        return Cache::WriteReference(GetDataDirectory(),Cache::ReferenceDataset::ItemStats,arr,capturedAt);
    }

    bool GW2API::SaveDyeColorsCache() {
        EnsureDataDirectory();
        int64_t capturedAt = 0;
        json arr;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            capturedAt = Cache::ReferenceSnapshotTime();
            arr = json::array();
            for (const auto& [id, row] : s_dye_cache) {
                const auto& dc=*row;
                json j;
                j["id"] = dc.id;
                j["name"] = dc.name;
                j["base"] = { dc.r, dc.g, dc.b };
                j["cloth"] = { dc.cloth_r, dc.cloth_g, dc.cloth_b };
                j["leather"] = { dc.leather_r, dc.leather_g, dc.leather_b };
                j["metal"] = { dc.metal_r, dc.metal_g, dc.metal_b };
                arr.push_back(std::move(j));
            }
        }
        return Cache::WriteReference(GetDataDirectory(),Cache::ReferenceDataset::Dyes,arr,capturedAt);
    }

    bool GW2API::LoadReferenceCaches() {
        auto readReference = [](Cache::ReferenceDataset dataset, json& out) {
            auto snapshot=Cache::ReadReference(GetDataDirectory(),dataset);
            if(!snapshot)return false;
            out=std::move(*snapshot);return out.is_array();
        };
        json j;
        if (auto items=Cache::ReadItems(GetDataDirectory(),Cache::ItemDataset::Details);items&&items->is_array()) {
            j=std::move(*items);
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& it : j) {
                if (!it.contains("id")) continue;
                ItemInfo info;
                info.id = it["id"].get<uint32_t>();
                info.name = it.value("name", "");
                info.icon_url = it.value("icon", "");
                info.rarity = it.value("rarity", "");
                info.type = it.value("type", "");
                info.description = it.value("description", "");
                info.chat_link = it.value("chat_link", "");
                info.default_skin = it.value("default_skin", 0u);
                if (it.contains("details")) info.details = it["details"];
                if(!info.name.empty())s_item_name_id_cache.emplace(info.name,info.id);
                s_item_cache[info.id]=std::make_shared<const ItemInfo>(std::move(info));
            }
        }
        if ([&]() {
            try {
                auto snapshot=Cache::ReadSkins(GetDataDirectory(),Cache::SkinProjection::Fashion,
                    std::filesystem::u8path(GetDataDirectory())/"skin_cache.json");
                if(!snapshot)return false;
                j=std::move(*snapshot);return j.is_array();
            } catch(...) { return false; }
        }()) {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& it : j) {
                if (!it.contains("id")) continue;
                SkinInfo info;
                info.id = it["id"].get<uint32_t>();
                info.name = it.value("name", "");
                info.icon_url = it.value("icon", "");
                info.type = it.value("type", "");
                info.rarity = it.value("rarity", "");
                info.weight_class = it.value("weight_class", "");
                info.dye_slot_count = it.value("dye_slot_count", -1);
                if (it.contains("dye_slot_materials") && it["dye_slot_materials"].is_array()) {
                    for (const auto& m : it["dye_slot_materials"])
                        info.dye_slot_materials.push_back(m.get<std::string>());
                }
                s_skin_cache[info.id]=std::make_shared<const SkinInfo>(std::move(info));
            }
        }
        if (readReference(Cache::ReferenceDataset::Specializations, j)) {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& it : j) {
                if (!it.contains("id")) continue;
                SpecializationInfo info;
                info.id = it["id"].get<uint32_t>();
                info.name = it.value("name", "");
                info.icon_url = it.value("icon", "");
                info.background_url = it.value("background", "");
                info.profession = it.value("profession", "");
                info.profession_icon_url = it.value("profession_icon", "");
                info.profession_icon_big_url = it.value("profession_icon_big", "");
                info.elite = it.value("elite", false);
                if (it.contains("minor_traits"))
                    for (const auto& t : it["minor_traits"]) info.minor_traits.push_back(t.get<uint32_t>());
                if (it.contains("major_traits"))
                    for (const auto& t : it["major_traits"]) info.major_traits.push_back(t.get<uint32_t>());
                s_spec_cache[info.id]=std::make_shared<const SpecializationInfo>(std::move(info));
            }
        }
        if (readReference(Cache::ReferenceDataset::Traits, j)) {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& it : j) {
                if (!it.contains("id")) continue;
                TraitInfo info;
                info.id = it["id"].get<uint32_t>();
                info.name = it.value("name", "");
                info.icon_url = it.value("icon", "");
                info.description = it.value("description", "");
                info.tier = it.value("tier", 0);
                info.order = it.value("order", 0);
                info.slot = it.value("slot", "");
                if (it.contains("facts")) info.facts = it["facts"];
                s_trait_cache[info.id]=std::make_shared<const TraitInfo>(std::move(info));
            }
        }
        if (readReference(Cache::ReferenceDataset::Skills, j)) {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& it : j) {
                if (!it.contains("id")) continue;
                // Legacy cache (pre-professions/specialization) is skipped so the skill
                // re-fetches on demand with the new fields — needed for racial filtering
                // and elite-spec gating.
                if (!it.contains("professions")) continue;
                SkillInfo info;
                info.id = it["id"].get<uint32_t>();
                info.name = it.value("name", "");
                info.icon_url = it.value("icon", "");
                info.description = it.value("description", "");
                info.type = it.value("type", "");
                info.specialization = it.value("specialization", 0u);
                if (it.contains("professions") && it["professions"].is_array())
                    for (const auto& p : it["professions"])
                        if (p.is_string()) info.professions.push_back(p.get<std::string>());
                if (it.contains("facts")) info.facts = it["facts"];
                s_skill_cache[info.id]=std::make_shared<const SkillInfo>(std::move(info));
            }
        }
        if (readReference(Cache::ReferenceDataset::ItemStats, j)) {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& it : j) {
                if (!it.contains("id")) continue;
                ItemStatInfo info;
                info.id = it["id"].get<uint32_t>();
                info.name = it.value("name", "");
                if (it.contains("attributes") && it["attributes"].is_array())
                    for (const auto& a : it["attributes"]) info.attributes.push_back(a.get<std::string>());
                s_itemstat_cache[info.id]=std::make_shared<const ItemStatInfo>(std::move(info));
            }
        }
        if (readReference(Cache::ReferenceDataset::Dyes, j)) {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& it : j) {
                if (!it.contains("id")) continue;
                DyeColor dc;
                dc.id = it["id"].get<int>();
                dc.name = it.value("name", "");
                auto readRGB = [&](const char* key, int& r, int& g, int& b) {
                    if (it.contains(key) && it[key].is_array() && it[key].size() >= 3) {
                        r = it[key][0].get<int>(); g = it[key][1].get<int>(); b = it[key][2].get<int>();
                    }
                };
                readRGB("base", dc.r, dc.g, dc.b);
                readRGB("cloth", dc.cloth_r, dc.cloth_g, dc.cloth_b);
                readRGB("leather", dc.leather_r, dc.leather_g, dc.leather_b);
                readRGB("metal", dc.metal_r, dc.metal_g, dc.metal_b);
                s_dye_cache[dc.id]=std::make_shared<const DyeColor>(std::move(dc));
            }
        }
        return true;
    }

    // =========================================================================
    // Palette ID Mapping
    // =========================================================================

    static const std::unordered_map<std::string, uint8_t> PROFESSION_CODES = {
        {"Guardian", 1}, {"Warrior", 2}, {"Engineer", 3}, {"Ranger", 4},
        {"Thief", 5}, {"Elementalist", 6}, {"Mesmer", 7}, {"Necromancer", 8},
        {"Revenant", 9}
    };

    static std::string ProfessionFromCode(uint8_t code) {
        for (const auto& [name, c] : PROFESSION_CODES) {
            if (c == code) return name;
        }
        return "";
    }

    bool GW2API::HasPaletteData(const std::string& profession) {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_palette_to_skill.find(profession) != s_palette_to_skill.end();
    }

    uint32_t GW2API::GetSkillIdFromPalette(const std::string& profession, uint16_t palette_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto pit = s_palette_to_skill.find(profession);
        if (pit == s_palette_to_skill.end()) return 0;
        auto it = pit->second.find(palette_id);
        return (it != pit->second.end()) ? it->second : 0;
    }

    uint16_t GW2API::GetPaletteIdFromSkill(const std::string& profession, uint32_t skill_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto pit = s_skill_to_palette.find(profession);
        if (pit == s_skill_to_palette.end()) return 0;
        auto it = pit->second.find(skill_id);
        return (it != pit->second.end()) ? it->second : 0;
    }

    void GW2API::FetchProfessionPaletteAsync(const std::string& profession) {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            // Already have data or already fetching
            if (s_palette_to_skill.find(profession) != s_palette_to_skill.end()) return;
            if (s_palette_fetching.count(profession)) return;
            s_palette_fetching.insert(profession);
        }

        auto prof = profession;
        AlterEgo::Background::Workers().Launch([prof]() {
            // Fetch with v=latest to get skills_by_palette
            std::string url = "https://api.guildwars2.com/v2/professions/" + prof + "?v=latest";
            std::string response = HttpGet(url);
            if (response.empty()) {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_palette_fetching.erase(prof);
                return;
            }

            try {
                json j = json::parse(response);
                if (j.contains("skills_by_palette") && j["skills_by_palette"].is_array()) {
                    std::unordered_map<uint16_t, uint32_t> p2s;
                    std::unordered_map<uint32_t, uint16_t> s2p;
                    for (const auto& pair : j["skills_by_palette"]) {
                        if (!pair.is_array() || pair.size() < 2) continue;
                        uint16_t palette_id = pair[0].get<uint16_t>();
                        uint32_t skill_id = pair[1].get<uint32_t>();
                        p2s[palette_id] = skill_id;
                        s2p[skill_id] = palette_id;
                    }

                    // Parse weapon data
                    std::map<std::string, ProfessionWeaponData> weapons;
                    if (j.contains("weapons") && j["weapons"].is_object()) {
                        for (auto it = j["weapons"].begin(); it != j["weapons"].end(); ++it) {
                            ProfessionWeaponData wd;
                            const auto& wj = it.value();
                            if (wj.contains("flags") && wj["flags"].is_array()) {
                                for (const auto& f : wj["flags"]) {
                                    std::string flag = f.get<std::string>();
                                    if (flag == "Mainhand") wd.mainhand = true;
                                    else if (flag == "Offhand") wd.offhand = true;
                                    else if (flag == "TwoHand") wd.two_handed = true;
                                }
                            }
                            wd.specialization = wj.value("specialization", (uint32_t)0);
                            if (wj.contains("skills") && wj["skills"].is_array()) {
                                for (const auto& sk : wj["skills"]) {
                                    std::string slot = sk.value("slot", "");
                                    uint32_t sid = sk.value("id", (uint32_t)0);
                                    std::string offhand = sk.value("offhand", "");
                                    std::string attunement = sk.value("attunement", "");

                                    int slotIdx = -1;
                                    if (slot == "Weapon_1") slotIdx = 0;
                                    else if (slot == "Weapon_2") slotIdx = 1;
                                    else if (slot == "Weapon_3") slotIdx = 2;
                                    else if (slot == "Weapon_4") slotIdx = 3;
                                    else if (slot == "Weapon_5") slotIdx = 4;
                                    if (slotIdx < 0) continue;

                                    if (!attunement.empty()) {
                                        // Elementalist: store per-attunement
                                        wd.attunement_skills[attunement][slotIdx] = sid;
                                    } else if (!offhand.empty() && slotIdx == 2) {
                                        // Thief dual wield: Weapon_3 varies by offhand
                                        wd.dual_wield[offhand] = sid;
                                    } else {
                                        // Default (no qualifier)
                                        wd.skills[slotIdx] = sid;
                                    }
                                }
                            }
                            weapons[it.key()] = wd;
                        }
                    }

                    std::lock_guard<std::mutex> lock(s_mutex);
                    s_palette_to_skill[prof] = std::move(p2s);
                    s_skill_to_palette[prof] = std::move(s2p);
                    if (!weapons.empty()) s_profession_weapons[prof]=std::make_shared<const std::map<std::string, ProfessionWeaponData>>(std::move(weapons));
                    s_palette_fetching.erase(prof);

                    if (s_api) {
                        std::string msg = "Loaded palette mapping for " + prof +
                            " (" + std::to_string(s_palette_to_skill[prof].size()) + " skills)";
                        s_api->Log(LOGL_INFO, "AlterEgo", msg.c_str());
                    }
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_palette_fetching.erase(prof);
            }
        }, [prof] { std::lock_guard<std::mutex> lock(s_mutex); s_palette_fetching.erase(prof); });
    }

} // namespace AlterEgo
