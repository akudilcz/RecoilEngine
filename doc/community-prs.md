# Community PRs: review log

Open upstream PRs reviewed for this fork, so later passes only look at new ones. Policy: the "What gets in" section of the [README](../README.markdown). The carried PRs are the `Merge PR #N` commits on `master` (`git log --first-parent --grep '^Merge PR'`). This log records review rounds and what was skipped.

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
