# Agent instructions — Alter Ego

## Scope and collaboration

- Requests to discuss or suggest are discussion only. Implement when approved; do not ask again for routine in-scope decisions.
- Work on the requested branch. Never modify or merge into main or another branch without approval.
- Preserve original Alter Ego library import compatibility, saved data, exact native component identity and independent functional Compare semantics and the Nexus ImGui 1.80 ABI. Do not upgrade ImGui or change its allocator/context contract independently.
- Prefer existing modules and canonical helpers over parallel implementations. Keep changes cohesive; extract a focused helper when useful rather than growing `dllmain.cpp` unnecessarily. Avoid unrelated refactors.

## Read only the relevant references

This is the authoritative agent instruction file; `CLAUDE.md` delegates here. Do not load every reference for every task.

| Task | Read first |
| --- | --- |
| UI, controls, menus, searches or filters | [UI component reference](docs/ui-components.md) |
| Persistence, APIs, caches, callbacks, lifecycle or architecture | [Architecture](docs/architecture.md) |
| Tests, CI or build changes | [Regression testing](docs/regression-testing.md) |
| AE2 share/import format | [Shared build specification](docs/shared-build-spec.md) |
| Dataset or asset generation | [Tool inventory](tools/README.md) |

## Non-negotiable guardrails

- Never hold a mutex across synchronous Nexus query events or callback-capable registration. Respect thread ownership and addon unload lifetimes.
- Reuse existing cache and refresh mechanisms. Local filtering, sorting and presentation must not issue new network requests. **Do not refactor refresh request lifecycles, scheduling, retries, caching or per-page state without specific approval.**
- **Clipboard:** Never implement or call direct Windows clipboard APIs in Alter Ego application code. Use the existing Nexus-owned ImGui clipboard callbacks for explicit copy/paste actions, preserving UTF-8 and host context; do not override the host callbacks or add native clipboard fallbacks. Vendored ImGui internals are not application-level clipboard code.
- For UI changes, reuse canonical controls and shared domain actions, preserve the established appearance and stable IDs, and update the UI reference when changing a canonical component. Explain deliberate direct ImGui exceptions near the call site.
- Preserve unknown/imported fields, distinguish missing data from verified matches, and retain transactional edits, backup and migration safeguards.
- SQLite is authoritative for Loadouts and Fashion. Use repository transactions, preserve exact content and referenced identities, and keep SQL off render paths. Never recreate legacy mirrors or silently replace damaged databases. Rebuildable achievement and Skinventory data belong in the independent cache database; use StoragePaths.h for classified filesystem paths and the existing repositories for relational data. Do not place user-created information in the disposable cache.
- Extend existing regression cases and suites. Keep assertions enabled; do not weaken tests or skip binary checks to obtain a green build. MetaBattle is excluded from automated regression testing by explicit user request; retain the runtime importer. Skill icon research is manual-only.

- Treat clipboard, imports, chat codes, API/Wiki responses and remote artwork as untrusted. Bound JSON before and during parsing via `UntrustedInput.h`; use typed validation, bound SQL values and trusted identifiers, and retain prior verified data on rejection. Preserve valid historical formats and unknown extension data within resource budgets.

## Definition of done — implementation and DLL delivery

An approved implementation request is not complete until the verified DLL has been delivered, or a genuine blocker requiring the user's involvement has been explained.

1. Implement and commit on the designated branch.
2. Trigger or locate the Windows DLL workflow for the exact implementation commit. Follow it through required offline regression tests, compilation, binary verification and packaging.
3. If a required step fails, inspect logs, diagnose, apply safe in-scope fixes or retry transient failures, and rebuild. Do not stop at a failure you can resolve.
4. Confirm GitHub Actions succeeded for the exact final implementation commit. Retrieve the matching artifact, extract `AlterEgo.dll`, verify Windows x64 UCRT and confirm the delivered file actually exists.
5. Deliver a working link to the actual binary, not merely a workflow or artifact page. Identify the commit and report verification and remaining limitations, including unperformed in-game testing.

A queued or running build, source commit, or successful build without DLL delivery is not completion. Continue checking while the environment permits. If it cannot continue, explicitly report the incomplete state and use a supported follow-up mechanism when available. Never fabricate a link or promise unsupported background work. Escalate only a material decision, missing permission or failure that cannot reasonably be resolved within the approved scope.

Discussion-only and documentation-only requests do not require a new DLL unless explicitly requested.

## Repository hygiene

- Keep durable guidance in the relevant existing reference, not duplicated instructions or dated plans. Update docs only when behavior or conventions change.
- Keep docs/roadmap.md forward-looking: unresolved implementation, investigation and proposals only. Before adding work, reconcile overlapping/superseded items. Distinguish proposed from approved; never treat a handoff or audit as approval. After required automated verification and DLL delivery, remove completed implementation entries instead of appending checked boxes or build reports. For partial work, retain only the unresolved requirement. Do not close a task whose implementation or required verification is still incomplete.
- Keep docs/development-status.md limited to the latest verified runtime checkpoint and specific outstanding validation (including deferred tests and known limitations). Update the checkpoint only with exact-commit evidence; documentation-only changes do not require a new DLL or invalidate the last runtime build. CI is not live validation. Preserve permanent design decisions in Architecture/UI/testing references and release-specific gates in the release checklist. No historical roadmap archive or repeated commit hashes/checksums/build timings: Git history and Actions retain evidence.
- Review documentation affected by the change for contradictions and obsolete instructions; prune completed historical narratives while retaining current behavior, migration guarantees, test procedures, unresolved risks and approvals. Do not reduce scope or discard an unresolved requirement merely to shorten a document.
- Preserve useful vendored libraries, licenses, compatibility code and reference tools. Keep `LICENSE` and `THIRD_PARTY_NOTICES.txt` with source and binary distributions; update notices when adding or updating dependencies, artwork or third-party datasets. For Wiki sources follow the [provenance and rights policy](docs/architecture.md#wiki-data-provenance-and-rights); do not enable unreviewed copyrightable material. For standalone DLL delivery, include the accompanying notices with the download; verify notice availability for catalog distribution. Remove confirmed obsolete artifacts, not working code merely to save space. Git history is the archive.
- Keep the public README concise and organized around current user-facing features, setup, usage, storage, development, credits and license. Keep internal detail in the linked references; acknowledge original authorship in Credits, not feature-by-feature comparisons. Update screenshot links when current images are available; remove superseded unreferenced screenshots at that time.
- Keep privacy and security reporting documents accurate. Update `PRIVACY.md` when network destinations, credentials or data handling change; do not claim release or policy validation without evidence. Track release gates in `docs/release-checklist.md` and user-visible release changes in `CHANGELOG.md`.
- Maintain [the dependency inventory](docs/dependencies.md) and license notices for vendor changes. Never bypass fingerprint or ABI checks; review advisory matches and uncovered dependencies manually. Keep product-specific CVE assessments evidence-backed in `vulnerability-assessments.json`; never suppress unknown or stale findings. Do not auto-merge dependency updates.
- Remove temporary diagnostics, scratch artifacts and experimental scaffolding when their purpose ends; retain useful failure diagnostics and tests.
