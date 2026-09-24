# Workbench results

Headline measurements over time. Raw per-cell data lives in `tools/workbench/results/<run>/` (not committed); this file keeps the conclusions.

Engines: **base** = upstream `e9d1993` + workbench only (fork branch `workbench-base`); **dev** = fork `master` (performance patches + reviewed community PRs + workbench); **nosim** = dev with our simulation performance patches reverted.

## 2026-09-24: simulation optimisations on lemon (i9-14900K, max sim speed, identical game state)

Measured with `--sim-speed max` (simulation time per frame, median of interleaved runs) on the headless engine; every step keeps the game state identical (sync_repro state digests over 3,000 frames).

| Step | big_battle ms/sim frame | mass_move_5000 ms/sim frame |
|---|---|---|
| before (fork master at start of the day) | ~11.4 | ~12.5 |
| + collision transform cache | ~10.4 (-10%) | - |
| + closest-target search nearest-first | ~7.2 (-24%) | - |
| + `-march=native` build | ~7.4 (noise) | ~11.5 (-1.5%) |
| + quad-field stamps out of the objects | ~7.1 (-13% vs native) | ~10.7 (-7%) |
| + model-uniform slot cache (draw side) | ~7.1 | ~10.7 (GetObjOffset gone from the profile) |

Overall: big battles ~1.6x the simulation throughput (3.9x real time at max speed), 5,000 moving units ~15% less sim time. On the RTX 4090, drawing is ~1.3-1.9 ms per frame at 1920x1080, so on lemon the simulation is the limit. Rejected: a `TraceRay` bounding-sphere pre-check (the exact version gives no gain), LTO (build image's gold lacks the LTO plugin; not pursued), `-march` beyond native (no gain: strict float flags block vectorisation).

## 2026-09-24: do our performance patches speed the engine up? (A/B, identical battles)

**noperf** = master with only our performance patches reverted (merged community PRs, fixes, workbench and the non-blocking GPU timer kept), so both engines simulate the same battles; `--seed 1234`, 5 interleaved repetitions each, median of per-window p50 (ms).

| Scenario | Metric | noperf | dev | change | verdict |
|---|---|---|---|---|---|
| big_battle | frame | 27.82 | 25.60 | -8.0% | same (noperf 22.3-28.7, dev 23.8-28.2) |
| big_battle | sim | 26.34 | 25.61 | -2.8% | same |
| big_battle | GPU | 30.24 | 28.56 | -5.6% | same |
| mass_move_500 | frame / sim | 14.45 / 6.40 | 14.43 / 6.37 | ~0% | same |
| mass_move_2000 | frame / sim | 26.05 / 16.92 | 25.90 / 16.75 | ~-1% | same |
| mass_move_5000 | frame / sim / GPU | 14.70 / 45.60 / 31.96 | 15.23 / 45.61 / 31.50 | +3.6% / 0% / -1.5% | same |
| render_baseline | frame / GPU | 13.86 / 11.75 | 13.36 / 11.87 | -3.6% / +1.0% | same |

**Conclusion: no measurable speed-up.** Every difference is inside run-to-run noise. The earlier "big_battle sim -14.9%" (base vs dev, below) came from the merged community PRs changing the battle, not from faster code. Where the time goes (dev, ms per sim frame): at 5000 units `Sim` 45 = `Sim::Unit::MoveType` 24 + unit scripts (`CUnitScriptEngine::Tick`) 11, with thread-pool wait time showing the parallel sections are not keeping cores busy; in `big_battle`, `Sim::Unit::SlowUpdate` 20, unsynced Lua callins (BAR widgets) 11, `TransformsUploader::Update` 7. Those are the targets for real gains.

## 2026-09-24: game logic coverage, update (dev, max sim speed)

After the fixes below, the whole logic set passes: `unit_movement` 346/346, `ship_movement` 104/104, `weapon_range_all` 238/238, `air_attack` 18/18, `unit_behaviours` 41/41 (restricted to the tech tree players can reach; the earlier 94 included scavenger and evolution-only defs). Wall clock: 219 s for all five, against ~58 min at 1x.

## 2026-09-24: game logic coverage (dev, flat arena)

Generated from the game's defs, so new units are covered without writing tests. Every failure names what happened (which unit and weapon hit, which weapons fired, how far short a unit stopped).

| Scenario | What each case checks | Cases | Pass |
|---|---|---|---|
| `unit_movement` | every mobile ground unit type arrives within budget, never over maxSpeed +10% | 346 (173 types) | 346 |
| `ship_movement` | the same for every ship and submarine, on a flooded arena | 104 (52 types) | 104 |
| `weapon_range_all` | every armed ground unit hits a structure inside its must-hit range and nothing it fires reaches past its reach | 238 (119 types; 18 without a weapon that can hurt a structure skipped) | 237 in the full run; the one failure (`corsiegebreaker`, 1500 energy per shot on an arena with no energy) passes after the fix (`--filter corsiegebreaker`) |
| `air_attack` | every armed aircraft that can hurt a structure strikes one 1500 elmos away | 18 | 18 (first hits after 5.0 s for drones to 13.1 s for `armpnix`) |
| `unit_behaviours` | every land factory produces, every mobile builder builds; transport, cloak, radar | 94 | 94 |
| `ui_lowfps` | box select and shift-drag build with the whole gesture inside one ~8 fps frame | 2 | 2 |

Getting there took four test-design fixes, each found by the workbench's own diagnostics: map terrain, leftover projectiles between batches, the range model (dummy weapons, underwater-only lasers, stockpiles, splash), a cloak widget holding fire, and weapons that need energy.

## 2026-09-24: settings sweep (`--suite render`, dev, 1 rep, median of per-window p50)

| Profile | render_baseline frame | GPU | mass_move_500 frame | GPU |
|---|---|---|---|---|
| low (no shadows, MSAA off, basic water) | 9.7 ms | 9.0 ms | 10.3 ms | 10.7 ms |
| default | 13.6 ms | 12.2 ms | 14.8 ms | 13.8 ms |
| ultra (shadow quality 6, MSAA 8, 40k particles) | 40.4 ms | 37.4 ms | 41.4 ms | 38.9 ms |

Ultra costs about 3x default and is GPU-bound at every profile (GPU ≈ frame time). The first run of this sweep exposed that GPU time was not being measured at high frame rates (see findings); these numbers are from the fixed engine.

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

## 2026-09-24: where the time goes at 5000 units (`mass_move_5000`, dev, ms per sim frame)

| Subsystem | ms |
|---|---|
| Sim (total) | 36.3 |
| Sim::Unit::MoveType | 19.1 |
| - CollisionDetection | 7.0 |
| - UpdateTraversalPlan | 7.0 |
| - UpdatePreCollisions | 1.7 |
| CUnitScriptEngine::Tick (unit animation scripts) | 9.2 |
| Sim::Unit::UpdatePreFrame | 1.9 |
| Draw (total) | 16.7 |
| Lua::Callins::Unsynced (BAR widgets) | 7.7 |
| Draw::Screen | 5.3 |

The two biggest targets for sim optimisation at scale are ground-unit movement (collision detection and traversal planning) and the unit script engine tick; on the draw side, BAR's unsynced Lua callins cost more than the world draw.
