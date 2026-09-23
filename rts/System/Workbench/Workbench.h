/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */
#pragma once

#include "WorkbenchResults.h"

#include <functional>
#include <string>
#include <vector>

// Recoil Workbench controller: enabled with --workbench <pattern>. Observes draw
// and sim frames inside scenario-marked windows, records checks, writes JSON
// results and ends the process with a meaningful exit code. Never touches sim state.
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
	// engine is exiting: a run that never reached FinishRun (game over, crash-free quit) is an error
	void OnShutdown();
	// fatal error / crash: record it against the running scenario and write results
	void OnCrash(const std::string& msg);

	void OnDrawFrame(float frameMs, float drawMs, float gpuMs);
	void OnSimFrame(int frameNum, float simMs, unsigned checksum, bool checksumValid);
	void OnMemorySample(size_t residentBytes);
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
