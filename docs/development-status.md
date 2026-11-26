# Development status — composable loadouts

Baseline: `42d589366fc2345bbc00a90ee554dfe0e94bc626`. This ledger describes source changes; CI and in-game validation must be recorded separately.

| Workstream | Status | Outstanding completion criteria / validation |
| --- | --- | --- |
| 1 Background worker ownership | Implemented — validation pending | Audited runtime translation units: all one-shot jobs use registry; IconManager/WikiImage are the two owned queues, with early stop admission, joined shutdown, thread-start failure and request/worker cleanup. Extended worker regression. Remaining in-game unload/artwork failure latency and external callback quiescence (workstream 2). |
| 2 External callback lifetime | Local admission/drain implemented — validation pending | Nexus render, input, events and fonts use the shared gate. Host guarantees against callbacks entering unmapped code, close-on-escape raw pointer lifetime, asynchronous texture resource consumption, and in-game unload remain unverified. |
| 3 Account isolation / correlation | In progress — response safety improved | Typed identities and exact account/ID helpers; Skinventory full API validation and staged exact batch responses; fashion unlock map cleared on account change; Clears/Vault/Achievement account checks under publication locks. Remaining: application-wide typed selection, per-request generation/correlation, completion batch provenance, and multi-account in-game validation. |
| 4 Incremental character persistence | Implemented — validation pending | Targeted single character writes and verified account roster deletions; previous disk and memory roster retained on deletion failure; warning on character write failure. New rollback/scope/cascade tests in database_core. Remaining: failed initial migration recovery, partial field freshness, retry of failed character writes, concurrency validation and connection overhead. |
| 5 Immutable render data | In progress | Audit item/stat, skill, trait, specialization, account and other container getters; ensure selection/order revisions match. |
| 6 Cache management / recovery | Implemented — validation pending | Storage UI confirms startup maintenance; whole cache quarantine, artwork-only reset and four selective relational groups, known legacy source quarantine, operation result. Tests added in database_core. Remaining: per-dataset size granularity, full integrity UI vs quick check, selective group regression breadth, in-game restart and quarantine cleanup usability. |
| 7 SQLite benchmarks | Implemented — measurement pending | Reproducible synthetic SQL fixture on actual cache/library schema, three journal/durability configurations, startup projections/reads/character and library writes/connection overhead/contention/size. CI report; native Windows DLL timings and journal policy decision remain pending. |
| 8 Wiki metadata migration | Implemented — validation pending | Schema 9 stores typed acquisition and failure rows; native-first migration, cached runtime projections, artwork reset clears failure rows only. Remaining broader cache inventory (workstream 9) and in-game validation. |
| 9 Configuration / backup audit | Not started | Full storage ownership inventory, verify backup/restore and original library migration, obsolete experimental paths. |
| 10 Component reuse / library | In progress | Audit every creation/modification/import path; exact identity, explicit sharing, compatible professions and revision checks. |
| 11 UI consistency | In progress | Continue common controls and account-safe selectors; storage UI and in-game narrow-window and clipboard validation. |

## This pass
- `src/CharacterCache.h`: account-scoped deletion of only verified absent rows, foreign-key cascade and transaction rollback.
- `src/GW2API.cpp`: serialize roster reconciliation with character persistence, retain memory state when deletion fails, log persistence errors. Existing refresh cadence unchanged.
- `src/IconManager.cpp`, `src/WikiImage.cpp`: queue failure/loading cleanup and stop notification; no external callback guarantee claimed.
- `tests/database_core.cpp`: new targeted deletion regression assertions.
- Required gates: offline suite (excluding MetaBattle by user request), Windows x64 UCRT DLL, assets and binary checks. No current-pass results claimed until CI is inspected.
- In-game: multi-account roster deletion and refresh, icon/wiki rendering under failed downloads, addon unload while queues active; then migration/library/backup smoke tests.

- Second implementation commit adds `CacheMaintenance.h`, classified startup maintenance paths, Storage & Cache UI and startup-only recovery tests. No in-game test or final CI pass claimed yet.
- First implementation commit `9612028c91411fcf354c8eb0e1d2111e6a109887`: live MetaBattle workflow `45463715043` succeeded; final commit requires its own workflows.

- Third implementation commit closes local queue admission before deregistration, prevents shutdown-driven negative-cache publication, contains queue initialization/allocation failures and extends background worker tests. No callback-lifetime guarantee claimed.

- Callback pass: close admission before deregistration and drain admitted bodies before joining workers or releasing state. Host must stop invoking callback addresses before DLL unmapping. Added callback_lifetime regression. No CI result yet.

- Wiki schema 9: native metadata and failure snapshots; strict legacy validation, authoritative empty snapshots, transactional replacement, artwork reset integration, diagnostics and regression coverage. No current-commit CI result yet.

- SQLite benchmark tool and Windows workflow report added. Current DELETE/FULL policy remains unchanged. Results are synthetic Python/SQLite host measurements, not DLL performance.

- Account response pass: typed Account/Character identity, exact skin batch IDs, atomic complete ownership publication, epoch check around API parsing, account-switch invalidation of transient fashion unlocks, completion account checks and response bounds. No refresh cadence change.

## Debugging handoff and deferred roadmap

- Last verified runtime code: `818fa528e6bf3a8c97a0efd728d2ac0130713a1a`. Windows build `45466226163` and live MetaBattle `45466226218` succeeded (32/32 offline, 6/6 strict live); DLL SHA-256 `9cb5efab59a631a4979671277cf669e9eead8f2a4e747eb6746874d79dc5ae77`. No in-game validation yet. Subsequent documentation-only commits do not constitute a new verified binary.
- Next phase is user-led in-game debugging and stabilization. Keep bug fixes focused, preserve cache/library data, diagnose against the exact tested DLL, and record confirmed fixes and remaining validation. Do not modify main. Maintain all eleven workstreams above; do not mark partial work complete merely because a helper exists.
- Track current native account, inventory and cross-feature implementation in the [roadmap](roadmap.md). Keep remaining validation and feature work visible there rather than duplicating the checklist here.

- In-game debugging report: Skinventory Short Bow and Harpoon Gun categories displayed no counts/rows. Fixed display-label vs API subtype mismatch at category indexing/lookup, shopping filters, saved selection and navigation, with shared alias regression assertions. Raw cached metadata unchanged; no cache reset required. Awaiting exact-commit build and user verification.

- In-game debugging report: Hide Complete left empty Daily Fractal card slots and section height unchanged. Pack visible cards by font-measured minimum width, share surplus across daily cards, keep compact Rec fixed and wrap as needed. Preserve original hover and completion semantics. Regression layout cases added; in-game visual validation pending.

- In-game debugging: Skinventory multi-select Browser/Shopping filtering, unified eligibility and counts, catalog/ownership generation invalidation, view menu and reveal navigation; remove mandatory category rail. Shared resizable split panels for Characters (including empty state), Loadouts, Fashion, Components, Achievements and Skinventory. Compare uses full-width results, not a fixed two-pane rail. Regression assertions added; in-game testing pending.

- In-game UI feedback: shared popup fields now render label first and aligned dropdowns, always-visible dim/inert Reset filters when inactive; Library Loadouts supports multi-select game modes with OR matching, legacy single-mode session migration, and regression checks (including Other for legacy loadouts). In-game validation pending.

## Native account and inventory status

Native authenticated account retrieval, account-scoped inventory, wallet, guild storage, progression and wardrobe ownership are implemented. The [roadmap](roadmap.md) tracks exact implementation and verification checkpoints; [architecture](architecture.md) documents storage and refresh behavior. Keys are entered explicitly in Nexus options and stored in Windows Credential Manager.

Remaining work includes independent dataset freshness, scheduling, semantic inventory alternatives and broader shopping acquisition mapping, plus authenticated multi-account and in-game validation. Do not treat an offline build as live verification.

The build cited above is a historical verified checkpoint, not the latest branch binary.

- Callback/UI ownership pass: chat and explicit Pie handoff copy bounded values into independent inboxes; toast and window/tracker keybind state is render-owned, no toast mutex across UI or Nexus calls. Ownership persistence captures the verified account and snapshot under the domain lock, serializes writes and cache loads, and never re-reads selection to save a completed response. Worker reopen fenced until join completes. Offline and Windows verification pending; native escape pointer, external texture/atlas lifetime and broad remaining account/render audit remain open.

- Account commit pass: exact credential epoch check and durable character/achievement publication share a lock with credential mutation. Reconciliation no longer reads shared replaceable roster state or exposes terminal status before finishing. Registry linking no longer holds credential mutex during roster network request. Native request rechecks epoch after JSON parsing. Offline and Windows verification pending; other account dataset commits and live host correlation remain open.

- Completion and ownership publication pass: account requests capture credential identity before launch, retries retain batch generation, final publication excludes key mutation; Clears/Vault write captured account data rather than looking up current selection after publish. Cache loads serialize against writes; watchdog timeout rechecks the active request. No freshness interval changes. CI/live verification pending.

- Clears/Vault rendering and rollover pass: remove domain mutex across entire content rendering, use coherent local frame snapshots; invalidate old Special response at known season advance and prevent stale Special restoration on persistence. Actual relational cache tests extend independent section/account fixture. CI and live verification pending.

- Completion/ownership first pass verified by workflow 45731076808. Frame-snapshot follow-up passed offline and parser sanitizer gates; corrected a Windows-only compiler error in the strike sort capture. Focused local ThreadSanitizer: 30 iterations each of `background_workers` and `callback_lifetime`, no reports. Final Windows build and in-game checks remain required.

- Inventory credential pass: retain the original registry generation and key per refresh, reject obsolete storage and failure commits, and serialize physical view publication with credential mutation. No refresh intervals or public metadata scope changed. Windows and live verification pending.

- Achievement presentation pass: independent revision caches, immutable shared definition payloads, coherent frame capture, name reverse index, pin actions by stable IDs and popout rebuild outside the domain mutex. Catalog serializers capture data before JSON conversion. 10,000-entry/1,000-frame cache regression added; automated and live verification pending.

- Achievement and inventory implementation at e6c49b2 passed required offline, ASan/UBSan, SQLite benchmark, Windows UCRT compilation, binary/resource/notice verification and artifact upload (45732287385); dependency inventory 45732287515 passed. ZIP artifact 15077297266 independently verified against its digest. Focused exact-source Linux TSan snapshot/worker regression passed 30 iterations. Follow-up cancels obsolete physical-equipment activity attempts and adds an opt-in repeatable native TSan configuration; final follow-up build and live verification pending.

- Same-credential ordering pass: shared roster owner, transactional roster/status/pruning, current durable membership at final character commit, physical endpoint owner writes failures as well as successes. Required CI and live verification pending.

- Metadata and shutdown pass: staged skin catalog publication, snapshot serialization outside render-facing locks, full/partial stat persistence ordering, interruptible catalog/price/artwork delays, font callback identifier validation and removal of stale roster/physical UI overlays. No refresh cadence or host ABI changes. CI and live verification pending.

Verification: implementation commits `d7bb44f` and `a8961e3` passed required offline regressions, parser ASan/UBSan, SQLite benchmark, Windows x64 UCRT and distribution checks (runs 45733189260 and 45733385817). Final dependency inventory 45733385862 passed. Artifact 15076808908 was retrieved; ZIP digest matched GitHub and the extracted DLL imports UCRT, not MSVCRT. Exact-source Linux ThreadSanitizer worker/snapshot/stop-delay regression passed one initial run and five repeated runs; a separate 30-run command exceeded the tool execution timeout and is not counted as a completed test. The opt-in full CMake TSan target, automatic TSan CI, live Nexus resource lifetime, broader metadata request ordering, synchronous WinINet cancellation and in-game verification remain open.

- Hardening follow-up: cooperative HTTP phase deadline, RAII ownership, cancellation-aware pacing and artwork publication, bounded public JSON, monotonic snapshot capture, delayed callback admission and earlier artwork worker teardown. Offline and Windows verification passed; strict synchronous HTTP cancellation and remaining cross-producer metadata ordering remain open. User deferred discretionary benchmarks and live in-game testing.

- Public item ownership implementation: schema v23, per-record request-start watermarks, commit-before-adopt for direct batches and Commerce, stale/equal rejection and removal of aggregate detail rewrite. Required CI and Windows verification passed. Other datasets still require audit.


Verification (2026-11-26 UTC): implementation `75203d0f7fa9bc883692133dafdb6967e1592379` passed all 36 required offline regressions, parser ASan/UBSan, the existing CI SQLite check, Windows x64 UCRT build and distribution verification (workflow 45734849005); dependency inventory 45734849121 passed. Artifact 15078252828 was retrieved, ZIP SHA-256 matched GitHub (`65fcfd33a938297fe5d8fc2133462b3faaed2d39a7620663d6baea3d3e4c85d8`), archive integrity passed, and the extracted DLL imports UCRT, not MSVCRT. Earlier attempts caught a shared test-fixture isolation issue and a migration SQL formatting mismatch in the existing benchmark schema extractor; both corrected without disabling checks. Discretionary benchmarks, full TSan configuration and live Nexus/in-game testing remain unperformed for this pass.

- Skin catalog/Fashion request ordering implemented with cache schema v24; no aggregate writes, canonical cross-projection adoption, bounded shared observation parsing and independent field groups. Offline/Windows verification pending.

- Reference v25 per-record ordered writes and commit-before-adoption for spec/trait/skill/stat/dye, palette admission cleanup and bounded native/Commerce parsing implemented. CI pending.

- Cancellation-state hardening implemented; durable retry markers now finalize only for non-cancelled catalog/Wiki outcomes. CI pending; asynchronous transport remains open.

- Metadata cooldown now requires actual transport send; cancelled HTTP diagnostics, relay and Clears bounded parsing implemented. CI pending.

- Native item/currency metadata ownership unified with request watermarks and shared publication. CI pending.
