#pragma once

// Unified progress/completion visual language for Alter Ego.
// Shared by the Characters-independent Achievements, Vault, and Clears views.
//   Complete: distinctly darker cells and muted text + subtle checkmark.
//   Available/incomplete: bright text, no status border or dot.
//   Gold: partial progress, selection/pins, and unclaimed rewards.
//   Green: completed sections and full (only when shown) progress bars.
//   Unknown: never imply completion when account data is missing.
// Keep all completion colors and row primitives here so these tabs stay aligned.
#include "imgui.h"

namespace AlterEgo::CompletionTheme {
// Layered surfaces: dark group = structure, individual cells = actionable work.
inline constexpr ImU32 kGroupBackground = IM_COL32(26, 28, 32, 246);
inline constexpr ImU32 kAvailableBackground = IM_COL32(58, 61, 68, 245);
// Secondary encounters are still available, but intentionally less prominent.
inline constexpr ImU32 kSecondaryBackground = IM_COL32(43, 46, 51, 245);
inline constexpr ImU32 kCompletedBackground = IM_COL32(32, 35, 39, 245);
inline constexpr ImU32 kAvailableIndicator = IM_COL32(69, 72, 79, 245);
inline constexpr ImU32 kCompletedIndicator = IM_COL32(30, 33, 37, 245);
inline constexpr ImU32 kRowBackground = kAvailableBackground;
inline constexpr ImU32 kCompleteBorder = IM_COL32(105, 194, 126, 240);
inline constexpr ImU32 kCompleteDot = IM_COL32(105, 204, 126, 255);
inline constexpr ImU32 kCompleteMark = IM_COL32(137, 148, 141, 205);
inline constexpr ImU32 kProgressTrack = IM_COL32(47, 49, 55, 200);
inline constexpr ImU32 kBountyToday = IM_COL32(255, 166, 92, 255);
inline constexpr ImU32 kBountyFuture = IM_COL32(101, 207, 203, 255);
inline constexpr ImU32 kMetaText = IM_COL32(162, 163, 160, 230);
inline constexpr float kHeadingScale = 1.18f;
inline constexpr ImU32 kCompleteText = IM_COL32(112, 120, 114, 235);
inline constexpr ImU32 kSecondaryText = IM_COL32(174, 177, 172, 245);
inline constexpr ImU32 kAvailableText = IM_COL32(236, 232, 221, 255);
inline constexpr ImU32 kSelectedGold = IM_COL32(232, 196, 122, 230);
inline constexpr ImU32 kInProgressGold = IM_COL32(244, 196, 122, 230);
inline constexpr ImU32 kUnknownText = IM_COL32(160, 155, 140, 255);

inline ImU32 Text(bool complete, bool secondary = false) {
    // Completion always wins over priority: all completed cells use the exact
    // same shared palette, whether they are bosses or secondary encounters.
    return complete ? kCompleteText : secondary ? kSecondaryText : kAvailableText;
}
inline void DrawRow(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                    bool complete, float rounding = 3.0f, bool secondary = false) {
    // Group backgrounds never change with completion. A completed cell must
    // remain perceptibly distinct from its group but noticeably darker than
    // unfinished bosses; incomplete secondary cells sit between the two.
    draw->AddRectFilled(min, max,
        complete ? kCompletedBackground :
        secondary ? kSecondaryBackground : kAvailableBackground, rounding);
}
// Group backgrounds visually separate wings and daily fractals from their
// item cells. Never color an outer group based on completion state.
inline void DrawGroup(ImDrawList* draw, const ImVec2& min,
                      const ImVec2& max, float rounding = 3.0f) {
    draw->AddRectFilled(min, max, kGroupBackground, rounding);
}
inline void DrawCompleteMark(ImDrawList* draw, const ImVec2& center,
                             bool complete, float radius = 3.8f) {
    if (!complete) return;
    // A neutral checkmark, rather than a saturated green dot.
    draw->AddLine(ImVec2(center.x-radius, center.y),
                  ImVec2(center.x-radius*0.2f, center.y+radius*0.8f),
                  kCompleteMark, 1.5f);
    draw->AddLine(ImVec2(center.x-radius*0.2f, center.y+radius*0.8f),
                  ImVec2(center.x+radius, center.y-radius*0.8f),
                  kCompleteMark, 1.5f);
}
inline ImU32 ProgressColor(int done, int total) {
    if (total <= 0 || done <= 0) return kProgressTrack;
    return done >= total ? kCompleteDot : kInProgressGold;
}
// Section-level bars: show only when partly complete. Fully complete is
// already represented by the right-hand count; zero is not actionable.
inline void RenderPartialProgress(int done, int total, float height = 4.0f) {
    if (total <= 0 || done <= 0 || done >= total) return;
    const float fraction = static_cast<float>(done) / static_cast<float>(total);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kInProgressGold);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kProgressTrack);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, height), "");
    ImGui::PopStyleColor(2);
}
// Shared vertical metrics prevent the small expand/collapse control from
// appearing top-aligned next to a larger heading in Vault and Clears.
inline float SectionHeadingRowHeight() {
    const float big = ImGui::GetFontSize() * kHeadingScale;
    const float frame = ImGui::GetFrameHeight();
    return (big > frame ? big : frame) + 4.0f;
}
inline bool CenteredSectionToggle(bool expanded, const char* title) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float buttonHeight = ImGui::GetFontSize(); // ImGui::SmallButton
    const float offset = (SectionHeadingRowHeight() - buttonHeight) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + offset));
    const bool clicked = ImGui::SmallButton(expanded ? "-" : "+");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s %s", expanded ? "Collapse" : "Expand", title);
    ImGui::SameLine(0.0f, 5.0f);
    const ImVec2 labelPos = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(labelPos.x, start.y));
    return clicked;
}
// Unified section title: large neutral title, small reset text, counts
// right-aligned. Keeps all parts on one line when space allows, otherwise
// places the right-hand summary on a second line rather than clipping.
inline void SectionHeading(const char* title, const char* reset,
                           const char* summary = nullptr,
                           ImU32 summaryColor = kMetaText,
                           ImTextureID leadingIcon = nullptr,
                           float leadingIconSize = 0.0f) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float available = ImGui::GetContentRegionAvail().x;
    const ImFont* font = ImGui::GetFont();
    const float small = ImGui::GetFontSize();
    const float big = small * kHeadingScale;
    const ImVec2 titleSize = font->CalcTextSizeA(big, 1.0e8f, 0.0f, title);
    const float resetWidth = (reset && *reset) ? ImGui::CalcTextSize(reset).x : 0.0f;
    const float summaryWidth = (summary && *summary) ? ImGui::CalcTextSize(summary).x : 0.0f;
    const float rowHeight = SectionHeadingRowHeight();
    const float iconWidth = leadingIconSize > 0.0f ? leadingIconSize + 5.0f : 0.0f;
    const bool wrapSummary = summaryWidth > 0 &&
        iconWidth + titleSize.x + resetWidth + summaryWidth + 35.0f > available;
    const float totalHeight = rowHeight + (wrapSummary ? small+4.0f : 0.0f);
    ImGui::Dummy(ImVec2((available > 1.0f ? available : 1.0f), totalHeight));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(start,
        ImVec2(start.x+available, start.y+totalHeight), true);
    if (leadingIconSize > 0.0f) {
        const ImVec2 iconMin(start.x, start.y + (rowHeight-leadingIconSize)*0.5f);
        if (leadingIcon) {
            dl->AddImage(leadingIcon, iconMin,
                ImVec2(iconMin.x + leadingIconSize, iconMin.y + leadingIconSize));
        } else if (title && *title) {
            dl->AddText(ImVec2(iconMin.x+3.0f,iconMin.y),
                kAvailableText, title, title+1);
        }
    }
    dl->AddText(font, big, ImVec2(start.x+iconWidth, start.y+(rowHeight-big)*0.5f),
        kAvailableText, title);
    if (reset && *reset) {
        const float resetX = start.x+iconWidth+titleSize.x+9.0f;
        const float resetLimit = wrapSummary ? start.x+available : start.x+available-summaryWidth-10.0f;
        if (resetX+resetWidth <= resetLimit)
            dl->AddText(ImVec2(resetX, start.y+(rowHeight-small)*0.5f),
                kMetaText, reset);
    }
    if (summary && *summary) {
        const float rightX = start.x+(available > summaryWidth+2.0f ? available-summaryWidth-2.0f : 0.0f);
        const float y = start.y + (wrapSummary ? rowHeight : (rowHeight-small)*0.5f);
        dl->AddText(ImVec2(rightX,y), summaryColor, summary);
    }
    dl->PopClipRect();
}
inline ImVec4 TextColor(bool complete) {
    return ImGui::ColorConvertU32ToFloat4(Text(complete));
}
} // namespace AlterEgo::CompletionTheme
