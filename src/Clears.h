#pragma once
#include <atomic>
#include <chrono>
#include <string>

// Cross-module globals — read/written from dllmain.cpp event handlers and render loop
extern std::atomic<bool> g_ClearsFetched;
extern std::atomic<bool> g_ClearsFetching;
extern std::atomic<bool> g_ClearsNeedRequery;
extern std::atomic<bool> g_ClearsQueryPending;
// Boot-time completion query is automatic, unlike the pending completion
// phase of a user-requested FetchClears. They must not share a cancellation path.
extern std::atomic<bool> g_ClearsAutoBootPending;
extern std::atomic<bool> g_VaultNeedRequery;

// Public API
void LoadClearsCache();
void ResetClearsPublicEpochCache();
void LoadVaultCache();
void RenderVault();
// Render-thread view transition only; no HTTP, no cancellation of in-flight work.
void EndVaultViewDemand();
void RenderClears();
void SyncCompletionAccountCaches();
void RenderVaultStatusRow();
void RenderClearsStatusRow();
bool VaultHideComplete();
bool ShowVaultAssistance();
void ToggleVaultAssistance();
bool ClearsHideComplete();
void ToggleVaultHideComplete();
void ToggleClearsHideComplete();
bool ShowFractalNextResets();
void ToggleFractalNextResets();
// Called once when navigating into a tab. Refreshes stale, unfinished
// sections only; the tab's manual Refresh continues to fetch everything.
void RefreshVaultOnEntry();
void RefreshClearsOnEntry();
// Reload restored UI preferences without requiring the next disk write to overwrite them.
void ReloadClearUiSettings();

// Reset time helpers (also used by Achievement Tracker persistence)
std::chrono::system_clock::time_point CalcLastDailyReset(std::chrono::system_clock::time_point now);
std::chrono::system_clock::time_point CalcLastWeeklyReset(std::chrono::system_clock::time_point now);
