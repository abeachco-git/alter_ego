# Temporary Vault / daily rollover rollback guide

**Purpose:** keep the fragile daily reset behavior auditable until live rollover testing confirms the new Vault comparison and retry admission. Remove this temporary note after live acceptance; Git history remains the permanent record. No data migration is involved.

## Baseline

- Last delivered baseline: runtime `577e9740d28a1f6cec44afda68de726e484d5e5c`, [build #708](https://github.com/abeachco-git/alter_ego/actions/runs/45934465193). Documentation checkpoint before this work: `bc20e3ce959ecddf5e25f88cffab8ddcdc62347a`.
- Earlier daily Vault and Fractal behavior was observed working by the user; build #708 itself has not been live-tested across reset.

## Targeted commits

| Commit | Scope | Roll back if |
| --- | --- | --- |
| [198ded0](https://github.com/abeachco-git/alter_ego/commit/198ded0d5128bb406fe5b635a30ac973df699bbd) | Vault directional period comparison in `ClearsReset.h` and existing `tests/clears_rotation.cpp`. | A new Vault daily/weekly/Special period is held too long or reset evidence is incorrectly accepted. |
| [d9a4da6](https://github.com/abeachco-git/alter_ego/commit/d9a4da6bcc9f200aa5c9bf3f889f6bee4d2a5d82) | Vault deferred HTTP 429 retry admission/intent only, through the existing render dispatcher and shared policy; test in `section_refresh`. | Vault retries stop incorrectly while the view is open, or manual/automatic opt-out and view switching behave incorrectly. |
| [bc22c96](https://github.com/abeachco-git/alter_ego/commit/bc22c96d3ffe48bc15c09506ee8f2ea8f72cd505) | Achievement progress polling moves to actually rendered browser/nonempty tracker consumers. Separate from Vault/Fractal rollover. | Achievement browser or pinned tracker demand regresses. |

## What changed, and what did not

Vault comparison is directional: any changed objective ID set, a single completed-to-incomplete objective, a claimed-to-unclaimed objective/reward, or a completed-to-incomplete meta objective is evidence of a new period. Forward progress, incomplete-to-incomplete changes, reorder and reward/target metadata changes alone are not evidence. The existing guarded all-zero fallback (900 seconds and at least three held attempts), empty Special exception and no-baseline behavior are unchanged. Without an authoritative period ID, a genuinely new period that has identical IDs and no observable reset transition can remain held; this is the intentional conservative tradeoff, not a confirmed reset.

Deferred Vault rate-limit retries still dispatch only from the rendered Vault view. A manual click bypasses automatic policy while the view remains open; leaving ends that manual retry consent. Reopening follows ordinary policy. A disallowed queued retry is recorded through the existing per-section failure path without dispatch; already dispatched requests can publish valid results. The bounded three short 429 retries and their timing are unchanged. No new background scheduler.

**Daily Fractal and Raid Bounty runtime paths are unchanged.** In particular, 00:00 UTC daily reset, Monday 07:30 UTC weekly reset, 60-second propagation grace, reset marker initialization, cache-busting tokens/headers, both public category routes, prior ID-set validation, complete definition validation, account completion broker, 30/60/120/300-second rollover retry cadence, section persistence/fingerprints and generation fencing are preserved. Weekly Clears progress comparison is unchanged. Manual successful multi-stage continuation behavior is unchanged. No public catalog policy or unrelated timing changes.

## Selective rollback

On the experimental branch, prefer a new revert commit rather than resetting the branch or deleting caches. Revert only the suspected commit (and any conflicting later change after review). To roll back the two Vault changes together while preserving Achievement visibility and unrelated work:

```sh
git switch experiment/composable-loadouts
git revert d9a4da6bcc9f200aa5c9bf3f889f6bee4d2a5d82
git revert 198ded0d5128bb406fe5b635a30ac973df699bbd
```

If only comparison is at fault, revert `198ded0` alone; if only retry admission is at fault, revert `d9a4da6` alone. Reconcile the Architecture and testing notes to the resulting behavior; rebuild, verify and deliver the matching DLL. Do not merge main, reset SQLite, or undo the working Fractal rollover implementation. Reverts can conflict if later work touches the same lines; review rather than force.

## Live acceptance checklist

- [ ] Before reset, capture yesterday's verified Vault objectives including claimed/completed and partially progressed examples, and daily Fractal IDs/names. Keep these as local observations; no secrets.
- [ ] Keep Vault visible across 00:00 UTC and the 60-second grace. Confirm old claimed rows are not promoted to the new day, one changed ID or done-to-not-done transition is sufficient, and the new objectives eventually verify.
- [ ] Keep Clears visible across another daily reset. Verify new daily Fractals and Bounties, prior-ID rejection, alternate route and completion progress after reset; verify weekly data remains intact.
- [ ] Leave Vault/Clears during a pending retry, reopen after grace/backoff and confirm eligible refresh resumes; no new automatic retry while hidden. Toggle automatic refresh off while a Vault 429 retry is pending; verify no automatic dispatch and manual refresh remains available. Test manual refresh with auto disabled, then leave/reopen (no inherited manual retry authorization).
- [ ] Test a partial Vault section failure, weekly reset and Special; check independently verified sections and timestamps remain correct.
- [ ] Compare actual game objectives and API activity; if a regression appears, record affected section, previous/current IDs and completion direction, reset time, visible demand and exact build before reverting.

Current verification: [build #709](https://github.com/abeachco-git/alter_ego/actions/runs/45935811447), exact commit `e54638c615c7fe2f36db25012872455865e6a0da`, passed required offline regressions, sanitizers, Windows x64 UCRT build and binary verification. Live reset acceptance remains open.

Leaving and reopening a page does not reset elapsed retry cooldowns or create a manual request. Reopening resumes ordinary demand eligibility; only an explicit Refresh bypasses ordinary cooldown. Existing Vault batch timeout remains independent.
