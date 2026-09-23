/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "System/Workbench/WorkbenchStats.h"
#include "System/Workbench/WorkbenchResults.h"

#include <algorithm>

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
	for (int i = 1; i <= 100; ++i)
		v.push_back(float(i));
	std::reverse(v.begin(), v.end()); // input order must not matter

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
	const WorkbenchScenario ok{"a", {}, {{"c1", true, ""}}, ""};
	const WorkbenchScenario bad{"b", {}, {{"c2", false, "expected 1 got 2"}}, ""};
	const WorkbenchScenario err{"c", {}, {}, "lua error"};

	CHECK(DecideExitCode({ok}, "") == 0);
	CHECK(DecideExitCode({ok, bad}, "") == 1);
	CHECK(DecideExitCode({ok, err}, "") == 2);
	CHECK(DecideExitCode({}, "") == 2); // nothing ran is an error, never a pass
	CHECK(DecideExitCode({ok}, "timeout") == 2);
}

TEST_CASE("ScenarioToJson layout")
{
	WorkbenchWindow w;
	w.name = "moving";
	w.frameMs = {10.0f, 20.0f};
	w.simFrames = 3;
	const WorkbenchScenario sc{"mass_move_500", {w}, {{"arrived", true, "480/500"}}, ""};
	const WorkbenchRunInfo info{"2026.09-test", "BAR test", "mass_*", "default"};

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

#include "System/Workbench/Workbench.h"

TEST_CASE("Workbench windows collect only while open")
{
	CWorkbench wb;
	wb.writeFiles = false;
	wb.Configure("mass_*", "", 60, "default");
	REQUIRE(wb.IsActive());

	wb.BeginScenario("s1");
	wb.OnDrawFrame(16.0f, 5.0f, 4.0f); // outside any window: ignored
	wb.BeginWindow("w");
	wb.OnDrawFrame(10.0f, 3.0f, 2.0f);
	wb.OnDrawFrame(20.0f, 6.0f, 4.0f);
	wb.OnSimFrame(1, 1.5f, 0xabcu, true);
	wb.EndWindow();
	wb.OnDrawFrame(99.0f, 9.0f, 9.0f); // after the window: ignored
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
	now = 4.0f;
	wb.Update(now);
	CHECK_FALSE(wb.IsFinished());
	now = 6.0f;
	wb.Update(now);
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

TEST_CASE("Engine shutdown before FinishRun is an error, not a silent pass")
{
	CWorkbench wb;
	wb.writeFiles = false;
	wb.Configure("x", "", 60, "default");
	wb.BeginScenario("s");
	wb.AddCheck("c", true, "");
	wb.OnShutdown(); // e.g. the game ended because a scenario destroyed every commander
	CHECK(wb.IsFinished());
	CHECK(wb.GetExitCode() == 2);
	CHECK(wb.GetScenarios()[0].error == "engine shut down before the scenario finished");
}

TEST_CASE("OnShutdown after FinishRun keeps the result")
{
	CWorkbench wb;
	wb.writeFiles = false;
	wb.Configure("x", "", 60, "default");
	wb.BeginScenario("s");
	wb.AddCheck("c", true, "");
	wb.FinishRun();
	wb.OnShutdown();
	CHECK(wb.GetExitCode() == 0);
}

TEST_CASE("A crash during a scenario is recorded against that scenario")
{
	CWorkbench wb;
	wb.writeFiles = false;
	wb.Configure("x", "", 60, "default");
	wb.BeginScenario("weapon_range");
	wb.OnCrash("Access violation (0xc0000005)");
	CHECK(wb.IsFinished());
	CHECK(wb.GetExitCode() == 2);
	CHECK(wb.GetScenarios()[0].error == "engine crashed: Access violation (0xc0000005)");
}

#include "System/Workbench/WorkbenchMemory.h"

TEST_CASE("Process resident memory is readable")
{
	CHECK(GetProcessResidentBytes() > 0);
}

TEST_CASE("Windows record memory start, peak and end")
{
	CWorkbench wb;
	wb.writeFiles = false;
	wb.Configure("x", "", 60, "default");
	wb.BeginScenario("s");
	wb.OnMemorySample(100u << 20); // outside a window: ignored
	wb.BeginWindow("w");
	wb.OnMemorySample(200u << 20);
	wb.OnMemorySample(500u << 20);
	wb.OnMemorySample(300u << 20);
	wb.EndWindow();
	const WorkbenchWindow& w = wb.GetScenarios()[0].windows[0];
	CHECK(w.memStartMB == Catch::Approx(200.0));
	CHECK(w.memPeakMB == Catch::Approx(500.0));
	CHECK(w.memEndMB == Catch::Approx(300.0));
}

TEST_CASE("Memory appears in the scenario JSON")
{
	WorkbenchWindow w;
	w.name = "w";
	w.memStartMB = 1.0;
	w.memPeakMB = 3.0;
	w.memEndMB = 2.0;
	const Json::Value j = ScenarioToJson({"s", {w}, {}, ""}, {});
	CHECK(j["windows"][0]["memoryMB"]["peak"].asDouble() == Catch::Approx(3.0));
	CHECK(j["windows"][0]["memoryMB"]["growth"].asDouble() == Catch::Approx(1.0));
}

TEST_CASE("A hang of the main thread is recorded against the running scenario")
{
	CWorkbench wb;
	wb.writeFiles = false;
	wb.Configure("x", "", 60, "default");
	wb.BeginScenario("sync_repro");
	wb.OnHang("main");
	CHECK(wb.IsFinished());
	CHECK(wb.GetExitCode() == 2);
	CHECK(wb.GetScenarios()[0].error == "engine hung: thread main unresponsive");
}
