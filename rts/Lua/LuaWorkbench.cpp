/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "LuaWorkbench.h"
#include "LuaUtils.h"
#include "System/Workbench/Workbench.h"

/***
 * @function Spring.Workbench.IsActive
 * @return boolean active true when the engine was started with --workbench
 */
int LuaWorkbench::IsActive(lua_State* L) { lua_pushboolean(L, workbench.IsActive()); return 1; }

/***
 * @function Spring.Workbench.GetPattern
 * @return string pattern the --workbench scenario pattern (comma-separated globs)
 */
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
	lua_pushliteral(L, "Workbench");
	lua_createtable(L, 0, 2);
	REGISTER_LUA_CFUNC(IsActive);
	REGISTER_LUA_CFUNC(GetPattern);
	lua_rawset(L, -3);
	return true;
}
