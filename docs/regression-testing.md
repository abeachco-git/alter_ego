# Regression testing

Run the platform-independent suites using the native compiler:

```sh
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests --parallel 2
ctest --test-dir build-tests --output-on-failure
```

Run a single domain with `ctest --test-dir build-tests -L comparison --output-on-failure`.
Run a single case with `ctest --test-dir build-tests -R 'comparison.infusion_totals' --output-on-failure`.

Nine domain executables contain 34 independently runnable regression cases.
Each CTest case is a separate process. Keep assertions enabled even in Release.
Add related coverage to existing cases; add a new case only for distinct behavior,
and register it in the existing domain in tests/CMakeLists.txt. No new standalone
test executable per bug. Use invariant checks for presentation rather than fixed
pixel values or exact cosmetic wording. Preserve semantic, data integrity,
original library compatibility and import checks. Obsolete experimental fixtures may be
removed after demonstrating that the behavior is no longer supported; Git history
is the archive. Do not remove tests solely because they fail.

The Windows workflow gates DLL packaging on all offline cases and binary checks.
MetaBattle is excluded from automated regression testing by explicit user request:
neither the offline parser case nor the live website workflow runs. The runtime
importer is retained. Snow Crows and all other offline cases remain enabled.
Skill icon research is manual-only; it builds a baseline DLL for accurate size
comparisons and uploads the report.

## CI maintenance

The Windows workflow uses ccache for both native regression compilation and
UCRT C/C++ compilation. GitHub Actions restores the previous compiler cache
and saves an updated cache after successful artifact packaging. Compiler
content, flags, source and header dependencies remain part of ccache validation;
do not enable unsafe sloppiness options. The 500 MB cache holds compiler
outputs only, never final test executables, test results or packaged DLLs.
Every build still configures, links, runs all offline tests, verifies the exact committed icon bytes in the linked DLL and embedded
assets and UCRT imports, and packages a newly linked DLL. Cold builds may be
slightly slower; use the reported ccache hit/miss statistics and workflow step
timings to evaluate warm builds. If the cache is absent or evicted, compile
normally.


Do not reintroduce automated MetaBattle checks without explicit approval. Do not weaken the remaining required offline cases or binary verification to resolve a failed build. Follow the delivery definition in [AGENTS.md](../AGENTS.md).

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

Achievement catalog regression coverage includes ordered group/category membership, definition bits/flags/prerequisites, rollback on invalid identity, group freshness rejection, legacy name migration. A populated v13 cache migration verifies retirement of both hero point tables and the waypoint catalog while preserving character rows, achievement names and verified name-index freshness. Legacy character import ignores retired hero point fields.

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

### Character memory integration

The character_request regression includes plausible level/profession/race/gender bounds, active/target roster membership, known account conflicts, gameplay and pending switch gating. Live metadata need not equal cached API metadata. In-game smoke tests: hover active and inactive rows in both compact and expanded layouts, verify map labels and unknown-map fallback; right-click Load on an inactive same-account character, confirm no repeated request during transition, then load a different character. Confirm active and other-account rows cannot load; invalid signatures must disable loading and invalid gameplay state must block the actual switch with an explanatory alert. The integrated path and subsequent game builds need runtime validation; host tests cannot prove internal call thread safety.

Character loading compatibility is structural rather than timestamp-based: both inspected November 2026 executable revisions have identical relevant layout and full switch/request function bytes. Host regression covers fingerprint mutation and truncation. An altered layout or function body must block loading; runtime validation remains mandatory after game updates.

Clears daily rollover requests use the existing category endpoint first and a documented alternate bulk route with explicit schema 2022-03-23 and language when a response is missing or unchanged. Both use no-cache headers, HTTP status and category identity validation; both numeric and object achievement-ID formats are tested. Unchanged lists remain unverified, incomplete detail ID sets cannot replace data, and the existing bounded retry cadence is preserved. The status row shows Waiting for API rather than Ready during backoff. This does not guarantee the ArenaNet service rotates promptly; a live reset remains necessary to validate behavior.

Inventory filter smoke test: confirm no Storage status tree, Character is the first filter, profession icons and colors match Compare, multiple selected characters remain selected while the popup is open, and All clears the character facet alone. Check roster-only names without class metadata, removable chips, account scope, and that opening or changing filters starts no API requests. Compare retains its Active choice and closes after choosing one character.

Tools smoke test: select characters with different gathering/fishing/Jade Bot items, verify names, icons, right-click Find in Inventory, known glyphs vs Unknown, charges including zero, and unchanged utility rows when switching Equipment templates. Check Back/Forward between Tools and Equipment; restart to verify cache round-trip. The database regression covers absent, zero and positive charges plus utility slot selection priority.

Wallet visual smoke: empty favorite box and gold asterisk use identical size, toggling persists and pins/unpins currency, alternating row shading remains after filtering/sorting, coin display and descriptions remain intact. No duplicate Ready/stale label in the list; footer reports missing, failed and stale per account.

Main tab grouping visual smoke: verify the muted dividers before Inventory and Vault with either adjacent tab selected, All Accounts disabled tabs, and a narrow/scrolling tab bar. Dividers must not be clickable, selectable or affect dropdown hit targets.

`imports.build_relay_policy` checks both enabled and disabled code recognition and import guidance. In-game smoke test: with the default disabled flag, inspect all share/context menus and import modal, paste a four-character code (generic rejection, no request), and verify AE2, game links and URL imports remain available. If re-enabling, test both directions against the relay and update privacy disclosures.

`refresh.achievement_names` exercises initial/daily/full planning, retry cooldown, automatic policy and exact batch identity validation (partial, duplicate, unexpected, malformed or empty results rejected). Database core verifies separate completed-check freshness, legacy import, rollback and ordinary name writes preserving verification. In-game smoke test: on startup no names fetch; open Achievements with an empty cache, search while it populates, restart and confirm cached names, enable Public Catalogs and verify incremental check after 24 hours, use Refresh Achievement Names to revalidate all listed IDs. Check failures leave old search results usable and report incomplete status; same query and popout reflect newly fetched names. Confirm ordinary progress refresh is unchanged.

Waypoint retirement smoke test: expand Lighting the Echovald Wilds in Achievements and the popout; no obsolete WP actions should appear. Pin/unpin, copy objective names in the tracker, sub-achievement/Skinventory navigation and Open in Wiki must still work. Vault assistance and vendor location links remain independent.

Public catalog coverage in database_core checks seven-day freshness, failure backoff/manual override, typed upgrade classification, Vault definition parsing and rejection of incomplete batches. Live API samples are an optional diagnostic, not an offline regression gate.

Public catalog tests cover v17 pet/legend persistence, exact protocol code resolution and historical picker display normalization. Live probes are bounded samples, not a full catalog scan or in-game validation.

Public catalog cadence tests verify independent weekly discovery and monthly definition refresh, manual override, combined fetch planning, canonical new item persistence and independent success timestamps. In-game verify fresh checks skip network, new item/skin discovery, and failure retaining prior success ages.

Wiki resources have offline Python validation tests and existing C++ rotation tests. Normal build compiles validated normalized tables; live probes are not evidence of complete acquisition catalogs. Verify first-run logs separately.

Hybrid Wiki tests cover shared generated phase identities, wrapped/raw JSON parsing, malformed sequence rejection, midnight alignment, runtime projection into Vault, embedded/cache selection, transactional persistence and rollback, and independent 30-day runtime freshness versus daily build eligibility. In-game check Nexus origin and age, force refresh with network available, then simulate offline failure and verify cached timer use through restart and reset. No live requests during offline tests.

Wiki freshness regression in database_core covers six-hour/60-minute event boundaries, stale date windows, 30-day/24-hour skin boundaries, cache v18 migration, old metadata retention, atomic per-record verification and independent failed-attempt age. In-game: select a skin with a cached image, verify only eligible metadata is retrieved, reselect immediately and after restart, simulate an invalid/failed page and confirm the prior acquisition and retry age survive. Leave Achievements open through expiry, simulate Wiki outage, verify no request flood and no expired dated event shown; reopen and check persisted cooldown. Existing image failure policy stays independent.

Unified freshness initial pass: section_refresh exercises configured objective interval, manual bypass, minimum reset epoch, stagger boundary, active/offline character policy primitives, and per-ID in-flight dedup/cooldown. native_inventory verifies Armory 30-minute TTL. Live verify no second account achievement HTTP request on category switches, Clears entry or pinned popout when a sufficiently fresh shared response exists; verify manual override and account isolation, Vault periodic polling with independent section failures, claimed-section suppression and reset rollover recovery. Later integration must exercise the character activity scheduler and durable state explicitly.

Verify weekly reset observed while Vault is visible, then open Clears: missing weekly definitions must be retrieved, even with valid daily rows. A manual Clears refresh retrieves all categories including completed weekly raid objectives; ordinary daily rollover reuses valid weekly definitions. Verify season metadata checks independently of claimed objective sections.

Offline reconciliation boundaries: 599 seconds since last seen must suppress an automatic pull; 600 permits it when activity is unreconciled. An early manual fetch bypasses the wait but cannot advance the logout coverage watermark. Verify request-start time, intervening login/activity, per-scope success/failure, restart persistence and absent snapshot cases when wiring the runtime scheduler. No assumption that the API guarantees data propagation within ten minutes.

Activity integration: section_refresh covers active demand eligibility, specialization change debounce, transient non-gameplay, confirmed logout, early manual response, post-grace reconciliation, per-scope independence, account identity and persistence. In-game verify Nexus gameplay signal on character select and loading, open/close and switch tabs during requests, no character HTTP while hidden, active inventory search refresh after 10 minutes, offline inactive rows remain usable, explicit early manual action and final delayed pull, multi-account and unknown roster behavior, and physical equipment parity with previous schema.

Confirm that disabling specialization refresh suppresses early build/equipment pulls but leaves normal 10-minute demand checks intact, and that a physical projection parse/storage failure does not mark Inventory current.

section_refresh covers fixed endpoint classification without retaining request URLs, rolling second-bucket expiration, session counts, HTTP 429 including incomplete reads, active counts, peak and shared broker counters. In-game verify API Activity stays quiet while hidden, manual and periodic actions produce expected transport counts, Wiki request starts are spaced, and startup prefetch/unload do not block the render thread. HTTP success alone is not data validation.

The fixed-icon generator enforces raid modifier IDs and 32x32 RGBA PNG format; linked DLL verification compares exact bytes. In-game test both Clears layouts with Wiki offline and artwork cache absent. No Wiki request for these icons should occur.

Artwork smoke: with a cleared artwork cache and offline network, verify all embedded icons render in both Clears and character views; delayed Nexus texture creation must not trigger HTTP. Dynamic artwork uses shared transport counts; verified cached artwork does not re-fetch across restart, failed download backs off seven days, cache rebuild clears failures, and shutdown cancellation does not record a failure.

Companion fixed assets: verify all 75 exact resource IDs and 64px RGBA PNGs at build time. In-game: open Revenant legend editor and Ranger pet picker with an existing public catalog and the artwork cache removed; no HTTP artwork downloads for the checked-in IDs. Unknown future IDs retain dynamic retrieval.

Cache v20 tests in persistence.database_core cover verified raw achievement isolation from UI projections, exact-account identity, stale and invalid writes, rollback, independent public Vault season verification/end persistence and cache deletion. Live objective checks: restart with fresh verified progress and season metadata, visit both Achievements/Clears and Vault without unnecessary HTTP, cross reset boundaries, force manual refresh, simulate failed persistence and missing progression permission, and confirm optimistic UI rows are never consumed as verified raw progress.

Metadata broker regression coverage includes bounded join, cancellation, release notification and aggregate local claim/coalescing/cooldown counters. Live: overlap a manual import/capture with a visible item enrichment request and verify only one same-ID fetch; open specialization metadata concurrently with trait lookups and verify shared ownership. Synchronous consumers retain their existing immediate lookup contract; bounded timeout may leave unresolved names rather than issuing duplicates.

Character scope regression: section_refresh checks visible masks, specialization opt-in, independent build success, oldest demanded timestamp and per-scope freshness. In game: toggle Equipment/Build/Tools/Compare, verify only demanded stale endpoints run, fail one endpoint and retain old fields while successful scopes commit, restart and check independent verification, then test same-account switching, manual scope refresh and logout grace. Verify physical gathering-tool and relic fallback mappings with current API data.

The v21 character cache fixture verifies legacy aggregate rows have no per-scope evidence, independent stamps and physical provenance round-trip, and invalid scope timestamps roll back the transaction. A coalesced physical response from before logout grace cannot reconcile a later consumer ticket.

Priority admission regressions verify manual/objective overtaking, FIFO within rank, bounded queue and cancellation. API Activity tests verify reason-scope restoration and separate render/API/Wiki counts. Live: run catalog discovery while manually refreshing character or Vault, confirm the next admitted request honors priority without violating host spacing, and confirm no sensitive identifiers appear in diagnostics.

Verify first-time missing character scope remains pending until ten minutes from login; explicit manual refresh can retrieve it immediately without treating the final offline state as reconciled. Live-test manual selected-character refresh of an original v2 legacy unscoped row and confirm only verified roster ownership is assigned.

Metadata ownership stress: leave the character/equipment, compare, fashion and stat selector views open during concurrent catalog updates and manual refresh; exercise imports while metadata is replaced. Consumers retain immutable records for their entire use. Verify no use-after-free or torn strings, and no per-frame deep copy of metadata records.

Shutdown/ownership stress: unload during queued, rate-limited and in-flight character, inventory, achievement, catalog, Clears and Vault requests; wait for owned jobs, confirm cancelled activity attempts do not advance successful verification, and restart with preserved cached data. Switch account A→B→A while definition/completion workers are pending, trigger a reset during a fetch, and ensure stale workers neither publish nor clear a replacement's busy marker. section_refresh covers cancelled/replaced activity tickets and transition-time shutdown observation. Real Nexus unload and blocking I/O latency remain live acceptance gates.

Activity regressions include two replacement tickets with the same second timestamp; the earlier completion or cancellation cannot change the replacement's attempt. Live: rapidly switch characters during catalog refresh and reload/unload; queued identity observations must be applied on the owner thread, final login history retained, and alerts never mutate another account's cache. Host callback thread guarantees and actual unload latency remain unverified.

HTTP shutdown smoke: cancel during DNS/connect/send/read, including Wiki artwork and ArenaNet responses. Confirm no subsequent network stage starts after observed cancellation, no partial response is published, and cancelled artwork is not marked remotely failed. Test slow streaming beyond the cooperative 30-second body deadline and oversized responses above 64 MiB; measure actual unload latency separately because synchronous WinINet operations may overrun the cooperative deadline.

Metadata cancellation: section_refresh checks an abandoned claim is immediately eligible again, only attempted IDs in a partial batch enter cooldown, and cancelling a manual retry preserves the previous cooldown. Live: unload before a queued job begins and between 200-ID batches; confirm unattempted IDs are not reported as failures. Verify forced item refresh bypasses ordinary cooldown without duplicating in-flight work; malformed/unrequested specialization, skill and stat IDs cannot replace the cache. Durable per-ID outcomes remain a separate test gate.

Character footer regression: section_refresh covers scheduler reason/eligibility agreement at login, specialization debounce, failed-attempt retry, offline grace, successful reconciliation and replacement-ticket failure ownership. In-game verify status and timestamp hover at narrow width; verify the character-level logout countdown appears once across multiple stale scopes, individual retry waits remain visible, and the next-eligible countdown is not repeated when identical; test automatic refresh disabled, missing ownership, partial endpoint failure, manual refresh during grace, active/offline transitions and countdown expiration. Confirm no new request originates from tooltip rendering.

Equipment/freshness UI smoke: compare Characters Equipment, Library viewer and edit at normal/narrow widths; check table alignment, weapon sets Set I/Set II (one row per set at normal widths, vertically centered labels, unshaded 8 px gap, alternating bands, aligned stacked fallback at narrow widths), long wrapped main/off-hand names and sigils, empty off-hand, Relic section, aquatic gear, infusion badges, and live save/edit context actions. Tools: check bright slot labels, missing slots, zero vs unreported charges. Copy Name: paste plain names separately from game links and structured configuration. Inventory: select one/multiple characters + Bags or Equipment, manually refresh and verify only matching character storage is requested, cached rows stay visible, and routine automatic waits are neutral. Verify Compare shares character freshness tooltip. Live tests pending.

Pin/UI batch: native_inventory checks persisted currency icon URL and unknown definition state. In-game check shared pin states in all three surfaces, legacy pins survive restart, icons cached across restart, pinned-first game order, Inventory label vs arrow vs Back/Forward, full-width section underlines in both equipment modes, and compact component control placement. Test old currency schema migration without losing balances.

Unified edit regression: native_editing exercises shared set private fork across multiple modified categories, unchanged other owner, exact reuse, explicit shared edit and failed transaction rollback. In-game verify select reference then change gear/skills/name, cancel vs parent save, conflict rejection if changing an already-edited category, preserved shared choices only for original selected identities, partial sets, stale revision and both preview modes.

Components smoke: default all nine groups, multi-select type filters and Clear all, empty groups/create, search and temporary expansion, both sort directions, unused only, resize, Back/Forward, exact kind+ID reveal (including duplicate IDs across categories), row/group/empty-space context actions and compact +, portable copy/import, guarded referenced deletion and stale-revision confirmation, no raw JSON preview. Verify full-width section underline in Characters and saved view/edit at varying widths; no schema changes or API work from filters.

The native_editing case also verifies Components empty/multiple type facets, both Name and Usage sort directions, case-folded ties and stable IDs using ComponentBrowseCore.h. Right-click selects its exact typed row before displaying its actions.

Unified Inventory search smoke: search a currency and physical item with overlapping numeric IDs, select both and verify Back/Forward and Find in Inventory distinguish their kinds. Check Wallet and Currency browsing, mixed type choices, positive balances, account scope, quantity (coin in copper), freshness, and exclusion by character, rarity, binding and transfer filters. Verify only Copy Name/Open Wiki on currencies, no physical stock or character actions, unchanged dedicated Wallet and no per-keystroke API requests. Offline native inventory regression covers wallet facet/balance eligibility and typed identity.

Clipboard ownership smoke test (Windows, in game): copy a plain ASCII and non-ASCII name, an item chat link and portable component JSON into a text editor; paste the component back via the existing import menu. Verify copy does not clear existing clipboard contents when the game has no valid owning window or the clipboard is held open by another process. Verify text-input Cut/Copy/Paste/Select All independently; those retain the host ImGui callbacks. Offline host regressions do not exercise Win32 clipboard ownership.

Library consistency smoke: create a standalone partial Equipment Set from an empty list; cancel, reject blank name/no selections, save and verify no Loadout changed. Test Components New/Paste and inline stored-details disclosure. Verify aquatic-only and blank Loadouts in view and Edit, including selecting underwater weapon types and preserving existing terrestrial weapon sets on save. Offline tests cover creation rollback, aquatic presence and weapon eligibility.

Library polish validation: test normal/disabled Skins and Wizard's Vault tabs, achievement Copy Name on main and pinned tracker, clipboard Unicode via host callbacks, view row hover/navigation/context, inherited indicators and compact Fashion row, inline reference changes updating traits/skills/gear, partial set switching, cancellation and overlapping edit rejection. Standalone set and cross-context popups use same reference fields. Check Regenerate Name with missing/late metadata; preserve all manually named records. Utility permutation regressions preserve exact stored payloads.

Gear Customize removal smoke: click X on a long exact infusion name and on each position in a three-infusion item; verify only the selected requirement disappears, Save enables, remaining ID/attribute pairs stay aligned, totals update, and removing the final infusion offers Add infusion. Repeat through right-click Clear. Check child Cancel and parent Cancel leave persisted data intact; save both drafts and reopen to verify the empty recorded snapshot survives. Verify New Loadout modal title and profession context creation.

Last Login regression: historical import must not fabricate last-seen freshness; newer history wins, accounts remain independent, unknown timestamps sort last in both directions, periodic activity/spec changes and logout do not invalidate login ordering, same-character relogin does, and persisted login history survives restart without restoring active identity. In game, check direction, mixed/filtered unknown rows, character switch, same-character relogin, selection preservation across roster updates and Compare, and legacy unresolved history.

Untrusted input: `persistence.database_core` checks valid apostrophes and SQL-like text, nested objects, duplicate keys, depth/string limits and raw versus escaped NUL. In-game verify paste of original and current loadouts/components, legacy v2 JSON and current ZIP backup, AE2 v2/v3 and unpadded links, invalid import rejection, and preservation of current library after failure. Wiki and image network validation remains a separate test boundary.

Security follow-up: offline tests cover approved artwork URLs, cross-origin/HTTP redirect rejection, cache key path traversal, PNG dimension cap, invalid image header, unbound SQL parameters and trailing SQL. Wiki Python tests cover same-host URL policy and C++ literal escaping. In-game verify real Wiki skin and currency artwork, ArenaNet icons, source-site imports and redirects; check invalid images never replace cached files.

Security corpus: required Windows workflow builds `tests/untrusted_fuzz.cpp` natively with ASan/UBSan and runs 12,000 deterministic malformed-input mutations. Portable import tests reject hidden name bytes. Verify custom font selection survives backup to another installation, invalid paths do not access remote shares, and original v2 library migration still works.

Font retirement: legacy persisted face 1 falls back to Nexus; face 0 remains default and face 2 remains custom. Existing local font files are untouched and selectable. Verify default font, legacy Inter preference, and local custom font size selection in game; source tests check persisted face decoding and custom identifier stability. No Inter resource or automatic download in the DLL.
