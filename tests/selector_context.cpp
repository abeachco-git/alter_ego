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
    ImGui::DestroyContext();
    return 0;
}
