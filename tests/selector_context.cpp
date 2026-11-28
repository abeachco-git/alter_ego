#include "../src/UIControls.h"
#include "../src/RefreshUI.h"
#include "../src/EntityMenuCore.h"
#include "../src/FastLoadUIState.h"
#include <cassert>
int case_selector_context(int, char**) {
    using namespace AlterEgo::UI;
    using namespace AlterEgo::FastLoadUI;
    assert(ParseMode("")==Mode::Enabled&&ParseMode("invalid")==Mode::Enabled);
    for(auto mode:{Mode::Disabled,Mode::Enabled,Mode::Confirm})assert(ParseMode(ModeName(mode))==mode);
    Confirmation request;assert(!request.Pending());
    request.Begin({"Other","account"},"Active","account","scope");
    assert(request.Pending()&&request.needsOpen);
    assert(request.Matches(Mode::Confirm,"Active","account","scope"));
    assert(!request.Matches(Mode::Disabled,"Active","account","scope"));
    assert(!request.Matches(Mode::Enabled,"Active","account","scope"));
    assert(!request.Matches(Mode::Confirm,"Other","account","scope"));
    assert(!request.Matches(Mode::Confirm,"Active","other-account","scope"));
    assert(!request.Matches(Mode::Confirm,"Active","account","other-scope"));
    request.Clear();assert(!request.Pending()&&!request.needsOpen);
    assert(IsActive("Active","account","Active","account"));
    assert(!IsActive("Other","account","Active","account"));
    assert(!IsActive("Active","other","Active","account"));
    assert(!IsActive("Active","","Active",""));
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.DisplaySize=ImVec2(800,600);
    unsigned char* pixels=nullptr;int width=0,height=0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    int selected=0,context=-1;
    ImVec2 second,preview;
    const ImVec2 outside(-100,-100);
    auto frame=[&](bool open,ImVec2 mouse,bool right,bool dismiss=false){
        io.DeltaTime=1.f/60.f;io.MousePos=mouse;io.MouseDown[1]=right;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(40,40));ImGui::SetNextWindowSize(ImVec2(400,300));
        ImGui::Begin("Selector test");
        if(dismiss)ImGui::ClosePopupToLevel(0,true);
        if(open)ImGui::OpenPopup("##selector");
        if(BeginCombo("##selector","Duplicate",0,180)){
            for(int i=0;i<2;++i){
                ImGui::PushID(i+1);
                if(EntityOption("Duplicate",selected==i,[&]{context=i;ImGui::TextUnformatted("Context");}))selected=i;
                if(i==1){auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();second=ImVec2((a.x+b.x)*.5f,(a.y+b.y)*.5f);}
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }else{
            auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();preview=ImVec2((a.x+b.x)*.5f,(a.y+b.y)*.5f);
            ImGui::PushID(selected+1);
            EntityContext("##selected_actions",[&]{context=selected+10;ImGui::TextUnformatted("Selected context");});
            ImGui::PopID();
        }
        ImGui::End();ImGui::Render();
    };
    frame(false,outside,false);frame(false,outside,false);
    frame(true,outside,false);frame(false,outside,false);frame(false,outside,false);
    const auto target=second;
    frame(false,target,true);frame(false,target,false);frame(false,target,false);
    assert(selected==0);assert(context==1);
    frame(false,outside,false,true);frame(false,outside,false);
    context=-1;const auto closed=preview;
    frame(false,closed,true);frame(false,closed,false);frame(false,closed,false);
    assert(selected==0);assert(context==10);
    // The actual native reference row must cover both columns and retain one
    // right-click target, including a wrapped value and the inherited glyph.
    frame(false,outside,false,true);frame(false,outside,false);
    const auto one=MeasureReferenceRow(13,13,13,200);
    const auto wrapped=MeasureReferenceRow(13,39,13,100);
    const auto narrow=MeasureReferenceRow(39,13,13,30);
    assert(one.labelY==one.valueY&&one.linkY==one.height*.5f);
    assert(wrapped.height==39&&wrapped.labelY==13&&wrapped.valueY==0&&wrapped.linkY==19.5f);
    assert(narrow.height==39&&narrow.labelY==0&&narrow.valueY==13);
    ImVec2 labelHit,linkHit,valueHit;int rowContext=0;bool hovered=false,clicked=false;
    auto referenceFrame=[&](ImVec2 mouse,bool right,bool left=false,bool dismiss=false,float w=400.f){
        io.DeltaTime=1.f/60.f;io.MousePos=mouse;io.MouseDown[1]=right;io.MouseDown[0]=left;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(40,40));ImGui::SetNextWindowSize(ImVec2(w,300));
        ImGui::Begin("Reference test");
        if(dismiss)ImGui::ClosePopupToLevel(0,true);
        if(ImGui::BeginTable("refs",2,ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("Category",ImGuiTableColumnFlags_WidthFixed,116.f);
            ImGui::TableSetupColumn("Selection",ImGuiTableColumnFlags_WidthStretch);
            clicked=ReferencePreviewRow("##ref","Weapons",
                "Long weapon description with a second and third wrapped line for narrow view",true,"Linked");
            const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
            const auto* table=ImGui::GetCurrentContext()->CurrentTable;
            assert(a.x<=table->Columns[0].WorkMinX&&b.x>=table->Columns[1].WorkMaxX);
            assert(b.y-a.y>ImGui::GetTextLineHeight());
            labelHit=ImVec2(table->Columns[0].WorkMinX+5,(a.y+b.y)*.5f);
            valueHit=ImVec2(table->Columns[1].WorkMinX+5,(a.y+b.y)*.5f);
            linkHit=ImVec2(table->Columns[1].WorkMaxX-7,(a.y+b.y)*.5f);
            hovered=ImGui::IsItemHovered();
            EntityContext("##ref_actions",[&]{++rowContext;ImGui::TextUnformatted("Actions");});
            ImGui::PushID("missing");
            ReferencePreviewRow("##ref","Fashion","Not set",false,nullptr,false);
            assert(ImGui::GetItemRectMin().y>=b.y-ImGui::GetStyle().ItemSpacing.y);
            ImGui::PopID();
            ImGui::EndTable();
        }
        ImGui::End();ImGui::Render();
    };
    referenceFrame(outside,false);referenceFrame(outside,false);
    referenceFrame(labelHit,false);assert(hovered);
    referenceFrame(valueHit,false);assert(hovered);
    referenceFrame(linkHit,false);assert(hovered);
    referenceFrame(linkHit,true);referenceFrame(linkHit,false);referenceFrame(linkHit,false);
    assert(rowContext>0&&!clicked);
    referenceFrame(outside,false,false,true);referenceFrame(outside,false);
    referenceFrame(labelHit,false,true);referenceFrame(labelHit,false,false);assert(clicked);
    referenceFrame(outside,false,false,false,260.f);referenceFrame(outside,false,false,false,260.f);
    referenceFrame(linkHit,false,false,false,260.f);assert(hovered);
    // Compare the actual native tab rectangles, including the grouped
    // boundary, at the same style used by an ordinary detail strip.
    auto tabFrame=[&](float w,bool grouped){
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(30,30));ImGui::SetNextWindowSize(ImVec2(w,200));
        ImGui::Begin(grouped?"Grouped tabs":"Ordinary tabs");
        std::vector<ImRect> rects;
        if(ImGui::BeginTabBar("tabs")){
            auto* bar=GImGui->CurrentTabBar;
            const auto next=ImGui::GetID("Third");
            TabGroupGaps gaps(bar,{grouped?next:0u});
            for(const char* name:{"First","Second","Third","Fourth"}){
                const bool visible=ImGui::BeginTabItem(name);
                if(rects.empty())gaps.AfterFirstTab();
                rects.emplace_back(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
                if(visible)ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();ImGui::Render();return rects;
    };
    tabFrame(700,true);tabFrame(700,false);
    tabFrame(700,true);tabFrame(700,false);
    const auto grouped=tabFrame(700,true),ordinary=tabFrame(700,false);
    assert(grouped.size()==4&&ordinary.size()==4);
    for(size_t i=1;i<4;++i){
        const float base=ordinary[i].Min.x-ordinary[i-1].Max.x;
        const float gap=grouped[i].Min.x-grouped[i-1].Max.x;
        assert(std::abs(gap-base-(i==2?TabGroupExtraGap:0.f))<1.f);
    }
    // Entity grouping deduplicates exact links, not equal display names.
    std::vector<AlterEgo::EntityMenu::Entity> entities;
    using AlterEgo::EntityMenu::AppendUnique;
    AppendUnique(entities,{"Rune","",""});
    assert(entities.empty());
    AppendUnique(entities,{"Rune","","link-a"});
    AppendUnique(entities,{"Rune","Equal","link-a"});
    AppendUnique(entities,{"Sigil","Equal","link-b"});
    AppendUnique(entities,{"Infusion","Stored only",""});
    AppendUnique(entities,{"Infusion","Stored only",""});
    assert(entities.size()==3&&entities[0].name=="Equal"&&entities[1].link=="link-b");
    assert(entities[0].Key()!=entities[1].Key());
    assert(entities[0].Key()==(AlterEgo::EntityMenu::Entity{"Rune","Renamed","link-a"}.Key()));
    assert(entities[2].link.empty()&&entities[2].Label()=="Stored only");
    assert((AlterEgo::EntityMenu::Entity{"Unknown","","link-c"}.Label()=="Unknown"));
    // Native menu IDs are independent of visible name and action label.
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(400,300));
    ImGui::Begin("Menu section test");
    ImGui::OpenPopup("mixed");
    if(ImGui::BeginPopup("mixed")){
        const auto root=ImGui::GetID(MenuIconLabel("Copy Name").c_str());
        ImGuiID characterCopy=0,characterShare=0;
        {
            MenuSection section("character","Character - Duplicate");
            characterCopy=ImGui::GetID(MenuIconLabel("Copy Name").c_str());
            characterShare=ImGui::GetID(MenuIconLabel("Share").c_str());
            MenuAction("Copy Name",MenuIcon::Copy);
            if(BeginActionMenu("Share",MenuIcon::Share))ImGui::EndMenu();
        }
        assert(ImGui::GetID(MenuIconLabel("Copy Name").c_str())==root);
        {
            MenuSection section("loadout","Loadout - Duplicate",true);
            assert(ImGui::GetID(MenuIconLabel("Copy Name").c_str())!=characterCopy);
            assert(ImGui::GetID(MenuIconLabel("Share").c_str())!=characterShare);
            MenuAction("Copy Name",MenuIcon::Copy);
            if(BeginActionMenu("Share",MenuIcon::Share))ImGui::EndMenu();
        }
        {
            MenuSection section("character","Character - Renamed");
            assert(ImGui::GetID(MenuIconLabel("Copy Name").c_str())==characterCopy);
        }
        assert(ImGui::GetID(MenuIconLabel("Copy Name").c_str())==root);
        ImGui::PushID(0);const auto firstSigil=ImGui::GetID("##single_upgrade_edit");ImGui::PopID();
        ImGui::PushID(1);const auto secondSigil=ImGui::GetID("##single_upgrade_edit");ImGui::PopID();
        assert(firstSigil!=secondSigil);
        MenuSectionHeading(std::string(300,'W').c_str());
        assert(ImGui::GetItemRectSize().x<=ImGui::GetFontSize()*24.f+1.f);
        assert(MenuObjectHeading("Loadout","Mixed Case")=="Loadout - Mixed Case");
        ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }else assert(false);
    ImGui::End();ImGui::Render();
    // Optional preview shared by Characters, Skinventory and Fashion.
    // Hidden controls must leave the previous item and layout untouched.
    io.MousePos=outside;io.MouseDown[0]=io.MouseDown[1]=false;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(40,40));
    ImGui::SetNextWindowSize(ImVec2(400,300));
    ImGui::Begin("Native preview test");
    Button("Copy");
    const auto beforePreview=ImGui::GetCursorScreenPos();
    const auto previousItem=ImGui::GetItemRectMin();
    assert(!NativePreviewButton(false));
    assert(ImGui::GetCursorScreenPos().x==beforePreview.x&&ImGui::GetCursorScreenPos().y==beforePreview.y);
    assert(ImGui::GetItemRectMin().x==previousItem.x&&ImGui::GetItemRectMin().y==previousItem.y);
    NativePreviewButton(true);
    assert(ImGui::GetItemRectMin().x>previousItem.x);
    assert(ImGui::GetItemRectSize().x>=CompactButtonWidth(NativePreviewLabel)-1.f);
    ImGui::OpenPopup("preview actions");
    if(ImGui::BeginPopup("preview actions")){
        MenuAction("Copy",MenuIcon::Copy);
        const auto beforeMenuPreview=ImGui::GetCursorScreenPos();
        for(bool separate:{false,true}){
            assert(!NativePreviewMenuAction(false,separate));
            assert(ImGui::GetCursorScreenPos().x==beforeMenuPreview.x&&ImGui::GetCursorScreenPos().y==beforeMenuPreview.y);
        }
        NativePreviewMenuAction(true,true);
        assert(ImGui::GetCursorScreenPos().y>beforeMenuPreview.y);
        ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }else assert(false);
    ImGui::End();ImGui::Render();
    // Footer reserve follows actual buttons and messages, not a fixed blank band.
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(400,300));
    ImGui::Begin("Footer reserve test");
    const float ordinary=ActionFooterReserve();
    assert(std::abs(ordinary-ActionButtonHeight()-ImGui::GetStyle().ItemSpacing.y)<.01f);
    assert(ActionFooterReserve("Save","Cancel",true)>ordinary);
    assert(ActionFooterReserve("Save","Cancel",false,"A validation error")>ordinary);
    const float y=ImGui::GetCursorScreenPos().y;
    const float available=ImGui::GetContentRegionAvail().y;
    BeginChild("body",ImVec2(0,-ordinary),false);ImGui::EndChild();
    SaveCancel("footer-reserve");
    assert(std::abs(ImGui::GetItemRectMax().y-(y+available))<=3.f+ImGui::GetStyle().ItemSpacing.y);
    ImGui::End();ImGui::Render();
    // Exercise the shared background policy in the native ImGui context.
    assert(ClampWindowTransparency(-1)==0&&ClampWindowTransparency(100)==80);
    assert(ClampWindowTransparency(25)==25);
    assert(BackgroundAlpha(.96f,0)==.96f);
    assert(std::abs(BackgroundAlpha(.96f,80)-.192f)<.0001f);
    assert(std::abs(BackgroundAlpha(.80f,50)-.4f)<.0001f);
    assert(BackgroundAlpha(.96f,0,BackgroundRole::Protected)==.96f);
    assert(BackgroundAlpha(.96f,50,BackgroundRole::Protected)==ProtectedBackgroundMinimum);
    assert(BackgroundAlpha(.96f,80,BackgroundRole::Protected)==ProtectedBackgroundMinimum);
    assert(BackgroundAlpha(.65f,50,BackgroundRole::Protected)==.65f);
    for(int p:{0,20,80})assert(BackgroundAlpha(.65f,p,BackgroundRole::Editor)==.65f);
    NameActionDraft draft;
    draft.Start("Original",false,NameActionDraft::Mode::Custom);
    assert(!draft.Dirty());
    draft.Regenerate("Original");
    assert(draft.Dirty()&&draft.mode==NameActionDraft::Mode::Automatic);
    draft.ManualEdit();assert(!draft.Dirty());
    draft.Regenerate("Generated");assert(draft.Dirty()&&draft.name=="Generated");
    draft.Start("Original",true);assert(!draft.Dirty()&&draft.name=="Original - Copy");
    assert(ProtectedOverlayAlpha(.96f,0)==0.f&&ProtectedOverlayAlpha(.96f,5)==0.f);
    for(int p:{0,5,20,50,80}){
        const float a=BackgroundAlpha(.96f,p),overlay=ProtectedOverlayAlpha(.96f,p);
        assert(std::abs(a+(1.f-a)*overlay-BackgroundAlpha(.96f,p,BackgroundRole::Protected))<.0001f);
    }
    const auto base=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const auto child=ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
    WindowTransparencyPercent=50;
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(400,300));
    BeginWindow("Background test");
    assert(!PreserveChildBackground(ImGui::GetCurrentWindowRead()));
    const auto cursor=ImGui::GetCursorScreenPos();
    const auto before=ImGui::GetWindowDrawList()->VtxBuffer.Size;
    DrawProtectedBackground(cursor,ImVec2(cursor.x+100,cursor.y+30));
    assert(ImGui::GetWindowDrawList()->VtxBuffer.Size>before);
    assert(ImGui::GetCursorScreenPos().x==cursor.x&&ImGui::GetCursorScreenPos().y==cursor.y);
    const ImVec4 dim(.2f,.2f,.2f,1.f);
    assert(ReadableMetadataColor(dim,0).x==dim.x);
    assert(ReadableMetadataColor(dim,50).x>dim.x);
    assert(ReadableMetadataColor(dim,50).w==dim.w);
    BeginChild("Panel",ImVec2(100,80),true);
    assert(!PreserveChildBackground(ImGui::GetCurrentWindowRead()));
    ImGui::EndChild();
    ImGui::OpenPopup("Modal");
    if(ImGui::BeginPopupModal("Modal")){
        assert(PreserveChildBackground(ImGui::GetCurrentWindowRead()));
        const auto p=ImGui::GetCursorScreenPos();
        const auto count=ImGui::GetWindowDrawList()->VtxBuffer.Size;
        DrawProtectedBackground(p,ImVec2(p.x+100,p.y+30));
        assert(ImGui::GetWindowDrawList()->VtxBuffer.Size==count);
        BeginChild("Modal panel",ImVec2(100,80),true);
        assert(PreserveChildBackground(ImGui::GetCurrentWindowRead()));
        ImGui::EndChild();
        ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    const float footerY=ImGui::GetCursorScreenPos().y;
    const float expectedY=footerY+(ActionButtonHeight()-ImGui::GetTextLineHeight())*.5f;
    AlterEgo::RefreshUI::Row("test_footer",0,"test",AlterEgo::RefreshUI::State::Ready,"");
    assert(std::abs(ImGui::GetItemRectMin().y-expectedY)<=1.f);
    ImGui::End();
    ImGui::SetNextWindowSize(ImVec2(400,300));
    BeginWindow("Editor background test",nullptr,0,BackgroundRole::Editor);
    assert(PreserveChildBackground(ImGui::GetCurrentWindowRead()));
    BeginChild("Editor panel",ImVec2(100,80),true);
    assert(PreserveChildBackground(ImGui::GetCurrentWindowRead()));
    BeginChild("Nested panel",ImVec2(50,40),true);
    assert(PreserveChildBackground(ImGui::GetCurrentWindowRead()));
    ImGui::EndChild();ImGui::EndChild();ImGui::End();
    ImGui::Render();
    assert(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).w==base.w);
    assert(ImGui::GetStyleColorVec4(ImGuiCol_ChildBg).w==child.w);
    WindowTransparencyPercent=0;
    ImGui::DestroyContext();
    return 0;
}
