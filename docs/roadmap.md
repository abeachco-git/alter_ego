# Roadmap — outstanding work

Forward-looking tracker for the experimental branch. This file contains only unresolved implementation, investigation and design decisions. [Development status](development-status.md) owns current verification gaps; [Architecture](architecture.md) and [UI reference](ui-components.md) own established behavior; [release checklist](release-checklist.md) owns release gates. Git history is the archive. Historical source audit and verification details remain available in [the pre-cleanup revision](https://github.com/abeachco-git/alter_ego/blob/aa94826e0dfeb6f019976ad78a941a4a060c10d1/docs/roadmap.md).

**Approval:** proposed means discussion only; previously approved outstanding work is identified below. No new implementation is authorized by this documentation cleanup. Priorities are relative, not a mandate to implement unrelated work. Split broad items into explicitly approved, independently testable batches.

## Approved character identity reconciliation

- [ ] Part 7A.2: verify unchanged mappings do not advance the revision and canonical evidence validation; finish explicit collision representation, targeted uncertainty, confirmed absence handling and history bounds. Do not activate live adoption until request fencing and recovery are integrated.

- [ ] Wire the single authenticated roster owner to reconcile full core evidence before any destructive pruning. Bootstrap from complete verified cached core evidence only; incomplete or ambiguous snapshots must retain prior evidence. Reuse normal roster/core retrieval; explicitly assess request volume and generation fencing before altering admission. Detect name swaps and same-name replacements even when the name set is unchanged.
- [ ] Part 7B: integrate crash-recoverable identity adoption with cache pruning and stale request rejection, including physical inventory and character detail publications; preserve account isolation, old verified evidence and historical provenance, never redirect queued Fast Load.
- [ ] Parts 7C–7D: migrate expected Loadout, activity, custom ordering, session/navigation, Inventory filters/group expansion and verified Fashion associations to stable local IDs or safely reconcile them. Preserve original capture names and never infer ownership from historical name strings. Establish notification and unresolved-match behavior; complete migration/backup/recovery and regression/live acceptance.
- [ ] Part 7E: extend fixtures to live integration and interrupted adoption across stores, missing/partial API data, deletion/recreation, repeated rename, collisions, name reuse and swaps; no changes to native Fast Load internals.

## Remaining refresh demand follow-up

- [ ] **Separate discussion:** decide which public catalogs genuinely require startup/settings-triggered retrieval. Do not change that policy as part of the approved account settings fix.

## Remaining Library work

- [ ] **Approved remainder:** inline inherited-category editing: review whether replacing an inherited category should require switching/detaching the set or a deliberate private override; do not silently mutate shared sets. Preserve the existing atomic parent Save review and explicit shared consent. Preserve the existing equipment ownership and draft rules; do not reopen completed Part 1 work.
- [ ] **Approved remainder, design decision needed:** auto-name provenance and deferred safe repair. Explicit Regenerate Name and readable suggestions already exist. Persist provenance only with a reviewed compatibility design; never rename custom/imported names or change identity. Resolve missing metadata through existing owners, not render-time SQL/HTTP. Agree naming formats and how confidently identified legacy auto-names can be repaired.
- [ ] **Audit remainder:** verify all create/edit/capture/import/migration, exact-reuse, compatibility, explicit-sharing and revision paths; reuse the established comparison and exact-identity helpers rather than duplicate semantic engines.

## Remaining freshness, metadata and request ownership

Previously approved program; the current intervals, coverage watermarks and ownership rules are documented in Architecture. Independent character scopes, raw achievement progress, durable season metadata, independent Clears/Vault section commits, priority transport admission and item/skin/reference publication ordering are already implemented. Remaining changes to scheduling, retries, transport or caching require specific scope approval under AGENTS.md.

- [ ] Audit remaining metadata owners (achievement definitions/catalog, professions/palettes, pets/legends, remaining Commerce/Wiki and account-specific paths) for durable per-ID freshness/negative observations, verified request ordering, cross-consumer coalescing, generation fences and cancellation/error publication. Do not reimplement completed item/skin/stat/dye/skill/trait/spec ordering.
- [ ] Complete outstanding dataset request deduplication and priority propagation; avoid worker waits during cooldown, audit upstream locks, extend stable staggering only where needed, preserve visible demand and no all-account startup sweep. Audit remaining category/pinned continuation ownership and shared public Clears definitions across accounts; preserve independent section commits and reset fingerprints.
- [ ] Expand diagnostic attribution for remaining endpoints/reasons and cache/dedup outcomes without sensitive data or telemetry. Audit before changing rate limits.
- [ ] Investigate schema-pinned character core `last_modified` as corroboration only; validate authenticated data before use. External play not observed locally cannot be inferred; do not replace the local activity/coverage model.
- [ ] Wiki owner audit: remaining image/metadata/currency coalescing and startup currency work; preserve known-good snapshots, reviewed seeds, cadence and host rate limits. Review practical need and rights before adding datasets; no bulk per-skin crawl.
- [ ] Public catalog cleanup: validate live completeness and duplicate identities before removing legacy static upgrade identities. Confirm remaining definition freshness and promotion of reviewed build snapshots; retain manual seed promotion where CI access fails.

## Security, thread safety and storage — outstanding

- [ ] Finish untrusted field/schema validation (settings, legacy imports, imported assets), remaining SQL raw-execution guard, decoder safety and broader parser fuzzing. Keep the existing bounded parser, deterministic sanitizer corpus and original library compatibility.
- [ ] Audit remaining render/event globals, immutable getter and selection/order revision boundaries, cross-domain lock ordering, remaining credential/request correlation, completion provenance and metadata failure/cancellation paths. Reuse the metadata work above; do not repeat verified owner migrations.
- [ ] Host callback/resource lifetime: establish Nexus callback quiescence before unmapping, raw Escape pointer lifetime, asynchronous texture/atlas ownership and shutdown ordering; never infer host guarantees from local tests.
- [ ] Strict cancellable HTTP transport design: synchronous WinINet calls remain non-interruptible despite cooperative bounds; review single-owner async state machine and cancellation/drain before implementation. No incidental transport changes.
- [ ] Storage recovery gaps: failed initial migration and failed-character-write retry policy; selective rebuild coverage, per-dataset size reporting, integrity check versus quick check, obsolete-path review and quarantine cleanup usability. Preserve originals and authoritative data.
- [ ] Optional automated TSan CI and journal policy review are deferred decisions; execution/measurement gaps are tracked in [development status](development-status.md), not required for the current consistency discussion.

## Known defect / investigation

- [ ] **Separate decision only:** whether disabling Fast Load should also disable native roster sampling. Current behavior intentionally retains sampling and character location retrieval; no change to native lifecycle without explicit approval.

- [ ] Intermittent Inventory/Skins row activation: user observed missed clicks with ArcDPS behind affected screen regions; narrowing search or navigating sometimes recovered selection. Shared pitch/hit-target corrections are implemented. Isolate host/overlay input routing by moving/hiding ArcDPS and reproducing in another Nexus window; no native-input bypass or speculative picker rewrite. Exact root cause remains unconfirmed.
- [ ] Reported Windows Defender detection: investigate against exact tested DLL and detection details if reproduced; do not claim malware clearance from CI or clipboard refactoring.

## Proposed or deferred features — discussion before implementation

- [ ] Inventory facets: wallet categories, armor weight, weapon subtype, level and tradeability where supported; retain typed currency/item distinctions and existing search-first/List versus Groups behavior.
- [ ] Acquisition aids: semantic alternatives, full Loadout eligibility, broader Shopping mappings, legendary material/currency requirements and unused ascended gear/upgrades; distinguish ownership from verified usability.
- [ ] Gathering tools/glyphs: existing tool display/storage remains; verify authenticated installed glyph/socket fields with explicit schema and fixtures before extending coverage. Track loose glyphs, distinguish homestead unlocks from stock, unknown vs empty, charges/binding and owner navigation; character-wide tools never counted per equipment tab or included in Equipment Sets.
- [ ] Trading Post: discuss order-depth-aware estimates, optional visible-row batching and price sorting. No market listing requests, automatic full-result price fetching or fee-policy change approved.
- [ ] Contextual achievement objective assistance: useful verified hints/chat links without external-page dependency or guessed matches; scope and sources require review.
- [ ] Wiki/resource additions (weekly effect/festival mappings, specialization prose, bounded skin/currency/vendor data, optional core specialization icon embedding) deferred until a practical need and rights review; existing embedded raid and companion icons are not new work.

## Verification and release

See [development status](development-status.md) for the current unresolved validation checklist, including catalog clicks, Wiki previews, live rollover, authenticated multi-account behavior, clipboard, migration/recovery, Library editing and storage. Keep release-only requirements in [release checklist](release-checklist.md). No implementation completion without the exact-commit required build gates and verified DLL delivery as specified in AGENTS.md.
