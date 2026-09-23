-- Exercises the Spring.Workbench entry points; run with --workbench api_selftest
return {
	name = "api_selftest",
	timeout = 30,
	run = function(ctx)
		local wb = Spring.Workbench
		ctx.check("is_active", wb.IsActive() == true, "IsActive")
		ctx.check("pattern", type(wb.GetPattern()) == "string", tostring(wb.GetPattern()))
		wb.BeginWindow("idle")
		ctx.waitFrames(30)
		wb.EndWindow()
		ctx.check("window_closed", true, "")
	end,
}
