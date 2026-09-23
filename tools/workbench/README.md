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

# 2. run scenarios and open the report
python tools/workbench/run.py --engine dev=C:/Workspace/bar/engine-dev/spring.exe \
    --data-dir C:/Workspace/bar/data --only "api_selftest,render_baseline,mass_move_500,weapon_range"
```

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

`ctx` API: `waitFrames(n)`, `waitSimFrames(n)`, `waitSeconds(s)`, `waitUntil(pred, timeoutSec) -> bool`, `window(name, fn)`, `check(name, pass, detail)`, `synced(fn, ...)`, `log(msg)`.

Engine API (`Spring.Workbench`, unsynced): `IsActive`, `GetPattern`, `BeginScenario`, `EndScenario`, `BeginWindow`, `EndWindow`, `Check`, `Error`, `RunError`, `SetFrameStall(ms)`, `FinishRun`. Synced Lua only gets `IsActive` and `GetPattern`.

A game opts in with a widget and a gadget that `VFS.Include("workbench/harness.lua")` when `Spring.Workbench and Spring.Workbench.IsActive()` (see BAR's `dbg_workbench.lua`).

## Tests

```bash
python -m unittest discover -s tools/workbench/tests          # runner + report
wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-test.sh   # C++ core (Catch2)
```
