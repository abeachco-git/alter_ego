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

Nexus options include Storage & Cache with an explicit Inspect Storage action, explanatory hover tooltip and cached diagnostic snapshot. Use shared section and action controls. No filesystem/SQL scan on each ImGui frame. The Rebuild button sits beside the operation dropdown (wraps at narrow widths) and offers confirmed startup-only rebuild of the entire cache, downloaded artwork, characters, account progress, achievement catalog, or shared public catalogs/prices. Show operation results and quarantine location; never invalidate live snapshots.

### Multi-select skin filters and split panels
- `UI::MultiFilterChoice` uses an unrestricted empty set and checkboxes; shared `SkinFilters` implements multi-select OR within facets and AND across applicable facets. Browser and Shopping keep independent selections, use removable chips and the same eligibility/count helpers. Browser progress describes selected scope before search/ownership filters; match/shown counts are separate. Catalog and ownership generations invalidate cached projections. Facet menu counts represent whole catalog facets, not conditional intersections. View as Grid is a Skinventory tab menu action, not a filter. Navigation reveals a requested skin by clearing conflicting restrictions. Legacy mandatory category defaults are not migrated into active filters.
- `UI::SplitPanel` captures available dimensions before the left child; use `split.left` for the child and `split.Divider(id, preferred)` between children (owns both SameLine calls). Persist preferred width only on drag release or double-click reset; temporary window clamps do not overwrite preference. Separate settings for Characters, Loadouts, Fashion, Components and Skinventory; Achievements retains its existing state file. Keep Nexus ImGui 1.80 ABI.

- All filter popup fields use left-aligned labels and uniform control width through `UI::FilterFieldLabel`, `FilterChoice`, `MultiFilterChoice`, and `FilterInt`; reset is always visible and inert/dim when no active filter chips exist. Loadouts game-mode selection is multi-select OR, with independent removable chips and session migration from the old single integer. Preserve search text on reset.

### Save to Library and Equipment Sets
Character identity appears first, followed by Save as, the component type when relevant, Name (or Replace for an existing component), modes for Complete Loadout, and only the required source tab selectors. Complete Loadout always includes equipment. Equipment-only captures require no build tab. Manual names survive source and target changes. New component can capture Equipment Set, which opens the separate Equipment Sets Library collection; each set can include any nonempty subset of equipment categories, selected at capture. The Loadout component preview groups Base Build and Skills under Build, followed by Equipment Set and individual equipment categories under Equipment, then Fashion. This is visual only. Edit Components opens a reference draft; linked categories are read-only and other references, set choice, and Fashion can be edited until Save/Cancel. Detach Set opens that draft rather than committing. Open Set, Copy Set, and Save as Equipment Set remain explicit actions. Equipment Sets has a read-only membership preview, Edit with a complete name/membership draft, referencing Loadout names, duplicate and unused-only deletion. The aggregate gear preview remains read-only when linked; use independent component editors or explicit shared set membership. Not included preserves the effective component on every linked Loadout. Adding an omitted category to a used set requires confirmation. Complete Loadout capture offers independent components, exact matching set or a new named set. Shared component edits and duplicate consolidation account for set references. No refresh or SQL on naming or browsing.

Fashion is a Save as target in the shared Save to Library dialog: Character, Save as, Name, Equipment tab, Save/Cancel. No build tab, component type or game modes. The general character Save button defaults to the active equipment tab; right-clicking an equipment tab preselects that tab. Live Fashion uses the existing wardrobe capture and native Fashion library, retains source tab/character and partial-capture metadata, and offers Select Existing or Save Separate Copy on an exact wardrobe match. Notes and weight remain editable in the Fashion collection. Do not reintroduce a separate Save Active Fashion button or context action.

### Navigation history

The main window has shared Back/Forward controls, mouse side buttons and Alt+Left/Right. History is session-only, bounded to 64 entries, and uses stable record IDs rather than list positions. `MarkNavigation()` identifies deliberate selection/cross-reference actions; filters and refreshes update context without pushing an entry. Keep new page destinations in `NavigationUI.h`; use the common history and never add a page-specific stack. Input callbacks queue directions only, respect focus/hover and modal/editor guards, and deregister on unload. Restore existing context, separate Browser/Shopping filters and scroll where available; missing IDs fall back to normal page selection. No history persistence or cache refresh policy changes.

### Editor transaction convention

Read-only previews never mutate saved records. `UI::SaveCancel` is the common footer for draft editors; validation disables Save, Cancel discards local state, stale revision prevents overwriting newer data, and failures retain the draft. Reference drafts (`RenderReferenceEditor`) commit all assignments together in one native transaction. Set membership additions require explicit confirmation of replacement on linked Loadouts; removal transfers the original reference. Cross-library Assign/Apply actions open the same Loadout reference draft. Existing typed component and aggregate Loadout payload editors remain separate so selecting a different component cannot accidentally overwrite its stored payload. No new schema or migration. Keep search, sort, and view controls immediate (not persisted record edits).

Fashion's modeless editor retains an existing draft instead of replacing it when another record is opened. Existing-look edits use stable ID and exact persisted baseline checks in the repository. The Save/Cancel footer accepts a distinct Save Separate Copy label when an exact Fashion capture match is offered. Aggregate Loadout list selection and drag/reorder are disabled while its draft is active.

The nested gear customization window uses a private equipment draft. Save applies equipment fields to the parent Loadout draft only; Cancel/close discards the child, including offhand removal, bulk gear changes and infusion totals. The parent Save stays unavailable until the child is closed. Gear edits must not overwrite unrelated Build or metadata fields.

## Character location and loading

The character-row hover tooltip displays the live roster location only; use a map ID fallback if its name is unavailable. Do not add location to the context menu or change API-derived character labels. Right-click Load resolves the actual row identity and invokes the canonical GameMemory::Load after the popup unwinds. Disable for active/foreign characters, invalid roster, unsupported resolver or a pending request and explain the reason on hover. The click performs fresh gameplay and context validation; do not disable the menu solely on a periodically sampled gameplay flag. Use the existing Nexus alert for validation failures and outcomes.

### Native inventory
Items, Wallet and Guild bank are separate Inventory subviews. Item filters and local sort are independent from refresh; automatic refresh is opt-in and visible-scope only. Multi-account controls are hidden with one linked account. Shared guild stock is separate from personal totals. Compare's exact saved item locations are informational, not semantic differences. Equipment/Shopping/Compare Find in Inventory actions use a shared navigation target and never fetch on click. Shopping Stock rows have zero remaining purchase cost and link to the corresponding cached item; stale/failed or uncertain acquisitions do not reduce totals.
