# Regression testing

Run the platform-independent suites using the native compiler:

```sh
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests --parallel 2
ctest --test-dir build-tests --output-on-failure
```

Run a single domain with `ctest --test-dir build-tests -L comparison --output-on-failure`.
Run a single case with `ctest --test-dir build-tests -R 'comparison.infusion_totals' --output-on-failure`.

Nine domain executables contain 24 independently runnable regression cases.
Each CTest case is a separate process. Keep assertions enabled even in Release.
Add related coverage to existing cases; add a new case only for distinct behavior,
and register it in the existing domain in tests/CMakeLists.txt. No new standalone
test executable per bug. Use invariant checks for presentation rather than fixed
pixel values or exact cosmetic wording. Preserve semantic, data integrity,
upstream compatibility and import checks. Obsolete experimental fixtures may be
removed after demonstrating that the behavior is no longer supported; Git history
is the archive. Do not remove tests solely because they fail.

The Windows workflow gates DLL packaging on all offline cases and binary checks.
The independent live MetaBattle workflow uses the same import executable with
a snapshot directory and verifies all six live imports strictly. Live site
availability never gates DLL packaging. Skill icon research is manual-only; it
builds a baseline DLL for accurate size comparisons and uploads the report.

## CI maintenance

Keep the six live MetaBattle import checks strict, independently reported, and upload their HTML snapshots for debugging. Live-site outages or changed content must remain visible in that workflow rather than blocking DLL packaging. Do not weaken the required offline cases or binary verification to resolve a failed build. Follow the delivery definition in [AGENTS.md](../AGENTS.md).

The persistence suite additionally exercises the SQLite foundation in
`persistence.database_core`: versioned schema, bound text, rollback, foreign
keys, migration interruption, restart, backup verification and rejection of
an unrelated database. This does not yet exercise the Loadout runtime adapter.

`persistence.native_library` exercises relational round-trips including
unknown extensions, physical sigils/infusions, Fashion, component references,
intentional duplicates, forced hash collision, exact candidates and usage,
restart, stale edit rejection and transactional writes.

`persistence.native_migration` covers original build and Fashion archives,
exact safe migration deduplication, abandoned staging directories, empty
initialized databases, corrupt native data, invalid legacy and unexpected
unexpected experimental v3 files, and verified database backups without overwriting destinations.

Native editing and portable interchange are covered by `persistence.native_editing` and `persistence.native_portable`: exact matching rather than functional similarity, reuse after modification, copy-on-edit, unchanged IDs, unknown fields and presence distinctions, intentionally unused records, linked Fashion and collision-safe portable merges. The backup tests exercise selective native ZIP/JSON restore and legacy import; obsolete mirror timestamp/reconciliation recovery tests have been removed in favor of native migration tests.

The database core case also covers 5,000 achievement progress rows and completed bits, exact account-name isolation, older snapshot rejection, transaction rollback, disposable cache deletion, configuration migration and archive independence. These are correctness regressions, not a timing benchmark.
