# Changelog

- Development DLL file version, CI run/attempt and Git commit embedded in Windows properties and Nexus addon options; upstream updater unchanged.

User-facing changes are grouped by release. This file begins with the independent fork; earlier development history remains in Git.

## Unreleased — first independent release

This is a draft for the experimental branch, not a published or fully validated release.

### Added

- Nexus Appearance setting for adjustable Alter Ego window background transparency with reset and persistent preference; text, controls and popup backgrounds retain their theme opacity.

- Reusable loadout components, shared Equipment Sets and independent Fashion records.
- Source refresh for Snow Crows and MetaBattle now stages presence-aware changes for review, preserves omitted local values and supports private or explicitly shared saves.
- Optional Aquatic equipment components, separate underwater skill selections, and mutually exclusive slot-based or aggregate infusion requirements with reversible editor drafts.
- Direct account linking with Windows Credential Manager, account-scoped inventory, wallet and guild bank views.
- Item-location navigation from character equipment, Compare and Skinventory shopping.
- Exact bank tab and slot, character bag and slot, and shared inventory slot labels, with separate stacks in numeric order.
- Inventory Items List or Groups view with browse-first hierarchical account/storage/character groups, shared filters and item details.
- Inventory search includes owned wallet currencies, with shared filters and account-wallet location details.
- Character gathering, fishing and Jade Bot details; explicit right-click character loading.
- Portable library import/export, recovery backups and Storage & Cache maintenance controls.
- Validated Wiki reference schedules for raid bounties, fractals and selected Vault event timers, with daily-eligible build refresh and an embedded/SQLite fallback for event timers. Opted-in runtime event timer refresh is eligible after 30 days.

### Changed

- Protect shared refresh/status footers and build-detection toast backgrounds from excessive transparency; improve neutral footer metadata contrast without changing status semantics or layout.

- Ranger pet and Revenant legend artwork now loads on demand through the same persistent artwork cache as skills, reducing the fixed DLL resources.

- Track character activity locally and refresh character/inventory snapshots on visible demand; 10-minute active interval and post-logout grace, 10-minute active Bank/shared inventory/material storage and 30-minute Legendary Armory.
- Batch complete automatic character refreshes with eligible cached characters from the same account, up to 20 per request; reuse bags and equipment in Inventory and batch missing item definitions. Manual refresh remains targeted or explicitly selected, and roster verification remains separate.
- Logging in no longer postpones already-due Bank, Shared Inventory, Materials or Legendary Armory retrieval. Wardrobe ownership refresh and retry eligibility are account-qualified.

- Start unified freshness scheduling: configurable objective tracking, shared achievement progress, per-section Vault refresh and automatic failure cooldowns, pre-dispatch public metadata deduplication, and 30-minute Legendary Armory freshness.

- Recheck active special events after six hours with a 60-minute failure cooldown and cached fallback; refresh skin acquisition metadata on selection after 30 days independently of cached artwork.

- Retire the single-achievement waypoint catalog and its obsolete controls; preserve Vault assistance and vendor location links.

- Retrieve achievement names directly from ArenaNet into SQLite, reuse cached definitions, refresh missing names incrementally and support explicit full revalidation without a hosted index.

- Remove hero point tracking, its unused requests, dormant UI and reference tooling. Cache schema 14 removes only the obsolete hero point tables.

- Disabled external one-time-code relay sharing and imports by default; local sharing remains available.

- Native SQLite storage for durable library data and rebuildable caches.
- Consistent multi-word and typo-tolerant searches, exact numeric inventory identifiers, persistent group expansion, Library selection reveal, and standard Wallet and Guild Bank catalog rows with sorting and balances.
- More consistent filters, resizable panels, navigation and refresh feedback.
- Account-aware data retrieval, per-dataset refresh preferences and independent Vault period storage.
- Independent project documentation, MIT license and third-party notices, including Wiki contributor credit and reference dataset source attribution.

### Known limitations

- GW2 API snapshots may lag behind the game; some fashion and gathering glyph details may be unavailable.
- Character loading relies on version-sensitive memory integration.
- Authenticated multi-account, migration, release packaging and in-game validation remain outstanding.

See the [roadmap](docs/roadmap.md) for internal work and the [release checklist](docs/release-checklist.md) for release gates.

- Correct shared Windows Unicode clipboard writes to use a valid game-process window owner rather than a null owner.

- Restore aquatic equipment in saved Loadouts, simplify Components context menus and slot disclosures, and allow standalone Equipment Set creation.

- Rename visible Skins and Wizard's Vault tabs; add achievement Copy Name; use host clipboard callbacks; compact Loadout references with inline editing and shared popup fields; improve automatic component naming and utility comparison regression coverage.

- Fix the infusion removal control in Gear Customize and use New Loadout consistently for creation.
