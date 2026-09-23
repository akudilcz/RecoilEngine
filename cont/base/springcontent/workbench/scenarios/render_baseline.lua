-- Game-agnostic render baseline: fixed camera path over the map, no units.
return {
	name = "render_baseline",
	timeout = 120,
	run = function(ctx)
		local mx, mz = Game.mapSizeX, Game.mapSizeZ
		local keys = {
			{ mx * 0.2, mz * 0.2 },
			{ mx * 0.8, mz * 0.2 },
			{ mx * 0.8, mz * 0.8 },
			{ mx * 0.2, mz * 0.8 },
		}
		ctx.waitSeconds(3) -- warm-up: shader compiles, texture streaming
		ctx.window("camera_path", function()
			for _, k in ipairs(keys) do
				Spring.SetCameraTarget(k[1], Spring.GetGroundHeight(k[1], k[2]), k[2], 2.0)
				ctx.waitSeconds(3)
			end
		end)
		ctx.check("completed", true, "")
	end,
}
