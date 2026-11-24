#pragma once
// Canonical general-purpose Alter Ego UI controls. Read docs/ui-components.md.
// Keep Nexus/ImGui 1.80 compatibility; no request or domain logic belongs here.
#include "imgui.h"
#include <string>
#include <algorithm>

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

enum class ButtonRole { Standard, Compact, Destructive, CompactDestructive };
inline bool Button(const char* label, ImVec2 size = ImVec2(0,0),
                   ButtonRole role = ButtonRole::Standard) {
    const bool compact = role == ButtonRole::Compact || role == ButtonRole::CompactDestructive;
    const bool destructive = role == ButtonRole::Destructive || role == ButtonRole::CompactDestructive;
    if (!compact && size.y <= 0.0f)
        size.y = std::max(ActionHeight, ImGui::GetFrameHeight());
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
