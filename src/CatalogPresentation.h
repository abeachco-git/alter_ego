#pragma once
#include "AddonShared.h"
#include "IconManager.h"
#include "CatalogRowCore.h"
#include <algorithm>
namespace AlterEgo::UI {
// A fixed-height list row; caller owns stable ID, selection, context menu,
// and domain-specific trailing content. No catalog requests from this helper.
inline float CatalogRowHeight(){return CatalogRowPitch(ImGui::GetTextLineHeight());}
// Shared striping for selectable catalog rows and ordinary wallet rows.
inline void CatalogRowBackground(int row,ImVec2 p,ImVec2 end,bool selected=false,bool hovered=false){
    const ImU32 bg=selected?IM_COL32(80,64,28,90):hovered?IM_COL32(60,56,40,60):row&1?IM_COL32(255,255,255,10):0;
    if(bg)ImGui::GetWindowDrawList()->AddRectFilled(p,end,bg,2.f);
}
// One graphic and stable hit target for every pin surface.
inline bool PinButton(const char* id,bool pinned,const char* pinTip,const char* unpinTip,float size=0.f){
    if(size<=0.f)size=ImGui::GetFrameHeight();
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const ImVec2 end(p.x+size,p.y+size);
    const bool clicked=ImGui::InvisibleButton(id,ImVec2(size,size));
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(p,end,pinned?IM_COL32(80,64,28,200):
        ImGui::IsItemHovered()?IM_COL32(60,56,40,150):IM_COL32(0,0,0,130),3.f);
    draw->AddRect(p,end,pinned?IM_COL32(232,196,122,230):IM_COL32(140,130,110,170),3.f);
    if(pinned){
        const auto text=ImGui::CalcTextSize("*");
        draw->AddText(ImVec2(p.x+(size-text.x)*.5f,p.y+(size-text.y)*.5f),
            IM_COL32(245,220,150,255),"*");
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",pinned?unpinTip:pinTip);
    return clicked;
}
struct CatalogRowResult {bool clicked=false,hovered=false;};
// Both catalog rows and tiles use the same native activation and hover
// target. Caller supplies its stable ID scope and immediately owns the
// context menu for this item.
inline CatalogRowResult CatalogHitTarget(const char* id,ImVec2 size){
    const bool clicked=ImGui::InvisibleButton(id,size);
    return {clicked,ImGui::IsItemHovered()};
}
// The fixed-height clipper must use exactly the vertical advance made by
// each submitted line, including explicit spacing. One shared loop prevents
// individual pages from drifting or inheriting a different style.
template<class RenderLine>
inline void CatalogLines(int count,float pitch,float gap,RenderLine render){
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(ImGui::GetStyle().ItemSpacing.x,gap));
    ImGuiListClipper clipper;clipper.Begin(count,pitch);
    while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row)render(row);
    ImGui::PopStyleVar();
}
template<class RenderRow>
inline void CatalogRows(int count,RenderRow render){
    CatalogLines(count,CatalogRowHeight(),0.f,render);
}
inline void CatalogScrollToRow(int row,float pitch){
    ImGui::SetScrollY(CatalogCenteredScroll(row,pitch,ImGui::GetWindowHeight()));
}
inline CatalogRowResult CatalogRow(int row,bool selected,uint32_t iconId,
    const std::string& url,const std::string& name,const std::string& rarity,
    const std::string& trailing={},bool dim=false,int ownership=-1) {
    const float pitch=CatalogRowHeight(),height=pitch;
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const float w=ImMax(1.f,ImGui::GetContentRegionAvail().x);
    const ImVec2 end(p.x+w,p.y+height);
    // Respect native button ownership and activation (click/release or
    // keyboard navigation); do not independently poll the mouse-down event.
    const auto hit=CatalogHitTarget("##catalog_row",ImVec2(w,height));
    const bool hovered=hit.hovered;
    auto* dl=ImGui::GetWindowDrawList();
    const auto rc=GetSkinRarityColor(rarity);
    CatalogRowBackground(row,p,end,selected,hovered);
    if(selected||hovered)dl->AddRect(p,end,selected?IM_COL32(232,196,122,220):IM_COL32(197,161,85,110),2.f);
    dl->AddRectFilled(ImVec2(p.x,p.y+2),ImVec2(p.x+3,end.y-2),ImGui::ColorConvertFloat4ToU32(dim?ImVec4(rc.x*.55f,rc.y*.55f,rc.z*.55f,.85f):rc));
    const float sz=16.f;
    const ImVec2 ip(p.x+8,p.y+(height-sz)*.5f),ie(ip.x+sz,ip.y+sz);
    dl->AddRectFilled(ip,ie,ImGui::ColorConvertFloat4ToU32(ImVec4(rc.x*.18f,rc.y*.18f,rc.z*.18f,1)),1.5f);
    auto* tex=iconId?IconManager::GetIcon(iconId):nullptr;
    if(tex&&tex->Resource)dl->AddImage(tex->Resource,ip,ie,ImVec2(0,0),ImVec2(1,1),dim?IM_COL32(170,170,170,130):IM_COL32_WHITE);
    else if(iconId&&!url.empty())IconManager::RequestIcon(iconId,url,true);
    const auto textColor=ImGui::GetColorU32(dim?ImGuiCol_TextDisabled:ImGuiCol_Text);
    const float ty=p.y+(height-ImGui::GetTextLineHeight())*.5f;
    const float tailWidth=trailing.empty()?0.f:ImGui::CalcTextSize(trailing.c_str()).x+12.f;
    const float right=end.x-6.f-(ownership>=0?16.f:tailWidth);
    const float left=ie.x+6.f;
    if(right>left){dl->PushClipRect(ImVec2(left,p.y),ImVec2(right,end.y),true);
        dl->AddText(ImVec2(left,ty),textColor,name.c_str());dl->PopClipRect();}
    if(!trailing.empty()&&w>tailWidth+30.f)dl->AddText(ImVec2(end.x-tailWidth+6,ty),ImGui::GetColorU32(ImGuiCol_TextDisabled),trailing.c_str());
    if(ownership>=0){const ImVec2 dot(end.x-12,p.y+height*.5f);
        if(ownership){dl->AddCircleFilled(dot,4,IM_COL32(50,110,60,220));dl->AddCircle(dot,4,IM_COL32(111,204,122,255),14,1.2f);
            dl->AddLine(ImVec2(dot.x-2,dot.y),ImVec2(dot.x-.5f,dot.y+1.5f),IM_COL32(180,240,190,255),1.5f);
            dl->AddLine(ImVec2(dot.x-.5f,dot.y+1.5f),ImVec2(dot.x+2.5f,dot.y-2),IM_COL32(180,240,190,255),1.5f);}
        else dl->AddCircle(dot,3.5f,IM_COL32(120,110,90,200),14,1.2f);}
    if(hovered&&ImGui::CalcTextSize(name.c_str()).x>right-left)ImGui::SetTooltip("%s",name.c_str());
    return hit;
}
// Fixed-pitch collapsible header for hierarchical catalogs. Native hit target
// and clipper geometry are the same as ordinary catalog rows.
inline bool CatalogBranchRow(int row,bool expanded,const std::string& name,
    const std::function<void(ImDrawList*,ImVec2,ImVec2)>& decorated={}){
    const auto p=ImGui::GetCursorScreenPos();
    const float h=CatalogRowHeight(),w=ImMax(1.f,ImGui::GetContentRegionAvail().x);
    const ImVec2 end(p.x+w,p.y+h);
    const auto hit=CatalogHitTarget("##catalog_branch",ImVec2(w,h));
    CatalogRowBackground(row,p,end,false,hit.hovered);
    auto* draw=ImGui::GetWindowDrawList();
    const auto color=ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec2 c(p.x+8.f,p.y+h*.5f);
    if(expanded){
        draw->AddLine(ImVec2(c.x-4,c.y-2),ImVec2(c.x,c.y+2),color,1.5f);
        draw->AddLine(ImVec2(c.x,c.y+2),ImVec2(c.x+4,c.y-2),color,1.5f);
    }else{
        draw->AddLine(ImVec2(c.x-2,c.y-4),ImVec2(c.x+2,c.y),color,1.5f);
        draw->AddLine(ImVec2(c.x+2,c.y),ImVec2(c.x-2,c.y+4),color,1.5f);
    }
    const ImVec2 left(p.x+18.f,p.y);
    if(end.x>left.x){
        draw->PushClipRect(left,end,true);
        if(decorated)decorated(draw,left,end);
        else draw->AddText(ImVec2(left.x,p.y+(h-ImGui::GetTextLineHeight())*.5f),color,name.c_str());
        draw->PopClipRect();
    }
    if(hit.hovered&&ImGui::CalcTextSize(name.c_str()).x+(decorated?32.f:0.f)>end.x-left.x)
        ImGui::SetTooltip("%s",name.c_str());
    return hit.clicked;
}
inline void CatalogBanner(const std::string& name,const std::string& subtitle,
                          const std::string& rarity,const std::string& badge={}) {
    const auto p=ImGui::GetCursorScreenPos();const float w=ImMax(1.f,ImGui::GetContentRegionAvail().x);
    const float pad=10.f,wrap=ImMax(1.f,w-2*pad-4);
    const auto rc=GetSkinRarityColor(rarity);
    const float nh=ImGui::CalcTextSize(name.c_str(),nullptr,false,wrap).y;
    const float sh=ImGui::CalcTextSize(subtitle.c_str(),nullptr,false,wrap).y;
    const float bh=badge.empty()?0.f:ImGui::GetTextLineHeight()+4.f;
    const ImVec2 end(p.x+w,p.y+2*pad+nh+4+sh+bh);
    auto* dl=ImGui::GetWindowDrawList();const auto tint=ImGui::ColorConvertFloat4ToU32(ImVec4(rc.x*.22f,rc.y*.22f,rc.z*.22f,.95f));
    dl->AddRectFilledMultiColor(p,end,tint,IM_COL32(20,20,24,250),IM_COL32(20,20,24,250),tint);
    dl->AddRectFilled(ImVec2(p.x,p.y+3),ImVec2(p.x+3,end.y-3),ImGui::ColorConvertFloat4ToU32(rc));
    ImGui::Dummy(ImVec2(w,end.y-p.y));
    const ImVec2 origin(p.x+pad+4,p.y+pad);
    dl->AddText(ImGui::GetFont(),ImGui::GetFontSize(),origin,ImGui::GetColorU32(ImGuiCol_Text),name.c_str(),nullptr,wrap);
    dl->AddText(ImGui::GetFont(),ImGui::GetFontSize(),ImVec2(origin.x,origin.y+nh+4),ImGui::GetColorU32(ImGuiCol_TextDisabled),subtitle.c_str(),nullptr,wrap);
    if(!badge.empty())dl->AddText(ImVec2(origin.x,origin.y+nh+4+sh+4),ImGui::GetColorU32(ImGuiCol_TextDisabled),badge.c_str());
}
}
