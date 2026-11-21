#pragma once
#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>
#include "imgui.h"

// Compact, consistent refresh/status presentation for Alter Ego tabs.
namespace AlterEgo::RefreshUI {

enum class State { Ready, Working, Success, Error };

inline std::string Age(std::time_t stamp) {
    if (stamp <= 0) return "not available";
    const auto seconds = std::max<std::time_t>(0, std::time(nullptr) - stamp);
    if (seconds < 60) return "just now";
    if (seconds < 3600) return std::to_string(seconds / 60) + "m ago";
    if (seconds < 86400) return std::to_string(seconds / 3600) + "h ago";
    return std::to_string(seconds / 86400) + "d ago";
}

inline void Timestamp(std::time_t stamp, const char* source = "data") {
    const std::string label = stamp > 0 ? "Updated " + Age(stamp) : "Not updated";
    const float right = ImGui::GetWindowContentRegionMax().x;
    const float x = right - ImGui::CalcTextSize(label.c_str()).x;
    ImGui::SameLine();
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
    ImGui::TextDisabled("%s", label.c_str());
    if (ImGui::IsItemHovered()) {
        if (stamp > 0) {
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &stamp);
#else
            localtime_r(&stamp, &local);
#endif
            char when[64] = {};
            std::strftime(when, sizeof(when), "%b %d, %Y at %I:%M:%S %p", &local);
            ImGui::SetTooltip("Last successful %s retrieval:\n%s (local time)", source, when);
        } else {
            ImGui::SetTooltip("No confirmed successful %s retrieval time", source);
        }
    }
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
    } else if (state == State::Error) {
        ImGui::TextColored(bad, "!  %s", message.c_str());
    } else if (state == State::Success) {
        ImGui::TextColored(good, "%s", message.c_str());
    } else {
        ImGui::TextColored(dim, "%s", message.c_str());
    }
}
} // namespace AlterEgo::RefreshUI
