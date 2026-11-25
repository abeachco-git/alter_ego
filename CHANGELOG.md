# Changelog

User-facing changes are grouped by release. This file begins with the independent fork; earlier development history remains in Git.

## Unreleased — first independent release

This is a draft for the experimental branch, not a published or fully validated release.

### Added

- Reusable loadout components, shared Equipment Sets and independent Fashion records.
- Direct account linking with Windows Credential Manager, account-scoped inventory, wallet and guild bank views.
- Item-location navigation from character equipment, Compare and Skinventory shopping.
- Character gathering, fishing and Jade Bot details; explicit right-click character loading.
- Portable library import/export, recovery backups and Storage & Cache maintenance controls.
- Validated Wiki reference schedules for raid bounties, fractals and selected Vault event timers, with daily-eligible build refresh and an embedded/SQLite fallback for event timers. Opted-in runtime event timer refresh is eligible after 30 days.

### Changed

- Recheck active special events after six hours with a 60-minute failure cooldown and cached fallback; refresh skin acquisition metadata on selection after 30 days independently of cached artwork.

- Retire the single-achievement waypoint catalog and its obsolete controls; preserve Vault assistance and vendor location links.

- Retrieve achievement names directly from ArenaNet into SQLite, reuse cached definitions, refresh missing names incrementally and support explicit full revalidation without a hosted index.

- Remove hero point tracking, its unused requests, dormant UI and reference tooling. Cache schema 14 removes only the obsolete hero point tables.

- Disabled external one-time-code relay sharing and imports by default; local sharing remains available.

- Native SQLite storage for durable library data and rebuildable caches.
- More consistent searches, filters, resizable panels, navigation and refresh feedback.
- Account-aware data retrieval, per-dataset refresh preferences and independent Vault period storage.
- Independent project documentation, MIT license and third-party notices, including Wiki contributor credit and reference dataset source attribution.

### Known limitations

- GW2 API snapshots may lag behind the game; some fashion and gathering glyph details may be unavailable.
- Character loading relies on version-sensitive memory integration.
- Authenticated multi-account, migration, release packaging and in-game validation remain outstanding.

See the [roadmap](docs/roadmap.md) for internal work and the [release checklist](docs/release-checklist.md) for release gates.
