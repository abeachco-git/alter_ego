# Development tools

These utilities are retained for active build use, dataset maintenance, or useful manual reference.

| Tool | Purpose |
| --- | --- |
| `dependency_inventory.py` | Validate vendored dependency inventory, generate CycloneDX and GitHub snapshots, optionally query OSV |
| `generate_embedded_icons.py` | Offline fixed icon resource generation from checked-in assets |
| `update_embedded_icons.py` | Manual acquisition into a new staging directory; review before updating Git assets |
| `verify_embedded_icons.py` | Verify linked DLL resources against the checked-in assets |
| `audit_skill_icons.py` | Manual skill-icon size research workflow |
| `gen_ae_icon.py` | Regenerate Quick Access icon arrays from the source `ae.png`; manual tool, not a build step |
| `fetch_item_ids.py`, `scrape_wiki_ids.py`, `wiki_item_ids.json` | Manual item-ID research/reference; not authoritative runtime data or CI dependencies |

Old banner and Janus icon generators and their generated artifacts have been retired. Git history retains them if needed.

## SQLite benchmark

Run `python3 tools/benchmark_sqlite.py --scale 1 --repeats 15 --output sqlite-benchmark.json`. Creates disposable synthetic cache and library databases using current real migration SQL, then measures warm startup projections, account reads, incremental character and library writes, short vs persistent connections, a writer competing with a held reader, and active/closed file sizes. DELETE/FULL is the baseline; WAL/FULL isolates journal behavior and WAL/NORMAL is a separate durability tradeoff. No user paths are accepted. The Windows workflow runs the benchmark on its Linux build host and includes the JSON report with the DLL artifact. Python's SQLite version, host and fixture scale are recorded; this is not actual DLL startup, vendored SQLite, Windows storage, or real-game workload. Repeat on a target Windows host and measure native code before changing defaults.


Wiki resource generation is separate from the existing manual item-ID research scripts. See [Wiki resource freshness policy](../docs/architecture.md#wiki-resource-freshness-policy) and [provenance and rights](../docs/architecture.md#wiki-data-provenance-and-rights). The enabled generator uses validated normalized snapshots, per-dataset 24-hour build eligibility and a structured primary retrieval path with rendered-table or raw-JSON fallback on failure. Do not introduce a blanket Wiki scrape. Record source provenance and update [third-party notices](../THIRD_PARTY_NOTICES.txt) before enabling a new dataset.

`wiki_resources.py --state <snapshot> --refresh` refreshes enabled daily-eligible rotation resources; `--generate <directory>` is offline. `--force` bypasses age explicitly and `--probe` performs only bounded exploratory requests. The checked-in snapshot is the offline baseline. CI restores/saves a separate state and publishes its snapshot with the build. Promote reviewed successful snapshots into Git; do not replace valid data with failed requests.

The primary rotation parser reads the actual MediaWiki template arrays/switches; event timers parse nested JSON and use explicit phase identities. Seed refreshed from Debian 2026-11-25. For a normal build, no Wiki requests until the per-resource 24-hour attempt age expires.

The generator emits WikiEventMetadata.inc from the manifest and verified snapshot, sharing exact phase identities and runtime age with the native parser. Build-time eligibility is 24 hours; runtime SQLite event refresh is 30 days.
