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

### Changed

- Disabled external one-time-code relay sharing and imports by default; local sharing remains available.

- Native SQLite storage for durable library data and rebuildable caches.
- More consistent searches, filters, resizable panels, navigation and refresh feedback.
- Account-aware data retrieval, per-dataset refresh preferences and independent Vault period storage.
- Independent project documentation, MIT license and third-party notices.

### Known limitations

- GW2 API snapshots may lag behind the game; some fashion and gathering glyph details may be unavailable.
- Character loading relies on version-sensitive memory integration.
- Authenticated multi-account, migration, release packaging and in-game validation remain outstanding.

See the [roadmap](docs/roadmap.md) for internal work and the [release checklist](docs/release-checklist.md) for release gates.
