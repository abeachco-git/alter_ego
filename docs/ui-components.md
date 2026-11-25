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
- Saved loadout header and context menu share ExecuteSavedLoadoutAction and RenderBuildShareMenuItems. The developer-only `BuildRelay::Enabled` flag in `src/BuildRelayPolicy.h` controls legacy one-time-code sharing, import guidance and relay dialogs; never add a separate GUI flag. Fashion/component actions and specialized responsive layouts still have separate wiring; extract shared commands when changing them.
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

Inventory subview chips distinguish the selected view. Wallet pins are stored in settings (not cache), pin currency IDs across accounts, and support pinned-only and hide-zero filters; pinned-first game order is fixed. Item filters include character, freshness, aggregate minimum quantity and potential transfer (known public flags, fresh unbound personal loose locations; not a verified trading-post listing). Compare adds exact-ID upgrade and infusion acquisition hints without changing semantic match results.

- Native Inventory Items and Wallet must use `UI::SearchToolbar` with removable chips and canonical `UI::FilterChoice` / `UI::FilterInt` fields; never implement per-view filter combos or a separate row of filter checkboxes. Wallet favorites-only and nonzero balances are filters, not persistent favorite mutations. Guild bank uses `UI::SearchField` while it has no additional facets. Keep sorting and account scope separate from filter reset. The current storage facet remains single-select because it also determines the explicit manual refresh scope; changing search, filters or sorting alone performs no API request.

### Native Inventory UI consistency
Inventory uses the global account selector (empty = All Accounts for multiple linked accounts), the existing tab-dropdown interaction for Items/Wallet/Guild Bank, and a reserved scrollable child plus RefreshUI::Row footer. The footer reports the oldest confirmed successful retrieval in the selected refresh scope, not batch completion; missing scopes leave it unverified and failures retain previous timestamps. Items use multi-select OR within binding/rarity/type/character facets and AND across facets, with individual removable chips; storage remains single-select to preserve manual scope. NavigationUI captures Inventory subview, target ID, search, facets and sort. Reveal uses existing global account scope if the item is there, otherwise All Accounts if a different linked account has cached stock. Wallet and guild coin display reuse RenderCoins; Items/Guild use shared rarity palette and item context menu. Stock Shopping rows retain normal coin-height geometry and locate stock through RenderSkinContextMenu. Native account key fields use canonical InputText with password mask; unlink uses destructive button role and pending actions are disabled. Preserve source/refresh semantics; visual tests outstanding.

The global account dropdown occupies the navigation header (right aligned, wraps when needed), never overlays the primary tab strip. Account validation uses the shared typed refresh status presentation. Location names are shared across item browsing, Compare and equipment menus. The existing equipped-item chat-link action retains instance-specific skin/socket metadata.

Inventory refresh tooltips show the active storage request and processed scope count, with Bags and Equipment grouped by account instead of one line per character. Completed scope status is published immediately through a transient progress overlay; full inventory projections are coalesced on the worker and published after roster discovery and completion. Character and Compare footers use the existing fetch detail for current character ordinal and retrieval stage. No additional API requests or render-time SQL.

Inventory has no inline Storage status tree; the shared footer tooltip owns retrieval diagnostics. Account scope remains in the global header (hidden for a single linked account). Character is the first filter and first chip group. Its multi-select picker shares the profession-colored icon row and cached character ordering with Compare, stays open for multiple choices, and preserves roster-only names with unknown profession. Empty selection means All. The shared picker uses embedded class artwork, never requests profession metadata merely to draw a row. Compare retains its single-target/Active semantics.

Inventory Items is a search-first view: do not scan or render matches until at least three non-whitespace query characters are entered, or the Character facet and at least one other item facet are active. Multiple character selections count as one facet; global account context does not count. With qualifying filters, empty or shorter queries show all filtered matches (short text is not applied until the minimum is reached). Explicit Find in Inventory navigation can reveal a short numeric ID. Filters remain editable before searching; clearing the query preserves filters and hides results unless the filter-only browse rule is satisfied. Use the existing shared SkinSearchCore token matcher (case-insensitive AND substring tokens, conservative typo tolerance for longer words, literal numeric tokens); tokenize once per query. Other browse-first lists retain their current empty-query behavior. Use HasMinimumQuery for future search-first views rather than embedding a minimum in the input widget itself.

Characters Tools is a read-only detail tab (navigation subview 2), with Gathering Tools, Fishing and Jade Bot sections. Reuse the equipment icon/tooltip/context menu and selected-character metadata batch. Show actual item names and optional skins, charges when reported, and glyph upgrade IDs/names when returned; an absent glyph is Unknown, not an empty socket. No equipment template selector or Tools library save category. Missing utility slots are Not reported. Back/Forward uses existing detail navigation.

Inventory Items and Skinventory list view share CatalogPresentation.h (fixed row pitch, clipper, cached priority icons, neutral names, rarity stripe, alternating backgrounds and detail banner). Skinventory retains ownership state and its domain actions. Inventory uses its own persisted splitter and selected item ID, matching quantity on the left and all cached locations in account scope on the right. Location context menus and item menus use shared Fast Load availability and deferred execution; the active character and mismatched/unverified accounts are disabled, and execution revalidates. Existing refresh cadence and search gate unchanged. Public inventory metadata is retained in reference_items during existing refresh batches; no detail-click API lookup. Older partial metadata is upgraded during subsequent inventory refreshes.

Wallet rows share CatalogRowBackground with Items/Skinventory. PinButton is a shared outlined square, empty or gold asterisk, with stable IDs and pin/unpin tooltips for Wallet, Achievements and its tracker popout; legacy favorites migrate to pins. Wallet retrieval status is only in the shared footer and its per-account tooltip, not an inline Ready label. No rarity stripe for currencies (no rarity data).

Main tab display order: Characters, Library, Compare, Inventory, Skinventory, Vault, Clears, Achievements. Inventory stays available in All Accounts mode; existing internal page IDs are independent of visual order.

Main tabs use UI::TabGroupDividerBefore for purely decorative, muted one-pixel separators before Inventory and Vault. Draw in the existing central-tab gap after all tabs have been submitted; do not create spacer tabs, hit targets or navigation IDs. Respect the actual tab-bar clip rectangle and skip missing/stale layout entries. No extra width or label, and the All Accounts Vault button uses its own ID.

The Achievements dropdown offers Refresh Achievement Names as a separate public-catalog full revalidation action. Keep the regular Refresh footer scoped to account progress. The shared footer reports catalog work/failure; its timestamp remains the last successful account progress retrieval. Search remains a local operation and invalidates when catalog/category generations change.

Legacy achievement-specific WP buttons have been retired from rows and the popout. Preserve pin actions, name copying, normal achievement/Skinventory navigation and Wiki access. Vault objective assistance and vendor waypoint actions are independent features.

Nexus Wiki reference schedules uses the standard section header and action button; show event phase count, source (embedded baseline/runtime cache), last verified age and refresh status. The displayed state is an immutable snapshot; rendering does not perform SQL or network requests.

Nexus automatic refresh includes Objective tracking (minutes), default 15, clamped 5–60, shared by pinned achievements, Clears and daily/weekly Vault. General browsing and Vault Special use 30 minutes. Retain dataset opt-in/exclusions and existing canonical refresh footers. Explicit refresh bypasses ordinary cooldown, never permissions or in-flight safety.

Nexus refresh no longer exposes an unconditional login refresh option. Character/Compare footer uses activity freshness, not raw snapshot age; Inventory freshness and availability share the same scope predicate. Display previous cached data while awaiting API synchronization. Active interval 10 minutes with visible demand; offline grace 10 minutes. Manual refresh retains existing controls.

Storage & Cache includes a collapsed API Activity tree. It samples the in-memory transport counters once per second while expanded, independent of Inspect Storage. Show rolling 10-minute/hour and session request totals, outcome/429, active/waiting and peak starts per second, with endpoint groups under a nested tree. No filesystem, SQL or network access is triggered by this diagnostic.

The Ranger pet picker displays 32px pet artwork from the existing IconManager; known IDs use embedded PNGs. Preserve the existing selection/disabled semantics and use stable pet ID scopes.

API Activity shows separate ArenaNet API, Wiki and ArenaNet artwork rows and compact fixed initiation-reason counts. Queue waiting is shown only for hosts with shared admission, and metadata ID counts remain distinct from HTTP requests.

Character and Compare retain the canonical RefreshUI::Row. Hover status or timestamp for per-demanded-scope success age, current/stale state, scheduler eligibility countdown, last observed activity, automatic policy, ownership and session-local endpoint failures. Status distinguishes propagation, cooldown, disabled policy, unverified ownership and due work; do not imply a request is running unless a worker is active. The timestamp remains the oldest confirmed scope. Tooltip uses the scheduler's eligibility decision, never a separate timer or network query.

Inventory footer timestamp/status follows the visible storage and character facets, not text, rarity or other item filters. Roster age is not a bag/equipment success timestamp; missing roster remains a prerequisite diagnostic. Selected-character scopes show their individual retrieval status and activity eligibility; both status and timestamp hover expose details. Manual refresh respects the selected character and storage facets across selected accounts; filters alone do not dispatch requests.

Live Characters and Library viewer/editor use EquipmentLayout.h for paired armor/trinket tables, equal full-width weapon columns with set labels, independent Relic header, and content-aware wrapped text. Live aquatic equipment retains its section; saved projections retain their existing supported slots. Domain icon/tooltip/edit/save handlers remain separate. Tools slot headers use normal foreground text and omit unreported charges, preserving empty slots. CopyNameMenuItem is the common plain-text action; existing chat-link and structured copy actions remain distinct. Routine character and inventory scheduler waits are neutral, errors and disabled-policy stale data remain actionable.

Plain-name actions preserve UTF-8 via Windows Unicode clipboard. Exact metadata names take precedence, saved names are used when no exact metadata is available, and unknown IDs are never copied as names. Upgrade/infusion lists, character selection entries, native component slots/catalogs, saved component/set/fashion names and ordinary gear/skill menus share CopyNameMenuItem without replacing configuration or chat-link copy. Manual inventory refresh uses the same MatchesStorageView predicate as the footer; an incompatible character/account-storage combination has no matching request. Routine Clears rollover is neutral; known transient Inventory failures with automatic retry are yellow rather than prompting a forced full refresh.

Section header underline spans its entire available width, including table columns. Inventory label click returns to Items; dropdown and history preserve explicit subviews. Wallet shows disk-cached currency icons using the common IconManager and reserved currency ID namespace. Compact Loadout Components disclosure follows skills and precedes equipment in both preview modes.

Top-level Loadout Edit now stages component, set and Fashion selection changes in the parent typed draft. The reference dialog Save stages changes (does not write), the top Save commits atomically and Cancel discards the draft. Overlapping unsaved payload edits block replacement of that category; unrelated edits survive reference changes. Linked gear uses the same editor, private by default, with explicit per-kind shared checkboxes in Loadout Components. Separate library component/set editors retain their explicit independent transactions. Private inherited edits fork shared sets, preserve unmodified categories and reuse exact existing components. In-game review still required.
