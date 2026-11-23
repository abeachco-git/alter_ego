# Composable Loadouts v3 — architecture and rollout

Branch: experiment/composable-loadouts. Forked directly from main, independent of the previous prototype.

Status: Experimental, new storage implementation is connected to existing SavedBuild UI compatibility projection. Independent component APIs are implemented; new selectors/editors are reserved for a second pass.

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

src/ComposableSemantics.h exposes rune and infusion aggregate summaries; sigil summaries keyed to weapon set; unordered utility matching. The existing comparison display remains unchanged until the second UI pass.

src/ComposableStore.h adds a protected migration and canonical store:
1. Read the existing build_library.json, verify its complete round-trip.
2. Write a byte-for-byte pre-migration backup at build_library.before_loadouts_v3.json, never overwriting a different existing backup.
3. Atomically create and revalidate loadouts_v3.json.
4. For each subsequent edit, validate and save a new v3 document; keep loadouts_v3.previous.json for recovery and write build_library.json as an atomic compatibility mirror.
5. If the canonical file is corrupt, stop instead of replacing it.
6. When an older DLL modifies the legacy file after the v3 store, reconcile only if it is demonstrably newer; otherwise stop rather than silently discarding information.

src/GW2API.cpp keeps existing SavedBuild UI behavior through a resolved v3 projection and converts existing save actions back to independent components. Public methods allow querying, assigning, copying, shared editing and optional Fashion association.

src/LibraryBackup.h exports complete ZIP archives with both v3 and legacy build JSON. Standalone Build JSON intentionally remains readable by older Alter Ego builds. Restore validates both versions, merges/overwrites safely, writes both together and creates a rollback archive.

## Regression coverage

- tests/composable_loadouts.cpp: lossless migration, nine groups, Fashion refs, shared edits, copy-on-edit, semantic comparison, corrupt-file refusal, restart persistence and older DLL reconciliation.
- tests/composable_backup.cpp: ZIP with both data formats, legacy standalone exports/imports, component ID collision handling, Fashion association, mismatched archive rejection.
- Existing comparator, importer, Fashion, Backup and independent functionality tests remain enabled in CI.
- GitHub workflow compiles the Windows x64 UCRT DLL on this branch.

## Second UI pass

- Browse and select independent components while retaining familiar UI layout.
- Edit shared, copy privately, assign existing, and optionally choose Fashion.
- Show semantic totals for runes, sigils and infusions, with exact-slot diagnostics available when needed.
- Build and Skills selection; auto-regenerate GW2 chat link when base build or skills change.
- Rework source imports/exports to accept independent components; retain currently working Snow Crows and MetaBattle parsers through compatibility until then.

## Development precautions

This is an experiment. Do not merge into main before testing migration/restart on a real library, in-game comparison and character equipment tabs, sources, Fashion, ZIP restore and the rest of the addon. Main is untouched. Keep a user backup and never assume compiling the DLL constitutes in-game validation.
