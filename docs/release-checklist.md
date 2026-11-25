# Independent release checklist

This is a release gate, not an implementation plan. Track feature work in the [roadmap](roadmap.md) and runtime validation in [regression testing](regression-testing.md). Check items only after confirming them.

## Identity and distribution

- [ ] Confirm Wayfinder name, Nexus registration and independent addon signature with Raidcore.
- [ ] Choose final DLL/display names, versioning, repository and update URLs; replace original branding and audit remaining source identifiers.
- [ ] Define and test migration from the original Alter Ego library and this experimental branch, including paths, credential targets, backups and rollback; prevent simultaneous conflicting addon instances.
- [ ] Review existing external dependencies: replace original-project dataset URLs with maintained independent sources; decide whether to retire or replace the external build-sharing relay. Keep clipboard/portable sharing available.
- [ ] Audit obsolete integration headers and unused resources without removing required compatibility or license notices.

## Policy, privacy and documentation

- [ ] Discuss Fast Load and memory access with Raidcore before catalog submission; confirm applicable disclosure labels, including AI-assisted development.
- [ ] Provide About & Licenses access in the addon, including embedded or otherwise accessible complete license notices for standalone DLL distribution.
- [ ] Review final icon, artwork and dataset provenance under applicable rights and terms.
- [ ] Re-audit outbound network destinations after the rename; update [privacy](../PRIVACY.md) to match actual behavior.
- [ ] Enable repository Issues and private vulnerability reporting on the public release repository; verify [security policy](../SECURITY.md) and both issue forms are reachable on its default branch.
- [ ] Replace README screenshots and install instructions, confirm final release name and update the [changelog](../CHANGELOG.md).

## Build and validation

- [ ] Pass offline regression, Windows x64 UCRT compilation, resource and binary verification for the exact release commit; package complete license notices.
- [ ] Complete authenticated single/multi-account, library migration, cache recovery, clipboard/import/export, Fast Load, unload and core UI smoke tests; record known limitations.
- [ ] Establish versioned GitHub prereleases/releases with matching DLL, license files and SHA-256 checksum; retain exact-commit debugging map/symbol artifacts.
- [ ] Test Nexus installation and update behavior from a clean environment. Verify the distributed DLL and its version/signature/update metadata.
- [ ] Publish only after the maintainer approves the tested release and outstanding risks.

No item in this checklist implies Nexus approval or successful live testing.
