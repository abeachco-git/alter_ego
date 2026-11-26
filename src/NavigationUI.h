#pragma once
// Included in dllmain after all page definitions. Render-thread only history;
// the input callback merely queues a direction (never calls ImGui or storage).
#include "NavigationHistory.h"
#include <optional>
#include <tuple>

struct NavigationSnapshot {
    int page=0,collection=0,sub=0,buildTab=0,equipTab=0,category=0,sort=0,view=0;
    uint32_t record=0,achievement=0;
    std::string id,kind,second,search,account,compareTarget;
    bool first=false,other=false,manual=false;
    std::set<std::string> modes,componentKinds;
    Skinventory::SkinFilters skins;
    AlterEgo::NativeAccounts::InventoryNavigation inventory;
    std::map<ImGuiID,float> scroll;
    bool SameDestination(const NavigationSnapshot& o) const {
        return std::tie(page,collection,sub,buildTab,equipTab,id,kind,second,record,achievement)==
               std::tie(o.page,o.collection,o.sub,o.buildTab,o.equipTab,o.id,o.kind,o.second,o.record,o.achievement);
    }
};
static AlterEgo::Navigation::History<NavigationSnapshot> g_Navigation;
static std::optional<NavigationSnapshot> g_NavRestore;
static int g_NavRestoreFrames=0;
static std::atomic<unsigned> g_NavInputMask{0}; // hover=1, focus=2, back=4, forward=8
static std::atomic<int> g_NavRequest{0};
static std::atomic<unsigned> g_NavConsumedButtons{0};

static NavigationSnapshot CaptureNavigation(bool withScroll=true) {
    NavigationSnapshot s;
    s.page=g_MainTab;s.account=g_SelectedAccountFilter;
    switch(s.page) {
        case 0: {
            auto chars=AlterEgo::GW2API::GetCharacters();
            if(g_SelectedCharIdx>=0&&g_SelectedCharIdx<(int)g_CharDisplayOrder.size()){
                const int idx=g_CharDisplayOrder[g_SelectedCharIdx];
                if(idx>=0&&idx<(int)chars->size())s.id=(*chars)[idx].name;
            }
            s.sub=g_SelectedTab;s.buildTab=g_SelectedBuildTab;s.equipTab=g_SelectedEquipTab;
            s.search=g_CharSearchBuf;s.sort=g_CharSortMode;s.first=g_CharSortAscending;break;
        }
        case 1: {
            s.collection=g_LibraryCollection;
            if(s.collection==0){
                const auto& builds=AlterEgo::GW2API::GetSavedBuilds();
                if(g_LibSelectedIdx>=0&&g_LibSelectedIdx<(int)builds.size())s.id=builds[g_LibSelectedIdx].id;
                s.search=g_LibSearchBuf;s.modes=g_LibFilterModes;
            }else if(s.collection==1){
                if(g_FashionSelected>=0&&g_FashionSelected<(int)g_FashionLibrary.looks.size())s.id=g_FashionLibrary.looks[g_FashionSelected].id;
                s.search=g_FashionSearch;s.category=g_FashionFilter;
            }else if(s.collection==2){
                s.id=g_ComponentSelectedId;s.kind=g_ComponentSelectedKind;s.search=g_ComponentSearch;
                s.componentKinds=g_ComponentKindFilters;s.sort=g_ComponentSort;s.first=g_ComponentUnusedOnly;s.other=g_ComponentSortAscending;
            }else{s.id=g_EquipmentSetSelectedId;s.search=g_EquipmentSetSearch;}
            break;
        }
        case 2:s.sub=g_SkinActiveTab;s.record=g_SkinSelectedId;
            if(s.sub==0){
                s.search=g_SkinSearchFilter;s.skins=g_SkinBrowserFilters;
                s.first=g_SkinShowOwned;s.other=g_SkinShowUnowned;s.view=g_SkinViewMode;
            }else{
                s.search=g_ShopNavigationQuery;s.skins=g_ShopNavigationFilters;
                s.category=g_ShopNavigationSource;s.sort=g_ShopNavigationMaxGold;
            }
            break;
        case 4:s.id=g_AchSelectedGroupId;s.record=g_AchSelectedCatId;s.achievement=g_NavAchievementId;
            s.search=g_AchSearchBuf;s.first=g_AchHideEmptyCats;s.other=g_AchHideCompletedCats;break;
        case 7:s.inventory=AlterEgo::NativeAccounts::CaptureInventoryNavigation();s.sub=s.inventory.subview;s.record=s.inventory.item;s.kind=s.inventory.currency?"currency":"item";
            if(s.sub==0){s.id=s.inventory.grouped?"groups":"list";s.second=s.inventory.grouped?s.inventory.group:std::string{};}break;
        case 5:s.id=g_LibCompareChar;s.manual=g_LibCompareSavedManual;
            if(s.manual)s.second=g_LibCompareBuildId;
            s.compareTarget=g_LibComparePreviousTarget;s.buildTab=g_LibCompareBuildTab;s.equipTab=g_LibCompareEquipTab;
            s.first=g_LibCompareShowMatches;s.other=g_LibCompareExactItems;break;
    }
    if(withScroll){
        const auto* root=ImGui::GetCurrentWindow();
        for(auto* w:GImGui->Windows)
            if(w->RootWindow==root&&w->LastFrameActive==ImGui::GetFrameCount()&&
                (w->Flags&ImGuiWindowFlags_ChildWindow))s.scroll[w->ID]=w->Scroll.y;
    }
    return s;
}
static bool NavigationBlocked(){
    return g_LibEditMode||g_FashionDialog||g_ComponentEditorOpen||g_ReferenceEditor.open||g_GearDialogOpen||
        g_SaveLibDialogOpen||g_NewBuildDialogOpen||g_LibShowImport||
        ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)||
        ImGui::GetIO().WantTextInput;
}
static void RestoreNavigation(const NavigationSnapshot& s){
    if(g_SelectedAccountFilter!=s.account){
        g_SelectedAccountFilter=s.account;
        if(!s.account.empty()){
            Skinventory::OwnedSkins::SetAccountName(s.account);
            if(AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::SkinOwnership))g_SkinRefreshOwned=true;
            g_ClearsNeedRequery=true;g_VaultNeedRequery=true;g_AchNeedRequery=true;
        }
    }
    switch(s.page){
        case 0:{
            g_CharSortMode=static_cast<decltype(g_CharSortMode)>(s.sort);g_CharSortAscending=s.first;
            RebuildCharDisplayOrder();auto chars=AlterEgo::GW2API::GetCharacters();
            g_SelectedCharIdx=-1;
            for(int i=0;i<(int)g_CharDisplayOrder.size();++i){
                const int idx=g_CharDisplayOrder[i];
                if(idx>=0&&idx<(int)chars->size()&&(*chars)[idx].name==s.id){g_SelectedCharIdx=i;break;}
            }
            g_SessionSelectedCharName.clear();g_DetailsFetched=false;
            g_SelectedBuildTab=s.buildTab;g_SelectedEquipTab=s.equipTab;
            snprintf(g_CharSearchBuf,sizeof(g_CharSearchBuf),"%s",s.search.c_str());break;
        }
        case 1:{
            g_LibraryCollection=s.collection;
            if(s.collection==0){
                const auto& builds=AlterEgo::GW2API::GetSavedBuilds();g_LibSelectedIdx=-1;
                for(int i=0;i<(int)builds.size();++i)if(builds[i].id==s.id){g_LibSelectedIdx=i;break;}
                g_LibDetailsFetched=false;g_LibFilterModes=s.modes;
                snprintf(g_LibSearchBuf,sizeof(g_LibSearchBuf),"%s",s.search.c_str());
            }else if(s.collection==1){
                g_FashionSelected=-1;
                for(int i=0;i<(int)g_FashionLibrary.looks.size();++i)if(g_FashionLibrary.looks[i].id==s.id){g_FashionSelected=i;break;}
                g_FashionFilter=s.category;snprintf(g_FashionSearch,sizeof(g_FashionSearch),"%s",s.search.c_str());
            }else if(s.collection==2){
                g_ComponentSelectedId=s.id;g_ComponentSelectedKind=s.kind;g_ComponentKindFilters=s.componentKinds;
                g_ComponentSearch=s.search;g_ComponentSort=s.sort;g_ComponentUnusedOnly=s.first;g_ComponentSortAscending=s.other;
                g_ComponentSelectIdPending.clear();g_ComponentSelectNamePending.clear();g_ComponentPendingKind=-1;
            }else{g_EquipmentSetSelectedId=s.id;g_EquipmentSetSearch=s.search;}
            break;
        }
        case 2:g_SkinSelectedId=s.record;
            if(s.sub==0){
                g_SkinBrowserFilters=s.skins;g_SkinShowOwned=s.first;g_SkinShowUnowned=s.other;g_SkinViewMode=s.view;
                snprintf(g_SkinSearchFilter,sizeof(g_SkinSearchFilter),"%s",s.search.c_str());
            }else{
                g_ShopNavigationQuery=s.search;g_ShopNavigationFilters=s.skins;
                g_ShopNavigationSource=s.category;g_ShopNavigationMaxGold=s.sort;
                g_ShopNavigationFilterDirty=true;
            }
            g_SkinScrollToSkin=false;break;
        case 4:{
            const bool changed=g_AchSelectedCatId!=s.record;
            g_AchSelectedCatId=s.record;g_AchSelectedGroupId=s.id;g_NavAchievementId=s.achievement;
            g_AchHideEmptyCats=s.first;g_AchHideCompletedCats=s.other;
            snprintf(g_AchSearchBuf,sizeof(g_AchSearchBuf),"%s",s.search.c_str());
            if(s.achievement){g_AchExpandedInList.insert(s.achievement);g_AchNavigateToId=s.achievement;}
            else g_AchNavigateToId=0;
            if(changed&&s.record)FetchAchCategoryDefs(s.record,true);
            break;
        }
        case 7:AlterEgo::NativeAccounts::RestoreInventoryNavigation(s.inventory);break;
        case 5:g_LibCompareChar=s.id;g_LibCompareBuildId=s.second;
            g_LibCompareSavedManual=s.manual;g_LibComparePreviousTarget=s.compareTarget;
            g_LibCompareBuildTab=s.buildTab;g_LibCompareEquipTab=s.equipTab;
            g_LibCompareShowMatches=s.first;g_LibCompareExactItems=s.other;break;
    }
}
static ImGuiTabItemFlags NavigationDetailFlags(int sub){
    return g_NavRestore&&g_NavRestore->page==0&&g_NavRestore->sub==sub?ImGuiTabItemFlags_SetSelected:0;
}
static ImGuiTabItemFlags NavigationTabFlags(int page){
    return g_NavRestore&&g_NavRestore->page==page?ImGuiTabItemFlags_SetSelected:0;
}
static void BeginNavigationFrame(){
    if(g_NavRestore&&g_NavRestore->page==2){
        g_SkinSwitchToBrowser=g_NavRestore->sub==0;
        g_SkinSwitchToShopping=g_NavRestore->sub==1;
    }
}
static void MoveNavigation(int direction){
    if(g_NavRestore||!g_Navigation.CanMove(direction))return;
    // Latest filters/scroll are retained in the current entry, not new entries.
    if(const auto* s=g_Navigation.Move(direction)){
        g_NavRestore=*s;g_NavRestoreFrames=6;g_NavIntentFrames=0;
        RestoreNavigation(*s);
        g_SwitchToLibrary=false;g_SwitchToCompare=false;g_SwitchToSkinventory=false;g_SwitchToInventory=false;
    }
}
static UINT NavigationWndProc(HWND,UINT msg,WPARAM wParam,LPARAM){
    AlterEgo::Callbacks::Gate::Lease lease(AlterEgo::Callbacks::Runtime());
    if(!lease)return 1;
    unsigned bit=0;int direction=0;bool up=false;
    if(msg==WM_XBUTTONDOWN||msg==WM_XBUTTONUP||msg==WM_XBUTTONDBLCLK){
        const auto x=HIWORD(wParam);if(x!=XBUTTON1&&x!=XBUTTON2)return 1;
        direction=x==XBUTTON1?-1:1;bit=x==XBUTTON1?1:2;
        up=msg==WM_XBUTTONUP;
    }else if(msg==WM_SYSKEYDOWN||msg==WM_SYSKEYUP||msg==WM_KEYDOWN||msg==WM_KEYUP){
        if(wParam!=VK_LEFT&&wParam!=VK_RIGHT)return 1;
        direction=wParam==VK_LEFT?-1:1;bit=wParam==VK_LEFT?4:8;
        up=msg==WM_SYSKEYUP||msg==WM_KEYUP;
    }else return 1;
    if(up){if(g_NavConsumedButtons.fetch_and(~bit)&bit)return 0;return 1;}
    if(g_NavConsumedButtons.load()&bit)return 0;
    const unsigned scope=g_NavInputMask.load();
    const bool mouse=bit<=2;
    if(!(scope&(direction<0?4:8))||!(scope&(mouse?1:2))||
       (!mouse&&!(GetKeyState(VK_MENU)&0x8000)))return 1;
    g_NavConsumedButtons.fetch_or(bit);g_NavRequest.store(direction);return 0;
}
static void RenderNavigationControls(){
    const bool blocked=NavigationBlocked()||g_NavRestore.has_value();
    int requested=g_NavRequest.exchange(0);
    const auto button=[&](const char* label,const char* tip,int direction){
        const bool enabled=!blocked&&g_Navigation.CanMove(direction);
        if(!enabled){ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true);ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*.45f);}
        if(AlterEgo::UI::Button(label,ImVec2(ImGui::GetFrameHeight(),0),AlterEgo::UI::ButtonRole::Compact)&&enabled)requested=direction;
        if(!enabled){ImGui::PopStyleVar();ImGui::PopItemFlag();}
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",blocked?"Finish or cancel the current edit/dialog first.":tip);
    };
    button("<##nav_back","Back (mouse back / Alt+Left)",-1);ImGui::SameLine();
    button(">##nav_forward","Forward (mouse forward / Alt+Right)",1);
    if(!blocked&&requested)MoveNavigation(requested);
    ImGui::SameLine();
}
static void EndNavigationFrame(){
    if(g_NavRestore){
        const auto& s=*g_NavRestore;
        if(g_MainTab==s.page&&(s.page!=2||g_SkinActiveTab==s.sub)&&(s.page!=0||g_SelectedTab==s.sub)){
            auto* root=ImGui::GetCurrentWindow();
            for(auto* w:GImGui->Windows)
                if(w->RootWindow==root&&w->LastFrameActive==ImGui::GetFrameCount()){
                    const auto it=s.scroll.find(w->ID);if(it!=s.scroll.end())ImGui::SetScrollY(w,it->second);
                }
            g_NavRestore.reset();g_NavIntentFrames=0;
            // Re-snapshot on the next frame after scroll targets are applied.
        }else if(--g_NavRestoreFrames<=0){g_NavRestore.reset();g_NavIntentFrames=0;}
    }else if(!g_SwitchToLibrary&&!g_SwitchToCompare&&!g_SwitchToSkinventory&&!g_SwitchToInventory){
        auto current=CaptureNavigation();
        const auto* old=g_Navigation.Current();
        const bool changed=old&&!old->SameDestination(current);
        const bool deliberate=g_NavIntentFrames>0;
        g_Navigation.Observe(std::move(current),deliberate);
        if(changed&&deliberate)g_NavIntentFrames=0;
        else if(g_NavIntentFrames>0)--g_NavIntentFrames;
    }
    unsigned scope=0;
    if(!NavigationBlocked()&&!g_NavRestore&&!ImGui::IsAnyItemActive()){
        if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))scope|=1;
        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))scope|=2;
        if(g_Navigation.CanMove(-1))scope|=4;
        if(g_Navigation.CanMove(1))scope|=8;
    }
    g_NavInputMask.store(scope);
}
