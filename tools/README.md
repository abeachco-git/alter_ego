# Development tools

These utilities are retained for active build use, dataset maintenance, or useful manual reference.

| Tool | Purpose |
| --- | --- |
| `dependency_inventory.py` | Validate vendored dependency inventory, generate CycloneDX and GitHub snapshots, optionally query OSV |
| `generate_embedded_icons.py` | Offline fixed icon resource generation from checked-in assets |
| `update_embedded_icons.py` | Manual acquisition into a new staging directory; review before updating Git assets |
| `verify_embedded_icons.py` | Verify linked DLL resources against the checked-in assets |
| `audit_skill_icons.py` | Manual skill-icon size research workflow |
| `fetch_achievement_names.py` | Refresh achievement name dataset |
| `build_achievement_waypoints.py` | Refresh achievement waypoint dataset |
| `build_hero_challenges.py` | Manual hero point coverage research; no tracked or runtime JSON catalog |
| `gen_ae_icon.py` | Regenerate Quick Access icon arrays from the source `ae.png`; manual tool, not a build step |
| `fetch_item_ids.py`, `scrape_wiki_ids.py`, `wiki_item_ids.json` | Manual item-ID research/reference; not authoritative runtime data or CI dependencies |

Old banner and Janus icon generators and their generated artifacts have been retired. Git history retains them if needed.

## SQLite benchmark

Run `python3 tools/benchmark_sqlite.py --scale 1 --repeats 15 --output sqlite-benchmark.json`. Creates disposable synthetic cache and library databases using current real migration SQL, then measures warm startup projections, account reads, incremental character and library writes, short vs persistent connections, a writer competing with a held reader, and active/closed file sizes. DELETE/FULL is the baseline; WAL/FULL isolates journal behavior and WAL/NORMAL is a separate durability tradeoff. No user paths are accepted. The Windows workflow runs the benchmark on its Linux build host and includes the JSON report with the DLL artifact. Python's SQLite version, host and fixture scale are recorded; this is not actual DLL startup, vendored SQLite, Windows storage, or real-game workload. Repeat on a target Windows host and measure native code before changing defaults.
