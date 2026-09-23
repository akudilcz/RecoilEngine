-- Exercises the Spring.Workbench entry points, the harness synced round trip and
-- synced callin forwarding; run with --workbench api_selftest
local created = {} -- synced: unitID -> true, filled by the UnitCreated callin

return {
	name = "api_selftest",
	timeout = 30,
	synced = {
		double = function(x) return x * 2 end,
		fail = function() error("intentional") end,
		-- creates and removes a unit of any type; returns whether UnitCreated saw it
		spawnProbe = function()
			local _, def = next(UnitDefs)
			local x, z = Game.mapSizeX * 0.5, Game.mapSizeZ * 0.5
			local u = def and Spring.CreateUnit(def.id, x, Spring.GetGroundHeight(x, z), z, 0, Spring.GetGaiaTeamID())
			if not u then return "could not create a unit" end
			local seen = created[u] and 1 or 0
			Spring.DestroyUnit(u, false, true)
			return seen
		end,
	},
	syncedCallins = {
		UnitCreated = function(unitID)
			created[unitID] = true
		end,
	},
	run = function(ctx)
		local wb = Spring.Workbench
		ctx.check("is_active", wb.IsActive() == true, "IsActive")
		ctx.check("pattern", type(wb.GetPattern()) == "string", tostring(wb.GetPattern()))

		local v = ctx.call("double", 21)
		ctx.check("synced_call_roundtrip", v == 42, "double(21) returned " .. tostring(v))

		local failed, err = ctx.call("fail")
		ctx.check("synced_call_error_propagates", failed == nil and tostring(err):find("intentional") ~= nil, tostring(err))

		local seen, perr = ctx.call("spawnProbe")
		ctx.check("synced_callins_forwarded", seen == 1,
			seen == 0 and "UnitCreated did not reach the scenario (does the game's gadget forward harness.SYNCED_CALLINS?)"
			or tostring(perr or seen))

		wb.BeginWindow("idle")
		ctx.waitFrames(30)
		wb.EndWindow()
		ctx.check("window_closed", true, "")
	end,
}
