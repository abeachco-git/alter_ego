#include "UntrustedInput.h"
#include "AccountIdentity.h"
#include "CallbackLifetime.h"
#include "BackgroundWorkers.h"
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
#include <cctype>

#include "imgui.h"
#include "imgui_internal.h"
#include <nlohmann/json.hpp>
#include "GW2API.h"
#include "NativeProgress.h"
#include "ArenaNetTransport.h"
#include "HttpClient.h"
#include "AddonShared.h"
#include "Clears.h"
#include "ClearsRotation.h"
#include "StrikeDisplayName.h"
#include "StrikePicker.h"
#include "ClearsReset.h"
#include "SectionRefresh.h"
#include "AutoRefreshPolicy.h"
#include "FractalScales.h"
#include "FractalCardLayout.h"
#include "CompletionTheme.h"
#include "RaidChipWidths.h"
#include "RefreshUI.h"
#include "IconManager.h"
#include "VaultAssistance.h"
#include "WikiEventRuntime.h"
#include "PublicCatalog.h"
#include "StoragePaths.h"
#include "CompletionCache.h"

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
// Daily rollover verification keeps *old IDs* only, never old-day
// completion values. Cleared daily entries stay unavailable until refreshed.
// These vectors and bools are protected by g_ClearsMutex.
static std::vector<uint32_t> g_ClearsPriorDailyFractalIds;
static std::vector<uint32_t> g_ClearsPriorDailyBountyIds;
// Only yesterday's verified T4 names can seed an offline rotation preview.
static std::vector<std::string> g_ClearsPriorDailyT4Names;
static bool g_ClearsDailyRolloverPending = false;
static std::array<ClearsReset::RetryState,4> g_ClearsSectionRetry,g_ClearsProgressRetry;
static std::array<std::vector<ClearEntry>,2> g_ClearsPriorWeekly;
static std::array<std::time_t,4> g_ClearsSectionSuccess{};
static std::atomic<long long> g_ClearsDailyRolloverRetryAtMs{0};
static constexpr const char* ClearKeys[]={"dailyFractals","dailyBounties","weeklyWings","weeklyStrikes"};
static constexpr const char* ClearLabels[]={"Daily Fractals","Daily Raid Bounties","Weekly Raids","Weekly Strikes"};
static void UpdateClearsRetryLocked(){
    g_ClearsDailyRolloverPending=g_DailyFractals.empty()&&!g_ClearsPriorDailyFractalIds.empty()||
                                 g_DailyBounties.empty()&&!g_ClearsPriorDailyBountyIds.empty();
    long long next=0;
    for(const auto* retries:{&g_ClearsSectionRetry,&g_ClearsProgressRetry})
        for(const auto& retry:*retries)if(retry.at>0&&(next==0||retry.at<next))next=retry.at;
    g_ClearsDailyRolloverRetryAtMs=next;
}
static bool ClearDefinitionsMissing(size_t i){
    return i==0?g_DailyFractals.empty():i==1?g_DailyBounties.empty():i==2?g_WeeklyWings.empty():!g_WeeklyStrikes.id;
}
static std::time_t g_ClearsLastSuccessTime = 0;
static std::chrono::steady_clock::time_point g_ClearsSuccessUntil{};
// steady_clock ms at which to fire a single BUSY backoff retry (0 = none)
static std::atomic<long long> g_ClearsRetryAtMs{0};
// A completion query can be outstanding even though category fetching is done.
// Track it separately so navigating between tabs cannot duplicate API queries.
static uint64_t g_ClearsGeneration=0,g_VaultGeneration=0;
static std::atomic<bool> g_ClearsCompletionInFlight{false};
static std::atomic<long long> g_ClearsQueryStartedAtMs{0};
static bool g_ClearsRetrySelective = false;
static constexpr long long CLEARS_QUERY_TIMEOUT_MS = 120000;

// Public globals — declared extern in Clears.h, accessed from dllmain.cpp
std::atomic<bool> g_ClearsFetched{false};
std::atomic<bool> g_ClearsFetching{false};
std::atomic<bool> g_ClearsNeedRequery{false};
std::atomic<bool> g_ClearsQueryPending{false};
std::atomic<bool> g_ClearsAutoBootPending{false};

// ---- Wizard's Vault state ----
using VaultObjective=AlterEgo::NativeProgress::Objective;
using VaultPeriod=AlterEgo::NativeProgress::Period;

static VaultPeriod g_VaultDaily;
static VaultPeriod g_VaultWeekly;
static VaultPeriod g_VaultSpecial;
static std::atomic<bool> g_VaultFetched{false};
static std::atomic<bool> g_VaultFetching{false};
static bool g_VaultInitialAttempted = false;
static bool g_VaultResponsesOK[3] = {};
static std::array<std::time_t,3> g_VaultSectionSuccess{};
static bool g_VaultTerminal[3]={};
static bool g_VaultBatchFailed=false,g_VaultPermissionDenied=false;
static std::time_t g_VaultLastSuccessTime = 0;
// Protected by g_ClearsMutex; complete only when all periods are verified.
static void UpdateVaultFreshness(){
    g_VaultLastSuccessTime=*std::min_element(g_VaultSectionSuccess.begin(),g_VaultSectionSuccess.end());
    g_VaultFetched=g_VaultLastSuccessTime>0;
}
static std::string g_VaultStatusMessage;
static bool g_VaultLastAttemptFailed = false;
static std::chrono::steady_clock::time_point g_VaultSuccessUntil{};
// steady_clock ms at which to fire a single BUSY backoff retry (0 = none)
// These per-type retry states and the staged responses are protected by
// g_ClearsMutex: the API worker delivers Vault responses from a worker thread.
static long long g_VaultRetryAtMs = 0;
static bool g_VaultRetryNeeded[3] = {};
static int g_VaultRetryAttempts[3] = {};
static long long g_VaultRequestStartMs = 0;
// Rollover confirmation is separate from rate-limit/BUSY retries above.
static VaultPeriod g_VaultPriorDaily;
static bool g_VaultDailyRolloverPending = false;
// Keep the complete previous week's state: GW2 can reuse weekly objective IDs,
// but their progress and claimed status reset independently.
static VaultPeriod g_VaultPriorWeekly;
static bool g_VaultWeeklyRolloverPending = false;
// Shared rollover retry/backoff; each section publishes independently.
static std::array<ClearsReset::RetryState,3> g_VaultSectionRetry;
static VaultPeriod g_VaultPriorSpecial;
static bool g_VaultSpecialRolloverPending=false;
static int64_t g_VaultSpecialResetEpoch=0;
static bool VaultRolloverPending(uint8_t i){
    return i==0?g_VaultDailyRolloverPending:i==1?g_VaultWeeklyRolloverPending:g_VaultSpecialRolloverPending;
}
static bool VaultAwaitingRetry(){
    return std::any_of(g_VaultSectionRetry.begin(),g_VaultSectionRetry.end(),[](const auto& r){return r.at!=0;});
}
static std::string g_VaultRequestAccount;
static AlterEgo::NativeAccounts::LinkedAccount g_VaultRequestCredential;
static constexpr long long VAULT_TIMEOUT_MS = 120000;
std::atomic<bool> g_VaultNeedRequery{false};

static std::string g_ClearsCacheAccount; // the account represented by current Clears data
static std::string g_VaultCacheAccount;  // the account represented by current Vault data

// Serialize capture and disk publication per completion dataset. Keep all
// cache SQL outside g_ClearsMutex and all Nexus calls outside both locks.
static std::mutex g_ClearsSaveMutex;
static std::mutex g_VaultSaveMutex;

static bool g_ShowVaultAssistance = true;
static bool g_VaultHideComplete = false;   // hide claimed vault objectives
static bool g_ClearsHideComplete = false;  // hide completed clear encounters
static bool g_ShowFractalNextResets = true; // show upcoming daily fractal forecast
static bool g_VaultOpen[3] = {true, true, true}; // Daily, Weekly, Special
static void LoadClearUiSettings();
static void SaveClearUiSettings();
static bool RenderVaultSectionToggle(int index, const char* title,
                                     ImVec4 color, const char* suffix,
                                     const char* summary,
                                     bool summaryComplete,std::chrono::system_clock::time_point seasonEnd);
static std::chrono::system_clock::time_point g_VaultLastDailyReset{};
static std::chrono::system_clock::time_point g_VaultLastWeeklyReset{};
static std::chrono::system_clock::time_point g_VaultSeasonEnd{};
static std::chrono::system_clock::time_point g_PublicVaultSeasonEnd{};
static int64_t g_PublicVaultSeasonSuccess=0,g_PublicVaultSeasonAttempt=0;
static bool g_PublicVaultSeasonPending=false;
static uint64_t g_VaultSeasonRevision=0,g_VaultRequestSeasonRevision=0;

// =========================================================================
// Clears - Backend (reset calculations, fetch, API response)
// =========================================================================

// Forward declarations of private helpers
static nlohmann::json CaptureClearsCacheLocked();
static bool PersistClearsCache(const std::string& account,const nlohmann::json& snapshot);
static std::string ShortenFractalName(const std::string& name);
static void FetchClears(bool manual=false);
static void SendClearsAchQuery(bool skipCompletedSections = false,bool manual=false);
static std::atomic<bool> g_ClearsPendingManual{false};
static void FetchVaultData(bool skipClaimedSections = false);
// Sorted IDs are used for a conservative post-reset freshness check.
static std::vector<uint32_t> DailyIds(const std::vector<ClearEntry>& entries) {
    std::vector<uint32_t> ids;
    ids.reserve(entries.size());
    for (const auto& e : entries) if (e.id != 0) ids.push_back(e.id);
    return ClearsReset::SortedIds(std::move(ids));
}
static int64_t ResetEpoch(std::chrono::system_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::seconds>(
        t.time_since_epoch()).count();
}
static void RequestVaultSection(uint8_t type,const std::string& account,const AlterEgo::NativeAccounts::LinkedAccount& credential,uint64_t generation,bool manual=false);
static nlohmann::json CaptureVaultCacheLocked();
static bool PersistVaultCache(const std::string& account,const nlohmann::json& snapshot);
static void RenderClearsTabContent(const std::string& dailyResetStr, const std::string& weeklyResetStr);
static void RenderVaultPeriodSection(int sectionIndex, const char* title, const char* resetLabel,
                                     ImVec4 color, const VaultPeriod& period,std::chrono::system_clock::time_point seasonEnd);
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

// Query incomplete sections only when opportunistically refreshing.
// Manual Refresh and reset fetching continue querying every achievement.
static void SendClearsAchQuery(bool skipCompletedSections,bool manual) {
    if (!APIDefs || g_ClearsCompletionInFlight.load()) return;
    const std::string acctName=GetEffectiveAccountName();
    const auto credential=AlterEgo::NativeAccounts::FindLinked(acctName);
    std::vector<uint32_t> allIds;
    std::array<bool,4> requestedSections{};
    bool force=manual;
    int64_t minimumEpoch=0;
    uint64_t generation=0;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if(acctName.empty() || acctName!=g_ClearsCacheAccount||g_ClearsCompletionInFlight||g_ClearsFetching)return;
        const auto now=std::chrono::system_clock::now();
        const int64_t daily=ResetEpoch(CalcLastDailyReset(now)),weekly=ResetEpoch(CalcLastWeeklyReset(now));
        const bool completed[]={AlterEgo::SectionRefresh::ClearsDone(g_DailyFractals),
            AlterEgo::SectionRefresh::ClearsDone(g_DailyBounties),AlterEgo::SectionRefresh::ClearsDone(g_WeeklyWings),
            g_WeeklyStrikes.id!=0&&g_WeeklyStrikes.done};
        for(size_t i=0;i<4;++i){
            const auto reset=i<2?daily:weekly;
            if(ClearDefinitionsMissing(i)||!ClearsReset::GracePassed(reset,ResetEpoch(now))||
               !g_ClearsProgressRetry[i].Ready(NowMs(),manual))continue;
            requestedSections[i]=!skipCompletedSections||!completed[i]||g_ClearsSectionSuccess[i]==0;
            if(!requestedSections[i])continue;
            minimumEpoch=std::max(minimumEpoch,reset+ClearsReset::kResetBufferSeconds);
            if(i>=2&&g_ClearsProgressRetry[i].reason=="API returned prior weekly progress")force=true;
            if(i==0)for(const auto& e:g_DailyFractals)allIds.push_back(e.id);
            if(i==1)for(const auto& e:g_DailyBounties)allIds.push_back(e.id);
            if(i==2)for(const auto& e:g_WeeklyWings)allIds.push_back(e.id);
            if(i==3)allIds.push_back(g_WeeklyStrikes.id);
        }
        if (allIds.empty()) return; // every known section is done
        g_ClearsRetrySelective = skipCompletedSections;
        g_ClearsCompletionInFlight = true;
        generation=++g_ClearsGeneration;
        g_ClearsQueryStartedAtMs = NowMs();
        g_ClearsStatusMsg = skipCompletedSections
            ? "Refreshing incomplete sections..." : "Querying completion...";
    }

    AlterEgo::Background::Workers().Launch([acctName,credential,allIds,generation,force,minimumEpoch,requestedSections]{
        auto result=AlterEgo::NativeProgress::FetchAchievements(AlterEgo::GW2API::GetDataDirectory(),credential,force,AlterEgo::RefreshEngine::ObjectiveSeconds(),minimumEpoch);
        AlterEgo::Background::Checkpoint();
        const bool accepted=AlterEgo::NativeAccounts::CommitIfCurrent(credential,[&]{
        std::lock_guard<std::mutex> saving(g_ClearsSaveMutex);
        nlohmann::json snapshot;
        {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            if(generation!=g_ClearsGeneration||!g_ClearsCompletionInFlight||acctName!=g_ClearsCacheAccount)return true;
            g_ClearsCompletionInFlight=false;g_ClearsQueryStartedAtMs=0;g_ClearsFetching=false;
            if(!result.snapshot){
                g_ClearsStatusMsg=result.error;
                for(size_t i=0;i<4;++i)if(requestedSections[i]){
                    auto& retry=g_ClearsProgressRetry[i];retry.Fail(NowMs(),result.error.empty()?"Progress refresh failed":result.error);
                    // Match the shared account broker's ordinary error cooldown.
                    retry.at=NowMs()+AlterEgo::RefreshEngine::FailureSeconds*1000;
                }
                UpdateClearsRetryLocked();return true;
            }
            const std::set<uint32_t> requested(allIds.begin(),allIds.end());
            auto apply=[&](ClearEntry& e){
                if(!requested.count(e.id))return;
                auto it=result.snapshot->entries.find(e.id);
                e.bitDone.assign(e.bitNames.size(),false);
                if(it==result.snapshot->entries.end()){e.done=false;e.current=0;return;}
                const auto& p=it->second;e.done=p.done;e.current=p.current;if(p.max>0)e.max=p.max;
                if(p.done&&e.max>0&&e.current>=e.max)e.bitDone.assign(e.bitNames.size(),true);
                else for(auto bit:p.completed_bits)if(bit<e.bitDone.size())e.bitDone[bit]=true;
            };
            auto applySection=[&](size_t section,std::vector<ClearEntry>& rows){
                if(!requestedSections[section]||rows.empty())return;
                auto next=rows;
                for(auto& e:next){if(!requested.count(e.id))return;apply(e);}
                if(section>=2&&ClearsReset::HoldClearProgress(g_ClearsPriorWeekly[section-2],next)){
                    g_ClearsProgressRetry[section].Fail(NowMs(),"API returned prior weekly progress");
                    return;
                }
                rows=std::move(next);g_ClearsSectionSuccess[section]=result.snapshot->fetchedAt;
                g_ClearsProgressRetry[section].Reset();
                if(section>=2)g_ClearsPriorWeekly[section-2].clear();
            };
            applySection(0,g_DailyFractals);applySection(1,g_DailyBounties);
            applySection(2,g_WeeklyWings);
            if(g_WeeklyStrikes.id){std::vector<ClearEntry> strikes{g_WeeklyStrikes};applySection(3,strikes);g_WeeklyStrikes=std::move(strikes.front());}
            UpdateClearsRetryLocked();
            g_ClearsFetched=std::any_of(g_ClearsSectionSuccess.begin(),g_ClearsSectionSuccess.end(),[](auto stamp){return stamp>0;});
            g_ClearsStatusMsg.clear();g_ClearsLastSuccessTime=result.snapshot->fetchedAt;
            g_LastClearsCompletionQuery=std::chrono::steady_clock::now();g_ClearsSuccessUntil=g_LastClearsCompletionQuery+std::chrono::seconds(4);
            snapshot=CaptureClearsCacheLocked();
        }
        if(!PersistClearsCache(acctName,snapshot)){
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            if(generation==g_ClearsGeneration&&acctName==g_ClearsCacheAccount)g_ClearsStatusMsg="Completion retrieved; cache write failed.";
        }
        return true;
        });
        if(!accepted){
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            if(generation==g_ClearsGeneration&&acctName==g_ClearsCacheAccount){g_ClearsCompletionInFlight=false;g_ClearsQueryStartedAtMs=0;g_ClearsStatusMsg="Account changed; refresh again.";}
        }
    },[generation]{std::lock_guard<std::mutex> lock(g_ClearsMutex);if(generation==g_ClearsGeneration){g_ClearsCompletionInFlight=false;g_ClearsQueryStartedAtMs=0;g_ClearsStatusMsg="Completion refresh cancelled";}});
}

// =========================================================================
// Clears - Wizard's Vault fetch and response
// =========================================================================
static const char* VaultSectionName(uint8_t type) {
    static const char* names[] = {"daily", "weekly", "special"};
    return type < 3 ? names[type] : "unknown";
}
static void ApplyVaultSection(uint8_t type,const std::string& account,uint64_t generation,int status,VaultPeriod period,std::string error={});
static void RequestVaultSection(uint8_t type,const std::string& account,const AlterEgo::NativeAccounts::LinkedAccount& credential,uint64_t generation,bool manual){
    AlterEgo::Background::Workers().Launch([type,account,credential,generation,manual]{
        AlterEgo::ApiActivity::ReasonScope reason(manual?AlterEgo::ApiActivity::Reason::Manual:AlterEgo::ApiActivity::Reason::Objective);
        const auto path=std::string("/v2/account/wizardsvault/")+VaultSectionName(type);
        auto r=AlterEgo::NativeAccounts::Request(credential,ClearsReset::FreshPath(path,ClearsReset::RequestToken()),{"progression"},ClearsReset::NoCacheHeaders);
        // Authenticated Vault has no documented alternate bulk route. Preserve
        // the canonical route if the service rejects an extra query parameter.
        if(r.status==400||r.status==404)
            r=AlterEgo::NativeAccounts::Request(credential,path,{"progression"},ClearsReset::NoCacheHeaders);
        VaultPeriod period;
        if(r){try{period=AlterEgo::NativeProgress::ParseVault(r.data,type==2);}
            catch(...){r.status=0;r.error="Invalid Vault objective response";}}
        if(!AlterEgo::NativeAccounts::CommitIfCurrent(credential,[&]{
            ApplyVaultSection(type,account,generation,r.status,std::move(period),r.error);return true;
        }))ApplyVaultSection(type,account,generation,0,{},"Account changed or cancelled");
    },[type,account,generation]{ApplyVaultSection(type,account,generation,0,{});});
}

// Start one asynchronous batch; each response is delivered separately later.
// Previous successful data and its timestamp remain valid while refreshing.
static void RefreshVaultSeason(bool manual){
    // Public season metadata is independent from account objective progress.
    bool fetchSeason=false;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        const auto now=static_cast<int64_t>(std::time(nullptr));
        const auto end=std::chrono::system_clock::to_time_t(g_PublicVaultSeasonEnd);
        if(g_PublicVaultSeasonEnd!=std::chrono::system_clock::time_point{})g_VaultSeasonEnd=g_PublicVaultSeasonEnd;
        fetchSeason=!g_PublicVaultSeasonPending&&AlterEgo::RefreshEngine::Due(
            g_PublicVaultSeasonSuccess,g_PublicVaultSeasonAttempt,now,AlterEgo::RefreshEngine::SeasonSeconds,
            manual,end>0&&now>=end?end:0);
        if(fetchSeason){g_PublicVaultSeasonPending=true;g_PublicVaultSeasonAttempt=now;}
    }
    if(fetchSeason)AlterEgo::Background::Workers().Launch([manual]{
        AlterEgo::ApiActivity::ReasonScope reason(manual?AlterEgo::ApiActivity::Reason::Manual:AlterEgo::ApiActivity::Reason::Objective);
        const auto root=AlterEgo::GW2API::GetDataDirectory();
        const auto apply=[](const AlterEgo::Cache::VaultSeasonSnapshot& season){
            const auto tp=std::chrono::system_clock::from_time_t(static_cast<std::time_t>(season.end));
            std::lock_guard<std::mutex> saving(g_VaultSaveMutex);
            std::string account;
            nlohmann::json snapshot;
            {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            if(season.verifiedAt<g_PublicVaultSeasonSuccess)return;
            const auto previous=g_VaultSeasonEnd;
            if(previous!=std::chrono::system_clock::time_point{}&&previous<=std::chrono::system_clock::now()&&previous!=tp){
                ++g_VaultSeasonRevision;
                if(!g_VaultSpecialRolloverPending){g_VaultPriorSpecial=g_VaultSpecial;g_VaultSpecialResetEpoch=ResetEpoch(previous);}
                g_VaultSpecialRolloverPending=true;g_VaultSectionRetry[2].Reset();
                g_VaultSpecial={};g_VaultSectionSuccess[2]=0;
                UpdateVaultFreshness();
            }
            g_PublicVaultSeasonEnd=tp;g_PublicVaultSeasonSuccess=season.verifiedAt;
            g_VaultSeasonEnd=tp;
            account=g_VaultCacheAccount;
            snapshot=CaptureVaultCacheLocked();
            }
            if(!account.empty())(void)PersistVaultCache(account,snapshot);
        };
        const auto finish=[](bool success){
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            g_PublicVaultSeasonPending=false;
            if(success)g_PublicVaultSeasonAttempt=0;
        };
        // Disk work stays on the owned worker. The account Vault projection
        // is not proof of the public season's verification time.
        try{
            const auto cached=AlterEgo::Cache::ReadVaultSeason(root);
            const auto now=static_cast<int64_t>(std::time(nullptr));
            if(cached&&cached->verifiedAt<=now&&cached->end>now){
                apply(*cached);
                if(!manual&&!AlterEgo::RefreshEngine::Due(cached->verifiedAt,0,now,AlterEgo::RefreshEngine::SeasonSeconds)){
                    finish(true);return;
                }
            }
        }catch(...){/* Preserve in-memory metadata; attempt a verified refresh. */}
        auto r=AlterEgo::ArenaNet::Get(ClearsReset::FreshPath("/v2/wizardsvault",ClearsReset::RequestToken()),{},ClearsReset::NoCacheHeaders);
        if(r.status_code==400||r.status_code==404)r=AlterEgo::ArenaNet::Get("/v2/wizardsvault",{},ClearsReset::NoCacheHeaders);
        AlterEgo::Background::Checkpoint();
        std::chrono::system_clock::time_point tp{};
        if(r.status_code==200)try{tp=ParseISO8601(AlterEgo::Untrusted::Parse(r.body,AlterEgo::Untrusted::PublicApi).at("end").get<std::string>());}catch(...){}
        const AlterEgo::Cache::VaultSeasonSnapshot next{
            static_cast<int64_t>(std::time(nullptr)),
            static_cast<int64_t>(std::chrono::system_clock::to_time_t(tp))};
        bool saved=false;
        try{saved=AlterEgo::Cache::WriteVaultSeason(root,next);}catch(...){}
        if(!saved){finish(false);return;}
        apply(next);finish(true);
    },[]{std::lock_guard<std::mutex> lock(g_ClearsMutex);g_PublicVaultSeasonPending=false;
        if(AlterEgo::Background::Workers().Stopping())g_PublicVaultSeasonAttempt=0;});
}

static void FetchVaultData(bool skipClaimedSections) {
    RefreshVaultSeason(!skipClaimedSections);
    if (!APIDefs || g_VaultFetching.load()) return;
    const std::string account = GetEffectiveAccountName();
    const auto credential=AlterEgo::NativeAccounts::FindLinked(account);
    uint64_t generation=0;
    std::array<bool, 3> request = {true, true, true};
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if (g_VaultFetching.load() || account.empty() || account!=g_VaultCacheAccount) return;
        // A section is skippable only when all awards are CLAIMED, and the
        // current period hasn't been invalidated by a reset. Never use an
        // unknown/expired season end to suppress a new Special season.
        const auto now=std::time(nullptr);
        const auto seasonEnd=std::chrono::system_clock::to_time_t(g_VaultSeasonEnd);
        const bool expired=seasonEnd>0&&now>=seasonEnd;
        if(expired&&!g_VaultSpecialRolloverPending){
            g_VaultPriorSpecial=g_VaultSpecial;g_VaultSpecialResetEpoch=seasonEnd;
            g_VaultSpecialRolloverPending=true;g_VaultSectionRetry[2].Reset();
            g_VaultSpecial={};g_VaultSectionSuccess[2]=0;++g_VaultSeasonRevision;UpdateVaultFreshness();
        }
        const VaultPeriod* periods[]={&g_VaultDaily,&g_VaultWeekly,&g_VaultSpecial};
        for(uint8_t i=0;i<3;++i){
            const auto interval=i==2?AlterEgo::RefreshEngine::BrowseSeconds:AlterEgo::RefreshEngine::ObjectiveSeconds();
            const bool retry=VaultRolloverPending(i)||g_VaultSectionRetry[i].at!=0;
            request[i]=!skipClaimedSections||(g_VaultSectionRetry[i].Ready(NowMs())&&
                (retry||(!AlterEgo::SectionRefresh::VaultClaimed(*periods[i])&&
                 AlterEgo::RefreshEngine::Due(g_VaultSectionSuccess[i],0,now,interval,false,0,
                    AlterEgo::RefreshEngine::Offset(account+"/vault/"+std::to_string(i))))));
        }
        // Never publish new-season progress against an expired known season.
        // Daily/weekly requests do not depend on that public endpoint.
        if(expired)request[2]=false;
        const auto clockNow=std::chrono::system_clock::now();
        if(!ClearsReset::GracePassed(ResetEpoch(CalcLastDailyReset(clockNow)),now))request[0]=false;
        if(!ClearsReset::GracePassed(ResetEpoch(CalcLastWeeklyReset(clockNow)),now))request[1]=false;
        if(g_VaultSpecialRolloverPending&&!ClearsReset::GracePassed(g_VaultSpecialResetEpoch,now))request[2]=false;
        if (!request[0] && !request[1] && !request[2]) return;
        generation=++g_VaultGeneration;
        g_VaultRequestCredential=credential;
        g_VaultRequestSeasonRevision=g_VaultSeasonRevision;
        g_VaultFetching = true;
        g_VaultInitialAttempted = true;
        g_VaultLastAttemptFailed = false;
        g_VaultBatchFailed=false;g_VaultPermissionDenied=false;
        g_VaultStatusMessage = skipClaimedSections
            ? "Refreshing unclaimed Vault sections..." : "Fetching Vault objectives (0/3)...";
        g_VaultRequestStartMs = NowMs();
        g_VaultRequestAccount = account;
        g_VaultRetryAtMs = 0;
        for (int i = 0; i < 3; ++i) {
            g_VaultResponsesOK[i] = !request[i];
            g_VaultTerminal[i]=!request[i];
            g_VaultRetryNeeded[i] = false;
            g_VaultRetryAttempts[i] = 0;
        }
    }

    for (uint8_t type = 0; type < 3; ++type) {
        if (!g_VaultFetching.load()) break;
        if (request[type]) RequestVaultSection(type,account,credential,generation,!skipClaimedSections);
    }


}

static void ApplyVaultSection(uint8_t type,const std::string& account,uint64_t generation,int status,VaultPeriod period,std::string error){
    std::lock_guard<std::mutex> saving(g_VaultSaveMutex);
    nlohmann::json snapshot;
    bool save=false;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if(type>2||generation!=g_VaultGeneration||!g_VaultFetching.load()||
            account!=g_VaultRequestAccount||account!=g_VaultCacheAccount||g_VaultTerminal[type])return;
        if(AlterEgo::Background::Workers().Stopping()){
            g_VaultTerminal[type]=true;g_VaultRetryNeeded[type]=false;
            if(std::all_of(std::begin(g_VaultTerminal),std::end(g_VaultTerminal),[](bool x){return x;}))g_VaultFetching=false;
            return;
        }
        if(type==2&&g_VaultRequestSeasonRevision!=g_VaultSeasonRevision){
            // Season changed while this batch was in flight. Reject only its
            // Special response; daily and weekly remain independent.
            status=0;error="Season changed during request";
        }
        if(status==429&&++g_VaultRetryAttempts[type]<=3){
            g_VaultRetryNeeded[type]=true;
            g_VaultRetryAtMs=std::max(g_VaultRetryAtMs,NowMs()+5000LL);
            g_VaultStatusMessage=std::string("ArenaNet rate limited - retrying ")+VaultSectionName(type);
            return;
        }
        g_VaultTerminal[type]=true;g_VaultRetryNeeded[type]=false;
        const auto now=std::time(nullptr);
        const auto reset=type==0?ResetEpoch(CalcLastDailyReset(std::chrono::system_clock::now())):
                         type==1?ResetEpoch(CalcLastWeeklyReset(std::chrono::system_clock::now())):g_VaultSpecialResetEpoch;
        const auto& prior=type==0?g_VaultPriorDaily:type==1?g_VaultPriorWeekly:g_VaultPriorSpecial;
        const bool held=status==200&&VaultRolloverPending(type)&&
            ClearsReset::HoldVault(prior,period,now-reset,g_VaultSectionRetry[type].failures,type==2);
        if(status==200&&!held){
            if(type==0){g_VaultDaily=std::move(period);g_VaultDailyRolloverPending=false;g_VaultPriorDaily={};}
            else if(type==1){g_VaultWeekly=std::move(period);g_VaultWeeklyRolloverPending=false;g_VaultPriorWeekly={};}
            else {g_VaultSpecial=std::move(period);g_VaultSpecialRolloverPending=false;g_VaultPriorSpecial={};g_VaultSpecialResetEpoch=0;}
            g_VaultSectionRetry[type].Reset();
            g_VaultSectionSuccess[type]=std::time(nullptr);
            
            g_VaultResponsesOK[type]=true;UpdateVaultFreshness();save=true;
        }else{
            g_VaultBatchFailed=true;
            if(status==401||status==403)g_VaultPermissionDenied=true;
            const std::string why=held?"API returned unchanged prior period":
                !error.empty()?error:status>0?"HTTP "+std::to_string(status):"Refresh failed";
            g_VaultSectionRetry[type].Fail(NowMs(),why);
            if(status==401||status==403)g_VaultSectionRetry[type].at=NowMs()+900000;
            g_VaultStatusMessage=std::string(VaultSectionName(type))+": "+why;
        }
        int terminal=0;for(bool done:g_VaultTerminal)if(done)++terminal;
        if(terminal==3){
            g_VaultFetching=false;g_VaultRetryAtMs=0;
            g_VaultLastAttemptFailed=g_VaultBatchFailed;
            if(!g_VaultBatchFailed){
                g_VaultStatusMessage="Refresh complete";
                g_VaultSuccessUntil=std::chrono::steady_clock::now()+std::chrono::seconds(4);
            }else{
                g_VaultStatusMessage="Partial refresh; see section status";
            }
        }else if(!g_VaultBatchFailed&&g_VaultRetryAtMs==0){
            g_VaultStatusMessage="Fetching Vault objectives ("+std::to_string(terminal)+"/3)...";
        }
        if(save)snapshot=CaptureVaultCacheLocked();
    }
    if(save&&!PersistVaultCache(account,snapshot)){
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if(generation==g_VaultGeneration&&account==g_VaultCacheAccount)g_VaultStatusMessage="Objectives retrieved; cache write failed.";
    }
}

// Read public categories with HTTP status and schema/identity validation.
static std::vector<uint32_t> ReadCategoryIds(const std::string& url,uint32_t catId) {
    const auto response=Skinventory::HttpClient::GetEx(url,
        ClearsReset::NoCacheHeaders);
    if(response.status_code!=200 && response.status_code!=206)return {};
    try {
        return ClearsReset::CategoryIds(AlterEgo::Untrusted::Parse(response.body,AlterEgo::Untrusted::PublicApi),catId);
    } catch (...) {return {};}
}

static std::vector<uint32_t> FetchCategoryIds(uint32_t catId,int64_t requestToken,
                                            const std::vector<uint32_t>& prior) {
    auto ids=ReadCategoryIds(ClearsReset::FreshCategoryUrl(catId,requestToken),catId);
    if(!ClearsReset::LooksUnrotated(prior,ids))return ids;
    // The alternate documented bulk endpoint uses an explicit schema and
    // language. It may avoid stale representation caches; never promote an
    // unchanged result just because it came from another URL.
    AlterEgo::Background::Checkpoint();
    auto alternate=ReadCategoryIds(ClearsReset::AlternateCategoryUrl(catId,requestToken),catId);
    if(!ClearsReset::LooksUnrotated(prior,alternate))return alternate;
    return ids.empty()?alternate:ids;
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
        auto j = AlterEgo::Untrusted::Parse(json,AlterEgo::Untrusted::PublicApi);
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
static void FetchClears(bool manual) {
    if(g_ClearsFetching||g_ClearsCompletionInFlight.load()||g_ClearsQueryPending.load())return;
    const auto account=GetEffectiveAccountName();
    if(account.empty())return;
    uint64_t generation=0;
    std::array<bool,4> request{};
    std::array<std::vector<uint32_t>,2> prior;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if(account!=g_ClearsCacheAccount||g_ClearsFetching||g_ClearsCompletionInFlight||g_ClearsQueryPending)return;
        const auto now=std::chrono::system_clock::now();
        for(size_t i=0;i<request.size();++i){
            const auto reset=ResetEpoch(i<2?CalcLastDailyReset(now):CalcLastWeeklyReset(now));
            request[i]=ClearsReset::GracePassed(reset,ResetEpoch(now))&&
                (manual||((ClearDefinitionsMissing(i)||g_ClearsSectionRetry[i].at!=0)&&g_ClearsSectionRetry[i].Ready(NowMs())));
        }
        if(std::none_of(request.begin(),request.end(),[](bool x){return x;}))return;
        prior={g_ClearsPriorDailyFractalIds,g_ClearsPriorDailyBountyIds};
        generation=++g_ClearsGeneration;g_ClearsFetching=true;
        g_ClearsStatusMsg="Fetching achievement data...";
    }
    AlterEgo::Background::Workers().Launch([manual,account,generation,request,prior]{
        AlterEgo::ApiActivity::ReasonScope reason(manual?AlterEgo::ApiActivity::Reason::Manual:AlterEgo::ApiActivity::Reason::Objective);
        bool any=false;
        auto publish=[&](size_t section,std::vector<ClearEntry> entries,const std::string& error){
            AlterEgo::Background::Checkpoint();
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            if(generation!=g_ClearsGeneration||account!=g_ClearsCacheAccount||!g_ClearsFetching)return false;
            if(!error.empty()){
                g_ClearsSectionRetry[section].Fail(NowMs(),error);
                g_ClearsStatusMsg=std::string(ClearLabels[section])+": "+error;
                UpdateClearsRetryLocked();return true;
            }
            // A successful definition refresh does not verify account progress.
            // Keep already-verified progress for unchanged IDs until the shared
            // authenticated completion response commits.
            auto adopt=[&](std::vector<ClearEntry>& old){
                const bool same=DailyIds(old)==DailyIds(entries);
                if(same)for(auto& e:entries){
                    auto it=std::find_if(old.begin(),old.end(),[&](const auto& v){return v.id==e.id;});
                    if(it!=old.end()){e.done=it->done;e.current=it->current;e.max=it->max;
                        if(e.bitNames==it->bitNames)e.bitDone=it->bitDone;
                        else g_ClearsSectionSuccess[section]=0;}
                }
                if(!same)g_ClearsSectionSuccess[section]=0;
                old=std::move(entries);
            };
            if(section==0){adopt(g_DailyFractals);g_ClearsPriorDailyFractalIds.clear();g_ClearsPriorDailyT4Names.clear();}
            else if(section==1){adopt(g_DailyBounties);g_ClearsPriorDailyBountyIds.clear();}
            else if(section==2)adopt(g_WeeklyWings);
            else{
                auto& e=entries.front();
                if(e.id==g_WeeklyStrikes.id){e.done=g_WeeklyStrikes.done;e.current=g_WeeklyStrikes.current;e.max=g_WeeklyStrikes.max;
                    if(e.bitNames==g_WeeklyStrikes.bitNames)e.bitDone=g_WeeklyStrikes.bitDone;else g_ClearsSectionSuccess[3]=0;}
                else g_ClearsSectionSuccess[3]=0;
                g_WeeklyStrikes=std::move(e);
            }
            g_ClearsSectionRetry[section].Reset();UpdateClearsRetryLocked();any=true;return true;
        };
        for(size_t i=0;i<2;++i)if(request[i]){
            AlterEgo::Background::Checkpoint();
            const auto ids=FetchCategoryIds(i==0?CAT_DAILY_FRACTALS:CAT_DAILY_BOUNTIES,ClearsReset::RequestToken(),prior[i]);
            if(ClearsReset::LooksUnrotated(prior[i],ids)){
                if(!publish(i,{},ids.empty()?"Missing category response (both API routes checked)":"API returned prior IDs (both routes checked)"))return;
                continue;
            }
            auto entries=FetchAchievementDetails(ids);
            if(!AlterEgo::Identity::ExactIds(ids,DailyIds(entries))){
                if(!publish(i,{},"Incomplete achievement definitions"))return;continue;
            }
            if(i==0)for(auto& e:entries)e.tier=ParseFractalTier(e.name);
            if(!publish(i,std::move(entries),{}))return;
        }
        if(request[2]||request[3]){
            AlterEgo::Background::Checkpoint();
            // Weekly raids and strikes share category 477, but neither
            // depends on the other's detail request succeeding.
            const auto ids=FetchCategoryIds(CAT_WEEKLY_RAIDS,ClearsReset::RequestToken(),{});
            for(size_t i=2;i<4;++i)if(request[i]){
                std::vector<uint32_t> wanted;
                for(auto id:ids)if((id==ACH_WEEKLY_STRIKES)==(i==3))wanted.push_back(id);
                if(wanted.empty()){if(!publish(i,{},"Missing weekly category IDs"))return;continue;}
                auto entries=FetchAchievementDetails(wanted);
                if(!AlterEgo::Identity::ExactIds(wanted,DailyIds(entries))){
                    if(!publish(i,{},"Incomplete achievement definitions"))return;continue;
                }
                if(i==2){
                    entries.erase(std::remove_if(entries.begin(),entries.end(),[](const auto& e){return e.bitNames.empty();}),entries.end());
                    static const std::unordered_map<uint32_t,int> wingOrder={{9128,1},{9147,2},{9182,3},{9144,4},{9111,5},{9120,6},{9156,7},{9181,8}};
                    std::sort(entries.begin(),entries.end(),[](const auto& x,const auto& y){
                        const auto a=wingOrder.find(x.id),b=wingOrder.find(y.id);
                        return (a==wingOrder.end()?99:a->second)<(b==wingOrder.end()?99:b->second);
                    });
                }
                if(entries.empty()){if(!publish(i,{},"Missing weekly encounter definitions"))return;continue;}
                if(!publish(i,std::move(entries),{}))return;
            }
        }
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if(generation!=g_ClearsGeneration||account!=g_ClearsCacheAccount)return;
        g_ClearsFetching=false;
        if(any||(manual&&(!g_DailyFractals.empty()||!g_DailyBounties.empty()||!g_WeeklyWings.empty()||g_WeeklyStrikes.id))){
            g_ClearsPendingManual=manual;g_ClearsQueryPending=true;
            g_ClearsStatusMsg="Querying completion...";
        }
    },[account,generation,request]{
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if(generation!=g_ClearsGeneration||account!=g_ClearsCacheAccount)return;
        g_ClearsFetching=false;
        if(!AlterEgo::Background::Workers().Stopping()){
            for(size_t i=0;i<request.size();++i)if(request[i]&&(ClearDefinitionsMissing(i)||g_ClearsSectionRetry[i].at!=0))
                g_ClearsSectionRetry[i].Fail(NowMs(),"Definition refresh interrupted");
            UpdateClearsRetryLocked();
        }
    });
}

// =========================================================================
// Clears - Persistence
// =========================================================================

// Caller owns g_ClearsMutex and the dataset save lock.
static nlohmann::json CaptureClearsCacheLocked() {

    nlohmann::json j;
    // Store fetch timestamp as seconds since epoch
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

    {
        j["account"]=g_ClearsCacheAccount;
        j["fetch_time"] = (int64_t)g_ClearsLastSuccessTime;

        nlohmann::json fractals = nlohmann::json::array();
        for (const auto& e : g_DailyFractals) fractals.push_back(serializeEntry(e));
        if(g_ClearsSectionSuccess[0]>0&&!fractals.empty()){j["dailyFractals"]=fractals;j["section_stamps"][ClearKeys[0]]=g_ClearsSectionSuccess[0];}

        nlohmann::json bounties = nlohmann::json::array();
        for (const auto& e : g_DailyBounties) bounties.push_back(serializeEntry(e));
        if(g_ClearsSectionSuccess[1]>0&&!bounties.empty()){j["dailyBounties"]=bounties;j["section_stamps"][ClearKeys[1]]=g_ClearsSectionSuccess[1];}

        nlohmann::json wings = nlohmann::json::array();
        for (const auto& e : g_WeeklyWings) wings.push_back(serializeEntry(e));
        if(g_ClearsSectionSuccess[2]>0&&!wings.empty()){j["weeklyWings"]=wings;j["section_stamps"][ClearKeys[2]]=g_ClearsSectionSuccess[2];}

        if(g_ClearsSectionSuccess[3]>0&&g_WeeklyStrikes.id){j["weeklyStrikes"]=serializeEntry(g_WeeklyStrikes);j["section_stamps"][ClearKeys[3]]=g_ClearsSectionSuccess[3];}
    }

    return j;
}
static bool PersistClearsCache(const std::string& account,const nlohmann::json& snapshot){
    return AlterEgo::Cache::WriteClearsSections(AlterEgo::GW2API::GetDataDirectory(),account,snapshot);
}

void LoadClearsCache() {
    const std::string account = GetEffectiveAccountName();
    if (account.empty()) return;
    std::lock_guard<std::mutex> saving(g_ClearsSaveMutex);
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        g_ClearsCacheAccount=account;
    }
    const auto snapshot=AlterEgo::Cache::ReadCompletion(AlterEgo::GW2API::GetDataDirectory(),
        AlterEgo::Cache::CompletionDataset::Clears,account,
        false);
    if(!snapshot)return;

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
        const auto& j=*snapshot;

        const int64_t fetchEpoch=j.value("fetch_time",int64_t{0});
        const auto now=std::chrono::system_clock::now();
        const auto dailyReset=CalcLastDailyReset(now),weeklyReset=CalcLastWeeklyReset(now);
        const auto dailyGrace=dailyReset+std::chrono::seconds(ClearsReset::kResetBufferSeconds);
        const auto weeklyGrace=weeklyReset+std::chrono::seconds(ClearsReset::kResetBufferSeconds);
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if(g_ClearsCacheAccount!=account)return;
        g_ClearsLastSuccessTime=static_cast<std::time_t>(fetchEpoch);g_ClearsSectionSuccess={};
        g_DailyFractals.clear();g_DailyBounties.clear();g_WeeklyWings.clear();g_WeeklyStrikes={};
        g_ClearsPriorDailyFractalIds.clear();g_ClearsPriorDailyBountyIds.clear();g_ClearsPriorDailyT4Names.clear();
        g_ClearsSectionRetry={};g_ClearsProgressRetry={};g_ClearsPriorWeekly={};
        for(size_t i=0;i<4;++i){
            if(!j.contains(ClearKeys[i]))continue;
            const auto stamp=j.contains("section_stamps")?j.at("section_stamps").value(ClearKeys[i],fetchEpoch):fetchEpoch;
            const auto time=std::chrono::system_clock::from_time_t(static_cast<std::time_t>(stamp));
            const auto grace=i<2?dailyGrace:weeklyGrace;
            const bool stale=time<grace&&now>=grace;
            if(i==3){
                auto strike=deserializeEntry(j.at(ClearKeys[i]));
                if(!stale){g_WeeklyStrikes=std::move(strike);if(g_WeeklyStrikes.id)g_ClearsSectionSuccess[i]=stamp;}
                else if(strike.id&&stamp>0&&CalcLastWeeklyReset(time)+std::chrono::hours(168)==weeklyReset)g_ClearsPriorWeekly[1]={std::move(strike)};
                continue;
            }
            std::vector<ClearEntry> entries;std::set<uint32_t> seen;
            for(const auto& ej:j.at(ClearKeys[i])){
                auto e=deserializeEntry(ej);if(e.id&&seen.insert(e.id).second)entries.push_back(std::move(e));
            }
            if(stale){
                // Only the immediately preceding day's fingerprint can prove
                // a held-over rotation; older rotations may legitimately recur.
                if(i<2&&stamp>0&&CalcLastDailyReset(time)+std::chrono::hours(24)==dailyReset){
                    if(i==0){g_ClearsPriorDailyFractalIds=DailyIds(entries);
                        for(const auto& e:entries)if(e.tier=="T4")g_ClearsPriorDailyT4Names.push_back(ShortenFractalName(e.name));}
                    else g_ClearsPriorDailyBountyIds=DailyIds(entries);
                }
                if(i==2&&stamp>0&&CalcLastWeeklyReset(time)+std::chrono::hours(168)==weeklyReset)g_ClearsPriorWeekly[0]=std::move(entries);
                continue;
            }
            if(entries.empty())continue;
            if(i==0)g_DailyFractals=std::move(entries);
            else if(i==1)g_DailyBounties=std::move(entries);
            else g_WeeklyWings=std::move(entries);
            g_ClearsSectionSuccess[i]=stamp;
        }
        UpdateClearsRetryLocked();
        g_ClearsFetched=std::any_of(g_ClearsSectionSuccess.begin(),g_ClearsSectionSuccess.end(),[](auto stamp){return stamp>0;});
        if(g_ClearsFetched)g_LastClearsCompletionQuery=std::chrono::steady_clock::now();
        // Cache load already applied reset invalidation per section.
        g_LastDailyReset=std::chrono::system_clock::from_time_t(static_cast<std::time_t>(ClearsReset::InitialMarker(ResetEpoch(dailyReset),ResetEpoch(now),86400)));
        g_LastWeeklyReset=std::chrono::system_clock::from_time_t(static_cast<std::time_t>(ClearsReset::InitialMarker(ResetEpoch(weeklyReset),ResetEpoch(now),604800)));

        // Legacy input has already been persisted before reset filtering.
        // Restore the account's disk cache only. Do not launch the HTTP
        // refresh from startup or account-cache synchronization; entering
        // the actual Clears tab (or pressing Refresh) owns that request.
    } catch (...) {}
}

// =========================================================================
// Clears - Wizard's Vault persistence
// =========================================================================

// Caller owns g_ClearsMutex and the dataset save lock.
static nlohmann::json CaptureVaultCacheLocked() {

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

    nlohmann::json j;
    {
        j["account"]=g_VaultCacheAccount;
        const VaultPeriod* periods[]={&g_VaultDaily,&g_VaultWeekly,&g_VaultSpecial};
        for(uint8_t i=0;i<3;++i)if(g_VaultSectionSuccess[i]>0){
            auto row=serializePeriod(*periods[i]);row["fetched_at"]=static_cast<int64_t>(g_VaultSectionSuccess[i]);
            j[VaultSectionName(i)]=std::move(row);
        }
        j["season_end"] = std::chrono::duration_cast<std::chrono::seconds>(
            g_VaultSeasonEnd.time_since_epoch()).count();
    }

    return j;
}
static bool PersistVaultCache(const std::string& account,const nlohmann::json& snapshot){
    return AlterEgo::Cache::WriteVaultPeriods(AlterEgo::GW2API::GetDataDirectory(),account,snapshot);
}

void LoadVaultCache() {
    const std::string account = GetEffectiveAccountName();
    if (account.empty()) return;
    std::lock_guard<std::mutex> saving(g_VaultSaveMutex);
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        g_VaultCacheAccount=account;
    }
    const auto snapshot=AlterEgo::Cache::ReadCompletion(AlterEgo::GW2API::GetDataDirectory(),
        AlterEgo::Cache::CompletionDataset::Vault,account,
        false);
    if(!snapshot)return;

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
        const auto& j=*snapshot;
        std::array<std::time_t,3> stamps{};
        for(uint8_t i=0;i<3;++i)if(j.contains(VaultSectionName(i)))stamps[i]=static_cast<std::time_t>(j.at(VaultSectionName(i)).value("fetched_at",int64_t{0}));
        auto dailyTime=std::chrono::system_clock::from_time_t(stamps[0]);
        auto weeklyTime=std::chrono::system_clock::from_time_t(stamps[1]);
        auto now         = std::chrono::system_clock::now();
        auto dailyReset  = CalcLastDailyReset(now)  + std::chrono::seconds(60);
        auto weeklyReset = CalcLastWeeklyReset(now) + std::chrono::seconds(60);
        bool dailyStale  = dailyTime < dailyReset  && now >= dailyReset;
        bool weeklyStale = weeklyTime < weeklyReset && now >= weeklyReset;

        {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            if(g_VaultCacheAccount!=account)return;
            g_VaultSectionSuccess=stamps;
            // Keep a fingerprint of the previous day's objectives for the
            // post-reset API freshness check. Never display stale daily rows.
            if (dailyStale && j.contains("daily")) {
                if(stamps[0]>0&&CalcLastDailyReset(dailyTime)+std::chrono::hours(24)==CalcLastDailyReset(now))g_VaultPriorDaily=deserializePeriod(j["daily"]);
                g_VaultDailyRolloverPending = true;
                g_VaultSectionRetry[0].Reset();
            }
            if (weeklyStale && j.contains("weekly")) {
                if(stamps[1]>0&&CalcLastWeeklyReset(weeklyTime)+std::chrono::hours(168)==CalcLastWeeklyReset(now))g_VaultPriorWeekly=deserializePeriod(j["weekly"]);
                g_VaultWeeklyRolloverPending = true;
                g_VaultSectionRetry[1].Reset();
            }
            if (stamps[0]>0||stamps[1]>0) {
                g_VaultLastDailyReset = CalcLastDailyReset(dailyTime);
                g_VaultLastWeeklyReset = CalcLastWeeklyReset(weeklyTime);
                if (now < dailyReset)
                    g_VaultLastDailyReset = dailyReset
                        - std::chrono::hours(24) - std::chrono::seconds(60);
                if (now < weeklyReset)
                    g_VaultLastWeeklyReset = weeklyReset
                        - std::chrono::hours(24 * 7) - std::chrono::seconds(60);
            }
            if (!dailyStale  && j.contains("daily"))   g_VaultDaily   = deserializePeriod(j["daily"]);
            if (!weeklyStale && j.contains("weekly"))  g_VaultWeekly  = deserializePeriod(j["weekly"]);
            if (j.contains("special"))                 g_VaultSpecial = deserializePeriod(j["special"]);
            int64_t seasonEndEpoch = j.value("season_end", (int64_t)0);
            if (seasonEndEpoch > 0)
                g_VaultSeasonEnd = std::chrono::system_clock::from_time_t((time_t)seasonEndEpoch);

            if(dailyStale)g_VaultSectionSuccess[0]=0;
            if(weeklyStale)g_VaultSectionSuccess[1]=0;
            if(seasonEndEpoch>0&&now>=g_VaultSeasonEnd){g_VaultPriorSpecial=g_VaultSpecial;g_VaultSpecialResetEpoch=seasonEndEpoch;g_VaultSpecialRolloverPending=true;g_VaultSpecial={};g_VaultSectionSuccess[2]=0;}
            if(g_PublicVaultSeasonEnd>g_VaultSeasonEnd){
                if(seasonEndEpoch>0&&stamps[2]<seasonEndEpoch){if(!g_VaultSpecialRolloverPending)g_VaultPriorSpecial=g_VaultSpecial;g_VaultSpecialResetEpoch=seasonEndEpoch;g_VaultSpecialRolloverPending=true;g_VaultSpecial={};g_VaultSectionSuccess[2]=0;}
                g_VaultSeasonEnd=g_PublicVaultSeasonEnd;
            }
            UpdateVaultFreshness();
        }

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
        {"Destroy the conjured amalgamate.",                               "CA"},
        {"Make your way through the sorting and appraisal rooms.",         "Sorting"},
        {"Defeat the twin largos.",                                        "Twins"},
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

// Raid/strike rows now use CompletionTheme styling; historical loading-screen
// JPEG banner resources are unused and deliberately excluded from this DLL.

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
        {"Bears",                                    "Voice of the Fallen and Claw of the Fallen"},
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
// Retain the bannerKey argument for existing caller compatibility only.
// Unified one-line clear cell. Background artwork made the trackers visually
// inconsistent and obscured labels; raid dates and completion remain visible.
static ImU32 BountyDateColor(const std::string& date,
                             bool dailyCompletedToday = false) {
    // The day's color is determined by GW2's UTC calendar, never by the
    // WEEKLY encounter checkmark. Today's claimed daily becomes gray;
    // remaining daily today is orange, future daily is teal.
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string today = ClearsRotation::BountyDateLabel(
        ClearsRotation::FloorDay(now) * ClearsRotation::kDay);
    if (date == today)
        return dailyCompletedToday ? AlterEgo::CompletionTheme::kCompleteText
                                   : AlterEgo::CompletionTheme::kBountyToday;
    return AlterEgo::CompletionTheme::kBountyFuture;
}

static void RenderClearBanner(const std::string& title, const std::string& subtitle,
                              bool fetched, bool done, ThemeGrad theme,
                              const char* tooltip = nullptr,
                              float widthOverride = 0.0f,
                              const std::string& bannerKey = "",
                              const std::string& bountyDate = "",
                              const char* fullTooltipTitle = nullptr,
                              bool dailyCompletedToday = false) {
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
            BountyDateColor(bountyDate, dailyCompletedToday), suffix.c_str());
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

struct WingEncChip {
    std::string label, fullName;
    bool done = false;                  // WEEKLY encounter completion
    bool secondary = false;
    std::string bountyDate;             // independent DAILY bounty date
    bool dailyCompletedToday = false;   // only claimed today's bounty is gray
};

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
            BountyDateColor(chip.bountyDate, chip.dailyCompletedToday), date.c_str());
    }
    dl->PopClipRect();
}


// Pack visible daily cards by measured content width; Rec has fixed width.
// Preserve the exact recommended scales and original card IDs.
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
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float scale = ImGui::GetFontSize() / 13.0f;
    const float pad = 7.0f * scale, xgap = 6.0f * scale, ygap = 5.0f * scale;
    const float cardHeight = 27.0f * scale, chipGap = 3.0f * scale;
    const float chipHeight = 20.0f * scale;
    const float chipWidth = std::max(ImGui::CalcTextSize("T4").x,
                                     ImGui::CalcTextSize("100").x) + 19.0f * scale;
    std::vector<FractalCardLayout::Card> measures;
    for (size_t i = 0; i < cards.size(); ++i) {
        const auto& card = cards[i];
        const int slots = card.recommended ? 3 : 4;
        const char* label = card.recommended ? "Rec" : card.title.c_str();
        const float minimum = ImGui::CalcTextSize(label).x + 23.0f * scale
            + slots * chipWidth + (slots - 1) * chipGap;
        measures.push_back({i, minimum, card.recommended,
            !hideComplete || !FractalCardComplete(card, fetched)});
    }
    const auto layout = FractalCardLayout::Pack(measures, width - 2.0f * pad, xgap);
    if (layout.cards.empty()) {
        if (std::none_of(measures.begin(), measures.end(), [](const auto& c) { return c.visible; }))
            ImGui::TextDisabled("  All daily fractals and recommendations completed.");
        return;
    }
    const float height = 2.0f * pad + layout.rows * cardHeight + (layout.rows - 1) * ygap;
    AlterEgo::CompletionTheme::DrawGroup(
        draw, origin, ImVec2(origin.x + width, origin.y + height));

    for (const auto& placement : layout.cards) {
        const size_t i = placement.index;
        const auto& card = cards[i];
        const bool complete = FractalCardComplete(card, fetched);
        const ImVec2 cmin(origin.x + pad + placement.x,
                          origin.y + pad + placement.row * (cardHeight + ygap));
        const ImVec2 cmax(cmin.x + placement.width, cmin.y + cardHeight);
        AlterEgo::CompletionTheme::DrawRow(draw, cmin, cmax, complete);

        const int slots = card.recommended ? 3 : 4;
        // Exceptionally narrow windows may require clipping the name;
        // keep status hitboxes inside their card and retain full tooltips.
        const float edge = std::min(6.0f * scale, placement.width * 0.1f);
        const float actualGap = std::min(chipGap, placement.width / (slots * 4.0f));
        const float actualWidth = std::max(0.0f, std::min(chipWidth,
            (placement.width - 2.0f * edge - (slots - 1) * actualGap) / slots));
        const float chipsWidth = slots * actualWidth + (slots - 1) * actualGap;
        const float chipStart = cmax.x - edge - chipsWidth;
        const float centerY = (cmin.y + cmax.y) * 0.5f;
        const float lineH = ImGui::GetTextLineHeight();
        // Entire card title dims when all constituent tiers are complete.
        if (chipStart - 6.0f * scale > cmin.x + 7.0f * scale) {
            draw->PushClipRect(ImVec2(cmin.x + 7.0f * scale, cmin.y),
                               ImVec2(chipStart - 6.0f * scale, cmax.y), true);
            draw->AddText(ImVec2(cmin.x + 7.0f * scale, centerY - lineH * 0.5f),
                          AlterEgo::CompletionTheme::Text(complete),
                          card.recommended ? "Rec" : card.title.c_str());
            draw->PopClipRect();
        }

        ImGui::PushID(static_cast<int>(i));
        // The card title is a hover target as well as its individual chips.
        // Keep this hit box entirely to the left of the first tier chip.
        ImGui::SetCursorScreenPos(ImVec2(cmin.x + 5.0f * scale, cmin.y));
        const float titleWidth = chipStart - cmin.x - 12.0f * scale;
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
            const float x = chipStart + tier * (actualWidth + actualGap);
            const ImVec2 slotMin(x, centerY - chipHeight * 0.5f);
            const ImVec2 slotMax(x + actualWidth, centerY + chipHeight * 0.5f);
            if (actualWidth <= 0.0f) continue;
            draw->AddRectFilled(slotMin, slotMax,
                done ? AlterEgo::CompletionTheme::kCompletedIndicator
                     : AlterEgo::CompletionTheme::kAvailableIndicator, 2.0f);
            // Each slot reserves exactly the same space for its check,
            // whether it is completed or not. Labels never shift.
            AlterEgo::CompletionTheme::DrawCompleteMark(
                draw, ImVec2(x + std::min(7.0f * scale, actualWidth * 0.5f), centerY), done,
                std::min(3.0f * scale, actualWidth * 0.4f));
            const std::string& text = card.labels[tier];
            const ImU32 ink = !entry ? AlterEgo::CompletionTheme::kUnknownText
                : !fetched ? AlterEgo::CompletionTheme::kUnknownText
                : AlterEgo::CompletionTheme::Text(done);
            draw->PushClipRect(slotMin, slotMax, true);
            draw->AddText(ImVec2(x + 15.0f * scale, centerY - lineH * 0.5f),
                          ink, text.empty() ? "?" : text.c_str());
            draw->PopClipRect();

            ImGui::SetCursorScreenPos(slotMin);
            ImGui::PushID(tier);
            ImGui::InvisibleButton("##fractalStatus", ImVec2(actualWidth, chipHeight));
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
                             uint32_t wingId,
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
    // Make the full wing heading a hover target on narrow windows too.
    const ImVec2 restoredCursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(cmin.x + padX, cmin.y + 2.0f));
    ImGui::InvisibleButton("##wingHeadingHover",
        ImVec2(wsSz.x + 10.0f + nameSz.x, 23.0f));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s - %s\n%d / %d encounters completed",
            WingExpansion(wingId), wingFullName.c_str(), doneCount, totalCount);
    ImGui::SetCursorScreenPos(restoredCursor);
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
        auto drawModifierIcon = [&](uint32_t iconId, const char* tooltip) {
            if (iconX + iconSz > rightLimit) return;
            const ImVec2 min(iconX, iconY);
            const ImVec2 max(iconX + iconSz, iconY + iconSz);
            Texture_t* tex = AlterEgo::IconManager::GetIcon(iconId);
            if (tex && tex->Resource) {
                dl->AddImage(tex->Resource, min, max);
            } else {
                dl->AddText(min, AlterEgo::CompletionTheme::kUnknownText, "?");
            }
            const ImVec2 previous = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(min);
            ImGui::PushID(static_cast<int>(iconId));
            ImGui::InvisibleButton("##weeklyModifier", ImVec2(iconSz, iconSz));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
            ImGui::PopID();
            ImGui::SetCursorScreenPos(previous);
            iconX += iconSz + 6.0f;
        };
        if (isCallOfTheMists)
            drawModifierIcon(AlterEgo::kCallOfTheMistsIcon,
                "Call of the Mists - boss gold and XP are doubled this week.");
        if (isEmboldened)
            drawModifierIcon(AlterEgo::kEmboldenedIcon,
                "Emboldened Mode - stacking buff for easier clears this week.");
    }

    // Encounter chips along the bottom
    float chipsY = cmin.y + h - 22.0f;
    float chipsX = cmin.x + padX;
    float chipsW = w - padX * 2;
    float chipH = 18.0f;
    if (!chips.empty()) {
        float gap = 4.0f;
        std::vector<std::string> labels;
        for (const auto& chip : chips) labels.push_back(chip.label);
        const auto widths = AlterEgo::RaidChipWidths::Calculate(wingId, labels, chipsW, gap);
        float chipX = chipsX;
        for (size_t i = 0; i < chips.size(); i++) {
            const float chipW = widths[i];
            ImVec2 chmin(chipX, chipsY);
            ImVec2 chmax(chmin.x + chipW, chmin.y + chipH);
            chipX += chipW + gap;

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
                                   uint32_t wingId,
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
        ImGui::SetTooltip("%s - %s\n%d / %d encounters completed",
                          WingExpansion(wingId), wingFullName.c_str(),
                          doneCount, totalCount);

    // Keep both weekly modifier icons beside the abbreviated wing label.
    float iconX = min.x + 40.0f;
    auto modifier = [&](bool enabled, uint32_t iconId, const char* tip) {
        if (!enabled) return;
        const float iconSize = 16.0f;
        const ImVec2 iMin(iconX, min.y + (height - iconSize) * 0.5f);
        const ImVec2 iMax(iMin.x + iconSize, iMin.y + iconSize);
        Texture_t* tex = AlterEgo::IconManager::GetIcon(iconId);
        if (tex && tex->Resource) dl->AddImage(tex->Resource, iMin, iMax);
        else dl->AddText(iMin, AlterEgo::CompletionTheme::kUnknownText, "?");
        ImGui::SetCursorScreenPos(iMin);
        ImGui::PushID(static_cast<int>(iconId));
        ImGui::InvisibleButton("##wingModifier", ImVec2(iconSize, iconSize));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::PopID();
        iconX += iconSize + 3.0f;
    };
    modifier(isCallOfTheMists, AlterEgo::kCallOfTheMistsIcon,
             "Call of the Mists - boss gold and XP are doubled this week.");
    modifier(isEmboldened, AlterEgo::kEmboldenedIcon,
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
        std::vector<std::string> labels;
        for (const auto& chip : chips) labels.push_back(chip.label);
        const auto widths = AlterEgo::RaidChipWidths::Calculate(wingId, labels, chipWTotal, gap);
        float chipX = chipStart;
        const float chipY = min.y + (height - 19.0f) * 0.5f;
        for (size_t i = 0; i < chips.size(); ++i) {
            const float chipW = widths[i];
            const ImVec2 cMin(chipX, chipY);
            const ImVec2 cMax(cMin.x + chipW, chipY + 19.0f);
            chipX += chipW + gap;
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

// Assistance is contextual and never changes completion state or starts a query.
static AlterEgo::VaultAssist::Options VaultOptions(const VaultObjective& obj) {
    if (!g_ShowVaultAssistance || obj.claimed || obj.isDone()) return {};
    // The account response remains authoritative for current objective and progress.
    // Resolve the public definition by exact ID; do not apply stale conflicting definitions.
    const auto catalog=AlterEgo::PublicCatalog::Get();
    auto it=catalog->objectives.find(obj.id);
    if(it!=catalog->objectives.end() &&
       AlterEgo::VaultAssist::Normalize(it->second.title)==AlterEgo::VaultAssist::Normalize(obj.title) &&
       AlterEgo::VaultAssist::Normalize(it->second.track)==AlterEgo::VaultAssist::Normalize(obj.track))
        return AlterEgo::VaultAssist::Match(it->second.title,it->second.track);
    return AlterEgo::VaultAssist::Match(obj.title,obj.track);
}
static void RenderVaultAssistance(AlterEgo::VaultAssist::Options options, float width, int64_t utc) {
    using namespace AlterEgo::VaultAssist;
    if (options.empty()) return;
    Sort(options,utc);
    const auto& first = options.front();
    const int next = NextSeconds(first,utc);
    const ImU32 color = next >= 0 && next <= 300 ? IM_COL32(125,210,150,255) :
        next >= 0 && next <= 1800 ? IM_COL32(224,190,119,255) : IM_COL32(157,175,194,255);
    const std::string hint = next >= 0 ? first.label + " - " + Countdown(next) : first.label;
    const float lineH = ImGui::GetTextLineHeight() + 8.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p,ImVec2(p.x+width,p.y+lineH),IM_COL32(35,38,44,175),2.0f);
    const float actionW = 23.0f;
    const bool hasLink = !first.link.empty() && width >= 90.0f;
    const float hintW = ImMax(1.0f,width - actionW * (hasLink ? 2 : 1));
    ImGui::SetCursorScreenPos(p);
    const bool open = ImGui::InvisibleButton("##assist_hint",ImVec2(hintW,lineH));
    const bool hintHovered = ImGui::IsItemHovered();
    dl->PushClipRect(ImVec2(p.x+6,p.y),ImVec2(p.x+hintW-2,p.y+lineH),true);
    dl->AddText(ImVec2(p.x+8,p.y+4),color,hint.c_str());
    dl->PopClipRect();
    if (hintHovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(first.label.c_str());
        if (!first.map.empty()) ImGui::TextDisabled("%s",first.map.c_str());
        ImGui::PushTextWrapPos(340.0f);
        ImGui::TextUnformatted(first.note.c_str());
        ImGui::PopTextWrapPos();
        if (next >= 0) ImGui::TextDisabled("Scheduled start; actual map progress can vary.");
        ImGui::TextDisabled("Click for all suggested locations.");
        ImGui::EndTooltip();
    }
    if (hasLink) {
        ImGui::SetCursorScreenPos(ImVec2(p.x+width-actionW*2,p.y));
        if (ImGui::InvisibleButton("##assist_copy",ImVec2(actionW,lineH))) {
            ImGui::SetClipboardText(first.link.c_str());
            if (APIDefs) APIDefs->GUI_SendAlert("Location chat link copied.");
        }
        const ImVec2 q(p.x+width-actionW*1.5f,p.y+lineH*0.5f);
        const ImU32 icon = IM_COL32(161,184,208,255);
        dl->AddRect(ImVec2(q.x-5,q.y-3),ImVec2(q.x+1,q.y+5),icon,1.0f);
        dl->AddRect(ImVec2(q.x-2,q.y-6),ImVec2(q.x+4,q.y+2),icon,1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy location chat link: %s",first.link.c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x+width-actionW,p.y));
    const bool arrow = ImGui::InvisibleButton("##assist_more",ImVec2(actionW,lineH));
    const ImVec2 center(p.x+width-actionW*0.5f,p.y+lineH*0.5f);
    dl->AddTriangleFilled(ImVec2(center.x-4,center.y-2),ImVec2(center.x+4,center.y-2),
        ImVec2(center.x,center.y+3),IM_COL32(175,175,185,255));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Objective assistance");
    if (open || arrow) ImGui::OpenPopup("##vault_assist_options");
    if (ImGui::BeginPopup("##vault_assist_options")) {
        ImGui::TextColored(ImVec4(0.91f,0.77f,0.48f,1.0f),"Objective Assistance");
        ImGui::TextDisabled("Suggested locations / scheduled starts");
        ImGui::Separator();
        for (size_t i = 0; i < options.size(); ++i) {
            const auto& option = options[i];
            ImGui::PushID(static_cast<int>(i));
            const int time = NextSeconds(option,utc);
            if (!option.link.empty()) {
                if (ImGui::Selectable(option.label.c_str())) {
                    ImGui::SetClipboardText(option.link.c_str());
                    if (APIDefs) APIDefs->GUI_SendAlert("Location chat link copied.");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy %s",option.link.c_str());
            } else ImGui::TextUnformatted(option.label.c_str());
            if (!option.map.empty()) ImGui::TextDisabled("%s",option.map.c_str());
            if (time >= 0) ImGui::TextColored(ImVec4(0.86f,0.74f,0.46f,1.0f),
                "%s%s",time == 0 ? "" : "Scheduled ",Countdown(time).c_str());
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+320.0f);
            ImGui::TextWrapped("%s",option.note.c_str());
            ImGui::PopTextWrapPos();
            if (i+1 < options.size()) ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Copy a location, paste into in-game chat.");
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x,p.y+lineH));
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
                                     const VaultPeriod& period,std::chrono::system_clock::time_point seasonEnd)
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
                                  summary, fullyClaimed,seasonEnd))
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

    // Keep the two-column grid aligned even when only one objective in
    // a pair has a second line. Narrow windows use one column.
    const float availW = ImGui::GetContentRegionAvail().x - 4.0f;
    const float gap = 6.0f;
    const int columns = availW >= 340.0f ? 2 : 1;
    const float cellW = ImMax(1.0f,(availW - gap * (columns-1)) / columns);
    const int64_t utc = static_cast<int64_t>(std::time(nullptr));
    std::vector<size_t> visible;
    for (size_t i = 0; i < period.objectives.size(); ++i)
        if (!g_VaultHideComplete || !period.objectives[i].claimed) visible.push_back(i);
    for (size_t i = 0; i < visible.size(); i += columns) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        float bottom = origin.y;
        for (int column = 0; column < columns && i + column < visible.size(); ++column) {
            const size_t index = visible[i+column];
            const auto& obj = period.objectives[index];
            ImGui::SetCursorScreenPos(ImVec2(origin.x+column*(cellW+gap),origin.y));
            ImGui::PushID(static_cast<int>(index));
            RenderVaultObjectiveRow(obj,cellW);
            auto options = VaultOptions(obj);
            if (!options.empty()) {
                ImGui::SetCursorScreenPos(ImVec2(origin.x+column*(cellW+gap),
                    ImGui::GetCursorScreenPos().y));
                RenderVaultAssistance(std::move(options),cellW,utc);
            }
            bottom = ImMax(bottom,ImGui::GetCursorScreenPos().y);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x,bottom));
        ImGui::Dummy(ImVec2(0,2));
    }

    ImGui::Unindent(4.0f);
}

// GW2's daily calendar rolls over at 00:00 UTC, independent of local time.
// Keep this single helper identical for Vault and Clears.
static void RenderGw2ServerDay() {
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string serverDay = ClearsRotation::BountyDateLabel(
        ClearsRotation::FloorDay(now) * ClearsRotation::kDay);
    ImGui::TextDisabled("GW2 Today: %s (UTC)", serverDay.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Daily reset: 00:00 UTC. Weekly reset: Monday 07:30 UTC. "
                          "GW2 Today can differ from your local date.");
}

static void RenderVaultTab(const std::string& dailyResetStr, const std::string& weeklyResetStr)
{
    // Handle retry/timeout on the render thread; callbacks never call Events_Raise.
    std::vector<uint8_t> dueRetryTypes;
    std::string retryAccount;
    AlterEgo::NativeAccounts::LinkedAccount retryCredential;
    uint64_t retryGeneration=0;
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
                    if (!g_VaultTerminal[i]) {
                        if (!missing.empty()) missing += ", ";
                        missing += VaultSectionName((uint8_t)i);
                    }
                }
                g_VaultStatusMessage = "Vault refresh timed out waiting for: " + missing;
                for(uint8_t i=0;i<3;++i)if(!g_VaultTerminal[i])g_VaultSectionRetry[i].Fail(now,"Request timed out");
            } else if (g_VaultRetryAtMs != 0 && now >= g_VaultRetryAtMs) {
                for (uint8_t i = 0; i < 3; ++i) {
                    if (g_VaultRetryNeeded[i] && !g_VaultResponsesOK[i]) {
                        g_VaultRetryNeeded[i] = false;
                        dueRetryTypes.push_back(i);
                    }
                }
                retryAccount = g_VaultRequestAccount;
                retryCredential=g_VaultRequestCredential;retryGeneration=g_VaultGeneration;
                g_VaultRetryAtMs = 0;
                g_VaultStatusMessage = "Retrying Vault objectives...";
            }
        }
    }
    for (uint8_t type : dueRetryTypes) {
        if (!g_VaultFetching.load()) break;
        RequestVaultSection(type,retryAccount,retryCredential,retryGeneration);
    }

    // Account switches invalidate the old snapshot and cancel pending retries.
    if (g_VaultNeedRequery) {
        g_VaultNeedRequery = false;
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        g_VaultFetching = false;
        g_VaultRetryAtMs = 0;
        g_VaultDaily = {}; g_VaultWeekly = {}; g_VaultSpecial = {};
        g_VaultFetched = false;
        g_VaultSectionSuccess={};UpdateVaultFreshness();
        g_VaultInitialAttempted = false;
        g_VaultLastAttemptFailed = false;
        g_VaultDailyRolloverPending = false;
        g_VaultWeeklyRolloverPending = false;
        g_VaultPriorDaily={};
        g_VaultPriorWeekly = {};
        g_VaultSectionRetry={};g_VaultSpecialRolloverPending=false;g_VaultPriorSpecial={};g_VaultSpecialResetEpoch=0;
        g_VaultStatusMessage.clear();
    }

    {
        static auto lastCheck=std::chrono::steady_clock::time_point{};
        const auto now=std::chrono::steady_clock::now();
        if(AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Vault)&&
            !g_VaultFetching.load()&&now-lastCheck>=std::chrono::seconds(2)){
            lastCheck=now;FetchVaultData(true);
        }
    }

    LoadClearUiSettings();

    RenderGw2ServerDay();
    ImGui::Separator();
    ImGui::BeginChild("##VaultContentScroll", ImVec2(0, 0), false);

    VaultPeriod daily,weekly,special;
    std::chrono::system_clock::time_point seasonEnd;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        daily=g_VaultDaily;weekly=g_VaultWeekly;special=g_VaultSpecial;seasonEnd=g_VaultSeasonEnd;
    }

    RenderVaultPeriodSection(0, "Daily Objectives", dailyResetStr.c_str(),
        ImVec4(0.61f, 0.43f, 0.92f, 1.0f), daily,seasonEnd);

    ImGui::Spacing();
    ImGui::Separator();

    RenderVaultPeriodSection(1, "Weekly Objectives", weeklyResetStr.c_str(),
        ImVec4(0.91f, 0.78f, 0.29f, 1.0f), weekly,seasonEnd);

    { // Always show Special, including pending/empty seasonal data.
        ImGui::Spacing();
        ImGui::Separator();
        std::string seasonLabel = "seasonal";
        if (seasonEnd != std::chrono::system_clock::time_point{}) {
            auto sNow = std::chrono::system_clock::now();
            if (seasonEnd > sNow) {
                auto rem = std::chrono::duration_cast<std::chrono::seconds>(seasonEnd - sNow).count();
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
            ImVec4(0.9f, 0.70f, 0.30f, 1.0f), special,seasonEnd);
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
    return AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(), "clears_ui.json");
}
static void LoadClearUiSettings() {
    if (g_ClearUiLoaded) return;
    g_ClearUiLoaded = true;
    // Older backups and missing settings keep the original visible default.
    g_ShowFractalNextResets = true;
    g_ShowVaultAssistance = true;
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
        if (j.contains("show_vault_assistance") && j["show_vault_assistance"].is_boolean())
            g_ShowVaultAssistance = j["show_vault_assistance"].get<bool>();
        g_ClearsHideComplete = j.value("hide_clears_complete", false);
        if (j.contains("show_fractal_next_resets") && j["show_fractal_next_resets"].is_boolean())
            g_ShowFractalNextResets = j["show_fractal_next_resets"].get<bool>();
    } catch (...) {}
}
void ReloadClearUiSettings() {
    g_ClearUiLoaded = false;
    LoadClearUiSettings();
}

static void SaveClearUiSettings() {
    try {
        auto path = ClearUiSettingsPath();
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        nlohmann::json j;
        j["expanded"] = {g_ClearOpen[0], g_ClearOpen[1], g_ClearOpen[2], g_ClearOpen[3]};
        j["vault_expanded"] = {g_VaultOpen[0], g_VaultOpen[1], g_VaultOpen[2]};
        j["hide_vault_complete"] = g_VaultHideComplete;
        j["show_vault_assistance"] = g_ShowVaultAssistance;
        j["hide_clears_complete"] = g_ClearsHideComplete;
        j["show_fractal_next_resets"] = g_ShowFractalNextResets;
        std::ofstream f(path, std::ios::trunc);
        if (f.good()) f << j.dump(2);
    } catch (...) {}
}
static bool RenderVaultSectionToggle(int index, const char* title,
                                     ImVec4 color, const char* suffix,
                                     const char* summary,
                                     bool summaryComplete,std::chrono::system_clock::time_point seasonEnd) {
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
        seasonEnd != std::chrono::system_clock::time_point{} &&
        ImGui::IsItemHovered()) {
        const std::time_t tt = std::chrono::system_clock::to_time_t(seasonEnd);
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
                                       bool completionKnown,
                                       bool* dailyCompletedToday = nullptr) {
    if (dailyCompletedToday) *dailyCompletedToday = false;
    if (!completionKnown) return "";
    const std::string date = ClearsRotation::NextBountyDate(
        encounter, utcNow, weeklyEndUtc);
    if (date.empty()) return "";

    // Only check today's DAILY achievement, never the weekly encounter bit.
    const int64_t todayUtc = ClearsRotation::FloorDay(utcNow) * ClearsRotation::kDay;
    if (dailyCompletedToday && date == ClearsRotation::BountyDateLabel(todayUtc)) {
        const std::string boss = ClearsRotation::Normalize(encounter);
        for (const auto& entry : daily) {
            if (ClearsRotation::Normalize(entry.name) == boss && entry.done) {
                *dailyCompletedToday = true;
                break;
            }
        }
    }
    return date;
}

static void RenderClearsTabContent(const std::string& dailyResetStr, const std::string& weeklyResetStr)
{
    LoadClearUiSettings();

    RenderGw2ServerDay();
    ImGui::Separator();
    ImGui::BeginChild("##ClearsContentScroll", ImVec2(0, 0), false);

    std::vector<ClearEntry> dailyFractals,dailyBounties,weeklyWings;
    ClearEntry weeklyStrikes;
    std::vector<std::string> priorT4Names;
    bool fetched,rolloverPending;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        dailyFractals=g_DailyFractals;dailyBounties=g_DailyBounties;
        weeklyWings=g_WeeklyWings;weeklyStrikes=g_WeeklyStrikes;
        priorT4Names=g_ClearsPriorDailyT4Names;
        fetched=g_ClearsFetched;rolloverPending=g_ClearsDailyRolloverPending;
    }
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
    if (dailyFractals.empty()) {
        if (rolloverPending) {
            // The hard-coded date anchor can drift. Only derive a forecast
            // from yesterday's verified three T4 objectives, if available.
            const auto* forecast = ClearsRotation::NextFractalsFromLive(
                priorT4Names);
            if (forecast) {
                ImGui::TextDisabled("Forecast from yesterday (unverified; completion unknown):");
                for (const char* name : forecast->dailies)
                    ImGui::BulletText("%s", name);
                ImGui::TextDisabled("Recommended: T1 %d, T2 %d, T3 %d",
                    forecast->recScales[0], forecast->recScales[1], forecast->recScales[2]);
            } else {
                ImGui::TextDisabled("  Today's daily API schedule is pending.");
            }
        } else {
            ImGui::TextDisabled("  No data");
        }
    } else {
        ImGui::Indent(4.0f);
        // Build three daily map cards, then a fourth Recommended card.
        // Status comes from the existing native achievement completion data.
        std::vector<DailyFractalCard> cards;
        for (const auto& e : dailyFractals) {
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
        for (const auto& e : dailyFractals)
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
        RenderDailyFractalCards(cards, fetched,
                                g_ClearsHideComplete && fetched);

        // Uncategorized fractals (no tier) — show as plain rows
        for (const auto& e : dailyFractals) {
            if (!e.tier.empty() || (g_ClearsHideComplete && fetched && e.done)) continue;
            ImGui::Text("  ");
            ImGui::SameLine();
            RenderClearStatus(fetched, e.done, e.name.c_str());
        }
        ImGui::Unindent(4.0f);
    }
    if (fetched && g_ShowFractalNextResets) {
        std::vector<std::string> todaysT4;
        for (const auto& e : dailyFractals)
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
        if (fetched && !dailyBounties.empty()) {
            for (const auto& bounty : dailyBounties)
                if (bounty.done) ++completed;
            snprintf(summary, sizeof(summary), "%d / %zu completed",
                completed, dailyBounties.size());
        }
        showBounties = RenderClearSectionToggle(1, "Daily Raid Bounties",
            ImVec4(0.9f, 0.6f, 0.3f, 1.0f), suffix,
            summary[0] ? summary : nullptr,
            summary[0] && completed == static_cast<int>(dailyBounties.size()));
    }

    if (showBounties) {
    if (dailyBounties.empty()) {
        if (rolloverPending) {
            const int rotation = ClearsRotation::BountySet(utcNow);
            ImGui::TextDisabled("Rotation forecast (not API-verified; completion unknown):");
            for (int column = 0; column < 4; ++column)
                ImGui::BulletText("%s", ClearsRotation::BountyName(rotation, column));
        } else {
            ImGui::TextDisabled("  No data");
        }
    } else {
        ImGui::Indent(4.0f);
        float availW = ImGui::GetContentRegionAvail().x - 4.0f;
        float gapBnty = 6.0f;
        float halfWBnty = (availW - gapBnty) * 0.5f;
        size_t visibleCount = 0;
        for (const auto& e : dailyBounties)
            if (!g_ClearsHideComplete || !fetched || !e.done) ++visibleCount;
        size_t shown = 0;
        for (size_t i = 0; i < dailyBounties.size(); i++) {
            const auto& e = dailyBounties[i];
            if (g_ClearsHideComplete && fetched && e.done) continue;
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
                display = AlterEgo::StrikeDisplayName(strikeName);
                subtitle = AlterEgo::StrikePickerCategory(strikeName);
                bannerKey = "strike:" + strikeName;
                theme = StrikeTheme(strikeName);
            } else {
                subtitle = "Raid Bounty";
                theme = ThemeGrad{ IM_COL32(60, 44, 20, 255), IM_COL32(200, 154, 74, 255) };
            }

            ImGui::PushID((int)i);
            ImVec2 startPos = ImGui::GetCursorScreenPos();
            RenderClearBanner(display, subtitle,
                fetched, e.done, theme,
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
        if (fetched && weeklyStrikes.id != 0 &&
            !weeklyStrikes.bitNames.empty()) {
            for (size_t i = 0; i < weeklyStrikes.bitNames.size(); ++i)
                if (i < weeklyStrikes.bitDone.size() &&
                    weeklyStrikes.bitDone[i]) ++completed;
            snprintf(summary, sizeof(summary), "%d / %zu completed",
                completed, weeklyStrikes.bitNames.size());
        }
        showStrikes = RenderClearSectionToggle(2, "Weekly Strikes",
            ImVec4(0.7f, 0.4f, 0.9f, 1.0f), suffix,
            summary[0] ? summary : nullptr,
            summary[0] && completed == static_cast<int>(weeklyStrikes.bitNames.size()));
    }

    if (showStrikes) {
    if (weeklyStrikes.id == 0) {
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
        pickerOrder.reserve(weeklyStrikes.bitNames.size());
        for (size_t i = 0; i < weeklyStrikes.bitNames.size(); ++i)
            pickerOrder.push_back(i);
        // Presentation order only: original achievement indices remain intact.
        std::stable_sort(pickerOrder.begin(), pickerOrder.end(),
            [&weeklyStrikes](size_t left, size_t right) {
                return AlterEgo::StrikePickerOrder(weeklyStrikes.bitNames[left]) <
                       AlterEgo::StrikePickerOrder(weeklyStrikes.bitNames[right]);
            });

        if (ImGui::BeginTable("##WeeklyStrikesGrid", strikeColumns,
                              ImGuiTableFlags_SizingStretchSame)) {
            for (const size_t i : pickerOrder) {
                const bool bitDone = i < weeklyStrikes.bitDone.size() &&
                    weeklyStrikes.bitDone[i];
                if (g_ClearsHideComplete && fetched && bitDone) continue;

                const std::string& name = weeklyStrikes.bitNames[i];
                // Display aliases only: never change the source achievement
                // name, bit index, theme, or bounty matching.
                const std::string normalized = ClearsRotation::Normalize(name);
                const bool isKela = normalized == "guardiansglade";
                const std::string displayName = AlterEgo::StrikeDisplayName(name);

                // One full encounter title, then its raid picker category;
                // never include historical map/zone locations.
                const char* fullTitle = isKela ? "Guardian's Glade" : name.c_str();
                const char* description = isKela
                    ? "Kela, Seneschal of Waves. Weekly completion is from Guardian's Glade."
                    : nullptr;
                const std::string subtitle = AlterEgo::StrikePickerCategory(name);
                bool dailyCompletedToday = false;
                const std::string date = UpcomingBountyLabel(
                    isKela ? "Kela" : name, dailyBounties,
                    utcNow, weeklyEndUtc, fetched, &dailyCompletedToday);

                if (shown % strikeColumns == 0) ImGui::TableNextRow();
                ImGui::TableSetColumnIndex((int)(shown % strikeColumns));
                ImGui::PushID((int)i);
                RenderClearBanner(displayName, subtitle,
                    fetched, bitDone, StrikeTheme(name),
                    /*tooltip*/ description,
                    /*widthOverride*/ ImGui::GetContentRegionAvail().x,
                    /*bannerKey*/ "strike:" + name, date,
                    /*fullTooltipTitle*/ fullTitle, dailyCompletedToday);
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
        if (fetched && !weeklyWings.empty()) {
            for (const auto& wing : weeklyWings)
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
    if (weeklyWings.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  No data");
    } else {
        ImGui::Indent(4.0f);
        uint32_t embWingId = CurrentEmboldenedWingId();
        uint32_t cotmWingId = CurrentCallOfTheMistsWingId();
        size_t visibleWings = 0;
        for (const auto& wing : weeklyWings) {
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
                if (g_ClearsHideComplete && fetched && bitDone) continue;
                WingEncChip c;
                c.label = shortEnc;
                c.fullName = wing.bitNames[i];
                c.done = bitDone;
                c.secondary = IsSecondaryRaidEncounter(shortEnc);
                const std::string bountyKey =
                    shortEnc == "CA" ? "Conjured Amalgamate" :
                    shortEnc == "Twins" ? "Twin Largos" : shortEnc;
                c.bountyDate = UpcomingBountyLabel(bountyKey, dailyBounties,
                    utcNow, weeklyEndUtc, fetched, &c.dailyCompletedToday);
                chips.push_back(c);
            }

            if (g_ClearsHideComplete && fetched && chips.empty()) continue;
            ++visibleWings;
            ImGui::PushID((int)wing.id);
            if (ImGui::GetContentRegionAvail().x >= 830.0f) {
                RenderCompactWingBanner(shortW ? shortW : "", cleanName,
                    wing.id, doneCount, totalCount, fetched, chips,
                    wing.id == embWingId, wing.id == cotmWingId);
            } else {
                RenderWingBanner(shortW ? shortW : "", cleanName,
                    wing.id, doneCount, totalCount, fetched,
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
    // ---- Clears reset detection ----
    // Do not consume a reset before its API grace period has elapsed. A
    // timestamp recorded during those 60 seconds used to lose the event.
    const int64_t nowEpoch = ResetEpoch(now);
    const int64_t dailyEpoch = ResetEpoch(dailyReset);
    const int64_t weeklyEpoch = ResetEpoch(weeklyReset);
    if (g_LastDailyReset == std::chrono::system_clock::time_point{})
        g_LastDailyReset = std::chrono::system_clock::from_time_t(
            (time_t)ClearsReset::InitialMarker(dailyEpoch, nowEpoch, 86400));
    if (g_LastWeeklyReset == std::chrono::system_clock::time_point{})
        g_LastWeeklyReset = std::chrono::system_clock::from_time_t(
            (time_t)ClearsReset::InitialMarker(weeklyEpoch, nowEpoch, 7 * 86400));

    bool resetTriggered = false;
    if (ClearsReset::Due(ResetEpoch(g_LastDailyReset), dailyEpoch, nowEpoch)) {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        ++g_ClearsGeneration;g_ClearsFetching=false;g_ClearsPendingManual=false;
        if (!g_ClearsDailyRolloverPending) {
            g_ClearsPriorDailyFractalIds = DailyIds(g_DailyFractals);
            g_ClearsPriorDailyBountyIds = DailyIds(g_DailyBounties);
            g_ClearsPriorDailyT4Names.clear();
            for (const auto& e : g_DailyFractals) {
                if (e.tier == "T4")
                    g_ClearsPriorDailyT4Names.push_back(ShortenFractalName(e.name));
            }
        }
        g_ClearsSectionRetry[0].Reset();g_ClearsSectionRetry[1].Reset();
        g_ClearsProgressRetry[0].Reset();g_ClearsProgressRetry[1].Reset();
        g_ClearsSectionSuccess[0]=0;g_ClearsSectionSuccess[1]=0;
        g_DailyFractals.clear();
        g_DailyBounties.clear();UpdateClearsRetryLocked();
        // Preserve the existing weekly state and its completion colors.
        g_ClearsFetched = !g_WeeklyWings.empty() || g_WeeklyStrikes.id != 0;
        g_ClearsQueryPending = false;
        g_ClearsCompletionInFlight = false;
        g_ClearsQueryStartedAtMs = 0;
        g_ClearsStatusMsg = AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Clears)
            ? "New daily reset detected; fetching current objectives..."
            : "Daily reset detected - click Refresh for current objectives";
        g_LastDailyReset = dailyReset;
        resetTriggered = true;
    }
    if (ClearsReset::Due(ResetEpoch(g_LastWeeklyReset), weeklyEpoch, nowEpoch)) {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        ++g_ClearsGeneration;g_ClearsFetching=false;g_ClearsPendingManual=false;
        if(g_ClearsSectionSuccess[2]>0)g_ClearsPriorWeekly[0]=g_WeeklyWings;
        if(g_ClearsSectionSuccess[3]>0&&g_WeeklyStrikes.id)g_ClearsPriorWeekly[1]={g_WeeklyStrikes};
        g_WeeklyWings.clear();
        g_WeeklyStrikes = ClearEntry{};
        g_ClearsSectionRetry[2].Reset();g_ClearsSectionRetry[3].Reset();
        g_ClearsProgressRetry[2].Reset();g_ClearsProgressRetry[3].Reset();
        g_ClearsSectionSuccess[2]=0;g_ClearsSectionSuccess[3]=0;UpdateClearsRetryLocked();
        g_ClearsFetched = !g_DailyFractals.empty() || !g_DailyBounties.empty();
        g_ClearsQueryPending = false;
        g_ClearsCompletionInFlight = false;
        g_ClearsQueryStartedAtMs = 0;
        g_LastWeeklyReset = weeklyReset;
        resetTriggered = true;
    }
    if (!showVault && AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Clears) &&
        resetTriggered && !g_ClearsFetching)
        FetchClears();

    // ---- Vault reset detection ----
    if (g_VaultLastDailyReset == std::chrono::system_clock::time_point{})
        g_VaultLastDailyReset = std::chrono::system_clock::from_time_t(
            (time_t)ClearsReset::InitialMarker(dailyEpoch, nowEpoch, 86400));
    if (g_VaultLastWeeklyReset == std::chrono::system_clock::time_point{})
        g_VaultLastWeeklyReset = std::chrono::system_clock::from_time_t(
            (time_t)ClearsReset::InitialMarker(weeklyEpoch, nowEpoch, 7 * 86400));

    bool vaultResetTriggered = false;
    if (ClearsReset::Due(ResetEpoch(g_VaultLastDailyReset), dailyEpoch, nowEpoch)) {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if (!g_VaultDailyRolloverPending) {
            g_VaultPriorDaily = g_VaultDaily;
            g_VaultSectionRetry[0].Reset();
        }
        ++g_VaultGeneration;g_VaultFetching=false;g_VaultRetryAtMs=0;
        g_VaultDailyRolloverPending = true;
        g_VaultDaily = {};  // do not display previous-day daily objectives
        g_VaultSectionSuccess[0]=0;UpdateVaultFreshness();
        g_VaultInitialAttempted = true; // explicit FetchVaultData/retry owns this batch
        g_VaultStatusMessage = AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Vault)
            ? "New daily reset detected; fetching current Vault..."
            : "Daily reset detected - click Refresh for current Vault";
        g_VaultLastDailyReset = dailyReset;
        vaultResetTriggered = true;
    }
    if (ClearsReset::Due(ResetEpoch(g_VaultLastWeeklyReset), weeklyEpoch, nowEpoch)) {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        if (!g_VaultWeeklyRolloverPending) {
            g_VaultPriorWeekly = g_VaultWeekly;
            g_VaultSectionRetry[1].Reset();
        }
        ++g_VaultGeneration;g_VaultFetching=false;g_VaultRetryAtMs=0;
        g_VaultWeeklyRolloverPending = true;
        g_VaultWeekly = {};  // never display the previous week's claimed objectives
        g_VaultSectionSuccess[1]=0;UpdateVaultFreshness();
        g_VaultInitialAttempted = true; // explicit FetchVaultData/retry owns this batch
        g_VaultStatusMessage = AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Vault)
            ? "New weekly reset detected; fetching current Vault..."
            : "Weekly reset detected - click Refresh for current Vault";
        g_VaultLastWeeklyReset = weeklyReset;
        vaultResetTriggered = true;
    }
    // Do not refresh Vault while showing Clears or on unrelated tab visits.
    if (showVault && AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Vault) &&
        vaultResetTriggered && !g_VaultFetching.load())
        FetchVaultData(true);

    // Delayed retries run on the render thread (the only thread allowed to
    // submit Nexus events), and never overlap with an in-flight fetch.
    // Vault retries are dispatched by its visible section scheduler.


    // A missing API response must not permanently suppress manual or
    // automatic refreshes. An expired query can be replaced after 45s.
    if (g_ClearsCompletionInFlight.load()) {
        const long long started = g_ClearsQueryStartedAtMs.load();
        if (started != 0 && NowMs() - started >= CLEARS_QUERY_TIMEOUT_MS) {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            if(g_ClearsCompletionInFlight&&g_ClearsQueryStartedAtMs==started){
                ++g_ClearsGeneration;
                g_ClearsCompletionInFlight = false;
                g_ClearsQueryStartedAtMs = 0;
                g_ClearsStatusMsg = "Completion query timed out; retry Refresh.";
            }
        }
    }

    // ---- Clears deferred logic ----
    if (!showVault && g_ClearsNeedRequery && !g_ClearsFetching) {
        g_ClearsNeedRequery = false;
        {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            ++g_ClearsGeneration;g_ClearsQueryPending=false;g_ClearsPendingManual=false;
            g_ClearsCompletionInFlight = false;
            g_ClearsQueryStartedAtMs = 0;
            g_ClearsRetryAtMs = 0;
            for (auto& e : g_DailyFractals) { e.done = false; e.current = 0; e.bitDone.assign(e.bitDone.size(), false); }
            for (auto& e : g_DailyBounties) { e.done = false; e.current = 0; e.bitDone.assign(e.bitDone.size(), false); }
            for (auto& e : g_WeeklyWings)   { e.done = false; e.current = 0; e.bitDone.assign(e.bitDone.size(), false); }
            g_WeeklyStrikes.done = false; g_WeeklyStrikes.current = 0;
            g_WeeklyStrikes.bitDone.assign(g_WeeklyStrikes.bitDone.size(), false);
            // Never display completion counts from the previous account,
            // even if the global automatic API refresh option is disabled.
            g_ClearsLastSuccessTime = 0;g_ClearsSectionSuccess={};
            g_ClearsStatusMsg = AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Clears)
                ? "Refreshing completion for new account..."
                : "Account changed - click Refresh to update completions";
        }
        if (AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Clears)) {
            if (g_ClearsFetched) SendClearsAchQuery();
            else FetchClears();
        }
    }
    // Boot synchronization is automatic. Manual FetchClears follow-through
    // uses g_ClearsQueryPending and must run regardless of the policy setting.
    // Boot-time account registry only confirms availability. Never query Clears
    // before this tab is opened, even when a cached account is selected.
    if (!showVault) g_ClearsAutoBootPending = false;
    if (!showVault && !g_ClearsFetching && g_ClearsQueryPending.exchange(false))
        SendClearsAchQuery(false,g_ClearsPendingManual.exchange(false));
    // Fire a scheduled BUSY backoff retry once its delay has elapsed.
    {
        long long retryAt = g_ClearsRetryAtMs.load();
        if (!showVault && retryAt != 0 && NowMs() >= retryAt &&
            !g_ClearsFetching) {
            g_ClearsRetryAtMs = 0;
            bool selective;
            {std::lock_guard<std::mutex> lock(g_ClearsMutex);selective=g_ClearsRetrySelective;}
            SendClearsAchQuery(selective);
        }
    }

    auto steadyNow = std::chrono::steady_clock::now();
    if(!showVault&&AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Clears)&&
        !g_ClearsFetching&&!g_ClearsQueryPending.load()&&!g_ClearsCompletionInFlight.load()){
        bool missing=false,progressDue=false;
        {std::lock_guard<std::mutex> lock(g_ClearsMutex);
            for(size_t i=0;i<4;++i){
                if((ClearDefinitionsMissing(i)||g_ClearsSectionRetry[i].at!=0)&&g_ClearsSectionRetry[i].Ready(NowMs()))missing=true;
                if(!ClearDefinitionsMissing(i)&&(g_ClearsSectionSuccess[i]==0||g_ClearsProgressRetry[i].at!=0)&&g_ClearsProgressRetry[i].Ready(NowMs()))progressDue=true;
            }}
        if(progressDue)SendClearsAchQuery(true);
        else if(missing)FetchClears(); // only due definition sections
    }
    if (AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Clears) &&
        !showVault && g_ClearsFetched && !g_ClearsFetching &&
        !g_ClearsCompletionInFlight.load() && !g_ClearsQueryPending.load() &&
        g_ClearsRetryAtMs.load() == 0 &&
        ([&]() {
            std::lock_guard<std::mutex> lock(g_ClearsMutex);
            if((g_LastClearsCompletionQuery!=std::chrono::steady_clock::time_point{}&&steadyNow-g_LastClearsCompletionQuery<std::chrono::seconds(2)))return false;
            const auto minimum=std::chrono::system_clock::to_time_t(std::max(dailyReset,weeklyReset));
            return AlterEgo::NativeProgress::AchievementDue(g_ClearsCacheAccount,g_ClearsLastSuccessTime,AlterEgo::RefreshEngine::ObjectiveSeconds(),minimum);
        })()) {
        {std::lock_guard<std::mutex> lock(g_ClearsMutex);g_LastClearsCompletionQuery=steadyNow;}
        SendClearsAchQuery(true);
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

// Called globally on startup and whenever the selected account changes, not
// only while Vault/Clears is visible. Each selected account restores its own
// disk cache immediately; the old account's status must never be displayed.
void SyncCompletionAccountCaches() {
    const std::string account = GetEffectiveAccountName();
    if (account == g_ClearsCacheAccount && account == g_VaultCacheAccount) {
        // At cold startup the active account name can become known before
        // account registry confirms there is exactly one configured account. Retry legacy
        // migration once after that confirmation rather than losing the old
        // disk snapshot until the next successful network refresh.
        static std::string legacyCheckedAccount;
        if (!account.empty() && AlterEgo::GW2API::HasAccountsData() &&
            AlterEgo::GW2API::GetAccountCount() == 1 &&
            legacyCheckedAccount != account) {
            legacyCheckedAccount = account;
            const auto root=AlterEgo::GW2API::GetDataDirectory();
            if (!AlterEgo::Cache::HasCompletion(root,AlterEgo::Cache::CompletionDataset::Clears,account))
                LoadClearsCache();
            if (!AlterEgo::Cache::HasCompletion(root,AlterEgo::Cache::CompletionDataset::Vault,account))
                LoadVaultCache();
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        ++g_ClearsGeneration;++g_VaultGeneration;
        g_ClearsCacheAccount = account;
        g_VaultCacheAccount = account;
        g_ClearsNeedRequery = false;
        g_VaultNeedRequery = false;
        g_ClearsFetched = false;
        g_ClearsFetching = false;
        g_ClearsQueryPending = false;
        g_ClearsAutoBootPending = false;
        g_ClearsCompletionInFlight = false;
        g_ClearsQueryStartedAtMs = 0;
        g_ClearsRetryAtMs = 0;
        g_ClearsDailyRolloverRetryAtMs = 0;
        g_ClearsDailyRolloverPending = false;g_ClearsSectionRetry={};g_ClearsProgressRetry={};g_ClearsPriorWeekly={};g_ClearsSectionSuccess={};
        g_ClearsLastSuccessTime = 0;
        g_ClearsSuccessUntil = {};
        g_LastClearsCompletionQuery = {};
        g_DailyFractals.clear();
        g_DailyBounties.clear();
        g_WeeklyWings.clear();
        g_WeeklyStrikes = {};
        g_ClearsPriorDailyFractalIds.clear();
        g_ClearsPriorDailyBountyIds.clear();
        g_ClearsPriorDailyT4Names.clear();
        g_LastDailyReset = {};
        g_LastWeeklyReset = {};
        g_ClearsStatusMsg.clear();

        g_VaultFetching = false;
        g_VaultFetched = false;
        g_VaultInitialAttempted = false;
        g_VaultRequestAccount.clear();g_VaultRequestCredential={};
        g_VaultRetryAtMs = 0;
        g_VaultRequestStartMs = 0;
        g_VaultDailyRolloverPending = false;
        g_VaultWeeklyRolloverPending = false;
        g_VaultSectionRetry={};g_VaultSpecialRolloverPending=false;g_VaultPriorSpecial={};g_VaultSpecialResetEpoch=0;
        g_VaultLastAttemptFailed = false;
        g_VaultSectionSuccess={};UpdateVaultFreshness();
        g_VaultSuccessUntil = {};
        g_VaultResponsesOK[0] = g_VaultResponsesOK[1] = g_VaultResponsesOK[2] = false;
        g_VaultDaily = {};
        g_VaultWeekly = {};
        g_VaultSpecial = {};
        g_VaultTerminal[0]=g_VaultTerminal[1]=g_VaultTerminal[2]=false;g_VaultBatchFailed=false;
        g_VaultSeasonEnd = {};
        g_VaultPriorDaily={};
        g_VaultPriorWeekly = {};
        g_VaultLastDailyReset = {};
        g_VaultLastWeeklyReset = {};
        g_VaultStatusMessage.clear();
    }
    if (!account.empty()) {
        LoadClearsCache();
        LoadVaultCache();
        // Account switching restores the correct cache immediately; the
        // active tab's next render or tab-entry refresh performs network work.
        // Never trigger both Clears and Vault from this global cache sync.
    }
}


bool ShowVaultAssistance() { LoadClearUiSettings(); return g_ShowVaultAssistance; }
void ToggleVaultAssistance() {
    LoadClearUiSettings();
    g_ShowVaultAssistance = !g_ShowVaultAssistance;
    SaveClearUiSettings();
}
bool VaultHideComplete() { LoadClearUiSettings(); return g_VaultHideComplete; }
bool ClearsHideComplete() { LoadClearUiSettings(); return g_ClearsHideComplete; }
bool ShowFractalNextResets() { LoadClearUiSettings(); return g_ShowFractalNextResets; }
void ToggleFractalNextResets() {
    LoadClearUiSettings();
    g_ShowFractalNextResets = !g_ShowFractalNextResets;
    SaveClearUiSettings();
}
void ToggleVaultHideComplete() {
    LoadClearUiSettings();
    g_VaultHideComplete = !g_VaultHideComplete;
    SaveClearUiSettings();
}
void ToggleClearsHideComplete() {
    LoadClearUiSettings();
    g_ClearsHideComplete = !g_ClearsHideComplete;
    SaveClearUiSettings();
}

void RenderVaultStatusRow() {
    // Snapshot worker-thread data before rendering; never keep the lock while
    // invoking a manual refresh, which can initiate new API requests.
    std::time_t updated;
    bool showingData, failed, pending, waitingForRollover;
    std::string status;
    std::chrono::steady_clock::time_point successUntil;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        updated = g_VaultLastSuccessTime;
        showingData = std::any_of(g_VaultSectionSuccess.begin(),g_VaultSectionSuccess.end(),[](auto stamp){return stamp>0;});
        failed = g_VaultLastAttemptFailed;
        pending = g_VaultFetching.load();
        waitingForRollover = AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Vault) &&
            VaultAwaitingRetry();
        status = g_VaultStatusMessage;
        for(uint8_t i=0;i<3;++i){
            status+="\n"+std::string(VaultSectionName(i))+": "+AlterEgo::RefreshUI::Age(g_VaultSectionSuccess[i]);
            const auto& retry=g_VaultSectionRetry[i];
            if(!retry.reason.empty())status+=" - "+retry.reason+"; retry in "+std::to_string(retry.Remaining(NowMs()))+"s";
        }
        successUntil = g_VaultSuccessUntil;
    }
    AlterEgo::RefreshUI::State state = AlterEgo::RefreshUI::State::Ready;
    std::string detail = status.empty()?(showingData ? "Ready" : "No Vault data available - click Refresh"):status;
    if (pending) {
        state = AlterEgo::RefreshUI::State::Working;
        detail = status.empty() ? "Fetching Vault objectives..." : status;
    } else if (waitingForRollover) {
        state = AlterEgo::RefreshUI::State::Ready;
        detail = status.empty() ? "Waiting for updated Vault objectives..." : status;
    } else if(failed){
        state=AlterEgo::RefreshUI::State::Error;detail=status;
    } else if (std::chrono::steady_clock::now() < successUntil) {
        state = AlterEgo::RefreshUI::State::Success;
        detail = status;
    }
    if (AlterEgo::RefreshUI::Row("vault", updated, "Vault", state, detail, pending,nullptr,
            !pending&&waitingForRollover?"Awaiting sync":nullptr,true))
        FetchVaultData();
}

void RenderClearsStatusRow() {
    const bool fetching = g_ClearsFetching || g_ClearsQueryPending.load() ||
        g_ClearsCompletionInFlight.load() || g_ClearsRetryAtMs.load() != 0;
    std::time_t updated;
    std::string status;
    bool waitingForDailyRollover;
    bool haveData,failed;
    std::chrono::steady_clock::time_point successUntil;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        updated = *std::min_element(g_ClearsSectionSuccess.begin(),g_ClearsSectionSuccess.end());
        status = g_ClearsStatusMsg;failed=!status.empty();
        haveData = g_ClearsFetched;
        waitingForDailyRollover = g_ClearsDailyRolloverRetryAtMs.load() != 0;
        for(size_t i=0;i<4;++i){
            status+="\n"+std::string(ClearLabels[i])+": "+AlterEgo::RefreshUI::Age(g_ClearsSectionSuccess[i]);
            for(const auto* retry:{&g_ClearsSectionRetry[i],&g_ClearsProgressRetry[i]})
                if(!retry->reason.empty())status+=" - "+retry->reason+"; retry in "+std::to_string(retry->Remaining(NowMs()))+"s";
        }
        successUntil = g_ClearsSuccessUntil;
    }
    AlterEgo::RefreshUI::State state = AlterEgo::RefreshUI::State::Ready;
    std::string detail = (haveData ? "Ready" : "No Clears data available - click Refresh")+status;
    if (fetching) {
        state = AlterEgo::RefreshUI::State::Working;
        detail = status.empty() ? "Querying completion..." : status;
    } else if (waitingForDailyRollover) {
        // Normal daily reset propagation is scheduled, not a failed retrieval.
        state = AlterEgo::RefreshUI::State::Ready;
        const auto remaining=std::max<long long>(0,g_ClearsDailyRolloverRetryAtMs.load()-NowMs());
        detail = (status.empty() ? "Daily API delayed" : status) +
            "; retry in " + std::to_string((remaining+999)/1000) + "s";
    } else if (failed) {
        state = AlterEgo::RefreshUI::State::Error;
        detail = status;
    } else if (std::chrono::steady_clock::now() < successUntil) {
        state = AlterEgo::RefreshUI::State::Success;
        detail = "Refresh complete"+status;
    }
    if (AlterEgo::RefreshUI::Row("clears", updated, "Clears", state, detail, fetching,nullptr,
            !fetching&&waitingForDailyRollover?"Awaiting sync":nullptr,true))
        FetchClears(true);
}

// Called AFTER tab rendering: account changes and UTC reset invalidation
// have already run. Navigation itself is the trigger; manual Refresh
// intentionally continues to force a full request.
void RefreshVaultOnEntry() {
    AlterEgo::WikiEvents::Refresh(AlterEgo::GW2API::GetDataDirectory());
    if (!AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Vault)) return;
    if (g_VaultNeedRequery || g_VaultFetching.load()) return;
    bool stale = false;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        stale = AlterEgo::SectionRefresh::Stale(
            g_VaultLastSuccessTime, std::time(nullptr)) ||
            g_VaultDailyRolloverPending || g_VaultWeeklyRolloverPending ||
            g_VaultSeasonEnd==std::chrono::system_clock::time_point{} ||
            std::chrono::system_clock::now()>=g_VaultSeasonEnd;
    }
    if (!stale) return;
    FetchVaultData(true);
}

void RefreshClearsOnEntry() {
    if (!AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Clears)) return;
    if (g_ClearsNeedRequery || g_ClearsFetching ||
        g_ClearsCompletionInFlight.load() ||
        g_ClearsQueryPending.load() || g_ClearsRetryAtMs.load() != 0) return;
    bool stale = false;
    bool haveData = false;
    {
        std::lock_guard<std::mutex> lock(g_ClearsMutex);
        stale = AlterEgo::NativeProgress::AchievementDue(g_ClearsCacheAccount,g_ClearsLastSuccessTime,AlterEgo::RefreshEngine::ObjectiveSeconds()) ||
            g_ClearsDailyRolloverPending;
        haveData = g_ClearsFetched && !g_ClearsDailyRolloverPending&&
            !g_DailyFractals.empty()&&!g_DailyBounties.empty()&&!g_WeeklyWings.empty()&&g_WeeklyStrikes.id;
        stale=stale||!haveData;
    }
    if (!stale) return;
    if (!haveData) {
        FetchClears(); // no usable IDs yet: retrieve category definitions
    } else {
        {std::lock_guard<std::mutex> lock(g_ClearsMutex);g_LastClearsCompletionQuery=std::chrono::steady_clock::now();}
        SendClearsAchQuery(true);
    }
}
