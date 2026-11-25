# Alter Ego

A Guild Wars 2 addon for [Raidcore Nexus](https://raidcore.gg/Nexus) that brings your characters, loadouts, inventory, wardrobe and account progress together in one in-game window.

Inspect characters without logging into each one, organize reusable builds and equipment, find items across your account, and track what you still need to complete.

> [!NOTE]
> This branch is an independently developed experimental fork. Features are under active development; automated builds do not replace in-game testing. Keep a backup of your library.

## Screenshots

Updated screenshots will be added before release.

## Features

### Characters, Library and Compare

- **Characters:** Browse characters, equipment and build tabs, gathering tools, fishing and Jade Bot equipment. View Agony Resistance, upgrades, infusions and stats. Sort by locally observed last login, add custom portraits, and use right-click **Load** to switch to another eligible character.
- **Loadout Library:** Save complete loadouts or reusable build, skill and equipment components. Combine selected equipment categories into shared Equipment Sets, reuse them across loadouts, or maintain independent components. Edit shared records or make a separate copy.
- **Fashion:** Save appearances from a selected equipment tab or import Wardrobe Template links. Manage skins and dyes separately from combat loadouts.
- **Compare:** Check a character's retrieved build and equipment against a saved loadout. See differences, missing or unverified information, utility changes and optional exact-item comparisons. Locate missing items in cached inventory.
- **Import and share:** Import Snow Crows and MetaBattle build URLs, supported game chat links and portable library data. Share builds, components and loadouts with other users.

### Inventory and Skinventory

- **Inventory:** Search items across bags, equipped gear, bank, shared inventory, material storage, gathering tools, legendary armory and delivery storage. Filter by location, character, rarity, type and binding; inspect individual locations.
- **Wallet:** Browse balances, pin favorite currencies and hide zero balances.
- **Guild Bank:** Browse available guild storage and coins when your linked account has the required access.
- **Skinventory:** Explore the wardrobe, filter owned or missing skins, and maintain a shopping list with price and eligible owned-stock information.

### Account progress

- **Vault:** Track daily, weekly and special Wizard's Vault objectives.
- **Clears:** Track daily fractals and raid bounties, weekly strikes and raid encounters.
- **Achievements:** Search achievements, pin goals and open a compact tracker.
- **Multi-account support:** Link multiple accounts and browse account-scoped data while keeping a shared, account-independent library.

## Getting started

### Requirements

- [Raidcore Nexus](https://raidcore.gg/Nexus) (API v6).
- A [Guild Wars 2 API key](https://account.guildwars2.com/applications) for account data.

Optional Nexus integrations: Events: Chat for chat-link detection and Events: Alerts for skin unlock and achievement notifications.

### Installation

Copy `AlterEgo.dll` to `<GW2>/addons/AlterEgo.dll` and load it through Nexus.

This is an experimental branch, not a published Nexus catalog release. Use a build intended for this branch; do not load two copies of the addon at once.

### Link your account

Create an API key with these permissions for full functionality:

`account`, `characters`, `inventories`, `builds`, `progression`, `unlocks`, `wallet`, `guilds`, `tradingpost`.

Open **Nexus → Alter Ego options**, enter the key and validate the account. Additional accounts can be linked there. API keys are stored in Windows Credential Manager, never in library exports.

Automatic refresh is opt-in and configurable by dataset. Manual refresh is always available for linked, authorized data.

## Usage notes

### Save and compare

In Characters, select a character and choose **Save**. Choose what to save, name it and select the applicable build or equipment tabs. Open the Library to manage saved records, or Compare to check a loadout against retrieved character data.

Changes to a shared component or Equipment Set can affect every referencing loadout. Use a separate copy for private changes. Equipment Sets may contain only the equipment categories you want to share.

### Finding items

In Inventory, enter at least three non-whitespace characters, or select a Character filter and another item filter to browse. Equipment, Compare and Shopping List can navigate directly to cached item locations.

### Limitations

- Character builds and gear come from the GW2 API and can lag behind in-game changes. Compare does not equip anything.
- Fast Load is an explicit, version-sensitive character switch; loading the active character or a character without verified same-account ownership is blocked.
- Equipped fashion captures may be partial. Use a Wardrobe Template chat link for fuller appearance information. Installed gathering glyphs are shown only when reported by the API.
- Item locations and shopping estimates depend on successful, recent API retrieval. Ownership alone does not establish that an item is transferable or usable.

### Custom portraits

Save a `.png`, `.jpg` or `.jpeg` named exactly like your character in `<GW2>/addons/AlterEgo/portraits/`. Re-select the character to refresh its portrait.

## Data and backups

Local data is stored under `<GW2>/addons/AlterEgo/`.

| Location | Contents |
| --- | --- |
| `library/library.db` | Saved loadouts, components, Equipment Sets and Fashion |
| `config/` | Settings and linked-account metadata |
| `cache/` | Rebuildable account data, reference catalogs and downloaded artwork |
| `portraits/` | Custom character portraits |
| `backups/` | Recovery snapshots and archived data |

Use **Library → Import / Export** to transfer or back up selected collections. Portable ZIP and JSON formats support library interchange, including recognized original build-library exports. Merge preserves existing records; replacement is explicit and creates a recovery snapshot.

Keep pre-migration backups. Cache rebuilds are available in Nexus options under **Storage & Cache** and run on restart; they do not erase the saved library. API keys and rebuildable caches are not included in library exports.

For details on stored data, external services and optional sharing, see [Privacy](PRIVACY.md).

## Development

GitHub Actions builds and verifies the Windows x64 UCRT DLL. Local cross-build on Debian 13:

```bash
sudo apt-get install cmake make g++-mingw-w64-ucrt64 gcc-mingw-w64-ucrt64 binutils-mingw-w64-ucrt64
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-mcrtdll=ucrtbase -DCMAKE_C_FLAGS=-mcrtdll=ucrtbase
cmake --build build --parallel 2
```

Output: `build/AlterEgo.dll`. Verify the UCRT import with `objdump -p build/AlterEgo.dll`; it must not import `msvcrt.dll`.

[Contributing](CONTRIBUTING.md) · [Changelog](CHANGELOG.md) · [Security](SECURITY.md).

Developer references: [Agent instructions](AGENTS.md) · [Architecture](docs/architecture.md) · [UI conventions](docs/ui-components.md) · [Regression testing](docs/regression-testing.md) · [Roadmap](docs/roadmap.md) · [Development status](docs/development-status.md) · [AE2 format](docs/shared-build-spec.md) · [Release checklist](docs/release-checklist.md) · [Dependency maintenance](docs/dependencies.md).

## Credits

Alter Ego is an independent fork of [PieOrCake's original Alter Ego addon](https://github.com/PieOrCake/alter_ego). Credit to PieOrCake for creating the original addon and its foundational work, including the character/build interface and shared build format. This fork is maintained separately and is not an official upstream release.

Thanks to Raidcore for Nexus, ArenaNet for the Guild Wars 2 API, the [Guild Wars 2 Wiki](https://wiki.guildwars2.com/) community and contributors for public reference data, and the open-source libraries that support the addon. Development is AI-assisted.

## License

Addon code is licensed under the [MIT License](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.txt) for bundled libraries, fonts, game content and Wiki data attribution, including the schedule source pages and applicable Wiki copyright terms. Third-party content is not relicensed under MIT.

This is an unofficial fan project and is not endorsed by ArenaNet, NCSOFT or Raidcore.
