# Development status — composable loadouts

Baseline: `42d589366fc2345bbc00a90ee554dfe0e94bc626`. This ledger describes source changes; CI and in-game validation must be recorded separately.

| Workstream | Status | Outstanding completion criteria / validation |
| --- | --- | --- |
| 1 Background worker ownership | In progress | Queue workers are explicitly joined; this pass fixes lost loading markers, wiki worker exceptions and stop notification. Audit remaining translation units, queue admission during shutdown, cancellation/failure regression and in-game unload. |
| 2 External callback lifetime | In progress | Admission/in-flight tracking and deregistration ordering across Nexus, fonts, textures and H&S; external quiescence contract remains unverified. |
| 3 Account isolation / correlation | In progress | Character gate and exact account persistence exist; typed selection/capture, completion and skin responses, delayed identical request correlation remain. H&S v4 request/response lifetime limits require verification. |
| 4 Incremental character persistence | Implemented — validation pending | Targeted single character writes and verified account roster deletions; previous disk and memory roster retained on deletion failure; warning on character write failure. New rollback/scope/cascade tests in database_core. Remaining: failed initial migration recovery, partial field freshness, retry of failed character writes, concurrency validation and connection overhead. |
| 5 Immutable render data | In progress | Audit item/stat, skill, trait, specialization, account and other container getters; ensure selection/order revisions match. |
| 6 Cache management / recovery | Not started | Existing diagnostic UI only. Implement safe startup maintenance, selective invalidation/rebuild, integrity, quarantine, artwork and confirmed results; never touch durable storage. |
| 7 SQLite benchmarks | Not started | Reproducible fixture, startup/reads/writes/contended workload, DELETE/WAL and durability choices. |
| 8 Wiki metadata migration | Not started | WikiImage JSON metadata and failures still on disk; inventory all remaining cache paths and migrate deliberately. |
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
