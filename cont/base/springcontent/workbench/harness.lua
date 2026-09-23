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
--     synced = { fn = function(...) end }, -- optional; ctx.synced("fn", ...) fires and forgets,
--                                          -- ctx.call("fn", ...) waits and returns value or nil, err
--   }                                      -- (args and return values: numbers/strings only)

local H = {}
local MSG_PREFIX = "workbench:"
local SCENARIO_DIR = "workbench/scenarios/"
local DEFAULT_TIMEOUT = 300
local REPLY_PARAM = "workbench_reply_"

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
	-- layout: <callId>|<scenario.fn>|<args...>; callId is empty for fire-and-forget calls
	local callId, fnKey = parts[1], parts[2]
	local fn = syncedFns[fnKey]
	local reply
	if fn then
		local args = {}
		for i = 3, #parts do
			local v = parts[i]
			args[#args + 1] = tonumber(v) or v
		end
		local ok, ret = pcall(fn, unpack(args))
		if ok then
			reply = ret
		else
			Spring.Log("Workbench", LOG.ERROR, "synced " .. fnKey .. ": " .. tostring(ret))
			reply = "error: " .. tostring(ret)
		end
	else
		Spring.Log("Workbench", LOG.ERROR, "unknown synced function " .. tostring(fnKey))
		reply = "error: unknown synced function " .. tostring(fnKey)
	end
	if callId ~= "" then
		Spring.SetGameRulesParam(REPLY_PARAM .. callId, reply == nil and "" or reply)
	end
	return true
end

-- ---------------------------------------------------------------- unsynced side
local queue, current, co, startedAt = {}, nil, nil, nil
local frames = 0
-- call ids are unique per LuaUI instance: a reloaded LuaUI must never read the reply to a
-- previous instance's call (the reply params live in synced state and are not cleared).
-- Seeded from the load frame on first use; this file is also loaded by synced code,
-- where os does not exist, so nothing here may run at load time beyond plain values.
local nextCallId
local gameFrameQueue = {}

local function send(callId, sc, fnName, ...)
	local parts = { MSG_PREFIX .. callId, sc.name .. "." .. fnName }
	for _, v in ipairs({ ... }) do parts[#parts + 1] = tostring(v) end
	Spring.SendLuaRulesMsg(table.concat(parts, "|"))
end

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
		send("", sc, fnName, ...)
	end
	-- runs a synced function and returns its result (number or string), or nil plus an
	-- error message. It does not raise: Lua 5.1 cannot yield inside pcall, so callers
	-- could not catch an error from a call that waits for its reply.
	function ctx.call(fnName, ...)
		nextCallId = (nextCallId or Spring.GetGameFrame() * 100000) + 1
		local key = REPLY_PARAM .. nextCallId
		send(tostring(nextCallId), sc, fnName, ...)
		local replied = ctx.waitUntil(function() return Spring.GetGameRulesParam(key) ~= nil end, 10)
		if not replied then
			return nil, "no reply from synced " .. fnName
		end
		local v = Spring.GetGameRulesParam(key)
		if type(v) == "string" and v:sub(1, 7) == "error: " then
			return nil, "synced " .. fnName .. " failed: " .. v:sub(8)
		end
		return v
	end
	-- runs fn from the next GameFrame callin, i.e. during the sim step, before the
	-- engine's GUI update fires CommandsChanged/SelectionChanged for that frame; this
	-- is where physical input events land relative to widgets (emulated input issued
	-- from Update would otherwise be seen by later widgets in the same pass). Waits
	-- until fn has run.
	function ctx.atNextGameFrame(fn)
		local done = false
		gameFrameQueue[#gameFrameQueue + 1] = function() fn(); done = true end
		ctx.waitUntil(function() return done end, 30)
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

-- call from the widget's GameFrame callin
function H.GameFrame()
	if #gameFrameQueue == 0 then return end
	local q = gameFrameQueue
	gameFrameQueue = {}
	for _, fn in ipairs(q) do
		local ok, err = pcall(fn)
		if not ok then Spring.Log("Workbench", LOG.ERROR, "atNextGameFrame: " .. tostring(err)) end
	end
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
