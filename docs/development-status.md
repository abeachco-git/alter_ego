# Development status — composable loadouts

Baseline: `42d589366fc2345bbc00a90ee554dfe0e94bc626`. This ledger describes source changes; CI and in-game validation must be recorded separately.

| Workstream | Status | Outstanding completion criteria / validation |
| --- | --- | --- |
| 1 Background worker ownership | Implemented — validation pending | Audited runtime translation units: all one-shot jobs use registry; IconManager/WikiImage are the two owned queues, with early stop admission, joined shutdown, thread-start failure and request/worker cleanup. Extended worker regression. Remaining in-game unload/artwork failure latency and external callback quiescence (workstream 2). |
| 2 External callback lifetime | Local admission/drain implemented — validation pending | Nexus render, input, events, H&S owned replies and fonts use the shared gate. Host guarantees against callbacks entering unmapped code, close-on-escape raw pointer lifetime, asynchronous texture resource consumption, and in-game unload remain unverified. |
| 3 Account isolation / correlation | In progress — response safety improved | Typed identities and exact account/ID helpers; Skinventory full API validation and staged exact batch responses; fashion unlock map cleared on account change; Clears/Vault/Achievement account checks under publication locks. Remaining: application-wide typed selection, per-request generation/correlation (identical H&S replies have no nonce), completion batch provenance, and multi-account in-game validation. |
| 4 Incremental character persistence | Implemented — validation pending | Targeted single character writes and verified account roster deletions; previous disk and memory roster retained on deletion failure; warning on character write failure. New rollback/scope/cascade tests in database_core. Remaining: failed initial migration recovery, partial field freshness, retry of failed character writes, concurrency validation and connection overhead. |
| 5 Immutable render data | In progress | Audit item/stat, skill, trait, specialization, account and other container getters; ensure selection/order revisions match. |
| 6 Cache management / recovery | Implemented — validation pending | Storage UI confirms startup maintenance; whole cache quarantine, artwork-only reset and four selective relational groups, known legacy source quarantine, operation result. Tests added in database_core. Remaining: per-dataset size granularity, full integrity UI vs quick check, selective group regression breadth, in-game restart and quarantine cleanup usability. |
| 7 SQLite benchmarks | Implemented — measurement pending | Reproducible synthetic SQL fixture on actual cache/library schema, three journal/durability configurations, startup projections/reads/character and library writes/connection overhead/contention/size. CI report; native Windows DLL timings and journal policy decision remain pending. |
| 8 Wiki metadata migration | Implemented — validation pending | Schema 9 stores typed acquisition and failure rows; native-first migration, cached runtime projections, artwork reset clears failure rows only. Remaining broader cache inventory (workstream 9) and in-game validation. |
| 9 Configuration / backup audit | Not started | Full storage ownership inventory, verify backup/restore and upstream migration, obsolete experimental paths. |
| 10 Component reuse / library | In progress | Audit every creation/modification/import path; exact identity, explicit sharing, compatible professions and revision checks. |
| 11 UI consistency | In progress | Continue common controls and account-safe selectors; storage UI and in-game narrow-window and clipboard validation. |

## This pass
- `src/CharacterCache.h`: account-scoped deletion of only verified absent rows, foreign-key cascade and transaction rollback.
- `src/GW2API.cpp`: serialize roster reconciliation with character persistence, retain memory state when deletion fails, log persistence errors. Existing refresh cadence unchanged.
- `src/IconManager.cpp`, `src/WikiImage.cpp`: queue failure/loading cleanup and stop notification; no external callback guarantee claimed.
- `tests/database_core.cpp`: new targeted deletion regression assertions.
- Required gates: offline suite, six strict live MetaBattle imports, Windows x64 UCRT DLL, assets and binary checks. No current-pass results claimed until CI is inspected.
- In-game: multi-account roster deletion and refresh, icon/wiki rendering under failed downloads, addon unload while queues active; then migration/library/backup smoke tests.

- Second implementation commit adds `CacheMaintenance.h`, classified startup maintenance paths, Storage & Cache UI and startup-only recovery tests. No in-game test or final CI pass claimed yet.
- First implementation commit `9612028c91411fcf354c8eb0e1d2111e6a109887`: live MetaBattle workflow `45463715043` succeeded; final commit requires its own workflows.

- Third implementation commit closes local queue admission before deregistration, prevents shutdown-driven negative-cache publication, contains queue initialization/allocation failures and extends background worker tests. No callback-lifetime guarantee claimed.

- Callback pass: close admission before deregistration and drain admitted bodies before joining workers or releasing state; rejected owned H&S replies are freed. Host must stop invoking callback addresses before DLL unmapping. Added callback_lifetime regression. No CI result yet.

- Wiki schema 9: native metadata and failure snapshots; strict legacy validation, authoritative empty snapshots, transactional replacement, artwork reset integration, diagnostics and regression coverage. No current-commit CI result yet.

- SQLite benchmark tool and Windows workflow report added. Current DELETE/FULL policy remains unchanged. Results are synthetic Python/SQLite host measurements, not DLL performance.

- Account response pass: typed Account/Character identity, exact skin batch IDs, atomic complete ownership publication, epoch check around API parsing, account-switch invalidation of transient fashion unlocks, completion account checks and response bounds. No refresh cadence change; H&S v4 lacks an explicit request nonce.

## Debugging handoff and deferred roadmap

- Last verified runtime code: `818fa528e6bf3a8c97a0efd728d2ac0130713a1a`. Windows build `45466226163` and live MetaBattle `45466226218` succeeded (32/32 offline, 6/6 strict live); DLL SHA-256 `9cb5efab59a631a4979671277cf669e9eead8f2a4e747eb6746874d79dc5ae77`. No in-game validation yet. Subsequent documentation-only commits do not constitute a new verified binary.
- Next phase is user-led in-game debugging and stabilization. Keep bug fixes focused, preserve cache/library data, diagnose against the exact tested DLL, and record confirmed fixes and remaining validation. Do not modify main. Maintain all eleven workstreams above; do not mark partial work complete merely because a helper exists.
- Future work approved for roadmap inclusion (not implementation): [native account inventory and Hoard & Seek integration](roadmap.md). Carry this link and a concise summary into every future handoff: native authenticated provider with optional external/compatibility modes, secure credential management, shared SQLite inventory/wallet/guild/armory, unified filterable Inventory UI, safe migration, and cross-feature inventory awareness. Stabilize the current experimental branch first; separate feature branch only after explicit approval.
