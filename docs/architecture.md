# Alter Ego architecture

This is the living reference for the experimental composable-loadouts branch, not a rollout plan. See [AGENTS.md](../AGENTS.md) for development rules, [UI components](ui-components.md) for presentation conventions, and [regression testing](regression-testing.md) for validation.

## Runtime and integration

Alter Ego is a Windows x64 UCRT DLL loaded by Raidcore Nexus. Nexus supplies the ImGui 1.80 context and allocator; preserve the ABI. The entry point and shared UI state live in `src/dllmain.cpp`, with dedicated composable, fashion, backup, refresh, and presentation helpers. `src/AddonShared.h` exposes shared UI utilities and the Nexus API handle.

`src/GW2API.h/.cpp` manages character data, retrieved build and equipment tabs, accounts, caches and library integration. Character snapshots are retrieved through Hoard & Seek using the event contract in `include/HoardAndSeekAPI.h`. Public catalog data is retrieved via `src/HttpClient.cpp`. MumbleLink provides identity and specialization observations, not live equipment contents.

**Threading:** Nexus `Events_Raise()` dispatches synchronously on the calling thread. Never hold a mutex across a query event that can invoke its response handler; preserve the pending/retry protocol in the Hoard & Seek interface. Font request callbacks can likewise re-enter synchronously: release the font manager mutex before registration calls and accept updated font pointers when the Nexus atlas is rebuilt.

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

## Storage invariants

- Canonical file: loadouts_v3.json, format alter_ego_composable_loadouts, version 3.
- Components: categorized arrays of objects with stable ID, name and data.
- Loadouts: ordered objects containing stable ID, metadata, component reference map and optional fashion_id.
- Unknown legacy root and gear fields are preserved where possible, including through legacy editor updates.
- Migration never automatically deduplicates components or grants shared ownership.
- Users explicitly choose assign existing, copy before edit, or edit a shared component.
- Referential integrity is validated before persistence; unused components may persist without a Loadout.
- Physical placement information is preserved even when semantic comparison intentionally ignores slot positions.
- Missing or partial requirements should report Unverified instead of Match.

## Core implementation

src/ComposableLoadouts.h implements lossless legacy split/reassembly, validation, transactional editing, reference assignment, copy-on-edit, optional Fashion links, collision-safe backup merges and old-UI reconciliation.

src/ComposableSemantics.h exposes rune and infusion aggregate summaries; sigil summaries keyed to weapon set; unordered utility matching. The Compare UI groups results by component and distinguishes incomplete data from verified matches.

src/ComposableStore.h adds a protected migration and canonical store:
1. Read the existing build_library.json, verify its complete round-trip.
2. Write a byte-for-byte pre-migration backup at build_library.before_loadouts_v3.json, never overwriting a different existing backup.
3. Atomically create and revalidate loadouts_v3.json.
4. For each subsequent edit, validate and save a new v3 document; keep loadouts_v3.previous.json for recovery and write build_library.json as an atomic compatibility mirror.
5. If the canonical file is corrupt, stop instead of replacing it.
6. When an older DLL modifies the legacy file after the v3 store, reconcile only if it is demonstrably newer; otherwise stop rather than silently discarding information.

src/GW2API.cpp keeps existing SavedBuild UI behavior through a resolved v3 projection and converts existing save actions back to independent components. Public methods allow querying, assigning, copying, shared editing and optional Fashion association.

src/LibraryBackup.h exports complete ZIP archives with both v3 and legacy build JSON. Standalone Build JSON intentionally remains readable by older Alter Ego builds. Restore validates both versions, merges/overwrites safely, writes both together and creates a rollback archive.

## Presentation and resources

The bundled Inter font and its OFL notice are embedded Windows resources. Custom fonts use the Nexus file registration API. The default font path leaves the ambient Nexus font unchanged. Preserve exact stored fields when presenting or editing incomplete imported data; Cancel must not persist edits.

Fixed profession, specialization, crafting and status icons are embedded during compilation. Item, skin and skill artwork uses a persistent runtime cache. Do not reintroduce removed raid/strike banner artwork unless explicitly requested.

The three files in `data/` provide achievement names, achievement waypoints and hero challenge locations. This branch currently retrieves those datasets from the upstream project's raw GitHub URLs; retaining the local copies and generators supports maintenance and upstream compatibility. See [tool inventory](../tools/README.md).

AE2 interchange is separate from the internal v3 data model: see [shared build specification](shared-build-spec.md) and the vendored JavaScript reference implementation in `lib/alter-ego-build/`.

## Release precautions

Keep the experimental branch separate from main until explicitly approved. Before merging, validate real-library migration, restart, component editing and comparison, imports, Fashion, ZIP restore and normal in-game behavior. CI compilation and unit tests do not replace in-game validation.
