#pragma once
// Canonical general-purpose Alter Ego UI controls. Read docs/ui-components.md.
// Keep Nexus/ImGui 1.80 compatibility; no request or domain logic belongs here.
#include "imgui.h"
#include <string>
#include <algorithm>
#include <cctype>

namespace AlterEgo::UI {
inline constexpr float ActionHeight = 30.0f;
inline constexpr float TooltipWidth = 300.0f;
inline constexpr float ActiveRadius = 3.0f;
inline const ImVec4 DestructiveText(1.0f, 0.5f, 0.5f, 1.0f);
inline const ImVec4 WarningText(1.0f, 0.80f, 0.34f, 1.0f);
inline const ImVec4 DifferenceText(1.0f, 0.58f, 0.50f, 1.0f);
inline const ImVec4 TooltipTitle(1.0f, 0.9f, 0.5f, 1.0f);
inline const ImVec4 TooltipBody(0.8f, 0.8f, 0.8f, 1.0f);
inline const ImVec4 SectionGold(0.70f, 0.58f, 0.20f, 1.0f);

inline float ActionButtonHeight() { return std::max(ActionHeight, ImGui::GetFrameHeight()); }

enum class ButtonRole { Standard, Compact, Destructive, CompactDestructive };
inline bool Button(const char* label, ImVec2 size = ImVec2(0,0),
                   ButtonRole role = ButtonRole::Standard) {
    const bool compact = role == ButtonRole::Compact || role == ButtonRole::CompactDestructive;
    const bool destructive = role == ButtonRole::Destructive || role == ButtonRole::CompactDestructive;
    if (!compact && size.y <= 0.0f)
        size.y = ActionButtonHeight();
    if (destructive) ImGui::PushStyleColor(ImGuiCol_Text, DestructiveText);
    const bool clicked = ImGui::Button(label, size);
    if (destructive) ImGui::PopStyleColor();
    return clicked;
}
inline bool SmallButton(const char* label) { return ImGui::SmallButton(label); }
inline bool DestructiveMenuItem(const char* label, bool enabled = true) {
    ImGui::PushStyleColor(ImGuiCol_Text, DestructiveText);
    const bool clicked = ImGui::MenuItem(label, nullptr, false, enabled);
    ImGui::PopStyleColor();
    return clicked;
}
enum class ComboRole { Native, Gold };
inline bool BeginCombo(const char* id, const char* preview,
                       ImGuiComboFlags flags = 0, float width = -1.0f,
                       ComboRole role = ComboRole::Native) {
    if (width > 0.0f) ImGui::SetNextItemWidth(width);
    if (role == ComboRole::Gold) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.09f, 0.06f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.16f, 0.13f, 0.08f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.20f, 0.16f, 0.09f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Border, SectionGold);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.55f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.07f, 0.05f, 0.98f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    }
    const bool open = ImGui::BeginCombo(id, preview, flags);
    if (role == ComboRole::Gold) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(6);
    }
    return open;
}
inline bool Combo(const char* label, int* selected, const char* const items[], int count,
                  int height = -1) {
    return ImGui::Combo(label, selected, items, count, height);
}
inline bool SelectableOption(const char* label, bool selected = false) {
    const bool clicked = ImGui::Selectable(label, selected);
    if (selected) ImGui::SetItemDefaultFocus();
    return clicked;
}
// InputTextEx in our vendored ImGui 1.80 already owns the clipboard context
// menu, selection, UTF-8 filtering and callbacks. Never add a second menu here.
inline bool InputText(const char* label, char* buffer, size_t capacity,
                      ImGuiInputTextFlags flags = 0) {
    return ImGui::InputText(label, buffer, capacity, flags);
}
inline bool InputTextWithHint(const char* label, const char* hint, char* buffer,
                              size_t capacity, ImGuiInputTextFlags flags = 0) {
    return ImGui::InputTextWithHint(label, hint, buffer, capacity, flags);
}
inline bool InputTextMultiline(const char* label, char* buffer, size_t capacity,
                               const ImVec2& size, ImGuiInputTextFlags flags = 0) {
    return ImGui::InputTextMultiline(label, buffer, capacity, size, flags);
}
inline int ResizeString(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(data->BufTextLen);
        data->Buf = str->data();
    }
    return 0;
}
inline bool InputText(const char* label, std::string& str, ImGuiInputTextFlags flags = 0) {
    return ImGui::InputText(label, str.data(), str.capacity() + 1,
        flags | ImGuiInputTextFlags_CallbackResize, ResizeString, &str);
}
inline bool InputTextWithHint(const char* label, const char* hint, std::string& str,
                              ImGuiInputTextFlags flags = 0) {
    return ImGui::InputTextWithHint(label, hint, str.data(), str.capacity() + 1,
        flags | ImGuiInputTextFlags_CallbackResize, ResizeString, &str);
}
inline bool InputTextMultiline(const char* label, std::string& str, const ImVec2& size,
                               ImGuiInputTextFlags flags = 0) {
    return ImGui::InputTextMultiline(label, str.data(), str.capacity() + 1, size,
        flags | ImGuiInputTextFlags_CallbackResize, ResizeString, &str);
}
// A catalog entry can be shown as a card in the full gear editor or a
// dropdown option in a standalone component. Both use the same search rules.
inline bool CatalogMatches(const char* name, const char* description, std::string filter) {
    const auto lower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
    std::transform(filter.begin(), filter.end(), filter.begin(), lower);
    if (filter.empty()) return true;
    std::string n = name ? name : "", d = description ? description : "";
    std::transform(n.begin(), n.end(), n.begin(), lower);
    std::transform(d.begin(), d.end(), d.begin(), lower);
    return n.find(filter) != std::string::npos || d.find(filter) != std::string::npos;
}
inline bool CatalogCard(const char* id, const char* name, const char* description, bool selected) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float line = std::max(14.0f, ImGui::GetTextLineHeight());
    const float height = line * 2.0f + 4.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x+width, pos.y+height);
    dl->AddRectFilled(pos, max, selected ? IM_COL32(60,80,100,200) : IM_COL32(35,35,40,180), 3.0f);
    dl->AddRect(pos, max, selected ? IM_COL32(100,160,220,255) : IM_COL32(80,80,80,180), 3.0f);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height));
    dl->PushClipRect(pos, max, true);
    dl->AddText(ImVec2(pos.x+6, pos.y+2), IM_COL32(200,180,255,255), name);
    dl->AddText(ImVec2(pos.x+6, pos.y+2+line), IM_COL32(150,150,150,220), description);
    dl->PopClipRect();
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(name);
        ImGui::PushTextWrapPos(TooltipWidth);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y+height+2));
    return clicked;
}
inline bool CatalogOption(const char* name, const char* description, bool selected) {
    const bool clicked = SelectableOption(name, selected);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", description);
    return clicked;
}
inline void ActiveIndicator(ImDrawList* draw, ImVec2 center) {
    draw->AddCircleFilled(center, ActiveRadius, IM_COL32(60, 200, 80, 255));
}
inline void TooltipText(const char* title, const char* body) {
    ImGui::TextColored(TooltipTitle, "%s", title);
    if (body && *body) {
        ImGui::PushTextWrapPos(TooltipWidth);
        ImGui::TextColored(TooltipBody, "%s", body);
        ImGui::PopTextWrapPos();
    }
}
} // namespace AlterEgo::UI
