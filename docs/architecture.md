# Alter Ego architecture

This is the living reference for the experimental composable-loadouts branch, not a rollout plan. See [AGENTS.md](../AGENTS.md) for development rules, [UI components](ui-components.md) for presentation conventions, and [regression testing](regression-testing.md) for validation.

## Runtime and integration

Alter Ego is a Windows x64 UCRT DLL loaded by Raidcore Nexus. Nexus supplies the ImGui 1.80 context and allocator; preserve the ABI. The entry point and shared UI state live in `src/dllmain.cpp`, with dedicated composable, fashion, backup, refresh, and presentation helpers. `src/AddonShared.h` exposes shared UI utilities and the Nexus API handle.

`src/GW2API.h/.cpp` manages character data, retrieved build and equipment tabs, accounts, caches and library integration. Character snapshots are retrieved through Hoard & Seek using the event contract in `include/HoardAndSeekAPI.h`. Public catalog data is retrieved via `src/HttpClient.cpp`. MumbleLink provides identity and specialization observations, not live equipment contents.

**Threading:** Nexus `Events_Raise()` dispatches synchronously on the calling thread. Never hold a mutex across a query event that can invoke its response handler; preserve the pending/retry protocol in the Hoard & Seek interface. Font request callbacks can likewise re-enter synchronously: release the font manager mutex before registration calls and accept updated font pointers when the Nexus atlas is rebuilt.

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

Equipment remains a logical *view* of its components, never a second monolithic saved object. Build is the combination of base_build and skills.

## Storage invariants and native repository

- Durable library: `library/library.db`, native SQLite schema version 1. No monolithic disk mirror.
- Nine independent categories, stable category-scoped IDs and ordered loadouts with optional Fashion foreign keys.
- Typed relational rows store known fields. Child tables retain physical equipment slots, utility and infusion ordering, exact IDs, mode associations, skins and dyes. Nullable columns and presence flags distinguish absent and empty; JSON extensions preserve unknown external fields.
- Nonunique indexed fingerprints shortlist exact candidates. Verify complete normalized content before reuse; functional Compare rules are not storage identity.
- Ordinary edits reuse matching components, update unshared records, and copy shared records privately. Explicit shared edits and deliberate independent copies remain available. Never automatically remove unused components.
- `SavedBuild` is a transient presentation/editor projection, not authoritative saved data. Cancel never commits a draft; unaffected resolved records can be reused after commands.

`Database.h` provides owning RAII connections, prepared statements, bound values, rollback transactions, application identity and schema versioning, integrity/foreign-key checks and online backup. Durable connections use DELETE journaling, FULL sync and a bounded 1.5-second busy timeout. One connection and its statements belong to one thread; never invoke external callbacks while a transaction is open.

`NativeLibrary.cpp`, `NativeLibrarySchema.h`, and `NativeLibraryCodec.h` own relational queries and conversions. Repository writes are atomic, revision-checked and limited to changed records. `NativeLibraryStore` serializes commands and publishes committed snapshots; no SQL belongs in rendering. `NativeLibraryEditing.h` provides field-level draft overlays, exact candidate indexing, shared-use checks and private edits. `ComposableSemantics.h` remains the separate functional Compare engine.

Startup stages and verifies migration of legacy build and Fashion files, reopens the candidate database, archives byte-verified originals under `backups/migration/`, then publishes without replacing an existing database. Existing initialized databases always win, even when empty; corrupt or foreign databases fail closed. Archive/remove experimental v3 files before migrating a pre-composable library. Do not migrate the v3 graph or maintain reverse synchronization.

`NativeLibraryPortable.h` and `LibraryBackup.h` export versioned portable JSON and ZIP archives; complete graphs include unused components and linked Fashion dependencies. Legacy monolithic JSON remains an import boundary only. Restore previews selections, protects references, creates a recovery ZIP and a verified native online backup under `backups/recovery/`, then commits changes. Keep full database backup/recovery distinct from portable interchange; the native copy is for recovery and the ZIP is selective.

### Future cache migrations

Keep rebuildable catalog, ownership snapshots, commerce and item/skill/stat metadata in a separate `cache/cache.db` when migrated. No empty cache DB is created. User pins, preferences, manual progress and saved library records must survive cache deletion. Keep textures, fonts and portraits as files and small settings/session JSON as appropriate. Each future migration needs a typed schema, validation, ownership, versioning and tests, not a JSON blob replacement. Vendored SQLite version and update procedure: [lib/sqlite/README.md](../lib/sqlite/README.md).

## Presentation and resources

The bundled Inter font and its OFL notice are embedded Windows resources. Custom fonts use the Nexus file registration API. The default font path leaves the ambient Nexus font unchanged. Preserve exact stored fields when presenting or editing incomplete imported data; Cancel must not persist edits.

Fixed profession, specialization, crafting and status icons are embedded during compilation. Item, skin and skill artwork uses a persistent runtime cache. Do not reintroduce removed raid/strike banner artwork unless explicitly requested.

The three files in `data/` provide achievement names, achievement waypoints and hero challenge locations. This branch currently retrieves those datasets from the upstream project's raw GitHub URLs; retaining the local copies and generators supports maintenance and upstream compatibility. See [tool inventory](../tools/README.md).

AE2 interchange is separate from the internal native data model: see [shared build specification](shared-build-spec.md) and the vendored JavaScript reference implementation in `lib/alter-ego-build/`.

## Release precautions

Keep the experimental branch separate from main until explicitly approved. Before merging, validate real-library migration, restart, component editing and comparison, imports, Fashion, ZIP restore and normal in-game behavior. CI compilation and unit tests do not replace in-game validation.
