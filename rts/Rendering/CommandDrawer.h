/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <vector>

#include "System/UnorderedMap.hpp"
#include "System/UnorderedSet.hpp"

struct Command;
class CCommandAI;
class CAirCAI;
class CBuilderCAI;
class CFactoryCAI;
class CMobileCAI;
class CUnit;

using CancelledBuildCommandTags = spring::unordered_map<int, spring::unordered_set<unsigned int>>;

struct CommandDrawer {
public:
	static CommandDrawer* GetInstance();

	// clear the set after WorldDrawer and MiniMap have both used it
	void Update() { luaQueuedUnitSet.clear(); }

	void Draw(const CCommandAI*, int queueDrawDepth = -1) const;
	void DrawLuaQueuedUnitSetCommands() const;
	// batches the build-site outline boxes for all given builders (grouped by
	// team-color) into a single render-buffer submission. Queued builds a pending
	// placement would cancel (keyed by builder unit ID, then command tag) get
	// cmdColors.buildBoxCancel. cullToCamera drops builds outside the camera
	// frustum; the minimap pass shows the whole map and must not cull.
	void DrawQuedBuildingSquares(
		const std::vector<const CBuilderCAI*>& myBuilderCAIs,
		const std::vector<const CBuilderCAI*>& allyBuilderCAIs,
		const CancelledBuildCommandTags& cancelledCommandTags,
		bool cullToCamera
	) const;

	void AddLuaQueuedUnit(const CUnit* unit, int queueDrawDepth = 0);

private:
	void DrawCommands(const CCommandAI*, int queueDrawDepth) const;
	void DrawAirCAICommands(const CAirCAI*, int queueDrawDepth) const;
	void DrawBuilderCAICommands(const CBuilderCAI*, int queueDrawDepth) const;
	void DrawFactoryCAICommands(const CFactoryCAI*, int queueDrawDepth) const;
	void DrawMobileCAICommands(const CMobileCAI*, int queueDrawDepth) const;

	void DrawWaitIcon(const Command&) const;
	void DrawDefaultCommand(const Command&, const CUnit*) const;

private:
	spring::unordered_set<std::pair<int, int>> luaQueuedUnitSet; //unitID, queueDepth (if > 0)
	static constexpr uint32_t cmdCircleResolution = 100;
};

#define commandDrawer (CommandDrawer::GetInstance())
