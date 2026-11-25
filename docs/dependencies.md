# Dependency maintenance

[dependencies.json](../dependencies.json) records bundled versions, upstream provenance, local Git blob/tree fingerprints, licenses and update policies. It covers DLL libraries and the embedded font, not every build tool, operating-system package, external artwork or optional source-only example.

- ImGui 1.80, its modified stb headers and Nexus API v6 are compatibility-locked. Never upgrade these automatically.
- SQLite and nlohmann/json updates are manual, with upstream release/security review and existing regression and binary gates. Inter updates are manual.
- The JavaScript AE2 library is first-party code with no runtime package dependencies. Dependabot is configured only for GitHub Actions (weekly grouped version PRs; default branch activation required). Do not auto-merge dependency PRs.

## Verification and reporting

Run from a clean committed checkout:

```sh
python3 tools/dependency_inventory.py
python3 tools/dependency_inventory.py --osv
```

The first command validates committed vendor fingerprints and checks ABI versions, then generates a CycloneDX 1.6 SBOM. The second also queries OSV for the verified upstream release/commit baselines. A baseline is not a claim that local bytes equal the upstream tree. Advisory matches generate warnings and appear in the CI summary and JSON report; network/validation failure fails the audit. No match does not establish safety. Nexus API and the three modified stb headers have no verified upstream commit and require manual advisory review. The parent ImGui result does not establish independent stb coverage.

The dedicated GitHub Actions workflow runs on pushes to main and the experimental branch, manual dispatch and weekly on the default branch. Its read-only job generates reports and uploads them. A separate job with narrowly scoped contents write permission submits the exact branch commit snapshot through GitHub's official Dependency Submission API using the built-in token. No external credentials, paid scanner or automatic patching. The default branch graph is the authoritative repository view; experimental submissions do not imply default branch coverage. Generic/GitHub PURLs may not map to supported advisory ecosystems.

Artifacts: `sbom.cdx.json`, `github-snapshot.json`, and `osv-report.json`. Include the matching SBOM with releases. These are source inventory reports, not a complete toolchain or binary SBOM.

## Updating a dependency

Review upstream advisories, verify release provenance, update the vendored source and its license notices, then record the actual versions, upstream baseline (or null if unknown) and Git object fingerprints from the reviewed committed vendor contents. Use `git rev-parse HEAD:path/to/vendor` for each listed path. Do not merely update fingerprints to silence an unexplained mismatch. Run the audit and full relevant regression/build gates. Review uncovered dependencies manually using upstream advisories and NVD.

References: [GitHub dependency submission](https://docs.github.com/en/rest/dependency-graph/dependency-submission), [OSV API](https://google.github.io/osv.dev/post-v1-querybatch/), [CycloneDX](https://cyclonedx.org/).
