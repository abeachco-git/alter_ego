# Regression testing

Run the platform-independent suites using the native compiler:

```sh
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests --parallel 2
ctest --test-dir build-tests --output-on-failure
```

Run a single domain with `ctest --test-dir build-tests -L comparison --output-on-failure`.
Run a single case with `ctest --test-dir build-tests -R 'comparison.infusion_totals' --output-on-failure`.

Nine domain executables contain 24 independently runnable regression cases.
Each CTest case is a separate process. Keep assertions enabled even in Release.
Add related coverage to existing cases; add a new case only for distinct behavior,
and register it in the existing domain in tests/CMakeLists.txt. No new standalone
test executable per bug. Use invariant checks for presentation rather than fixed
pixel values or exact cosmetic wording. Preserve semantic, data integrity,
upstream compatibility and import checks. Obsolete experimental fixtures may be
removed after demonstrating that the behavior is no longer supported; Git history
is the archive. Do not remove tests solely because they fail.

The Windows workflow gates DLL packaging on all offline cases and binary checks.
The independent live MetaBattle workflow uses the same import executable with
a snapshot directory and verifies all six live imports strictly. Live site
availability never gates DLL packaging. Skill icon research is manual-only; it
builds a baseline DLL for accurate size comparisons and uploads the report.

## CI maintenance

Keep the six live MetaBattle import checks strict, independently reported, and upload their HTML snapshots for debugging. Live-site outages or changed content must remain visible in that workflow rather than blocking DLL packaging. Do not weaken the required offline cases or binary verification to resolve a failed build. Follow the delivery definition in [AGENTS.md](../AGENTS.md).

The persistence suite additionally exercises the SQLite foundation in
`persistence.database_core`: versioned schema, bound text, rollback, foreign
keys, migration interruption, restart, backup verification and rejection of
an unrelated database. This does not yet exercise the Loadout runtime adapter.

`persistence.native_library` exercises relational round-trips including
unknown extensions, physical sigils/infusions, Fashion, component references,
intentional duplicates, forced hash collision, exact candidates and usage,
restart, stale edit rejection and transactional writes.

`persistence.native_migration` covers original build and Fashion archives,
exact safe migration deduplication, abandoned staging directories, empty
initialized databases, corrupt native data, invalid legacy and unexpected
unexpected experimental v3 files, and verified database backups without overwriting destinations.

Native editing and portable interchange are covered by `persistence.native_editing` and `persistence.native_portable`: exact matching rather than functional similarity, reuse after modification, copy-on-edit, unchanged IDs, unknown fields and presence distinctions, intentionally unused records, linked Fashion and collision-safe portable merges. The backup tests exercise selective native ZIP/JSON restore and legacy import; obsolete mirror timestamp/reconciliation recovery tests have been removed in favor of native migration tests.

The database core case also covers 5,000 achievement progress rows and completed bits, exact account-name isolation, older snapshot rejection, transaction rollback, disposable cache deletion, configuration migration and archive independence. These are correctness regressions, not a timing benchmark.

Achievement catalog regression coverage includes ordered group/category membership, definition bits/flags/prerequisites, rollback on invalid identity, group freshness rejection, legacy name migration, waypoint and hero-challenge round trips.

Skin metadata tests cover preservation of catalog fields and Fashion dye data across both projections, ownership account isolation and stale snapshot rejection.

Commerce and cache-path tests exercise one-to-many and many-to-many item relationships, shared item price observations, stale price rejection, vendor prices and migration of rebuildable JSON to the cache directory.

Native editing tests also exercise explicit private-editor reuse, stale source and mismatched candidate rejection, preserved unrelated references and private Base Build profession protection.

Reference metadata coverage verifies typed skill, trait, specialization, item-stat and dye round trips, ordered child records, malformed/duplicate-row rollback, older snapshot rejection, legacy import and native precedence.

Item metadata regression coverage includes v6 canonical name sharing with Commerce, detail completeness, alias migration and precedence, stale-write rejection and duplicate-row rollback.

Storage diagnostics tests cover missing paths without creation, read-only database access, identity rejection, record counts, schema and artwork size.

Portable graph merge tests include same-ID/different-name records, incompatible Skills ownership, reserved collision suffixes, and verified compatible Skills reuse.

Native editing and migration also verify that aggregate edits cannot change an existing Loadout's profession, and that identical legacy Skills with missing or different professions remain independent.

Character cache coverage exercises native-first legacy migration, exact account identity, unknown-account records, ordered equipment and build data, missing newly persisted fields, stale capture rejection and rollback on duplicate identity.

Completion cache coverage includes same-slug account isolation, native snapshot presence, verified legacy migration, ordered encounter bits and objectives, season metadata, stale timestamp rejection and transactional rollback. Existing Clears reset and Vault rollover tests remain required; live account switching/reset behavior still requires game validation.

The refresh suite includes `refresh.background_workers`: closed admission, normal completion, exception cleanup, active job tracking, cooperative cancellation, rejected nested launch during shutdown, joining and reopening. This host test does not establish Nexus callback quiescence or in-game unload latency.

The refresh suite includes `character_request` for account/endpoint matching, consumed duplicates, legacy single-account handling and exact roster scope. Live callback ordering remains unverified.

Character persistence regression now covers incremental replacement, untouched same-name accounts and unknown rows, stale record rejection, rollback after malformed child data, and append ordering.

Verified roster deletion coverage checks account scope, preservation of unknown owners and row IDs, cascading child removal, idempotence, and rollback on a forced mid-transaction failure.

Startup maintenance regression covers exclusive scheduling, no live deletion, targeted character invalidation, preserved account progress, artwork-only clearing, preserved wiki metadata, corrupt database/sidecar quarantine, durable file preservation and consumed requests.

Background worker coverage additionally verifies throwing failure-handler containment, repeated launches, reopening and complete joining. Windows artwork queue behavior and unload latency require in-game validation.

Equipment set coverage in `persistence.native_library` verifies direct exclusive set links, exact capture matching, shared membership changes, copy/detach isolation, rejection of implicit aggregate gear edits, persistence, portable graph round-trip, duplicate consolidation and merge. Experimental SQLite schemas are not supported migration sources; original v2 migration remains covered by the native migration suite. Signed SQLite infusion IDs must survive conversion to unsigned GW2 IDs in SavedBuild projections.

### Equipment Set category ownership

- Create an armor-only set and a set that excludes relics, sigils and infusions; verify omitted categories remain independently selectable on linked Loadouts.
- Removing membership preserves the effective selection on all linked Loadouts; including a category prompts before replacing independent selections. Switching sets preserves categories not included in the new set. Copy & Edit Set preserves the current membership by default; detach preserves every effective reference.
- Verify mixed ownership after a database reopen, portable Loadout round-trip and shared membership changes. Conflicting owners and empty sets are rejected. Compare, share and infusion totals resolve the complete effective equipment.

### Navigation smoke tests

- Open a Loadout's Relic, press mouse Back, verify original Loadout and scroll; Forward returns to the Relic. Repeat across Characters, Equipment Sets, Fashion, Compare, Skinventory and Achievements. Change filters and refresh data without creating entries. Verify new navigation after Back discards Forward history.
- Check disabled arrows, Alt+Left/Right, no interception outside Alter Ego, no navigation during modal/unsaved editor, stable selection after sorting/deletion, and clean addon unload.

### Reference edit transactions

- Verify Base Build/Skills, Equipment Set/equipment and Fashion display groups in both read-only and aggregate edit mode. Open Edit Components or Edit Equipment Set, change multiple categories and name/Fashion, Cancel, and verify identical graph and revision. Save applies all changes together; invalid refs, empty sets, overlapping owners and stale revisions fail closed. Replace a linked category only after explicit confirmation. Check Assign to Loadout and Apply to Loadout from other collections use the same draft. Open component payload editor, edit nested fields, Cancel, and verify no shared change. Verify mouse Back and row switching cannot silently discard an open draft.

- Test gear customization Cancel/close after changing a two-handed weapon, infusions, relic or copying to several slots. No parent draft fields should change. Save applies the child to the parent only; parent Cancel still discards everything. Verify Fashion stale draft rejection and unchanged stable ID after edit.
