# Recoil Workbench — Design

Date: 2026-09-23
Status: approved direction (engine core + Lua scenarios), pending spec review
Supersedes: the BAR-only draft on Beyond-All-Reason branch `workbench`

## Goal

A dog-food test environment built into the Recoil engine: it runs the real engine and the real game exactly as a player would (real renderer, real sim, real Lua, real input path) and turns what happens into numbers and verdicts. It should flush out performance regressions and game-logic bugs before players see them, and be the tool engine and game developers reach for by default because it is the fastest way to answer:

- Did my change make anything slower, anywhere?
- Did it break any unit, weapon, behaviour or UI interaction?
- Does the simulation still replay bit-identically?

## Principles

- **Dog food**: no mocks, no special sim paths. The workbench only observes, drives input through the same paths as a player, and issues orders the way players and AIs do. What passes in the workbench works in the game.
- **Engine owns the harness, games own the scenarios**: measurement, sync checksums, input emulation, timeouts, results and reporting live in the engine; game-specific scenarios live with each game (BAR ships a scenario pack) and use a small, stable Lua API.
- **One command, actionable output** (see Developer experience).
- **Measurement must not perturb**: collection is cheap, allocation-free per frame, GPU timing is asynchronous (never stalls the pipeline).

## Architecture

```
RecoilEngine
  rts/System/Workbench/
    Workbench.{h,cpp}          mode controller: CLI options, lifecycle, watchdog, exit codes
    WorkbenchMetrics.{h,cpp}   per-frame collectors: frame/sim/draw time, profiler timers,
                               async GPU timer queries, counts, Lua memory
    WorkbenchSync.{h,cpp}      per-sim-frame sync checksum log + expected-checksum comparison
    WorkbenchResults.{h,cpp}   JSON result writer (jsoncpp, already linked)
  rts/Lua/LuaWorkbench.{h,cpp} Lua API table `Spring.Workbench` (unsynced + synced subsets)
  cont/base/springcontent/workbench/
    harness.lua                generic scenario loader/scheduler for LuaUI and LuaRules
    scenarios/                 engine-level, game-agnostic scenarios (empty-map render baseline,
                               camera sweep, input round-trip)
  tools/workbench/             host side (Python 3)
    run.py                     matrix runner: builds x settings profiles x scenarios x reps
    report.py                  compare.html generator
    profiles/*.cfg             settings profiles (low, default, high, shadows-off, ...)
    templates/                 start-script and report templates
    tests/                     unit tests for runner/report

Beyond-All-Reason (scenario pack)
  workbench/                   BAR scenarios using Spring.Workbench + the existing SyncedRun proxy
    perf/  units/  ui/  repro/
```

### Engine mode

Enabled with command-line flags (gflags, like the existing ones in `SpringApp.cpp`):

- `--workbench <pattern>`: run matching scenarios, then quit.
- `--workbench-out <dir>`: results directory (default `<write-dir>/workbench`).
- `--workbench-timeout <seconds>`: hard watchdog for the whole run; per-scenario timeouts come from the scenario.
- `--workbench-expect-sync <file>`: checksum log to compare against (reproducibility).

In workbench mode the engine:

1. Starts the game from the given start script (offline, `HostPort=0`).
2. Exposes `Spring.Workbench` to LuaUI and LuaRules. The engine cannot inject scripts into a game's Lua environments, so the game opts in with a tiny widget and gadget that `VFS.Include("workbench/harness.lua")` when `Spring.Workbench.IsActive()`. The harness ships in the engine's `springcontent` archive (visible to every game) and finds scenarios in the game's `workbench/` folder first, then the engine's generic ones.
3. Collects metrics only inside scenario-marked windows.
4. Logs `CSyncChecker::GetChecksum()` every sim frame into a compact binary log; if an expected log is given, it stops at the first mismatch and records frame, expected and actual.
5. Writes one JSON file per scenario and a `run.json` summary, then exits with code 0 (all pass), 1 (check failures), 2 (crash, timeout or error).

Nothing in the sim changes behaviour in workbench mode; the only additions are observation and the existing input-emulation path.

### Lua API (`Spring.Workbench`)

Unsynced (LuaUI):
- `BeginWindow(name)`, `EndWindow(name)`: mark measurement windows.
- `Check(name, pass, detail)`: record a verdict.
- `SetFrameStall(ms)`: artificially stall draw frames (low-FPS UI tests).
- `GetSyncChecksum()`: current checksum (read-only).
- `Finish()`: end the scenario.
- `IsActive()`: true in workbench mode (scenario code no-ops otherwise).

Synced (LuaRules): `IsActive()`, `Check(name, pass, detail)` (synced-safe: results are buffered and written unsynced; nothing affects sim state).

Input driving uses the existing `debug.emulateKey*` and `debug.emulateMouse*`.

## Scenarios

### Engine-level (in engine repo)
- `render_baseline`: empty map, scripted camera path, per settings profile.
- `input_roundtrip`: emulated press/drag/release batched into one frame; verifies the engine delivers the events in order (guards the KeyInput modifier fix).

### BAR pack
- **Performance**: `mass_move_{500,2000,5000}`, `battle`, `render_sweep`.
- **Per-unit-type** (generated from UnitDefs, one check per `<check>:<unitDefName>`): weapon range (fires at range - margin, not at range + margin), movement (arrives within budget, never over maxSpeed, never stuck), core behaviours (build, produce, transport, take off/land, cloak, radar/jammer), per-unit cost.
- **UI regression**: low-FPS box-select and drag-build (the bugs fixed on `fix/lowfps-drag-select` and in `KeyInput`).
- **Reproducibility**: record `battle` with a fixed seed and the sync log; replay the demo with `--workbench-expect-sync`; any divergence reports the first frame.

## Metrics

Per window: draw frame time p50/p95/p99/max; sim frame time and count; GPU frame time (async `GL_TIME_ELAPSED` queries, read back a few frames later); selected profiler timers (Sim::Path, Sim::Los, Sim::Unit::Weapon, Draw::World::*, Lua) as mean and p95; unit and projectile counts; Lua memory.

JSON per scenario: `{scenario, engine:{version, branch, commit}, game:{name, version}, profile, repetition, windows:[{name, frames, frameTimeMs, gpuTimeMs, simTimeMs, timers, counts}], checks:[{name, pass, detail, repro}], sync:{frames, lastChecksum, divergence?}}`.

## Developer experience

- **One command**: `python tools/workbench/run.py --builds master,integration --suite smoke` runs everything, collects results and opens the report. Engine and data-dir discovery with sensible defaults.
- **Tiers**: `smoke` (< 10 min), `standard` (< 45 min), `full` (overnight). `--only <pattern>` runs any single scenario or unit.
- **Actionable failures**: each failure states expected vs actual, the unit/weapon/frame, and the exact one-line command to reproduce just that case.
- **Trustworthy numbers**: warm-up windows, repetitions, medians and spread; regressions are flagged only beyond both a relative threshold and the measured noise.
- **Report worth opening**: self-contained HTML, regressions and improvements summarised at the top, per-scenario charts and per-unit tables below.
- **Easy to extend**: a scenario is one Lua file; per-unit checks cover new units automatically.

## Error handling

- Engine watchdog: timeout writes partial results and exits with code 2.
- Crashes: the host runner records the cell as `error` with the infolog tail and continues the matrix.
- Scenario errors are caught by the harness and recorded as failed checks; the run continues with the next scenario.
- Unknown profile keys and missing scenarios are reported before launch.

## Testing the workbench itself

- C++: unit tests for metrics percentiles, sync log read/compare and result writing (Catch2, `test/engine/System/`).
- Python: `unittest` for runner matrix expansion, result parsing and regression detection.
- Acceptance: the smoke tier completes in under 10 minutes on the user's PC and produces a report; a deliberately introduced regression (a sleep in a timer zone) and a deliberately broken unit are both flagged.

## Phasing

1. Engine mode + metrics + results + Lua API; `run.py` single-cell; engine `render_baseline`; BAR `mass_move_500` and weapon-range checks; minimal report.
2. Sync log + expected-sync compare; BAR `battle`, `render_sweep`, movement, behaviours, per-unit cost; full matrix runner and HTML report.
3. Low-FPS UI regression (engine `input_roundtrip`, BAR box-select/drag-build) and replay reproducibility.

## Out of scope

Interactive panel; headless/CI execution (the design doesn't prevent it later); network/multiplayer scenarios.
