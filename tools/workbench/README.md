# Recoil Workbench

A test environment built into the engine. It runs the **real engine and the real game** exactly like a player would, then turns what happened into numbers and verdicts:

- **Performance**: frame time, CPU draw time, GPU time and sim time (p50/p95/p99/max) inside scenario-defined windows.
- **Game logic**: per-scenario checks (did every unit fire at its stated range, did the army arrive, ...).
- **Determinism**: the synced-state checksum of the last sim frame is recorded in every run.

Design: [`doc/superpowers/specs/2026-09-23-workbench-design.md`](../../doc/superpowers/specs/2026-09-23-workbench-design.md).

## Quick start (Windows host)

```bash
# 1. build (fast: mirrors the checkout into WSL) and deploy a runnable engine
wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh
wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/deploy.sh /mnt/c/Workspace/bar/engine-dev

# 2. run the smoke suite (~2 min) and open the report
python tools/workbench/run.py --suite smoke --engine dev=C:/Workspace/bar/engine-dev/spring.exe \
    --data-dir C:/Workspace/bar/data
```

Suites: `smoke` (the default), `standard` (adds big battles, every ground unit's and ship's movement, every factory's and builder's production and every armed aircraft's strike), `full` (adds every armed unit's weapon range; hours), `determinism` (`sync_repro`; use with `--spectate --seed N`), `logic` (every generated game-logic check, 747 checks in ~3.5 min at max sim speed) and `render` (graphics cost across the `low`, `default` and `ultra` settings profiles; the report adds a settings-sweep table). `--only <globs>` runs any scenarios by name instead; `--profile` picks profiles explicitly; `--filter <globs>` focuses generated scenarios on some cases, e.g. `--only weapon_range_all --filter corsiegebreaker` (scenarios ask `ctx.wants(caseName)`).

Compare two builds (the first engine is the baseline):

```bash
python tools/workbench/run.py --engine base=C:/Workspace/bar/engine-base/spring.exe \
    --engine dev=C:/Workspace/bar/engine-dev/spring.exe --data-dir C:/Workspace/bar/data \
    --only mass_move_500 --reps 3
```

`--data-dir` must contain `games/BAR.sdd` (the game checkout or a link to it) and the map (`--map`, default `Red Comet Remake 1.8`).

## What a run does

`run.py` expands `engines × profiles × reps` into cells. For each cell it writes a start script (offline, `HostPort=0`) and launches:

```
spring.exe --isolation --write-dir <data> --config profiles/<profile>.cfg \
    --workbench <pattern> --workbench-out <cell>/results --workbench-timeout <s> <startscript>
```

The engine loads the game normally. The game's opt-in widget/gadget load `workbench/harness.lua` (shipped in the engine's `springcontent`), which runs every scenario matching `<pattern>` from the game's `workbench/scenarios/` and the engine's generic ones, then quits.

Exit codes: `0` all checks passed · `1` a check failed · `2` error, timeout, or no scenario matched.

Per cell you get `results/<scenario>.json`, `results/run.json`, `infolog.txt`, `command.txt` (the exact command to reproduce the cell) and `cell.json`. `compare.html` summarises regressions, improvements, failed checks and errored cells.

A metric only counts as a regression or improvement when its median moves by more than both 5% and the baseline's spread across repetitions, so noise doesn't raise alarms. Use `--reps 3` or more when comparing performance.

## What every run checks automatically

- **Log issues**: the infolog is scanned for widget load failures, Lua errors and fatal errors; they are listed in the report even when every check passes.
- **Memory**: each window records process memory at start, end and peak; growth and peak are compared against the baseline like timings.
- **Crashes and game-ending scenarios**: an engine crash is recorded against the running scenario (no dialog is shown, so unattended runs never hang), and an engine that exits before the run finished is exit code `2`, never a silent pass. Symbolize a crash stack with
  `wsl -d Ubuntu -- bash tools/workbench/symbolize.sh /mnt/c/<engine dir> /mnt/c/<cell>/infolog.txt`.

## Simulation determinism

Engine changes that claim "no behaviour change" must produce bit-identical simulations. `sync_repro` (BAR pack) runs the seeded synctest battle from a fixed frame and records per-frame sync checksums; the report compares every cell's stream with the baseline's and names the first diverging frame:

```bash
python tools/workbench/run.py --only sync_repro --seed 1234 --spectate --reps 2 \
    --engine base=C:/Workspace/bar/engine-base/spring.exe --engine dev=C:/Workspace/bar/engine-dev/spring.exe \
    --data-dir C:/Workspace/bar/data
```

`--seed` sets `FixedRNGSeed`; `--spectate` makes the local player a spectator and both teams NullAI, so no widget can issue orders at real-time-dependent frames (as a playing player they do, and runs diverge). Run determinism comparisons on an otherwise idle machine.

A baseline engine is upstream plus only the workbench commits (built from a real clone, not a worktree: `git clone --no-local`, then `git submodule update --init --recursive`).

Findings the workbench has surfaced so far: [`doc/workbench/FINDINGS.md`](../../doc/workbench/FINDINGS.md).

## Writing a scenario

One Lua file in `workbench/scenarios/` (game) or `cont/base/springcontent/workbench/scenarios/` (engine, game-agnostic):

```lua
return {
	name = "my_scenario",          -- selected with --only / --workbench (comma-separated globs)
	timeout = 120,                 -- seconds (default 300)
	synced = {                     -- optional: runs in LuaRules synced, called via ctx.synced
		spawn = function(teamID, count) --[[ Spring.CreateUnit(...) ]] end,
	},
	run = function(ctx)            -- runs as a coroutine in LuaUI
		ctx.synced("spawn", Spring.GetMyTeamID(), 100)   -- args: numbers/strings
		ctx.waitSimFrames(30)
		ctx.window("fighting", function()               -- measured window
			ctx.waitSeconds(20)
		end)
		ctx.check("survivors", true, "detail shown in the report")
	end,
}
```

Game-logic scenarios should set `simSpeed = "max"`: the harness pins the sim to 100x (in practice 22-34x, CPU-bound) for that scenario and back to 1x afterwards, so performance and UI scenarios always run in real time. Such scenarios must wait in sim time (`waitSimFrames`, `waitSimSeconds`); `waitSeconds` is wall-clock. The full logic set (every ground unit's movement and range, every ship, aircraft, factory and builder: 747 checks) takes about 3.5 minutes instead of an hour.

`ctx` API: `waitFrames(n)`, `waitSimFrames(n)`, `waitSimSeconds(s)`, `waitSeconds(s)` (wall clock), `wants(caseName)` (honours `--filter`), `waitUntil(pred, timeoutSec) -> bool`, `window(name, fn)`, `check(name, pass, detail)`, `synced(fn, ...)` (fire and forget), `call(fn, ...) -> value | nil, err` (runs a synced function and waits for its return value), `atNextGameFrame(fn)` (runs fn during the next sim step, before the GUI update; use it for emulated input that must land where physical input does), `log(msg)`.

A scenario can also handle synced callins, for exact attribution (which unit hit what, with which weapon): `syncedCallins = { UnitDamaged = function(unitID, unitDefID, team, damage, paralyzer, weaponDefID, projectileID, attackerID, attackerDefID) ... end }`. Supported: `UnitCreated`, `UnitFinished`, `UnitDamaged`, `UnitDestroyed` (`harness.SYNCED_CALLINS`; the game's gadget forwards them). Every loaded scenario's handlers run for the whole run, so react only to units your scenario made. Errors in handlers are logged and flagged in the report.

Per-unit checks should measure the unit, not the map: BAR's `workbench/lib/arena.lua` levels the terrain (`prepare(height)`; a negative height floods it for ships), removes features, turns global LOS off and makes pre-existing units hold fire; `clear` removes everything the scenario made, including projectiles still in flight, and re-levels the ground (craters from earlier batches block flat-trajectory weapons); `arena.sweep(ctx)` clears twice around a pause for delayed death effects (`synced = { prepare = arena.prepare, clear = arena.clear }`). Test units that need no player interaction belong to a team the local player does not control, so no player widget gives them orders. `workbench/lib/techtree.lua` lists the units players can actually build. BAR's `workbench/lib/movement.lua` and `workbench/lib/weapons.lua` generate per-unit-type checks.

`ctx.call` never raises: Lua 5.1 cannot yield inside `pcall`, so check its second return value instead. Read enemy or hidden state through `ctx.call` rather than unsynced Lua, which only sees what the local player can see.

Engine API (`Spring.Workbench`, unsynced): `IsActive`, `GetPattern`, `BeginScenario`, `EndScenario`, `BeginWindow`, `EndWindow`, `Check`, `Error`, `RunError`, `SetFrameStall(ms)`, `FinishRun`. Synced Lua only gets `IsActive` and `GetPattern`.

A game opts in with a widget and a gadget that `VFS.Include("workbench/harness.lua")` when `Spring.Workbench and Spring.Workbench.IsActive()` (see BAR's `dbg_workbench.lua`).

## Tests

CI (`.github/workflows/workbench.yml`) runs on every change to the workbench: runner and report tests, a Lua 5.1 parse of the harness and engine scenarios, and the C++ unit tests (`test_Workbench`, `test_KeyInput`) in the official build image. BAR's own luacheck CI covers the game's scenarios.

`--jobs N` runs a cell's scenarios in N engine instances with private write dirs and merges the results. Measure before relying on it: on a 14-thread desktop 3 jobs were slower than 1 (315 s against 219 s), because each instance's sim already uses every core.

```bash
python -m unittest discover -s tools/workbench/tests          # runner + report
wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-test.sh   # C++ core (Catch2)
```
