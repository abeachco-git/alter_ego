# Roadmap — account data and inventory

Living implementation tracker for the experimental branch. Checked items indicate implementation, not necessarily in-game validation. See [development status](development-status.md) for broader stabilization work.

## Implementation tracker (living)

- [x] Native registry, explicit API key setup, permission reporting, Windows Credential Manager.
- [x] Direct native character/roster, equipment/build, skin, achievement, Clears and Vault consumers. Verified build c54865a; live testing outstanding.
- [x] Account GUID ownership, same-account Fast Load gate, account-independent Library; conditional multi-account UI.
- [x] Personal bank, shared inventory, material storage, bags, equipment, tools, armory and delivery item storage.
- [x] Wallet balances and currency metadata, guild leader access and guild GUID-deduplicated stash/coins; CI passed at 972a3c9; live validation pending.
- [x] Per-storage TTLs, failed-attempt cooldown, visible selected-account opt-in inventory refresh; broader dataset policies and independently committing Vault/Clears periods still outstanding.
- [x] Inventory fuzzy search, rarity/type filters and name/quantity sort.
- [x] Exact-ID Compare acquisition aid and binding/freshness-aware location display; Inventory navigation from equipment and Shopping List; cached unique acquisition candidates subtract personal loose stock from remaining shopping cost (CI passed at 972a3c9; live validation pending).
- [x] Compare exact-ID upgrade, rune, sigil, relic and exact-infusion deficit hints; attribute-based alternatives remain semantic only.
- [ ] Semantic inventory alternatives, full loadout eligibility, broaden Shopping acquisition mapping beyond known TP item map.
- [x] Configurable automatic refresh exclusions by dataset, persisted in settings; manual refresh unaffected. Bounded FIFO shared HTTP admission with cancellation/timeout; CI passed at cdac06a, live validation pending.
- [x] Durable wallet favorites, favorites/zero filters and sort; inventory character, freshness, minimum total quantity and potential transfer filters. CI passed at cdac06a; live validation pending.
- [x] Inventory Items and Wallet search/filter controls use the existing shared SearchToolbar, FilterChoice, FilterInt and removable chips; Guild bank uses SearchField. CI passed at 57d47f9, live validation pending.
- [ ] Wallet categories, armor/weapon subtype and level filters, endpoint deduplication and priority scheduling.
- [x] Independent Vault daily/weekly/special persistence and timestamps, section failure isolation, selective automatic refresh; CI/live validation pending.
- [x] Registry snapshots carry their credential generation; obsolete snapshot permissions cannot acquire a replacement credential.
- [x] Independent achievement name catalog: direct ArenaNet retrieval, SQLite persistence, missing-ID checks, complete-check freshness, explicit full name refresh and no startup name download. Live validation pending.
- [x] Retire single-achievement waypoint catalog and external dataset downloads; preserve unrelated location assistance.
- [ ] Explore contextual achievement objective assistance (like Vault assistance): actionable hints and verified chat links when useful, no forced external page, no guessed fuzzy matches. Discussion/design only; scope and sources require approval.
- [ ] Full independent character/progress dataset freshness; independent Clears section commit; no startup burst.
- [ ] Authenticated single/multi-account tests, installed glyph field verification, and benchmark.

## UI consistency audit — outstanding corrections

Source audit at 57d47f9 (not an in-game visual test). Checked UI items passed Windows CI at 59216bc; live validation pending. Apply the existing UI guide; do not introduce separate per-page UI primitives or alter refresh scheduling as part of UI polish.

- [x] High: unify Inventory's duplicate local account selector with the main account selection model; preserve active-account versus browsing-account semantics, single-account concealment, and explicit All Accounts browsing. Use stable account IDs in dropdowns.
- [x] High: use the existing RefreshUI::Row bottom footer for Items, Wallet and Guild bank, with a reserved scrollable content child, disabled refresh during active work, scoped last-success timestamps and meaningful stale/partial/failure details. Do not present a batch completion as proof all datasets succeeded; retain scope-specific storage diagnostics.
- [x] High: capture/restore Inventory subview, search, filters, sort, target item ID and account context in NavigationUI; use MarkNavigation for explicit destinations and avoid new history entries on filtering. Reveal a target by clearing conflicting filters, preserving compatible account scope when possible.
- [x] Use existing tab dropdown conventions for Inventory subviews where appropriate; share existing menu hover/click behavior and avoid overflowing the primary tab row/account selector at narrow widths.
- [x] Use canonical multi-select facets with independently removable chips for applicable Inventory type/rarity/binding/character choices; preserve single storage refresh scope unless explicitly decoupled. Keep wallet filtering canonical.
- [x] Shared rarity and coin presentation, Items/Guild Bank context menus, Stock navigation via shared skin context menu and consistent price row geometry.
- [x] Common location labels across Inventory, Compare and equipment location menus; global account selector moved into navigation header to avoid tab overlay.
- [ ] Verify large-list scrolling, narrow layout and Shopping row geometry in game.
- [x] Account options canonical password inputs, disabled actions, destructive unlink styling, grouped refresh preferences.
- [x] Shared typed account validation/status feedback; preserve manual refresh independent of master settings.
- [ ] Verify account options and master/child state in game.
- [x] Compare location hints: use stable item IDs (not array position), concise location labels and shared Find in Inventory action; keep acquisition hints distinct from semantic matches.
- [ ] Verify no-account/not-retrieved/empty/filter-no-match/permission-denied/failed states, conditional account labels, narrow window layouts, multiple font sizes, keyboard focus, clipboard and Back/Forward in game.

## Remaining architecture and feature goals

See [Architecture](architecture.md) for implemented storage, account identity, transport and freshness rules. Keep a single native API service and independent dataset policies. No third-party compatibility bridge without a separately approved design.

- Complete per-dataset scheduling, endpoint deduplication, rate limiting/backoff, priorities, cancellation and request correlation. Avoid all-account startup bursts. Preserve verified snapshots on failed or incomplete responses.
- Extend the relational inventory model and immutable search projections only where needed. Distinguish empty, failed, denied, unknown and stale scopes. Keep credentials and user settings outside the rebuildable cache.
- Extend item filters with armor weight, weapon subtype, level and tradeability where supported; add wallet categories. Preserve the shared search-first behavior, stable sorting, explicit refresh scope and navigation.
- Broaden acquisition aids: semantic alternatives, full loadout eligibility, shopping mappings, legendary material/currency requirements and unused ascended gear/upgrade discovery. Never infer usability from ownership alone.
- Keep account-independent library records separate from account-scoped character and inventory data. Fast Load requires verified same-account ownership and must reject the active character.

## Gathering tools and glyphs

- Include equipped mining picks, logging axes, harvesting sickles, unequipped tools and loose glyphs in searchable inventory; identify character, slot, binding, and charges/quantity when the API provides them. Gathering tools are character-wide, not equipment-tab components; keep them out of Equipment Sets and avoid counting them once per tab. Reuse character equipment records where feasible.
- Inspect authenticated character/equipment responses using an explicit schema version and fixtures. Gathering tool IDs are exposed, but installed glyph/socket data has been reported missing; verify actual responses before promising coverage. Distinguish unknown glyph from verified empty, and never infer installed glyphs from tool skins or historical bundle defaults. Track loose glyphs and separately distinguish homestead glyph unlocks from physical inventory.
- Include tool/glyph item-location search and navigation to the owning character. Inventory-aware Compare must distinguish physical ownership, availability, and incomplete data.
- References: [ArenaNet equipment tab announcement](https://en-forum.guildwars2.com/topic/65255-api-updates-december-18-2019/), [GW2 character equipment API](https://wiki.guildwars2.com/wiki/API:2/characters/:id/equipment), [API endpoint index](https://wiki.guildwars2.com/wiki/API).

## Character loading and location

Implemented on the experimental branch after user-led live verification of the
standalone test. See [architecture](architecture.md) and [UI components](ui-components.md).
The memory reader and switch are implemented locally; do not reintroduce
the temporary diagnostics tab. The integrated path still needs in-game testing.
Locations belong only in row tooltips. Do not override API character metadata
or require it to match live memory plausibility checks.

## Independent release

Track release-specific identity, privacy, distribution and validation gates in the [independent release checklist](release-checklist.md). Do not duplicate them in this implementation tracker.

## Acceptance

Unit/regression coverage for account switching, late responses, permission failures, cancellation/unload, rate limits, complete-empty vs failed/partial scoped snapshots, guild dedup, legendary counts, filtering/sorting and stale search generation, cache reset and non-destructive migration. Validate native operation in game including multiple accounts and per-dataset refresh policies; preserve all existing CI gates and produce a verified Windows x64 UCRT DLL. Benchmark startup, refresh request counts, database size, search latency, and DLL size against the stabilized baseline; no performance or size target is claimed as measured today.


### Public catalog modernization
- First pass: shared equipment discovery, SQLite public upgrade and Vault definitions, seven-day refresh and Nexus manual actions.
- Remaining: verify live catalog completeness and duplicate identity handling, remove legacy static upgrade identities after a validated replacement, finish skills/traits/profession and specialization per-record freshness, and implement Wiki-only schedules separately.
- Second pass: public pet/legend SQLite catalogs, exact schema-versioned legend codes, seven-day complete checks for item stats and dyes, Nexus manual refresh, legacy effect text removed. Preserve old saved values; live validation outstanding.

- Third pass: split seven-day item/skin discovery from 30-day upgrade, pet, legend, dye and stat definition refresh; shared incremental item mapping, verified skin checks, manual discovery actions. In-game full discovery and lifecycle validation still outstanding.

- Wiki resource policy agreed: all static Wiki reference datasets (including skin acquisition and currency/vendor metadata where bounded bulk retrieval is feasible) now target a 24-hour build-time refresh; active event data remains a six-hour runtime cache. Implementation pending: verify exact bulk sources, normalized schemas, attribution, last-known-good persistence and fallback; wire consumers and offline tests. Never perform a full per-skin Wiki crawl during ordinary builds.

- Wiki rotation bootstrap: normalized seed, daily-eligible build refresh, API/HTML fallback, generated C++ data and bounded initial event/skin/currency probes. Pending: verify real first-run payloads, promote validated snapshots, event phase matching, weekly raid/festival data, specialization descriptions and bounded skin/currency acquisition integration. Existing runtime Wiki retrieval stays unchanged until consumers are ready.

- Debian bootstrap: verified raid/fractal template source and 27 exact Vault event timer phases; daily-eligible build snapshots wired to consumers. Remaining Wiki: underlying weekly effect templates, festival mappings, specialization text, bounded skin/currency sources; GitHub live refresh still blocked by HTTP 403, use reviewed offline seed.

- Hybrid Wiki event timer resources: v18 SQLite baseline merge, validated runtime parser and newest snapshot, 30-day automatic runtime checks, manual Nexus action and daily build eligibility. Pending: live Windows runtime verification, remaining Wiki datasets and durable promotion of newer CI snapshots; complex rotation templates remain build-only.

- Wiki attribution: current raid/fractal/event timer source credits and rights boundaries documented in third-party notices; new sources require dataset-specific provenance and reuse review. Specialization prose and bulk acquisition remain gated; final distribution rights/notice review is tracked in the release checklist.

- Final Wiki freshness: active special-event retry/recheck and v19 on-demand skin acquisition success/attempt tracking implemented; live smoke tests pending. Emboldened, Call of the Mists, festival mappings, specialization prose and bulk skin/currency catalog research deferred unless a practical need arises. Manual promotion of reviewed build snapshots remains acceptable.

## Unified freshness and request engine — approved

The goal is freshness based on change likelihood rather than data age alone. Use common policy decisions, one owner per network dataset, account GUID scoped data, separate successful verification/attempt timestamps, bounded retries, request coalescing and local-only diagnostics. Preserve manual priority: explicit Refresh bypasses ordinary TTL, staggering and failure cooldown, but not permissions, cancellation, in-flight coalescing or host Retry-After. Stagger automatic recurring work with stable small offsets, never force all datasets/accounts at once. Preserve current rollover delays (30/60/120/300 seconds), reset validation and previous snapshots. No startup full account sweep.

- [x] Foundation: shared RefreshEngine policy helpers, configurable Nexus Objective tracking (default 15 minutes), 30-minute general browsing/Special, deterministic recurring objective offsets, shared account achievement broker for Achievements/Clears, manual bypass of ordinary broker cooldown, pre-dispatch dedup for public metadata and achievement definition async requests, Vault periodic checks and section attempt cooldown, independent in-memory public season metadata TTL, Armory 30-minute TTL. Offline and in-game verification tracked separately.
- [ ] Character activity: persist account GUID + character last login, last seen (including logout), per-scope success/attempt and specialization-change invalidation. Do not fetch just for logging in. Active character eligible after 10 minutes of activity since login/last successful pull, only when the relevant view needs the data; specialization changes remain a valid debounced early refresh signal. Track activity locally even while hidden, but do not dispatch character API requests without visible demand. Offline snapshots stay fresh indefinitely if observed activity is covered by a verified snapshot; do not substitute raw snapshot age for known activity. Otherwise reconcile once after a minimum 10-minute grace measured from the last observed active time (including confirmed logout); reset the grace on renewed activity. Apply to core/builds/equipment/bags. Maintain a separate persisted reconciled-through activity watermark: a successful manual pull during the grace may display newer data but must not clear pending logout reconciliation. Only a verified response from a request started after the grace, with no intervening activity, can advance that watermark for the offline session. Failed refresh retains data and the pending reconciliation. Investigate ArenaNet last_modified as an additional signal, not replacement for local observation. Use same predicate in UI and availability computations; old cache with no locally observed activity is assumed unchanged, not verified against external play. Confirm correct account and no lost dirty state during character switching/shutdown.
- [x] Initial character inventory bags/equipment activity policy and shared physical equipment response; preserve full character tabs separately. Verify live schema and bindings before release. Bank/shared/materials/Armory: consistent 30-minute freshness while active, account-activity reconciliation while inactive; delivery can change offline and is demand-based. Roster discovery 24h or explicit action. Wallet/guild follow independent visible scopes; no all-account automatic sweep.
- [ ] Complete objective centralization: durable verified raw account snapshot shared by Achievements/Clears, remove obsolete category/pinned timers, honor per-dataset exclusions; share definitions with Clears. Daily/weekly category reuse now implemented for existing valid period data, including full manual refresh and recovery of missing weekly definitions; independent section persistence and live regression still pending. Preserve weekly stuck/reset regression cases and independent section commits. Season metadata durable 24h and rollover-aware; manual requests force all relevant sections.
- [ ] Extend common per-ID request broker across synchronous and async consumers, persistent negative/failure cache, batched IDs and existing-item/skill/trait/profession definition ages. Existing item/skin discovery 7d, known upgrades/stats/dyes/pets/legends 30d, other item definitions 30d on demand, skills/traits 7d on demand, professions/specs 30d. Trading Post prices 15m on demand; ownership 15m while visible. Ordinary failure cooldown 15m, explicit manual bypass; preserve dataset-specific Wiki/build/reset backoffs. Prioritize manual and active-objective work over background catalog discovery; stagger with host-wide budget, consider 2 requests/sec ArenaNet after dedup.
- [ ] Central Wiki host rate limiter (initial 1 request/sec shared admission implemented; raid modifier startup fetch eliminated by embedded assets; coalescing and live validation pending), coalesce image/metadata/currency work and move startup currency fetch off render/init thread. Preserve 24h build eligibility, event timer 30d runtime, active event 6h/60m failure, skin acquisition 30d/24h failure, artwork indefinite/7d negative; no new low-priority Wiki datasets.
- [ ] Local API Activity diagnostics in Storage & Cache (initial transport counters, rolling windows, outcomes, endpoint groups, queue and achievement reuse implemented; broader attribution and live validation pending): per endpoint/dataset/reason, attempts, success, failures, 429, cache/dedup hits, rolling counts and peak; never credentials, raw payloads or external telemetry. Compare live behavior with expected network budget and regression fixtures, including manual bypass, account switch, stale propagation, concurrency, reset and restart.

Character propagation evidence: the [GW2 API client endpoint documentation](https://github.com/queicherius/gw2api-client/blob/master/docs/endpoints.md) lists five-minute caches for character core, equipment, inventory and crafting; this is not an end-to-end ArenaNet propagation guarantee. Ten minutes is the agreed minimum grace, subject to in-game measurement. Keep `last_modified` (schema 2019-02-21 or newer) as corroboration, not a substitute for locally observed activity.

- [x] Initial activity integration: account/character local history and per-scope coverage/attempt ledger in config, Nexus gameplay observation, demand-only Characters/Compare and Inventory/Shopping, 10-minute active and offline grace, 30-minute account storage, 24h roster, shared physical equipment endpoint/projection. Offline regressions and in-game validation pending. Remaining: more granular independent character core/build/crafting freshness, complete persisted raw response/metadata broker, robust detection of play outside the addon and verified last_modified handling; live validation of gameplay data link, physical equipment schema, permission edges and request budgets.

- [x] Fixed icon fallback blocked by actual linked resource existence; shared artwork HTTP transport and persistent seven-day failed-download cache; Scribe metadata cleanup. Live validation pending.
- [x] Companion artwork: reviewed eight Revenant legend swap-skill icons and 67 Ranger pet icons embedded with canonical IDs; pet picker uses them. Live validation pending. Core specialization picker artwork remains demand-loaded; decide embedding separately. Specialization backgrounds and vendor currency images remain indefinitely cached on demand.
