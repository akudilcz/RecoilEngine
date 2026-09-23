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
	names.reserve(scenarios.size());
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
