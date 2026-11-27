#include "HttpClient.h"
#include "ExternalResourceSafety.h"
#include "BackgroundWorkers.h"
#include <ctime>
#include "IconManager.h"
#include "StoragePaths.h"
#include "GW2API.h"

#include <windows.h>
#include <wincrypt.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace AlterEgo {

AddonAPI_t* IconManager::s_API = nullptr;
std::unordered_map<std::string, Texture_t*> IconManager::s_IconCache;
std::unordered_set<std::string> IconManager::s_DiskCheckedMissing;
std::unordered_map<std::string, bool> IconManager::s_LoadingIcons;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> IconManager::s_FailedIcons;
std::mutex IconManager::s_Mutex;
std::unordered_map<std::string,int64_t> IconManager::s_NetworkFailures;
std::unordered_map<uint32_t,std::string> IconManager::s_LatestKeys;
std::unordered_map<uint32_t,std::pair<std::string,std::string>> IconManager::s_KeyMemo;
std::vector<IconManager::QueuedRequest> IconManager::s_RequestQueue;
std::chrono::steady_clock::time_point IconManager::s_LastRequestTime = std::chrono::steady_clock::now();
std::string IconManager::s_IconsDir;
std::thread IconManager::s_DownloadThread;
std::condition_variable IconManager::s_QueueCV;
std::atomic<bool> IconManager::s_StopWorker{true};
std::vector<std::string> IconManager::s_ReadyQueue;


static HMODULE GetEmbeddedResourceModule() {
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&GetEmbeddedResourceModule, &module);
    return module;
}

// Actual linked resources, not an assumed range of IDs, determine whether
// a network fallback is forbidden. This is independent of texture readiness.
static HRSRC EmbeddedResource(uint32_t id) {
    const auto module=GetEmbeddedResourceModule();
    if(!module)return nullptr;
    const std::string name="AE_"+std::to_string(id);
    return FindResourceA(module,name.c_str(),RT_RCDATA);
}

// Fixed UI graphics are compiled into RCDATA by generate_embedded_icons.py.
// Reading them never needs profession metadata or an HTTP request.
static Texture_t* LoadEmbeddedIcon(AddonAPI_t* api, uint32_t id,
                                   const std::string& identifier) {
    HMODULE module = GetEmbeddedResourceModule();
    if (!module || !api || !api->Textures_GetOrCreateFromMemory) return nullptr;
    HRSRC resource = EmbeddedResource(id);
    if (!resource) return nullptr;
    HGLOBAL handle = LoadResource(module, resource);
    if (!handle) return nullptr;
    const DWORD len = SizeofResource(module, resource);
    const void* bytes = LockResource(handle);
    if (!bytes || !len) return nullptr;
    try {
        return api->Textures_GetOrCreateFromMemory(
            identifier.c_str(), const_cast<void*>(bytes), static_cast<uint64_t>(len));
    } catch (...) {
        return nullptr;
    }
}

void IconManager::Initialize(AddonAPI_t* api) {
    s_API = api;
    try {
        // Resolve the directory before either render or download can use it.
        GetIconsDir();
        LoadArtworkState();
        s_StopWorker = false;
        s_DownloadThread = std::thread(DownloadWorker);
    } catch (...) {
        RequestStop();
        if(api)api->Log(LOGL_WARNING,"AlterEgo","Could not start icon download worker.");
    }
}

void IconManager::RequestStop() {
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_StopWorker = true;
    }
    s_QueueCV.notify_all();
}

void IconManager::Shutdown() {
    RequestStop();
    if (s_DownloadThread.joinable()) {
        s_DownloadThread.join();
    }

    std::lock_guard<std::mutex> lock(s_Mutex);
    s_IconCache.clear();
    s_DiskCheckedMissing.clear();
    s_LoadingIcons.clear();
    s_ReadyQueue.clear();
    s_RequestQueue.clear();
    s_FailedIcons.clear();
    s_NetworkFailures.clear();
    s_LatestKeys.clear();
    s_KeyMemo.clear();
    s_API=nullptr;
}

std::string IconManager::GetIconsDir() {
    if (!s_IconsDir.empty()) return s_IconsDir;

    // Always resolve beside this DLL, not beside the GW2 process executable.
    // This is also where character data and other AlterEgo caches are stored.
    std::string addonDir = GW2API::GetDataDirectory();
    CreateDirectoryA(addonDir.c_str(), NULL);
    s_IconsDir = AlterEgo::Storage::ResolveCache(addonDir, "icons", "textures");
    CreateDirectoryA(s_IconsDir.c_str(), NULL);
    return s_IconsDir;
}

std::string IconManager::GetIconFilePath(const std::string& key) {
    return GetIconsDir()+"\\"+key+".png";
}
// SHA-256 of the exact validated URL. Existing Windows CryptoAPI/advapi32
// supplies the digest; no network or host texture call under the state lock.
static std::string ArtworkDigest(const std::string& url){
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    if(!CryptAcquireContextA(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))return {};
    BYTE bytes[32]={};DWORD count=sizeof(bytes);
    const bool ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&
        CryptHashData(hash,reinterpret_cast<const BYTE*>(url.data()),static_cast<DWORD>(url.size()),0)&&
        CryptGetHashParam(hash,HP_HASHVAL,bytes,&count,0)&&count==sizeof(bytes);
    if(hash)CryptDestroyHash(hash);
    CryptReleaseContext(provider,0);
    if(!ok)return {};
    static constexpr char hex[]="0123456789abcdef";
    std::string out;out.reserve(64);
    for(auto byte:bytes){out.push_back(hex[byte>>4]);out.push_back(hex[byte&15]);}
    return out;
}
std::string IconManager::ResolveKey(uint32_t id,const std::string& url){
    if(!id)return {};
    if(EmbeddedResource(id))return std::to_string(id);
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if(url.empty()){
            const auto it=s_LatestKeys.find(id);
            return ArtworkRequest::OfflineKey(id,it==s_LatestKeys.end()?std::string{}:it->second);
        }
        const auto it=s_KeyMemo.find(id);
        if(it!=s_KeyMemo.end()&&it->second.first==url)return it->second.second;
    }
    if(!ExternalResource::ArtworkUrl(url))return {};
    auto key=ArtworkRequest::VersionKey(id,ArtworkDigest(url));
    if(!key.empty()){
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_KeyMemo[id]={url,key};
    }
    return key;
}
// Version-qualified negative entries and last successful offline pointers.
// Legacy ID-only failures cannot suppress an exact new source URL.
void IconManager::LoadArtworkState(){
    std::ifstream file(GetIconsDir()+"\\artwork_state.txt");
    const auto now=static_cast<int64_t>(std::time(nullptr));
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_NetworkFailures.clear();s_LatestKeys.clear();
    std::string line;
    while(std::getline(file,line)){
        if(line.size()>150)continue;
        std::istringstream in(line);char kind=0;std::string key,extra;int64_t value=0;
        if(!(in>>kind>>key>>value)||(in>>extra))continue;
        const auto owner=ArtworkRequest::VersionOwner(key);if(!owner)continue;
        if(kind=='L'&&value==owner)s_LatestKeys[owner]=key;
        else if(kind=='F'&&value>0&&value<=now&&now-value<NETWORK_RETRY_SECONDS)s_NetworkFailures[key]=value;
    }
}
void IconManager::SaveArtworkState(){
    std::unordered_map<std::string,int64_t> failures;
    std::unordered_map<uint32_t,std::string> latest;
    {std::lock_guard<std::mutex> lock(s_Mutex);failures=s_NetworkFailures;latest=s_LatestKeys;}
    const auto path=GetIconsDir()+"\\artwork_state.txt",temporary=path+".part";
    std::ofstream file(temporary,std::ios::trunc);
    if(!file)return;
    for(const auto& [id,key]:latest)file<<"L "<<key<<' '<<id<<'\n';
    for(const auto& [key,stamp]:failures)file<<"F "<<key<<' '<<stamp<<'\n';
    file.close();
    if(!file.good()||!MoveFileExA(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        DeleteFileA(temporary.c_str());
}

bool IconManager::DownloadToFile(const std::string& url, const std::string& filePath) {
    if (AlterEgo::Background::Workers().Stopping()) return {};
    // All dynamic artwork uses the common HTTP transport and diagnostics.
    if(!AlterEgo::ExternalResource::ArtworkUrl(url))return false;
    auto response=Skinventory::HttpClient::GetEx(url);
    if(response.status_code<200||response.status_code>=300||response.body.empty()||
       response.body.size()>32u*1024u*1024u)return false;
    const auto& buffer=response.body;
    if(!AlterEgo::ExternalResource::ImageHeader(buffer))return false;

    // Verify image magic bytes (PNG or JPEG)
    bool isPng = (buffer.size() >= 8 && buffer[0] == (char)0x89 && buffer[1] == 'P' && buffer[2] == 'N' && buffer[3] == 'G');
    bool isJpeg = (buffer.size() >= 3 && (unsigned char)buffer[0] == 0xFF && (unsigned char)buffer[1] == 0xD8 && (unsigned char)buffer[2] == 0xFF);
    if (!isPng && !isJpeg) {
        return false;
    }

    // Use correct extension based on format
    std::string actualPath = filePath;
    if (isJpeg && actualPath.size() > 4 && actualPath.substr(actualPath.size() - 4) == ".png") {
        actualPath = actualPath.substr(0, actualPath.size() - 4) + ".jpg";
    }

    const auto temporary=actualPath+".part";
    std::ofstream file(temporary, std::ios::binary|std::ios::trunc);
    if (!file.is_open()) return false;
    file.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();
    if(!file.good()||s_StopWorker||Background::Workers().Stopping()||!MoveFileExA(temporary.c_str(),actualPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileA(temporary.c_str());return false;
    }
    return true;
}

bool IconManager::LoadIconFromDisk(const std::string& key) {
    if(!s_API||key.empty())return false;
    std::string path=GetIconFilePath(key);
    if(GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES){
        path=path.substr(0,path.size()-4)+".jpg";
        if(GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES)return false;
    }
    const std::string identifier="AE_ICON_"+key;
    try{
        Texture_t* tex=s_API->Textures_GetOrCreateFromFile(identifier.c_str(),path.c_str());
        if(tex&&tex->Resource){
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_IconCache[key]=tex;s_LoadingIcons.erase(key);s_FailedIcons.erase(key);
            return true;
        }
    }catch(...){}
    return false;
}
void IconManager::Tick(){
    if(!s_API)return;
    std::vector<std::string> ready;
    {std::lock_guard<std::mutex> lock(s_Mutex);ready.swap(s_ReadyQueue);}
    for(const auto& key:ready){
        try{if(LoadIconFromDisk(key))continue;}catch(...){}
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_LoadingIcons.erase(key);s_FailedIcons[key]=std::chrono::steady_clock::now();
    }
}
Texture_t* IconManager::GetByKey(const std::string& key,uint32_t embeddedId){
    if(!s_API||key.empty())return nullptr;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        auto it=s_IconCache.find(key);
        if(it!=s_IconCache.end()){
            if(it->second&&it->second->Resource)return it->second;
            s_IconCache.erase(it);
        }
    }
    const std::string identifier="AE_ICON_"+key;
    Texture_t* tex=nullptr;
    try{tex=s_API->Textures_Get(identifier.c_str());}catch(...){return nullptr;}
    if(tex&&tex->Resource){
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_IconCache[key]=tex;s_LoadingIcons.erase(key);s_FailedIcons.erase(key);
        return tex;
    }
    if(embeddedId&&key==std::to_string(embeddedId)&&EmbeddedResource(embeddedId)){
        if(auto* embedded=LoadEmbeddedIcon(s_API,embeddedId,identifier);embedded&&embedded->Resource){
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_IconCache[key]=embedded;s_LoadingIcons.erase(key);s_FailedIcons.erase(key);
            return embedded;
        }
        return nullptr; // linked resource wins even while Nexus is publishing
    }
    bool check=false;
    {std::lock_guard<std::mutex> lock(s_Mutex);check=s_DiskCheckedMissing.insert(key).second;}
    if(check&&LoadIconFromDisk(key)){
        std::lock_guard<std::mutex> lock(s_Mutex);
        auto it=s_IconCache.find(key);if(it!=s_IconCache.end())return it->second;
    }
    return nullptr;
}
Texture_t* IconManager::GetIcon(uint32_t id){
    return GetByKey(ResolveKey(id,{}),id);
}
Texture_t* IconManager::GetOrRequestIcon(uint32_t id,const std::string& iconUrl,bool priority){
    const auto key=ResolveKey(id,iconUrl);
    return ArtworkRequest::Resolve(id,iconUrl,priority,
        [&](uint32_t,const std::string&){return GetByKey(key,id);},
        [&](uint32_t requested,const std::string& url,bool urgent){RequestKey(requested,key,url,urgent);});
}
void IconManager::RequestIcon(uint32_t id,const std::string& iconUrl,bool priority){
    (void)GetOrRequestIcon(id,iconUrl,priority);
}
void IconManager::RequestKey(uint32_t id,const std::string& key,const std::string& url,bool priority){
    if(!s_API||s_StopWorker||ArtworkRequest::VersionOwner(key)!=id||!id)return;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if(s_StopWorker||s_IconCache.count(key))return;
        if(s_LoadingIcons.count(key)){
            if(priority)for(auto it=s_RequestQueue.begin();it!=s_RequestQueue.end();++it)
                if(it->key==key){std::rotate(s_RequestQueue.begin(),it,it+1);break;}
            return;
        }
        const auto now=static_cast<int64_t>(std::time(nullptr));
        auto network=s_NetworkFailures.find(key);
        if(network!=s_NetworkFailures.end()){
            if(now>=network->second&&now-network->second<NETWORK_RETRY_SECONDS)return;
            s_NetworkFailures.erase(network);
        }
        auto fail=s_FailedIcons.find(key);
        if(fail!=s_FailedIcons.end()){
            if(std::chrono::steady_clock::now()-fail->second<std::chrono::seconds(RETRY_COOLDOWN_SEC))return;
            s_FailedIcons.erase(fail);
        }
        try{
            s_LoadingIcons[key]=true;
            QueuedRequest req{id,key,url};
            if(priority)s_RequestQueue.insert(s_RequestQueue.begin(),std::move(req));
            else s_RequestQueue.push_back(std::move(req));
        }catch(...){s_LoadingIcons.erase(key);return;}
    }
    s_QueueCV.notify_one();
}
void IconManager::ProcessRequestQueue(){
    QueuedRequest request{};
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if(s_RequestQueue.empty())return;
        request=std::move(s_RequestQueue.front());s_RequestQueue.erase(s_RequestQueue.begin());
    }
    const auto& key=request.key;
    try{
        if(DownloadToFile(request.iconUrl,GetIconFilePath(key))){
            {
                std::lock_guard<std::mutex> lock(s_Mutex);
                s_NetworkFailures.erase(key);
                // An old queued response can populate only its own version.
                // It must not replace the offline pointer after newer demand.
                const auto current=s_KeyMemo.find(request.id);
                if(current!=s_KeyMemo.end()&&current->second.second==key)s_LatestKeys[request.id]=key;
                if(!s_StopWorker)s_ReadyQueue.push_back(key);else s_LoadingIcons.erase(key);
            }
            SaveArtworkState();return;
        }
    }catch(...){}
    bool failed=false;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_LoadingIcons.erase(key);
        if(!s_StopWorker&&!Background::Workers().Stopping()){
            s_NetworkFailures[key]=static_cast<int64_t>(std::time(nullptr));failed=true;
        }
    }
    if(failed)SaveArtworkState();
}

void IconManager::DownloadWorker() {
    while (!s_StopWorker) {
        {
            std::unique_lock<std::mutex> lock(s_Mutex);
            s_QueueCV.wait_for(lock, std::chrono::milliseconds(REQUEST_DELAY_MS), [] {
                return s_StopWorker.load() || !s_RequestQueue.empty();
            });
            if (s_StopWorker) return;
            if (s_RequestQueue.empty()) continue;
        }

        try {
            ProcessRequestQueue();
        } catch (...) {}

        std::unique_lock<std::mutex> delayLock(s_Mutex);
        if(s_QueueCV.wait_for(delayLock,std::chrono::milliseconds(REQUEST_DELAY_MS),[]{return s_StopWorker.load();}))return;
    }
}

bool IconManager::IsIconLoaded(uint32_t id){
    const auto key=ResolveKey(id,{});
    if(key.empty())return false;
    std::lock_guard<std::mutex> lock(s_Mutex);
    if(s_IconCache.count(key)||s_LoadingIcons.count(key))return true;
    auto fail=s_FailedIcons.find(key);
    return fail!=s_FailedIcons.end()&&std::chrono::steady_clock::now()-fail->second<std::chrono::seconds(RETRY_COOLDOWN_SEC);
}

} // namespace AlterEgo
