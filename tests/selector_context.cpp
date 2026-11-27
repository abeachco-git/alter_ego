#include "../src/UIControls.h"
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
    ImGui::DestroyContext();
    return 0;
}
