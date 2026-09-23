# Workbench results

Headline measurements over time. Raw per-cell data lives in `tools/workbench/results/<run>/` (not committed); this file keeps the conclusions.

Engines: **base** = upstream `e9d1993` + workbench only (fork branch `workbench-base`); **dev** = fork `master` (performance patches + reviewed community PRs + workbench); **nosim** = dev with our simulation performance patches reverted.

## 2026-09-24: determinism (`sync_repro`, seed 1234, spectate, 3000-frame seeded battle)

| Comparison | Result |
|---|---|
| base vs base (2 runs) | identical |
| dev vs dev (2 runs) | identical |
| dev vs nosim | **identical**: our simulation performance patches are behaviour-neutral on this battle |
| base vs dev | diverged at battle frame 4: expected, merged community PRs change behaviour on purpose (#3354, #3240, ...) |

## 2026-09-24: performance, light load (3 reps each, median of per-window p50)

| Scenario / metric | base | dev | change | verdict |
|---|---|---|---|---|
| render_baseline frame time | 13.48 ms | 13.07 ms | -3.0% | same |
| render_baseline CPU draw | 5.76 ms | 5.16 ms | -10.4% | same (noisy) |
| render_baseline GPU | 12.68 ms | 10.85 ms | -14.5% | same (base spread 10.6-13.9) |
| mass_move_500 frame time | 14.58 ms | 12.66 ms | -13.2% | same (base spread 12.6-14.9) |
| mass_move_500 sim | 5.56 ms | 5.42 ms | -2.4% | same |
| peak memory | ~7.89 GB | ~7.88 GB | ~0% | same |

The direction favours dev on rendering, but at this load and with 3 repetitions the differences stay inside the run-to-run noise, so the report does not claim them. Heavier scenarios (`mass_move_2000/5000`, `big_battle`) and more repetitions are needed to measure the patches' effect.

## 2026-09-24: performance, heavy load (3 reps each, median of per-window p50)

| Scenario / metric | base | dev | change | verdict |
|---|---|---|---|---|
| mass_move_2000 frame time | 26.85 ms | 25.92 ms | -3.5% | same |
| mass_move_2000 sim | 16.58 ms | 16.23 ms | -2.1% | same |
| mass_move_5000 frame time | 51.56 ms | 51.78 ms | +0.4% | same |
| mass_move_5000 sim | 40.50 ms | 40.68 ms | +0.4% | same |
| mass_move_5000 GPU | 34.14 ms | 33.00 ms | -3.3% | same (dev lower in all 3 runs) |
| big_battle sim | 24.30 ms | 20.67 ms | -14.9% | **improvement** (just beyond base spread) |
| big_battle frame time | 33.03 ms | 31.66 ms | -4.2% | same |
| big_battle GPU | 34.43 ms | 32.33 ms | -6.1% | same |

Caveats: base and dev do not simulate identical battles (merged community PRs change behaviour; see determinism above), so part of the `big_battle` sim difference may be a different fight rather than faster code. At 5000 units the frame is sim-bound (~40 ms/frame) and our patches do not move it: that is where the next optimisation work should go. Per-window profiler timings (added after this run) show which subsystems own that time.
