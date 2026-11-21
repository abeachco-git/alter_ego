#pragma once

// Unified progress/completion visual language for Alter Ego.
// Shared by the Characters-independent Achievements, Vault, and Clears views.
//   Complete: muted text + green border + green status dot.
//   Available/incomplete: bright text, no status border or dot.
//   Gold: pin/selection and claimable rewards, NOT completion.
//   Unknown: never imply completion when account data is missing.
// Keep all completion colors and row primitives here so these tabs stay aligned.
#include "imgui.h"

namespace AlterEgo::CompletionTheme {
inline constexpr ImU32 kRowBackground = IM_COL32(29, 31, 37, 245);
inline constexpr ImU32 kCompleteBorder = IM_COL32(105, 194, 126, 240);
inline constexpr ImU32 kCompleteDot = IM_COL32(105, 204, 126, 255);
inline constexpr ImU32 kCompleteText = IM_COL32(146, 148, 142, 240);
inline constexpr ImU32 kAvailableText = IM_COL32(236, 232, 221, 255);
inline constexpr ImU32 kSelectedGold = IM_COL32(232, 196, 122, 230);
inline constexpr ImU32 kInProgressGold = IM_COL32(244, 196, 122, 230);
inline constexpr ImU32 kUnknownText = IM_COL32(160, 155, 140, 255);

inline ImU32 Text(bool complete) {
    return complete ? kCompleteText : kAvailableText;
}
inline void DrawRow(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                    bool complete, float rounding = 3.0f) {
    draw->AddRectFilled(min, max, kRowBackground, rounding);
    if (complete)
        draw->AddRect(min, max, kCompleteBorder, rounding);
}
inline void DrawCompleteDot(ImDrawList* draw, const ImVec2& center,
                            bool complete, float radius = 3.8f) {
    if (complete)
        draw->AddCircleFilled(center, radius, kCompleteDot);
}
inline ImVec4 TextColor(bool complete) {
    return ImGui::ColorConvertU32ToFloat4(Text(complete));
}
} // namespace AlterEgo::CompletionTheme
