#include "HttpClient.h"
#include "ExternalResourceSafety.h"
#include "BackgroundWorkers.h"
#include <ctime>
#include "IconManager.h"
#include "StoragePaths.h"
#include "GW2API.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace AlterEgo {

AddonAPI_t* IconManager::s_API = nullptr;
std::unordered_map<uint32_t, Texture_t*> IconManager::s_IconCache;
std::unordered_set<uint32_t> IconManager::s_DiskCheckedMissing;
std::unordered_map<uint32_t, bool> IconManager::s_LoadingIcons;
std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> IconManager::s_FailedIcons;
std::mutex IconManager::s_Mutex;
std::unordered_map<uint32_t,int64_t> IconManager::s_NetworkFailures;
std::vector<IconManager::QueuedRequest> IconManager::s_RequestQueue;
std::chrono::steady_clock::time_point IconManager::s_LastRequestTime = std::chrono::steady_clock::now();
std::string IconManager::s_IconsDir;
std::thread IconManager::s_DownloadThread;
std::condition_variable IconManager::s_QueueCV;
std::atomic<bool> IconManager::s_StopWorker{true};
std::vector<uint32_t> IconManager::s_ReadyQueue;


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
        LoadNetworkFailures();
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

std::string IconManager::GetIconFilePath(uint32_t id) {
    std::stringstream ss;
    ss << GetIconsDir() << "\\" << id << ".png";
    return ss.str();
}

// Only load during initialization and persist on the owned artwork worker.
void IconManager::LoadNetworkFailures() {
    std::ifstream file(GetIconsDir()+"\\failed_icons.txt");
    uint32_t id=0;int64_t stamp=0;const auto now=static_cast<int64_t>(std::time(nullptr));
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_NetworkFailures.clear();
    while(file>>id>>stamp)if(id&&stamp>0&&stamp<=now&&now-stamp<NETWORK_RETRY_SECONDS)
        s_NetworkFailures[id]=stamp;
}
void IconManager::SaveNetworkFailures() {
    std::unordered_map<uint32_t,int64_t> snapshot;
    {std::lock_guard<std::mutex> lock(s_Mutex);snapshot=s_NetworkFailures;}
    const auto path=GetIconsDir()+"\\failed_icons.txt",temporary=path+".part";
    std::ofstream file(temporary,std::ios::trunc);
    if(!file)return;
    for(const auto& [id,stamp]:snapshot)file<<id<<' '<<stamp<<'\n';
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
    if(!file.good()||!MoveFileExA(temporary.c_str(),actualPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileA(temporary.c_str());return false;
    }
    return true;
}

bool IconManager::LoadIconFromDisk(uint32_t id) {
    if (!s_API) return false;

    std::string filePath = GetIconFilePath(id);

    // Try .png first, then .jpg
    DWORD attrs = GetFileAttributesA(filePath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        // Try .jpg extension
        std::string jpgPath = filePath.substr(0, filePath.size() - 4) + ".jpg";
        attrs = GetFileAttributesA(jpgPath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) return false;
        filePath = jpgPath;
    }

    std::stringstream ss;
    ss << "AE_ICON_" << id;
    std::string identifier = ss.str();

    try {
        Texture_t* tex = s_API->Textures_GetOrCreateFromFile(identifier.c_str(), filePath.c_str());
        if (tex && tex->Resource) {
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_IconCache[id] = tex;
            s_LoadingIcons.erase(id);
            s_FailedIcons.erase(id);
            return true;
        }
    } catch (...) {}
    return false;
}

void IconManager::Tick() {
    if (!s_API) return;

    std::vector<uint32_t> ready;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        ready.swap(s_ReadyQueue);
    }
    for (uint32_t id : ready) {
        try {
            if(LoadIconFromDisk(id))continue;
        } catch (...) {}
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_LoadingIcons.erase(id);
            s_FailedIcons[id] = std::chrono::steady_clock::now();
        }
    }
}

Texture_t* IconManager::GetIcon(uint32_t id) {
    if (!s_API) return nullptr;

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        auto it = s_IconCache.find(id);
        if (it != s_IconCache.end()) {
            if (it->second && it->second->Resource) {
                return it->second;
            }
            s_IconCache.erase(it);
        }
    }

    std::stringstream ss;
    ss << "AE_ICON_" << id;
    std::string identifier = ss.str();

    Texture_t* tex = nullptr;
    try {
        tex = s_API->Textures_Get(identifier.c_str());
    } catch (...) {
        return nullptr;
    }

    if (tex) {
        if (tex->Resource) {
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_IconCache[id] = tex;
            s_LoadingIcons.erase(id);
            s_FailedIcons.erase(id);
            return tex;
        }
    }
    // A missing Nexus texture does not mean a queued HTTP download failed.
    // Only the queue owner or disk publication may release its loading marker.

    // Built-in static icons win over network and disk lookup. The first frame
    // can render these even before the GW2 profession API has responded.
    if (Texture_t* embedded = LoadEmbeddedIcon(s_API, id, identifier)) {
        if (embedded->Resource) {
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_IconCache[id] = embedded;
            s_LoadingIcons.erase(id);
            s_FailedIcons.erase(id);
            return embedded;
        }
    }
    // A verified embedded resource may still be awaiting Nexus publication.
    // Never replace it from disk or the network just because it is not ready.
    if(EmbeddedResource(id))return nullptr;
    // Offline-first fallback for EVERY cached icon, not only class artwork:
    // a saved item/skin/skill PNG is usable even if its metadata URL has not
    // arrived yet. Avoid checking the same missing disk file each frame.
    bool shouldCheckDisk = false;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        shouldCheckDisk = s_DiskCheckedMissing.insert(id).second;
    }
    if (shouldCheckDisk && LoadIconFromDisk(id)) {
        std::lock_guard<std::mutex> lock(s_Mutex);
        auto found = s_IconCache.find(id);
        if (found != s_IconCache.end()) return found->second;
    }
    return nullptr;
}

void IconManager::RequestIcon(uint32_t id, const std::string& iconUrl, bool priority) {
    if (!s_API || s_StopWorker) return;
    // Embedded assets can be resolved without a URL; this also avoids queuing
    // needless downloads for the nine professions, elite specs, and UI glyphs.
    if (GetIcon(id)||EmbeddedResource(id)) return;

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_StopWorker || s_IconCache.find(id) != s_IconCache.end()) return;
        if (s_LoadingIcons.find(id) != s_LoadingIcons.end()) {
            // Already queued — but if we're now asking for it with priority,
            // pull it forward in the queue.
            if (priority) {
                for (auto it = s_RequestQueue.begin(); it != s_RequestQueue.end(); ++it) {
                    if (it->id == id) {
                        std::rotate(s_RequestQueue.begin(),it,it+1);
                        break;
                    }
                }
            }
            return;
        }

        const auto now=static_cast<int64_t>(std::time(nullptr));
        auto network=s_NetworkFailures.find(id);
        if(network!=s_NetworkFailures.end()){
            if(now>=network->second&&now-network->second<NETWORK_RETRY_SECONDS)return;
            s_NetworkFailures.erase(network);
        }
        auto failIt = s_FailedIcons.find(id);
        if (failIt != s_FailedIcons.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - failIt->second).count();
            if (elapsed < RETRY_COOLDOWN_SEC) return;
            s_FailedIcons.erase(failIt);
        }

        s_LoadingIcons[id] = true;
    }

    // GetIcon has already attempted the disk cache on the render thread;
    // callers which reach RequestIcon without GetIcon get that check too.
    bool wasChecked = false;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        wasChecked = s_DiskCheckedMissing.count(id) != 0;
    }
    if (!wasChecked && LoadIconFromDisk(id)) return;

    // No URL and no disk cache → nothing we can do right now. Clear the loading
    // flag without marking failed so a later call (with URL) can retry.
    if (iconUrl.empty()) {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_LoadingIcons.erase(id);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if(s_StopWorker) {s_LoadingIcons.erase(id);return;}
        try {
            QueuedRequest req{id,iconUrl};
            if (priority)
                s_RequestQueue.insert(s_RequestQueue.begin(),std::move(req));
            else
                s_RequestQueue.push_back(std::move(req));
        } catch (...) {
            s_LoadingIcons.erase(id);
            return;
        }
    }
    s_QueueCV.notify_one();
}

void IconManager::ProcessRequestQueue() {
    uint32_t id = 0;
    std::string iconUrl;

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_RequestQueue.empty()) return;
        id = s_RequestQueue.front().id;
        iconUrl = std::move(s_RequestQueue.front().iconUrl);
        s_RequestQueue.erase(s_RequestQueue.begin());
    }

    if (iconUrl.empty()) {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_LoadingIcons.erase(id);
        s_FailedIcons[id] = std::chrono::steady_clock::now();
        return;
    }

    try {
        std::string filePath = GetIconFilePath(id);
        if (DownloadToFile(iconUrl, filePath)) {
            bool removed=false;
            {std::lock_guard<std::mutex> lock(s_Mutex);
             removed=s_NetworkFailures.erase(id)!=0;
             if(!s_StopWorker)s_ReadyQueue.push_back(id);else s_LoadingIcons.erase(id);}
            if(removed)SaveNetworkFailures();
            return;
        }
    } catch (...) {}

    bool failed=false;
    {std::lock_guard<std::mutex> lock(s_Mutex);
     s_LoadingIcons.erase(id);
     if(!s_StopWorker&&!Background::Workers().Stopping()){
         s_NetworkFailures[id]=static_cast<int64_t>(std::time(nullptr));failed=true;
     }}
    if(failed)SaveNetworkFailures();
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

        std::this_thread::sleep_for(std::chrono::milliseconds(REQUEST_DELAY_MS));
    }
}

bool IconManager::IsIconLoaded(uint32_t id) {
    std::lock_guard<std::mutex> lock(s_Mutex);
    if (s_IconCache.find(id) != s_IconCache.end()) return true;
    if (s_LoadingIcons.find(id) != s_LoadingIcons.end()) return true;

    auto failIt = s_FailedIcons.find(id);
    if (failIt != s_FailedIcons.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - failIt->second).count();
        if (elapsed < RETRY_COOLDOWN_SEC) return true;
    }
    return false;
}

} // namespace AlterEgo
