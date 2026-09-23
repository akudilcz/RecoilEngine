/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "WorkbenchResults.h"
#include "WorkbenchStats.h"

#include <cstdio>
#include <filesystem>
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

static void AddRunInfo(const WorkbenchRunInfo& info, Json::Value& root)
{
	root["engine"]["version"] = info.engineVersion;
	root["game"]["name"] = info.gameName;
	root["pattern"] = info.pattern;
	root["profile"] = info.profile;
}

Json::Value ScenarioToJson(const WorkbenchScenario& scenario, const WorkbenchRunInfo& info)
{
	Json::Value root;
	root["scenario"] = scenario.name;
	AddRunInfo(info, root);

	root["windows"] = Json::arrayValue;
	for (const WorkbenchWindow& w: scenario.windows) {
		Json::Value jw;
		jw["name"] = w.name;
		jw["frameTimeMs"] = SummaryToJson(w.frameMs);
		jw["drawTimeMs"] = SummaryToJson(w.drawMs);
		jw["gpuTimeMs"] = SummaryToJson(w.gpuMs);
		jw["simTimeMs"] = SummaryToJson(w.simMs);
		jw["simFrames"] = w.simFrames;
		jw["memoryMB"]["start"] = w.memStartMB;
		jw["memoryMB"]["peak"] = w.memPeakMB;
		jw["memoryMB"]["end"] = w.memEndMB;
		jw["memoryMB"]["growth"] = w.memEndMB - w.memStartMB;
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
	AddRunInfo(info, root);
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
	// create missing directories at write time: the default output dir is relative and
	// only meaningful after the engine has changed into its write dir
	std::error_code ec;
	if (const auto parent = std::filesystem::path(path).parent_path(); !parent.empty())
		std::filesystem::create_directories(parent, ec);

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
