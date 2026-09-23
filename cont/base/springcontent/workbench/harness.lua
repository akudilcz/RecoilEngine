-- Recoil Workbench harness. Loaded by a game's opt-in widget (LuaUI) and gadget
-- (LuaRules) when Spring.Workbench.IsActive(). Runs scenarios from the game's
-- workbench/scenarios/ first (VFS shadows engine files of the same name), then
-- the engine's generic ones.
--
-- Scenario file contract:
--   return {
--     name = "unique_name",           -- matched against the --workbench pattern (comma-separated globs)
--     timeout = 120,                  -- seconds, optional (default 300)
--     run = function(ctx) ... end,    -- runs as a coroutine in LuaUI
--     synced = { fn = function(...) end }, -- optional; call with ctx.synced("fn", ...) (numbers/strings only)
--   }

local H = {}
local MSG_PREFIX = "workbench:"
local SCENARIO_DIR = "workbench/scenarios/"
local DEFAULT_TIMEOUT = 300

local function globToPattern(glob)
	local p = glob:gsub("[%^%$%(%)%%%.%[%]%+%-]", "%%%0"):gsub("%*", ".*"):gsub("%?", ".")
	return "^" .. p .. "$"
end

local function matches(name, spec)
	for part in spec:gmatch("[^,]+") do
		if name:find(globToPattern(part)) then
			return true
		end
	end
	return false
end

local function loadScenarios(spec)
	local files = VFS.DirList(SCENARIO_DIR, "*.lua")
	table.sort(files)
	local list, seen = {}, {}
	for _, file in ipairs(files) do
		local ok, sc = pcall(VFS.Include, file)
		if ok and type(sc) == "table" and type(sc.name) == "string" then
			if not seen[sc.name] and matches(sc.name, spec) then
				seen[sc.name] = true
				list[#list + 1] = sc
			end
		elseif not ok then
			Spring.Log("Workbench", LOG.ERROR, "failed to load " .. file .. ": " .. tostring(sc))
		end
	end
	return list
end

-- ------------------------------------------------------------------ synced side
local syncedFns = {}

function H.StartSynced()
	for _, sc in ipairs(loadScenarios(Spring.Workbench.GetPattern())) do
		for fnName, fn in pairs(sc.synced or {}) do
			syncedFns[sc.name .. "." .. fnName] = fn
		end
	end
end

-- called from the gadget's synced RecvLuaMsg
function H.RecvSynced(msg)
	if msg:sub(1, #MSG_PREFIX) ~= MSG_PREFIX then
		return false
	end
	local parts = {}
	for field in (msg:sub(#MSG_PREFIX + 1) .. "|"):gmatch("([^|]*)|") do
		parts[#parts + 1] = field
	end
	local fn = syncedFns[parts[1]]
	if fn then
		local args = {}
		for i = 2, #parts do
			local v = parts[i]
			args[#args + 1] = tonumber(v) or v
		end
		local ok, err = pcall(fn, unpack(args))
		if not ok then
			Spring.Log("Workbench", LOG.ERROR, "synced " .. parts[1] .. ": " .. tostring(err))
		end
	else
		Spring.Log("Workbench", LOG.ERROR, "unknown synced function " .. tostring(parts[1]))
	end
	return true
end

-- ---------------------------------------------------------------- unsynced side
local queue, current, co, startedAt = {}, nil, nil, nil
local frames = 0

local function makeCtx(sc)
	local ctx = {}
	function ctx.waitFrames(n)
		local target = frames + n
		while frames < target do coroutine.yield() end
	end
	function ctx.waitSimFrames(n)
		local target = Spring.GetGameFrame() + n
		while Spring.GetGameFrame() < target do coroutine.yield() end
	end
	function ctx.waitSeconds(s)
		local t0 = Spring.GetTimer()
		while Spring.DiffTimers(Spring.GetTimer(), t0) < s do coroutine.yield() end
	end
	function ctx.waitUntil(pred, timeoutSec)
		local t0 = Spring.GetTimer()
		while not pred() do
			if Spring.DiffTimers(Spring.GetTimer(), t0) > timeoutSec then return false end
			coroutine.yield()
		end
		return true
	end
	function ctx.window(name, fn)
		Spring.Workbench.BeginWindow(name)
		fn()
		Spring.Workbench.EndWindow()
	end
	function ctx.check(name, pass, detail)
		Spring.Workbench.Check(name, pass and true or false, detail and tostring(detail) or "")
	end
	function ctx.synced(fnName, ...)
		local parts = { MSG_PREFIX .. sc.name .. "." .. fnName }
		for _, v in ipairs({ ... }) do parts[#parts + 1] = tostring(v) end
		Spring.SendLuaRulesMsg(table.concat(parts, "|"))
	end
	function ctx.log(msg)
		Spring.Echo("[Workbench] " .. sc.name .. ": " .. tostring(msg))
	end
	return ctx
end

local function startNext()
	current = table.remove(queue, 1)
	if not current then
		co = nil
		Spring.Workbench.FinishRun()
		return
	end
	Spring.Workbench.BeginScenario(current.name)
	startedAt = Spring.GetTimer()
	local ctx = makeCtx(current)
	co = coroutine.create(function() current.run(ctx) end)
end

function H.Start()
	local spec = Spring.Workbench.GetPattern()
	queue = loadScenarios(spec)
	if #queue == 0 then
		Spring.Workbench.RunError("no scenarios matched " .. spec)
		Spring.Workbench.FinishRun()
		return
	end
	startNext()
end

-- call every LuaUI Update
function H.Update()
	frames = frames + 1
	if not co then return end

	local limit = current.timeout or DEFAULT_TIMEOUT
	if Spring.DiffTimers(Spring.GetTimer(), startedAt) > limit then
		Spring.Workbench.Error("scenario timeout after " .. limit .. "s")
		co = nil
	else
		local ok, err = coroutine.resume(co)
		if not ok then
			Spring.Workbench.Error(tostring(err))
			co = nil
		elseif coroutine.status(co) == "dead" then
			co = nil
		end
	end

	if not co then
		Spring.Workbench.EndScenario()
		startNext()
	end
end

return H
