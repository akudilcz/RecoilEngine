# Workbench findings

Bugs and oddities the Recoil Workbench has surfaced, newest first. Each entry says how it was found and what happened to it.

| Date | Area | Finding | How found | Status |
|---|---|---|---|---|
| 2026-09-24 | Engine (upstream) | `CGlobalRendering::CalcGLDeltaTime` busy-waits on `GL_QUERY_RESULT_AVAILABLE`; if the query was never issued it spins forever. The upstream-based workbench baseline hung in pregame this way. | `sync_repro` on the baseline engine (hang stack symbolized) | Fixed on our branch (non-blocking query); upstream still has it: worth a PR |
| 2026-09-24 | Workbench | A hung main thread can't run the workbench watchdog; the runner only noticed at its hard timeout. | Same hang | Fixed: the engine's hang detector ends workbench runs with exit 2 |
| 2026-09-24 | BAR balance | `corstorm` (range 475) did no damage to a stationary structure at 428 (90% of range) in 12 s. Its missile has `weaponvelocity` 190 and `weapontimer` 2 s, about 380 elmos of guided flight, so it likely loses guidance before reaching targets near its stated range. | `weapon_range` scenario | **Open**: needs a BAR balance decision |
| 2026-09-24 | Engine memory | The engine sat at ~8.1 GB working set during `weapon_range` (stable over 30 s, so not a runaway leak). | Observed during a run | Open: add per-window memory metric to the workbench |
| 2026-09-24 | Engine (our perf patch) | Use-after-free crash in `ILosType::UpdateHeightMapSynced` (`LosHandler.cpp:683`). The LOS-cache "lazy deletion" optimisation left stale pointers in `losCache`; a reactivated-then-recached instance appeared twice, the first copy was deleted, and the next heightmap change (a building flattening ground) dereferenced the second. | `weapon_range` scenario (engine crash, symbolized with `tools/workbench/symbolize.sh`) | Fixed: reverted (9aad7dca59) |
| 2026-09-23 | Workbench | A crash dialog blocked the engine until the runner's hard timeout. | First crashing run | Fixed: no dialogs in workbench mode, crash recorded against the scenario |
| 2026-09-23 | Workbench | A scenario that ended the game (destroyed all commanders) made the run exit 0 with no results. | First `weapon_range` run | Fixed: engine exit before `FinishRun` is exit code 2 |
| 2026-09-23 | BAR + engine | `gui_gameinfo`, `gui_keybind_info`, `gui_teamstats`, `widget_selector` failed to load: in raw-first VFS mode the engine's `LuaUI/Headers/keysym.h.lua` (which uses `setmetatable` since RecoilEngine #3105) shadowed the game's copy inside an empty include env. | Infolog scan in the first real run | Fixed in BAR (load via `VFS.ZIP`); the runner now flags widget load failures on every run |
| 2026-09-23 | Workbench | `--config` profiles were overwritten by the engine after each run. | First run | Fixed: each cell gets its own copy |
