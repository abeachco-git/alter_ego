#pragma once
#include "SearchCore.h"
// Canonical general-purpose Alter Ego UI controls. Read docs/ui-components.md.
// Keep Nexus/ImGui 1.80 compatibility; no request or domain logic belongs here.
#include "imgui.h"
#include "imgui_internal.h"
#include "SplitPanelCore.h"
#include "EditorLifecycle.h"
#include "TooltipCore.h"
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

// Ordinary window backgrounds only; never mutate the host style or popup opacity.
// Percentage is additional transparency relative to the current theme.
inline constexpr int MaxWindowTransparency=80;
inline int WindowTransparencyPercent=0;
inline int ClampWindowTransparency(double value){
    if(!(value>=0.0))return 0;
    if(value>=MaxWindowTransparency)return MaxWindowTransparency;
    return static_cast<int>(value);
}
enum class BackgroundRole { Ordinary, Protected, Editor };
inline constexpr float ProtectedBackgroundMinimum=.82f;
inline float BackgroundAlpha(float base,int percent,BackgroundRole role=BackgroundRole::Ordinary){
    if(role==BackgroundRole::Editor)return base;
    const float ordinary=base*(1.f-ClampWindowTransparency(percent)/100.f);
    return role==BackgroundRole::Protected?std::max(ordinary,std::min(base,ProtectedBackgroundMinimum)):ordinary;
}
// Composite a theme-colored local surface over an ordinary window to reach
// the same protected alpha. The zero setting adds no drawing or color changes.
inline float ProtectedOverlayAlpha(float base,int percent){
    const float ordinary=BackgroundAlpha(base,percent);
    const float target=BackgroundAlpha(base,percent,BackgroundRole::Protected);
    return ordinary<target?(target-ordinary)/(1.f-ordinary):0.f;
}
inline ImVec4 ReadableMetadataColor(ImVec4 color,int percent){
    const float blend=.45f*std::min(1.f,ClampWindowTransparency(percent)/50.f);
    const auto& text=ImGui::GetStyleColorVec4(ImGuiCol_Text);
    color.x+=std::max(0.f,text.x-color.x)*blend;
    color.y+=std::max(0.f,text.y-color.y)*blend;
    color.z+=std::max(0.f,text.z-color.z)*blend;
    return color;
}
inline ImGuiID EditorBackgroundKey(){return ImHashStr("AlterEgo.EditorBackgroundRole");}
inline bool PreserveChildBackground(const ImGuiWindow* parent){
    for(auto* w=parent;w;w=w->ParentWindow)
        if((w->Flags&(ImGuiWindowFlags_Popup|ImGuiWindowFlags_Tooltip|ImGuiWindowFlags_Modal))||
            w->StateStorage.GetBool(EditorBackgroundKey()))return true;
    return false;
}
inline void ApplyWindowBackground(ImGuiCol color,BackgroundRole role=BackgroundRole::Ordinary){
    if(WindowTransparencyPercent&&role!=BackgroundRole::Editor)
        ImGui::SetNextWindowBgAlpha(BackgroundAlpha(ImGui::GetStyleColorVec4(color).w*ImGui::GetStyle().Alpha,WindowTransparencyPercent,role));
}
inline bool BeginWindow(const char* name,bool* open=nullptr,ImGuiWindowFlags flags=0,BackgroundRole role=BackgroundRole::Ordinary){
    ApplyWindowBackground(ImGuiCol_WindowBg,role);
    const bool visible=ImGui::Begin(name,open,flags);
    ImGui::GetCurrentWindow()->StateStorage.SetBool(EditorBackgroundKey(),role==BackgroundRole::Editor);
    return visible;
}
// Local decorative backing for compact status surfaces in ordinary windows.
// No new item, cursor movement, hit target, style override or popup changes.
inline void DrawProtectedBackground(ImVec2 min,ImVec2 max,float rounding=3.f){
    if(!WindowTransparencyPercent||PreserveChildBackground(ImGui::GetCurrentWindowRead())||max.x<=min.x||max.y<=min.y)return;
    ImVec4 color=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    color.w=ProtectedOverlayAlpha(color.w*ImGui::GetStyle().Alpha,WindowTransparencyPercent);
    if(color.w>0.f)ImGui::GetWindowDrawList()->AddRectFilled(min,max,ImGui::ColorConvertFloat4ToU32(color),rounding);
}
inline bool BeginChild(const char* id,ImVec2 size=ImVec2(0,0),bool border=false,ImGuiWindowFlags flags=0){
    if(!PreserveChildBackground(ImGui::GetCurrentWindowRead()))
        ApplyWindowBackground(ImGuiCol_ChildBg);
    return ImGui::BeginChild(id,size,border,flags);
}

// Presentation only: caller owns the native hit target and all data.
inline float TooltipWrapWidth(float base=TooltipWidth){
    return std::max(1.f,std::min(base*ImGui::GetFontSize()/13.f,ImGui::GetIO().DisplaySize.x-2.f*ImGui::GetStyle().WindowPadding.x-16.f));
}
inline void TooltipLine(const char* text,ImVec4 color=TooltipBody,float width=TooltipWidth){
    if(!text||!*text)return;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+TooltipWrapWidth(width));
    ImGui::TextColored(color,"%s",text);
    ImGui::PopTextWrapPos();
}
inline void TooltipText(const char* title,const char* body){
    TooltipLine(title,TooltipTitle);TooltipLine(body);
}
inline void DetailTooltip(const char* title,const char* body=nullptr,ImVec4 color=TooltipTitle){
    if((!title||!*title)&&(!body||!*body))return;
    ImGui::BeginTooltipEx(ImGuiWindowFlags_None,ImGuiTooltipFlags_OverridePreviousTooltip);
    TooltipLine(title,color);TooltipLine(body);
    ImGui::EndTooltip();
}
inline void Hint(const char* text){DetailTooltip(text,nullptr,TooltipBody);}
inline void DisabledHint(const char* reason){
    if(reason&&*reason&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))Hint(reason);
}
inline void ClippedTextHint(bool hovered,const std::string& label,float available){
    if(hovered&&!label.empty()&&ImGui::CalcTextSize(label.c_str()).x>available)Hint(label.c_str());
}
using TooltipCore::FriendlyAttribute;
inline float ActionButtonHeight() { return std::max(ActionHeight, ImGui::GetFrameHeight()); }
// Use the native same-line baseline for text next to a custom-height action.
inline void AlignTextToActionHeight(float height){
    auto& dc=ImGui::GetCurrentWindow()->DC;
    dc.CurrLineTextBaseOffset=std::max(dc.CurrLineTextBaseOffset,(height-ImGui::GetTextLineHeight())*.5f);
}

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
inline void ActionMessage(const char* message,bool error=false){
    if(!message||!*message)return;
    if(error)ImGui::PushStyleColor(ImGuiCol_Text,DestructiveText);
    ImGui::TextWrapped("%s",message);
    if(error)ImGui::PopStyleColor();
}
inline float ActionButtonWidth(const char* label){
    return std::max(80.f,ImGui::CalcTextSize(label,nullptr,true).x+2*ImGui::GetStyle().FramePadding.x);
}
inline float ActionPairWidth(const char* primary,const char* secondary){
    return ActionButtonWidth(primary)+ActionButtonWidth(secondary)+ImGui::GetStyle().ItemSpacing.x;
}
// Reserve the footer width before the auto-resize modal measures its content.
inline bool BeginConfirmationModal(const char* popup,const char* primary,const char* secondary="Cancel",ImGuiWindowFlags flags=0){
    if(!ImGui::IsPopupOpen(popup))return false;
    const auto minWidth=std::min(ActionPairWidth(primary,secondary)+2*ImGui::GetStyle().WindowPadding.x+2*ImGui::GetStyle().WindowBorderSize+2.f,
        std::max(1.f,ImGui::GetIO().DisplaySize.x-2*ImGui::GetStyle().DisplaySafeAreaPadding.x));
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWidth,0),ImVec2(FLT_MAX,FLT_MAX));
    return ImGui::BeginPopupModal(popup,nullptr,flags|ImGuiWindowFlags_AlwaysAutoResize);
}
inline EditAction ActionFooter(const char* scope,bool enabled,const char* primary,
    const char* secondary="Cancel",ButtonRole role=ButtonRole::Standard,
    const char* reason=nullptr,bool pending=false,const char* error=nullptr){
    ImGui::PushID(scope);
    const float primaryWidth=ActionButtonWidth(primary),secondaryWidth=ActionButtonWidth(secondary);
    const bool fits=ImGui::GetContentRegionAvail().x>=ActionPairWidth(primary,secondary);
    auto draw=[&](const char* label,float w,ButtonRole style,bool available,const char* why){
        if(!available){ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true);ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*.45f);}
        const bool clicked=Button(label,ImVec2(w,0),style)&&available;
        if(!available){
            DisabledHint(why);
            ImGui::PopStyleVar();ImGui::PopItemFlag();
        }
        return clicked;
    };
    const bool save=draw(primary,primaryWidth,role,enabled&&!pending,pending?"Operation in progress.":reason);
    if(fits)ImGui::SameLine();
    const bool cancel=draw(secondary,secondaryWidth,ButtonRole::Standard,!pending,"Operation in progress.");
    if(pending)ActionMessage("Working...");
    if(error&&*error)ActionMessage(error,true);
    ImGui::PopID();
    return save?EditAction::Save:cancel?EditAction::Cancel:EditAction::None;
}
inline EditAction SaveCancel(const char* scope,bool canSave=true,const char* saveLabel="Save",
    const char* reason=nullptr,bool pending=false,const char* error=nullptr){
    return ActionFooter(scope,canSave,saveLabel,"Cancel",ButtonRole::Standard,reason,pending,error);
}
inline EditAction ConfirmCancel(const char* scope,bool enabled,const char* confirm,
    const char* reason=nullptr,const char* error=nullptr,bool pending=false){
    return ActionFooter(scope,enabled,confirm,"Cancel",ButtonRole::Destructive,reason,pending,error);
}
// Read only the native ImGui key state, never install or override a host
// keyboard handler. An active field or deeper popup owns Escape first.
inline bool EditorEscape(){
    if(!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)||
       ImGui::GetIO().WantTextInput||ImGui::IsAnyItemActive()||
       ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))return false;
    const int key=ImGui::GetKeyIndex(ImGuiKey_Escape);
    return key>=0&&ImGui::IsKeyPressed(key,false);
}
// Call at the owning window's stable ID scope every frame. No data is discarded
// until the caller receives true, including when a nested draft is dirty.
inline bool DiscardRequested(const char* popup,bool requested,bool dirty,bool pending=false,
    const char* message="Your unsaved changes will be lost."){
    using namespace AlterEgo::EditorLifecycle;
    const auto decision=RequestClose(requested,dirty,pending);
    if(decision==CloseDecision::Close)return true;
    if(decision==CloseDecision::ConfirmDiscard)ImGui::OpenPopup(popup);
    bool discard=false;
    if(BeginConfirmationModal(popup,"Discard changes","Keep editing")){
        ImGui::TextWrapped("%s",message);
        const auto action=ActionFooter("discard",true,"Discard changes","Keep editing",ButtonRole::Destructive);
        if(action==EditAction::Save){discard=true;ImGui::CloseCurrentPopup();}
        else if(action==EditAction::Cancel||EditorEscape())ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return discard;
}
inline bool SmallButton(const char* label) { return ImGui::SmallButton(label); }
inline bool DestructiveMenuItem(const char* label, bool enabled = true) {
    ImGui::PushStyleColor(ImGuiCol_Text, DestructiveText);
    const bool clicked = ImGui::MenuItem(label, nullptr, false, enabled);
    ImGui::PopStyleColor();
    return clicked;
}
// Original vector menu icons, compiled into the DLL. Keep native MenuItem/
// BeginMenu responsible for navigation, selection, focus and popup closing.
// A font-responsive text gutter reserves space; drawing is non-interactive.
// Muted menu headings are non-interactive; a bounded wrap avoids expanding a
// popup to the length of an arbitrary character/Loadout name.
inline void MenuSectionHeading(const char* title){
    if(!title||!*title)return;
    const float width=std::max(1.f,std::min(ImGui::GetFontSize()*24.f,
        ImGui::GetIO().DisplaySize.x-2.f*ImGui::GetStyle().WindowPadding.x-16.f));
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width);
    ImGui::TextUnformatted(title);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}
inline std::string MenuObjectHeading(const char* type,const std::string& name){
    return name.empty()?std::string(type):std::string(type)+" - "+name;
}
// Role ID is stable independently of display text, with the enclosing popup
// already scoped to its exact source. Identical action labels never collide.
class MenuSection {
public:
    MenuSection(const char* role,const char* title=nullptr,bool separator=false){
        if(separator)ImGui::Separator();
        ImGui::PushID(role);
        MenuSectionHeading(title);
    }
    ~MenuSection(){ImGui::PopID();}
    MenuSection(const MenuSection&)=delete;
    MenuSection& operator=(const MenuSection&)=delete;
};
enum class MenuIcon { New, Paste, Edit, Rename, Duplicate, Copy, Share, Compare, Preview, Delete, Regenerate, Open, Detach, Link, Save, Search };
inline std::string MenuIconLabel(const char* label){
    std::string gutter;
    const float width=ImGui::GetFontSize()*1.35f;
    for(int i=0;i<20&&ImGui::CalcTextSize(gutter.c_str()).x<width;++i)gutter+=' ';
    return gutter+label;
}
inline void DrawMenuIcon(ImDrawList* draw,ImVec2 pos,MenuIcon icon,ImU32 color,float size){
    const float s=size*.82f;
    pos.y+=(size-s)*.5f;pos.x+=(size-s)*.5f;
    auto p=[&](float x,float y){return ImVec2(pos.x+x*s,pos.y+y*s);};
    const float thick=std::max(1.f,size*.085f);
    auto line=[&](float a,float b,float c,float d){draw->AddLine(p(a,b),p(c,d),color,thick);};
    auto rect=[&](float a,float b,float c,float d){draw->AddRect(p(a,b),p(c,d),color,s*.08f,0,thick);};
    auto circle=[&](float x,float y,float r){draw->AddCircle(p(x,y),s*r,color,12,thick);};
    switch(icon){
    case MenuIcon::Search:circle(.42f,.42f,.29f);line(.63f,.63f,.91f,.91f);break;
    case MenuIcon::Save:rect(.12f,.08f,.88f,.92f);rect(.27f,.08f,.72f,.36f);rect(.29f,.56f,.71f,.92f);line(.62f,.14f,.62f,.29f);break;
    case MenuIcon::New:line(.12f,.5f,.88f,.5f);line(.5f,.12f,.5f,.88f);break;
    case MenuIcon::Paste:rect(.18f,.2f,.82f,.94f);rect(.35f,.04f,.65f,.27f);line(.34f,.48f,.66f,.48f);line(.34f,.68f,.66f,.68f);break;
    case MenuIcon::Edit:line(.15f,.85f,.25f,.55f);line(.25f,.55f,.72f,.08f);line(.72f,.08f,.92f,.28f);line(.92f,.28f,.45f,.75f);line(.45f,.75f,.15f,.85f);line(.61f,.19f,.81f,.39f);break;
    case MenuIcon::Rename:line(.5f,.1f,.5f,.9f);line(.27f,.1f,.73f,.1f);line(.27f,.9f,.73f,.9f);break;
    case MenuIcon::Duplicate:rect(.06f,.06f,.62f,.62f);rect(.38f,.38f,.94f,.94f);break;
    case MenuIcon::Copy:rect(.12f,.06f,.7f,.76f);rect(.31f,.24f,.89f,.94f);line(.45f,.48f,.74f,.48f);break;
    case MenuIcon::Share:circle(.2f,.5f,.13f);circle(.8f,.16f,.13f);circle(.8f,.84f,.13f);line(.32f,.43f,.68f,.23f);line(.32f,.57f,.68f,.77f);break;
    case MenuIcon::Compare:rect(.07f,.12f,.93f,.88f);line(.5f,.12f,.5f,.88f);break;
    case MenuIcon::Preview:line(.04f,.5f,.3f,.24f);line(.3f,.24f,.7f,.24f);line(.7f,.24f,.96f,.5f);line(.96f,.5f,.7f,.76f);line(.7f,.76f,.3f,.76f);line(.3f,.76f,.04f,.5f);circle(.5f,.5f,.17f);break;
    case MenuIcon::Delete:line(.12f,.23f,.88f,.23f);line(.36f,.1f,.64f,.1f);line(.36f,.1f,.36f,.23f);line(.64f,.1f,.64f,.23f);rect(.23f,.23f,.77f,.91f);line(.42f,.4f,.42f,.75f);line(.58f,.4f,.58f,.75f);break;
    case MenuIcon::Regenerate:draw->PathArcTo(p(.5f,.5f),s*.35f,.1f,5.3f,12);draw->PathStroke(color,0,thick);line(.7f,.14f,.92f,.14f);line(.92f,.14f,.92f,.36f);break;
    case MenuIcon::Open:rect(.1f,.32f,.7f,.9f);line(.42f,.55f,.9f,.08f);line(.58f,.08f,.9f,.08f);line(.9f,.08f,.9f,.4f);break;
    case MenuIcon::Detach:line(.08f,.76f,.38f,.46f);line(.62f,.54f,.92f,.24f);line(.18f,.18f,.82f,.82f);break;
        case MenuIcon::Link:line(.12f,.60f,.12f,.80f);line(.12f,.80f,.32f,.88f);line(.32f,.88f,.63f,.57f);line(.37f,.43f,.68f,.12f);line(.68f,.12f,.88f,.20f);line(.88f,.20f,.88f,.40f);line(.88f,.40f,.60f,.68f);line(.32f,.68f,.68f,.32f);break;
    }
}
inline bool MenuAction(const char* label,MenuIcon icon,bool enabled=true,const char* reason=nullptr,const char* description=nullptr){
    const auto pos=ImGui::GetCursorScreenPos();
    auto* draw=ImGui::GetWindowDrawList();
    const auto size=ImGui::GetFontSize();
    const auto padded=MenuIconLabel(label);
    const bool destructive=icon==MenuIcon::Delete;
    const bool clicked=destructive?DestructiveMenuItem(padded.c_str(),enabled):ImGui::MenuItem(padded.c_str(),nullptr,false,enabled);
    DrawMenuIcon(draw,pos,icon,enabled?(destructive?ImGui::GetColorU32(DestructiveText):ImGui::GetColorU32(ImGuiCol_Text)):ImGui::GetColorU32(ImGuiCol_TextDisabled),size);
    if(!enabled)DisabledHint(reason);
    else if(description&&*description&&ImGui::IsItemHovered())Hint(description);
    return clicked;
}
inline bool BeginActionMenu(const char* label,MenuIcon icon,bool enabled=true,const char* reason=nullptr){
    const auto pos=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
    const auto size=ImGui::GetFontSize();const auto color=ImGui::GetColorU32(enabled?ImGuiCol_Text:ImGuiCol_TextDisabled);
    const auto padded=MenuIconLabel(label);
    const bool open=ImGui::BeginMenu(padded.c_str(),enabled);
    DrawMenuIcon(draw,pos,icon,color,size);
    if(!enabled)DisabledHint(reason);
    return open;
}
inline bool AvailableActionButton(const char* label,ButtonRole role,bool enabled=true,const char* reason=nullptr){
    if(!enabled){ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true);ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*.45f);}
    const bool clicked=Button(label,ImVec2(0,0),role)&&enabled;
    if(!enabled){
        DisabledHint(reason);
        ImGui::PopStyleVar();ImGui::PopItemFlag();
    }
    return clicked;
}
// Shared split dropdown row: primary label navigates, independent side
// chevron opens a flyout. Caller owns popup contents and close policy.
struct TabFlyoutResult {bool clicked=false;ImVec2 anchor;};
inline TabFlyoutResult TabFlyoutRow(const char* label,bool selected,const char* arrowId,const char* popup,float labelWidth=190.f){
    const float h=ImGui::GetTextLineHeightWithSpacing();
    const bool clicked=ImGui::Selectable(label,selected,ImGuiSelectableFlags_DontClosePopups,ImVec2(labelWidth,h));
    const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
    const auto color=ImGui::GetColorU32(ImGuiCol_Text);
    auto* draw=ImGui::GetWindowDrawList();
    if(selected){
        const ImVec2 check(max.x-17.f,(min.y+max.y)*.5f);
        draw->AddLine(ImVec2(check.x-4,check.y),ImVec2(check.x-1,check.y+3),color,1.7f);
        draw->AddLine(ImVec2(check.x-1,check.y+3),ImVec2(check.x+5,check.y-4),color,1.7f);
    }
    ImGui::SameLine(0.f,0.f);
    const auto p=ImGui::GetCursorScreenPos();
    const ImVec2 size(27.f,h);
    if(ImGui::InvisibleButton(arrowId,size)||(ImGui::IsItemHovered()&&!ImGui::IsPopupOpen(popup)))ImGui::OpenPopup(popup);
    const ImVec2 c(p.x+size.x*.5f,p.y+size.y*.5f);
    draw->AddLine(ImVec2(c.x-2,c.y-4),ImVec2(c.x+2,c.y),color,1.6f);
    draw->AddLine(ImVec2(c.x+2,c.y),ImVec2(c.x-2,c.y+4),color,1.6f);
    return {clicked,ImVec2(p.x+size.x,min.y)};
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
// Shared direction button; sorting remains a domain decision.
inline bool SortDirectionButton(const char* id,bool& ascending,float width=24.f){
    const bool changed=Button(id,ImVec2(width,0),ButtonRole::Compact);
    if(changed)ascending=!ascending;
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    const float x=(lo.x+hi.x)*.5f,y=(lo.y+hi.y)*.5f,r=5.f;
    const auto color=IM_COL32(220,220,220,255);
    auto* draw=ImGui::GetWindowDrawList();
    if(ascending)draw->AddTriangleFilled(ImVec2(x,y-r),ImVec2(x-r,y+r),ImVec2(x+r,y+r),color);
    else draw->AddTriangleFilled(ImVec2(x-r,y-r),ImVec2(x+r,y-r),ImVec2(x,y+r),color);
    if(ImGui::IsItemHovered())Hint(ascending?"Ascending; reverse sort":"Descending; reverse sort");
    return changed;
}
inline bool SelectableOption(const char* label, bool selected = false) {
    const bool clicked = ImGui::Selectable(label, selected);
    if (selected) ImGui::SetItemDefaultFocus();
    return clicked;
}
// Attach an opt-in menu immediately after the native hit target, before another
// widget or popup changes the current item/window. Callers scope by stable entity ID.
template<class Render>
inline void EntityContext(const char* id, Render render) {
    ImGui::PushID(static_cast<int>(ImGui::GetCurrentWindow()->DC.LastItemId));
    if (ImGui::BeginPopupContextItem(id)) { render(); ImGui::EndPopup(); }
    ImGui::PopID();
}
template<class Render>
inline bool EntityOption(const char* label, bool selected, Render render) {
    const bool clicked = SelectableOption(label, selected);
    EntityContext("##entity_option_actions", render);
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
// Small forms have a stable responsive width but natural content height.
// At small display heights the window scrolls, including its footer; no
// unconditional child-height or hidden-message reserve is needed.
inline void ContentSizedWindow(float preferredWidth,float margin=24.f){
    const auto display=ImGui::GetIO().DisplaySize;
    const float width=std::max(1.f,std::min(preferredWidth,display.x-margin));
    const float height=std::max(1.f,display.y-margin);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width,0),ImVec2(width,height));
}
// Name-only record operations. The caller owns the stable target, validation
// and commit. Failed transactions leave the name and popup intact.
struct NameActionDraft{
    enum class Mode { Existing, Custom, Automatic };
    std::string name,error,initialName;
    Mode mode=Mode::Existing,initialMode=Mode::Existing;
    bool pending=false,duplicate=false;
    void Start(const std::string& original,bool copy,Mode origin=Mode::Existing){
        name=original+(copy?" - Copy":"");initialName=name;error.clear();duplicate=copy;pending=true;
        mode=initialMode=origin;
    }
    void ManualEdit(){mode=Mode::Custom;}
    void Regenerate(const std::string& generated){name=generated;mode=Mode::Automatic;}
    bool Dirty() const{return name!=initialName||mode!=initialMode;}
};
template<class Commit>
inline void RenderNameAction(const char* popup,NameActionDraft& draft,bool valid,const char* explanation,Commit commit,
                             const std::function<std::string()>& generate={},const char* category=nullptr){
    if(draft.pending){draft.pending=false;ImGui::OpenPopup(popup);}
    if(!ImGui::IsPopupOpen(popup))return;
    ContentSizedWindow(std::max(340.f,ImGui::GetFontSize()*26.f));
    if(!ImGui::BeginPopupModal(popup,nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::TextUnformatted(draft.duplicate?"Duplicate record":"Rename record");
    if(category&&*category)ImGui::TextDisabled("%s",category);
    if(explanation&&*explanation)ImGui::TextWrapped("%s",explanation);
    if(!valid)ImGui::TextWrapped("Record changed. Cancel and review again.");
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    if(InputText("##name",draft.name)&&generate)draft.ManualEdit();
    if(generate){
        if(AvailableActionButton("Regenerate Name",ButtonRole::Standard,valid,"Record changed; reopen this dialog."))draft.Regenerate(generate());
        ImGui::TextDisabled("%s",draft.mode==NameActionDraft::Mode::Automatic?"Automatic":
            draft.mode==NameActionDraft::Mode::Custom?"Custom name":"Existing name");
    }
    const bool dirty=draft.Dirty();
    const bool named=draft.name.find_first_not_of(" \t\r\n")!=std::string::npos;
    const bool canSave=EditorLifecycle::CanSave(valid&&named,dirty,draft.duplicate);
    const char* reason=!valid?"Record changed; reopen this dialog.":!named?"Enter a name.":"No changes to save.";
    const auto action=SaveCancel("name_action",canSave,draft.duplicate?"Duplicate":"Save",reason,false,draft.error.c_str());
    if(action==EditAction::Save){
        if(commit(draft.name,draft.duplicate)){draft={};ImGui::CloseCurrentPopup();}
        else if(draft.error.empty())draft.error="Could not save. Check the name or reopen this record.";
    }else if(DiscardRequested("Discard changes?##name_action",action==EditAction::Cancel||EditorEscape(),dirty)){
        draft={};ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
// Compact search and filter presentation. All query state and filtering
// semantics remain owned by the caller; these helpers never fetch data.
inline float CompactButtonWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}
inline void SameLineIfActionFits(const char* label){
    if(ImGui::GetWindowPos().x+ImGui::GetContentRegionMax().x-ImGui::GetItemRectMax().x>=CompactButtonWidth(label)+ImGui::GetStyle().ItemSpacing.x)ImGui::SameLine();
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
    if (ImGui::IsItemHovered()) Hint("Clear search");
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
// Group boundaries alone receive 3 extra pixels on each side. The native
// resize-down layout owns width calculation and clipping. Reserve the group
// width before the first tab submits its layout, then restore the available
// rectangle and shift subsequent native offsets before they are submitted.
// No spacer item, additional ID, style mutation or vendored ABI change.
inline constexpr float TabGroupExtraGap=6.f;
struct TabGroupGaps {
    ImGuiTabBar* bar=nullptr;
    std::vector<ImGuiID> boundaries;
    float reserved=0.f;
    float originalMaxX=0.f;
    TabGroupGaps(ImGuiTabBar* current,std::initializer_list<ImGuiID> before):bar(current){
        if(!bar||!bar->WantLayout||!(bar->Flags&ImGuiTabBarFlags_FittingPolicyResizeDown))return;
        int live=0;
        for(const auto& tab:bar->Tabs){
            if(tab.LastFrameVisible<bar->PrevFrameVisible||tab.WantClose)continue;
            if(tab.Flags&(ImGuiTabItemFlags_Leading|ImGuiTabItemFlags_Trailing)){boundaries.clear();return;}
            if(live++&&std::find(before.begin(),before.end(),tab.ID)!=before.end())boundaries.push_back(tab.ID);
        }
        reserved=TabGroupExtraGap*static_cast<float>(boundaries.size());
        if(reserved<=0.f||bar->BarRect.GetWidth()<=reserved+1.f){reserved=0;return;}
        originalMaxX=bar->BarRect.Max.x;
        bar->BarRect.Max.x-=reserved;
    }
    TabGroupGaps(const TabGroupGaps&)=delete;
    TabGroupGaps& operator=(const TabGroupGaps&)=delete;
    void AfterFirstTab(){
        if(reserved<=0.f)return;
        bar->BarRect.Max.x=originalMaxX;
        if(!bar->WantLayout){
            float offset=0.f;
            for(auto& tab:bar->Tabs){
                if(std::find(boundaries.begin(),boundaries.end(),tab.ID)!=boundaries.end())offset+=TabGroupExtraGap;
                tab.Offset+=offset;
            }
            bar->ScrollingRectMaxX+=reserved;
            bar->WidthAllTabs+=reserved;
            bar->WidthAllTabsIdeal+=reserved;
            auto* window=ImGui::GetCurrentWindow();
            window->DC.CursorMaxPos.x=std::max(window->DC.CursorMaxPos.x,bar->BarRect.Min.x+bar->WidthAllTabs);
            window->DC.IdealMaxPos.x=std::max(window->DC.IdealMaxPos.x,bar->BarRect.Min.x+bar->WidthAllTabsIdeal);
        }
        reserved=0.f;
    }
    ~TabGroupGaps(){if(reserved>0.f)bar->BarRect.Max.x=originalMaxX;}
};
// Decorative group dividers in the actual enlarged central-tab gaps. Draw
// after all tabs have submitted, with no hit target or navigation stop.
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
        if (ImGui::IsItemHovered()) Hint("Clear all filters (keep search text)");
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
    if (ImGui::IsItemHovered()) Hint("Search and filter options");
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
        if(ImGui::IsItemHovered())Hint(canReset?"Reset filters (keep search text)":"No active filters");
        ImGui::EndPopup();
    }
    RenderFilterChips(collectFilters(), resetFilters);
    ImGui::PopID();
    return changed;
}
// A catalog entry can be shown as a card in the full gear editor or a
// dropdown option in a standalone component. Both use the same search rules.
inline bool CatalogMatches(const char* name, const char* description, std::string filter) {
    return AlterEgo::Search::MatchesFields(AlterEgo::Search::Words(filter),{name?name:"",description?description:""});
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
    if(ImGui::IsItemHovered())DetailTooltip(name,description);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y+height+2));
    return clicked;
}
inline bool CatalogOption(const char* name, const char* description, bool selected) {
    const bool clicked = SelectableOption(name, selected);
    if(ImGui::IsItemHovered())DetailTooltip(name,description);
    return clicked;
}
// A read-only two-column reference row is one native hit target. Measure both
// cells before submission, and keep the editable frame-padding convention out
// of this path. Table work bounds come from the pinned ImGui 1.80 layout.
struct ReferenceRowLayout {
    float height, labelY, valueY, linkY, wrap;
};
inline ReferenceRowLayout MeasureReferenceRow(float labelHeight,float valueHeight,float lineHeight,float wrap) {
    const float height=std::max(lineHeight,std::max(labelHeight,valueHeight));
    return {height,(height-labelHeight)*.5f,(height-valueHeight)*.5f,height*.5f,wrap};
}
inline bool ReferencePreviewRow(const char* id,const char* label,const std::string& value,
                                bool inherited,const char* hint,bool interactive=true) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const auto* table=ImGui::GetCurrentContext()->CurrentTable;
    const float line=ImGui::GetTextLineHeight();
    const float labelWidth=std::max(1.f,ImGui::GetContentRegionAvail().x);
    const float width=std::max(1.f,table->Columns[1].WorkMaxX-table->Columns[1].WorkMinX);
    const float reserve=inherited?line*1.4f:0.f;
    const float wrap=std::max(1.f,width-reserve);
    const auto layout=MeasureReferenceRow(
        ImGui::CalcTextSize(label,nullptr,false,labelWidth).y,
        ImGui::CalcTextSize(value.c_str(),nullptr,false,wrap).y,line,wrap);
    const auto start=ImGui::GetCursorScreenPos();
    auto* draw=ImGui::GetWindowDrawList();
    if(!ImGui::GetCurrentWindow()->SkipItems)
        draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),ImVec2(start.x,start.y+layout.labelY),
                      ImGui::GetColorU32(ImGuiCol_Text),label,nullptr,labelWidth);
    ImGui::TableSetColumnIndex(1);
    const auto pos=ImGui::GetCursorScreenPos();
    // Zero width is essential: SpanAllColumns then uses the entire table
    // work rectangle, not a value-cell-sized rectangle starting at column 0.
    const bool clicked=interactive
        ? ImGui::Selectable(id,false,ImGuiSelectableFlags_SpanAllColumns,ImVec2(0,layout.height))
        : (ImGui::Dummy(ImVec2(width,layout.height)),false);
    if(!ImGui::GetCurrentWindow()->SkipItems){
        draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),ImVec2(pos.x,pos.y+layout.valueY),
                      ImGui::GetColorU32(interactive?ImGuiCol_Text:ImGuiCol_TextDisabled),
                      value.c_str(),nullptr,layout.wrap);
        if(inherited&&width>=reserve){
            const float scale=line/13.f;
            const float x=pos.x+width-reserve*.5f,y=pos.y+layout.linkY;
            const auto color=ImGui::GetColorU32(ImGuiCol_TextDisabled);
            draw->AddCircle(ImVec2(x-3.f*scale,y+2.f*scale),3.f*scale,color,12,1.2f*scale);
            draw->AddCircle(ImVec2(x+3.f*scale,y-2.f*scale),3.f*scale,color,12,1.2f*scale);
            draw->AddLine(ImVec2(x-scale,y+scale),ImVec2(x+scale,y-scale),color,1.2f*scale);
        }
    }
    if(interactive&&hint&&ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",value.c_str(),hint);
    return clicked;
}
inline void ActiveIndicator(ImDrawList* draw, ImVec2 center) {
    draw->AddCircleFilled(center, ActiveRadius, IM_COL32(60, 200, 80, 255));
}
} // namespace AlterEgo::UI
