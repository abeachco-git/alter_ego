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

## Acceptance

Unit/regression coverage for account switching, late responses, permission failures, cancellation/unload, rate limits, complete-empty vs failed/partial scoped snapshots, guild dedup, legendary counts, filtering/sorting and stale search generation, cache reset and non-destructive migration. Validate native operation in game including multiple accounts and per-dataset refresh policies; preserve all existing CI gates and produce a verified Windows x64 UCRT DLL. Benchmark startup, refresh request counts, database size, search latency, and DLL size against the stabilized baseline; no performance or size target is claimed as measured today.
