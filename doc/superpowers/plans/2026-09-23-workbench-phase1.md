# Recoil Workbench — Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An engine `--workbench` mode that runs Lua scenarios in the real game, measures frame/draw/GPU/sim time and records checks, writes JSON results, and exits with a meaningful code; plus a host runner and a minimal comparison report; plus the first engine and BAR scenarios.

**Architecture:** A small C++ core in `rts/System/Workbench/` (pure stats/results logic + a controller singleton hooked into `CGame::Draw`, `CGame::Update` and the NETMSG_NEWFRAME handler), exposed to Lua as `Spring.Workbench`. A Lua harness shipped in `springcontent` (`workbench/harness.lua`) loads scenarios from the game's and engine's `workbench/scenarios/` folders. Python tools in `tools/workbench/` run matrices of builds × profiles × scenarios and render a report.

**Tech Stack:** C++20 (engine), jsoncpp (already linked as `prd::jsoncpp`), Catch2 (`test/`), Lua 5.1 (engine Lua env), Python 3 stdlib only.

**Spec:** `doc/superpowers/specs/2026-09-23-workbench-design.md`

## Global Constraints

- No behaviour change in the simulation: workbench code only observes; synced Lua functions must not write synced state.
- Measurement is allocation-free per frame after warm-up (reserve sample vectors when a window begins); GPU timing never blocks (reuse `CGlobalRendering::CalcGLDeltaTime`, which is non-blocking).
- Exit codes: `0` all checks passed, `1` at least one check failed, `2` error/timeout/no scenarios (use `spring::EXIT_CODE_SUCCESS` (0), `spring::EXIT_CODE_FAILURE` (1), and `2` literally as `WORKBENCH_EXIT_ERROR`).
- Sync checksum use must be guarded by `#ifdef SYNCCHECK` (the `.cpp` is compiled out otherwise).
- Do not use `DEFINE_int32_EX` (broken macro in `GflagsExt.h`); use plain `DEFINE_int32`.
- New engine files go in `rts/System/CMakeLists.txt`'s `sources_engine_System_common` list (Lua file in `rts/Lua/CMakeLists.txt`), new springcontent files must be appended to the explicit `FILES` list in `cont/base/springcontent/CMakeLists.txt`.
- Line endings: engine sources are CRLF in this checkout; match the file you edit.
- Builds: `wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh [targets]` (Windows targets); unit tests build with the linux docker target (see Task 1).

## Review Focus

1. A scenario that errors (Lua error) or never calls `Finish()` mid-window → the harness records an error check, closes the window, writes the scenario JSON, and moves on; the watchdog still ends the run.
2. The game window is minimised/inactive (`CGame::Draw` returns early) → the watchdog in `CGame::Update` still fires; draw windows simply have fewer samples.
3. `--workbench <pattern>` matching no scenario → run exits `2` with `run.json` `error: "no scenarios matched ..."`, never a silent `0`.
4. Engine built without `SYNCCHECK` → results omit the checksum (`sync.available=false`), nothing crashes.
5. Runner: engine crashes or hangs → `run.py` kills it at the timeout, records the cell as `error` with the last 40 infolog lines, and continues the matrix.

---

## File Structure

Engine (`C:\Workspace\bar\RecoilEngine`):
- Create `rts/System/Workbench/WorkbenchStats.{h,cpp}` — percentile/mean summary of a float sample vector (pure).
- Create `rts/System/Workbench/WorkbenchResults.{h,cpp}` — plain structs (`WorkbenchWindow`, `WorkbenchCheck`, `WorkbenchScenario`, `WorkbenchRun`) → `Json::Value` → file (pure except file I/O).
- Create `rts/System/Workbench/Workbench.{h,cpp}` — controller singleton: options, lifecycle, sampling hooks, watchdog, exit.
- Create `rts/Lua/LuaWorkbench.{h,cpp}` — `Spring.Workbench` sub-table.
- Modify `rts/System/SpringApp.cpp` — flags + `workbench.Configure(...)`.
- Modify `rts/Game/Game.cpp` — draw-end sample + `workbench.Update()` watchdog.
- Modify `rts/Net/NetCommands.cpp` — sim-frame sample after `SimFrame()`.
- Modify `rts/Lua/LuaUI.cpp`, `rts/Lua/LuaHandleSynced.cpp` — push `Spring.Workbench`.
- Modify `rts/System/CMakeLists.txt`, `rts/Lua/CMakeLists.txt`.
- Create `cont/base/springcontent/workbench/harness.lua`, `cont/base/springcontent/workbench/scenarios/render_baseline.lua`; modify `cont/base/springcontent/CMakeLists.txt`.
- Create `test/engine/System/testWorkbench.cpp`; modify `test/CMakeLists.txt`.
- Create `tools/workbench/run.py`, `tools/workbench/report.py`, `tools/workbench/profiles/default.cfg`, `tools/workbench/templates/startscript.txt`, `tools/workbench/tests/test_run.py`, `tools/workbench/tests/test_report.py`, `tools/workbench/README.md`.

Game (`C:\Workspace\bar\Beyond-All-Reason`):
- Create `luaui/Widgets/dbg_workbench.lua`, `luarules/gadgets/dbg_workbench.lua` — opt-in loaders.
- Create `workbench/scenarios/mass_move_500.lua`, `workbench/scenarios/weapon_range.lua`.

---

### Task 1: Stats and results core (pure C++ + unit tests)

**Files:**
- Create: `rts/System/Workbench/WorkbenchStats.h`, `rts/System/Workbench/WorkbenchStats.cpp`
- Create: `rts/System/Workbench/WorkbenchResults.h`, `rts/System/Workbench/WorkbenchResults.cpp`
- Create: `test/engine/System/testWorkbench.cpp`
- Modify: `test/CMakeLists.txt` (add block after the Float3 block, ~line 196)
- Modify: `rts/System/CMakeLists.txt` (append two entries to `sources_engine_System_common`, next to `"${CMAKE_CURRENT_SOURCE_DIR}/TimeProfiler.cpp"` at ~line 72)

**Interfaces:**
- Produces:
  - `struct WorkbenchSummary { size_t count; float mean, p50, p95, p99, max; };`
  - `WorkbenchSummary SummarizeSamples(std::vector<float> samples);` (by value: sorts its copy; empty → all zero)
  - `struct WorkbenchWindow { std::string name; std::vector<float> frameMs, drawMs, gpuMs, simMs; int simFrames = 0; };`
  - `struct WorkbenchCheck { std::string name; bool pass; std::string detail; };`
  - `struct WorkbenchScenario { std::string name; std::vector<WorkbenchWindow> windows; std::vector<WorkbenchCheck> checks; std::string error; };`
  - `struct WorkbenchRunInfo { std::string engineVersion, gameName, pattern, profile; };`
  - `Json::Value ScenarioToJson(const WorkbenchScenario&, const WorkbenchRunInfo&);`
  - `Json::Value RunToJson(const std::vector<std::string>& scenarioNames, int exitCode, const std::string& error, const WorkbenchRunInfo&, bool syncAvailable, int lastSimFrame, unsigned lastChecksum);`
  - `bool WriteJsonFile(const std::string& path, const Json::Value&);`
  - `int DecideExitCode(const std::vector<WorkbenchScenario>&, const std::string& runError);` → 0/1/2

- [ ] **Step 1: Write the failing tests**

`test/engine/System/testWorkbench.cpp`:
```cpp
/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "System/Workbench/WorkbenchStats.h"
#include "System/Workbench/WorkbenchResults.h"

#include <catch_amalgamated.hpp>

TEST_CASE("SummarizeSamples on empty input is all zero")
{
	const WorkbenchSummary s = SummarizeSamples({});
	CHECK(s.count == 0);
	CHECK(s.mean == 0.0f);
	CHECK(s.p99 == 0.0f);
}

TEST_CASE("SummarizeSamples nearest-rank percentiles")
{
	std::vector<float> v;
	for (int i = 1; i <= 100; ++i) v.push_back(float(i)); // 1..100, unsorted order irrelevant
	std::reverse(v.begin(), v.end());

	const WorkbenchSummary s = SummarizeSamples(v);
	CHECK(s.count == 100);
	CHECK(s.mean == Catch::Approx(50.5f));
	CHECK(s.p50 == 50.0f);
	CHECK(s.p95 == 95.0f);
	CHECK(s.p99 == 99.0f);
	CHECK(s.max == 100.0f);
}

TEST_CASE("DecideExitCode")
{
	WorkbenchScenario ok{"a", {}, {{"c1", true, ""}}, ""};
	WorkbenchScenario bad{"b", {}, {{"c2", false, "expected 1 got 2"}}, ""};
	WorkbenchScenario err{"c", {}, {}, "lua error"};

	CHECK(DecideExitCode({ok}, "") == 0);
	CHECK(DecideExitCode({ok, bad}, "") == 1);
	CHECK(DecideExitCode({ok, err}, "") == 2);
	CHECK(DecideExitCode({}, "") == 2);            // nothing ran is an error, never a pass
	CHECK(DecideExitCode({ok}, "timeout") == 2);
}

TEST_CASE("ScenarioToJson layout")
{
	WorkbenchWindow w;
	w.name = "moving";
	w.frameMs = {10.0f, 20.0f};
	w.simFrames = 3;
	WorkbenchScenario sc{"mass_move_500", {w}, {{"arrived", true, "480/500"}}, ""};
	WorkbenchRunInfo info{"2026.09-test", "BAR test", "mass_*", "default"};

	const Json::Value j = ScenarioToJson(sc, info);
	CHECK(j["scenario"].asString() == "mass_move_500");
	CHECK(j["engine"]["version"].asString() == "2026.09-test");
	CHECK(j["windows"][0]["name"].asString() == "moving");
	CHECK(j["windows"][0]["frameTimeMs"]["count"].asUInt() == 2);
	CHECK(j["windows"][0]["frameTimeMs"]["max"].asFloat() == 20.0f);
	CHECK(j["windows"][0]["simFrames"].asInt() == 3);
	CHECK(j["checks"][0]["pass"].asBool() == true);
	CHECK(j["checks"][0]["detail"].asString() == "480/500");
	CHECK(j["error"].isNull());
}
```

`test/CMakeLists.txt` block (after the Float3 block):
```cmake
################################################################################
### Workbench
	set(test_name Workbench)
	set(test_src
			"${CMAKE_CURRENT_SOURCE_DIR}/engine/System/testWorkbench.cpp"
			"${ENGINE_SOURCE_DIR}/System/Workbench/WorkbenchStats.cpp"
			"${ENGINE_SOURCE_DIR}/System/Workbench/WorkbenchResults.cpp"
			${test_Common_sources}
		)
	set(test_libs
			prd::jsoncpp
		)
	add_spring_test(${test_name} "${test_src}" "${test_libs}" "")
```

- [ ] **Step 2: Run the tests to verify they fail**

Run (WSL): `cd ~/bar/RecoilEngine && rsync -a --delete --exclude 'build-*/' --exclude '.cache/' /mnt/c/Workspace/bar/RecoilEngine/ ./ && docker-build-v2/build.sh linux -t test_Workbench`
Expected: compile error, `System/Workbench/WorkbenchStats.h: No such file or directory`.
(First linux configure takes a while; later runs use `--compile linux -t test_Workbench`.)

- [ ] **Step 3: Implement**

`rts/System/Workbench/WorkbenchStats.h`:
```cpp
/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */
#pragma once

#include <cstddef>
#include <vector>

struct WorkbenchSummary {
	size_t count = 0;
	float mean = 0.0f;
	float p50 = 0.0f;
	float p95 = 0.0f;
	float p99 = 0.0f;
	float max = 0.0f;
};

// nearest-rank percentiles; takes a copy because it sorts
WorkbenchSummary SummarizeSamples(std::vector<float> samples);
```

`rts/System/Workbench/WorkbenchStats.cpp`:
```cpp
/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "WorkbenchStats.h"

#include <algorithm>
#include <cmath>
#include <numeric>

static float NearestRank(const std::vector<float>& sorted, float pct)
{
	const size_t n = sorted.size();
	const size_t rank = static_cast<size_t>(std::ceil(pct / 100.0f * n));
	return sorted[std::clamp<size_t>(rank, 1, n) - 1];
}

WorkbenchSummary SummarizeSamples(std::vector<float> samples)
{
	WorkbenchSummary s;
	if (samples.empty())
		return s;

	std::sort(samples.begin(), samples.end());
	s.count = samples.size();
	s.mean = static_cast<float>(std::accumulate(samples.begin(), samples.end(), 0.0) / s.count);
	s.p50 = NearestRank(samples, 50.0f);
	s.p95 = NearestRank(samples, 95.0f);
	s.p99 = NearestRank(samples, 99.0f);
	s.max = samples.back();
	return s;
}
```

`rts/System/Workbench/WorkbenchResults.h`:
```cpp
/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */
#pragma once

#include <string>
#include <vector>

#include <json/json.h>

struct WorkbenchWindow {
	std::string name;
	std::vector<float> frameMs; // full frame-to-frame time, one per draw frame
	std::vector<float> drawMs;  // CPU time spent in CGame::Draw
	std::vector<float> gpuMs;   // GPU time of the previous frame (async timer query)
	std::vector<float> simMs;   // CPU time per SimFrame
	int simFrames = 0;
};

struct WorkbenchCheck {
	std::string name;
	bool pass = false;
	std::string detail;
};

struct WorkbenchScenario {
	std::string name;
	std::vector<WorkbenchWindow> windows;
	std::vector<WorkbenchCheck> checks;
	std::string error;
};

struct WorkbenchRunInfo {
	std::string engineVersion;
	std::string gameName;
	std::string pattern;
	std::string profile;
};

static constexpr int WORKBENCH_EXIT_ERROR = 2;

Json::Value ScenarioToJson(const WorkbenchScenario& scenario, const WorkbenchRunInfo& info);
Json::Value RunToJson(const std::vector<std::string>& scenarioNames, int exitCode, const std::string& error,
                      const WorkbenchRunInfo& info, bool syncAvailable, int lastSimFrame, unsigned lastChecksum);
bool WriteJsonFile(const std::string& path, const Json::Value& value);
int DecideExitCode(const std::vector<WorkbenchScenario>& scenarios, const std::string& runError);
```

`rts/System/Workbench/WorkbenchResults.cpp`:
```cpp
/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "WorkbenchResults.h"
#include "WorkbenchStats.h"

#include <cstdio>
#include <fstream>
#include <memory>

static Json::Value SummaryToJson(const std::vector<float>& samples)
{
	const WorkbenchSummary s = SummarizeSamples(samples);
	Json::Value j;
	j["count"] = Json::UInt64(s.count);
	j["mean"] = s.mean;
	j["p50"] = s.p50;
	j["p95"] = s.p95;
	j["p99"] = s.p99;
	j["max"] = s.max;
	return j;
}

static Json::Value InfoToJson(const WorkbenchRunInfo& info, Json::Value& root)
{
	root["engine"]["version"] = info.engineVersion;
	root["game"]["name"] = info.gameName;
	root["pattern"] = info.pattern;
	root["profile"] = info.profile;
	return root;
}

Json::Value ScenarioToJson(const WorkbenchScenario& scenario, const WorkbenchRunInfo& info)
{
	Json::Value root;
	root["scenario"] = scenario.name;
	InfoToJson(info, root);

	root["windows"] = Json::arrayValue;
	for (const WorkbenchWindow& w: scenario.windows) {
		Json::Value jw;
		jw["name"] = w.name;
		jw["frameTimeMs"] = SummaryToJson(w.frameMs);
		jw["drawTimeMs"] = SummaryToJson(w.drawMs);
		jw["gpuTimeMs"] = SummaryToJson(w.gpuMs);
		jw["simTimeMs"] = SummaryToJson(w.simMs);
		jw["simFrames"] = w.simFrames;
		root["windows"].append(jw);
	}

	root["checks"] = Json::arrayValue;
	for (const WorkbenchCheck& c: scenario.checks) {
		Json::Value jc;
		jc["name"] = c.name;
		jc["pass"] = c.pass;
		jc["detail"] = c.detail;
		root["checks"].append(jc);
	}

	if (!scenario.error.empty())
		root["error"] = scenario.error;

	return root;
}

Json::Value RunToJson(const std::vector<std::string>& scenarioNames, int exitCode, const std::string& error,
                      const WorkbenchRunInfo& info, bool syncAvailable, int lastSimFrame, unsigned lastChecksum)
{
	Json::Value root;
	InfoToJson(info, root);
	root["exitCode"] = exitCode;
	root["scenarios"] = Json::arrayValue;
	for (const std::string& n: scenarioNames)
		root["scenarios"].append(n);
	if (!error.empty())
		root["error"] = error;
	root["sync"]["available"] = syncAvailable;
	root["sync"]["lastFrame"] = lastSimFrame;
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%08x", lastChecksum);
	root["sync"]["lastChecksum"] = std::string(buf);
	return root;
}

bool WriteJsonFile(const std::string& path, const Json::Value& value)
{
	Json::StreamWriterBuilder builder;
	builder["indentation"] = "  ";
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
	writer->write(value, &out);
	out << "\n";
	return static_cast<bool>(out);
}

int DecideExitCode(const std::vector<WorkbenchScenario>& scenarios, const std::string& runError)
{
	if (!runError.empty() || scenarios.empty())
		return WORKBENCH_EXIT_ERROR;

	bool anyFailed = false;
	for (const WorkbenchScenario& s: scenarios) {
		if (!s.error.empty())
			return WORKBENCH_EXIT_ERROR;
		for (const WorkbenchCheck& c: s.checks)
			anyFailed |= !c.pass;
	}
	return anyFailed ? 1 : 0;
}
```

Add to `rts/System/CMakeLists.txt` in `sources_engine_System_common`:
```cmake
		"${CMAKE_CURRENT_SOURCE_DIR}/Workbench/WorkbenchStats.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Workbench/WorkbenchResults.cpp"
```

- [ ] **Step 4: Run the tests to verify they pass**

Run (WSL): `cd ~/bar/RecoilEngine && rsync -a --delete --exclude 'build-*/' --exclude '.cache/' /mnt/c/Workspace/bar/RecoilEngine/ ./ && docker-build-v2/build.sh --compile linux -t test_Workbench && ./build-amd64-linux/test/test_Workbench`
Expected: `All tests passed (… assertions in 4 test cases)`.

- [ ] **Step 5: Commit**

```bash
git add rts/System/Workbench test/engine/System/testWorkbench.cpp test/CMakeLists.txt rts/System/CMakeLists.txt
git commit -m "workbench: stats and JSON results core with tests"
```

---

### Task 2: Workbench controller, CLI flags and engine hooks

**Files:**
- Create: `rts/System/Workbench/Workbench.h`, `rts/System/Workbench/Workbench.cpp`
- Modify: `rts/System/CMakeLists.txt` (add `Workbench/Workbench.cpp`)
- Modify: `rts/System/SpringApp.cpp` (flags near line 163; configure in `ParseCmdLine` after the `write_dir` handling ~line 494)
- Modify: `rts/Game/Game.cpp` (`CGame::Update` start ~line 1178; `CGame::Draw` end ~lines 1562-1569)
- Modify: `rts/Net/NetCommands.cpp` (NETMSG_NEWFRAME, ~lines 618-639)
- Test: `test/engine/System/testWorkbench.cpp` (controller window logic)
- Modify: `test/CMakeLists.txt` (add `Workbench.cpp` to the Workbench test sources)

**Interfaces:**
- Consumes: Task 1 structs and functions.
- Produces (`class CWorkbench`, global `extern CWorkbench workbench;`):
  - `void Configure(const std::string& pattern, const std::string& outDir, int timeoutSec, const std::string& profile);`
  - `bool IsActive() const;` `const std::string& GetPattern() const;`
  - `void BeginScenario(const std::string& name);` `void EndScenario();`
  - `void BeginWindow(const std::string& name);` `void EndWindow();`
  - `void AddCheck(const std::string& name, bool pass, const std::string& detail);`
  - `void SetScenarioError(const std::string& msg);` `void SetRunError(const std::string& msg);`
  - `void SetFrameStall(int ms);`
  - `void FinishRun();` — writes `run.json`, sets `spring::exitCode`, `gu->globalQuit = true`.
  - Hooks: `void OnDrawFrame(float frameMs, float drawMs, float gpuMs);` `void OnSimFrame(int frameNum, float simMs, unsigned checksum, bool checksumValid);` `void Update(float nowSec);` (watchdog).
  - Test seam: `void SetClockForTest(std::function<float()> nowSec);` and `bool IsFinished() const;` `int GetExitCode() const;` `const std::vector<WorkbenchScenario>& GetScenarios() const;` `bool writeFiles` public member (false in tests).

- [ ] **Step 1: Write the failing tests** (append to `testWorkbench.cpp`)

```cpp
#include "System/Workbench/Workbench.h"

TEST_CASE("Workbench windows collect only while open")
{
	CWorkbench wb;
	wb.writeFiles = false;
	wb.Configure("mass_*", "", 60, "default");
	REQUIRE(wb.IsActive());

	wb.BeginScenario("s1");
	wb.OnDrawFrame(16.0f, 5.0f, 4.0f);      // outside any window: ignored
	wb.BeginWindow("w");
	wb.OnDrawFrame(10.0f, 3.0f, 2.0f);
	wb.OnDrawFrame(20.0f, 6.0f, 4.0f);
	wb.OnSimFrame(1, 1.5f, 0xabcu, true);
	wb.EndWindow();
	wb.OnDrawFrame(99.0f, 9.0f, 9.0f);      // after window: ignored
	wb.AddCheck("c", true, "");
	wb.EndScenario();

	const auto& sc = wb.GetScenarios();
	REQUIRE(sc.size() == 1);
	REQUIRE(sc[0].windows.size() == 1);
	CHECK(sc[0].windows[0].frameMs.size() == 2);
	CHECK(sc[0].windows[0].simFrames == 1);
}

TEST_CASE("Ending a scenario with an open window closes it")
{
	CWorkbench wb;
	wb.writeFiles = false;
	wb.Configure("x", "", 60, "default");
	wb.BeginScenario("s");
	wb.BeginWindow("w");
	wb.OnDrawFrame(10.0f, 1.0f, 1.0f);
	wb.EndScenario();
	CHECK(wb.GetScenarios()[0].windows.size() == 1);
}

TEST_CASE("Watchdog ends the run with exit code 2")
{
	float now = 0.0f;
	CWorkbench wb;
	wb.writeFiles = false;
	wb.SetClockForTest([&]() { return now; });
	wb.Configure("x", "", 5, "default");
	wb.BeginScenario("s");
	now = 4.0f; wb.Update(now);
	CHECK_FALSE(wb.IsFinished());
	now = 6.0f; wb.Update(now);
	CHECK(wb.IsFinished());
	CHECK(wb.GetExitCode() == 2);
}

TEST_CASE("FinishRun with no scenarios is an error")
{
	CWorkbench wb;
	wb.writeFiles = false;
	wb.Configure("nothing_*", "", 60, "default");
	wb.SetRunError("no scenarios matched nothing_*");
	wb.FinishRun();
	CHECK(wb.GetExitCode() == 2);
}
```

- [ ] **Step 2: Run to verify failure**

Run (WSL): same command as Task 1 Step 4 after adding `"${ENGINE_SOURCE_DIR}/System/Workbench/Workbench.cpp"` to the test sources.
Expected: compile error `Workbench.h: No such file`.

- [ ] **Step 3: Implement the controller**

`rts/System/Workbench/Workbench.h`:
```cpp
/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */
#pragma once

#include "WorkbenchResults.h"

#include <functional>
#include <string>
#include <vector>

class CWorkbench {
public:
	void Configure(const std::string& pattern, const std::string& outDir, int timeoutSec, const std::string& profile);
	bool IsActive() const { return active; }
	bool IsFinished() const { return finished; }
	int GetExitCode() const { return exitCode; }
	const std::string& GetPattern() const { return info.pattern; }
	const std::vector<WorkbenchScenario>& GetScenarios() const { return scenarios; }

	void BeginScenario(const std::string& name);
	void EndScenario();
	void BeginWindow(const std::string& name);
	void EndWindow();
	void AddCheck(const std::string& name, bool pass, const std::string& detail);
	void SetScenarioError(const std::string& msg);
	void SetRunError(const std::string& msg);
	void SetFrameStall(int ms) { frameStallMs = ms; }
	int GetFrameStall() const { return frameStallMs; }
	void FinishRun();

	void OnDrawFrame(float frameMs, float drawMs, float gpuMs);
	void OnSimFrame(int frameNum, float simMs, unsigned checksum, bool checksumValid);
	void Update(float nowSec);

	void SetEngineInfo(const std::string& engineVersion, const std::string& gameName);
	void SetClockForTest(std::function<float()> clock) { nowFunc = std::move(clock); }

	bool writeFiles = true;

private:
	WorkbenchScenario* Current() { return inScenario ? &scenarios.back() : nullptr; }
	WorkbenchWindow* CurrentWindow();
	void WriteScenario(const WorkbenchScenario& sc);

	bool active = false;
	bool finished = false;
	bool inScenario = false;
	bool inWindow = false;
	int exitCode = 0;
	int timeoutSec = 0;
	int frameStallMs = 0;
	float startSec = 0.0f;
	int lastSimFrame = -1;
	unsigned lastChecksum = 0;
	bool syncAvailable = false;
	std::string outDir;
	std::string runError;
	WorkbenchRunInfo info;
	std::vector<WorkbenchScenario> scenarios;
	std::function<float()> nowFunc;
};

extern CWorkbench workbench;
```

`rts/System/Workbench/Workbench.cpp`:
```cpp
/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "Workbench.h"

#include "System/Log/ILog.h"
#include "System/Misc/SpringTime.h"
#include "System/SpringExitCode.h"
#ifndef UNIT_TEST
#include "Game/GlobalUnsynced.h"
#endif

#include <filesystem>

CWorkbench workbench;

static constexpr size_t WINDOW_RESERVE = 4096;

void CWorkbench::Configure(const std::string& pattern, const std::string& dir, int timeout, const std::string& profile)
{
	active = !pattern.empty();
	info.pattern = pattern;
	info.profile = profile;
	outDir = dir;
	timeoutSec = timeout;
	if (!nowFunc)
		nowFunc = []() { return spring_gettime().toSecsf(); };
	startSec = nowFunc();

	if (active && writeFiles && !outDir.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(outDir, ec);
	}
	if (active)
		LOG("[Workbench] active: pattern=\"%s\" out=\"%s\" timeout=%ds", pattern.c_str(), outDir.c_str(), timeout);
}

void CWorkbench::SetEngineInfo(const std::string& engineVersion, const std::string& gameName)
{
	info.engineVersion = engineVersion;
	info.gameName = gameName;
}

WorkbenchWindow* CWorkbench::CurrentWindow()
{
	WorkbenchScenario* sc = Current();
	if (sc == nullptr || !inWindow || sc->windows.empty())
		return nullptr;
	return &sc->windows.back();
}

void CWorkbench::BeginScenario(const std::string& name)
{
	if (!active || finished)
		return;
	if (inScenario)
		EndScenario();
	scenarios.push_back({name, {}, {}, ""});
	inScenario = true;
	LOG("[Workbench] scenario %s", name.c_str());
}

void CWorkbench::EndScenario()
{
	if (!inScenario)
		return;
	EndWindow();
	inScenario = false;
	WriteScenario(scenarios.back());
}

void CWorkbench::BeginWindow(const std::string& name)
{
	WorkbenchScenario* sc = Current();
	if (sc == nullptr)
		return;
	EndWindow();
	WorkbenchWindow w;
	w.name = name;
	w.frameMs.reserve(WINDOW_RESERVE);
	w.drawMs.reserve(WINDOW_RESERVE);
	w.gpuMs.reserve(WINDOW_RESERVE);
	w.simMs.reserve(WINDOW_RESERVE);
	sc->windows.push_back(std::move(w));
	inWindow = true;
}

void CWorkbench::EndWindow()
{
	inWindow = false;
}

void CWorkbench::AddCheck(const std::string& name, bool pass, const std::string& detail)
{
	if (WorkbenchScenario* sc = Current(); sc != nullptr) {
		sc->checks.push_back({name, pass, detail});
		if (!pass)
			LOG_L(L_WARNING, "[Workbench] FAIL %s/%s: %s", sc->name.c_str(), name.c_str(), detail.c_str());
	}
}

void CWorkbench::SetScenarioError(const std::string& msg)
{
	if (WorkbenchScenario* sc = Current(); sc != nullptr) {
		sc->error = msg;
		LOG_L(L_ERROR, "[Workbench] ERROR %s: %s", sc->name.c_str(), msg.c_str());
	}
}

void CWorkbench::SetRunError(const std::string& msg)
{
	runError = msg;
	LOG_L(L_ERROR, "[Workbench] run error: %s", msg.c_str());
}

void CWorkbench::OnDrawFrame(float frameMs, float drawMs, float gpuMs)
{
	if (WorkbenchWindow* w = CurrentWindow(); w != nullptr) {
		w->frameMs.push_back(frameMs);
		w->drawMs.push_back(drawMs);
		if (gpuMs > 0.0f)
			w->gpuMs.push_back(gpuMs);
	}
}

void CWorkbench::OnSimFrame(int frameNum, float simMs, unsigned checksum, bool checksumValid)
{
	lastSimFrame = frameNum;
	lastChecksum = checksum;
	syncAvailable = checksumValid;
	if (WorkbenchWindow* w = CurrentWindow(); w != nullptr) {
		w->simMs.push_back(simMs);
		w->simFrames += 1;
	}
}

void CWorkbench::Update(float nowSec)
{
	if (!active || finished || timeoutSec <= 0)
		return;
	if ((nowSec - startSec) <= float(timeoutSec))
		return;
	SetScenarioError("watchdog timeout");
	SetRunError("timeout after " + std::to_string(timeoutSec) + "s");
	FinishRun();
}

void CWorkbench::WriteScenario(const WorkbenchScenario& sc)
{
	if (!writeFiles || outDir.empty())
		return;
	const std::string path = outDir + "/" + sc.name + ".json";
	if (!WriteJsonFile(path, ScenarioToJson(sc, info)))
		LOG_L(L_ERROR, "[Workbench] could not write %s", path.c_str());
}

void CWorkbench::FinishRun()
{
	if (finished)
		return;
	if (inScenario)
		EndScenario();

	exitCode = DecideExitCode(scenarios, runError);
	finished = true;

	std::vector<std::string> names;
	for (const WorkbenchScenario& s: scenarios)
		names.push_back(s.name);

	if (writeFiles && !outDir.empty())
		WriteJsonFile(outDir + "/run.json", RunToJson(names, exitCode, runError, info, syncAvailable, lastSimFrame, lastChecksum));

	LOG("[Workbench] finished: %zu scenarios, exit code %d", scenarios.size(), exitCode);

#ifndef UNIT_TEST
	spring::exitCode = exitCode;
	gu->globalQuit = true;
#endif
}
```

Note: the test target defines `-DUNIT_TEST` globally (test/CMakeLists.txt line 34), so the `gu`/`exitCode` side effects are compiled out in tests. Logging comes from `test_Common_sources`; the default clock uses `spring_gettime`, so add `"${ENGINE_SOURCE_DIR}/System/Misc/SpringTime.cpp"` (and `${sources_engine_System_Threading}` if the linker asks for it, as the Float3 test does) to the Workbench test sources in this task, next to `Workbench.cpp`.

- [ ] **Step 4: Wire the engine**

`rts/System/SpringApp.cpp` — flags (after `onlyLocal`, ~line 163):
```cpp
DEFINE_string   (workbench,                                "",    "Run Recoil Workbench scenarios matching this pattern, then quit");
DEFINE_string_EX(workbench_out,      "workbench-out",      "",    "Workbench results directory (default <write-dir>/workbench)");
DEFINE_int32    (workbench_timeout,                        1800,  "Workbench watchdog in seconds for the whole run");
DEFINE_string_EX(workbench_profile,  "workbench-profile",  "default", "Name of the settings profile, recorded in results");
```
In `SpringApp::ParseCmdLine`, after the `write_dir` handling (~line 494), add `#include "System/Workbench/Workbench.h"` at the top of the file and:
```cpp
	if (!FLAGS_workbench.empty()) {
		std::string out = FLAGS_workbench_out;
		if (out.empty())
			out = (FLAGS_write_dir.empty() ? std::string(".") : FLAGS_write_dir) + "/workbench";
		workbench.Configure(FLAGS_workbench, out, FLAGS_workbench_timeout, FLAGS_workbench_profile);
	}
```

`rts/Game/Game.cpp` — at the top of `CGame::Update()` (line ~1178, first statement) add:
```cpp
	workbench.Update(spring_gettime().toSecsf());
```
At the end of `CGame::Draw()` right after `gu->avgDrawFrameTime = ...` (~line 1564), before the `FRAME_END` timestamp, add:
```cpp
	if (workbench.IsActive()) {
		const float gpuMs = globalRendering->CalcGLDeltaTime(CGlobalRendering::FRAME_REF_TIME_QUERY_IDX, CGlobalRendering::FRAME_END_TIME_QUERY_IDX) * 1e-6f;
		workbench.OnDrawFrame(globalRendering->lastFrameTime, currentFrameDrawTime.toMilliSecsf(), gpuMs);
		if (const int stall = workbench.GetFrameStall(); stall > 0)
			spring_sleep(spring_msecs(stall));
	}
```
Include `"System/Workbench/Workbench.h"`. (Check that `currentFrameDrawTime` is the name of the local at ~1563; it is `spring_time`. `spring_sleep`/`spring_msecs` are in `System/Misc/SpringTime.h`.)

Also in `CGame::Draw`, when the game has loaded (first call), set engine info once:
```cpp
	static bool workbenchInfoSet = false;
	if (workbench.IsActive() && !workbenchInfoSet) {
		workbench.SetEngineInfo(SpringVersion::GetFull(), modInfo.humanNameVersioned);
		workbenchInfoSet = true;
	}
```
(`SpringVersion` from `"Game/GameVersion.h"`, `modInfo` from `"Sim/Misc/ModInfo.h"`, both already included by Game.cpp — verify and add if not.)

`rts/Net/NetCommands.cpp` — in NETMSG_NEWFRAME, wrap the `SimFrame();` call:
```cpp
			const spring_time wbSimStart = spring_gettime();
			SimFrame();
			const float wbSimMs = (spring_gettime() - wbSimStart).toMilliSecsf();
```
and after the `#ifdef SYNCCHECK ... #endif` block that calls `SetPrevChecksum` (after line ~639) add:
```cpp
			if (workbench.IsActive()) {
			#ifdef SYNCCHECK
				workbench.OnSimFrame(gs->frameNum, wbSimMs, CSyncChecker::GetPrevChecksum(), true);
			#else
				workbench.OnSimFrame(gs->frameNum, wbSimMs, 0, false);
			#endif
			}
```
Include `"System/Workbench/Workbench.h"`.

Add `"${CMAKE_CURRENT_SOURCE_DIR}/Workbench/Workbench.cpp"` to `sources_engine_System_common`.

- [ ] **Step 5: Run unit tests, then build the engine**

Run (WSL): Task 1 Step 4 command → Expected: all 8 test cases pass.
Run (Windows): `wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh engine-legacy`
Expected: build succeeds (`exit 0`, no `error:` lines in the log).

- [ ] **Step 6: Commit**

```bash
git add rts/System/Workbench rts/System/CMakeLists.txt rts/System/SpringApp.cpp rts/Game/Game.cpp rts/Net/NetCommands.cpp test
git commit -m "workbench: controller, --workbench flags, draw/sim/watchdog hooks"
```

---

### Task 3: `Spring.Workbench` Lua API

**Files:**
- Create: `rts/Lua/LuaWorkbench.h`, `rts/Lua/LuaWorkbench.cpp`
- Modify: `rts/Lua/CMakeLists.txt` (next to `LuaDebugExtra.cpp`, line ~13)
- Modify: `rts/Lua/LuaUI.cpp` (~line 131), `rts/Lua/LuaHandleSynced.cpp` (unsynced ~line 135, synced ~line 510)

**Interfaces:**
- Consumes: `workbench` (Task 2).
- Produces Lua API:
  - Unsynced: `Spring.Workbench.IsActive() -> bool`, `GetPattern() -> string`, `BeginScenario(name)`, `EndScenario()`, `BeginWindow(name)`, `EndWindow()`, `Check(name, pass, detail?)`, `Error(msg)`, `RunError(msg)`, `SetFrameStall(ms)`, `FinishRun()`
  - Synced: `Spring.Workbench.IsActive()`, `GetPattern()` only.
  - C++: `bool LuaWorkbench::PushUnsynced(lua_State* L); bool LuaWorkbench::PushSynced(lua_State* L);` — each pushes the `"Workbench"` sub-table into the table on top of the stack (to be used with `AddEntriesToTable(L, "Spring", ...)`).

- [ ] **Step 1: Write a Lua selftest scenario that exercises the API** (it doubles as the failing test: it errors until the API exists)

`cont/base/springcontent/workbench/scenarios/api_selftest.lua`:
```lua
-- Exercises every Spring.Workbench entry point; run with --workbench api_selftest
return {
	name = "api_selftest",
	timeout = 30,
	run = function(ctx)
		local wb = Spring.Workbench
		ctx.check("is_active", wb.IsActive() == true, "IsActive")
		ctx.check("pattern", type(wb.GetPattern()) == "string", tostring(wb.GetPattern()))
		wb.BeginWindow("idle")
		ctx.waitFrames(30)
		wb.EndWindow()
		ctx.check("window_closed", true, "")
	end,
}
```

- [ ] **Step 2: Implement**

`rts/Lua/LuaWorkbench.h`:
```cpp
/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */
#pragma once

struct lua_State;

class LuaWorkbench {
public:
	static bool PushUnsynced(lua_State* L);
	static bool PushSynced(lua_State* L);
private:
	static int IsActive(lua_State* L);
	static int GetPattern(lua_State* L);
	static int BeginScenario(lua_State* L);
	static int EndScenario(lua_State* L);
	static int BeginWindow(lua_State* L);
	static int EndWindow(lua_State* L);
	static int Check(lua_State* L);
	static int Error(lua_State* L);
	static int RunError(lua_State* L);
	static int SetFrameStall(lua_State* L);
	static int FinishRun(lua_State* L);
};
```

`rts/Lua/LuaWorkbench.cpp`:
```cpp
/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "LuaWorkbench.h"
#include "LuaUtils.h"
#include "System/Workbench/Workbench.h"

int LuaWorkbench::IsActive(lua_State* L) { lua_pushboolean(L, workbench.IsActive()); return 1; }
int LuaWorkbench::GetPattern(lua_State* L) { lua_pushsstring(L, workbench.GetPattern()); return 1; }
int LuaWorkbench::BeginScenario(lua_State* L) { workbench.BeginScenario(luaL_checkstring(L, 1)); return 0; }
int LuaWorkbench::EndScenario(lua_State* L) { workbench.EndScenario(); return 0; }
int LuaWorkbench::BeginWindow(lua_State* L) { workbench.BeginWindow(luaL_checkstring(L, 1)); return 0; }
int LuaWorkbench::EndWindow(lua_State* L) { workbench.EndWindow(); return 0; }
int LuaWorkbench::Check(lua_State* L)
{
	workbench.AddCheck(luaL_checkstring(L, 1), luaL_checkboolean(L, 2), luaL_optstring(L, 3, ""));
	return 0;
}
int LuaWorkbench::Error(lua_State* L) { workbench.SetScenarioError(luaL_checkstring(L, 1)); return 0; }
int LuaWorkbench::RunError(lua_State* L) { workbench.SetRunError(luaL_checkstring(L, 1)); return 0; }
int LuaWorkbench::SetFrameStall(lua_State* L) { workbench.SetFrameStall(luaL_checkint(L, 1)); return 0; }
int LuaWorkbench::FinishRun(lua_State* L) { workbench.FinishRun(); return 0; }

bool LuaWorkbench::PushUnsynced(lua_State* L)
{
	lua_pushliteral(L, "Workbench");
	lua_createtable(L, 0, 11);
	REGISTER_LUA_CFUNC(IsActive);
	REGISTER_LUA_CFUNC(GetPattern);
	REGISTER_LUA_CFUNC(BeginScenario);
	REGISTER_LUA_CFUNC(EndScenario);
	REGISTER_LUA_CFUNC(BeginWindow);
	REGISTER_LUA_CFUNC(EndWindow);
	REGISTER_LUA_CFUNC(Check);
	REGISTER_LUA_CFUNC(Error);
	REGISTER_LUA_CFUNC(RunError);
	REGISTER_LUA_CFUNC(SetFrameStall);
	REGISTER_LUA_CFUNC(FinishRun);
	lua_rawset(L, -3);
	return true;
}

bool LuaWorkbench::PushSynced(lua_State* L)
{
	// read-only: nothing reachable from synced Lua may affect sim state
	lua_pushliteral(L, "Workbench");
	lua_createtable(L, 0, 2);
	REGISTER_LUA_CFUNC(IsActive);
	REGISTER_LUA_CFUNC(GetPattern);
	lua_rawset(L, -3);
	return true;
}
```
(`luaL_checkboolean`, `lua_pushsstring` and `luaL_checkint` are the engine's LuaUtils/Lua helpers used throughout `rts/Lua`; if `luaL_checkboolean` is not available, use `luaL_checktype(L, 2, LUA_TBOOLEAN); lua_toboolean(L, 2)`.)

Registration:
- `LuaUI.cpp` in the table list (~line 131): add `!AddEntriesToTable(L, "Spring", LuaWorkbench::PushUnsynced) ||`
- `LuaHandleSynced.cpp` `CUnsyncedLuaHandle::Init` (~line 135): `if (!AddEntriesToTable(L, "Spring", LuaWorkbench::PushUnsynced)) KILL`
- `LuaHandleSynced.cpp` `CSyncedLuaHandle::Init` (~line 510): `if (!AddEntriesToTable(L, "Spring", LuaWorkbench::PushSynced)) KILL`
- include `"LuaWorkbench.h"` in both files; add `"${CMAKE_CURRENT_SOURCE_DIR}/LuaWorkbench.cpp"` to `rts/Lua/CMakeLists.txt`.

- [ ] **Step 3: Build**

Run: `wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh engine-legacy`
Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add rts/Lua/LuaWorkbench.* rts/Lua/CMakeLists.txt rts/Lua/LuaUI.cpp rts/Lua/LuaHandleSynced.cpp cont/base/springcontent/workbench/scenarios/api_selftest.lua
git commit -m "workbench: Spring.Workbench Lua API"
```
(The selftest runs in Task 5 once the harness exists.)

---

### Task 4: Lua harness and engine scenario

**Files:**
- Create: `cont/base/springcontent/workbench/harness.lua`
- Create: `cont/base/springcontent/workbench/scenarios/render_baseline.lua`
- Modify: `cont/base/springcontent/CMakeLists.txt` (append the three workbench files incl. `api_selftest.lua` to `FILES`)

**Interfaces:**
- Consumes: `Spring.Workbench` (Task 3).
- Produces: `harness.lua` returns `{ Start = function(env) end, Update = function() end, RecvSynced = function(msg) end }` usable from a widget or gadget. Scenario file contract:
  ```lua
  return {
    name = "unique_name",            -- required; matched against the --workbench pattern
    timeout = 120,                   -- seconds, optional (default 300)
    run = function(ctx) ... end,     -- runs as a coroutine in LuaUI
    synced = { fnName = function(args...) ... end }, -- optional, callable via ctx.synced("fnName", ...)
  }
  ```
  `ctx` API: `ctx.waitFrames(n)`, `ctx.waitSimFrames(n)`, `ctx.waitSeconds(s)`, `ctx.waitUntil(predicate, timeoutSec) -> bool`, `ctx.window(name, fn)` (Begin/EndWindow around fn), `ctx.check(name, pass, detail)`, `ctx.synced(fnName, ...)` (fire-and-forget; args must be numbers or strings), `ctx.log(msg)`.

- [ ] **Step 1: Implement the harness**

`cont/base/springcontent/workbench/harness.lua`:
```lua
-- Recoil Workbench harness. Loaded by a game's opt-in widget (LuaUI) and gadget
-- (LuaRules) when Spring.Workbench.IsActive(). Runs scenarios from the game's
-- workbench/scenarios/ first (VFS shadows engine files of the same name), then
-- the engine's generic ones.

local H = {}
local MSG_PREFIX = "workbench:"
local SCENARIO_DIR = "workbench/scenarios/"
local DEFAULT_TIMEOUT = 300

local function globToPattern(glob)
	local p = glob:gsub("[%^%$%(%)%%%.%[%]%+%-]", "%%%0"):gsub("%*", ".*"):gsub("%?", ".")
	return "^" .. p .. "$"
end

local function matches(name, spec)
	for part in spec:gmatch("[^,]+") do
		if name:find(globToPattern(part)) then
			return true
		end
	end
	return false
end

local function loadScenarios(spec)
	local files = VFS.DirList(SCENARIO_DIR, "*.lua")
	table.sort(files)
	local list, seen = {}, {}
	for _, file in ipairs(files) do
		local ok, sc = pcall(VFS.Include, file)
		if ok and type(sc) == "table" and type(sc.name) == "string" then
			if not seen[sc.name] and matches(sc.name, spec) then
				seen[sc.name] = true
				list[#list + 1] = sc
			end
		elseif not ok then
			Spring.Log("Workbench", LOG.ERROR, "failed to load " .. file .. ": " .. tostring(sc))
		end
	end
	return list
end

-- ------------------------------------------------------------------ synced side
local syncedFns = {}

function H.StartSynced()
	for _, sc in ipairs(loadScenarios(Spring.Workbench.GetPattern())) do
		for fnName, fn in pairs(sc.synced or {}) do
			syncedFns[sc.name .. "." .. fnName] = fn
		end
	end
end

-- called from the gadget's synced RecvLuaMsg
function H.RecvSynced(msg)
	if msg:sub(1, #MSG_PREFIX) ~= MSG_PREFIX then
		return false
	end
	local parts = {}
	for field in msg:sub(#MSG_PREFIX + 1):gmatch("([^|]*)|?") do
		parts[#parts + 1] = field
	end
	local fn = syncedFns[parts[1]]
	if fn then
		local args = {}
		for i = 2, #parts do
			local v = parts[i]
			if v ~= "" then
				args[#args + 1] = tonumber(v) or v
			end
		end
		local ok, err = pcall(fn, unpack(args))
		if not ok then
			Spring.Log("Workbench", LOG.ERROR, "synced " .. parts[1] .. ": " .. tostring(err))
		end
	end
	return true
end

-- ---------------------------------------------------------------- unsynced side
local queue, current, co, deadline = {}, nil, nil, 0
local frames, simFrame = 0, 0

local function makeCtx(sc)
	local ctx = {}
	function ctx.waitFrames(n)
		local target = frames + n
		while frames < target do coroutine.yield() end
	end
	function ctx.waitSimFrames(n)
		local target = Spring.GetGameFrame() + n
		while Spring.GetGameFrame() < target do coroutine.yield() end
	end
	function ctx.waitSeconds(s)
		local t0 = Spring.GetTimer()
		while Spring.DiffTimers(Spring.GetTimer(), t0) < s do coroutine.yield() end
	end
	function ctx.waitUntil(pred, timeoutSec)
		local t0 = Spring.GetTimer()
		while not pred() do
			if Spring.DiffTimers(Spring.GetTimer(), t0) > timeoutSec then return false end
			coroutine.yield()
		end
		return true
	end
	function ctx.window(name, fn)
		Spring.Workbench.BeginWindow(name)
		fn()
		Spring.Workbench.EndWindow()
	end
	function ctx.check(name, pass, detail)
		Spring.Workbench.Check(name, pass and true or false, detail and tostring(detail) or "")
	end
	function ctx.synced(fnName, ...)
		local parts = { MSG_PREFIX .. sc.name .. "." .. fnName }
		for _, v in ipairs({ ... }) do parts[#parts + 1] = tostring(v) end
		Spring.SendLuaRulesMsg(table.concat(parts, "|"))
	end
	function ctx.log(msg)
		Spring.Echo("[Workbench] " .. sc.name .. ": " .. tostring(msg))
	end
	return ctx
end

local function startNext()
	current = table.remove(queue, 1)
	if not current then
		Spring.Workbench.FinishRun()
		return
	end
	Spring.Workbench.BeginScenario(current.name)
	deadline = Spring.GetTimer()
	local ctx = makeCtx(current)
	co = coroutine.create(function() current.run(ctx) end)
end

function H.Start()
	local spec = Spring.Workbench.GetPattern()
	queue = loadScenarios(spec)
	if #queue == 0 then
		Spring.Workbench.RunError("no scenarios matched " .. spec)
		Spring.Workbench.FinishRun()
		return
	end
	startNext()
end

-- call every LuaUI Update
function H.Update()
	frames = frames + 1
	if not co then return end

	local limit = current.timeout or DEFAULT_TIMEOUT
	if Spring.DiffTimers(Spring.GetTimer(), deadline) > limit then
		Spring.Workbench.Error("scenario timeout after " .. limit .. "s")
		co = nil
	else
		local ok, err = coroutine.resume(co)
		if not ok then
			Spring.Workbench.Error(tostring(err))
			co = nil
		elseif coroutine.status(co) == "dead" then
			co = nil
		end
	end

	if not co then
		Spring.Workbench.EndScenario()
		startNext()
	end
end

return H
```

`cont/base/springcontent/workbench/scenarios/render_baseline.lua`:
```lua
-- Game-agnostic render baseline: fixed camera path over the map, no units.
return {
	name = "render_baseline",
	timeout = 120,
	run = function(ctx)
		local mx, mz = Game.mapSizeX, Game.mapSizeZ
		local keys = {
			{ mx * 0.2, 1500, mz * 0.2 },
			{ mx * 0.8, 1500, mz * 0.2 },
			{ mx * 0.8, 1500, mz * 0.8 },
			{ mx * 0.2, 1500, mz * 0.8 },
		}
		ctx.waitSeconds(3) -- warm-up: shader compiles, texture streaming
		ctx.window("camera_path", function()
			for _, k in ipairs(keys) do
				Spring.SetCameraTarget(k[1], Spring.GetGroundHeight(k[1], k[3]), k[3], 2.0)
				ctx.waitSeconds(3)
			end
		end)
		ctx.check("completed", true, "")
	end,
}
```

Append to `cont/base/springcontent/CMakeLists.txt` `FILES` (next to `LuaGadgets/gadgets.lua`):
```cmake
	workbench/harness.lua
	workbench/scenarios/api_selftest.lua
	workbench/scenarios/render_baseline.lua
```

- [ ] **Step 2: Build base content**

Run: `wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh basecontent`
Expected: exit 0; `unzip -l ~/bar/RecoilEngine/build-amd64-windows/base/springcontent.sdz | grep workbench` lists the three files.

- [ ] **Step 3: Commit**

```bash
git add cont/base/springcontent/workbench cont/base/springcontent/CMakeLists.txt
git commit -m "workbench: Lua harness and engine scenarios"
```

---

### Task 5: BAR opt-in loaders and first scenarios

**Files (Beyond-All-Reason repo):**
- Create: `luaui/Widgets/dbg_workbench.lua`
- Create: `luarules/gadgets/dbg_workbench.lua`
- Create: `workbench/scenarios/mass_move_500.lua`
- Create: `workbench/scenarios/weapon_range.lua`

**Interfaces:**
- Consumes: harness contract (Task 4), `Spring.Workbench` (Task 3).

- [ ] **Step 1: Loaders**

`luaui/Widgets/dbg_workbench.lua`:
```lua
local widget = widget ---@type Widget

function widget:GetInfo()
	return {
		name = "Recoil Workbench",
		desc = "Runs Recoil Workbench scenarios when the engine is started with --workbench",
		license = "GNU GPL, v2 or later",
		layer = -math.huge,
		enabled = true,
	}
end

local harness

function widget:Initialize()
	if not (Spring.Workbench and Spring.Workbench.IsActive()) then
		widgetHandler:RemoveWidget(self)
		return
	end
	harness = VFS.Include("workbench/harness.lua")
end

function widget:GameStart()
	harness.Start()
end

function widget:Update()
	if harness and Spring.GetGameFrame() > 0 then
		harness.Update()
	end
end
```

`luarules/gadgets/dbg_workbench.lua`:
```lua
local gadget = gadget ---@type Gadget

function gadget:GetInfo()
	return {
		name = "Recoil Workbench",
		desc = "Synced half of the Recoil Workbench harness (runs scenario synced functions)",
		license = "GNU GPL, v2 or later",
		layer = 0,
		enabled = true,
	}
end

if not gadgetHandler:IsSyncedCode() then
	return
end

if not (Spring.Workbench and Spring.Workbench.IsActive()) then
	return
end

local harness = VFS.Include("workbench/harness.lua")

function gadget:Initialize()
	harness.StartSynced()
end

function gadget:RecvLuaMsg(msg, playerID)
	return harness.RecvSynced(msg)
end
```

- [ ] **Step 2: Scenarios**

`workbench/scenarios/mass_move_500.lua`:
```lua
-- 500 ground units of one type cross the map; measures sim/pathing cost.
local COUNT = 500
local UNIT = "armpw"

return {
	name = "mass_move_500",
	timeout = 240,
	synced = {
		spawn = function(teamID, count)
			local def = UnitDefNames[UNIT]
			local side = math.ceil(math.sqrt(count))
			local x0, z0 = Game.mapSizeX * 0.15, Game.mapSizeZ * 0.15
			for i = 0, count - 1 do
				local x = x0 + (i % side) * 48
				local z = z0 + math.floor(i / side) * 48
				Spring.CreateUnit(def.id, x, Spring.GetGroundHeight(x, z), z, 0, teamID)
			end
		end,
		moveAll = function(teamID)
			local tx, tz = Game.mapSizeX * 0.85, Game.mapSizeZ * 0.85
			for _, u in ipairs(Spring.GetTeamUnits(teamID)) do
				Spring.GiveOrderToUnit(u, CMD.MOVE, { tx, Spring.GetGroundHeight(tx, tz), tz }, 0)
			end
		end,
	},
	run = function(ctx)
		local team = Spring.GetMyTeamID()
		ctx.synced("spawn", team, COUNT)
		local spawned = ctx.waitUntil(function() return #Spring.GetTeamUnits(team) >= COUNT end, 30)
		ctx.check("spawned", spawned, #Spring.GetTeamUnits(team) .. "/" .. COUNT)
		if not spawned then return end
		ctx.synced("moveAll", team)
		ctx.window("moving", function() ctx.waitSimFrames(30 * 60) end) -- 60 s of sim
		local tx, tz = Game.mapSizeX * 0.85, Game.mapSizeZ * 0.85
		local near = 0
		for _, u in ipairs(Spring.GetTeamUnits(team)) do
			local x, _, z = Spring.GetUnitPosition(u)
			if x and math.abs(x - tx) + math.abs(z - tz) < 1500 then near = near + 1 end
		end
		ctx.check("progress", near > 0, near .. " units near target after 60 s")
	end,
}
```

`workbench/scenarios/weapon_range.lua`:
```lua
-- Per-unit weapon range check for a sample of ground units: an enemy target
-- just inside range must take damage; one just outside must not.
local SAMPLE = { "armpw", "armrock", "armham", "corak", "corthud", "corstorm" }
local MARGIN = 0.1     -- fraction of range
local WAIT_SECONDS = 12

return {
	name = "weapon_range",
	timeout = 60 + #SAMPLE * 2 * (WAIT_SECONDS + 3),
	synced = {
		place = function(attackerName, attackerTeam, targetTeam, distance)
			local x, z = Game.mapSizeX * 0.5, Game.mapSizeZ * 0.5
			local a = Spring.CreateUnit(attackerName, x, Spring.GetGroundHeight(x, z), z, 0, attackerTeam)
			local tx = x + distance
			Spring.CreateUnit("armsolar", tx, Spring.GetGroundHeight(tx, z), z, 0, targetTeam)
			-- freeze the attacker so it cannot walk into range (an ATTACK order would
			-- close the distance and make every "outside" case fire), let it pick the
			-- target itself, and give its allyteam full LOS so units whose sight is
			-- shorter than their weapon range are still tested on range alone
			Spring.MoveCtrl.Enable(a)
			Spring.GiveOrderToUnit(a, CMD.FIRE_STATE, { 2 }, 0)
			Spring.SetGlobalLos(Spring.GetUnitAllyTeam(a), true)
		end,
		clear = function()
			for _, u in ipairs(Spring.GetAllUnits()) do
				Spring.DestroyUnit(u, false, true)
			end
		end,
	},
	run = function(ctx)
		local me = Spring.GetMyTeamID()
		local enemy
		for _, t in ipairs(Spring.GetTeamList()) do
			if not Spring.AreTeamsAllied(t, me) and t ~= Spring.GetGaiaTeamID() then enemy = t break end
		end
		if not enemy then
			ctx.check("enemy_team", false, "start script needs a non-allied team")
			return
		end
		for _, name in ipairs(SAMPLE) do
			local def = UnitDefNames[name]
			local range = def and def.maxWeaponRange or 0
			if range <= 0 then
				ctx.check("range:" .. name, false, "unit missing or unarmed")
			else
				for _, case in ipairs({ { "inside", range * (1 - MARGIN), true }, { "outside", range * (1 + MARGIN), false } }) do
					ctx.synced("clear")
					ctx.waitSimFrames(2)
					ctx.synced("place", name, me, enemy, case[2])
					ctx.waitSimFrames(2)
					local target
					for _, u in ipairs(Spring.GetAllUnits()) do
						if Spring.GetUnitTeam(u) == enemy then target = u end
					end
					local _, maxHp = target and Spring.GetUnitHealth(target)
					ctx.waitSeconds(WAIT_SECONDS)
					local hp = target and Spring.ValidUnitID(target) and Spring.GetUnitHealth(target) or 0
					local damaged = (maxHp or 0) > 0 and hp < maxHp
					ctx.check(
						case[1] .. ":" .. name,
						damaged == case[3],
						string.format("range %.0f distance %.0f expected %s, target hp %.0f/%.0f",
							range, case[2], case[3] and "damage" or "no damage", hp, maxHp or 0)
					)
				end
			end
		end
		ctx.synced("clear")
	end,
}
```

- [ ] **Step 3: Commit (BAR repo)**

```bash
cd /c/Workspace/bar/Beyond-All-Reason
git add luaui/Widgets/dbg_workbench.lua luarules/gadgets/dbg_workbench.lua workbench
git commit -m "workbench: BAR opt-in loaders, mass_move_500 and weapon_range scenarios"
```

---

### Task 6: Host runner `run.py`

**Files:**
- Create: `tools/workbench/run.py`, `tools/workbench/templates/startscript.txt`, `tools/workbench/profiles/default.cfg`, `tools/workbench/tests/test_run.py`

**Interfaces:**
- Produces:
  - CLI: `python tools/workbench/run.py --engine NAME=PATH [--engine ...] --data-dir DIR [--only PATTERN] [--profile NAME ...] [--reps N] [--timeout SEC] [--out DIR] [--map NAME] [--no-report]`
  - Functions (importable for tests): `expand_matrix(engines: dict[str,str], profiles: list[str], reps: int) -> list[Cell]` where `Cell = namedtuple("Cell", "engine exe profile rep")`; `render_startscript(template: str, map_name: str) -> str`; `tail_lines(path: str, n: int) -> list[str]`; `run_cell(cell, args, out_root) -> dict` (keys `status` in `ok|checks_failed|error|timeout`, `exit_code`, `results_dir`, `infolog_tail`).

- [ ] **Step 1: Write failing tests**

`tools/workbench/tests/test_run.py`:
```python
import os, sys, tempfile, unittest
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import run


class ExpandMatrix(unittest.TestCase):
    def test_cross_product_in_stable_order(self):
        cells = run.expand_matrix({"a": "a.exe", "b": "b.exe"}, ["low", "high"], 2)
        self.assertEqual(len(cells), 8)
        self.assertEqual(cells[0], run.Cell("a", "a.exe", "low", 0))
        self.assertEqual(cells[-1], run.Cell("b", "b.exe", "high", 1))


class StartScript(unittest.TestCase):
    def test_map_substituted_and_offline(self):
        s = run.render_startscript("MapName=$MAP;\nHostPort=0;", "Red Comet Remake 1.8")
        self.assertIn("MapName=Red Comet Remake 1.8;", s)
        self.assertIn("HostPort=0;", s)


class Status(unittest.TestCase):
    def test_exit_codes_map_to_status(self):
        self.assertEqual(run.status_for(0, timed_out=False), "ok")
        self.assertEqual(run.status_for(1, timed_out=False), "checks_failed")
        self.assertEqual(run.status_for(2, timed_out=False), "error")
        self.assertEqual(run.status_for(-1003, timed_out=False), "error")
        self.assertEqual(run.status_for(None, timed_out=True), "timeout")


class Tail(unittest.TestCase):
    def test_tail_missing_file_is_empty(self):
        self.assertEqual(run.tail_lines("/nonexistent/infolog.txt", 5), [])

    def test_tail_last_n(self):
        with tempfile.NamedTemporaryFile("w", delete=False) as f:
            f.write("\n".join(str(i) for i in range(100)))
        try:
            self.assertEqual(run.tail_lines(f.name, 3), ["97", "98", "99"])
        finally:
            os.unlink(f.name)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify failure**

Run: `python -m unittest discover -s tools/workbench/tests -v`
Expected: `ModuleNotFoundError: No module named 'run'`.

- [ ] **Step 3: Implement**

`tools/workbench/templates/startscript.txt`:
```
[GAME]
{
	GameType=Beyond All Reason $VERSION;
	MapName=$MAP;
	IsHost=1;
	MyPlayerName=Workbench;
	HostIP=127.0.0.1;
	HostPort=0;
	StartPosType=3;
	[MODOPTIONS]
	{
	}
	[PLAYER0]
	{
		Name=Workbench;
		Team=0;
		Spectator=0;
	}
	[TEAM0]
	{
		TeamLeader=0;
		AllyTeam=0;
		Side=Armada;
		StartPosX=1000;
		StartPosZ=1000;
	}
	[TEAM1]
	{
		TeamLeader=0;
		AllyTeam=1;
		Side=Cortex;
		StartPosX=6000;
		StartPosZ=6000;
	}
	[ALLYTEAM0]
	{
		NumAllies=0;
	}
	[ALLYTEAM1]
	{
		NumAllies=0;
	}
}
```

`tools/workbench/profiles/default.cfg`:
```
Fullscreen = 0
XResolutionWindowed = 1920
YResolutionWindowed = 1080
VSync = 0
UseHighResTimer = 1
```

`tools/workbench/run.py`:
```python
#!/usr/bin/env python3
"""Recoil Workbench matrix runner: engines x profiles x repetitions, one engine launch per cell."""
import argparse
import collections
import datetime
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
Cell = collections.namedtuple("Cell", "engine exe profile rep")


def expand_matrix(engines, profiles, reps):
    return [Cell(name, exe, prof, rep) for name, exe in engines.items() for prof in profiles for rep in range(reps)]


def render_startscript(template, map_name):
    return template.replace("$MAP", map_name)


def status_for(exit_code, timed_out):
    if timed_out:
        return "timeout"
    return {0: "ok", 1: "checks_failed"}.get(exit_code, "error")


def tail_lines(path, n):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read().splitlines()[-n:]
    except OSError:
        return []


def run_cell(cell, args, out_root):
    cell_dir = os.path.join(out_root, cell.engine, cell.profile, f"rep{cell.rep}")
    os.makedirs(cell_dir, exist_ok=True)
    with open(os.path.join(HERE, "templates", "startscript.txt"), encoding="utf-8") as f:
        script = render_startscript(f.read(), args.map)
    script_path = os.path.join(cell_dir, "startscript.txt")
    with open(script_path, "w", encoding="utf-8") as f:
        f.write(script)

    cmd = [
        cell.exe,
        "--isolation", "--write-dir", args.data_dir,
        "--config", os.path.join(HERE, "profiles", cell.profile + ".cfg"),
        "--workbench", args.only,
        "--workbench-out", os.path.join(cell_dir, "results"),
        "--workbench-timeout", str(args.timeout),
        "--workbench-profile", cell.profile,
        script_path,
    ]
    with open(os.path.join(cell_dir, "command.txt"), "w", encoding="utf-8") as f:
        f.write(subprocess.list2cmdline(cmd))

    timed_out, code = False, None
    try:
        code = subprocess.run(cmd, cwd=os.path.dirname(cell.exe), timeout=args.timeout + 120).returncode
    except subprocess.TimeoutExpired:
        timed_out = True

    infolog = os.path.join(args.data_dir, "infolog.txt")
    if os.path.exists(infolog):
        shutil.copy(infolog, os.path.join(cell_dir, "infolog.txt"))
    result = {
        "engine": cell.engine, "profile": cell.profile, "rep": cell.rep,
        "status": status_for(code, timed_out), "exit_code": code,
        "results_dir": os.path.join(cell_dir, "results"),
        "infolog_tail": tail_lines(infolog, 40) if status_for(code, timed_out) in ("error", "timeout") else [],
    }
    with open(os.path.join(cell_dir, "cell.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    return result


def parse_args(argv):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--engine", action="append", required=True, help="NAME=PATH to spring.exe (repeatable)")
    p.add_argument("--data-dir", required=True, help="write/data dir with games/BAR.sdd and maps/")
    p.add_argument("--only", default="*", help="scenario name pattern(s), comma separated globs")
    p.add_argument("--profile", action="append", default=None, help="settings profile name (repeatable)")
    p.add_argument("--reps", type=int, default=1)
    p.add_argument("--timeout", type=int, default=1800)
    p.add_argument("--map", default="Red Comet Remake 1.8")
    p.add_argument("--out", default=os.path.join(HERE, "results"))
    p.add_argument("--no-report", action="store_true")
    args = p.parse_args(argv)
    args.profile = args.profile or ["default"]
    for prof in args.profile:
        if not os.path.exists(os.path.join(HERE, "profiles", prof + ".cfg")):
            p.error(f"unknown profile '{prof}' (no profiles/{prof}.cfg)")
    engines = {}
    for spec in args.engine:
        name, _, path = spec.partition("=")
        if not path or not os.path.exists(path):
            p.error(f"--engine {spec}: expected NAME=PATH to an existing spring.exe")
        engines[name] = path
    args.engines = engines
    return args


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    out_root = os.path.join(args.out, datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    cells = expand_matrix(args.engines, args.profile, args.reps)
    summary = []
    for i, cell in enumerate(cells, 1):
        print(f"[{i}/{len(cells)}] {cell.engine} / {cell.profile} / rep {cell.rep} ...", flush=True)
        r = run_cell(cell, args, out_root)
        print(f"    -> {r['status']} (exit {r['exit_code']})", flush=True)
        summary.append(r)
    with open(os.path.join(out_root, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    if not args.no_report:
        import report
        path = report.write_report(out_root)
        print(f"report: {path}")
    return 0 if all(r["status"] == "ok" for r in summary) else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run tests to verify pass**

Run: `python -m unittest discover -s tools/workbench/tests -v`
Expected: 6 tests OK.

- [ ] **Step 5: Commit**

```bash
git add tools/workbench/run.py tools/workbench/templates tools/workbench/profiles tools/workbench/tests/test_run.py
git commit -m "workbench: matrix runner"
```

---

### Task 7: Minimal comparison report

**Files:**
- Create: `tools/workbench/report.py`, `tools/workbench/tests/test_report.py`

**Interfaces:**
- Consumes: `summary.json` and per-cell `results/*.json` (layout from Task 6 / Task 1 JSON).
- Produces: `load_cells(out_root) -> list[dict]`, `median(values) -> float`, `compare(baseline: list[float], candidate: list[float], rel_threshold=0.05) -> str` in `{"regression","improvement","same","n/a"}`, `write_report(out_root) -> str` (path to `compare.html`).

- [ ] **Step 1: Failing tests**

`tools/workbench/tests/test_report.py`:
```python
import os, sys, unittest
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import report


class Compare(unittest.TestCase):
    def test_same_within_threshold(self):
        self.assertEqual(report.compare([10.0, 10.2, 9.9], [10.1, 10.3, 10.0]), "same")

    def test_regression_beyond_threshold_and_spread(self):
        self.assertEqual(report.compare([10.0, 10.1, 9.9], [12.0, 12.1, 11.9]), "regression")

    def test_improvement(self):
        self.assertEqual(report.compare([10.0, 10.1, 9.9], [8.0, 8.1, 7.9]), "improvement")

    def test_noisy_delta_is_same(self):
        # 20% median delta but the baseline spread is larger than the delta
        self.assertEqual(report.compare([5.0, 10.0, 15.0], [12.0, 12.0, 12.0]), "same")

    def test_missing_data(self):
        self.assertEqual(report.compare([], [1.0]), "n/a")


class Median(unittest.TestCase):
    def test_even_and_odd(self):
        self.assertEqual(report.median([3, 1, 2]), 2)
        self.assertEqual(report.median([4, 1, 2, 3]), 2.5)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify failure**

Run: `python -m unittest discover -s tools/workbench/tests -v`
Expected: `ModuleNotFoundError: No module named 'report'`.

- [ ] **Step 3: Implement**

`tools/workbench/report.py`:
```python
#!/usr/bin/env python3
"""Recoil Workbench report: turns a run directory into a self-contained compare.html."""
import glob
import html
import json
import os
import sys


def median(values):
    s = sorted(values)
    n = len(s)
    if n == 0:
        return None
    mid = n // 2
    return s[mid] if n % 2 else (s[mid - 1] + s[mid]) / 2


def compare(baseline, candidate, rel_threshold=0.05):
    """Lower is better (times). A change counts only beyond both the relative threshold and the baseline spread."""
    if not baseline or not candidate:
        return "n/a"
    b, c = median(baseline), median(candidate)
    if b == 0:
        return "n/a"
    delta = c - b
    spread = max(baseline) - min(baseline)
    if abs(delta) <= max(abs(b) * rel_threshold, spread):
        return "same"
    return "regression" if delta > 0 else "improvement"


def load_cells(out_root):
    with open(os.path.join(out_root, "summary.json"), encoding="utf-8") as f:
        cells = json.load(f)
    for cell in cells:
        cell["scenarios"] = {}
        for path in glob.glob(os.path.join(cell["results_dir"], "*.json")):
            if os.path.basename(path) == "run.json":
                continue
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            cell["scenarios"][data["scenario"]] = data
    return cells


def _metric_series(cells, engine, profile, scenario, window, metric):
    out = []
    for c in cells:
        if c["engine"] == engine and c["profile"] == profile:
            sc = c["scenarios"].get(scenario)
            for w in (sc or {}).get("windows", []):
                if w["name"] == window and w[metric]["count"] > 0:
                    out.append(w[metric]["p50"])
    return out


def write_report(out_root):
    cells = load_cells(out_root)
    engines = list(dict.fromkeys(c["engine"] for c in cells))
    profiles = list(dict.fromkeys(c["profile"] for c in cells))
    base = engines[0] if engines else None
    rows, checks, errors = [], [], []

    keys = set()
    for c in cells:
        if c["status"] in ("error", "timeout"):
            errors.append(c)
        for name, sc in c["scenarios"].items():
            for w in sc.get("windows", []):
                keys.add((c["profile"], name, w["name"]))
            for chk in sc.get("checks", []):
                if not chk["pass"]:
                    checks.append((c["engine"], c["profile"], name, chk["name"], chk["detail"], c["results_dir"]))

    for profile, scenario, window in sorted(keys):
        for metric in ("frameTimeMs", "simTimeMs", "gpuTimeMs", "drawTimeMs"):
            bs = _metric_series(cells, base, profile, scenario, window, metric)
            for eng in engines:
                s = _metric_series(cells, eng, profile, scenario, window, metric)
                verdict = "baseline" if eng == base else compare(bs, s)
                rows.append((profile, scenario, window, metric, eng, median(s), verdict))

    esc = html.escape
    parts = ["<!doctype html><meta charset=utf-8><title>Workbench report</title>",
             "<style>body{font:14px system-ui;margin:24px}table{border-collapse:collapse}"
             "td,th{border:1px solid #ccc;padding:4px 8px}.regression{background:#fdd}"
             ".improvement{background:#dfd}</style>",
             f"<h1>Workbench report</h1><p>{esc(out_root)}</p>"]
    regs = [r for r in rows if r[6] == "regression"]
    imps = [r for r in rows if r[6] == "improvement"]
    parts.append(f"<h2>Summary</h2><p>{len(regs)} regressions, {len(imps)} improvements, "
                 f"{len(checks)} failed checks, {len(errors)} errored cells.</p>")
    if errors:
        parts.append("<h2>Errors</h2><ul>")
        for c in errors:
            parts.append(f"<li>{esc(c['engine'])}/{esc(c['profile'])} rep {c['rep']}: {esc(c['status'])}"
                         f"<pre>{esc(chr(10).join(c['infolog_tail']))}</pre></li>")
        parts.append("</ul>")
    if checks:
        parts.append("<h2>Failed checks</h2><table><tr><th>engine</th><th>profile</th><th>scenario</th>"
                     "<th>check</th><th>detail</th><th>repro</th></tr>")
        for eng, prof, sc, name, detail, rdir in checks:
            cmd_path = os.path.join(os.path.dirname(rdir), "command.txt")
            parts.append(f"<tr><td>{esc(eng)}</td><td>{esc(prof)}</td><td>{esc(sc)}</td><td>{esc(name)}</td>"
                         f"<td>{esc(detail)}</td><td>{esc(cmd_path)}</td></tr>")
        parts.append("</table>")
    parts.append("<h2>Metrics (median of p50 per repetition, ms)</h2><table><tr><th>profile</th><th>scenario</th>"
                 "<th>window</th><th>metric</th><th>engine</th><th>median</th><th>vs baseline</th></tr>")
    for profile, scenario, window, metric, eng, med, verdict in rows:
        cls = verdict if verdict in ("regression", "improvement") else ""
        val = "" if med is None else f"{med:.2f}"
        parts.append(f"<tr class='{cls}'><td>{esc(profile)}</td><td>{esc(scenario)}</td><td>{esc(window)}</td>"
                     f"<td>{esc(metric)}</td><td>{esc(eng)}</td><td>{val}</td><td>{esc(verdict)}</td></tr>")
    parts.append("</table>")
    path = os.path.join(out_root, "compare.html")
    with open(path, "w", encoding="utf-8") as f:
        f.write("".join(parts))
    return path


if __name__ == "__main__":
    print(write_report(sys.argv[1]))
```

- [ ] **Step 4: Run tests**

Run: `python -m unittest discover -s tools/workbench/tests -v`
Expected: all tests OK (12).

- [ ] **Step 5: Commit**

```bash
git add tools/workbench/report.py tools/workbench/tests/test_report.py
git commit -m "workbench: comparison report"
```

---

### Task 8: End-to-end smoke and acceptance

**Files:**
- Create: `tools/workbench/README.md`
- Modify: `C:\Workspace\bar\engine\` contents (deploy build output — not in git)

- [ ] **Step 1: Build and deploy**

```bash
wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh engine-legacy basecontent
```
Copy from `\\wsl$\Ubuntu\home\<user>\bar\RecoilEngine\build-amd64-windows\` to `C:\Workspace\bar\engine\`: `spring.exe` (and `spring.dbg` if present), `base\springcontent.sdz`, `base\maphelper.sdz`, `base\cursors.sdz`, `base\spring\bitmaps.sdz` → `engine\base\spring\`, and `cont\base\RecoilEngine_4K.png` → `engine\base\` (the runtime DLLs are already there).

- [ ] **Step 2: Run the engine selftest**

```bash
python tools/workbench/run.py --engine dev=C:/Workspace/bar/engine/spring.exe --data-dir C:/Workspace/bar/data --only api_selftest,render_baseline --timeout 300
```
Expected: `-> ok (exit 0)`, `report: …compare.html`; `results/api_selftest.json` has 3 passing checks and one window with `frameTimeMs.count > 0`; `run.json` has `sync.available: true` and `lastFrame > 0`.

- [ ] **Step 3: Run the BAR scenarios**

```bash
python tools/workbench/run.py --engine dev=C:/Workspace/bar/engine/spring.exe --data-dir C:/Workspace/bar/data --only mass_move_500,weapon_range
```
Expected: exit ok or checks_failed with per-unit details in the report (a failure here is a finding, not a harness bug, if the detail string is meaningful).

- [ ] **Step 4: Acceptance — injected regression and broken check**

1. Temporarily add `spring_sleep(spring_msecs(3));` inside `CGame::SimFrame()` (inside the `Sim` timer scope), build as a second engine `slow`, deploy to `C:\Workspace\bar\engine-slow\` (copy the whole `engine` folder then replace `spring.exe`).
2. Run `python tools/workbench/run.py --engine dev=…/engine/spring.exe --engine slow=…/engine-slow/spring.exe --data-dir C:/Workspace/bar/data --only mass_move_500 --reps 3`.
   Expected: report lists `simTimeMs` for `mass_move_500/moving` as `regression` for `slow`.
3. Temporarily change `weapon_range`'s `MARGIN` to `-0.3` (so "outside" is inside), run `--only weapon_range`: expected failed checks `outside:<unit>` listed with repro path.
4. Revert both temporary changes (`git checkout -- rts/Game/Game.cpp`, BAR `git checkout -- workbench/scenarios/weapon_range.lua`).

- [ ] **Step 5: Write `tools/workbench/README.md`** (how to build, deploy, run, read the report, add a scenario — the scenario contract from Task 4 and the `ctx` API) and commit.

```bash
git add tools/workbench/README.md
git commit -m "workbench: README and phase-1 acceptance"
git push fork master
```
