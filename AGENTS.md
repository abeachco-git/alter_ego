# Agent instructions — Alter Ego

## UI changes: reuse first
Before adding or modifying any UI, read [docs/ui-components.md](docs/ui-components.md) and inspect the existing canonical component. Do not create a parallel renderer simply because a feature lives on a different tab. Fix shared behavior at its source and migrate affected callers. Preserve the established appearance unless the task explicitly requests a redesign.

- Buttons: use `RenderActionButton`, `RenderGoldButton`, `RenderChipButton` (AddonShared.h) or `AlterEgo::UI::Button` / `SmallButton` for explicit compact/destructive roles.
- Dropdowns: use `AlterEgo::UI::BeginCombo` / `Combo`, or `RenderThemedCombo` for the existing gold variant. Preserve stable IDs, active/auto semantics and keyboard navigation. Always EndCombo on a successful BeginCombo.
- Text/search fields: use `AlterEgo::UI::InputText`, `InputTextWithHint`, `InputTextMultiline` (buffer and string overloads). The existing themed Cut/Copy/Paste/Select All menu is implemented in vendored ImGui InputTextEx and inherits Alter Ego's GW2 style. Preserve its theme, native selection and clipboard behavior; never duplicate that menu.
- Indicators, palette and tooltip text: use UIControls.h, CompletionTheme.h and existing profession/rarity helpers; do not invent per-tab color/size constants.
- Skills: use RenderSkillIcon / RenderEditableSkillIcon (both delegate to RenderSkillIconCore); traits use RenderTraitIcon. Infusion totals use InfusionTotalsView.h.
- Context menus: reuse existing domain commands (ExecuteSavedLoadoutAction / RenderBuildShareMenuItems for saved loadouts) and destructive action helpers. A new action should be implemented once and exposed wherever appropriate.
- Refresh buttons (including the footer and picker) MUST use the same standard button renderer and sizing as other actions. Visual styling and layout may be standardized; **do not refactor refresh request lifecycles, scheduling, retries, caching, or per-page refresh state without specific approval.** Preserve existing enablement conditions and click handlers. The shared footer lives in RefreshUI.h.
- Gear catalog controls: UI::CatalogMatches, CatalogCard, CatalogOption. Gear views/editors remain distinct in some areas; share typed pickers and presentation where semantics agree, never collapse missing, unknown, empty or imported fields into the same value.

## Guardrails
- Nexus supplies an ImGui 1.80 context and allocator. Do not upgrade vendored ImGui or change its ABI independently.
- Preserve upstream PieOrCake library import compatibility, composable semantics and saved data.
- Keep work on the requested branch; do not merge into main without explicit approval.
- For DLL delivery, verify GitHub Actions succeeded for the exact commit, retrieve the matching artifact and verify the Windows x64 UCRT DLL. Link the actual binary, not just a workflow page. Never describe a pending build as complete.
- Update the UI component guide when introducing or changing a canonical component. Explain any intentional direct ImGui exception near its call site.
