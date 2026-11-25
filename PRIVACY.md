# Privacy

Alter Ego runs locally inside Guild Wars 2 through Raidcore Nexus. Its account features use the ArenaNet API. This describes the current experimental branch, not the privacy practices of Nexus, ArenaNet or external websites.

## Credentials and local data

- API keys are entered explicitly in Nexus addon options and stored in Windows Credential Manager. Authenticated API requests send the key as an HTTPS authorization header to `api.guildwars2.com`. The account registry stores account identifiers, display names and granted permissions separately.
- The local addon folder contains saved loadouts, Fashion, settings, login history, account and character metadata, retrieved equipment and inventory, wallet, guild data, unlocks, progression, and cached reference material. Local SQLite databases and exports are not application-level encrypted.
- The addon reads local game identity and specialization information and uses a version-sensitive game-memory integration for character roster/location and explicit Fast Load. This is not a remote game-memory service.
- No dedicated analytics or automatic crash-report upload was identified in the reviewed addon request paths. Nexus, the game, Windows and remote services operate independently and may have their own diagnostics or logging.

## Network services

| Service | Purpose |
| --- | --- |
| ArenaNet API and render service | Key validation, account data, item metadata, prices and artwork |
| Guild Wars 2 Wiki and image hosts | Public reference information, search and artwork |
| GitHub raw content | Achievement names and waypoints; currently hosted in the original project's repository |
| Snow Crows and MetaBattle | Explicit build URL imports and source refreshes |

The legacy one-time build-sharing relay is disabled in this build. Its import and upload actions are unavailable; the addon does not contact that service for sharing. Clipboard build codes and portable library exports work locally. If a future build restores the relay, its privacy implications must be reviewed and disclosed.

Remote services may receive normal request metadata such as IP addresses and requested URLs or identifiers. Opening wiki or source links in your browser is subject to that site's policies.

## Your controls

Automatic API refresh is opt-in and configurable by dataset; manual retrieval is available independently. Unlinking an account removes its registry entry and attempts to remove its credential, but does not erase saved library records or all previously cached data. If credential deletion fails, remove the remaining entry in Windows Credential Manager. Revoke keys at [ArenaNet account management](https://account.guildwars2.com/applications) when they are no longer needed.

Library exports exclude API credentials and rebuildable caches. Depending on your selected collections they can include personal build names, notes, character associations, login timestamps, settings and custom portraits or fonts. Treat exported files and recovery backups as private unless you have reviewed their contents.

Use **Storage & Cache** in Nexus options to rebuild disposable data. Rebuilds can quarantine previous files under `backups/`; they are not secure deletion. To remove local data, unload the addon, preserve anything you want to keep, and remove the relevant addon files and remaining credentials. Revocation and deletion are separate actions.

For vulnerability reports, see [Security](SECURITY.md).
