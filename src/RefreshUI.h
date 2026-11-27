#pragma once
#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>
#include "imgui.h"
#include "UIControls.h"
#include "RefreshStatusCore.h"

// Compact, consistent refresh/status presentation for Alter Ego tabs.
namespace AlterEgo::RefreshUI {

enum class State { Ready, Working, Success, Error, Waiting };

inline State StatusState(RefreshStatus::Kind kind){
    using K=RefreshStatus::Kind;
    switch(kind){
    case K::Working:case K::Queued:return State::Working;
    case K::Retry:case K::Disabled:return State::Waiting;
    case K::Failed:case K::NoAPI:case K::Unverified:case K::Denied:return State::Error;
    case K::Complete:return State::Success;
    default:return State::Ready;
    }
}
inline std::string Age(std::time_t stamp,int64_t now=0) {
    if(now)return RefreshStatus::Age(stamp,now);
    const auto current=std::time(nullptr);
    return RefreshStatus::Age(stamp>current?current:stamp,current);
}
inline void DiagnosticsTooltip(const char* title,const std::string& body){
    UI::DetailTooltip(title,body.c_str());
}
inline void RetrievalTooltip(std::time_t stamp,const char* source,const std::string& detail={}){
    std::string body=source;
    if(stamp>0){
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local,&stamp);
#else
        localtime_r(&stamp,&local);
#endif
        char when[64]={};
        std::strftime(when,sizeof(when),"%b %d, %Y at %I:%M:%S %p",&local);
        body+=": ";body+=when;body+=" (local time)";
    }
    if(!detail.empty())body+="\n\n"+detail;
    DiagnosticsTooltip(stamp>0?"Last confirmed retrieval":"No confirmed complete retrieval",body);
}

inline void Timestamp(std::time_t stamp, const char* source = "data") {
    const std::string label = stamp > 0 ? "Updated " + Age(stamp) : "Not updated";
    const float right = ImGui::GetWindowContentRegionMax().x;
    const float x = right - ImGui::CalcTextSize(label.c_str()).x;
    ImGui::SameLine();
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
    ImGui::AlignTextToFramePadding(); // Align with adjacent full-height button.
    ImGui::TextDisabled("%s", label.c_str());
    if(ImGui::IsItemHovered())RetrievalTooltip(stamp,source);
}

inline void Status(State state, const std::string& message) {
    const ImVec4 dim(0.58f, 0.58f, 0.58f, 1.0f);
    const ImVec4 warn(0.98f, 0.80f, 0.34f, 1.0f);
    const ImVec4 good(0.48f, 0.82f, 0.52f, 1.0f);
    const ImVec4 bad(0.98f, 0.42f, 0.40f, 1.0f);
    if (state == State::Working) {
        constexpr char chars[] = "|/-\\";
        const int i = (int)(ImGui::GetTime() * 8.0) % 4;
        ImGui::TextColored(warn, "%c  %s", chars[i], message.c_str());
    } else if (state == State::Waiting) {
        ImGui::TextColored(warn, "%s", message.c_str());
    } else if (state == State::Error) {
        ImGui::TextColored(bad, "!  %s", message.c_str());
    } else if (state == State::Success) {
        ImGui::TextColored(good, "%s", message.c_str());
    } else {
        ImGui::TextColored(dim, "%s", message.c_str());
    }
}

inline float RowHeight() {
    return UI::ActionButtonHeight() + ImGui::GetStyle().ItemSpacing.y + 3.0f;
}

// One shared bottom status row for Characters, Compare, Skinventory, Vault,
// Clears and Achievements. Status details appear on hover; the last successful
// retrieval time is always right aligned and never replaced by a failure time.
// The optional ellipsis reuses the Characters multi-character picker.
inline bool Row(const char* id, std::time_t stamp, const char* source,
                State state, const std::string& detail, bool disabled = false,
                bool* moreClicked = nullptr, const char* statusOverride = nullptr, bool timestampDetails = false,
                int64_t sampledNow=0,const char* disabledReason=nullptr,ImVec2* moreAnchor=nullptr) {
    ImGui::PushID(id);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float buttonH = UI::ActionButtonHeight();
    if (disabled) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
    const bool clicked = UI::Button("Refresh", ImVec2(0, buttonH));
    if (disabled) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
    if(disabled)UI::DisabledHint(disabledReason);
    if (moreClicked) {
        ImGui::SameLine(0.0f, 5.0f);
        if(disabled){
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*0.5f);
        }
        *moreClicked = UI::Button("...", ImVec2(29.0f, buttonH))&&!disabled;
        if(moreAnchor)*moreAnchor=ImGui::GetItemRectMin();
        if(disabled){
            ImGui::PopStyleVar();ImGui::PopItemFlag();
            UI::DisabledHint(disabledReason);
        }else if(ImGui::IsItemHovered())
            UI::Hint("Choose one or more characters to refresh");
    }

    ImGui::SameLine(0.0f, 9.0f);
    const float statusX = ImGui::GetCursorScreenPos().x;
    const std::string stampLabel = stamp > 0 ? "Updated " + Age(stamp,sampledNow) : "Not updated";
    const float stampWidth = ImGui::CalcTextSize(stampLabel.c_str()).x;
    const float rightX = start.x + width;
    const float stampX = std::max(statusX, rightX - stampWidth);
    const float maxStatus = std::max(0.0f, stampX - statusX - 8.0f);
    const char* shortStatus = state == State::Working ? "Refreshing" :
                              state == State::Success ? "Complete" :
                              state == State::Error ? "Failed" :
                              state == State::Waiting ? "Waiting for API" : "Ready";
    if(statusOverride&&*statusOverride)shortStatus=statusOverride;
    std::string statusText(shortStatus);
    if(sampledNow&&state==State::Working){
        constexpr char spinner[]="|/-\\\\";
        statusText=std::string(1,spinner[static_cast<int>(ImGui::GetTime()*8.0)%4])+" "+statusText;
    }
    while (!statusText.empty() &&
           ImGui::CalcTextSize(statusText.c_str()).x > maxStatus)
        statusText.pop_back();
    const ImVec4 statusColor = (state == State::Working || state == State::Waiting) ? ImVec4(0.98f, 0.80f, 0.34f, 1.0f) :
        state == State::Success ? ImVec4(0.48f, 0.82f, 0.52f, 1.0f) :
        state == State::Error ? ImVec4(0.98f, 0.42f, 0.40f, 1.0f) :
        ImVec4(0.58f, 0.58f, 0.58f, 1.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(statusColor, "%s", statusText.c_str());
    if (ImGui::IsItemHovered()){
        DiagnosticsTooltip(shortStatus,detail);
    }

    // Keep timestamp in the same row even when a long API status is reported.
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(stampX,
        start.y + (buttonH - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::TextDisabled("%s", stampLabel.c_str());
    if(ImGui::IsItemHovered())RetrievalTooltip(stamp,source,(timestampDetails||statusText.empty())?detail:std::string{});
    ImGui::PopID();
    return clicked;
}

} // namespace AlterEgo::RefreshUI
