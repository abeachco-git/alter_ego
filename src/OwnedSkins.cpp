#include "OwnedSkins.h"
#include "HoardAndSeekAPI.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <memory>
#include <chrono>
#include <ctime>
#include <windows.h>
#include "GW2API.h"

using json = nlohmann::json;

namespace Skinventory {

    AddonAPI_t* OwnedSkins::s_API = nullptr;
    std::string OwnedSkins::s_data_dir;
    std::unordered_set<uint32_t> OwnedSkins::s_owned_skins;
    std::unordered_set<uint32_t> OwnedSkins::s_queried_skins;
    std::mutex OwnedSkins::s_mutex;
    std::atomic<bool> OwnedSkins::s_has_data{false};
    std::atomic<bool> OwnedSkins::s_hs_available{false};
    std::atomic<bool> OwnedSkins::s_querying{false};
    std::atomic<bool> OwnedSkins::s_data_updated{false};
    std::string OwnedSkins::s_status_message = "Waiting for Hoard & Seek...";
    std::time_t OwnedSkins::s_last_success_time = 0;
    std::atomic<uint64_t> OwnedSkins::s_generation{0};
    std::unordered_set<uint32_t> OwnedSkins::s_locally_marked;
    std::vector<uint32_t> OwnedSkins::s_pending_ids;
    size_t OwnedSkins::s_batch_index = 0;
    std::atomic<bool> OwnedSkins::s_batch_pending{false};
    std::string OwnedSkins::s_account_name;

    #define SKINVENTORY_REQUESTER "Skinventory"
    #define EV_SKINVENTORY_SKINS_RESPONSE "EV_SKINVENTORY_SKINS_RESPONSE"
    #define EV_SKINVENTORY_API_RESPONSE "EV_SKINVENTORY_API_RESPONSE"

    // --- Single-shot backoff retry when H&S returns HOARD_STATUS_BUSY ---
    // (proxy queue full / per-addon rate limit). We schedule ONE retry at a
    // future time rather than spinning every frame. Driven from Tick().
    namespace {
        std::atomic<int>       g_retryKind{0};   // 0 = none, 1 = API query, 2 = batch query
        std::atomic<long long> g_retryAtMs{0};   // steady_clock ms at which to retry

        long long NowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        void ScheduleRetry(int kind, uint32_t retry_after_ms) {
            g_retryKind = kind;
            g_retryAtMs = NowMs() + (retry_after_ms ? (long long)retry_after_ms : 1000LL);
        }
    }

    void OwnedSkins::Tick() {
        // Fire a scheduled BUSY backoff retry once its delay has elapsed.
        int kind = g_retryKind.load();
        if (kind != 0 && g_retryAtMs.load() != 0 && NowMs() >= g_retryAtMs.load()) {
            g_retryKind = 0;
            g_retryAtMs = 0;
            if (kind == 1) {
                s_querying = false;        // allow RequestOwnedSkinsViaApi to re-enter
                RequestOwnedSkinsViaApi();
            } else if (kind == 2) {
                SendNextBatch();           // resend current batch (s_batch_index unchanged)
            }
        }
        if (s_batch_pending.exchange(false)) {
            SendNextBatch();
        }
    }

    void OwnedSkins::SetDataDirectory(const std::string& dir) {
        s_data_dir = dir;
    }

    void OwnedSkins::Initialize(AddonAPI_t* api) {
        s_API = api;
        if (!s_API) return;

        // The selected account is set by the main render loop; do not load
        // legacy global ownership into an unknown account at startup.

        s_API->Events_Subscribe(EV_HOARD_PONG, OnPong);
        s_API->Events_Subscribe(EV_HOARD_DATA_UPDATED, OnDataUpdated);
        s_API->Events_Subscribe(EV_SKINVENTORY_SKINS_RESPONSE, OnSkinsResponse);
        s_API->Events_Subscribe(EV_SKINVENTORY_API_RESPONSE, OnApiResponse);
    }

    void OwnedSkins::Shutdown() {
        if (!s_API) return;

        s_API->Events_Unsubscribe(EV_HOARD_PONG, OnPong);
        s_API->Events_Unsubscribe(EV_HOARD_DATA_UPDATED, OnDataUpdated);
        s_API->Events_Unsubscribe(EV_SKINVENTORY_SKINS_RESPONSE, OnSkinsResponse);
        s_API->Events_Unsubscribe(EV_SKINVENTORY_API_RESPONSE, OnApiResponse);
        s_API = nullptr;
    }

    void OwnedSkins::PingHoardAndSeek() {
        if (!s_API) return;
        s_API->Events_Raise(EV_HOARD_PING, nullptr);
    }

    void OwnedSkins::SetAccountName(const std::string& name) {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_account_name == name) return;
            s_account_name = name;
            s_owned_skins.clear();
            s_queried_skins.clear();
            s_locally_marked.clear();
            s_pending_ids.clear();
            s_batch_index = 0;
            s_batch_pending = false;
            s_querying = false;
            s_has_data = false;
            s_last_success_time = 0;
            s_generation++;
            s_status_message = "No ownership data - click Refresh";
        }
        g_retryKind = 0;
        g_retryAtMs = 0;
        // Account-specific disk data is restored as soon as the dropdown
        // changes, even with automatic API refresh disabled.
        if (!name.empty() && LoadFromCache()) {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_account_name == name) {
                s_has_data = true;
                s_status_message = "Owned: " + std::to_string(s_owned_skins.size()) + " (cached)";
            }
        }
    }

    void OwnedSkins::OnPong(void* eventArgs) {
        if (!eventArgs) return;
        auto* pong = (HoardPongPayload*)eventArgs;
        if (pong->api_version < 2) return;

        s_hs_available = true;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (pong->has_data) {
                s_status_message = "Hoard & Seek connected";
            } else {
                s_status_message = "Hoard & Seek has no data - refresh in H&S first";
            }
        }
    }

    void OwnedSkins::OnDataUpdated(void* eventArgs) {
        // H&S finished a data refresh — signal that we should re-query skins
        s_hs_available = true;
        s_data_updated = true;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_status_message = "Hoard & Seek data updated";
        }
    }

    bool OwnedSkins::ConsumeDataUpdatedFlag() {
        return s_data_updated.exchange(false);
    }

    void OwnedSkins::RequestOwnedSkinsViaApi() {
        if (!s_API || !s_hs_available) return;
        if (s_querying) return;

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_querying = true;
            s_status_message = "Querying /v2/account/skins...";
        }

        std::string acctName;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            acctName = s_account_name;
        }
        HoardQueryApiRequest req{};
        req.api_version = HOARD_API_VERSION;
        strncpy(req.requester, SKINVENTORY_REQUESTER, sizeof(req.requester));
        strncpy(req.endpoint, "/v2/account/skins", sizeof(req.endpoint));
        strncpy(req.response_event, EV_SKINVENTORY_API_RESPONSE, sizeof(req.response_event));
        if (!acctName.empty())
            strncpy(req.account_name, acctName.c_str(), sizeof(req.account_name) - 1);

        s_API->Events_Raise(EV_HOARD_QUERY_API, &req);
    }

    void OwnedSkins::OnApiResponse(void* eventArgs) {
        if (!eventArgs) return;
        auto* resp = (HoardQueryApiResponse*)eventArgs;
        std::unique_ptr<HoardQueryApiResponse> respGuard(resp); // caller owns the response

        // Ignore delayed replies after an account switch.
        if (resp->api_version >= 3) {
            const std::string echoed(resp->account_name,
                strnlen(resp->account_name, sizeof(resp->account_name)));
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_account_name.empty() || echoed != s_account_name) return;
        }

        if (resp->status == HOARD_STATUS_DENIED) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_status_message = "Permission denied by Hoard & Seek";
            s_querying = false;
            return;
        }

        if (resp->status == HOARD_STATUS_BUSY) {
            ScheduleRetry(1, resp->retry_after_ms);
            std::lock_guard<std::mutex> lock(s_mutex);
            s_status_message = "Hoard & Seek busy, retrying shortly...";
            s_querying = false;
            return;
        }

        if (resp->status == HOARD_STATUS_OK) {
            // Check if truncated — if so, data is incomplete
            // Also check json_length vs buffer: /v2/account/skins can exceed 64KB
            if (resp->truncated || resp->json_length >= sizeof(resp->json)) {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_status_message = "API response truncated, falling back to batch query...";
                s_querying = false;
                // Caller should fall back to RequestOwnedSkins()
                return;
            }

            // Parse the JSON array of owned skin IDs
            try {
                json j = json::parse(resp->json);
                if (j.is_array()) {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    s_owned_skins.clear();
                    for (const auto& id : j) {
                        s_owned_skins.insert(id.get<uint32_t>());
                    }
                    // Re-insert locally marked skins (from alerts) that stale API data may not include
                    for (uint32_t id : s_locally_marked) {
                        s_owned_skins.insert(id);
                    }
                    s_has_data = true;
                    s_querying = false;
                    s_last_success_time = std::time(nullptr);
                    s_generation++;
                    s_status_message = "Owned: " + std::to_string(s_owned_skins.size());
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_status_message = "Failed to parse /v2/account/skins response";
                s_querying = false;
            }

            // Save to disk cache
            SaveToCache();
        }
    }

    void OwnedSkins::RequestOwnedSkins(const std::vector<uint32_t>& skin_ids) {
        if (!s_API || !s_hs_available || skin_ids.empty()) return;
        if (s_querying) return;

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_pending_ids = skin_ids;
            s_batch_index = 0;
            s_querying = true;
            s_status_message = "Querying owned skins (batch)...";
        }

        SendNextBatch();
    }

    void OwnedSkins::SendNextBatch() {
        if (!s_API) return;

        // Prepare request under lock, then release lock BEFORE calling Events_Raise.
        // Nexus may dispatch events synchronously (inline), which would call our
        // OnSkinsResponse handler. If we held s_mutex here, that would deadlock.
        HoardQuerySkinsRequest req{};  // Stack-allocated (matches FlipOut pattern)
        bool allDone = false;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_batch_index >= s_pending_ids.size()) {
                s_querying = false;
                s_has_data = true;
                s_last_success_time = std::time(nullptr);
                s_status_message = "Owned: " + std::to_string(s_owned_skins.size()) +
                                   " / " + std::to_string(s_queried_skins.size());
                allDone = true;
            }
        }
        if (allDone) {
            SaveToCache();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            size_t end = std::min(s_batch_index + 200, s_pending_ids.size());

            req.api_version = HOARD_API_VERSION;
            strncpy(req.requester, SKINVENTORY_REQUESTER, sizeof(req.requester));
            strncpy(req.response_event, EV_SKINVENTORY_SKINS_RESPONSE, sizeof(req.response_event));
            if (!s_account_name.empty())
                strncpy(req.account_name, s_account_name.c_str(), sizeof(req.account_name) - 1);

            req.id_count = 0;
            for (size_t i = s_batch_index; i < end; i++) {
                req.ids[req.id_count++] = s_pending_ids[i];
            }

            s_status_message = "Querying skins " + std::to_string(s_batch_index) +
                               "-" + std::to_string(end) + "/" + std::to_string(s_pending_ids.size());
        }
        // Lock released — safe to raise event now
        s_API->Events_Raise(EV_HOARD_QUERY_SKINS, &req);
    }

    void OwnedSkins::OnSkinsResponse(void* eventArgs) {
        if (!eventArgs) return;
        auto* resp = (HoardQuerySkinsResponse*)eventArgs;
        std::unique_ptr<HoardQuerySkinsResponse> respGuard(resp); // caller owns the response

        // Ignore delayed replies after an account switch.
        if (resp->api_version >= 3) {
            const std::string echoed(resp->account_name,
                strnlen(resp->account_name, sizeof(resp->account_name)));
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_account_name.empty() || echoed != s_account_name) return;
        }

        if (resp->status == HOARD_STATUS_DENIED) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_status_message = "Permission denied by Hoard & Seek";
            s_querying = false;
            return;
        }

        if (resp->status == HOARD_STATUS_BUSY) {
            // Keep s_querying / s_batch_index unchanged — resend this same batch
            // after the backoff delay (handled in Tick).
            ScheduleRetry(2, resp->retry_after_ms);
            std::lock_guard<std::mutex> lock(s_mutex);
            s_status_message = "Hoard & Seek busy, retrying shortly...";
            return;
        }

        if (resp->status == HOARD_STATUS_OK) {
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                for (uint32_t i = 0; i < resp->entry_count; i++) {
                    uint32_t id = resp->entries[i].id;
                    s_queried_skins.insert(id);
                    if (resp->entries[i].unlocked) {
                        s_owned_skins.insert(id);
                    }
                }
                s_batch_index += resp->entry_count;
            }

            // Defer next batch to Tick() to avoid deep recursion
            // (H&S delivers events synchronously inside Events_Raise)
            s_batch_pending = true;
        }
    }

    bool OwnedSkins::IsOwned(uint32_t skin_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_owned_skins.count(skin_id) > 0;
    }

    void OwnedSkins::MarkOwned(uint32_t skin_id) {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_owned_skins.insert(skin_id);
            s_locally_marked.insert(skin_id);
        }
        s_generation++;
        SaveToCache();
    }

    bool OwnedSkins::HasData() {
        return s_has_data;
    }

    bool OwnedSkins::IsHoardAndSeekAvailable() {
        return s_hs_available;
    }

    bool OwnedSkins::IsQuerying() {
        return s_querying;
    }

    std::string OwnedSkins::GetStatusMessage() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_status_message;
    }

    std::time_t OwnedSkins::GetLastSuccessTime() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_last_success_time;
    }

    size_t OwnedSkins::GetOwnedCount() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_owned_skins.size();
    }

    size_t OwnedSkins::GetQueriedCount() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_queried_skins.size();
    }

    uint64_t OwnedSkins::GetGeneration() {
        return s_generation;
    }

    // --- Disk Cache ---

    static std::string AccountCachePath(const std::string& dir, const std::string& account) {
        std::string slug;
        slug.reserve(account.size());
        for (unsigned char c : account)
            slug.push_back(std::isalnum(c) || c == '-' || c == '_' ? (char)c : '_');
        return dir + "/owned_skins_" + slug + ".json";
    }

    bool OwnedSkins::SaveToCache() {
        std::string account;
        std::time_t success = 0;
        json ids = json::array();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            account = s_account_name;
            success = s_last_success_time;
            for (uint32_t id : s_owned_skins) ids.push_back(id);
        }
        if (s_data_dir.empty() || account.empty()) return false;
        try { std::filesystem::create_directories(s_data_dir); }
        catch (...) { return false; }

        json cache;
        cache["account"] = account;
        cache["skins"] = std::move(ids);
        cache["last_success"] = (int64_t)success;
        const std::string path = AccountCachePath(s_data_dir, account);
        const std::string tmp = path + ".tmp";
        try {
            {
                std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
                if (!file.is_open()) return false;
                const std::string data = cache.dump();
                file.write(data.data(), (std::streamsize)data.size());
                file.flush();
                if (!file.good()) return false;
            }
            // A response after changing accounts cannot overwrite the new
            // account's file: the snapshot always carries its original name.
            if (!MoveFileExA(tmp.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;
            return true;
        } catch (...) { return false; }
    }

    bool OwnedSkins::LoadFromCache() {
        std::string account;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            account = s_account_name;
        }
        if (s_data_dir.empty() || account.empty()) return false;
        const std::string path = AccountCachePath(s_data_dir, account);
        std::ifstream file(path);
        bool legacy = false;
        if (!file.is_open() && AlterEgo::GW2API::HasAccountsData() &&
            AlterEgo::GW2API::GetAccountCount() == 1) {
            // Only migrate an old unscoped cache when the configured account
            // count proves it cannot belong to a different account.
            file.open(s_data_dir + "/owned_skins.json");
            legacy = file.is_open();
        }
        if (!file.is_open()) return false;
        try {
            json j = json::parse(file);
            const json* skins = &j;
            std::time_t lastSuccess = 0;
            if (j.is_object()) {
                if (!j.contains("skins") || !j["skins"].is_array()) return false;
                if (j.contains("account") && j["account"].get<std::string>() != account)
                    return false;
                skins = &j["skins"];
                lastSuccess = (std::time_t)j.value("last_success", (int64_t)0);
            }
            if (!skins->is_array()) return false;
            std::unordered_set<uint32_t> restored;
            for (const auto& id : *skins) restored.insert(id.get<uint32_t>());
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                if (s_account_name != account) return false;
                s_owned_skins = std::move(restored);
                s_last_success_time = lastSuccess;
                s_generation++;
                s_has_data = true;
            }
            if (legacy) SaveToCache();
            return true;
        } catch (...) { return false; }
    }

}
