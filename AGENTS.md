# Agent instructions — Alter Ego

## Scope and collaboration

- Requests to discuss or suggest are discussion only. Implement when approved; do not ask again for routine in-scope decisions.
- Work on the requested branch. Never modify or merge into main or another branch without approval.
- Preserve existing local libraries, supported original Pie or Cake external import/export, GW2 build links, Snow Crows and MetaBattle runtime import/refresh, current Alter Ego portable round-trips, exact native component identity and independent functional Compare semantics, and the Nexus ImGui 1.80 ABI. Older experimental Alter Ego portable formats have no compatibility requirement; this does not authorize discarding local database data. Do not upgrade ImGui or change its allocator/context contract independently.
- Use canonical implementations and focused reusable helpers, not parallel implementations or page-local copies. Avoid unrelated refactors.

## Read only the relevant references

This is the authoritative agent instruction file; `CLAUDE.md` delegates here. Do not load every reference for every task.

| Task | Read first |
| --- | --- |
| UI, controls, menus, searches or filters | [UI component reference](docs/ui-components.md) |
| Persistence, APIs, caches, callbacks, lifecycle or architecture | [Architecture](docs/architecture.md) |
| Freshness, refresh status, retry policy or freshness tooltips | [Unified freshness engine](docs/architecture.md#unified-freshness-engine) and [UI component reference](docs/ui-components.md) |
| Tests, CI or build changes | [Regression testing](docs/regression-testing.md) |
| AE2 share/import format | [Shared build specification](docs/shared-build-spec.md) |
| Dataset or asset generation | [Tool inventory](tools/README.md) |

## Canonical implementation and portability — required

- Before changing functionality, audit existing implementations, helpers, owners and all materially equivalent consumers. Identify the canonical implementation and document demonstrated inconsistencies before changing policy.
- Functionally equivalent behavior must share one implementation. Express legitimate dataset/context differences through explicit policy parameters, not copied logic, hardcoded page timers or new competing owners. Correct defects at the common source and review affected callers; do not force different semantics into a common abstraction.
- Keep domain calculations, validation and policy portable, independently testable standard C++. Separate Windows/Nexus/ImGui, HTTP, persistence and rendering adapters from domain logic. Reuse existing pure helpers and tests; justify necessary platform-specific or specialized exceptions near the owning code/reference.
- Ownership, freshness, visible demand, admission, retrieval and verified publication are distinct. Keep exact account/character/guild/public identity and credentials qualified throughout. Scheduler, status/tooltip and availability must derive equivalent decisions from the same authoritative policy and evidence. A login activates visible demand; it cannot renew or postpone already-overdue character or shared account data. Ordinary active freshness is based on verified retrieval, not login time. Logout reconciliation and objective reset propagation remain distinct explicit policies. Do not bypass reset/propagation, cooldown, permission or identity guards for consistency.
- Apply this requirement across the application, including UI controls/actions, comparison, searches/filters, imports, persistence and request/metadata owners. Regression tests must exercise common behavior across consumers and preserve intentionally distinct policies. Report the canonical helper, reviewed consumers, exceptions and unresolved inconsistencies in the completion summary.
- These rules do not authorize unapproved lifecycle changes; raise material policy differences for discussion under the guardrails below.

## Non-negotiable guardrails

- Never hold a mutex across synchronous Nexus query events or callback-capable registration. Respect thread ownership and addon unload lifetimes.
- Reuse existing cache and refresh mechanisms. Local filtering, sorting and presentation must not issue new network requests. **Do not refactor refresh request lifecycles, scheduling, retries, caching or per-page state without specific approval.**
- **Clipboard:** Never implement or call direct Windows clipboard APIs in Alter Ego application code. Use the existing Nexus-owned ImGui clipboard callbacks for explicit copy/paste actions, preserving UTF-8 and host context; do not override the host callbacks or add native clipboard fallbacks. Vendored ImGui internals are not application-level clipboard code.
- Every displayed game entity with a supported native chat link and valid exact identity should expose right-click Copy Chat Link alongside Copy Name. Never infer a link from a name, truncate an ID, or conflate a native link with portable/configuration copy. Keep selector context actions independent of selection.
- For UI changes, reuse canonical controls and shared domain actions, preserve the established appearance and stable IDs, and update the UI reference when changing a canonical component. Explain deliberate direct ImGui exceptions near the call site.
- Preserve unknown/imported fields, distinguish missing data from verified matches, and retain transactional edits, backup and migration safeguards.
- SQLite is authoritative for Loadouts and Fashion. Use repository transactions, preserve exact content and referenced identities, and keep SQL off render paths. Never recreate legacy mirrors or silently replace damaged databases. Rebuildable achievement and Skinventory data belong in the independent cache database; use StoragePaths.h for classified filesystem paths and the existing repositories for relational data. Do not place user-created information in the disposable cache.
- Extend existing regression cases and suites. Keep assertions enabled; do not weaken tests or skip binary checks to obtain a green build. MetaBattle is excluded from automated regression testing by explicit user request; retain the runtime importer. Skill icon research is manual-only.

- Treat clipboard, imports, chat codes, API/Wiki responses and remote artwork as untrusted. Bound JSON before and during parsing via `UntrustedInput.h`; use typed validation, bound SQL values and trusted identifiers, and retain prior verified data on rejection. Preserve supported interchange formats and unknown extension data within resource budgets.

## Definition of done — implementation and DLL delivery

An approved implementation request is not complete until the verified DLL has been delivered, or a genuine blocker requiring the user's involvement has been explained.

1. Audit equivalent consumers and shared implementations, record the approved policy and necessary exceptions, implement at the canonical source, extend cross-consumer regression coverage and reconcile affected architecture/UI/testing guidance. Commit on the designated branch.
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
- **README is for a first-time player, not a changelog.** Lead with what Alter Ego does, availability, installation, account setup and useful everyday workflows. Describe stable/current capabilities in plain language; consolidate related features into short benefit-oriented explanations rather than appending a bullet for every patch, internal improvement or regression fix. Keep setup, data/backup guidance, limitations, credits and license discoverable. Link to CHANGELOG.md for what changed, development status for verification, and architecture/UI/testing references for implementation details. Audit existing README content before adding anything; update only when it materially helps a new or returning user understand or use the addon. Acknowledge original authorship in Credits, not feature-by-feature comparisons. Update screenshot links when current images are available; remove superseded unreferenced screenshots at that time.
- Keep privacy and security reporting documents accurate. Update `PRIVACY.md` when network destinations, credentials or data handling change; do not claim release or policy validation without evidence. Track release gates in `docs/release-checklist.md` and user-visible release changes in `CHANGELOG.md`.
- Maintain [the dependency inventory](docs/dependencies.md) and license notices for vendor changes. Never bypass fingerprint or ABI checks; review advisory matches and uncovered dependencies manually. Keep product-specific CVE assessments evidence-backed in `vulnerability-assessments.json`; never suppress unknown or stale findings. Do not auto-merge dependency updates.
- Remove temporary diagnostics, scratch artifacts and experimental scaffolding when their purpose ends; retain useful failure diagnostics and tests.
