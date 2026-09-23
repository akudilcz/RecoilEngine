/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "CommandDrawer.h"
#include "LineDrawer.h"
#include "Game/Camera.h"
#include "Game/GameHelper.h"
#include "Game/UI/CommandColors.h"
#include "Game/WaitCommandsAI.h"
#include "Map/Ground.h"
#include "Rendering/GL/glExtra.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Units/CommandAI/Command.h"
#include "Sim/Units/CommandAI/CommandQueue.h"
#include "Sim/Units/CommandAI/CommandAI.h"
#include "Sim/Units/CommandAI/AirCAI.h"
#include "Sim/Units/CommandAI/BuilderCAI.h"
#include "Sim/Units/CommandAI/FactoryCAI.h"
#include "Sim/Units/CommandAI/MobileCAI.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/UnitDefHandler.h"
#include "System/SpringMath.h"
#include "System/Log/ILog.h"

static const CUnit* GetTrackableUnit(const CUnit* caiOwner, const CUnit* cmdUnit)
{
	if (cmdUnit == nullptr)
		return nullptr;
	if ((cmdUnit->losStatus[caiOwner->allyteam] & (LOS_INLOS | LOS_INRADAR)) == 0)
		return nullptr;

	return cmdUnit;
}

CommandDrawer* CommandDrawer::GetInstance() {
	// luaQueuedUnitSet gets cleared each frame, so this is fine wrt. reloading
	static CommandDrawer drawer;
	return &drawer;
}



void CommandDrawer::Draw(const CCommandAI* cai, int queueDrawDepth) const {
	// note: {Air,Builder}CAI inherit from MobileCAI, so test that last
	if ((dynamic_cast<const     CAirCAI*>(cai)) != nullptr) {     DrawAirCAICommands(static_cast<const     CAirCAI*>(cai), queueDrawDepth); return; }
	if ((dynamic_cast<const CBuilderCAI*>(cai)) != nullptr) { DrawBuilderCAICommands(static_cast<const CBuilderCAI*>(cai), queueDrawDepth); return; }
	if ((dynamic_cast<const CFactoryCAI*>(cai)) != nullptr) { DrawFactoryCAICommands(static_cast<const CFactoryCAI*>(cai), queueDrawDepth); return; }
	if ((dynamic_cast<const  CMobileCAI*>(cai)) != nullptr) {  DrawMobileCAICommands(static_cast<const  CMobileCAI*>(cai), queueDrawDepth); return; }

	DrawCommands(cai, queueDrawDepth);
}



void CommandDrawer::AddLuaQueuedUnit(const CUnit* unit, int queueDrawDepth) {
	// needs to insert by id, pointers can become dangling
	luaQueuedUnitSet.insert({ unit->id, queueDrawDepth });
}

void CommandDrawer::DrawLuaQueuedUnitSetCommands() const
{
	if (luaQueuedUnitSet.empty())
		return;

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);

	lineDrawer.Configure(cmdColors.UseColorRestarts(),
	                     cmdColors.UseRestartColor(),
	                     cmdColors.restart,
	                     cmdColors.RestartAlpha());
	lineDrawer.SetupLineStipple();

	glEnable(GL_BLEND);
	glBlendFunc((GLenum)cmdColors.QueuedBlendSrc(),
	            (GLenum)cmdColors.QueuedBlendDst());

	glLineWidth(cmdColors.QueuedLineWidth());

	for (const auto& [unitID, qDrawDepth] : luaQueuedUnitSet) {
		const CUnit* unit = unitHandler.GetUnit(unitID);

		if (unit == nullptr || unit->commandAI == nullptr)
			continue;

		Draw(unit->commandAI, qDrawDepth);
	}

	glLineWidth(1.0f);
	glEnable(GL_DEPTH_TEST);
}

void CommandDrawer::DrawCommands(const CCommandAI* cai, int queueDrawDepth) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	if (queueDrawDepth <= 0)
		queueDrawDepth = commandQue.size();

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		if (std::distance(commandQue.begin(), ci) > queueDrawDepth)
			break;

		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_ATTACK:
			case CMD_MANUALFIRE: {
				if (ci->GetNumParams() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci->GetNumParams() >= 3);

					const float x = ci->GetParam(0);
					const float z = ci->GetParam(2);
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner);
			} break;
		}
	}

	lineDrawer.FinishPath();
}



void CommandDrawer::DrawAirCAICommands(const CAirCAI* cai, int queueDrawDepth) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	if (queueDrawDepth <= 0)
		queueDrawDepth = commandQue.size();

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		if (std::distance(commandQue.begin(), ci) > queueDrawDepth)
			break;

		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.move);
			} break;
			case CMD_FIGHT: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.fight);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.patrol);
			} break;

			case CMD_ATTACK: {
				if (ci->GetNumParams() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci->GetNumParams() >= 3);

					const float x = ci->GetParam(0);
					const float z = ci->GetParam(2);
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_AREA_ATTACK: {
				const float3& endPos = ci->GetPos(0);

				lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.attack);
				lineDrawer.Break(endPos, cmdColors.attack);

				glSurfaceCircle(endPos, ci->GetParam(3), { cmdColors.attack }, cmdCircleResolution);

				lineDrawer.RestartWithColor(cmdColors.attack);
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner);
			} break;
		}
	}

	lineDrawer.FinishPath();
}



void CommandDrawer::DrawBuilderCAICommands(const CBuilderCAI* cai, int queueDrawDepth) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	if (queueDrawDepth <= 0)
		queueDrawDepth = commandQue.size();

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		if (std::distance(commandQue.begin(), ci) > queueDrawDepth)
			break;

		const int cmdID = ci->GetID();

		if (cmdID < 0) {
			if (cai->buildOptions.find(cmdID) != cai->buildOptions.end()) {
				BuildInfo bi;

				if (!bi.Parse(*ci))
					continue;

				cursorIcons.AddBuildIcon(cmdID, bi.pos, owner->team, bi.buildFacing);
				lineDrawer.DrawLine(bi.pos, cmdColors.build);

				// draw metal extraction range
				if (bi.def->extractRange > 0.0f) {
					lineDrawer.Break(bi.pos, cmdColors.build);
					glSurfaceCircle(bi.pos, bi.def->extractRange, { cmdColors.rangeExtract }, 40.0f);
					lineDrawer.Restart();
				}
			}
			continue;
		}

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.move);
			} break;
			case CMD_FIGHT:{
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.fight);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.patrol);
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_RESTORE: {
				const float3& endPos = ci->GetPos(0);

				lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.restore);
				lineDrawer.Break(endPos, cmdColors.restore);

				glSurfaceCircle(endPos, ci->GetParam(3), { cmdColors.restore }, cmdCircleResolution);

				lineDrawer.RestartWithColor(cmdColors.restore);
			} break;

			case CMD_ATTACK:
			case CMD_MANUALFIRE: {
				if (ci->GetNumParams() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci->GetNumParams() >= 3);

					const float x = ci->GetParam(0);
					const float z = ci->GetParam(2);
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_RECLAIM:
			case CMD_RESURRECT: {
				const float* color = (cmdID == CMD_RECLAIM) ? cmdColors.reclaim
				                                             : cmdColors.resurrect;
				if (ci->GetNumParams() == 4) {
					const float3& endPos = ci->GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, color);
					lineDrawer.Break(endPos, color);

					glSurfaceCircle(endPos, ci->GetParam(3), { color }, cmdCircleResolution);

					lineDrawer.RestartWithColor(color);
				} else {
					assert(ci->GetParam(0) >= 0.0f);

					const unsigned int id = std::max(0.0f, ci->GetParam(0));

					if (id >= unitHandler.MaxUnits()) {
						const CFeature* feature = featureHandler.GetFeature(id - unitHandler.MaxUnits());

						if (feature != nullptr)
							lineDrawer.DrawLineAndIcon(cmdID, feature->GetObjDrawMidPos(), color);

					} else {
						const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(id));

						if (unit != nullptr && unit != owner)
							lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), color);

					}
				}
			} break;

			case CMD_REPAIR:
			case CMD_CAPTURE: {
				const float* color = (ci->GetID() == CMD_REPAIR) ? cmdColors.repair: cmdColors.capture;

				if (ci->GetNumParams() == 4) {
					const float3& endPos = ci->GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, color);
					lineDrawer.Break(endPos, color);

					glSurfaceCircle(endPos, ci->GetParam(3), { color }, cmdCircleResolution);

					lineDrawer.RestartWithColor(color);
				} else {
					if (ci->GetNumParams() >= 1) {
						const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

						if (unit != nullptr)
							lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), color);

					}
				}
			} break;

			case CMD_LOAD_ONTO: {
				const CUnit* unit = unitHandler.GetUnitUnsafe(ci->GetParam(0));
				lineDrawer.DrawLineAndIcon(cmdID, unit->pos, cmdColors.load);
			} break;
			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(ci->GetID());
			} break;

			default: {
				DrawDefaultCommand(*ci, owner);
			} break;
		}
	}

	lineDrawer.FinishPath();
}



void CommandDrawer::DrawFactoryCAICommands(const CFactoryCAI* cai, int queueDrawDepth) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;
	const CCommandQueue& newUnitCommands = cai->newUnitCommands;

	if (queueDrawDepth <= 0)
		queueDrawDepth = newUnitCommands.size();

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	if (!commandQue.empty() && (commandQue.front().GetID() == CMD_WAIT))
		DrawWaitIcon(commandQue.front());

	for (auto ci = newUnitCommands.begin(); ci != newUnitCommands.end(); ++ci) {
		if (std::distance(newUnitCommands.begin(), ci) > queueDrawDepth)
			break;

		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0) + UpVector * 3.0f, cmdColors.move);
			} break;
			case CMD_FIGHT: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0) + UpVector * 3.0f, cmdColors.fight);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0) + UpVector * 3.0f, cmdColors.patrol);
			} break;

			case CMD_ATTACK: {
				if (ci->GetNumParams() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci->GetNumParams() >= 3);

					const float x = ci->GetParam(0);
					const float z = ci->GetParam(2);
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner);
			} break;
		}

		if ((cmdID < 0) && (ci->GetNumParams() >= 3)) {
			BuildInfo bi;

			if (!bi.Parse(*ci))
				continue;

			cursorIcons.AddBuildIcon(cmdID, bi.pos, owner->team, bi.buildFacing);
			lineDrawer.DrawLine(bi.pos, cmdColors.build);

			// draw metal extraction range
			if (bi.def->extractRange > 0.0f) {
				lineDrawer.Break(bi.pos, cmdColors.build);
				glSurfaceCircle(bi.pos, bi.def->extractRange, { cmdColors.rangeExtract }, 40.0f);
				lineDrawer.Restart();
			}
		}
	}

	lineDrawer.FinishPath();
}



void CommandDrawer::DrawMobileCAICommands(const CMobileCAI* cai, int queueDrawDepth) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	if (queueDrawDepth <= 0)
		queueDrawDepth = commandQue.size();

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.move);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.patrol);
			} break;
			case CMD_FIGHT: {
				if (ci->GetNumParams() >= 3)
					lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.fight);

			} break;

			case CMD_ATTACK:
			case CMD_MANUALFIRE: {
				if (ci->GetNumParams() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				}

				if (ci->GetNumParams() >= 3) {
					const float x = ci->GetParam(0);
					const float z = ci->GetParam(2);
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_LOAD_ONTO: {
				const CUnit* unit = unitHandler.GetUnitUnsafe(ci->GetParam(0));
				lineDrawer.DrawLineAndIcon(cmdID, unit->pos, cmdColors.load);
			} break;

			case CMD_LOAD_UNITS: {
				if (ci->GetNumParams() == 4) {
					const float3& endPos = ci->GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.load);
					lineDrawer.Break(endPos, cmdColors.load);

					glSurfaceCircle(endPos, ci->GetParam(3), { cmdColors.load }, cmdCircleResolution);

					lineDrawer.RestartWithColor(cmdColors.load);
				} else {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->GetParam(0)));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.load);

				}
			} break;

			case CMD_UNLOAD_UNITS: {
				if (ci->GetNumParams() == 5) {
					const float3& endPos = ci->GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.unload);
					lineDrawer.Break(endPos, cmdColors.unload);

					glSurfaceCircle(endPos, ci->GetParam(3), { cmdColors.unload }, cmdCircleResolution);

					lineDrawer.RestartWithColor(cmdColors.unload);
				}
			} break;

			case CMD_UNLOAD_UNIT: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.unload);
			} break;
			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner);
			} break;
		}
	}

	lineDrawer.FinishPath();
}


void CommandDrawer::DrawWaitIcon(const Command& cmd) const
{
	waitCommandsAI.AddIcon(cmd, lineDrawer.GetLastPos());
}

void CommandDrawer::DrawDefaultCommand(const Command& c, const CUnit* owner) const
{
	// TODO add Lua callin perhaps, for more elaborate needs?
	const CCommandColors::DrawData* dd = cmdColors.GetCustomCmdData(c.GetID());

	if (dd == nullptr)
		return;

	switch (c.GetNumParams()) {
		case  0: { return; } break;
		case  1: {         } break;
		case  2: {         } break;
		default: {
			const float3 endPos = c.GetPos(0) + UpVector * 3.0f;

			if (!dd->showArea || (c.GetNumParams() < 4)) {
				lineDrawer.DrawLineAndIcon(dd->cmdIconID, endPos, dd->color);
			} else {
				lineDrawer.DrawLineAndIcon(dd->cmdIconID, endPos, dd->color);
				lineDrawer.Break(endPos, dd->color);
				glSurfaceCircle(endPos, c.GetParam(3), { dd->color }, cmdCircleResolution);
				lineDrawer.RestartWithColor(dd->color);
			}

			return;
		}
	}

	// allow a second param (ignored here) for custom commands
	const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(c.GetParam(0)));

	if (unit == nullptr)
		return;

	lineDrawer.DrawLineAndIcon(dd->cmdIconID, unit->GetObjDrawErrorPos(owner->allyteam), dd->color);
}

void CommandDrawer::DrawQuedBuildingSquares(const std::vector<const CBuilderCAI*>& myBuilderCAIs, const std::vector<const CBuilderCAI*>& allyBuilderCAIs) const
{
	struct QueuedBuild {
		float3 pos;
		float xsize;
		float zsize;
		bool underwater;
		SColor color;
	};

	// persistent across calls, only cleared (not freed) each frame
	static std::vector<QueuedBuild> builds;
	builds.clear();

	const auto parseInto = [&builds](const std::vector<const CBuilderCAI*>& cais, const SColor& color) {
		for (const CBuilderCAI* cai : cais) {
			const CCommandQueue& commandQue = cai->commandQue;
			const auto& buildOptions = cai->buildOptions;

			for (const Command& c: commandQue) {
				if (buildOptions.find(c.GetID()) == buildOptions.end())
					continue;

				BuildInfo bi;

				if (!bi.Parse(c))
					continue;

				bi.pos = CGameHelper::Pos2BuildPos(bi, false);

				const float xsize = bi.GetXSize() * (SQUARE_SIZE >> 1);
				const float zsize = bi.GetZSize() * (SQUARE_SIZE >> 1);
				const float radius = math::sqrt(xsize * xsize + zsize * zsize);

				// cull build positions that are not visible
				if (!camera->InView(bi.pos, radius))
					continue;

				builds.push_back({ bi.pos, xsize, zsize, (bi.pos.y < 0.0f), color });
			}
		}
	};

	parseInto(myBuilderCAIs, SColor(cmdColors.buildBox));
	parseInto(allyBuilderCAIs, SColor(cmdColors.allyBuildBox));

	if (builds.empty())
		return;

	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
	using IndcType = uint32_t;

	static constexpr SColor capColor    (0.0f, 0.5f, 1.0f, 1.0f); // same as end color of lines
	static constexpr SColor lineColorTop(0.0f, 0.0f, 1.0f, 0.5f); // start color
	static constexpr SColor lineColorBot(0.0f, 0.5f, 1.0f, 1.0f); // end color

	const auto addLine = [&rb](const float3& p0, const SColor& c0, const float3& p1, const SColor& c1) {
		const IndcType base = rb.GetBaseVertex();

		rb.AddVertex({ p0, c0 });
		rb.AddVertex({ p1, c1 });
		rb.AddIndices(std::vector<IndcType>{ base, static_cast<IndcType>(base + 1) });
	};

	for (const QueuedBuild& b : builds) {
		const float x1 = b.pos.x - b.xsize;
		const float z1 = b.pos.z - b.zsize;
		const float x2 = b.pos.x + b.xsize;
		const float z2 = b.pos.z + b.zsize;
		const float h = b.pos.y + 1.0f;

		rb.AddQuadLines(
			VA_TYPE_C{ float3(x1, h, z1), b.color },
			VA_TYPE_C{ float3(x1, h, z2), b.color },
			VA_TYPE_C{ float3(x2, h, z2), b.color },
			VA_TYPE_C{ float3(x2, h, z1), b.color }
		);

		if (!b.underwater)
			continue;

		rb.AddQuadLines(
			VA_TYPE_C{ float3(x1, 0.0f, z1), capColor },
			VA_TYPE_C{ float3(x1, 0.0f, z2), capColor },
			VA_TYPE_C{ float3(x2, 0.0f, z2), capColor },
			VA_TYPE_C{ float3(x2, 0.0f, z1), capColor }
		);

		addLine(float3(x1, b.pos.y, z1), lineColorTop, float3(x1, 0.0f, z1), lineColorBot);
		addLine(float3(x2, b.pos.y, z1), lineColorTop, float3(x2, 0.0f, z1), lineColorBot);
		addLine(float3(x2, b.pos.y, z2), lineColorTop, float3(x2, 0.0f, z2), lineColorBot);
		addLine(float3(x1, b.pos.y, z2), lineColorTop, float3(x1, 0.0f, z2), lineColorBot);
	}

	auto& shader = rb.GetShader();
	shader.Enable();
	rb.DrawElements(GL_LINES);
	shader.Disable();
}

