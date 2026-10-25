# Community PRs: review log

Open upstream PRs reviewed for this fork, so later passes only look at new ones. Policy: the "What gets in" section of the [README](../README.markdown). The carried PRs are the `Merge PR #N` commits on `master` (`git log --first-parent --grep '^Merge PR'`). This log records review rounds and what was skipped.

## 2026-10-25

Reviewed: open, non-draft PRs updated since 2026-10-10 that weren't reviewed in the last round, skipping features, docs/CI and macOS-only work. lemon renders on an RTX 4090 with the NVIDIA driver, so work aimed at Mesa, Zink or KosmicKrisp was judged by its effect there.

Merged, bug fixes: #3615, #3613, #3607, #3606, #3604, #3599, #3598, #3597, #3596, #3595, #3590, #3587, #3586, #3582, #3581, #3580, #3577, #3576, #3575, #3573, #3569, #3567, #3563, #3562, #3561, #3560, #3559, #3558, #3557, #3555, #3554, #3553, #3552, #3551, #3550, #3537, #3529, #3526, #3522, #3521, #3503, #3502, #3501, #3474, #3470, #3468, #3467 (onto the carried #3420 chunked update).
Merged, performance: #3500, #3464 (passes `sync_repro` against master; see the merge commit).

Skipped:

| PR | Why |
|---|---|
| #3487, #3508, #3509, #3510, #3511, #3512, #3514, #3515, #3520, #3523, #3565, #3572, #3583, #3588, #3589, #3603, #3605, #3612, #3617 | Synced behaviour fixes (movement, weapons, pathing, reclaim, targeting, COB): they change simulation results |
| #3610 | Changes how `GiveOrder*` reads synced params tables (entries past a hole) |
| #3525 | Changes which binding fires for existing keysets |
| #3496 | Overlaps #3576 (taken); one query map shared by all Lua states, freed together |
| #3473 | On top of #3464; maintainer asked for the GCC extension separately, no 1%-low gain |
| #3439 | Synced; measured -0.4%, and the description is for another commit |
| #3343 | Turns off engine smoke shadows by default; +0.5% after the tester's setup was corrected |
| #2680 | Diverges replay checksums (stale piece transforms); the fix PR was closed |
| #3192 | Synced CommandAI overlap rewrite, needs a companion game PR |
| #3533 | Zink/macOS workaround for a Mesa bug since fixed upstream; no gain |
| #3556, #3542, #3543, #3494, #3516, #3566 | Driver-specific (Mesa/Zink) buffer and texture paths, unmeasured on NVIDIA |
| #3457 | Unmeasured, changes shore output, no maintainer approval |
| #3611 | Rewrites live table keys on the saving client; low value for BAR |
| #3497, #3498, #3499 | Change Lua API or save permissions for existing content |
| #3531, #3431, #3407 | Changes requested |

## 2026-10-10

Reviewed: open, non-draft PRs updated since 2026-03, skipping features, rewrites, docs/CI and macOS-only work.

Merged, bug fixes: #3459, #3448, #3460, #3461, #3438, #3333, #3368, #3293, #3218, #3202, #3319.
Merged, performance: #3446, #3437, #3435, #3420, #3426, #3411, #3417, #3416, #3428, #3436 (with #3433), #3434 (two of three commits).

Skipped:

| PR | Why |
|---|---|
| #3350 | Mostly redundant with #3354 (carried); its `finishCommandBudget` isn't reset by #3354's path, so they'd need reconciling |
| #3412 | Refactor with no fix yet; conflicts with CommandAI changes |
| #3276 | Sound-thread concurrency change still awaiting technical review upstream |
| #3145, #3142 | Change what `unbind` / `unbindkeyset` remove in existing keybind files (BAR's specs expect current behaviour) |
| #3163 | Writes `Fullscreen=0` into the saved config; maintainer asked for a non-persisting override |
| #3022 | Changes requested; only matters for core-profile GL (macOS) |
| #3197 | No effect for BAR (ships its own resources.lua) |
| #3444 | Redundant with the existing water visibility check; adds water pop-in |
| #3458 | Unreviewed; decals stop projecting onto the map border |
| #3429 | Unreviewed, unmeasured on Linux; likely adds a futex wake per `for_mt` |
