#ifndef ICONMANAGER_H
#define ICONMANAGER_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <chrono>
#include "nexus/Nexus.h"
#include "ArtworkRequest.h"

namespace AlterEgo {

inline constexpr uint32_t kEmboldenedIcon=9300001;
inline constexpr uint32_t kCallOfTheMistsIcon=9300002;

class IconManager {
public:
    static void Initialize(AddonAPI_t* api);
    static void RequestStop();
    static void Shutdown();

    // Get icon texture for a given identifier (returns nullptr if not loaded yet)
    static Texture_t* GetIcon(uint32_t id);

    // Shared offline-first artwork resolution for callers with a metadata URL.
    static Texture_t* GetOrRequestIcon(uint32_t id, const std::string& iconUrl,
                                       bool priority = false);

    // Request icon to be loaded (async) - uses GW2 API icon URL.
    // When priority=true, the request jumps to the front of the queue —
    // useful for icons currently visible in the viewport.
    static void RequestIcon(uint32_t id, const std::string& iconUrl, bool priority = false);

    // Process icon queue (call every frame, even when window is hidden)
    static void Tick();

    // Check if icon is loaded
    static bool IsIconLoaded(uint32_t id);

private:
    static void ProcessRequestQueue();
    static void DownloadWorker();

    // Disk cache helpers
    static std::string GetIconsDir();
    static std::string GetIconFilePath(uint32_t id);
    static bool DownloadToFile(const std::string& url, const std::string& filePath);
    static bool LoadIconFromDisk(uint32_t id);
    static void LoadNetworkFailures();
    static void SaveNetworkFailures();

    static AddonAPI_t* s_API;
    static std::unordered_map<uint32_t, Texture_t*> s_IconCache;
    // One-time negative disk lookups prevent filesystem stats on every frame.
    static std::unordered_set<uint32_t> s_DiskCheckedMissing;
    static std::unordered_map<uint32_t, bool> s_LoadingIcons;
    static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> s_FailedIcons;
    // Texture publication has a short session cooldown; failed HTTP artwork
    // has a separate durable seven-day retry policy.
    static std::unordered_map<uint32_t,int64_t> s_NetworkFailures;
    static constexpr int64_t NETWORK_RETRY_SECONDS=7LL*86400;
    static const int RETRY_COOLDOWN_SEC = 300;
    static std::mutex s_Mutex;
    static std::string s_IconsDir;

    // Rate limiting
    struct QueuedRequest {
        uint32_t id;
        std::string iconUrl;
    };
    static std::vector<QueuedRequest> s_RequestQueue;
    static std::chrono::steady_clock::time_point s_LastRequestTime;
    static const int REQUEST_DELAY_MS = 100;

    // Background download thread
    static std::thread s_DownloadThread;
    static std::condition_variable s_QueueCV;
    static std::atomic<bool> s_StopWorker;

    // Ready queue: items downloaded to disk, waiting for render-thread texture load
    static std::vector<uint32_t> s_ReadyQueue;
};

} // namespace AlterEgo

#endif // ICONMANAGER_H
