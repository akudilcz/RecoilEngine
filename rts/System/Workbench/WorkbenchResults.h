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
	double memStartMB = 0.0; // process resident memory at the first sample in the window
	double memPeakMB = 0.0;
	double memEndMB = 0.0;
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
