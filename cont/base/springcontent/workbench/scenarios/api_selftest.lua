-- Exercises the Spring.Workbench entry points and the harness synced round trip;
-- run with --workbench api_selftest
return {
	name = "api_selftest",
	timeout = 30,
	synced = {
		double = function(x) return x * 2 end,
		fail = function() error("intentional") end,
	},
	run = function(ctx)
		local wb = Spring.Workbench
		ctx.check("is_active", wb.IsActive() == true, "IsActive")
		ctx.check("pattern", type(wb.GetPattern()) == "string", tostring(wb.GetPattern()))

		local v = ctx.call("double", 21)
		ctx.check("synced_call_roundtrip", v == 42, "double(21) returned " .. tostring(v))

		local failed, err = ctx.call("fail")
		ctx.check("synced_call_error_propagates", failed == nil and tostring(err):find("intentional") ~= nil, tostring(err))

		wb.BeginWindow("idle")
		ctx.waitFrames(30)
		wb.EndWindow()
		ctx.check("window_closed", true, "")
	end,
}
