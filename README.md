# Alter Ego

A Guild Wars 2 addon for [Raidcore Nexus](https://raidcore.gg/Nexus) that brings your characters, loadouts, inventory, wardrobe and account progress together in one in-game window.

Inspect characters without logging into each one, organize reusable builds and equipment, find items across your account, and track what you still need to complete.

> [!NOTE]
> This branch is an independently developed experimental fork. Features are under active development; automated builds do not replace in-game testing. Keep a backup of your library.

## Screenshots

![Character equipment, saved loadout matching and sharing menu](docs/screenshots/characters-equipment.png)

| Library | Compare |
| --- | --- |
| [![Saved loadouts](docs/screenshots/library-loadouts.png)](docs/screenshots/library-loadouts.png) | [![Loadout comparison](docs/screenshots/compare.png)](docs/screenshots/compare.png) |
| **Inventory** | **Account progress** |
| [![Item locations](docs/screenshots/inventory-item-locations.png)](docs/screenshots/inventory-item-locations.png) | [![Wizard's Vault objectives](docs/screenshots/wizards-vault.png)](docs/screenshots/wizards-vault.png) |

More screenshots:

- **Characters:** [Builds](docs/screenshots/characters-build.png) · [Tools and glyphs](docs/screenshots/characters-tools.png)
- **Library:** [Component editor](docs/screenshots/library-components.png) · [Equipment Sets](docs/screenshots/library-equipment-sets.png) · [Gear customization](docs/screenshots/equipment-customization.png)
- **Inventory and skins:** [Inventory filters](docs/screenshots/inventory-filters.png) · [Skin browser](docs/screenshots/skins-browser.png)
- **Account progress:** [Clears](docs/screenshots/clears.png) · [Achievements](docs/screenshots/achievements.png)

## Features

### Characters, Library and Compare

- **Characters:** Browse your characters' builds, equipment and upgrades without switching characters. Find saved loadout matches, view character portraits and switch to an eligible character using Fast Load.
- **Library:** Organize complete loadouts and reusable build, skill and equipment components. Share Equipment Sets between loadouts, or keep equipment choices independent. Create and edit loadouts directly in the Library.
- **Fashion:** Save appearances from character equipment or Wardrobe Template links, independently of combat loadouts.
- **Compare:** Check a character against a saved loadout, identify differences or missing equipment, and find items in your inventory.
- **Import and share:** Import supported game chat links, Snow Crows and MetaBattle builds, and portable library data. Share builds and reusable components.

### Inventory and Skins

- **Inventory:** Search items and wallet currencies across linked accounts. Find items in character inventories, equipped gear, bank, material storage, shared inventory and legendary armory.
- **Wallet and Guild Bank:** Browse currency balances and accessible guild storage.
- **Skins:** Explore your wardrobe, track missing skins and maintain a shopping list with price estimates and eligible owned-stock information.

### Account progress

- **Wizard's Vault:** Track daily, weekly and special objectives, with assistance for supported objectives.
- **Clears:** Track daily fractals and raid bounties, weekly strikes and raid encounters.
- **Achievements:** Search achievements, pin goals and follow progress in a compact tracker.
- **Multi-account support:** Link multiple accounts while keeping a shared, account-independent library.

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

In Characters, use **Save to Library** to capture a loadout, component, Equipment Set or appearance. In the Library, choose **Edit** to change component selections and individual gear or skills. Use Compare to check saved loadouts against retrieved character data.

Changes to a shared component or Equipment Set can affect every referencing loadout. Use a separate copy for private changes. Equipment Sets may contain only the equipment categories you want to share.

### Finding items

In Inventory, search by name or narrow the results with filters. Select Wallet or Currency to browse balances without a search term. Equipment, Compare and Shopping List can also navigate directly to cached item locations.

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
