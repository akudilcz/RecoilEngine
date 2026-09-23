/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */
#pragma once

struct lua_State;

// Spring.Workbench: Lua access to the Recoil Workbench (see System/Workbench).
// Unsynced handles get the full table; synced handles only read-only queries,
// because nothing reachable from synced Lua may affect simulation state.
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
