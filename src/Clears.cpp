#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <set>
#include <memory>
#include <array>

#include "imgui.h"
#include "imgui_internal.h"
#include <nlohmann/json.hpp>
#include "GW2API.h"
#include "HoardAndSeekAPI.h"
#include "HttpClient.h"
#include "AddonShared.h"
#include "Clears.h"
#include "ClearsRotation.h"
#include "FractalScales.h"
#include "CompletionTheme.h"
#include "RefreshUI.h"
#include "WikiImage.h"

// =========================================================================
// Clears - Data types and module-private state
// =========================================================================

// Achievement category IDs
static constexpr uint32_t CAT_DAILY_FRACTALS = 88;
static constexpr uint32_t CAT_DAILY_BOUNTIES = 475;
static constexpr uint32_t CAT_WEEKLY_RAIDS   = 477;
static constexpr uint32_t ACH_WEEKLY_STRIKES = 9125;

struct ClearEntry {
    uint32_t id = 0;
    std::string name;
    std::string tier;
    bool done = false;
    int32_t current = 0;
    int32_t max = 0;
    std::vector<std::string> bitNames;
    std::vector<bool> bitDone;
};

// Module-private state (not accessible outside this TU)
static std::vector<ClearEntry> g_DailyFractals;
static std::vector<ClearEntry> g_DailyBounties;
static std::vector<ClearEntry> g_WeeklyWings;
static ClearEntry g_WeeklyStrikes;
static std::mutex g_ClearsMutex;
static std::string g_ClearsStatusMsg;
static std::chrono::system_clock::time_point g_LastDailyReset{};
static std::chrono::system_clock::time_point g_LastWeeklyReset{};
static std::chrono::steady_clock::time_point g_LastClearsCompletionQuery{};
static std::time_t g_ClearsLastSuccessTime = 0;
static std::chrono::steady_clock::time_point g_ClearsSuccessUntil{};
// steady_clock ms at which to fire a single BUSY backoff retry (0 = none)
static std::atomic<long long> g_ClearsRetryAtMs{0};

// Public globals — declared extern in Clears.h, accessed from dllmain.cpp
bool g_ClearsFetched = false;
bool g_ClearsFetching = false;
bool g_ClearsNeedRequery = false;
std::atomic<bool> g_ClearsQueryPending{false};

// ---- Wizard's Vault state ----
struct VaultObjective {
    uint32_t id = 0;
    std::string title;
    std::string track;  // "PvE", "PvP", "WvW"
    int32_t acclaim = 0;
    int32_t progress_current = 0;
    int32_t progress_complete = 0;
    bool claimed = false;
    bool isDone() const { return progress_complete > 0 && progress_current >= progress_complete; }
};

struct VaultPeriod {
    int32_t meta_progress_current = 0;
    int32_t meta_progress_complete = 0;
    int32_t meta_reward_astral = 0;
    bool meta_reward_claimed = false;
    std::vector<VaultObjective> objectives;
};

static VaultPeriod g_VaultDaily;
static VaultPeriod g_VaultWeekly;
static VaultPeriod g_VaultSpecial;
static std::atomic<bool> g_VaultFetched{false};
static std::atomic<bool> g_VaultFetching{false};
static bool g_VaultInitialAttempted = false;
static bool g_VaultResponsesOK[3] = {};
static VaultPeriod g_VaultPending[3];
static std::time_t g_VaultLastSuccessTime = 0;
static std::string g_VaultStatusMessage;
static bool g_VaultLastAttemptFailed = false;
static std::chrono::steady_clock::time_point g_VaultSuccessUntil{};
// steady_clock ms at which to fire a single BUSY backoff retry (0 = none)
// These per-type retry states and the staged responses are protected by
// g_ClearsMutex: H&S delivers Vault responses from a worker thread.
static long long g_VaultRetryAtMs = 0;
static bool g_VaultRetryNeeded[3] = {};
static int g_VaultRetryAttempts[3] = {};
static long long g_VaultRequestStartMs = 0;
static std::string g_VaultRequestAccount;
static constexpr long long VAULT_TIMEOUT_MS = 45000;
bool g_VaultNeedRequery = false;
static bool g_VaultHideComplete = false;   // hide claimed vault objectives
static bool g_ClearsHideComplete = false;  // hide completed clear encounters
static bool g_VaultOpen[3] = {true, true, true}; // Daily, Weekly, Special
static void LoadClearUiSettings();
static void SaveClearUiSettings();
static bool RenderVaultSectionToggle(int index, const char* title,
                                     ImVec4 color, const char* suffix,
                                     const char* summary,
                                     bool summaryComplete);
static std::chrono::system_clock::time_point g_VaultLastDailyReset{};
static std::chrono::system_clock::time_point g_VaultLastWeeklyReset{};
static std::chrono::system_clock::time_point g_VaultSeasonEnd{};

// =========================================================================
// Clears - Backend (reset calculations, fetch, H&S response)
// =========================================================================

// Forward declarations of private helpers
static void SaveClearsCache();
static void FetchClears();
static void SendClearsAchQuery();
static void FetchVaultData();
static void RequestVaultSection(uint8_t type, const std::string& account);
static void SaveVaultCache();
static void RenderClearsTabContent(const std::string& dailyResetStr, const std::string& weeklyResetStr);
static void RenderVaultPeriodSection(int sectionIndex, const char* title, const char* resetLabel,
                                     ImVec4 color, const VaultPeriod& period);
static void RenderVaultTab(const std::string& dailyResetStr, const std::string& weeklyResetStr);

// Calculate the most recent daily reset (00:00 UTC) before 'now'
std::chrono::system_clock::time_point CalcLastDailyReset(std::chrono::system_clock::time_point now) {
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif
    utc.tm_hour = 0; utc.tm_min = 0; utc.tm_sec = 0;
    auto reset = std::chrono::system_clock::from_time_t(
#ifdef _WIN32
        _mkgmtime(&utc)
#else
        timegm(&utc)
#endif
    );
    if (reset > now) reset -= std::chrono::hours(24);
    return reset;
}

// Calculate the most recent weekly reset (Monday 07:30 UTC) before 'now'
std::chrono::system_clock::time_point CalcLastWeeklyReset(std::chrono::system_clock::time_point now) {
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif
    // tm_wday: 0=Sun, 1=Mon, ...
    int daysSinceMonday = (utc.tm_wday + 6) % 7; // Mon=0, Tue=1, ... Sun=6
    utc.tm_hour = 7; utc.tm_min = 30; utc.tm_sec = 0;
    utc.tm_mday -= daysSinceMonday;
    auto reset = std::chrono::system_clock::from_time_t(
#ifdef _WIN32
        _mkgmtime(&utc)
#else
        timegm(&utc)
#endif
    );
    if (reset > now) reset -= std::chrono::hours(24 * 7);
    return reset;
}

// Format time until next reset as human-readable string
static std::string FormatTimeUntilReset(std::chrono::system_clock::time_point resetTime,
                                         std::chrono::hours period) {
    auto now = std::chrono::system_clock::now();
    auto nextReset = resetTime + period;
    if (nextReset <= now) return "now";
    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(nextReset - now).count();
    int d = (int)(remaining / 86400);
    int h = (int)((remaining % 86400) / 3600);
    int m = (int)((remaining % 3600) / 60);
    if (d > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%dd %dh", d, h);
        return buf;
    }
    if (h > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%dh %dm", h, m);
        return buf;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%dm", m);
    return buf;
}

static std::chrono::system_clock::time_point ParseISO8601(const std::string& s) {
    int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &min, &sec) != 6)
        return {};
    struct tm t{};
    t.tm_year = year - 1900; t.tm_mon = mon - 1; t.tm_mday = day;
    t.tm_hour = hour; t.tm_min = min; t.tm_sec = sec;
#ifdef _WIN32
    return std::chrono::system_clock::from_time_t(_mkgmtime(&t));
#else
    return std::chrono::system_clock::from_time_t(timegm(&t));
#endif
}

// Parse tier from fractal achievement name
static std::string ParseFractalTier(const std::string& name) {
    if (name.find("Tier 4") != std::string::npos) return "T4";
    if (name.find("Tier 3") != std::string::npos) return "T3";
    if (name.find("Tier 2") != std::string::npos) return "T2";
    if (name.find("Tier 1") != std::string::npos) return "T1";
    if (name.find("Recommended") != std::string::npos) return "Rec";
    return "";
}

// Monotonic milliseconds, used to schedule single-shot BUSY backoff retries.
static long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Send H&S achievement query for all currently tracked clears IDs
static void SendClearsAchQuery() {
    std::vector<uint32_t> allIds;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        for (const auto& e : g_DailyFractals) allIds.push_back(e.id);
        for (const auto& e : g_DailyBounties) allIds.push_back(e.id);
        for (const auto& e : g_WeeklyWings)   allIds.push_back(e.id);
        if (g_WeeklyStrikes.id > 0) allIds.push_back(g_WeeklyStrikes.id);
    }
    if (allIds.empty() || !APIDefs) return;

    std::string acctName = GetEffectiveAccountName();
    HoardQueryAchievementRequest req{};
    req.api_version = HOARD_API_VERSION;
    strncpy(req.requester, "Alter Ego", sizeof(req.requester) - 1);
    strncpy(req.response_event, EV_AE_CLEARS_ACH_RESPONSE, sizeof(req.response_event) - 1);
    if (!acctName.empty())
        strncpy(req.account_name, acctName.c_str(), sizeof(req.account_name) - 1);
    req.id_count = (uint32_t)std::min(allIds.size(), (size_t)200);
    for (uint32_t i = 0; i < req.id_count; i++) {
        req.ids[i] = allIds[i];
    }
    APIDefs->Events_Raise(EV_HOARD_QUERY_ACHIEVEMENT, &req);
}

// H&S achievement response handler for all clears
void OnClearsAchResponse(void* eventArgs) {
    if (!eventArgs) return;
    auto* resp = (HoardQueryAchievementResponse*)eventArgs;
    std::unique_ptr<HoardQueryAchievementResponse> respGuard(resp); // caller owns the response
    if (resp->api_version < 2) { return; }

    if (resp->status != HOARD_STATUS_OK) {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if (resp->status == HOARD_STATUS_BUSY) {
            g_ClearsStatusMsg = "Hoard & Seek busy, retrying shortly...";
            // Schedule a single backoff retry (Events_Raise must be on render thread)
            g_ClearsRetryAtMs = NowMs() + (resp->retry_after_ms ? (long long)resp->retry_after_ms : 1000LL);
        } else {
            if (resp->status == HOARD_STATUS_DENIED)
                g_ClearsStatusMsg = "H&S permission denied";
            else
                g_ClearsStatusMsg = "H&S error";
            g_ClearsFetching = false;
        }
        return;
    }

    // Build lookup maps from response
    struct AchData {
        bool done;
        int32_t current;
        int32_t max;
        std::set<uint32_t> bits;
    };
    std::unordered_map<uint32_t, AchData> achMap;
    for (uint32_t i = 0; i < resp->entry_count; i++) {
        auto& e = resp->entries[i];
        AchData ad;
        ad.done = e.done;
        ad.current = e.current;
        ad.max = e.max;
        for (uint32_t b = 0; b < e.bit_count && b < 64; b++) {
            ad.bits.insert(e.bits[b]);
        }
        achMap[e.id] = std::move(ad);
    }

    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);

        // Apply to all tracked entries
        auto applyTo = [&](ClearEntry& ce) {
            auto it = achMap.find(ce.id);
            if (it != achMap.end()) {
                ce.done = it->second.done;
                ce.current = it->second.current;
                ce.max = it->second.max;
                // API clears bits when achievement is fully completed;
                // detect this and mark all encounters done
                if (it->second.done && ce.max > 0 && ce.current >= ce.max) {
                    ce.bitDone.assign(ce.bitNames.size(), true);
                } else {
                    ce.bitDone.assign(ce.bitNames.size(), false);
                    for (uint32_t bitIdx : it->second.bits) {
                        if (bitIdx < ce.bitDone.size()) {
                            ce.bitDone[bitIdx] = true;
                        }
                    }
                }
            }
        };

        for (auto& e : g_DailyFractals)  applyTo(e);
        for (auto& e : g_DailyBounties)  applyTo(e);
        for (auto& e : g_WeeklyWings)    applyTo(e);
        applyTo(g_WeeklyStrikes);

        g_ClearsFetched = true;
        g_ClearsFetching = false;
        g_ClearsStatusMsg = "";
        g_ClearsLastSuccessTime = std::time(nullptr);
        g_ClearsSuccessUntil = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        g_LastClearsCompletionQuery = std::chrono::steady_clock::now();
    }

    // Persist to disk
    SaveClearsCache();
}

// =========================================================================
// Clears - Wizard's Vault fetch and response
// =========================================================================

static const char* VaultSectionName(uint8_t type) {
    static const char* names[] = {"daily", "weekly", "special"};
    return type < 3 ? names[type] : "unknown";
}

// Called only from the render thread. Never hold g_ClearsMutex around
// Events_Raise: H&S may also respond synchronously for cached data.
static void RequestVaultSection(uint8_t type, const std::string& account) {
    HoardQueryWizardsVaultRequest req{};
    req.api_version = HOARD_API_VERSION;
    strncpy(req.requester, "Alter Ego", sizeof(req.requester) - 1);
    strncpy(req.response_event, EV_AE_VAULT_RESPONSE, sizeof(req.response_event) - 1);
    req.type = type;
    if (!account.empty())
        strncpy(req.account_name, account.c_str(), sizeof(req.account_name) - 1);
    APIDefs->Events_Raise(EV_HOARD_QUERY_WIZARDSVAULT, &req);
}

// Start one asynchronous batch; each response is delivered separately later.
// Previous successful data and its timestamp remain valid while refreshing.
static void FetchVaultData() {
    if (!APIDefs || g_VaultFetching.load()) return;
    const std::string account = GetEffectiveAccountName();
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if (g_VaultFetching.load()) return;
        g_VaultFetching = true;
        g_VaultInitialAttempted = true;
        g_VaultLastAttemptFailed = false;
        g_VaultStatusMessage = "Fetching Vault objectives (0/3)...";
        g_VaultRequestStartMs = NowMs();
        g_VaultRequestAccount = account;
        g_VaultRetryAtMs = 0;
        for (int i = 0; i < 3; ++i) {
            g_VaultResponsesOK[i] = false;
            g_VaultPending[i] = {};
            g_VaultRetryNeeded[i] = false;
            g_VaultRetryAttempts[i] = 0;
        }
    }

    for (uint8_t type = 0; type < 3; ++type) {
        if (!g_VaultFetching.load()) break; // permission/terminal error already received
        RequestVaultSection(type, account);
    }

    // Best-effort season end date, independent of the three objective responses.
    HoardQueryApiRequest season{};
    season.api_version = HOARD_API_VERSION;
    strncpy(season.requester, "Alter Ego", sizeof(season.requester) - 1);
    strncpy(season.endpoint, "/v2/wizardsvault", sizeof(season.endpoint) - 1);
    strncpy(season.response_event, EV_AE_VAULT_SEASON_RESP, sizeof(season.response_event) - 1);
    if (!account.empty())
        strncpy(season.account_name, account.c_str(), sizeof(season.account_name) - 1);
    APIDefs->Events_Raise(EV_HOARD_QUERY_API, &season);
}

void OnVaultSeasonResponse(void* eventArgs) {
    if (!eventArgs) return;
    auto* resp = static_cast<HoardQueryApiResponse*>(eventArgs);
    std::unique_ptr<HoardQueryApiResponse> respGuard(resp); // caller owns the response
    if (resp->status == HOARD_STATUS_BUSY) {
        // Optional season metadata: don't restart otherwise successful objectives.
        return;
    }
    if (resp->status != HOARD_STATUS_OK || resp->json_length == 0) return;
    try {
        auto j = nlohmann::json::parse(resp->json);
        std::string endStr = j.value("end", "");
        if (!endStr.empty()) {
            auto tp = ParseISO8601(endStr);
            if (tp != std::chrono::system_clock::time_point{}) {
                {
                    std::lock_guard<std::mutex> lock(g_ClearsMutex);
                    g_VaultSeasonEnd = tp;
                }
                // Save the asynchronously delivered timestamp so a restart
                // can retain the countdown without another successful fetch.
                SaveVaultCache();
            }
        }
    } catch (...) {}
}

void OnVaultResponse(void* eventArgs) {
    if (!eventArgs) return;
    auto* resp = (HoardQueryWizardsVaultResponse*)eventArgs;
    std::unique_ptr<HoardQueryWizardsVaultResponse> respGuard(resp);
    if (resp->api_version < 2 || resp->type > 2) return;

    // Parse into local memory first; publish only under the shared mutex.
    VaultPeriod period;
    if (resp->status == HOARD_STATUS_OK) {
        period.meta_progress_current  = resp->meta_progress_current;
        period.meta_progress_complete = resp->meta_progress_complete;
        period.meta_reward_astral     = resp->meta_reward_astral;
        period.meta_reward_claimed    = resp->meta_reward_claimed != 0;
        for (uint32_t i = 0; i < resp->objective_count && i < 16; ++i) {
            const auto& o = resp->objectives[i];
            VaultObjective obj;
            obj.id = o.id;
            obj.title = o.title;
            obj.track = o.track;
            obj.acclaim = o.acclaim;
            obj.progress_current = o.progress_current;
            obj.progress_complete = o.progress_complete;
            obj.claimed = o.claimed != 0;
            period.objectives.push_back(std::move(obj));
        }
    }

    bool save = false;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if (!g_VaultFetching.load()) return;

        // Reject delayed responses for an account that is no longer being queried.
        const std::string echoedAccount(resp->account_name,
            strnlen(resp->account_name, sizeof(resp->account_name)));
        if (!g_VaultRequestAccount.empty() && !echoedAccount.empty() &&
            echoedAccount != g_VaultRequestAccount) return;

        const int type = resp->type;
        if (g_VaultResponsesOK[type]) return; // duplicate response
        if (resp->status == HOARD_STATUS_BUSY) {
            if (++g_VaultRetryAttempts[type] <= 3) {
                g_VaultRetryNeeded[type] = true;
                const long long next = NowMs() +
                    (resp->retry_after_ms ? (long long)resp->retry_after_ms : 1000LL);
                // Wait for the latest required backoff before re-sending pending sections.
                g_VaultRetryAtMs = std::max(g_VaultRetryAtMs, next);
                g_VaultStatusMessage = std::string("Hoard & Seek busy - retrying ") +
                    VaultSectionName((uint8_t)type) + " objectives...";
            } else {
                g_VaultFetching = false;
                g_VaultLastAttemptFailed = true;
                g_VaultRetryAtMs = 0;
                g_VaultStatusMessage = std::string("Refresh failed - H&S busy after retries (") +
                    VaultSectionName((uint8_t)type) + ")";
            }
        } else if (resp->status != HOARD_STATUS_OK) {
            g_VaultFetching = false;
            g_VaultLastAttemptFailed = true;
            g_VaultRetryAtMs = 0;
            if (resp->status == HOARD_STATUS_DENIED)
                g_VaultStatusMessage = std::string("Hoard & Seek permission denied (") +
                    VaultSectionName((uint8_t)type) + ")";
            else
                g_VaultStatusMessage = std::string("Vault ") + VaultSectionName((uint8_t)type) +
                    " request failed (status " + std::to_string((int)resp->status) + ")";
        } else {
            g_VaultPending[type] = std::move(period);
            g_VaultResponsesOK[type] = true;
            g_VaultRetryNeeded[type] = false;
            int received = 0;
            for (bool ok : g_VaultResponsesOK) if (ok) ++received;
            if (received == 3) {
                // Commit atomically when *all* three sections have responded.
                g_VaultDaily   = std::move(g_VaultPending[0]);
                g_VaultWeekly  = std::move(g_VaultPending[1]);
                g_VaultSpecial = std::move(g_VaultPending[2]);
                g_VaultLastSuccessTime = std::time(nullptr);
                g_VaultFetched = true;
                g_VaultFetching = false;
                g_VaultRetryAtMs = 0;
                g_VaultLastAttemptFailed = false;
                g_VaultStatusMessage = "Refresh complete";
                g_VaultSuccessUntil = std::chrono::steady_clock::now() +
                    std::chrono::seconds(4);
                save = true;
            } else if (g_VaultRetryAtMs == 0) {
                g_VaultStatusMessage = "Fetching Vault objectives (" +
                    std::to_string(received) + "/3)...";
            }
        }
    }
    // Persist after releasing the callback mutex; SaveVaultCache takes it again.
    if (save) SaveVaultCache();
}

// Helper: fetch a category's achievement IDs from public API
static std::vector<uint32_t> FetchCategoryIds(uint32_t catId) {
    std::string url = "https://api.guildwars2.com/v2/achievements/categories/" + std::to_string(catId);
    std::string json = Skinventory::HttpClient::Get(url);
    std::vector<uint32_t> ids;
    try {
        auto j = nlohmann::json::parse(json);
        if (j.contains("achievements") && j["achievements"].is_array()) {
            for (const auto& id : j["achievements"]) {
                ids.push_back(id.get<uint32_t>());
            }
        }
    } catch (...) {}
    return ids;
}

// Helper: fetch achievement details (name, bits) from public API
static std::vector<ClearEntry> FetchAchievementDetails(const std::vector<uint32_t>& ids) {
    std::vector<ClearEntry> entries;
    if (ids.empty()) return entries;

    std::string idStr;
    for (size_t i = 0; i < ids.size(); i++) {
        if (i > 0) idStr += ",";
        idStr += std::to_string(ids[i]);
    }
    std::string json = Skinventory::HttpClient::Get(
        "https://api.guildwars2.com/v2/achievements?ids=" + idStr);
    if (json.empty()) return entries;

    try {
        auto j = nlohmann::json::parse(json);
        if (j.is_array()) {
            for (const auto& ach : j) {
                ClearEntry ce;
                ce.id = ach.value("id", 0u);
                ce.name = ach.value("name", "");
                if (ce.id == 0 || ce.name.empty()) continue;

                // Parse bits (sub-objectives)
                if (ach.contains("bits") && ach["bits"].is_array()) {
                    for (const auto& bit : ach["bits"]) {
                        ce.bitNames.push_back(bit.value("text", ""));
                    }
                }

                // Parse tier count from tiers
                if (ach.contains("tiers") && ach["tiers"].is_array()) {
                    auto& tiers = ach["tiers"];
                    if (!tiers.empty()) {
                        ce.max = tiers.back().value("count", 0);
                    }
                }

                entries.push_back(std::move(ce));
            }
        }
    } catch (...) {}
    return entries;
}

// Fetch all clears data (runs on background thread)
static void FetchClears() {
    if (g_ClearsFetching) return;
    g_ClearsFetching = true;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        g_ClearsStatusMsg = "Fetching achievement data...";
        // Keep successfully cached data displayed during a refresh.
    }

    std::thread([]() {
        // Step 1: Fetch all category achievement IDs
        auto fractalIds = FetchCategoryIds(CAT_DAILY_FRACTALS);
        auto bountyIds  = FetchCategoryIds(CAT_DAILY_BOUNTIES);
        auto weeklyIds  = FetchCategoryIds(CAT_WEEKLY_RAIDS);

        if (fractalIds.empty() && bountyIds.empty() && weeklyIds.empty()) {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            g_ClearsStatusMsg = "Failed to fetch achievement categories";
            g_ClearsFetching = false;
            return;
        }

        // Step 2: Fetch all achievement details
        {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            g_ClearsStatusMsg = "Fetching achievement details...";
        }

        auto fractalEntries = FetchAchievementDetails(fractalIds);
        for (auto& e : fractalEntries) {
            e.tier = ParseFractalTier(e.name);
        }

        auto bountyEntries = FetchAchievementDetails(bountyIds);
        auto weeklyEntries = FetchAchievementDetails(weeklyIds);

        // Separate weekly entries into wings vs strikes meta
        ClearEntry strikesEntry{};
        std::vector<ClearEntry> wingEntries;
        for (auto& e : weeklyEntries) {
            if (e.id == ACH_WEEKLY_STRIKES) {
                strikesEntry = std::move(e);
            } else if (!e.bitNames.empty()) {
                // Per-wing achievements have bits for encounters
                wingEntries.push_back(std::move(e));
            }
        }

        // Sort wings into canonical W1-W8 order by achievement ID
        static const std::unordered_map<uint32_t, int> wingOrder = {
            {9128, 1}, // Spirit Vale
            {9147, 2}, // Salvation Pass
            {9182, 3}, // Stronghold of the Faithful
            {9144, 4}, // Bastion of the Penitent
            {9111, 5}, // Hall of Chains
            {9120, 6}, // Mythwright Gambit
            {9156, 7}, // Key of Ahdashim
            {9181, 8}, // Mount Balrior
        };
        std::sort(wingEntries.begin(), wingEntries.end(),
            [](const ClearEntry& a, const ClearEntry& b) {
                auto ai = wingOrder.find(a.id);
                auto bi = wingOrder.find(b.id);
                int ao = (ai != wingOrder.end()) ? ai->second : 99;
                int bo = (bi != wingOrder.end()) ? bi->second : 99;
                return ao < bo;
            });

        {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            g_DailyFractals = std::move(fractalEntries);
            g_DailyBounties = std::move(bountyEntries);
            g_WeeklyWings = std::move(wingEntries);
            g_WeeklyStrikes = std::move(strikesEntry);
            g_ClearsStatusMsg = "Querying completion...";
        }

        // Background fetch done — allow render thread to send H&S completion query
        g_ClearsFetching = false;
        g_ClearsQueryPending = true;
    }).detach();
}

// =========================================================================
// Clears - Persistence
// =========================================================================

static void SaveClearsCache() {
    std::string dir = AlterEgo::GW2API::GetDataDirectory();
    std::filesystem::create_directories(dir);
    std::string path = dir + "/clears_cache.json";

    nlohmann::json j;
    // Store fetch timestamp as seconds since epoch
    auto epochSecs = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    j["fetch_time"] = g_ClearsLastSuccessTime > 0 ? (int64_t)g_ClearsLastSuccessTime : (int64_t)epochSecs;

    auto serializeEntry = [](const ClearEntry& e) -> nlohmann::json {
        nlohmann::json ej;
        ej["id"] = e.id;
        ej["name"] = e.name;
        ej["tier"] = e.tier;
        ej["done"] = e.done;
        ej["current"] = e.current;
        ej["max"] = e.max;
        ej["bitNames"] = e.bitNames;
        std::vector<int> bits;
        for (bool b : e.bitDone) bits.push_back(b ? 1 : 0);
        ej["bitDone"] = bits;
        return ej;
    };

    // Snapshot data under lock to prevent concurrent modification
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);

        nlohmann::json fractals = nlohmann::json::array();
        for (const auto& e : g_DailyFractals) fractals.push_back(serializeEntry(e));
        j["dailyFractals"] = fractals;

        nlohmann::json bounties = nlohmann::json::array();
        for (const auto& e : g_DailyBounties) bounties.push_back(serializeEntry(e));
        j["dailyBounties"] = bounties;

        nlohmann::json wings = nlohmann::json::array();
        for (const auto& e : g_WeeklyWings) wings.push_back(serializeEntry(e));
        j["weeklyWings"] = wings;

        j["weeklyStrikes"] = serializeEntry(g_WeeklyStrikes);
    }

    std::ofstream file(path);
    if (file.is_open()) file << j.dump(2);
}

void LoadClearsCache() {
    std::string path = AlterEgo::GW2API::GetDataDirectory() + "/clears_cache.json";
    std::ifstream file(path);
    if (!file.is_open()) return;

    auto deserializeEntry = [](const nlohmann::json& ej) -> ClearEntry {
        ClearEntry e;
        e.id = ej.value("id", 0u);
        e.name = ej.value("name", "");
        e.tier = ej.value("tier", "");
        e.done = ej.value("done", false);
        e.current = ej.value("current", 0);
        e.max = ej.value("max", 0);
        if (ej.contains("bitNames") && ej["bitNames"].is_array()) {
            for (const auto& bn : ej["bitNames"]) e.bitNames.push_back(bn.get<std::string>());
        }
        if (ej.contains("bitDone") && ej["bitDone"].is_array()) {
            for (const auto& bd : ej["bitDone"]) e.bitDone.push_back(bd.get<int>() != 0);
        }
        return e;
    };

    try {
        auto j = nlohmann::json::parse(file);

        int64_t fetchEpoch = j.value("fetch_time", (int64_t)0);
        auto fetchTime = std::chrono::system_clock::from_time_t((time_t)fetchEpoch);
        g_ClearsLastSuccessTime = (std::time_t)fetchEpoch;

        // Check if a reset has occurred since the cached data was fetched
        // Add 60s buffer so we don't treat cache as stale before the API has updated
        auto now = std::chrono::system_clock::now();
        auto dailyReset = CalcLastDailyReset(now) + std::chrono::seconds(60);
        auto weeklyReset = CalcLastWeeklyReset(now) + std::chrono::seconds(60);
        bool dailyStale = fetchTime < dailyReset && now >= dailyReset;
        bool weeklyStale = fetchTime < weeklyReset && now >= weeklyReset;

        {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);

            // Clear vectors before loading to prevent accumulation
            g_DailyFractals.clear();
            g_DailyBounties.clear();
            g_WeeklyWings.clear();
            g_WeeklyStrikes = ClearEntry{};

            // Helper: deduplicate entries by ID
            auto dedup = [](std::vector<ClearEntry>& vec) {
                std::unordered_set<uint32_t> seen;
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                    [&seen](const ClearEntry& e) {
                        return !seen.insert(e.id).second;
                    }), vec.end());
            };

            // Load daily data only if no daily reset has occurred since fetch
            if (!dailyStale) {
                if (j.contains("dailyFractals") && j["dailyFractals"].is_array()) {
                    for (const auto& ej : j["dailyFractals"])
                        g_DailyFractals.push_back(deserializeEntry(ej));
                    dedup(g_DailyFractals);
                }
                if (j.contains("dailyBounties") && j["dailyBounties"].is_array()) {
                    for (const auto& ej : j["dailyBounties"])
                        g_DailyBounties.push_back(deserializeEntry(ej));
                    dedup(g_DailyBounties);
                }
            }

            // Load weekly data only if no weekly reset has occurred since fetch
            if (!weeklyStale) {
                if (j.contains("weeklyWings") && j["weeklyWings"].is_array()) {
                    for (const auto& ej : j["weeklyWings"])
                        g_WeeklyWings.push_back(deserializeEntry(ej));
                    dedup(g_WeeklyWings);
                    // Sort into canonical W1-W8 order
                    static const std::unordered_map<uint32_t, int> wingOrder = {
                        {9128, 1}, {9147, 2}, {9182, 3}, {9144, 4},
                        {9111, 5}, {9120, 6}, {9156, 7}, {9181, 8},
                    };
                    std::sort(g_WeeklyWings.begin(), g_WeeklyWings.end(),
                        [](const ClearEntry& a, const ClearEntry& b) {
                            auto ai = wingOrder.find(a.id);
                            auto bi = wingOrder.find(b.id);
                            int ao = (ai != wingOrder.end()) ? ai->second : 99;
                            int bo = (bi != wingOrder.end()) ? bi->second : 99;
                            return ao < bo;
                        });
                }
                if (j.contains("weeklyStrikes")) {
                    g_WeeklyStrikes = deserializeEntry(j["weeklyStrikes"]);
                }
            }

            // If we loaded any data, mark as fetched
            if (!g_DailyFractals.empty() || !g_WeeklyWings.empty()) {
                g_ClearsFetched = true;
                g_LastClearsCompletionQuery = std::chrono::steady_clock::now();
            }

            g_LastDailyReset = dailyReset;
            g_LastWeeklyReset = weeklyReset;
        }

        // If any data is stale, trigger a full re-fetch
        if (dailyStale || weeklyStale) {
            FetchClears();
        }
    } catch (...) {}
}

// =========================================================================
// Clears - Wizard's Vault persistence
// =========================================================================

static void SaveVaultCache() {
    std::string dir = AlterEgo::GW2API::GetDataDirectory();
    std::filesystem::create_directories(dir);
    std::string path = dir + "/vault_cache.json";

    auto serializePeriod = [](const VaultPeriod& p) -> nlohmann::json {
        nlohmann::json j;
        j["meta_progress_current"]  = p.meta_progress_current;
        j["meta_progress_complete"] = p.meta_progress_complete;
        j["meta_reward_astral"]     = p.meta_reward_astral;
        j["meta_reward_claimed"]    = p.meta_reward_claimed;
        nlohmann::json objs = nlohmann::json::array();
        for (const auto& o : p.objectives) {
            nlohmann::json oj;
            oj["id"]               = o.id;
            oj["title"]            = o.title;
            oj["track"]            = o.track;
            oj["acclaim"]          = o.acclaim;
            oj["progress_current"] = o.progress_current;
            oj["progress_complete"]= o.progress_complete;
            oj["claimed"]          = o.claimed;
            objs.push_back(std::move(oj));
        }
        j["objectives"] = std::move(objs);
        return j;
    };

    auto epochSecs = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    nlohmann::json j;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        j["fetch_time"] = (int64_t)g_VaultLastSuccessTime;
        j["daily"]      = serializePeriod(g_VaultDaily);
        j["weekly"]     = serializePeriod(g_VaultWeekly);
        j["special"]    = serializePeriod(g_VaultSpecial);
        j["season_end"] = std::chrono::duration_cast<std::chrono::seconds>(
            g_VaultSeasonEnd.time_since_epoch()).count();
    }

    std::ofstream file(path);
    if (file.is_open()) file << j.dump(2);
}

void LoadVaultCache() {
    std::string path = AlterEgo::GW2API::GetDataDirectory() + "/vault_cache.json";
    std::ifstream file(path);
    if (!file.is_open()) return;

    auto deserializePeriod = [](const nlohmann::json& j) -> VaultPeriod {
        VaultPeriod p;
        p.meta_progress_current  = j.value("meta_progress_current", 0);
        p.meta_progress_complete = j.value("meta_progress_complete", 0);
        p.meta_reward_astral     = j.value("meta_reward_astral", 0);
        p.meta_reward_claimed    = j.value("meta_reward_claimed", false);
        if (j.contains("objectives") && j["objectives"].is_array()) {
            for (const auto& oj : j["objectives"]) {
                VaultObjective o;
                o.id                = oj.value("id", 0u);
                o.title             = oj.value("title", "");
                o.track             = oj.value("track", "");
                o.acclaim           = oj.value("acclaim", 0);
                o.progress_current  = oj.value("progress_current", 0);
                o.progress_complete = oj.value("progress_complete", 0);
                o.claimed           = oj.value("claimed", false);
                p.objectives.push_back(std::move(o));
            }
        }
        return p;
    };

    try {
        auto j = nlohmann::json::parse(file);
        int64_t fetchEpoch = j.value("fetch_time", (int64_t)0);
        auto fetchTime = std::chrono::system_clock::from_time_t((time_t)fetchEpoch);
        g_VaultLastSuccessTime = (std::time_t)fetchEpoch;

        auto now         = std::chrono::system_clock::now();
        auto dailyReset  = CalcLastDailyReset(now)  + std::chrono::seconds(60);
        auto weeklyReset = CalcLastWeeklyReset(now) + std::chrono::seconds(60);
        bool dailyStale  = fetchTime < dailyReset  && now >= dailyReset;
        bool weeklyStale = fetchTime < weeklyReset && now >= weeklyReset;

        {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            if (!dailyStale  && j.contains("daily"))   g_VaultDaily   = deserializePeriod(j["daily"]);
            if (!weeklyStale && j.contains("weekly"))  g_VaultWeekly  = deserializePeriod(j["weekly"]);
            if (j.contains("special"))                 g_VaultSpecial = deserializePeriod(j["special"]);
            int64_t seasonEndEpoch = j.value("season_end", (int64_t)0);
            if (seasonEndEpoch > 0)
                g_VaultSeasonEnd = std::chrono::system_clock::from_time_t((time_t)seasonEndEpoch);

            if (!g_VaultDaily.objectives.empty() || !g_VaultWeekly.objectives.empty())
                g_VaultFetched = true;
        }

        // Stale data will be re-fetched on first render of the vault tab
        if (dailyStale || weeklyStale)
            g_VaultFetched = false;

    } catch (...) {}
}

// =========================================================================
// Clears - UI rendering helpers
// =========================================================================

// Shared current 1–100 scale map also powers daily and next-reset tooltips.
static const char* FractalNameForScale(int scale) {
    return FractalScales::NameForScale(scale);
}

// Helper: strip fractal achievement name to short form
static std::string ShortenFractalName(const std::string& name) {
    // "Daily Recommended Fractal—Swampland" → "Swampland"
    auto pos = name.find("Fractal");
    if (pos != std::string::npos && pos + 7 < name.size()) {
        size_t start = pos + 7;
        while (start < name.size() && (name[start] == ' ' || (unsigned char)name[start] == 0xe2)) {
            if ((unsigned char)name[start] == 0xe2 && start + 2 < name.size())
                start += 3;
            else
                start++;
        }
        std::string tail = name.substr(start);
        // "Scale 31" → "Swampland (31)"
        if (tail.rfind("Scale ", 0) == 0) {
            int scale = atoi(tail.c_str() + 6);
            if (const char* mapName = FractalNameForScale(scale)) {
                char buf[96];
                snprintf(buf, sizeof(buf), "%s (%d)", mapName, scale);
                return std::string(buf);
            }
        }
        return tail;
    }
    // "Daily Tier 4 Swampland" → "Swampland"
    auto pos2 = name.find("Tier");
    if (pos2 != std::string::npos) {
        size_t afterTier = name.find(' ', pos2 + 5);
        if (afterTier != std::string::npos && afterTier + 1 < name.size())
            return name.substr(afterTier + 1);
    }
    return name;
}

// Map verbose API encounter text to readable short names
static std::string ShortenEncounterName(const std::string& text) {
    static const std::unordered_map<std::string, std::string> nameMap = {
        // W1 — Spirit Vale
        {"Defeat the Vale Guardian.",                                      "Vale Guardian"},
        {"Traverse the Spirit Woods.",                                     "Spirit Woods"},
        {"Destroy Gorseval.",                                              "Gorseval"},
        {"Cull the bandits to lure out the bandit leader.",                ""},
        {"Defeat Sabetha the Saboteur.",                                   "Sabetha"},
        // W2 — Salvation Pass
        {"Defeat Slothasor.",                                              "Sloth"},
        {"Protect the caged prisoners.",                                   "Trio"},
        {"Cull the bandits in the ruins.",                                 ""},
        {"Defeat Inquisitor Matthias Gabrel.",                             "Matthias"},
        // W3 — Stronghold of the Faithful
        {"Escort Glenna to the stronghold's courtyard.",                   "Escort"},
        {"Defeat McLeod and breach the stronghold.",                       ""},
        {"Destroy the Keep Construct.",                                    "Keep Construct"},
        {"Traverse the Twisted Castle.",                                   "Twisted Castle"},
        {"Defeat Xera.",                                                   "Xera"},
        // W4 — Bastion of the Penitent
        {"Defeat Cairn the Indomitable.",                                  "Cairn"},
        {"Defeat the Mursaat Overseer.",                                   "Mursaat"},
        {"Defeat Samarog.",                                                "Samarog"},
        {"Free the prisoner from his bonds.",                              "Deimos"},
        // W5 — Hall of Chains
        {"Defeat the Soulless Horror.",                                    "Soulless Horror"},
        {"Traverse the River of Souls.",                                   "River"},
        {"Restore the Statue of Ice.",                                     "Broken King"},
        {"Restore the Statue of Death and Resurrection.",                  "Eater"},
        {"Restore the Statue of Darkness.",                                "Eyes"},
        {"Defeat Dhuum.",                                                  "Dhuum"},
        // W6 — Mythwright Gambit
        {"Destroy the conjured amalgamate.",                               "Conjured Amalgamate"},
        {"Make your way through the sorting and appraisal rooms.",         "Sorting"},
        {"Defeat the twin largos.",                                        "Twin Largos"},
        {"Clear a path through Qadim's minions to the Mythwright Cauldron.", "Qadim's Minions"},
        {"Defeat Qadim.",                                                  "Qadim"},
        // W7 — Key of Ahdashim
        {"Get Glenna and the key to Ahdashim's front gate.",               "Gate"},
        {"Defeat Cardinal Adina.",                                         "Adina"},
        {"Defeat Cardinal Sabir.",                                         "Sabir"},
        {"Defeat Qadim the Peerless.",                                     "Qadim the Peerless"},
        // W8 — Mount Balrior
        {"Cleanse the camp of titanspawn.",                                "Camp"},
        {"Defeat the sentient conduit.",                                   "Conduit"},
        {"Defeat the blighted beast and his empowering allies.",           ""},
        {"Defeat Decima.",                                                 "Decima"},
        {"Defeat Greer.",                                                  "Greer"},
        {"Defeat Ura.",                                                    "Ura"},
    };
    auto it = nameMap.find(text);
    if (it != nameMap.end()) return it->second;
    return text; // fallback to original
}

// Helper: render a done/not-done/unknown indicator
static void RenderClearStatus(bool fetched, bool done, const char* label) {
    if (!fetched)
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[?] %s", label);
    else if (done)
        ImGui::TextColored(ImVec4(0.56f, 0.59f, 0.56f, 1.0f), "[x] %s", label);
    else
        ImGui::TextColored(ImVec4(0.94f, 0.92f, 0.87f, 1.0f), "%s", label);
}

// ============= Banner-row helpers (strikes / raid wings) =============

#include "EmbeddedBanners.h"

// Look up the embedded banner JPEG for a key (e.g. "wing:9128", "strike:Cold War")
// and return a loaded Nexus texture, or nullptr if none.
static Texture_t* GetBannerTexture(const std::string& key) {
    unsigned int len = 0;
    const unsigned char* data = AlterEgo::EmbeddedBanners::FindBanner(key, len);
    if (!data || len == 0) return nullptr;

    std::string texId = "AE_BNR_" + key;
    Texture_t* tex = APIDefs->Textures_Get(texId.c_str());
    if (!tex || !tex->Resource) {
        tex = APIDefs->Textures_GetOrCreateFromMemory(
            texId.c_str(), const_cast<unsigned char*>(data), (uint64_t)len);
    }
    return (tex && tex->Resource) ? tex : nullptr;
}

struct ThemeGrad { ImU32 a, b; };

static ThemeGrad WingTheme(uint32_t wingId) {
    switch (wingId) {
        case 9128: return { IM_COL32(26, 58, 94, 255),  IM_COL32(74, 139, 201, 255) }; // W1 Spirit Vale (blue)
        case 9147: return { IM_COL32(74, 26, 26, 255),  IM_COL32(160, 60, 50, 255)  }; // W2 Salvation Pass (red)
        case 9182: return { IM_COL32(20, 60, 32, 255),  IM_COL32(80, 150, 80, 255)  }; // W3 Stronghold (green)
        case 9144: return { IM_COL32(58, 26, 78, 255),  IM_COL32(107, 42, 143, 255) }; // W4 Bastion (purple)
        case 9111: return { IM_COL32(28, 24, 38, 255),  IM_COL32(90, 80, 130, 255)  }; // W5 Hall of Chains (dark slate)
        case 9120: return { IM_COL32(78, 38, 14, 255),  IM_COL32(200, 110, 30, 255) }; // W6 Mythwright (gold/fire)
        case 9156: return { IM_COL32(74, 42, 20, 255),  IM_COL32(200, 154, 74, 255) }; // W7 Key of Ahdashim (tan)
        case 9181: return { IM_COL32(56, 28, 16, 255),  IM_COL32(150, 70, 40, 255)  }; // W8 Mount Balrior
    }
    return { IM_COL32(28, 26, 32, 255), IM_COL32(60, 56, 70, 255) };
}

static ThemeGrad FractalTierTheme(const std::string& tier) {
    if (tier == "T1")  return { IM_COL32(30, 70, 36, 255),   IM_COL32(110, 200, 122, 255) }; // green
    if (tier == "T2")  return { IM_COL32(26, 60, 110, 255),  IM_COL32(110, 192, 224, 255) }; // blue
    if (tier == "T3")  return { IM_COL32(80, 50, 14, 255),   IM_COL32(216, 161, 74, 255)  }; // orange
    if (tier == "T4")  return { IM_COL32(70, 22, 22, 255),   IM_COL32(217, 106, 106, 255) }; // red
    if (tier == "Rec") return { IM_COL32(58, 32, 78, 255),   IM_COL32(160, 110, 200, 255) }; // purple/gold-ish
    return { IM_COL32(40, 36, 28, 255), IM_COL32(120, 100, 60, 255) };
}

// Map a bounty boss name to its raid wing id (for banner background + theme).
// Returns 0 if unknown.
static const char* BountyWingFullName(uint32_t wingId) {
    switch (wingId) {
        case 9128: return "Spirit Vale";
        case 9147: return "Salvation Pass";
        case 9182: return "Stronghold of the Faithful";
        case 9144: return "Bastion of the Penitent";
        case 9111: return "Hall of Chains";
        case 9120: return "Mythwright Gambit";
        case 9156: return "The Key of Ahdashim";
        case 9181: return "Mount Balrior";
    }
    return "";
}

static const char* BountyWingShortName(uint32_t wingId) {
    switch (wingId) {
        case 9128: return "W1";
        case 9147: return "W2";
        case 9182: return "W3";
        case 9144: return "W4";
        case 9111: return "W5";
        case 9120: return "W6";
        case 9156: return "W7";
        case 9181: return "W8";
    }
    return "";
}

// Map a bounty name to a strike banner key (e.g. "strike:Cold War").
// Returns empty if no match. Handles bounty names that may be boss names
// rather than the strike's mission name.
static std::string BountyToStrikeName(const std::string& name) {
    static const std::unordered_map<std::string, std::string> map = {
        // Strike mission name (canonical) → banner key
        {"Shiverpeaks Pass",                          "Shiverpeaks Pass"},
        {"Voice of the Fallen and Claw of the Fallen","Voice of the Fallen and Claw of the Fallen"},
        {"Fraenir of Jormag",                         "Fraenir of Jormag"},
        {"Boneskinner",                               "Boneskinner"},
        {"Whisper of Jormag",                         "Whisper of Jormag"},
        {"Forging Steel",                             "Forging Steel"},
        {"Cold War",                                  "Cold War"},
        {"Aetherblade Hideout",                       "Aetherblade Hideout"},
        {"Xunlai Jade Junkyard",                      "Xunlai Jade Junkyard"},
        {"Kaineng Overlook",                          "Kaineng Overlook"},
        {"Harvest Temple",                            "Harvest Temple"},
        {"Old Lion's Court",                          "Old Lion's Court"},
        {"Cosmic Observatory",                        "Cosmic Observatory"},
        {"Temple of Febe",                            "Temple of Febe"},
        {"Guardian's Glade",                          "Guardian's Glade"},
        // Common boss-name aliases that resolve to the same banner
        {"Voice and Claw",                            "Voice of the Fallen and Claw of the Fallen"},
        {"Voice and Claw of the Fallen",              "Voice of the Fallen and Claw of the Fallen"},
        {"Fraenir",                                   "Fraenir of Jormag"},
        {"Whisper",                                   "Whisper of Jormag"},
        {"Mai Trin",                                  "Aetherblade Hideout"},
        {"Ankka",                                     "Xunlai Jade Junkyard"},
        {"Minister Li",                               "Kaineng Overlook"},
        {"The Dragonvoid",                            "Harvest Temple"},
        {"Dragonvoid",                                "Harvest Temple"},
        {"Dagda",                                     "Cosmic Observatory"},
        {"Cerus",                                     "Temple of Febe"},
        {"Eparch",                                    "Guardian's Glade"},
    };
    auto it = map.find(name);
    return (it != map.end()) ? it->second : std::string();
}

static uint32_t BountyBossToWing(const std::string& bossName) {
    static const std::unordered_map<std::string, uint32_t> map = {
        // W1 Spirit Vale
        {"Vale Guardian", 9128}, {"Gorseval", 9128}, {"Gorseval the Multifarious", 9128},
        {"Sabetha", 9128}, {"Sabetha the Saboteur", 9128},
        // W2 Salvation Pass
        {"Slothasor", 9147}, {"Bandit Trio", 9147}, {"Matthias", 9147}, {"Matthias Gabrel", 9147},
        // W3 Stronghold of the Faithful
        {"Keep Construct", 9182}, {"Xera", 9182},
        // W4 Bastion of the Penitent
        {"Cairn", 9144}, {"Cairn the Indomitable", 9144},
        {"Mursaat Overseer", 9144}, {"Samarog", 9144}, {"Deimos", 9144},
        // W5 Hall of Chains
        {"Soulless Horror", 9111}, {"Dhuum", 9111},
        // W6 Mythwright Gambit
        {"Conjured Amalgamate", 9120}, {"Twin Largos", 9120}, {"Largos", 9120}, {"Qadim", 9120},
        // W7 Key of Ahdashim
        {"Cardinal Adina", 9156}, {"Adina", 9156},
        {"Cardinal Sabir", 9156}, {"Sabir", 9156},
        {"Qadim the Peerless", 9156},
        // W8 Mount Balrior
        {"Greer", 9181}, {"Decima", 9181}, {"Ura", 9181},
    };
    auto it = map.find(bossName);
    return (it != map.end()) ? it->second : 0;
}

// Match the in-game raid picker without changing the source achievement-bit order.
// Forging Steel has no corresponding objective in the current 14-bit list.
static const char* StrikePickerCategory(const std::string& name) {
    const std::string key = ClearsRotation::Normalize(name);
    if (key == "oldlionscourt") return "Core Game";
    if (key == "shiverpeakspass" || key == "voiceofthefallenandclawofthefallen" ||
        key == "fraenirofjormag" || key == "boneskinner" ||
        key == "whisperofjormag" || key == "coldwar") return "Icebrood Saga";
    if (key == "aetherbladehideout" || key == "xunlaijadejunkyard" ||
        key == "kainengoverlook" || key == "harvesttemple") return "End of Dragons";
    if (key == "cosmicobservatory" || key == "templeoffebe") return "Secrets of the Obscure";
    if (key == "guardiansglade" || key == "kela") return "Janthir Wilds";
    if (key == "nexusofeternity" || key == "vloxx") return "Visions of Eternity";
    return "Strike Mission";
}

static int StrikePickerOrder(const std::string& name) {
    static const std::unordered_map<std::string, int> ranks = {
        {"oldlionscourt", 10},
        {"shiverpeakspass", 20}, {"voiceofthefallenandclawofthefallen", 21},
        {"fraenirofjormag", 22}, {"boneskinner", 23},
        {"whisperofjormag", 24}, {"coldwar", 26},
        {"aetherbladehideout", 30}, {"xunlaijadejunkyard", 31},
        {"kainengoverlook", 32}, {"harvesttemple", 33},
        {"cosmicobservatory", 40}, {"templeoffebe", 41},
        {"guardiansglade", 50}, {"kela", 50}, {"nexusofeternity", 60}
    };
    const auto it = ranks.find(ClearsRotation::Normalize(name));
    return it != ranks.end() ? it->second : 100;
}

static const char* WingExpansion(uint32_t wingId) {
    switch (wingId) {
        case 9128: case 9147: case 9182: case 9144: return "Heart of Thorns";
        case 9111: case 9120: case 9156: return "Path of Fire";
        case 9181: return "Janthir Wilds";
    }
    return "Raid";
}

static bool IsSecondaryRaidEncounter(const std::string& displayName) {
    static const std::unordered_set<std::string> secondary = {
        "Spirit Woods", "Trio", "Escort", "Twisted Castle",
        "River", "Broken King", "Eater", "Eyes",
        "Sorting", "Qadim's Minions", "Gate", "Camp", "Conduit"
    };
    return secondary.count(displayName) != 0;
}

static ThemeGrad StrikeTheme(const std::string& name) {
    // Hash → pick from a curated palette of 8 themes so colours stay stable per strike
    static const ThemeGrad palette[] = {
        { IM_COL32(26, 78, 110, 255), IM_COL32(90, 203, 230, 255) }, // ice blue
        { IM_COL32(110, 58, 20, 255), IM_COL32(255, 155, 58, 255) }, // ember
        { IM_COL32(78, 26, 26, 255),  IM_COL32(200, 74, 94, 255)  }, // crimson
        { IM_COL32(74, 74, 110, 255), IM_COL32(26, 26, 62, 255)   }, // void
        { IM_COL32(110, 90, 30, 255), IM_COL32(228, 196, 122, 255)}, // gold dust
        { IM_COL32(20, 70, 36, 255),  IM_COL32(100, 180, 100, 255)}, // verdant
        { IM_COL32(60, 30, 80, 255),  IM_COL32(160, 80, 200, 255) }, // amethyst
        { IM_COL32(20, 30, 50, 255),  IM_COL32(80, 110, 180, 255) }, // dusk
    };
    size_t h = std::hash<std::string>{}(name) % (sizeof(palette) / sizeof(palette[0]));
    return palette[h];
}

// Render a single banner row (strike / generic clear). If widthOverride > 0,
// use that width instead of the full available width (used for multi-column layouts).
// If bannerKey resolves to an embedded loading-screen JPEG, that image is used
// as the background; otherwise the gradient theme is used.
// Unified one-line clear cell. Background artwork made the trackers visually
// inconsistent and obscured labels; raid dates and completion remain visible.
static ImU32 BountyDateColor(const std::string& date) {
    // GW2 uses UTC resets, not local midnight.
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string today = ClearsRotation::BountyDateLabel(
        ClearsRotation::FloorDay(now) * ClearsRotation::kDay);
    return date == today ? AlterEgo::CompletionTheme::kBountyToday
                         : AlterEgo::CompletionTheme::kBountyFuture;
}

static void RenderClearBanner(const std::string& title, const std::string& subtitle,
                              bool fetched, bool done, ThemeGrad theme,
                              const char* tooltip = nullptr,
                              float widthOverride = 0.0f,
                              const std::string& bannerKey = "",
                              const std::string& bountyDate = "",
                              const char* fullTooltipTitle = nullptr) {
    (void)theme;
    (void)bannerKey;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = widthOverride > 0.0f ? widthOverride : ImGui::GetContentRegionAvail().x;
    const float height = 24.0f;
    const ImVec2 end(pos.x + width, pos.y + height);
    const bool complete = fetched && done;
    // Shared completion state: completed muted/green; available bright/borderless.
    AlterEgo::CompletionTheme::DrawRow(dl, pos, end, complete, 2.0f);
    AlterEgo::CompletionTheme::DrawCompleteMark(
        dl, ImVec2(pos.x + 9.0f, pos.y + height * 0.5f), complete);
    if (!fetched) {
        dl->AddText(ImVec2(pos.x + 5.0f, pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
                    IM_COL32(160, 155, 140, 255), "?");
    }

    // Check gutter is fixed even when unfinished, as in raid/fractal chips.
    const ImVec2 titlePos(pos.x + 19.0f,
                          pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f);
    dl->PushClipRect(ImVec2(titlePos.x, pos.y), end, true);
    dl->AddText(titlePos, AlterEgo::CompletionTheme::Text(complete), title.c_str());
    if (!bountyDate.empty()) {
        const std::string suffix = " - " + bountyDate;
        dl->AddText(ImVec2(titlePos.x + ImGui::CalcTextSize(title.c_str()).x, titlePos.y),
            BountyDateColor(bountyDate), suffix.c_str());
    }
    dl->PopClipRect();

    ImGui::InvisibleButton("##clear", ImVec2(width, height));
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(fullTooltipTitle && *fullTooltipTitle ?
            fullTooltipTitle : title.c_str());
        if (!subtitle.empty()) ImGui::TextDisabled("%s", subtitle.c_str());
        if (tooltip && *tooltip) ImGui::TextWrapped("%s", tooltip);
        if (!bountyDate.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.42f, 1.0f),
                               "Daily bounty: %s", bountyDate.c_str());
        ImGui::EndTooltip();
    }
    ImGui::Dummy(ImVec2(0, 1.0f));
}

struct WingEncChip { std::string label; std::string fullName; bool done = false; bool secondary = false; std::string bountyDate; };

// Shared drawing only: tier and wing layouts still own their hit areas,
// hover details, positions, and completion rules.
static void DrawEncounterChip(ImDrawList* dl, const WingEncChip& chip,
                              ImVec2 chmin, ImVec2 chmax,
                              float dotRadius, bool showBountyDate) {
    AlterEgo::CompletionTheme::DrawRow(dl, chmin, chmax, chip.done, 3.0f, chip.secondary);
    const float dotX = chmin.x + 7.0f;
    const float dotY = chmin.y + (chmax.y - chmin.y) * 0.5f;
    AlterEgo::CompletionTheme::DrawCompleteMark(
        dl, ImVec2(dotX, dotY), chip.done, dotRadius);
    const float labelX = dotX + dotRadius + 5.0f;
    const ImVec2 labelSize = ImGui::CalcTextSize(chip.label.c_str());
    const float labelY = chmin.y + (chmax.y - chmin.y - labelSize.y) * 0.5f;
    dl->PushClipRect(chmin, chmax, true);
    dl->AddText(ImVec2(labelX, labelY),
        AlterEgo::CompletionTheme::Text(chip.done, chip.secondary), chip.label.c_str());
    if (showBountyDate && !chip.bountyDate.empty()) {
        const std::string date = " - " + chip.bountyDate;
        dl->AddText(ImVec2(labelX + labelSize.x, labelY),
            BountyDateColor(chip.bountyDate), date.c_str());
    }
    dl->PopClipRect();
}


// All four cells are one-line entries, arranged as two columns by two rows
// at normal addon widths. A single-column fallback prevents overlap in small
// windows. The last cell shows the T1/T2/T3 Recommended SCALE numbers.
struct DailyFractalCard {
    std::string title;
    std::array<const ClearEntry*, 4> entries{};
    std::array<std::string, 4> labels{};
    bool recommended = false;
};

static int RecommendedFractalScale(const ClearEntry& entry) {
    // API names use "Scale 11"; ShortenFractalName can produce
    // "Deepstone (11)". Accept either form, but never fabricate a scale.
    const std::size_t pos = entry.name.find("Scale ");
    if (pos != std::string::npos) {
        const int scale = std::atoi(entry.name.c_str() + pos + 6);
        if (scale >= 1 && scale <= 100) return scale;
    }
    const std::string shortName = ShortenFractalName(entry.name);
    const std::size_t open = shortName.rfind('(');
    if (open != std::string::npos && !shortName.empty() && shortName.back() == ')') {
        const int scale = std::atoi(shortName.c_str() + open + 1);
        if (scale >= 1 && scale <= 100) return scale;
    }
    return 0;
}

static bool FractalCardComplete(const DailyFractalCard& card, bool fetched) {
    if (!fetched) return false;
    const int count = card.recommended ? 3 : 4;
    for (int tier = 0; tier < count; ++tier)
        if (!card.entries[tier] || !card.entries[tier]->done) return false;
    return true;
}

static void RenderDailyFractalCards(const std::vector<DailyFractalCard>& cards,
                                    bool fetched, bool hideComplete) {
    if (cards.empty()) return;
    bool anythingVisible = false;
    for (const auto& card : cards)
        if (!hideComplete || !FractalCardComplete(card, fetched))
            anythingVisible = true;
    if (!anythingVisible) {
        ImGui::TextDisabled("  All daily fractals and recommendations completed.");
        return;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float pad = 7.0f, xgap = 6.0f, ygap = 5.0f, cardHeight = 27.0f;
    const float chipWidth = 39.0f, chipGap = 3.0f, chipHeight = 20.0f;
    const float chipsWidth = 4.0f * chipWidth + 3.0f * chipGap;
    // Two columns need enough room for the longest daily name plus chips.
    const int columns = width >= 680.0f ? 2 : 1;
    const int rows = (static_cast<int>(cards.size()) + columns - 1) / columns;
    const float cardWidth = (width - 2.0f * pad - (columns - 1) * xgap) / columns;
    const float height = 2.0f * pad + rows * cardHeight + (rows - 1) * ygap;
    AlterEgo::CompletionTheme::DrawGroup(
        draw, origin, ImVec2(origin.x + width, origin.y + height));

    for (size_t i = 0; i < cards.size(); ++i) {
        const auto& card = cards[i];
        const bool complete = FractalCardComplete(card, fetched);
        // Hide Complete does not reorder other cards. An empty slot remains
        // where a completed card was, so the two-column grid never jumps.
        if (hideComplete && complete) continue;
        const int col = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        const ImVec2 cmin(origin.x + pad + col * (cardWidth + xgap),
                          origin.y + pad + row * (cardHeight + ygap));
        const ImVec2 cmax(cmin.x + cardWidth, cmin.y + cardHeight);
        AlterEgo::CompletionTheme::DrawRow(draw, cmin, cmax, complete);

        const float chipStart = cmax.x - 6.0f - chipsWidth;
        const float centerY = (cmin.y + cmax.y) * 0.5f;
        const float lineH = ImGui::GetTextLineHeight();
        // Entire card title dims when all constituent tiers are complete.
        draw->PushClipRect(ImVec2(cmin.x + 7.0f, cmin.y),
                           ImVec2(chipStart - 6.0f, cmax.y), true);
        draw->AddText(ImVec2(cmin.x + 7.0f, centerY - lineH * 0.5f),
                      AlterEgo::CompletionTheme::Text(complete),
                      card.title.c_str());
        draw->PopClipRect();

        const int slots = card.recommended ? 3 : 4;
        ImGui::PushID(static_cast<int>(i));
        // The card title is a hover target as well as its individual chips.
        // Keep this hit box entirely to the left of the first tier chip.
        ImGui::SetCursorScreenPos(ImVec2(cmin.x + 5.0f, cmin.y));
        const float titleWidth = chipStart - cmin.x - 12.0f;
        if (titleWidth > 1.0f) {
            ImGui::InvisibleButton("##fractalCardTitle",
                ImVec2(titleWidth, cardHeight));
            if (ImGui::IsItemHovered()) {
                std::string info;
                if (!card.recommended) {
                    info = FractalScales::DailyTooltip(card.title);
                } else {
                    info = "Recommended";
                    for (int tier = 0; tier < 3; ++tier) {
                        const ClearEntry* rec = card.entries[tier];
                        if (!rec) continue;
                        const int scale = RecommendedFractalScale(*rec);
                        const char* canonical = FractalScales::NameForScale(scale);
                        const std::string name = canonical ? canonical :
                            ShortenFractalName(rec->name);
                        info += "\nT" + std::to_string(tier + 1) +
                            " (" + (scale > 0 ? std::to_string(scale) : "?") +
                            ") " + name;
                    }
                }
                ImGui::SetTooltip("%s", info.c_str());
            }
        }
        for (int tier = 0; tier < slots; ++tier) {
            const ClearEntry* entry = card.entries[tier];
            const bool done = fetched && entry && entry->done;
            const float x = chipStart + tier * (chipWidth + chipGap);
            const ImVec2 slotMin(x, centerY - chipHeight * 0.5f);
            const ImVec2 slotMax(x + chipWidth, centerY + chipHeight * 0.5f);
            draw->AddRectFilled(slotMin, slotMax,
                done ? AlterEgo::CompletionTheme::kCompletedIndicator
                     : AlterEgo::CompletionTheme::kAvailableIndicator, 2.0f);
            // Each slot reserves exactly the same space for its check,
            // whether it is completed or not. Labels never shift.
            AlterEgo::CompletionTheme::DrawCompleteMark(
                draw, ImVec2(x + 7.0f, centerY), done, 3.0f);
            const std::string& text = card.labels[tier];
            const ImU32 ink = !entry ? AlterEgo::CompletionTheme::kUnknownText
                : !fetched ? AlterEgo::CompletionTheme::kUnknownText
                : AlterEgo::CompletionTheme::Text(done);
            draw->PushClipRect(slotMin, slotMax, true);
            draw->AddText(ImVec2(x + 15.0f, centerY - lineH * 0.5f),
                          ink, text.empty() ? "?" : text.c_str());
            draw->PopClipRect();

            ImGui::SetCursorScreenPos(slotMin);
            ImGui::PushID(tier);
            ImGui::InvisibleButton("##fractalStatus", ImVec2(chipWidth, chipHeight));
            if (ImGui::IsItemHovered()) {
                // Daily: the map name and every qualifying tier scale.
                // Recommended: only the exact numbered scale grants credit.
                std::string info;
                if (card.recommended) {
                    const int scale = entry ? RecommendedFractalScale(*entry) : 0;
                    const char* canonical = FractalScales::NameForScale(scale);
                    const std::string name = canonical ? canonical :
                        (entry ? ShortenFractalName(entry->name) : "Unknown");
                    info = "Recommended\nT" + std::to_string(tier + 1) +
                        " (" + (scale > 0 ? std::to_string(scale) : "?") +
                        ") " + name;
                } else {
                    info = FractalScales::DailyTooltip(card.title);
                    info += "\nSelected: " +
                        FractalScales::TierScales(card.title, tier + 1);
                }
                info += !entry ? "\nNo objective data" :
                    !fetched ? "\nProgress unknown" :
                    done ? "\nCompleted" : "\nRemaining";
                ImGui::SetTooltip("%s", info.c_str());
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height + 2.0f));
    ImGui::Dummy(ImVec2(0, 1.0f));
}

// Compact single-row tier banner with inline chips — for recommended fractals.
// Layout: [accent] [TIER] [title]   [chip][chip][chip]   X/Y
static void RenderTierRow(const char* tierLabel,
                          const std::string& title,
                          int doneCount, int totalCount,
                          bool fetched,
                          ThemeGrad theme,
                          ImU32 accentColor,
                          const std::vector<WingEncChip>& chips) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cmin = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = 34.0f;
    ImVec2 cmax(cmin.x + w, cmin.y + h);

    // Outer group is dark; inner REC cells use the shared item backgrounds.
    const bool fullyDone = fetched && totalCount > 0 && doneCount == totalCount;
    AlterEgo::CompletionTheme::DrawGroup(dl, cmin, cmax);
    (void)theme;

    // Neutral group heading; no category-colored stripe.
    (void)accentColor;

    float padX = 10.0f;
    float midY = cmin.y + h * 0.5f;

    // Tier label (left, tier-coloured)
    float tierX = cmin.x + padX;
    ImVec2 tierSz = ImGui::CalcTextSize(tierLabel);
    dl->AddText(ImVec2(tierX, midY - tierSz.y * 0.5f),
        AlterEgo::CompletionTheme::kAvailableText, tierLabel);

    // Title
    float titleX = tierX + tierSz.x + 10.0f;
    ImU32 titleCol = AlterEgo::CompletionTheme::Text(fullyDone);
    ImVec2 titleSz = ImGui::CalcTextSize(title.c_str());
    dl->AddText(ImVec2(titleX, midY - titleSz.y * 0.5f), titleCol, title.c_str());

    // Progress (right)
    char prog[32];
    snprintf(prog, sizeof(prog), "%d / %d", doneCount, totalCount);
    ImVec2 progSz = ImGui::CalcTextSize(prog);
    ImU32 progCol = doneCount > 0 && doneCount < totalCount
        ? AlterEgo::CompletionTheme::kInProgressGold
        : AlterEgo::CompletionTheme::kMetaText;
    float progX = cmax.x - padX - progSz.x;
    dl->AddText(ImVec2(progX, midY - progSz.y * 0.5f), progCol, prog);

    // Hit-test the whole row first (so chips can override with their own)
    ImGui::InvisibleButton("##tierrow", ImVec2(w, h));

    // Chips area between title and progress
    float chipsStartX = titleX + titleSz.x + 12.0f;
    float chipsEndX = progX - 10.0f;
    float chipsW = chipsEndX - chipsStartX;
    float chipH = 18.0f;
    float chipY = midY - chipH * 0.5f;
    if (!chips.empty() && chipsW > 40.0f) {
        float gap = 4.0f;
        float chipW = (chipsW - gap * (chips.size() - 1)) / (float)chips.size();
        for (size_t i = 0; i < chips.size(); i++) {
            ImVec2 chmin(chipsStartX + (chipW + gap) * i, chipY);
            ImVec2 chmax(chmin.x + chipW, chmin.y + chipH);

            DrawEncounterChip(dl, chips[i], chmin, chmax,
                              /*dotRadius*/ 3.5f, /*showBountyDate*/ false);

            // Per-chip hover (overlay on top of the row hit area)
            ImGui::SetCursorScreenPos(chmin);
            ImGui::PushID((int)i);
            ImGui::InvisibleButton("##chip", ImVec2(chipW, chipH));
            if (ImGui::IsItemHovered() && !chips[i].fullName.empty()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(chips[i].fullName.c_str());
                ImGui::EndTooltip();
                // Tooltip supplies hover feedback without creating a false completion border.
            }
            ImGui::PopID();
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(cmin.x, cmin.y + h));
    ImGui::Dummy(ImVec2(0, 2));
}


// ============================================================
// Weekly raid wing rotations (Emboldened / Call of the Mists)
// ============================================================
//
// Both rotations advance every Monday at 08:30 UTC through the 8 wings
// in release order. Call of the Mists sits one slot ahead of Emboldened.
// Anchor: Monday 2026-05-18 08:30 UTC → Salvation Pass is emboldened
//                                       Stronghold has Call of the Mists.
// Verified against wiki.guildwars2.com/wiki/Emboldened and Call_of_the_Mists.

static constexpr int64_t kRotationAnchorUnix = 1779093000; // 2026-05-18 08:30 UTC
static constexpr int     kRotationAnchorEmboldenedIndex = 1; // W2 Salvation Pass
static constexpr int64_t kWeekSeconds = 7 * 24 * 3600;

// Wing IDs in rotation order (W1..W8).
static uint32_t WingIdByRotationIndex(int idx) {
    static const uint32_t kWingByIdx[8] = {
        9128, // W1 Spirit Vale
        9147, // W2 Salvation Pass
        9182, // W3 Stronghold of the Faithful
        9144, // W4 Bastion of the Penitent
        9111, // W5 Hall of Chains
        9120, // W6 Mythwright Gambit
        9156, // W7 The Key of Ahdashim
        9181, // W8 Mount Balrior
    };
    if (idx < 0 || idx >= 8) return 0;
    return kWingByIdx[idx];
}

static int CurrentEmboldenedRotationIndex() {
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t diff = now - kRotationAnchorUnix;
    int64_t weeks = diff / kWeekSeconds;
    if (diff < 0 && (diff % kWeekSeconds) != 0) weeks -= 1; // floor toward -inf
    int idx = (int)(((weeks + kRotationAnchorEmboldenedIndex) % 8 + 8) % 8);
    return idx;
}

static uint32_t CurrentEmboldenedWingId() {
    return WingIdByRotationIndex(CurrentEmboldenedRotationIndex());
}

static uint32_t CurrentCallOfTheMistsWingId() {
    return WingIdByRotationIndex((CurrentEmboldenedRotationIndex() + 1) % 8);
}


static void RenderWingBanner(const std::string& wingShortName,    // e.g. "W4"
                             const std::string& wingFullName,     // e.g. "Bastion of the Penitent"
                             int doneCount, int totalCount,
                             bool fetched,
                             ThemeGrad theme,
                             const std::vector<WingEncChip>& chips,
                             const std::string& bannerKey = "",
                             bool isEmboldened = false,
                             bool isCallOfTheMists = false) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cmin = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = 52.0f;
    ImVec2 cmax(cmin.x + w, cmin.y + h);

    // A dark outer group separates wings, with brighter/dimmer shared
    // item surfaces inside. Do not color the entire group when completed.
    const bool fullyDone = fetched && totalCount > 0 && doneCount == totalCount;
    AlterEgo::CompletionTheme::DrawGroup(dl, cmin, cmax);
    (void)theme; // Neutral wing heading, like the other groups.

    // Top row text
    float padX = 10.0f;
    float topY = cmin.y + 6.0f;
    // Roman
    dl->AddText(ImVec2(cmin.x + padX, topY),
        AlterEgo::CompletionTheme::Text(fullyDone), wingShortName.c_str());
    // Name
    ImVec2 wsSz = ImGui::CalcTextSize(wingShortName.c_str());
    dl->AddText(ImVec2(cmin.x + padX + wsSz.x + 10.0f, topY),
        AlterEgo::CompletionTheme::Text(fullyDone),
        wingFullName.c_str());
    ImVec2 nameSz = ImGui::CalcTextSize(wingFullName.c_str());
    // Progress fraction (right-aligned)
    char prog[32];
    snprintf(prog, sizeof(prog), "%d / %d", doneCount, totalCount);
    ImVec2 pSz = ImGui::CalcTextSize(prog);
    ImU32 progCol = fullyDone ? AlterEgo::CompletionTheme::kCompleteText
        : doneCount > 0 && doneCount < totalCount
            ? AlterEgo::CompletionTheme::kInProgressGold
            : AlterEgo::CompletionTheme::kAvailableText;
    dl->AddText(ImVec2(cmax.x - padX - pSz.x, topY), progCol, prog);

    // Weekly modifiers: unobtrusive, icon-only, with the full explanation
    // on hover. These are independent of the wing's completion state.
    if (isEmboldened || isCallOfTheMists) {
        const float iconSz = 19.0f;
        const float iconY = topY + (ImGui::GetTextLineHeight() - iconSz) * 0.5f;
        float iconX = cmin.x + padX + wsSz.x + 10.0f + nameSz.x + 10.0f;
        const float rightLimit = cmax.x - padX - pSz.x - 10.0f;
        auto drawModifierIcon = [&](const char* key, const char* tooltip) {
            if (iconX + iconSz > rightLimit) return;
            const ImVec2 min(iconX, iconY);
            const ImVec2 max(iconX + iconSz, iconY + iconSz);
            Texture_t* tex = Skinventory::WikiImage::GetCurrencyIcon(key);
            if (tex && tex->Resource) {
                dl->AddImage(tex->Resource, min, max);
            } else {
                dl->AddText(min, AlterEgo::CompletionTheme::kUnknownText, "?");
            }
            const ImVec2 previous = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(min);
            ImGui::PushID(key);
            ImGui::InvisibleButton("##weeklyModifier", ImVec2(iconSz, iconSz));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
            ImGui::PopID();
            ImGui::SetCursorScreenPos(previous);
            iconX += iconSz + 6.0f;
        };
        if (isCallOfTheMists)
            drawModifierIcon("Call_of_the_Mists",
                "Call of the Mists - boss gold and XP are doubled this week.");
        if (isEmboldened)
            drawModifierIcon("Emboldened",
                "Emboldened Mode - stacking buff for easier clears this week.");
    }

    // Encounter chips along the bottom
    float chipsY = cmin.y + h - 22.0f;
    float chipsX = cmin.x + padX;
    float chipsW = w - padX * 2;
    float chipH = 18.0f;
    if (!chips.empty()) {
        float gap = 4.0f;
        float chipW = (chipsW - gap * (chips.size() - 1)) / (float)chips.size();
        for (size_t i = 0; i < chips.size(); i++) {
            ImVec2 chmin(chipsX + (chipW + gap) * i, chipsY);
            ImVec2 chmax(chmin.x + chipW, chmin.y + chipH);

            DrawEncounterChip(dl, chips[i], chmin, chmax,
                              /*dotRadius*/ 4.0f, /*showBountyDate*/ true);

            // Hover area + tooltip with full encounter name
            ImGui::SetCursorScreenPos(chmin);
            ImGui::PushID((int)i);
            ImGui::InvisibleButton("##chip", ImVec2(chipW, chipH));
            if (ImGui::IsItemHovered() && !chips[i].fullName.empty()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(chips[i].fullName.c_str());
                if (!chips[i].bountyDate.empty())
                    ImGui::Text("Daily raid bounty: %s", chips[i].bountyDate.c_str());
                ImGui::EndTooltip();
                // Brighten border on hover
                // Tooltip supplies hover feedback without a completion-like border.
            }
            ImGui::PopID();
        }
    }

    // Final cursor advance for the whole banner
    ImGui::SetCursorScreenPos(ImVec2(cmin.x, cmax.y + 1.0f));
}

// Compact wide-window raid layout: W1 [+modifiers] | encounter chips | 4/4.
// Preserve original two-row wing banners as the narrow-window fallback.
static void RenderCompactWingBanner(const std::string& wingShortName,
                                   const std::string& wingFullName,
                                   int doneCount, int totalCount, bool fetched,
                                   const std::vector<WingEncChip>& chips,
                                   bool isEmboldened, bool isCallOfTheMists) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 29.0f;
    const ImVec2 max(min.x + width, min.y + height);
    const bool fullyDone = fetched && totalCount > 0 && doneCount == totalCount;
    AlterEgo::CompletionTheme::DrawGroup(dl, min, max);
    const float textY = min.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    const float leftX = min.x + 10.0f;
    dl->AddText(ImVec2(leftX, textY),
        AlterEgo::CompletionTheme::Text(fullyDone), wingShortName.c_str());

    // Hovering W1, W2, etc. gives the full wing name and completion summary.
    ImGui::SetCursorScreenPos(ImVec2(leftX - 3.0f, min.y + 2.0f));
    ImGui::InvisibleButton("##wingLabel", ImVec2(31.0f, height - 4.0f));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\n%d / %d encounters completed",
                          wingFullName.c_str(), doneCount, totalCount);

    // Keep both weekly modifier icons beside the abbreviated wing label.
    float iconX = min.x + 40.0f;
    auto modifier = [&](bool enabled, const char* key, const char* tip) {
        if (!enabled) return;
        const float iconSize = 16.0f;
        const ImVec2 iMin(iconX, min.y + (height - iconSize) * 0.5f);
        const ImVec2 iMax(iMin.x + iconSize, iMin.y + iconSize);
        Texture_t* tex = Skinventory::WikiImage::GetCurrencyIcon(key);
        if (tex && tex->Resource) dl->AddImage(tex->Resource, iMin, iMax);
        else dl->AddText(iMin, AlterEgo::CompletionTheme::kUnknownText, "?");
        ImGui::SetCursorScreenPos(iMin);
        ImGui::PushID(key);
        ImGui::InvisibleButton("##wingModifier", ImVec2(iconSize, iconSize));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::PopID();
        iconX += iconSize + 3.0f;
    };
    modifier(isCallOfTheMists, "Call_of_the_Mists",
             "Call of the Mists - boss gold and XP are doubled this week.");
    modifier(isEmboldened, "Emboldened",
             "Emboldened Mode - stacking buff for easier clears this week.");

    char fraction[32];
    snprintf(fraction, sizeof(fraction), "%d / %d", doneCount, totalCount);
    const float fractionWidth = ImGui::CalcTextSize(fraction).x;
    const ImU32 counterColor = fullyDone ? AlterEgo::CompletionTheme::kCompleteText
        : doneCount > 0 ? AlterEgo::CompletionTheme::kInProgressGold
        : AlterEgo::CompletionTheme::kAvailableText;
    dl->AddText(ImVec2(max.x - fractionWidth - 9.0f, textY),
                counterColor, fraction);

    // Reserve an icon gutter on every wing so encounter columns align.
    const float chipStart = min.x + 85.0f;
    const float chipEnd = max.x - fractionWidth - 20.0f;
    const float chipWTotal = ImMax(0.0f, chipEnd - chipStart);
    const float gap = 4.0f;
    if (!chips.empty() && chipWTotal > gap * (chips.size() - 1)) {
        const float chipW = (chipWTotal - gap * (chips.size() - 1)) / chips.size();
        const float chipY = min.y + (height - 19.0f) * 0.5f;
        for (size_t i = 0; i < chips.size(); ++i) {
            const ImVec2 cMin(chipStart + i * (chipW + gap), chipY);
            const ImVec2 cMax(cMin.x + chipW, chipY + 19.0f);
            DrawEncounterChip(dl, chips[i], cMin, cMax, 3.5f, true);
            ImGui::SetCursorScreenPos(cMin);
            ImGui::PushID((int)i);
            ImGui::InvisibleButton("##chip", ImVec2(chipW, 19.0f));
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(chips[i].fullName.c_str());
                if (!chips[i].bountyDate.empty())
                    ImGui::Text("Daily raid bounty: %s", chips[i].bountyDate.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, max.y + 2.0f));
}

// Get wing short name "W1"-"W8" from wing ID, fallback empty string.
static const char* WingShortName(uint32_t wingId) {
    return BountyWingShortName(wingId);
}

// Get clean wing name with "Weekly " stripped
static std::string CleanWingName(const std::string& raw) {
    if (raw.rfind("Weekly ", 0) == 0) return raw.substr(7);
    return raw;
}

// =========================================================================
// Clears - Vault tab rendering
// =========================================================================

// Per-track accent colour for objective rows
static ImU32 VaultTrackColor(const std::string& track) {
    if (track == "PvP") return IM_COL32(204, 84, 84, 255);   // red
    if (track == "WvW") return IM_COL32(84, 135, 204, 255);  // blue
    return IM_COL32(84, 186, 84, 255);                       // PvE green
}

// Slim two-column row for a single vault objective.
static void RenderVaultObjectiveRow(const VaultObjective& obj, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cmin = ImGui::GetCursorScreenPos();
    float h = 26.0f;
    ImVec2 cmax(cmin.x + width, cmin.y + h);

    // Claimed rewards are complete, including in older snapshots where
    // progress is absent. Completed entries stay green like Clears.
    const bool claimed = obj.claimed;
    const bool done = claimed || obj.isDone();
    AlterEgo::CompletionTheme::DrawRow(dl, cmin, cmax, done, 2.0f);
    const float midY = cmin.y + h * 0.5f;
    const float dotX = cmin.x + 10.0f;
    AlterEgo::CompletionTheme::DrawCompleteMark(
        dl, ImVec2(dotX, midY), done);

    float padX = 8.0f;
    // Fixed gutter for the completion check: labels do not jump.
    float tagX = cmin.x + 20.0f;
    if (!obj.track.empty()) {
        ImVec2 tagSz = ImGui::CalcTextSize(obj.track.c_str());
        dl->AddText(ImVec2(tagX, midY - tagSz.y * 0.5f),
                    done ? IM_COL32(122, 146, 125, 230) : VaultTrackColor(obj.track),
                    obj.track.c_str());
        tagX += tagSz.x + 8.0f;
    }

    // Acclaim pill (right-aligned)
    char pill[16];
    snprintf(pill, sizeof(pill), "+%d", obj.acclaim);
    ImVec2 pillSz = ImGui::CalcTextSize(pill);
    float pillPadX = 5.0f, pillPadY = 2.0f;
    float pillW = pillSz.x + pillPadX * 2;
    float pillH = pillSz.y + pillPadY * 2;
    ImVec2 pillMin(cmax.x - padX - pillW, midY - pillH * 0.5f);
    ImVec2 pillMax(pillMin.x + pillW, pillMin.y + pillH);

    ImU32 pillBg, pillBorder, pillText;
    if (claimed) {
        pillBg     = IM_COL32(35, 37, 41, 190);
        pillBorder = IM_COL32(89, 93, 96, 140);
        pillText   = AlterEgo::CompletionTheme::kCompleteText;
    } else if (done) {
        // earned but unclaimed — the actionable state, brightest
        pillBg     = IM_COL32(80, 60, 18, 230);
        pillBorder = IM_COL32(232, 196, 122, 255);
        pillText   = IM_COL32(245, 220, 150, 255);
    } else {
        pillBg     = IM_COL32(32, 28, 18, 200);
        pillBorder = IM_COL32(140, 116, 60, 200);
        pillText   = IM_COL32(220, 192, 122, 230);
    }
    dl->AddRectFilled(pillMin, pillMax, pillBg, 3.0f);
    dl->AddRect(pillMin, pillMax, pillBorder, 3.0f, 0, 1.0f);
    dl->AddText(ImVec2(pillMin.x + pillPadX, pillMin.y + pillPadY), pillText, pill);

    // A dimmed title means already complete; unfinished tasks remain bright.
    // The gold unclaimed AA pill above is intentionally still prominent.
    const ImU32 titleCol = AlterEgo::CompletionTheme::Text(done);

    ImVec2 titleSz = ImGui::CalcTextSize(obj.title.c_str());
    float titleX = tagX;
    float titleEndX = pillMin.x - 8.0f;

    // If there's room and the obj is in-progress, show "N/M" right before the pill
    char progBuf[24] = {0};
    bool showProg = !done && obj.progress_complete > 1;
    float progW = 0.0f;
    if (showProg) {
        snprintf(progBuf, sizeof(progBuf), "%d/%d", obj.progress_current, obj.progress_complete);
        progW = ImGui::CalcTextSize(progBuf).x + 6.0f;
    }
    float titleClipEnd = titleEndX - progW;

    dl->PushClipRect(ImVec2(titleX, cmin.y), ImVec2(titleClipEnd, cmax.y), true);
    dl->AddText(ImVec2(titleX, midY - titleSz.y * 0.5f), titleCol, obj.title.c_str());
    dl->PopClipRect();

    if (showProg) {
        ImVec2 pSz = ImGui::CalcTextSize(progBuf);
        dl->AddText(ImVec2(titleEndX - pSz.x, midY - pSz.y * 0.5f),
            IM_COL32(150, 142, 120, 230), progBuf);
    }

    // Hit test (whole row) — tooltip with full title if it was truncated
    ImGui::InvisibleButton("##vobj", ImVec2(width, h));
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(obj.title.c_str());
        if (claimed)
            ImGui::TextColored(ImVec4(0.55f, 0.88f, 0.58f, 1.0f), "Completed - reward claimed");
        else if (done)
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "Completed - reward unclaimed");
        else
            ImGui::TextDisabled("Not completed");
        ImGui::EndTooltip();
        // Hover never changes an unfinished row into a bordered cell.
    }
}

static void RenderVaultPeriodSection(int sectionIndex, const char* title,
                                     const char* resetLabel, ImVec4 color,
                                     const VaultPeriod& period)
{
    // One heading for title, reset, claimed count and Astral Acclaim.
    // Count progress by completed objectives, independent of claiming rewards.
    int claimedCount = 0, completedCount = 0, totalAA = 0, earnedAA = 0;
    for (const auto& o : period.objectives) {
        if (o.claimed) { ++claimedCount; earnedAA += o.acclaim; }
        if (o.claimed || o.isDone()) ++completedCount;
        totalAA += o.acclaim;
    }
    totalAA += period.meta_reward_astral;
    if (period.meta_reward_claimed) earnedAA += period.meta_reward_astral;
    char suffix[64], summary[96];
    snprintf(suffix, sizeof(suffix), "(%s)", resetLabel);
    snprintf(summary, sizeof(summary), "%d/%zu claimed   %d/%d AA",
             claimedCount, period.objectives.size(), earnedAA, totalAA);
    // A Vault period is fully claimed only after both its objectives and
    // all available Astral Acclaim (including its meta reward) are claimed.
    const bool fullyClaimed = !period.objectives.empty() &&
        claimedCount == static_cast<int>(period.objectives.size()) &&
        earnedAA >= totalAA;
    if (!RenderVaultSectionToggle(sectionIndex, title, color, suffix,
                                  summary, fullyClaimed))
        return;
    if (period.objectives.empty()) {
        ImGui::TextDisabled("  No data");
        return;
    }

    ImGui::Indent(4.0f);
    // Gold while partial; no redundant bar for 0% or 100%.
    if (completedCount > 0 &&
        completedCount < static_cast<int>(period.objectives.size())) {
        AlterEgo::CompletionTheme::RenderPartialProgress(
            completedCount, static_cast<int>(period.objectives.size()), 4.0f);
        ImGui::Spacing();
    }

    // Hide claimed objectives only; completed but unclaimed rewards remain visible.
    size_t visibleCount = 0;
    for (const auto& obj : period.objectives)
        if (!g_VaultHideComplete || !obj.claimed) ++visibleCount;
    if (visibleCount == 0) {
        ImGui::TextDisabled("All objectives claimed.");
        ImGui::Unindent(4.0f);
        return;
    }

    // Two-column grid of objective rows
    float availW = ImGui::GetContentRegionAvail().x - 4.0f;
    float gap = 6.0f;
    float halfW = (availW - gap) * 0.5f;

    size_t shown = 0;
    for (size_t i = 0; i < period.objectives.size(); i++) {
        const auto& obj = period.objectives[i];
        if (g_VaultHideComplete && obj.claimed) continue;
        ImGui::PushID((int)i);
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        RenderVaultObjectiveRow(obj, halfW);
        ImGui::PopID();

        ++shown;
        if (shown % 2 == 1 && shown < visibleCount) {
            ImGui::SetCursorScreenPos(ImVec2(startPos.x + halfW + gap, startPos.y));
        } else {
            ImGui::Dummy(ImVec2(0, 2));
        }
    }

    ImGui::Unindent(4.0f);
}

static void RenderVaultTab(const std::string& dailyResetStr, const std::string& weeklyResetStr)
{
    // Handle retry/timeout on the render thread; callbacks never call Events_Raise.
    std::vector<uint8_t> dueRetryTypes;
    std::string retryAccount;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if (g_VaultFetching.load()) {
            const long long now = NowMs();
            if (g_VaultRequestStartMs != 0 &&
                now - g_VaultRequestStartMs >= VAULT_TIMEOUT_MS) {
                g_VaultFetching = false;
                g_VaultLastAttemptFailed = true;
                g_VaultRetryAtMs = 0;
                std::string missing;
                for (int i = 0; i < 3; ++i) {
                    if (!g_VaultResponsesOK[i]) {
                        if (!missing.empty()) missing += ", ";
                        missing += VaultSectionName((uint8_t)i);
                    }
                }
                g_VaultStatusMessage = "Vault refresh timed out waiting for: " + missing;
            } else if (g_VaultRetryAtMs != 0 && now >= g_VaultRetryAtMs) {
                for (uint8_t i = 0; i < 3; ++i) {
                    if (g_VaultRetryNeeded[i] && !g_VaultResponsesOK[i]) {
                        g_VaultRetryNeeded[i] = false;
                        dueRetryTypes.push_back(i);
                    }
                }
                retryAccount = g_VaultRequestAccount;
                g_VaultRetryAtMs = 0;
                g_VaultStatusMessage = "Retrying Vault objectives...";
            }
        }
    }
    for (uint8_t type : dueRetryTypes) {
        if (!g_VaultFetching.load()) break;
        RequestVaultSection(type, retryAccount);
    }

    // Account switches invalidate the old snapshot and cancel pending retries.
    if (g_VaultNeedRequery) {
        g_VaultNeedRequery = false;
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        g_VaultFetching = false;
        g_VaultRetryAtMs = 0;
        g_VaultDaily = {}; g_VaultWeekly = {}; g_VaultSpecial = {};
        g_VaultFetched = false;
        g_VaultLastSuccessTime = 0;
        g_VaultInitialAttempted = false;
        g_VaultLastAttemptFailed = false;
        g_VaultStatusMessage.clear();
    }

    if (!g_VaultFetched.load() && !g_VaultFetching.load() && !g_VaultInitialAttempted)
        FetchVaultData();

    const bool fetching = g_VaultFetching.load();
    if (fetching) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
    if (ImGui::SmallButton("Refresh##vault") && !fetching) FetchVaultData();
    if (fetching) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
    ImGui::SameLine();
    LoadClearUiSettings();
    const char* filterLabel = g_VaultHideComplete
        ? "Show Complete##vault" : "Hide Complete##vault";
    if (ImGui::SmallButton(filterLabel)) {
        g_VaultHideComplete = !g_VaultHideComplete;
        SaveClearUiSettings();
    }

    // Snapshot the response-thread state before giving it to ImGui.
    std::time_t updated;
    bool showingData, failed, pending;
    std::string status;
    std::chrono::steady_clock::time_point successUntil;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        updated = g_VaultLastSuccessTime;
        showingData = g_VaultFetched.load();
        failed = g_VaultLastAttemptFailed;
        pending = g_VaultFetching.load();
        status = g_VaultStatusMessage;
        successUntil = g_VaultSuccessUntil;
    }
    AlterEgo::RefreshUI::Timestamp(updated, "Vault");
    if (pending) {
        AlterEgo::RefreshUI::Status(AlterEgo::RefreshUI::State::Working,
            status.empty() ? "Fetching Vault objectives..." : status);
    } else if (failed) {
        AlterEgo::RefreshUI::Status(AlterEgo::RefreshUI::State::Error, status);
    } else if (std::chrono::steady_clock::now() < successUntil) {
        AlterEgo::RefreshUI::Status(AlterEgo::RefreshUI::State::Success, "Refresh complete");
    } else {
        AlterEgo::RefreshUI::Status(AlterEgo::RefreshUI::State::Ready,
            showingData ? "Ready" : "No Vault data available - click Refresh");
    }

    ImGui::Separator();
    ImGui::BeginChild("##VaultContentScroll", ImVec2(0, 0), false);

    std::lock_guard<std::mutex> lock(g_ClearsMutex);

    RenderVaultPeriodSection(0, "Daily Objectives", dailyResetStr.c_str(),
        ImVec4(0.61f, 0.43f, 0.92f, 1.0f), g_VaultDaily);

    ImGui::Spacing();
    ImGui::Separator();

    RenderVaultPeriodSection(1, "Weekly Objectives", weeklyResetStr.c_str(),
        ImVec4(0.91f, 0.78f, 0.29f, 1.0f), g_VaultWeekly);

    if (!g_VaultSpecial.objectives.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        std::string seasonLabel = "seasonal";
        if (g_VaultSeasonEnd != std::chrono::system_clock::time_point{}) {
            auto sNow = std::chrono::system_clock::now();
            if (g_VaultSeasonEnd > sNow) {
                auto rem = std::chrono::duration_cast<std::chrono::seconds>(g_VaultSeasonEnd - sNow).count();
                // Count calendar days remaining, consistent with the in-game
                // Wizard's Vault display; never show 0 while time remains.
                const int days = static_cast<int>((rem + 86399) / 86400);
                seasonLabel = std::to_string(days) +
                    (days == 1 ? " day remaining" : " days remaining");
            } else {
                seasonLabel = "ended";
            }
        }
        RenderVaultPeriodSection(2, "Special Objectives", seasonLabel.c_str(),
            ImVec4(0.9f, 0.70f, 0.30f, 1.0f), g_VaultSpecial);
    }
    ImGui::EndChild();
}

// =========================================================================
// Clears - Main render function
// =========================================================================

// UI-only preferences. Completion fetching remains active while collapsed.
static bool g_ClearOpen[4] = {true, true, true, true};
static bool g_ClearUiLoaded = false;
static std::string ClearUiSettingsPath() {
    return AlterEgo::GW2API::GetDataDirectory() + "/clears_ui.json";
}
static void LoadClearUiSettings() {
    if (g_ClearUiLoaded) return;
    g_ClearUiLoaded = true;
    try {
        std::ifstream f(ClearUiSettingsPath());
        if (!f.good()) return;
        nlohmann::json j; f >> j;
        if (!j.is_object() || !j.contains("expanded") || !j["expanded"].is_array()) return;
        const auto& a = j["expanded"];
        for (size_t i = 0; i < 4 && i < a.size(); ++i)
            if (a[i].is_boolean()) g_ClearOpen[i] = a[i].get<bool>();
        if (j.contains("vault_expanded") && j["vault_expanded"].is_array()) {
            const auto& v = j["vault_expanded"];
            for (size_t i = 0; i < 3 && i < v.size(); ++i)
                if (v[i].is_boolean()) g_VaultOpen[i] = v[i].get<bool>();
        }
        g_VaultHideComplete = j.value("hide_vault_complete", false);
        g_ClearsHideComplete = j.value("hide_clears_complete", false);
    } catch (...) {}
}
static void SaveClearUiSettings() {
    try {
        auto path = ClearUiSettingsPath();
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        nlohmann::json j;
        j["expanded"] = {g_ClearOpen[0], g_ClearOpen[1], g_ClearOpen[2], g_ClearOpen[3]};
        j["vault_expanded"] = {g_VaultOpen[0], g_VaultOpen[1], g_VaultOpen[2]};
        j["hide_vault_complete"] = g_VaultHideComplete;
        j["hide_clears_complete"] = g_ClearsHideComplete;
        std::ofstream f(path, std::ios::trunc);
        if (f.good()) f << j.dump(2);
    } catch (...) {}
}
static bool RenderVaultSectionToggle(int index, const char* title,
                                     ImVec4 color, const char* suffix,
                                     const char* summary,
                                     bool summaryComplete) {
    (void)color;
    LoadClearUiSettings();
    ImGui::PushID("VaultSection");
    ImGui::PushID(index);
    if (AlterEgo::CompletionTheme::CenteredSectionToggle(
            g_VaultOpen[index], title)) {
        g_VaultOpen[index] = !g_VaultOpen[index];
        SaveClearUiSettings();
    }
    AlterEgo::CompletionTheme::SectionHeading(title, suffix, summary,
        AlterEgo::CompletionTheme::Text(summaryComplete));
    // The heading is a single invisible layout item, so the season's exact
    // end timestamp can be shown on hover without adding visible clutter.
    if (index == 2 &&
        g_VaultSeasonEnd != std::chrono::system_clock::time_point{} &&
        ImGui::IsItemHovered()) {
        const std::time_t tt = std::chrono::system_clock::to_time_t(g_VaultSeasonEnd);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &tt);
#else
        gmtime_r(&tt, &utc);
#endif
        char when[64]{};
        if (std::strftime(when, sizeof(when), "%a %b %d, %Y at %H:%M UTC", &utc))
            ImGui::SetTooltip("Wizard's Vault season ends: %s", when);
    }
    ImGui::PopID();
    ImGui::PopID();
    return g_VaultOpen[index];
}
static bool RenderClearSectionToggle(int index, const char* title,
                                     ImVec4 color, const char* suffix,
                                     const char* summary = nullptr,
                                     bool summaryComplete = false) {
    (void)color;
    LoadClearUiSettings();
    ImGui::PushID(index);
    if (AlterEgo::CompletionTheme::CenteredSectionToggle(
            g_ClearOpen[index], title)) {
        g_ClearOpen[index] = !g_ClearOpen[index];
        SaveClearUiSettings();
    }
    AlterEgo::CompletionTheme::SectionHeading(title, suffix, summary,
        summary ? AlterEgo::CompletionTheme::Text(summaryComplete)
                : AlterEgo::CompletionTheme::kMetaText);
    ImGui::PopID();
    return g_ClearOpen[index];
}
static std::string UpcomingBountyLabel(const std::string& encounter,
                                       const std::vector<ClearEntry>& daily,
                                       int64_t utcNow, int64_t weeklyEndUtc,
                                       bool completionKnown) {
    if (!completionKnown) return "";
    const std::string boss = ClearsRotation::Normalize(encounter);
    bool todayOffered = false, todayCompleted = false;
    const int set = ClearsRotation::BountySet(utcNow);
    for (int j = 0; j < 4; ++j)
        if (ClearsRotation::Normalize(ClearsRotation::BountyName(set,j)) == boss)
            todayOffered = true;
    if (todayOffered) {
        for (const auto& entry : daily) {
            if (ClearsRotation::Normalize(entry.name) == boss && entry.done)
                todayCompleted = true;
        }
    }
    return ClearsRotation::NextBountyDate(
        encounter, utcNow, weeklyEndUtc, todayCompleted);
}

static void RenderClearsTabContent(const std::string& dailyResetStr, const std::string& weeklyResetStr)
{
    const bool fetching = g_ClearsFetching || g_ClearsQueryPending.load() ||
        g_ClearsRetryAtMs.load() != 0;
    if (fetching) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    if (ImGui::SmallButton("Refresh##clears") && !fetching) FetchClears();
    if (fetching) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
    ImGui::SameLine();
    LoadClearUiSettings();
    const char* filterLabel = g_ClearsHideComplete
        ? "Show Complete##clears" : "Hide Complete##clears";
    if (ImGui::SmallButton(filterLabel)) {
        g_ClearsHideComplete = !g_ClearsHideComplete;
        SaveClearUiSettings();
    }
    AlterEgo::RefreshUI::Timestamp(g_ClearsLastSuccessTime, "Clears");
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if (fetching)
            AlterEgo::RefreshUI::Status(AlterEgo::RefreshUI::State::Working,
                g_ClearsStatusMsg.empty() ? "Querying completion..." : g_ClearsStatusMsg);
        else if (!g_ClearsStatusMsg.empty())
            AlterEgo::RefreshUI::Status(AlterEgo::RefreshUI::State::Error, g_ClearsStatusMsg);
        else if (std::chrono::steady_clock::now() < g_ClearsSuccessUntil)
            AlterEgo::RefreshUI::Status(AlterEgo::RefreshUI::State::Success, "Refresh complete");
        else
            AlterEgo::RefreshUI::Status(AlterEgo::RefreshUI::State::Ready,
                g_ClearsFetched ? "Ready" : "No Clears data available - click Refresh");
    }

    ImGui::Separator();
    ImGui::BeginChild("##ClearsContentScroll", ImVec2(0, 0), false);

    std::lock_guard<std::mutex> lock(g_ClearsMutex);
    const int64_t utcNow = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t weeklyEndUtc = std::chrono::duration_cast<std::chrono::seconds>(
        (CalcLastWeeklyReset(std::chrono::system_clock::now()) +
        std::chrono::hours(24 * 7)).time_since_epoch()).count();

    // ---- Daily Fractals ----
    bool showFractals = true;
    {
        char suffix[64];
        snprintf(suffix, sizeof(suffix), "(resets in %s)", dailyResetStr.c_str());
        showFractals = RenderClearSectionToggle(0, "Daily Fractals", ImVec4(0.3f, 0.7f, 0.9f, 1.0f), suffix);
    }

    if (showFractals) {
    if (g_DailyFractals.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  No data");
    } else {
        ImGui::Indent(4.0f);
        // Build three daily map cards, then a fourth Recommended card.
        // Status comes from the existing H&S achievement completion data.
        std::vector<DailyFractalCard> cards;
        for (const auto& e : g_DailyFractals) {
            int tier = -1;
            if      (e.tier == "T1") tier = 0;
            else if (e.tier == "T2") tier = 1;
            else if (e.tier == "T3") tier = 2;
            else if (e.tier == "T4") tier = 3;
            if (tier < 0) continue;
            const std::string name = ShortenFractalName(e.name);
            auto it = std::find_if(cards.begin(), cards.end(),
                [&](const DailyFractalCard& c) { return c.title == name; });
            if (it == cards.end()) {
                cards.emplace_back();
                cards.back().title = name;
                it = cards.end() - 1;
            }
            it->entries[tier] = &e;
        }
        std::sort(cards.begin(), cards.end(),
            [](const DailyFractalCard& a, const DailyFractalCard& b) {
                return a.title < b.title;
            });
        DailyFractalCard recommended;
        recommended.title = "Recommended";
        recommended.recommended = true;
        std::vector<const ClearEntry*> recs;
        for (const auto& e : g_DailyFractals)
            if (e.tier == "Rec") recs.push_back(&e);
        std::sort(recs.begin(), recs.end(),
            [](const ClearEntry* a, const ClearEntry* b) {
                return RecommendedFractalScale(*a) < RecommendedFractalScale(*b);
            });
        for (const ClearEntry* e : recs) {
            const int scale = RecommendedFractalScale(*e);
            int slot = scale >= 1 && scale <= 75 ? (scale - 1) / 25 : -1;
            if (slot < 0 || recommended.entries[slot]) {
                // Unknown/duplicate scales: keep the entry visible rather
                // than overwriting another scale or inventing a tier.
                slot = -1;
                for (int t = 0; t < 3; ++t)
                    if (!recommended.entries[t]) { slot = t; break; }
            }
            if (slot < 0) continue;
            recommended.entries[slot] = e;
            recommended.labels[slot] = scale ? std::to_string(scale) : "?";
        }
        for (int t = 0; t < 4; ++t) {
            const std::string tier = "T" + std::to_string(t + 1);
            for (auto& card : cards) card.labels[t] = tier;
            if (t < 3 && recommended.labels[t].empty())
                recommended.labels[t] = "?";
        }
        cards.push_back(std::move(recommended));
        RenderDailyFractalCards(cards, g_ClearsFetched,
                                g_ClearsHideComplete && g_ClearsFetched);

        // Uncategorized fractals (no tier) — show as plain rows
        for (const auto& e : g_DailyFractals) {
            if (!e.tier.empty() || (g_ClearsHideComplete && g_ClearsFetched && e.done)) continue;
            ImGui::Text("  ");
            ImGui::SameLine();
            RenderClearStatus(g_ClearsFetched, e.done, e.name.c_str());
        }
        ImGui::Unindent(4.0f);
    }
    if (g_ClearsFetched) {
        std::vector<std::string> todaysT4;
        for (const auto& e : g_DailyFractals)
            if (e.tier == "T4")
                todaysT4.push_back(ShortenFractalName(e.name));
        const auto* next = ClearsRotation::NextFractalsFromLive(todaysT4);
        const std::string date = ClearsRotation::BountyDateLabel(
            (ClearsRotation::FloorDay(utcNow) + 1) * ClearsRotation::kDay);
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.13f, 0.16f, 0.92f));
        // The preview is a fixed-height read-only card, not a scroll area.
        // Forward mouse-wheel scrolling to the surrounding Clears page.
        if (ImGui::BeginChild("##nextFractalPreview", ImVec2(0, 79.0f),
                              true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGui::TextColored(ImVec4(0.64f, 0.74f, 0.86f, 1.0f),
                "Next Reset - %s", date.c_str());
            if (next) {
                // Two rows x four invisible columns. The left column holds
                // the category; the other three hold daily maps / recs.
                // Use normal bright text, keeping only the heading blue.
                if (ImGui::BeginTable("##nextResetGrid", 4,
                                      ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 105.0f);
                    ImGui::TableSetupColumn("One", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Two", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Three", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::ColorConvertU32ToFloat4(
                            AlterEgo::CompletionTheme::kAvailableText));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Dailies");
                    for (int i = 0; i < 3; ++i) {
                        ImGui::TableSetColumnIndex(i + 1);
                        ImGui::TextUnformatted(next->dailies[i]);
                        if (ImGui::IsItemHovered()) {
                            const std::string info =
                                FractalScales::DailyTooltip(next->dailies[i]);
                            ImGui::SetTooltip("%s", info.c_str());
                        }
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Recommended");
                    for (int i = 0; i < 3; ++i) {
                        ImGui::TableSetColumnIndex(i + 1);
                        // Match the current Recommended tooltip ordering:
                        // tier, exact scale in parentheses, fractal name.
                        const std::string label = "T" + std::to_string(i + 1) +
                            " (" + std::to_string(next->recScales[i]) + ") " +
                            next->recNames[i];
                        ImGui::TextUnformatted(label.c_str());
                        if (ImGui::IsItemHovered()) {
                            const std::string info = "Recommended\n" + label +
                                "\nSpecific scale required";
                            ImGui::SetTooltip("%s", info.c_str());
                        }
                    }
                    ImGui::PopStyleColor();
                    ImGui::EndTable();
                }
            } else {
                ImGui::TextDisabled("Next reset forecast unavailable");
                ImGui::TextDisabled("Today's daily schedule could not be verified.");
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    } // expanded section

    ImGui::Spacing();
    ImGui::Separator();

    // ---- Daily Raid Bounties ----
    bool showBounties = true;
    {
        char suffix[64];
        snprintf(suffix, sizeof(suffix), "(resets in %s)", dailyResetStr.c_str());
        char summary[64] = {};
        int completed = 0;
        if (g_ClearsFetched && !g_DailyBounties.empty()) {
            for (const auto& bounty : g_DailyBounties)
                if (bounty.done) ++completed;
            snprintf(summary, sizeof(summary), "%d / %zu completed",
                completed, g_DailyBounties.size());
        }
        showBounties = RenderClearSectionToggle(1, "Daily Raid Bounties",
            ImVec4(0.9f, 0.6f, 0.3f, 1.0f), suffix,
            summary[0] ? summary : nullptr,
            summary[0] && completed == static_cast<int>(g_DailyBounties.size()));
    }

    if (showBounties) {
    if (g_DailyBounties.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  No data");
    } else {
        ImGui::Indent(4.0f);
        float availW = ImGui::GetContentRegionAvail().x - 4.0f;
        float gapBnty = 6.0f;
        float halfWBnty = (availW - gapBnty) * 0.5f;
        size_t visibleCount = 0;
        for (const auto& e : g_DailyBounties)
            if (!g_ClearsHideComplete || !g_ClearsFetched || !e.done) ++visibleCount;
        size_t shown = 0;
        for (size_t i = 0; i < g_DailyBounties.size(); i++) {
            const auto& e = g_DailyBounties[i];
            if (g_ClearsHideComplete && g_ClearsFetched && e.done) continue;
            std::string display = e.name;
            if (display.find("Raid Bounty: ") == 0)
                display = display.substr(13);

            uint32_t wingId = BountyBossToWing(display);
            std::string strikeName = wingId ? std::string() : BountyToStrikeName(display);
            std::string subtitle;
            std::string bannerKey;
            ThemeGrad theme;
            if (wingId) {
                const char* wingShort = BountyWingShortName(wingId);
                const char* wingFull  = BountyWingFullName(wingId);
                if (wingShort && *wingShort) { subtitle = wingShort; }
                if (wingFull && *wingFull) {
                    if (!subtitle.empty()) subtitle += " \xC2\xB7 ";
                    subtitle += wingFull;
                }
                if (subtitle.empty()) subtitle = "Raid Bounty";
                bannerKey = "wing:" + std::to_string(wingId);
                theme = WingTheme(wingId);
            } else if (!strikeName.empty()) {
                subtitle = StrikePickerCategory(strikeName);
                bannerKey = "strike:" + strikeName;
                theme = StrikeTheme(strikeName);
            } else {
                subtitle = "Raid Bounty";
                theme = ThemeGrad{ IM_COL32(60, 44, 20, 255), IM_COL32(200, 154, 74, 255) };
            }

            ImGui::PushID((int)i);
            ImVec2 startPos = ImGui::GetCursorScreenPos();
            RenderClearBanner(display, subtitle,
                g_ClearsFetched, e.done, theme,
                /*tooltip*/ nullptr, /*widthOverride*/ halfWBnty,
                bannerKey);
            ImGui::PopID();

            ++shown;
            if (shown % 2 == 1 && shown < visibleCount) {
                ImGui::SetCursorScreenPos(ImVec2(startPos.x + halfWBnty + gapBnty, startPos.y));
            }
        }
        if (visibleCount == 0)
            ImGui::TextDisabled("  All daily bounties completed.");
        ImGui::Unindent(4.0f);
    }

    } // expanded section

    ImGui::Spacing();
    ImGui::Separator();

    // ---- Weekly Strikes ----
    bool showStrikes = true;
    {
        char suffix[64];
        snprintf(suffix, sizeof(suffix), "(resets in %s)", weeklyResetStr.c_str());
        char summary[64] = {};
        int completed = 0;
        if (g_ClearsFetched && g_WeeklyStrikes.id != 0 &&
            !g_WeeklyStrikes.bitNames.empty()) {
            for (size_t i = 0; i < g_WeeklyStrikes.bitNames.size(); ++i)
                if (i < g_WeeklyStrikes.bitDone.size() &&
                    g_WeeklyStrikes.bitDone[i]) ++completed;
            snprintf(summary, sizeof(summary), "%d / %zu completed",
                completed, g_WeeklyStrikes.bitNames.size());
        }
        showStrikes = RenderClearSectionToggle(2, "Weekly Strikes",
            ImVec4(0.7f, 0.4f, 0.9f, 1.0f), suffix,
            summary[0] ? summary : nullptr,
            summary[0] && completed == static_cast<int>(g_WeeklyStrikes.bitNames.size()));
    }

    if (showStrikes) {
    if (g_WeeklyStrikes.id == 0) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  No data");
    } else {
        ImGui::Indent(4.0f);
        // Three columns at wider widths, two at narrower widths. Use an
        // ImGui table so rows wrap predictably even with Hide Complete enabled.
        // All 14 completion bits remain in their original order.
        const float gridWidth = ImGui::GetContentRegionAvail().x;
        const int strikeColumns = gridWidth >= 900.0f ? 3 : 2;
        size_t shown = 0;
        std::vector<size_t> pickerOrder;
        pickerOrder.reserve(g_WeeklyStrikes.bitNames.size());
        for (size_t i = 0; i < g_WeeklyStrikes.bitNames.size(); ++i)
            pickerOrder.push_back(i);
        // Presentation order only: original achievement indices remain intact.
        std::stable_sort(pickerOrder.begin(), pickerOrder.end(),
            [](size_t left, size_t right) {
                return StrikePickerOrder(g_WeeklyStrikes.bitNames[left]) <
                       StrikePickerOrder(g_WeeklyStrikes.bitNames[right]);
            });

        if (ImGui::BeginTable("##WeeklyStrikesGrid", strikeColumns,
                              ImGuiTableFlags_SizingStretchSame)) {
            for (const size_t i : pickerOrder) {
                const bool bitDone = i < g_WeeklyStrikes.bitDone.size() &&
                    g_WeeklyStrikes.bitDone[i];
                if (g_ClearsHideComplete && g_ClearsFetched && bitDone) continue;

                const std::string& name = g_WeeklyStrikes.bitNames[i];
                // Display aliases only: never change the source achievement
                // name, bit index, theme, or bounty matching.
                const std::string normalized = ClearsRotation::Normalize(name);
                const bool isKela = normalized == "guardiansglade";
                std::string displayName = name;
                if (isKela)
                    displayName = "Kela";
                else if (normalized == "voiceofthefallenandclawofthefallen" ||
                         normalized == "voiceandclaw")
                    displayName = "Voice and Claw of Fallen";
                else if (normalized == "fraenirofjormag")
                    displayName = "Fraenir";
                else if (normalized == "whisperofjormag")
                    displayName = "Whisper";

                // One full encounter title, then its raid picker category;
                // never include historical map/zone locations.
                const char* fullTitle = isKela ? "Guardian's Glade" : name.c_str();
                const char* description = isKela
                    ? "Kela, Seneschal of Waves. Weekly completion is from Guardian's Glade."
                    : nullptr;
                const std::string subtitle = StrikePickerCategory(name);
                const std::string date = UpcomingBountyLabel(
                    isKela ? "Kela" : name, g_DailyBounties,
                    utcNow, weeklyEndUtc, g_ClearsFetched);

                if (shown % strikeColumns == 0) ImGui::TableNextRow();
                ImGui::TableSetColumnIndex((int)(shown % strikeColumns));
                ImGui::PushID((int)i);
                RenderClearBanner(displayName, subtitle,
                    g_ClearsFetched, bitDone, StrikeTheme(name),
                    /*tooltip*/ description,
                    /*widthOverride*/ ImGui::GetContentRegionAvail().x,
                    /*bannerKey*/ "strike:" + name, date,
                    /*fullTooltipTitle*/ fullTitle);
                ImGui::PopID();
                ++shown;
            }
            // Nexus is a fifteenth visual cell, but NOT a fifteenth
            // achievement bit. Keep unknown rather than claiming a clear.
            if (shown % strikeColumns == 0) ImGui::TableNextRow();
            ImGui::TableSetColumnIndex((int)(shown % strikeColumns));
            ImGui::PushID("vloxx");
            RenderClearBanner("Nexus", "Visions of Eternity",
                /*fetched*/ false, /*done*/ false, StrikeTheme("Vloxx"),
                "Vloxx. Weekly clear not verified because the 14-bit Weekly Raid Encounters achievement excludes Nexus of Eternity.",
                ImGui::GetContentRegionAvail().x, "strike:vloxx", "",
                "Nexus of Eternity");
            ImGui::PopID();
            ImGui::EndTable();
        }
        if (shown == 0)
            ImGui::TextDisabled("  All weekly raid encounter objectives completed.");

        ImGui::Unindent(4.0f);
    }

    } // expanded section

    ImGui::Spacing();
    ImGui::Separator();

    // ---- Weekly Raids ----
    bool showRaids = true;
    {
        char suffix[64];
        snprintf(suffix, sizeof(suffix), "(resets in %s)", weeklyResetStr.c_str());
        char summary[64] = {};
        int completed = 0, total = 0;
        if (g_ClearsFetched && !g_WeeklyWings.empty()) {
            for (const auto& wing : g_WeeklyWings)
                for (size_t i = 0; i < wing.bitNames.size(); ++i)
                    if (!ShortenEncounterName(wing.bitNames[i]).empty()) {
                        ++total;
                        if (i < wing.bitDone.size() && wing.bitDone[i])
                            ++completed;
                    }
            if (total > 0)
                snprintf(summary, sizeof(summary), "%d / %d completed",
                    completed, total);
        }
        showRaids = RenderClearSectionToggle(3, "Weekly Raids",
            ImVec4(0.9f, 0.7f, 0.3f, 1.0f), suffix,
            summary[0] ? summary : nullptr, total > 0 && completed == total);
    }

    if (showRaids) {
    if (g_WeeklyWings.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  No data");
    } else {
        ImGui::Indent(4.0f);
        uint32_t embWingId = CurrentEmboldenedWingId();
        uint32_t cotmWingId = CurrentCallOfTheMistsWingId();
        size_t visibleWings = 0;
        for (const auto& wing : g_WeeklyWings) {
            std::string cleanName = CleanWingName(wing.name);
            const char* shortW = WingShortName(wing.id);

            // Build chip list, skipping progress-only bits (empty short name)
            std::vector<WingEncChip> chips;
            int doneCount = 0, totalCount = 0;
            for (size_t i = 0; i < wing.bitNames.size(); i++) {
                std::string shortEnc = ShortenEncounterName(wing.bitNames[i]);
                if (shortEnc.empty()) continue; // skip
                bool bitDone = (i < wing.bitDone.size()) ? wing.bitDone[i] : false;
                ++totalCount;
                if (bitDone) ++doneCount;
                if (g_ClearsHideComplete && g_ClearsFetched && bitDone) continue;
                WingEncChip c;
                c.label = shortEnc;
                c.fullName = wing.bitNames[i];
                c.done = bitDone;
                c.secondary = IsSecondaryRaidEncounter(shortEnc);
                c.bountyDate = UpcomingBountyLabel(shortEnc, g_DailyBounties,
                    utcNow, weeklyEndUtc, g_ClearsFetched);
                chips.push_back(c);
            }

            if (g_ClearsHideComplete && g_ClearsFetched && chips.empty()) continue;
            ++visibleWings;
            ImGui::PushID((int)wing.id);
            if (ImGui::GetContentRegionAvail().x >= 830.0f) {
                RenderCompactWingBanner(shortW ? shortW : "", cleanName,
                    doneCount, totalCount, g_ClearsFetched, chips,
                    wing.id == embWingId, wing.id == cotmWingId);
            } else {
                RenderWingBanner(shortW ? shortW : "", cleanName,
                    doneCount, totalCount, g_ClearsFetched,
                    WingTheme(wing.id), chips,
                    /*bannerKey*/ "wing:" + std::to_string(wing.id),
                    /*isEmboldened*/ wing.id == embWingId,
                    /*isCallOfTheMists*/ wing.id == cotmWingId);
            }
            ImGui::PopID();
        }
        if (visibleWings == 0)
            ImGui::TextDisabled("  All weekly raids completed.");
        ImGui::Unindent(4.0f);
    }
    } // expanded section

    ImGui::EndChild();
}

static void RenderClearsShared(bool showVault) {
    auto now = std::chrono::system_clock::now();

    auto dailyReset  = CalcLastDailyReset(now);
    auto weeklyReset = CalcLastWeeklyReset(now);
    auto dailyBuffered  = dailyReset  + std::chrono::seconds(60);
    auto weeklyBuffered = weeklyReset + std::chrono::seconds(60);

    // ---- Clears reset detection ----
    bool resetTriggered = false;
    if (g_LastDailyReset != std::chrono::system_clock::time_point{} &&
        dailyReset > g_LastDailyReset && now >= dailyBuffered) {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        g_DailyFractals.clear();
        g_DailyBounties.clear();
        g_ClearsFetched = false;
        resetTriggered = true;
    }
    if (g_LastWeeklyReset != std::chrono::system_clock::time_point{} &&
        weeklyReset > g_LastWeeklyReset && now >= weeklyBuffered) {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        g_WeeklyWings.clear();
        g_WeeklyStrikes = ClearEntry{};
        g_ClearsFetched = false;
        resetTriggered = true;
    }
    g_LastDailyReset  = dailyReset;
    g_LastWeeklyReset = weeklyReset;

    if (resetTriggered && !g_ClearsFetching)
        FetchClears();

    // ---- Vault reset detection ----
    if (g_VaultLastDailyReset != std::chrono::system_clock::time_point{} &&
        dailyReset > g_VaultLastDailyReset && now >= dailyBuffered) {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        g_VaultDaily = {};
        g_VaultFetched = false;
        g_VaultInitialAttempted = false;
    }
    if (g_VaultLastWeeklyReset != std::chrono::system_clock::time_point{} &&
        weeklyReset > g_VaultLastWeeklyReset && now >= weeklyBuffered) {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        g_VaultWeekly = {};
        g_VaultFetched = false;
        g_VaultInitialAttempted = false;
    }
    g_VaultLastDailyReset  = dailyReset;
    g_VaultLastWeeklyReset = weeklyReset;

    // ---- Clears deferred logic ----
    if (g_ClearsNeedRequery && g_ClearsFetched && !g_ClearsFetching) {
        g_ClearsNeedRequery = false;
        {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            for (auto& e : g_DailyFractals) { e.done = false; e.current = 0; e.bitDone.assign(e.bitDone.size(), false); }
            for (auto& e : g_DailyBounties) { e.done = false; e.current = 0; e.bitDone.assign(e.bitDone.size(), false); }
            for (auto& e : g_WeeklyWings)   { e.done = false; e.current = 0; e.bitDone.assign(e.bitDone.size(), false); }
            g_WeeklyStrikes.done = false; g_WeeklyStrikes.current = 0;
            g_WeeklyStrikes.bitDone.assign(g_WeeklyStrikes.bitDone.size(), false);
            g_ClearsStatusMsg = "Refreshing completion for new account...";
        }
        SendClearsAchQuery();
    }
    if (g_ClearsQueryPending.exchange(false) && !g_ClearsFetching)
        SendClearsAchQuery();
    // Fire a scheduled BUSY backoff retry once its delay has elapsed.
    {
        long long retryAt = g_ClearsRetryAtMs.load();
        if (retryAt != 0 && NowMs() >= retryAt && !g_ClearsFetching) {
            g_ClearsRetryAtMs = 0;
            SendClearsAchQuery();
        }
    }

    auto steadyNow = std::chrono::steady_clock::now();
    if (g_ClearsFetched && !g_ClearsFetching &&
        g_LastClearsCompletionQuery != std::chrono::steady_clock::time_point{} &&
        (steadyNow - g_LastClearsCompletionQuery) >= std::chrono::minutes(10)) {
        g_LastClearsCompletionQuery = steadyNow;
        SendClearsAchQuery();
    }

    std::string dailyResetStr = FormatTimeUntilReset(dailyReset, std::chrono::hours(24));
    std::string weeklyResetStr = FormatTimeUntilReset(weeklyReset, std::chrono::hours(24 * 7));
    if (showVault)
        RenderVaultTab(dailyResetStr, weeklyResetStr);
    else
        RenderClearsTabContent(dailyResetStr, weeklyResetStr);
}
void RenderVault() { RenderClearsShared(true); }
void RenderClears() { RenderClearsShared(false); }
