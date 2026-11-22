#pragma once
#include <atomic>
#include <chrono>
#include <string>

// Event names for H&S responses
#define EV_AE_CLEARS_ACH_RESPONSE "EV_ALTER_EGO_CLEARS_ACH_RESP"
#define EV_AE_VAULT_RESPONSE      "EV_ALTER_EGO_VAULT_RESP"
#define EV_AE_VAULT_SEASON_RESP   "EV_ALTER_EGO_VAULT_SEASON_RESP"

// Cross-module globals — read/written from dllmain.cpp event handlers and render loop
extern bool g_ClearsFetched;
extern bool g_ClearsFetching;
extern bool g_ClearsNeedRequery;
extern std::atomic<bool> g_ClearsQueryPending;
// Boot-time completion query is automatic, unlike the pending completion
// phase of a user-requested FetchClears. They must not share a cancellation path.
extern std::atomic<bool> g_ClearsAutoBootPending;
extern bool g_VaultNeedRequery;

// Public API
void LoadClearsCache();
void LoadVaultCache();
void RenderVault();
void RenderClears();
void SyncCompletionAccountCaches();
void RenderVaultStatusRow();
void RenderClearsStatusRow();
bool VaultHideComplete();
bool ClearsHideComplete();
void ToggleVaultHideComplete();
void ToggleClearsHideComplete();
// Called once when navigating into a tab. Refreshes stale, unfinished
// sections only; the tab's manual Refresh continues to fetch everything.
void RefreshVaultOnEntry();
void RefreshClearsOnEntry();
// Reload restored UI preferences without requiring the next disk write to overwrite them.
void ReloadClearUiSettings();
void OnClearsAchResponse(void* eventArgs);
void OnVaultResponse(void* eventArgs);
void OnVaultSeasonResponse(void* eventArgs);

// Reset time helpers (also used by Achievement Tracker persistence)
std::chrono::system_clock::time_point CalcLastDailyReset(std::chrono::system_clock::time_point now);
std::chrono::system_clock::time_point CalcLastWeeklyReset(std::chrono::system_clock::time_point now);
