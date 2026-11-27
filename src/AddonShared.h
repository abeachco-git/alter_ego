#pragma once
// Declarations shared across multiple addon modules.
// Implementations live in dllmain.cpp.

#include <string>
#include <functional>
#include <cstdint>
#include <set>
#include <vector>
#include <utility>
#include "nexus/Nexus.h"
#include "imgui.h"
#include "UIControls.h"
#include "ChatLink.h"

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

// Copy text through the Nexus-owned ImGui clipboard callbacks.
void CopyToClipboard(const std::string& text);
// Copy plain names without IDs, formatting or chat-link encoding.
inline void CopyNameMenuItem(const std::string& name,const char* label="Copy Name",bool icon=true){
    if(!name.empty()&&(icon?AlterEgo::UI::MenuAction(label,AlterEgo::UI::MenuIcon::Copy):ImGui::MenuItem(label)))CopyToClipboard(name);
}

// Native link copy is distinct from typed equipment/configuration copy. The
// existing item/simple encoders represent 24-bit IDs; never truncate a target.
inline bool ChatLinkIdValid(uint32_t id) { return AlterEgo::ChatLink::EntityIdValid(id); }
inline void CopyNativeLinkMenuItem(const std::string& link,const char* label="Copy Chat Link"){
    if(!link.empty()&&AlterEgo::UI::MenuAction(label,AlterEgo::UI::MenuIcon::Link))CopyToClipboard(link);
}
inline void CopyEntityChatLink(AlterEgo::ChatLinkType kind,uint32_t id,const char* label="Copy Chat Link",
                              uint32_t skin=0,uint32_t upgrade1=0,uint32_t upgrade2=0){
    CopyNativeLinkMenuItem(AlterEgo::ChatLink::EncodeEntity(kind,id,skin,upgrade1,upgrade2),label);
}
inline void CopyEntityMenu(const std::string& name,AlterEgo::ChatLinkType kind,uint32_t id){
    CopyNameMenuItem(name,"Copy Name",true);
    CopyEntityChatLink(kind,id);
}
inline void NameOptionContext(const std::string& name){
    if(name.empty())return;
    ImGui::PushID(name.c_str());AlterEgo::UI::EntityContext("##name_actions",[&]{CopyNameMenuItem(name,"Copy Name",true);});ImGui::PopID();
}
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

// Canonical Characters sort preference for render-thread-only presentation subsets.
std::vector<std::string> OrderedCharacterAccounts();
std::vector<std::pair<std::string,std::string>> OrderedCharacterKeys(
    const std::set<std::pair<std::string,std::string>>& requested);
std::vector<std::string> OrderedCharacterNames(const std::vector<std::string>& names);

// Character filter shares Compare's profession rows; empty means All.
// Names are limited to the caller's verified roster; unknown class stays selectable.
bool RenderCharacterMultiFilter(const char* label, std::set<std::string>& selected,
                                const std::set<std::string>& names);

// Shared profession artwork/name for clipped character branch headers.
void DrawInventoryCharacterLabel(ImDrawList* draw,ImVec2 min,ImVec2 max,const std::string& profession,const std::string& name);

// Common item presentation and actions, shared by Skinventory and native Inventory.
ImVec4 GetSkinRarityColor(const std::string& rarity);
float CoinAmountWidth(int64_t copper);
void RenderCoins(int64_t copper);
void MarkInventoryNavigation();
std::string CharacterLoadUnavailable(const std::string& character,const std::string& account);
bool RenderCharacterLoadMenu(const char* label,const std::string& character,const std::string& account);
void ProcessCharacterLoad();
std::string PlainItemDescription(const std::string& text);

// Returns the account GUID to use for per-account API queries.
// Returns an empty string when "All Accounts" is selected.
std::string GetEffectiveAccountName();

// Atomically write `content` to `finalPath` via write-temp-then-rename.
// Returns true on success. Prevents partial-file pollution on crash mid-write.
bool WriteFileAtomic(const std::string& finalPath, const std::string& content);
