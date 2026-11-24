#include "IconManager.h"
#include "StoragePaths.h"
#include "GW2API.h"

#include <windows.h>
#include <wininet.h>
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
std::vector<IconManager::QueuedRequest> IconManager::s_RequestQueue;
std::chrono::steady_clock::time_point IconManager::s_LastRequestTime = std::chrono::steady_clock::now();
std::string IconManager::s_IconsDir;
std::thread IconManager::s_DownloadThread;
std::condition_variable IconManager::s_QueueCV;
std::atomic<bool> IconManager::s_StopWorker{false};
std::vector<uint32_t> IconManager::s_ReadyQueue;


static HMODULE GetEmbeddedResourceModule() {
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&GetEmbeddedResourceModule, &module);
    return module;
}

// Fixed UI graphics are compiled into RCDATA by generate_embedded_icons.py.
// Reading them never needs profession metadata or an HTTP request.
static Texture_t* LoadEmbeddedIcon(AddonAPI_t* api, uint32_t id,
                                   const std::string& identifier) {
    HMODULE module = GetEmbeddedResourceModule();
    if (!module || !api || !api->Textures_GetOrCreateFromMemory) return nullptr;
    const std::string name = "AE_" + std::to_string(id);
    HRSRC resource = FindResourceA(module, name.c_str(), RT_RCDATA);
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
    s_StopWorker = false;
    s_DownloadThread = std::thread(DownloadWorker);
}

void IconManager::Shutdown() {
    s_StopWorker = true;
    s_QueueCV.notify_all();
    if (s_DownloadThread.joinable()) {
        s_DownloadThread.join();
    }

    std::lock_guard<std::mutex> lock(s_Mutex);
    s_IconCache.clear();
    s_DiskCheckedMissing.clear();
    s_LoadingIcons.clear();
    s_ReadyQueue.clear();
    s_RequestQueue.clear();
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

bool IconManager::DownloadToFile(const std::string& url, const std::string& filePath) {
    HINTERNET hInternet = InternetOpenA("AlterEgo/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }

    std::vector<char> buffer;
    char chunk[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, chunk, sizeof(chunk), &bytesRead) && bytesRead > 0) {
        buffer.insert(buffer.end(), chunk, chunk + bytesRead);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    if (buffer.empty()) return false;

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

    std::ofstream file(actualPath, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(buffer.data(), buffer.size());
    file.close();
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
            LoadIconFromDisk(id);
        } catch (...) {
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
    } else {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_LoadingIcons.find(id) != s_LoadingIcons.end()) {
            s_LoadingIcons.erase(id);
            s_FailedIcons[id] = std::chrono::steady_clock::now();
        }
    }

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
    if (!s_API) return;
    // Embedded assets can be resolved without a URL; this also avoids queuing
    // needless downloads for the nine professions, elite specs, and UI glyphs.
    if (GetIcon(id)) return;

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_IconCache.find(id) != s_IconCache.end()) return;
        if (s_LoadingIcons.find(id) != s_LoadingIcons.end()) {
            // Already queued — but if we're now asking for it with priority,
            // pull it forward in the queue.
            if (priority) {
                for (auto it = s_RequestQueue.begin(); it != s_RequestQueue.end(); ++it) {
                    if (it->id == id) {
                        QueuedRequest req = *it;
                        s_RequestQueue.erase(it);
                        s_RequestQueue.insert(s_RequestQueue.begin(), req);
                        break;
                    }
                }
            }
            return;
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
        QueuedRequest req;
        req.id = id;
        req.iconUrl = iconUrl;
        if (priority)
            s_RequestQueue.insert(s_RequestQueue.begin(), req);
        else
            s_RequestQueue.push_back(req);
    }
    s_QueueCV.notify_one();
}

void IconManager::ProcessRequestQueue() {
    uint32_t id = 0;
    std::string iconUrl;

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_RequestQueue.empty()) return;
        QueuedRequest req = s_RequestQueue.front();
        s_RequestQueue.erase(s_RequestQueue.begin());
        id = req.id;
        iconUrl = req.iconUrl;
    }

    if (iconUrl.empty()) {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_LoadingIcons.erase(id);
        s_FailedIcons[id] = std::chrono::steady_clock::now();
        return;
    }

    std::string filePath = GetIconFilePath(id);
    if (DownloadToFile(iconUrl, filePath)) {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_ReadyQueue.push_back(id);
        return;
    }

    std::lock_guard<std::mutex> lock(s_Mutex);
    s_LoadingIcons.erase(id);
    s_FailedIcons[id] = std::chrono::steady_clock::now();
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
