# UI component reference

This is the discoverable source of truth for developers and coding agents. Read it before editing any UI. Preserve the current visuals and behavior: reuse is the goal, not blindly restyling every widget.

| Need | Canonical entry point | Source |
|---|---|---|
| Standard action | RenderActionButton(label, size) | AddonShared.h → dllmain.cpp → UIControls.h |
| Primary gold action | RenderGoldButton | AddonShared.h |
| Filter or toggle chip | RenderChipButton | AddonShared.h |
| Compact / destructive action | UI::Button(label, size, ButtonRole::Compact / Destructive), UI::SmallButton | UIControls.h |
| Destructive context menu | UI::DestructiveMenuItem | UIControls.h |
| Saved loadout commands | ExecuteSavedLoadoutAction; RenderBuildShareMenuItems | dllmain.cpp (header and right-click menu share domain handlers) |
| Gear catalog entry | UI::CatalogMatches, CatalogCard, CatalogOption | UIControls.h (full gear editor and standalone component dropdowns share filtering) |
| Native dropdown | UI::BeginCombo(id, preview, flags, width), UI::Combo | UIControls.h |
| Gold dropdown | RenderThemedCombo or UI::BeginCombo with ComboRole::Gold | UIControls.h |
| Dropdown option | UI::SelectableOption (standard selection/focus); custom drawing allowed | UIControls.h |
| Single-line text / search | UI::InputText / InputTextWithHint | UIControls.h |
| Multiline text | UI::InputTextMultiline | UIControls.h |
| Active character/template marker | UI::ActiveIndicator | UIControls.h |
| Tooltip title/body, destructive/warning/difference colors | UI::TooltipText and named palette values | UIControls.h |
| Section header (equipment/detail) | RenderSectionHeader | AddonShared.h |
| Completion/group heading, checkmark, progress | CompletionTheme::SectionHeading, CenteredSectionToggle, DrawCompleteMark, RenderPartialProgress | CompletionTheme.h |
| Empty state and spinner | RenderEmptyCard / RenderSpinner | AddonShared.h |
| Build and equipment template strip | RenderChipStrip | dllmain.cpp |
| Character/profession text | RenderProfessionCharacterName / DrawProfessionCharacterName, GetProfessionColor | dllmain.cpp |
| Skill icons | RenderSkillIcon / RenderEditableSkillIcon → RenderSkillIconCore | dllmain.cpp |
| Trait icons | RenderTraitIcon (read-only/editable) | dllmain.cpp |
| Infusion totals | RenderInfusionTotals | InfusionTotalsView.h |
| Refresh footer | RefreshUI::Row; UI::Button / UI::ActionButtonHeight | RefreshUI.h (standard button styling; do not change lifecycle) |
| Gear summaries / ordering | Composable::Presentation::GearSlotSummary / SortedGearSlots | ComposableGearPresentation.h |
| Equipment catalogs | RUNE_LIST / SIGIL_LIST / RELIC_LIST, GetSortedStatCombos | dllmain.cpp; reused in ComposableNativeEditor.h |

All paths are relative to src/. UI:: is AlterEgo::UI. AddonShared.h includes UIControls.h. Use a semantic role, not arbitrary colors. Existing dense widgets may intentionally retain their compact height.

## Example
```cpp
if (RenderActionButton("Edit##item")) StartEdit();
if (AlterEgo::UI::BeginCombo("##choice", current.c_str())) {
    for (const auto& option : options) {
        ImGui::PushID(option.id.c_str());
        if (AlterEgo::UI::SelectableOption(option.name.c_str(), option.id == selected))
            selected = option.id;
        ImGui::PopID();
    }
    ImGui::EndCombo();
}
AlterEgo::UI::InputTextWithHint("##search", "Search...", searchString);
if (AlterEgo::UI::Button("Delete##item", ImVec2(0,0),
                        AlterEgo::UI::ButtonRole::Destructive)) OpenConfirmation();
```

Use stable IDs (not a display name or mutable row index when a persistent ID exists). Keep actions separate from display, and invoke the same domain action from header and context menu. Defer destructive changes until safe to mutate the collection.

## Intentional exceptions and remaining work
- Standardize refresh button appearance and layout using UI::Button / RenderActionButton and UI::ActionButtonHeight. Preserve the existing disabled state, click handlers, request execution, scheduling, timeouts, retries, caching and page-specific state. Do not consolidate request logic without explicit approval.
- The existing custom Cut/Copy/Paste/Select All menu lives in lib/imgui/imgui_widgets.cpp InputTextEx and inherits the active GW2 theme (popup background, hover/active colors, padding and typography). Preserve the theme and existing native text selection, UTF-8 filtering and callbacks. Test clipboard in game; do not add a wrapper popup or change ImGui version.
- Custom draw-list cards, roster rows, raid encounter chips, drag targets, and selected/active markers can have different geometry. Reuse palette and meaningful primitives; don't replace specialized rendering solely to remove raw calls.
- Equipment and independent component editing still have different storage and commit paths; shared catalogs and presentation helpers are the starting point. Further consolidation must preserve slot count, unknown vs absent metadata, exact IDs, copy-on-write and shared-component semantics. Do not claim all editor behavior is unified.
- Saved loadout header and context menu share ExecuteSavedLoadoutAction and RenderBuildShareMenuItems. Fashion/component actions and specialized responsive layouts still have separate wiring; extract shared commands when changing them.
- Validate at different fonts and window widths, ensure popup focus and right-click work, and verify DLL compilation/artifact for the exact commit.
