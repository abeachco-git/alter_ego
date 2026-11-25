#pragma once
// Declarations shared across multiple addon modules.
// Implementations live in dllmain.cpp.

#include <string>
#include <functional>
#include <cstdint>
#include <set>
#include "nexus/Nexus.h"
#include "imgui.h"
#include "UIControls.h"

// A non-modal window can remain open behind its launcher. Request focus at
// the launch site and consume it immediately before that window's Begin(),
// which may occur on the next frame. Never steal focus every render frame.
struct AddonWindowFocusRequest {
    bool pending = false;
    void Request() { pending = true; }
    void ApplyBeforeBegin() {
        if (!pending) return;
        pending = false;
        ImGui::SetNextWindowFocus();
    }
};

// The Nexus API handle — defined as a non-static global in dllmain.cpp
extern AddonAPI_t* APIDefs;

// Copy text to the Windows clipboard
void CopyToClipboard(const std::string& text);

// Draw a gradient-backed section header with a colored accent underline.
// Used by equipment panel, clears, and other tabs.
void RenderSectionHeader(const char* label, ImVec4 color, const char* suffix = nullptr);

// Shared height for ordinary actions. Dense completion chips/icon hit targets stay compact.
inline constexpr float kAlterEgoActionButtonHeight = AlterEgo::UI::ActionHeight;
// Neutral full-height button with a font-responsive minimum. Optional fixed width.
bool RenderActionButton(const char* label, ImVec2 size = ImVec2(0, 0));

// Gold-trimmed button matching the RenderSectionHeader primary-action vocabulary.
// Returns true on click. Pass explicit size for fixed-width rows.
bool RenderGoldButton(const char* label, ImVec2 size = ImVec2(0, 0));

// Small chip-style button — secondary action vocabulary.
// active: highlighted when true. accentColor: pass ImVec4(0,0,0,0) for default gold tint.
bool RenderChipButton(const char* label, bool active, ImVec4 accentColor = ImVec4(0, 0, 0, 0), bool compact = false);

// Gold-bordered combo box. Wraps ImGui::BeginCombo with themed styling.
// width <= 0: caller sets width via SetNextItemWidth; > 0: sets width here.
// Returns true when open — caller must call ImGui::EndCombo().
bool RenderThemedCombo(const char* id, const char* current, float width = -1.0f);

// Centered themed empty-state / first-run card.
// iconTex: optional Nexus texture (pass nullptr to skip icon row).
// primaryLabel / secondaryLabel: pass nullptr to omit the respective button.
void RenderEmptyCard(Texture_t* iconTex,
                     const char* headline,
                     const char* body,
                     const char* primaryLabel, std::function<void()> primaryCB,
                     const char* secondaryLabel, std::function<void()> secondaryCB);

// Inline animated loading spinner: rotating arc + label.
void RenderSpinner(const char* label, ImVec4 color = ImVec4(0.7f, 0.85f, 0.95f, 1.0f));

// Character filter shares Compare's profession rows; empty means All.
// Names are limited to the caller's verified roster; unknown class stays selectable.
bool RenderCharacterMultiFilter(const char* label, std::set<std::string>& selected,
                                const std::set<std::string>& names);

// Common item presentation and actions, shared by Skinventory and native Inventory.
ImVec4 GetSkinRarityColor(const std::string& rarity);
void RenderCoins(int64_t copper);

// Returns the account GUID to use for per-account API queries.
// Returns an empty string when "All Accounts" is selected.
std::string GetEffectiveAccountName();

// Atomically write `content` to `finalPath` via write-temp-then-rename.
// Returns true on success. Prevents partial-file pollution on crash mid-write.
bool WriteFileAtomic(const std::string& finalPath, const std::string& content);
