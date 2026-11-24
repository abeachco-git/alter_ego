# Alter Ego architecture

This is the living reference for the experimental composable-loadouts branch, not a rollout plan. See [AGENTS.md](../AGENTS.md) for development rules, [UI components](ui-components.md) for presentation conventions, and [regression testing](regression-testing.md) for validation.

## Runtime and integration

Alter Ego is a Windows x64 UCRT DLL loaded by Raidcore Nexus. Nexus supplies the ImGui 1.80 context and allocator; preserve the ABI. The entry point and shared UI state live in `src/dllmain.cpp`, with dedicated composable, fashion, backup, refresh, and presentation helpers. `src/AddonShared.h` exposes shared UI utilities and the Nexus API handle.

`src/GW2API.h/.cpp` manages character data, retrieved build and equipment tabs, accounts, caches and library integration. Character snapshots are retrieved through Hoard & Seek using the event contract in `include/HoardAndSeekAPI.h`. Public catalog data is retrieved via `src/HttpClient.cpp`. MumbleLink provides identity and specialization observations, not live equipment contents.

**Threading:** Nexus `Events_Raise()` dispatches synchronously on the calling thread. Never hold a mutex across a query event that can invoke its response handler; preserve the pending/retry protocol in the Hoard & Seek interface. Font request callbacks can likewise re-enter synchronously: release the font manager mutex before registration calls and accept updated font pointers when the Nexus atlas is rebuilt.

### Native character loading and location

`GameMemory.h` owns bounded, copied game-roster snapshots and the explicitly
user-initiated character switch. The standalone diagnostic tab was removed
after a successful live roster and off-main-thread switch test. This is an
undocumented, version-sensitive integration, not a supported Nexus contract.
The switch is gated to the inspected executable, exact function bytes, valid
context/gameplay and fresh roster; a request remains pending until the
target identity is observed or a 60-second gameplay timeout expires. No
automatic retry, game patch or thread injection. Memory metadata is validated
for plausible ranges and unique names, never required to equal stale API
metadata. The current live roster and any known account conflict govern
eligibility. Map IDs are resolved from public API data in owned worker batches
with session caching; only the row tooltip presents location. Neither map
lookup nor hover performs synchronous HTTP or SQL. On an unsupported game
build, loading fails closed; roster location availability is independent.

## Development safety and ownership

These are review rules for changes, not a claim that every existing path has already been audited.

- Keep Nexus/ImGui calls on their documented owning thread. Inspect callback and worker ownership before changing asynchronous code; callbacks must not access destroyed state or publish stale results to a different character/account. Preserve the established shutdown ordering, unsubscribe/release registrations and stop or join owned workers before unloading their code or state. Avoid holding locks while calling re-entrant external APIs.
- Treat refresh policy, cache ownership, invalidation and in-flight deduplication as domain responsibilities. Reuse existing requests and cached snapshots; ordinary drawing, sorting, searching and filtering must not initiate new requests. Preserve approved explicit, scheduled and lazy fetch paths rather than accidentally disabling legitimate lazy loads. Do not alter refresh lifecycles without specific approval.
- Distinguish absent, incomplete, stale and verified data. Failed or partial requests must not silently replace good snapshots or be presented as complete. Use stable identifiers rather than display labels for stored identity and completion matching.
- Keep domain operations independent from presentation when practical. Reuse existing owners for persistence, refresh, catalog matching and comparison. For new substantial functionality, prefer a cohesive module with pure testable helpers where appropriate; avoid mechanical file splitting or a broad rewrite of dllmain.cpp. Do not change unrelated architecture merely to follow this preference.
- Prefer scoped diagnostics with enough context to investigate failures without exposing API keys or private account data. Remove temporary debugging scaffolding after diagnosis, while retaining actionable error handling.

## Major modules

| Module | Responsibility |
| --- | --- |
| `Clears.cpp` and reset/rotation helpers | Wizard's Vault, raid and fractal tracking |
| `ChatLink.cpp` | GW2 build/wardrobe links and AE2 interchange |
| `ComposableUI.h`, `ComposableNativeEditor.h` | Loadout and component browsing, editing and assignment |
| `FashionLibrary.h`, `FashionUI.h` | Saved appearance and wardrobe interchange |
| `SkinCache.cpp`, `OwnedSkins.cpp`, `Commerce.cpp`, `WikiImage.cpp` | Skin catalog, unlock status, prices and artwork |
| `IconManager.cpp`, `FontManager.cpp` | Nexus textures and font atlas integration |
| `LibraryBackup.h`, `LibraryBackupUI.h` | Library ZIP/JSON export and restore |

## Vocabulary and components

A Loadout is an ordered composition of reusable component records, with an optional Fashion ID referencing the existing Fashion library.

Nine component categories:

| Component | Fields | Semantic comparison |
| --- | --- | --- |
| base_build | Profession, traits, specialization, pets, legends, Build Template weapon picks | GW2 Build Template meaning |
| skills | Heal, three utility slots, elite, aquatic skills | Utilities are unordered for matching but slot order is preserved for export |
| weapons | Physical weapon sets, weapon type, item ID, stats | Compare by weapon set |
| armor | Armor slots, stats, item IDs | Compare base stats by slot |
| trinkets | Rings, amulet, backpack, accessories and unknown future slots | Compare base stats by slot |
| runes | Global rune, per-slot runes and IDs | Aggregate counts of effects, not placement |
| sigils | Physical sigil data from weapon slots | Aggregate per active weapon set, not per hand |
| infusions | Installed IDs/locations, aggregate requirements, AR/stat totals | Aggregate totals/requirements, not slot |
| relic | Relic identity and effect | Independent relic comparison |

Equipment remains a logical *view* of its components, never a second monolithic saved object. Build is the combination of base_build and skills. Named Equipment Sets own any nonempty subset of seven equipment categories. A linked Loadout stores one set ID and direct references for every category not included in the set, plus Base Build and Skills. A category has exactly one owner when present. Removing membership transfers its existing reference to each linked Loadout; adding membership replaces direct references after confirmation. EffectiveRefs is the canonical expansion used by Resolve and projection invalidation. Set membership edits propagate to linked Loadouts. Detach transfers existing references without duplicating payloads; Copy & Edit Set creates a separate named set with the same component references. Shared component payload edits still propagate to their owners. A referenced set cannot be deleted.

## Storage invariants and native repository

- Durable library: `library/library.db`, native SQLite schema version 4. Fresh schema only; archive experimental SQLite versions 1–3 before migrating the original v2 library. Foreign keys and triggers prevent mixed direct/linked equipment ownership. No monolithic disk mirror.
- Nine independent categories, stable category-scoped IDs and ordered loadouts with optional Fashion foreign keys.
- Typed relational rows store known fields. Child tables retain physical equipment slots, utility and infusion ordering, exact IDs, mode associations, skins and dyes. Nullable columns and presence flags distinguish absent and empty; JSON extensions preserve unknown external fields.
- Nonunique indexed fingerprints shortlist exact candidates. Verify complete normalized content before reuse; functional Compare rules are not storage identity.
- Ordinary edits reuse matching components, update unshared records, and copy shared records privately. Explicit shared edits and deliberate independent copies remain available. Never automatically remove unused components.
- `SavedBuild` is a transient presentation/editor projection, not authoritative saved data. Cancel never commits a draft; unaffected resolved records can be reused after commands.

`Database.h` provides owning RAII connections, prepared statements, bound values, rollback transactions, application identity and schema versioning, integrity/foreign-key checks and online backup. Durable connections use DELETE journaling, FULL sync and a bounded 1.5-second busy timeout. One connection and its statements belong to one thread; never invoke external callbacks while a transaction is open.

`NativeLibrary.cpp`, `NativeLibrarySchema.h`, and `NativeLibraryCodec.h` own relational queries and conversions. Repository writes are atomic, revision-checked and limited to changed records. `NativeLibraryStore` serializes commands and publishes committed snapshots; no SQL belongs in rendering. `NativeLibraryEditing.h` provides field-level draft overlays, exact candidate indexing, shared-use checks and private edits. `ComposableSemantics.h` remains the separate functional Compare engine.

Startup stages and verifies migration of legacy build and Fashion files, reopens the candidate database, archives byte-verified originals under `backups/migration/`, then publishes without replacing an existing database. Existing initialized databases always win, even when empty; corrupt or foreign databases fail closed. Archive/remove experimental v3 files before migrating a pre-composable library. Do not migrate the v3 graph or maintain reverse synchronization.

`NativeLibraryPortable.h` and `LibraryBackup.h` export versioned portable JSON and ZIP archives; complete graphs include unused components and linked Fashion dependencies. Legacy monolithic JSON remains an import boundary only. Restore previews selections, protects references, creates a recovery ZIP and a verified native online backup under `backups/recovery/`, then commits changes. Keep full database backup/recovery distinct from portable interchange; the native copy is for recovery and the ZIP is selective.

### Storage paths and cache ownership

`StoragePaths.h` defines the shared directory layout. The four settings/session/UI JSON files live in `config/`; pinned achievement state and login timestamps live in `library/`. Existing destination files win. Valid root-level JSON is byte-verified, archived under `backups/migration/`, staged, then published without replacing a destination. Failed or malformed migrations retain the original path. Portable ZIP entry names remain unchanged for compatibility; backup reads and restore writes resolve the same canonical paths.

`cache/cache.db` uses independent application identity and schema versioning. The achievement progress repository owns typed account snapshots, progress records and completed bit rows. Full account names are keys; legacy slug files are imported only when their embedded identity matches. Transactions replace one account at a time, reject older fetch timestamps and retain the previous committed snapshot on failure. SQL runs at existing persistence boundaries, not on every render frame. Connections are short-lived and serialized; current journal settings remain DELETE/FULL pending concurrency measurements. A corrupt or foreign cache is reported rather than silently overwritten; close the addon before deleting the rebuildable cache DB.

Cache schema v2 also stores achievement definitions, ordered bits/flags/prerequisites, groups, categories and ordered membership, search names, waypoints and hero challenge metadata. Reference freshness is dataset-scoped. The established UI projections are reconstructed in bulk; JSON is a transient codec and legacy import boundary only. Cache schema v3 adds shared skin metadata, subtype/weight, restrictions, flags, ordered dye materials and exact-account ownership snapshots. Fashion and Skinventory keep separate in-memory projections and completeness markers but write the same authoritative relational skin records; narrower responses cannot remove the other projection's fields. Legacy skin and ownership files are read only at migration. Cache schema v4 stores the Skinventory item map, many-to-many candidate relationships, vendor costs, normalized item price observations and per-skin verified cheapest-choice references. Failed refreshes preserve previous prices; TTL still governs retries. The current fetch engine persists selected winners rather than every candidate observation. Other public reference/account snapshots remain JSON under `cache/`, with textures and wiki images moved to `cache/textures/` and `cache/wiki_images/`. Legacy rebuildable files move only when a canonical destination is absent. Further relational public metadata migration and cache diagnostics remain outstanding. User pins, preferences, manual progress and saved library records must survive cache deletion. Keep textures, fonts and portraits as ordinary files; do not store large JSON documents as opaque SQLite payloads. Vendored SQLite update procedure: [lib/sqlite/README.md](../lib/sqlite/README.md).

## Presentation and resources

The bundled Inter font and its OFL notice are embedded Windows resources. Custom fonts use the Nexus file registration API. The default font path leaves the ambient Nexus font unchanged. Preserve exact stored fields when presenting or editing incomplete imported data; Cancel must not persist edits.

Fixed profession, specialization, crafting and status icons are embedded during compilation. Item, skin and skill artwork uses a persistent runtime cache. Do not reintroduce removed raid/strike banner artwork unless explicitly requested.

The three files in `data/` provide achievement names, achievement waypoints and hero challenge locations. This branch currently retrieves those datasets from the upstream project's raw GitHub URLs; retaining the local copies and generators supports maintenance and upstream compatibility. See [tool inventory](../tools/README.md).

AE2 interchange is separate from the internal native data model: see [shared build specification](shared-build-spec.md) and the vendored JavaScript reference implementation in `lib/alter-ego-build/`.

## Release precautions

Keep the experimental branch separate from main until explicitly approved. Before merging, validate real-library migration, restart, component editing and comparison, imports, Fashion, ZIP restore and normal in-game behavior. CI compilation and unit tests do not replace in-game validation.

### Explicit component consolidation

The Components detail pane offers exact duplicate consolidation into the selected survivor, with affected Loadout names and confirmation. The native transaction revalidates equality, revision and skill profession compatibility before rewriting references and deleting the redundant record. Independent duplicates remain valid until explicitly consolidated. Usage counts are indexed per library revision; existing editor drafts reject a changed library revision rather than overwriting newly saved data.

Private Copy & Edit offers explicit reuse of an exact compatible component, verified again in the native transaction against the current revision and original reference. Shared edits and intentional duplicates preserve their identity; exact matches are disclosed rather than silently consolidated. Private Base Build editing cannot change the profession of an already referenced Loadout.

Cache schema v5 stores specializations, ordered minor/major trait IDs, traits, skills, ordered profession membership, item stats and ordered attribute names, and dyes with native RGB channels for each material. Only the API's heterogeneous tooltip facts remain per-record JSON. ReferenceMetadataCache owns atomic snapshot replacement, stale-capture rejection, native-first legacy migration and bulk projections; no network policy changes. Item metadata, item names, character/Vault/Clears snapshots and wiki data remain to be evaluated separately.

Cache schema v6 consolidates item details and Commerce item names in reference_items; the complete flag distinguishes a public detail fetch from a name-only record. Commerce owns candidate membership and pricing, not a second name table. Exact name-to-ID aliases remain a separate relational lookup to preserve selected IDs for ambiguous names and historical spellings. Full details take precedence over narrower Commerce names. Type-specific item details remain an API extension field; typed common metadata is authoritative. Item/name JSON is legacy migration input only. SaveItemNameCache clears its dirty flag only after a successful commit of the same in-memory snapshot.

Storage & Cache diagnostics are available in Nexus addon options on explicit inspection. A read-only SQLite connection checks identity, schema, quick integrity and foreign keys, and reports database sizes, reference counts and freshness plus downloaded artwork size. Diagnostics do not create, migrate, repair, delete, or fetch anything. All-cache rebuilding remains a game-closed operation; selective live invalidation requires coordination with in-memory projections and workers.

Native graph merges reuse a colliding ID only when both name and exact payload match; Skills additionally require independently verified matching professions. Conflicts are remapped without consuming another incoming component's original ID. Unused independent components remain intact.

Aggregate Loadout edits enforce the same profession guard as private and shared Base Build edits: create a new Loadout to change profession. Legacy migration never deduplicates Skills with missing profession context, even if their payload is identical.

Cache schema v7 moves the character projection to typed character, crafting, equipment, ordered upgrade/infusion/dye, attribute, tab-name, build/skill/spec/pet/legend/weapon and hero-point records. Exact account and character identities distinguish unknown-account legacy rows. Native presence wins over legacy JSON, including empty snapshots. Capture and write are serialized; a full projection is replaced atomically at the existing persistence boundary. This first pass retains aggregate roster reconciliation; incremental per-character SQL updates, account-response ownership and worker lifecycle still require audit. Newly supported pet, legend, weapon and binding fields retain missing-field markers rather than inventing known data. No refresh policy change.

Cache schema v8 replaces sanitized Clears and Wizard's Vault filenames with independently keyed exact-account snapshots, typed section/entry/bit and period/objective rows, and season metadata. Existing reset filtering and request scheduling remain in Clears.cpp. Persist original legacy snapshots before reset filtering; require an embedded matching identity for scoped legacy files and verified single-account context for old unscoped input. Writes are serialized and transactional; older dataset timestamps are rejected. Runtime cache loading rechecks the represented account before publishing. Diagnostics includes both datasets. Per-section source freshness and late callback generation checks remain future work.

### One-shot worker ownership

`BackgroundWorkers.h` owns the joinable one-shot jobs previously detached in GW2API, Clears, Skinventory, Commerce, achievement metadata, relay and website imports. Completed handles are reaped on launch. The lifecycle owner opens the registry during load, closes admission before deregistering callbacks and joins all jobs before persistence and subsystem teardown. Domain busy-state failure handlers run outside the registry lock; cancellation is cooperative and exceptions do not escape the worker entry point. Large aggregate fetches check cancellation at batch boundaries. WinINet read helpers reject truncated responses and use the existing 12-second operation timeout across clients. This is not a hard total network deadline; DNS/proxy resolution and synchronous external callbacks still require runtime validation. Icon and WikiImage retain their existing owned queue workers. Font release is outside the font mutex; callback deregistration semantics and full account-generation validation remain outstanding. No request scheduling, retry cadence or startup policy changes.

Character refresh checks echoed account and endpoint against a single pending request; the H&S protocol has no request nonce, so identical delayed replies remain a limitation. Only explicit verified account rosters authorize pruning. Registry-only fast paths no longer authorize deletion. Character reads use immutable revision-cached shared projections; account and pending-list reads return copies. Critical partial or malformed replies do not replace the previous character; failed optional hero-point queries retain known values. Exact account and character identity governs replacement and crafting reuse. No refresh cadence or schema change.

Completed character refreshes serialize only the exact account/name record once a native snapshot exists. Incremental SQL replaces that character and its children in one transaction, retaining its row ID and order, rejecting older per-character freshness and leaving other records intact. Only the first snapshot retains the aggregate transaction. Verified roster deletion uses a targeted account-scoped transaction, commits before publishing the removal, and retains previous rows on failure. A failed baseline inspection fails closed. No schema version change.

### Startup cache maintenance

The Storage & Cache options schedule an exclusive, fixed-operation request under `config/cache-maintenance-request`. On next startup, before subsystem initialization or background jobs, `CacheMaintenance.h` consumes the request and either quarantines the entire cache directory (including damaged DB and sidecars), archives artwork and failure markers while retaining wiki acquisition metadata, or invalidates one relational dataset group in a transaction after archiving known legacy sources. Public catalogs and Commerce share item identity and are invalidated together. Account progress includes Clears, Vault, owned skins and achievement progress; durable pins are separate. Normal fetch paths rebuild data without changing their cadence. Moved files remain under `backups/cache-quarantine`; partial failure is reported rather than silently deleting or retrying against newly rebuilt data. No durable library, configuration, login history or recovery backup is removed. This is restart-only, not live eviction.

The dedicated IconManager and WikiImage queue threads close admission and notify before addon deregistration; all one-shot jobs and both queues are joined before API teardown. Queue exceptions and cancellation release loading markers without treating shutdown as a confirmed remote failure. Worker audit covered all runtime translation units; external callback quiescence is a separate open workstream.

### External callback admission

`CallbackLifetime.h` provides a shared local admission gate for render/options, input, Nexus events (including H&S and Skinventory) and font callbacks. Close admission before deregistration, then drain admitted bodies before worker join and state teardown. Rejected caller-owned H&S response objects are still destroyed. The gate mutex is never held across callback bodies or host calls, including synchronous reentry. This does not serialize concurrent admitted bodies or prove host deregistration quiescence: Nexus/H&S must not call into unmapped DLL code. Close-on-escape is a registered raw pointer, and texture loads with null callback depend on the host's resource ownership. Unload must not be invoked reentrantly from an admitted callback; the host lifecycle contract and in-game behavior remain to be verified.

Cache schema v9 stores wiki acquisition metadata and skin/currency failure timestamps in separate typed tables. WikiMetadataCache owns native-first legacy migration and transactional snapshots with independent presence markers. WikiImage retains in-memory projections; no render-time SQL was added. Startup artwork reset preserves acquisition metadata and empties only negative-cache records, retaining the authoritative empty marker. Existing image bytes remain ordinary files. No network retry policy changed.

`AccountIdentity.h` defines exact account/character identities and a shared reply matcher. Echoed account names must match; old non-echoing responses are accepted only with verified single-account context. Skinventory distinguishes full API and batch ownership requests, verifies endpoint and captured account epoch before publishing, validates complete requested batch ID sets, and keeps partial batch results separate until completion. The opportunistic fashion unlock map is invalidated when the logged-in account changes. Clears, Vault and achievement replies check the represented account under publication mutexes and cap response array access. H&S v4 supplies account/endpoint or returned IDs, not a request nonce; an identical delayed reply for a later identical request cannot be distinguished. Further typed selection and completion correlation remain open.
