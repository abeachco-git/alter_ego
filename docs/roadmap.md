# Roadmap — outstanding work

Experimental branch, continuing after Part 8. Only unresolved implementation, investigation and design decisions belong here. [Development status](development-status.md) owns live and deferred verification; [Architecture](architecture.md), [UI reference](ui-components.md) and [regression testing](regression-testing.md) own established behavior and test procedures. [Release checklist](release-checklist.md) owns release gates. Git history is the archive.

**Approval:** "Approved remainder" retains the previous scope; a design decision or proposed item requires discussion before implementation. Changes to refresh lifecycles, scheduling, retries, caching or transport require specific approval under [AGENTS.md](../AGENTS.md). Split work into independently testable batches. Do not reopen an implemented capability solely because its live acceptance is deferred; reopen on evidence of a defect.

## Part 9 — Metadata and request ownership

**9A–9B approved scope (2026-11-27):** independently audit the current implementation before changes; verify the findings below and remove already-completed requirements only on evidence. Implement in independently verified batches. Retain existing character coverage and credential/identity fences, exact-account isolation, shared roster/physical/progress owners, item/skin/stat/dye/skill/trait/spec/currency publication ordering, reset fingerprints, independent Clears/Vault section commits, intervals (established public definition and profession metadata policies are documented in Architecture), and visible-demand policy. No changes to Fast Load, native memory sampling, Wizard's Vault, character refresh intervals, transport admission/rate limits, or account-wide startup sweeps.

- [ ] **9B.3 — Approved audit and narrowly evidenced corrections only.** Examine upstream roster/physical/achievement broker contention, cross-consumer dedup, priority inheritance for remaining queued work, cancellation-aware delays, cooldown worker occupancy and stable staggering only where missing. Preserve existing logical ownership and publication/credential/binding fences. Do not blanket-convert roster or physical equipment owners or rewrite transport/worker pool; demonstrate concrete contention and regression-safe solution before changing sensitive owners; seek approval for material additional lifecycle changes. No preemption of admitted requests.

- [ ] **Character summary follow-up:** authenticated live parity of summary physical equipment and Inventory bags with existing schema-pinned endpoints; only then consider integrating them through the existing shared physical owner (never bypass publication fences). See Architecture and regression testing. No broader scheduling changes.

Remaining approved batch: targeted 9B.3. Verify each with relevant regressions and exact-commit DLL workflow. Keep live acceptance in development status. 9C–9F are outside this approval.

- [ ] **9C — Public catalog demand and retirement (design + live investigation):** decide which public catalogs actually require startup/settings-triggered retrieval; validate completeness and duplicate identities before removing static upgrade identities. Confirm remaining definition freshness and reviewed build snapshot promotion; retain manual seed promotion where CI access fails.
- [ ] **9D — Wiki ownership:** remaining image/metadata/currency coalescing and startup currency work; preserve verified snapshots, reviewed seeds, cadence and host limits. No bulk per-skin crawl or unreviewed datasets.
- [ ] **9E — Diagnostic gaps:** attribute remaining unclassified endpoints/reasons and cache/dedup outcomes. Existing API activity and metadata claim counters are not new work. No sensitive data or telemetry; audit before changing rate limits.
- [ ] **9F — Investigation only:** validate authenticated schema-pinned character core `last_modified` for possible corroboration. Already parsed/stored; no proven basis to infer external play or replace local activity/coverage.

## Part 10 — Security and lifecycle

- [ ] **10A — Remaining trust boundaries:** settings and legacy-import fields/schema, imported/on-disk assets and decoder safety beyond image header checks, residual raw SQL execution guard and expanded adversarial cases. Retain existing bounded parsing, prepared bindings, deterministic sanitizer corpus and supported interchange.
- [ ] **10B — Remaining concurrency boundaries:** audit render/event globals, immutable getters and selection/order revisions, cross-domain lock ordering and credential/request/completion publication not already covered by 9A–9B or the character identity work. Only fix demonstrated gaps; preserve canonical owners.
- [ ] **10C — Host contract investigation:** Nexus post-deregistration callback quiescence, raw Escape pointer lifetime, texture/atlas ownership and shutdown ordering. Local admission/drain and worker joining exist; host guarantees require evidence beyond local tests.
- [ ] **10D — Transport design decision:** synchronous WinINet is cooperatively bounded but not strictly interruptible. Review a single-owner asynchronous cancellation/drain state machine, including final callback quiescence, before approving transport changes.
- [ ] **10E — Deferred decision:** optional full native TSan CI integration. Existing required sanitizer coverage and focused TSan configuration remain; see development status for execution gaps.

## Part 11 — Storage resilience

- [ ] **11A — Recovery:** failed initial migration recovery and failed-character-write retry policy; preserve originals and authoritative data, including account identity and library.
- [ ] **11B — Cache maintenance residuals:** identify specific uncovered datasets/obsolete paths within existing selective rebuild operations and improve quarantine cleanup usability. Existing startup-only full/selective quarantine and preservation safeguards are implemented; do not rebuild that mechanism.
- [ ] **11C — Diagnostics:** per-dataset byte-size reporting beyond existing file sizes/counts, and integrity-check versus quick-check policy. Keep inspection explicit and off ordinary render paths.
- [ ] **11D — Deferred decision:** journal policy review only after native contention measurements (development status).

## Part 12 — Defect investigations and separate decisions

- [ ] **12A — Inventory/Skins row activation:** intermittent missed clicks correlated with ArcDPS overlapping affected regions. Shared pitch/hit-target corrections already exist; reproduce with ArcDPS moved/hidden and another Nexus window before changes. No speculative picker rewrite or native-input bypass.
- [ ] **12B — Windows Defender:** investigate if reproduced, using the exact DLL and detection details; CI does not establish antivirus clearance.
- [ ] **12C — Fast Load (separate decision only):** whether Disabled should also disable native roster sampling. Currently sampling and location remain available intentionally; no changes to native loading, memory scanning or switch lifecycle without explicit approval.

## Part 13 — Proposed features (discussion before implementation)

- [ ] **13A — Inventory facets:** wallet categories, armor weight, weapon subtype, level and tradeability where supported. Preserve typed currency/item distinctions and List versus Groups behavior.
- [ ] **13B — Acquisition aids:** extend existing exact-item/stock aids with semantic alternatives, full Loadout eligibility, broader Shopping mappings, legendary materials/currencies and unused ascended gear/upgrades. Distinguish ownership from verified usability; reuse canonical comparison and Inventory.
- [ ] **13C — Gathering tools/glyphs:** verify authenticated installed glyph/socket fields before extending existing tool display/storage. Loose glyphs, homestead unlocks versus stock, unknown versus empty, charges/binding and owner navigation; character-wide tools are not per-equipment-tab or Equipment Set data.
- [ ] **13D — Trading Post:** order-depth-aware estimates, optional visible-row batching and price sorting. No market listing requests, automatic full-result price fetching or fee-policy changes approved.
- [ ] **13E — Achievement assistance:** verified contextual hints/chat links without guessed matches or external-page dependency; review scope/sources.
- [ ] **13F — Wiki/resource additions:** weekly effects/festivals, specialization prose, bounded skin/currency/vendor data and optional core specialization icon embedding, deferred until practical need and rights review.

## Verification and release

Parts 7–8 and other completed implementation acceptance, discretionary performance measurements and non-live verification remain in [development status](development-status.md); release-only gates remain in [release checklist](release-checklist.md). Do not duplicate those lists here. No approved implementation is complete without exact-commit required build gates and verified DLL delivery per AGENTS.md.

