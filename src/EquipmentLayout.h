#pragma once
#include "AddonShared.h"
#include <algorithm>
#include <string>

namespace AlterEgo::UI {
// All equipment views use one geometry. Domain renderers retain their icon
// interactions, tooltip, data source and editing semantics.
struct EquipmentText {
    ImVec2 origin;
    float icon,width,x,y;
    EquipmentText(ImVec2 pos,float iconSize,float available):
        origin(pos),icon(iconSize),width(std::max(1.f,available-iconSize-6.f)),x(pos.x+iconSize+6.f),y(pos.y){}
    void Line(const std::string& value,ImU32 color){
        if(value.empty())return;
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),
            ImVec2(x,y),color,value.c_str(),nullptr,width);
        y+=ImGui::CalcTextSize(value.c_str(),nullptr,false,width).y;
    }
    void ContextTarget() const {
        ImGui::SetCursorScreenPos(ImVec2(x,origin.y));
        ImGui::Dummy(ImVec2(width,std::max(1.f,y-origin.y)));
    }
    void Finish() const{
        const auto bottom=std::max(origin.y+icon,y);
        ImGui::SetCursorScreenPos(ImVec2(origin.x,bottom));
        ImGui::Dummy(ImVec2(0,0));
    }
};
enum EquipmentSections : unsigned { ArmorSection=1,TrinketSection=2,WeaponSection=4,RelicSection=8,AquaticSection=16,AllEquipmentSections=31 };
template<class Slot,class Header>
void EquipmentLayout(const char* id,bool aquatic,Slot slot,Header header,unsigned sections=AllEquipmentSections){
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(4,2));
    constexpr const char* armor[]={"Helm","Shoulders","Coat","Gloves","Leggings","Boots"};
    constexpr const char* trinkets[]={"Backpack","Accessory1","Accessory2","Amulet","Ring1","Ring2"};
    const bool showArmor=sections&ArmorSection,showTrinkets=sections&TrinketSection;
    if((showArmor||showTrinkets)&&ImGui::BeginTable("##armor_trinkets",showArmor&&showTrinkets?2:1,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_RowBg)){
        ImGui::TableNextRow();
        if(showArmor){ImGui::TableNextColumn();header("Armor",4);}
        if(showTrinkets){ImGui::TableNextColumn();header("Trinkets",6);}
        for(int n=0;n<6;++n){
            ImGui::TableNextRow();
            if(showArmor){ImGui::TableNextColumn();slot(armor[n]);}
            if(showTrinkets){ImGui::TableNextColumn();slot(trinkets[n]);}
        }
        ImGui::EndTable();
    }
    const bool narrow=ImGui::GetContentRegionAvail().x<360.f;
    if(sections&WeaponSection){
    ImGui::Spacing();header("Weapons",2);
    // Keep the set marker in its own compact column, sized to the current font.
    // At narrow widths the two weapons stack inside that same set's content cell.
    if(ImGui::BeginTable("##weapon_sets",narrow?2:3,ImGuiTableFlags_SizingStretchSame)){
        ImGui::TableSetupColumn("##set",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("Set II").x);
        ImGui::TableSetupColumn("##main",ImGuiTableColumnFlags_WidthStretch);
        if(!narrow)ImGui::TableSetupColumn("##off",ImGuiTableColumnFlags_WidthStretch);
        for(int n=0;n<2;++n){
            // An unshaded spacer separates the groups without shifting the banding.
            if(n==1)ImGui::TableNextRow(ImGuiTableRowFlags_None,8.f);
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(n==0?ImGuiCol_TableRowBg:ImGuiCol_TableRowBgAlt));
            ImGui::TableSetColumnIndex(0);
            const auto labelPos=ImGui::GetCursorScreenPos();
            ImGui::TableSetColumnIndex(1);
            slot(n==0?"WeaponA1":"WeaponB1");
            float bottom=ImGui::GetCursorScreenPos().y;
            if(!narrow)ImGui::TableSetColumnIndex(2);
            slot(n==0?"WeaponA2":"WeaponB2");
            bottom=std::max(bottom,ImGui::GetCursorScreenPos().y);
            // Both slot renderers advance the cursor past their wrapped content.
            // Revisit the label cell on this row to center it against the taller slot.
            ImGui::TableSetColumnIndex(0);
            const float contentHeight=bottom-labelPos.y-ImGui::GetStyle().ItemSpacing.y;
            ImGui::SetCursorScreenPos(ImVec2(labelPos.x,labelPos.y+
                std::max(0.f,(contentHeight-ImGui::GetTextLineHeight())*0.5f)));
            ImGui::TextColored(SectionGold,"%s",n==0?"Set I":"Set II");
        }
        ImGui::EndTable();
    }
    }
    if(sections&RelicSection){ImGui::Spacing();header("Relic",7);slot("Relic");}
    if(aquatic&&(sections&AquaticSection)){
        ImGui::Spacing();header("Aquatic",-1);slot("HelmAquatic");
        if(narrow){slot("WeaponAquaticA");slot("WeaponAquaticB");}
        else if(ImGui::BeginTable("##aquatic",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchSame)){
            ImGui::TableNextRow();ImGui::TableNextColumn();slot("WeaponAquaticA");
            ImGui::TableNextColumn();slot("WeaponAquaticB");ImGui::EndTable();
        }
    }
    ImGui::PopStyleVar();ImGui::PopID();
}
}
