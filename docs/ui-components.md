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

## Skinventory
- `RenderSkinContextMenu` in dllmain.cpp is the single skin menu; scope each popup to the stable skin ID. Skin links use `ChatLink::EncodeSkin`; actual TP item names and links come from the winning price candidate, not the skin name. Use `CopyToClipboard`.
- `SkinSearchCore.h` provides shared token/substring/typo search and price selection. Shopping filters operate on cached results and preserve the table sort; do not trigger price queries on typing or filtering.
- Commerce v2 derived cache retains all known tradeable equipment/skin-unlock candidates and names, selects the cheapest available sell listing, and does not reuse legacy rarity-selected prices. Vendor coverage remains limited to known cultural armor prices.

- Keep cached shopping candidate metadata and sort keys separate from local filters. Never scan or reprice all candidates per keystroke. Price snapshots are returned by value under the Commerce lock. A failed price batch must not publish a partial winner as the cheapest.

- Preview in Game is optional, using existing `PieUI_PreviewChatLink` when Pie UI is present. Its documented native action supports skin links (see https://github.com/PieOrCake/pie_ui/blob/master/INTEGRATION.md).

## Search and filter conventions
- `UI::SearchField(id, hint, query)` supports fixed char arrays and std::string; themed input, stable ID, clear-query button and width-aware layout. Use it in plain lists and searchable pickers, not ordinary edit fields.
- `UI::SearchToolbar(id, hint, query, collectFilters, drawFilters, resetFilters, optionalActionLabel, optionalActionClicked)` is the canonical compact toolbar for lists with filters. `collectFilters` returns `UI::FilterChips`: each active chip has a stable ID, display label and a remove callback that clears only that filter. A required default scope may have an empty callback, is shown as context, and is not counted. Count and chips derive from the same state. Chips wrap at narrow widths and each has a visible x; Clear all resets filters only. Search x clears query only. Popup body and reset are caller-owned; optional action returns a click, never executes a request itself.
- `UI::FilterChoice` provides consistent enum and string choices. Use specific descriptive filter labels and sensible default values. Only expose relevant controls, show active values, and retain existing persisted options, sort, and selection behavior. A required category or view preference can be displayed as context without pretending to be a search match.
- Characters and global Achievements use SearchField (sort and category-scoped completion visibility are separate); Loadouts, Fashion, Components, Skinventory Browser and Shopping use SearchToolbar. Gear stat/upgrade and component picker searches use SearchField. No raw per-page search input rendering; preserve ImGui clipboard context menu. Domain-specific search matching remains unchanged (Skinventory keeps substring/token/typo matching); no new network requests or refresh lifecycle changes.

- Optional toolbar actions use standard action-button height; the adjacent search and filter controls align to that height without changing popup styling. Never make a refresh button an exception to shared button styling.

- Individual filter removal must preserve other compatible filters, search text, sorting and view settings. Skinventory Shopping marks only its local filter results dirty; no price refresh. The Browser ownership chip resets to All without changing list/grid. A removable component kind resets to its required default Base Build and clears incompatible selection. Keep both Skinventory views and all other filter toolbars in sync through `RenderFilterChips`.

## Canonical-control guardrails

- Preserve dropdown active/auto semantics and keyboard navigation; always pair a successful BeginCombo with EndCombo.
- Use existing profession/rarity helpers and named palette/indicator primitives instead of new per-tab colors or size constants. Use RenderGoldButton, RenderChipButton and the shared button roles according to their established meanings.
- Share typed gear pickers and presentation when their semantics agree; never conflate missing, unknown, empty or imported values. Keep commit paths appropriate to the underlying data.
- All skin surfaces use RenderSkinContextMenu. The selected Commerce candidate is based on available sell listings, never rarity; copy the actual priced item's name for Trading Post search.
- Every active contextual filter must have a stable removable UI::FilterChip with its own reset callback, except required default context. Clear all preserves search text; search x preserves filters. Both Skinventory views use the shared toolbar and chips. Preserve sorting and compatible selections; changing filters does not refresh prices or make API requests.
- When a shared control changes, update this guide and migrate affected callers where the scope allows. Preserve established appearance unless a redesign is requested.

### Library persistence terminology

Use Loadout Library, Components, Fashion, Import Library and Export Library. JSON is a portable interchange format, not the runtime storage model. Keep the existing combined equipment editor and independent component editors; save a draft through native commands and never mutate persistent state from presentation enrichment. Selective restore includes required Fashion dependencies and creates recovery snapshots before replacing data.

The standalone component editor checks exact candidates on draft or revision changes, not on every frame. Create may open an existing match; private Copy & Edit may explicitly reassign to one. Save in shared or duplicate mode preserves the independent identity. Unverified Skills profession context cannot authorize automatic reuse.

Nexus options include Storage & Cache with an explicit Inspect Storage action and cached diagnostic snapshot. Use shared section and action controls. No filesystem/SQL scan on each ImGui frame and no live destructive rebuild control until safe invalidation exists.
