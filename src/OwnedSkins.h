#pragma once

#include <vector>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <string>
#include <ctime>
#include "nexus/Nexus.h"

#include <nlohmann/json.hpp>

namespace Skinventory {

    class OwnedSkins {
    public:
        // Initialize: subscribe to native API events
        static void Initialize(AddonAPI_t* api);
        // Cleanup: unsubscribe from events
        static void Shutdown();

        // Call once per frame to send deferred batch queries
        static void Tick();

        // Set data directory for disk cache
        static void SetDataDirectory(const std::string& dir);

        // Set account name for multi-account native API v3 queries
        static void SetAccountName(const std::string& name);

        // Ping native API to check availability

        // Request owned skins via native API API proxy (/v2/account/skins) — single request
        static void RequestOwnedSkinsViaApi(bool manual=true);
        // Read-only, account/credential-qualified ordinary freshness policy.
        static bool RefreshDue();

        // Request owned skin data from native API (batched, up to 200 at a time)
        // Fallback if API proxy response is truncated.
        static void RequestOwnedSkins(const std::vector<uint32_t>& skin_ids);

        // Check if a specific skin is owned (returns false if unknown/not yet queried)
        static bool IsOwned(uint32_t skin_id);
        static bool HasDataForAccount(const std::string& account);
        static bool IsOwnedForAccount(const std::string& account,uint32_t id);

        // Mark a skin as owned at runtime (e.g. from Events: Alerts unlock notification)
        static void MarkOwned(uint32_t skin_id);

        // Has native API responded with data?
        static bool HasData();

        // Is native API available?
        static bool IsAccountAvailable();

        // Is a query in progress?
        static bool IsQuerying();

        // Was native API data updated since last check? (auto-clears on read)
        static bool ConsumeDataUpdatedFlag();

        // Status message for UI
        static std::string GetStatusMessage();
        static std::time_t GetLastSuccessTime();

        // Get counts
        static size_t GetOwnedCount();
        static size_t GetQueriedCount();

        // Generation counter — increments on every ownership change (for cache invalidation)
        static uint64_t GetGeneration();
        static std::unordered_set<uint32_t> GetOwnedSnapshot();

    private:
        static AddonAPI_t* s_API;
        static std::string s_data_dir;
        static std::unordered_set<uint32_t> s_owned_skins;
        static std::unordered_set<uint32_t> s_queried_skins; // All skins we've asked about
        static std::mutex s_mutex;
        static std::atomic<bool> s_has_data;
        static std::atomic<bool> s_querying;
        static std::atomic<bool> s_data_updated;
        static std::string s_status_message;
        static std::time_t s_last_success_time;
        static std::atomic<uint64_t> s_generation;
        static std::unordered_set<uint32_t> s_locally_marked; // skins marked via alerts, protected from API overwrites

        static std::string s_account_name; // for multi-account native API v3 queries
    };

}
