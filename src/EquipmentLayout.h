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
    void Finish() const{
        const auto bottom=std::max(origin.y+icon,y);
        ImGui::SetCursorScreenPos(ImVec2(origin.x,bottom));
        ImGui::Dummy(ImVec2(0,0));
    }
};
template<class Slot,class Header>
void EquipmentLayout(const char* id,bool aquatic,Slot slot,Header header){
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(4,2));
    constexpr const char* armor[]={"Helm","Shoulders","Coat","Gloves","Leggings","Boots"};
    constexpr const char* trinkets[]={"Backpack","Accessory1","Accessory2","Amulet","Ring1","Ring2"};
    if(ImGui::BeginTable("##armor_trinkets",2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_RowBg)){
        ImGui::TableNextRow();
        ImGui::TableNextColumn();header("Armor",4);
        ImGui::TableNextColumn();header("Trinkets",6);
        for(int n=0;n<6;++n){
            ImGui::TableNextRow();
            ImGui::TableNextColumn();slot(armor[n]);
            ImGui::TableNextColumn();slot(trinkets[n]);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();header("Weapons",2);
    const bool narrow=ImGui::GetContentRegionAvail().x<360.f;
    if(narrow){
        for(int n=0;n<2;++n){
            ImGui::TextColored(SectionGold,"%s",n==0?"I":"II");
            slot(n==0?"WeaponA1":"WeaponB1");slot(n==0?"WeaponA2":"WeaponB2");
        }
    }else if(ImGui::BeginTable("##weapon_sets",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchSame)){
        ImGui::TableSetupColumn("##set",ImGuiTableColumnFlags_WidthFixed,24.f);
        ImGui::TableSetupColumn("##main",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##off",ImGuiTableColumnFlags_WidthStretch);
        for(int n=0;n<2;++n){
            ImGui::TableNextRow();
            ImGui::TableNextColumn();ImGui::TextColored(SectionGold,"%s",n==0?"I":"II");
            ImGui::TableNextColumn();slot(n==0?"WeaponA1":"WeaponB1");
            ImGui::TableNextColumn();slot(n==0?"WeaponA2":"WeaponB2");
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();header("Relic",7);slot("Relic");
    if(aquatic){
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
