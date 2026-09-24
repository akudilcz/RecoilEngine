-- Regression checks for Lua API fixes carried by this fork, run in the real engine with
-- any game (only needs one UnitDef). Each check names the fix it guards.
local units = {} -- synced: probe units created by spawn

local function anyUnitDef()
	for _, def in pairs(UnitDefs) do
		if not def.isBuilding and (def.speed or 0) > 0 then return def end
	end
	local _, def = next(UnitDefs)
	return def
end

return {
	name = "lua_api_regressions",
	timeout = 60,
	synced = {
		-- two probe units of the local player's team (so LuaUI may SetUnitNoDraw them and
		-- they are in its LOS): one at (cx, cz), one 2000 elmos east; returns "idNear,idFar"
		spawn = function(team)
			local def = anyUnitDef()
			local cx, cz = Game.mapSizeX * 0.25, Game.mapSizeZ * 0.5
			local a = Spring.CreateUnit(def.id, cx, Spring.GetGroundHeight(cx, cz), cz, 0, team)
			local b = Spring.CreateUnit(def.id, cx + 2000, Spring.GetGroundHeight(cx + 2000, cz), cz, 0, team)
			if not (a and b) then return "could not create probe units" end
			units = { a, b }
			return a .. "," .. b
		end,
		-- GetUnitsInPlanes with the optional allegiance argument used to index the
		-- wrong stack slot and crash (upstream PR #3214); half-space x <= cx + 100
		unitsWestOf = function(team)
			local cx = Game.mapSizeX * 0.25
			local found = Spring.GetUnitsInPlanes({ { 1, 0, 0, -(cx + 100) } }, team) or {}
			local set = {}
			for _, u in ipairs(found) do set[u] = true end
			return (set[units[1]] and "1" or "0") .. (set[units[2]] and "1" or "0")
		end,
		clear = function()
			for _, u in ipairs(units) do
				if Spring.ValidUnitID(u) then Spring.DestroyUnit(u, false, true) end
			end
			units = {}
			return 0
		end,
	},
	run = function(ctx)
		-- GetCEGID for an undefined CEG must not allocate an id (unsynced calls doing so
		-- desynced clients; upstream PR #3284)
		ctx.check("GetCEGID_undefined_is_nil", Spring.GetCEGID("workbench_no_such_ceg") == nil,
			"GetCEGID('workbench_no_such_ceg') returned " .. tostring(Spring.GetCEGID("workbench_no_such_ceg")))

		local ids, err = ctx.call("spawn", Spring.GetMyTeamID())
		local near, far = tostring(ids):match("^(%d+),(%d+)$")
		if not near then
			ctx.check("probe_units", false, tostring(err or ids))
			return
		end
		near = tonumber(near)
		ctx.waitSimFrames(2)

		local inPlanes, perr = ctx.call("unitsWestOf", Spring.GetMyTeamID())
		ctx.check("GetUnitsInPlanes_with_allegiance", inPlanes == "10",
			"near/far unit in the half-space: " .. tostring(perr or inPlanes) .. " (expected 10)")

		-- GetVisibleUnits caches its result per frame; SetUnitNoDraw in the same frame
		-- must invalidate it (this fork's cache, fixed after review)
		Spring.SetCameraTarget(Game.mapSizeX * 0.25, Spring.GetGroundHeight(Game.mapSizeX * 0.25, Game.mapSizeZ * 0.5), Game.mapSizeZ * 0.5, 0)
		ctx.waitFrames(5)
		local function visible()
			for _, u in ipairs(Spring.GetVisibleUnits(-1, 30, true) or {}) do
				if u == near then return true end
			end
			return false
		end
		local before = visible()
		Spring.SetUnitNoDraw(near, true)
		local after = visible() -- same frame: a stale cache would still list it
		Spring.SetUnitNoDraw(near, false)
		ctx.check("GetVisibleUnits_sees_SetUnitNoDraw", before and not after,
			string.format("visible before %s, after SetUnitNoDraw(true) in the same frame %s", tostring(before), tostring(after)))

		ctx.call("clear")
	end,
}
