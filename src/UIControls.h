#pragma once
// Canonical general-purpose Alter Ego UI controls. Read docs/ui-components.md.
// Keep Nexus/ImGui 1.80 compatibility; no request or domain logic belongs here.
#include "imgui.h"
#include "imgui_internal.h"
#include "SplitPanelCore.h"
#include <set>
#include <string>
#include <algorithm>
#include <cctype>
#include <vector>
#include <functional>

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
// Shared explicit edit footer. Callers own validation, draft and commit.
enum class EditAction { None, Save, Cancel };
inline EditAction SaveCancel(const char* scope,bool canSave=true,const char* saveLabel="Save") {
    ImGui::PushID(scope);
    if(!canSave){ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true);ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*.45f);}
    const bool save=Button(saveLabel,ImVec2(std::max(80.f,ImGui::CalcTextSize(saveLabel).x+2*ImGui::GetStyle().FramePadding.x),0))&&canSave;
    if(!canSave){ImGui::PopStyleVar();ImGui::PopItemFlag();}
    ImGui::SameLine();
    const bool cancel=Button("Cancel",ImVec2(80,0));
    ImGui::PopID();
    return save?EditAction::Save:cancel?EditAction::Cancel:EditAction::None;
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
// Compact search and filter presentation. All query state and filtering
// semantics remain owned by the caller; these helpers never fetch data.
inline float CompactButtonWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}
template<class Edit, class Clear, class HasText>
inline bool SearchFieldCore(const char* id, float width, Edit edit, Clear clear, HasText hasText) {
    ImGui::PushID(id);
    const float space = ImGui::GetStyle().ItemSpacing.x;
    const float clearWidth = ImGui::GetFrameHeight();
    const float available = std::max(1.0f, width > 0.0f ? width : ImGui::GetContentRegionAvail().x);
    const bool fit = available >= clearWidth + space + ImGui::GetFontSize() * 3.0f;
    ImGui::SetNextItemWidth(fit ? available - clearWidth - space : available);
    bool changed = edit();
    if (fit) ImGui::SameLine();
    const bool populated = hasText();
    if (!populated) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
    if (Button("x##clear_search", ImVec2(clearWidth, ImGui::GetFrameHeight()), ButtonRole::Compact) && populated) {
        clear(); changed = true;
    }
    if (!populated) ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear search");
    ImGui::PopID();
    return changed;
}
template<size_t N>
inline bool SearchField(const char* id, const char* hint, char (&query)[N], float width = -1.0f) {
    return SearchFieldCore(id, width,
        [&] { return InputTextWithHint("##query", hint, query, N); },
        [&] { query[0] = '\0'; }, [&] { return query[0] != '\0'; });
}
inline bool SearchField(const char* id, const char* hint, std::string& query, float width = -1.0f) {
    return SearchFieldCore(id, width,
        [&] { return InputTextWithHint("##query", hint, query); },
        [&] { query.clear(); }, [&] { return !query.empty(); });
}
// Canonical popup field layout. Label comes first, then an aligned input.
// Hidden widget labels retain stable IDs within each field's own ID scope.
inline void FilterFieldLabel(const char* label) {
    const float x=ImGui::GetCursorPosX();
    const float labelWidth=ImGui::GetFontSize()*10.f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(x+std::max(labelWidth,ImGui::CalcTextSize(label).x+ImGui::GetStyle().ItemSpacing.x));
}
inline float FilterFieldWidth() {return ImGui::GetFontSize()*13.f;}
inline bool FilterChoice(const char* label, int& selected, const char* const options[], int count) {
    ImGui::PushID(label);
    FilterFieldLabel(label);
    ImGui::SetNextItemWidth(FilterFieldWidth());
    const bool changed=Combo("##value", &selected, options, count);
    ImGui::PopID();
    return changed;
}
inline bool FilterChoice(const char* label, std::string& selected, const std::vector<std::string>& options) {
    ImGui::PushID(label);
    FilterFieldLabel(label);
    bool changed = false;
    if (BeginCombo("##value", selected.empty() ? "All" : selected.c_str(), 0, FilterFieldWidth())) {
        if (SelectableOption("All", selected.empty()) && !selected.empty()) { selected.clear(); changed = true; }
        for (const auto& option : options)
            if (SelectableOption(option.c_str(), selected == option) && selected != option) {
                selected = option; changed = true;
            }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}
inline bool FilterInt(const char* label,int& value) {
    ImGui::PushID(label);
    FilterFieldLabel(label);
    ImGui::SetNextItemWidth(FilterFieldWidth());
    const bool changed=ImGui::InputInt("##value",&value);
    ImGui::PopID();
    return changed;
}
// Checkbox multi-select: empty means unrestricted; keep popup open.
inline bool MultiFilterChoice(const char* label,std::set<std::string>& selected,
    const std::vector<std::pair<std::string,std::string>>& options) {
    ImGui::PushID(label);
    FilterFieldLabel(label);
    bool changed=false;
    const auto preview=selected.empty()?std::string("All"):
        selected.size()==1?*selected.begin():std::to_string(selected.size())+" selected";
    if(BeginCombo("##value",preview.c_str(),0,FilterFieldWidth())) {
        if(ImGui::Selectable("All",selected.empty(),ImGuiSelectableFlags_DontClosePopups)) {
            if(!selected.empty()){selected.clear();changed=true;}
        }
        for(const auto& option:options) {
            ImGui::PushID(option.first.c_str());
            bool on=selected.count(option.first)!=0;
            if(ImGui::Checkbox(option.second.c_str(),&on)) {
                if(on)selected.insert(option.first);else selected.erase(option.first);
                changed=true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}
// Decorative group dividers in the gaps between central tabs. Draw after
// submitting all tabs (the layout is current), without adding any ImGui item,
// hit target, selection, keyboard stop or width to the tab bar.
inline void TabGroupDividerBefore(const ImGuiTabBar* bar,ImGuiID following){
    if(!bar||!following)return;
    for(int i=1;i<bar->Tabs.Size;++i){
        const auto& next=bar->Tabs[i];
        const auto& prev=bar->Tabs[i-1];
        if(next.ID!=following)continue;
        if(next.LastFrameVisible!=ImGui::GetFrameCount()||
           prev.LastFrameVisible!=ImGui::GetFrameCount())return;
        // Both main-tab groups use the central section. Respect the real gap
        // and clipping rectangle from the vendored ImGui 1.80 tab layout.
        const float left=bar->BarRect.Min.x+IM_FLOOR(prev.Offset-bar->ScrollingAnim)+prev.Width;
        const float right=bar->BarRect.Min.x+IM_FLOOR(next.Offset-bar->ScrollingAnim);
        if(right-left<2.f)return;
        const float x=IM_FLOOR((left+right)*.5f)+.5f;
        const float y=bar->BarRect.Min.y;
        const float h=bar->BarRect.GetHeight();
        if(x<bar->ScrollingRectMinX+1.f||x>bar->ScrollingRectMaxX-1.f||h<=0.f)return;
        auto* draw=ImGui::GetWindowDrawList();
        draw->PushClipRect(ImVec2(bar->ScrollingRectMinX,y),
                           ImVec2(bar->ScrollingRectMaxX,y+h),true);
        auto color=ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        color.w*=.58f;
        draw->AddLine(ImVec2(x,y+h*.15f),ImVec2(x,y+h*.85f),
                      ImGui::GetColorU32(color),1.f);
        draw->PopClipRect();
        return;
    }
}
// Splitter owns both SameLine calls. Capture before drawing the left child.
// Persist only explicit drag/reset, not temporary clamping on window resize.
struct SplitPanel {
    float width,height,overhead,left,def,minLeft,minRight,grip;
    SplitPanel(float preferred,float defaultWidth,float minimumLeft=120.f,float minimumRight=200.f)
        :width(ImGui::GetContentRegionAvail().x),height(std::max(1.f,ImGui::GetContentRegionAvail().y)),
         overhead(8.f+2.f*ImGui::GetStyle().ItemSpacing.x),def(defaultWidth),
         minLeft(minimumLeft),minRight(minimumRight),grip(8.f) {
        left=FitSplitWidth(preferred,width,overhead,minLeft,minRight);
    }
    bool Divider(const char* id,float& preferred) const {
        ImGui::SameLine();
        const auto pos=ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id,ImVec2(grip,height));
        const bool hover=ImGui::IsItemHovered(), active=ImGui::IsItemActive();
        if(hover||active)ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        bool save=false;
        if(hover&&ImGui::IsMouseDoubleClicked(0)) {preferred=def;save=true;}
        else if(active&&ImGui::GetIO().MouseDelta.x!=0.f)
            preferred=FitSplitWidth(left+ImGui::GetIO().MouseDelta.x,width,overhead,minLeft,minRight);
        if(ImGui::IsItemDeactivated())save=true;
        const float x=pos.x+grip*.5f;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x,pos.y),ImVec2(x,pos.y+height),
            hover||active?IM_COL32(180,160,80,220):IM_COL32(120,110,90,150),2.f);
        ImGui::SameLine();
        return save;
    }
};
// An empty remove action denotes required context, not an active filter.
struct FilterChip {
    std::string id;
    std::string label;
    std::function<void()> remove;
};
using FilterChips = std::vector<FilterChip>;
inline int ActiveFilterCount(const FilterChips& chips) {
    return static_cast<int>(std::count_if(chips.begin(), chips.end(),
        [](const FilterChip& chip) { return static_cast<bool>(chip.remove); }));
}
// A chip is one compact click target with its removal x on the right.
// Use the real filter key for identity, not the changing display value.
inline void RenderFilterChips(const FilterChips& chips, const std::function<void()>& reset) {
    const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float startX = ImGui::GetCursorPosX();
    float used = 0.0f;
    bool any = false;
    auto place = [&](float width) {
        if (any && used + gap + width <= avail) { ImGui::SameLine(); used += gap + width; }
        else { ImGui::SetCursorPosX(startX); used = width; }
        any = true;
    };
    for (const auto& chip : chips) {
        if (chip.label.empty()) continue;
        ImGui::PushID(chip.id.c_str());
        if (!chip.remove) {
            const float width = std::min(avail, ImGui::CalcTextSize(chip.label.c_str()).x);
            place(width);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", chip.label.c_str());
        } else {
            std::string shown = chip.label;
            // Preserve the close affordance even when a narrow sidebar truncates a label.
            while (!shown.empty() && CompactButtonWidth((shown + "...  x").c_str()) > avail) {
                size_t end = shown.size() - 1;
                while (end > 0 && (static_cast<unsigned char>(shown[end]) & 0xC0) == 0x80) --end;
                shown.resize(end);
            }
            if (CompactButtonWidth((chip.label + "  x").c_str()) <= avail) shown = chip.label;
            else if (!shown.empty()) shown += "...";
            const std::string label = (shown.empty() ? std::string("x") : shown + "  x") + "###remove_filter";
            const float width = std::min(avail, CompactButtonWidth(label.c_str()));
            place(width);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.40f, 0.18f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.46f, 0.22f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.58f, 0.46f, 0.22f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.55f, 1.0f));
            const bool clicked = Button(label.c_str(), ImVec2(width, 0), ButtonRole::Compact);
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove filter: %s", chip.label.c_str());
            if (clicked) chip.remove();
        }
        ImGui::PopID();
    }
    if (ActiveFilterCount(chips) > 0) {
        const float width = std::min(avail, CompactButtonWidth("Clear all"));
        place(width);
        if (Button("Clear all##filters", ImVec2(width, 0), ButtonRole::Compact)) reset();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear all filters (keep search text)");
    }
}
template<class Query, class CollectFilters, class DrawFilters, class ResetFilters>
inline bool SearchToolbar(const char* id, const char* hint, Query& query,
                          CollectFilters collectFilters,
                          DrawFilters drawFilters, ResetFilters resetFilters,
                          const char* actionLabel = nullptr, bool* actionClicked = nullptr) {
    ImGui::PushID(id);
    const int activeCount = ActiveFilterCount(collectFilters());
    const std::string text = activeCount > 0 ? "Filters (" + std::to_string(activeCount) + ")" : "Filters";
    const std::string filterLabel = text + "###filter_button";
    const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float fw = CompactButtonWidth(filterLabel.c_str());
    const float aw = actionLabel ? CompactButtonWidth(actionLabel) : 0.0f;
    const float reserve = fw + gap + (actionLabel ? aw + gap : 0.0f);
    // A toolbar containing a normal action (e.g. Refresh Prices) keeps
    // standard action height; align its search/filter controls to that height.
    if (actionLabel) {
        ImVec2 pad = ImGui::GetStyle().FramePadding;
        pad.y = std::max(pad.y, (ActionButtonHeight() - ImGui::GetFontSize()) * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, pad);
    }
    const bool sameRow = avail >= reserve + ImGui::GetFrameHeight() + gap + ImGui::GetFontSize() * 6.0f;
    bool changed = SearchField("search", hint, query, sameRow ? avail - reserve : avail);
    if (sameRow) ImGui::SameLine();
    if (Button(filterLabel.c_str(), ImVec2(0, 0), ButtonRole::Compact))
        ImGui::OpenPopup("##filters");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search and filter options");
    if (actionLabel) {
        if (sameRow || avail >= fw + gap + aw) ImGui::SameLine();
        const bool clicked = Button(actionLabel);
        if (actionClicked) *actionClicked = clicked;
        ImGui::PopStyleVar();
    } else if (actionClicked) *actionClicked = false;
    if (ImGui::BeginPopup("##filters")) {
        drawFilters();
        const bool canReset=ActiveFilterCount(collectFilters())>0;
        ImGui::Separator();
        if(!canReset)ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*.45f);
        const bool resetClicked=Button("Reset filters",ImVec2(0,0),ButtonRole::Compact);
        if(!canReset)ImGui::PopStyleVar();
        if(resetClicked&&canReset)resetFilters();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",canReset?"Reset filters (keep search text)":"No active filters");
        ImGui::EndPopup();
    }
    RenderFilterChips(collectFilters(), resetFilters);
    ImGui::PopID();
    return changed;
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
