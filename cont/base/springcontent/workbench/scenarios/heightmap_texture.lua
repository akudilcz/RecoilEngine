-- Game-agnostic: after terrain deformation, the heightmap texture the ground is drawn
-- from (SMFVertProg samples heightMapTex for every ground vertex) must match the
-- unsynced heightmap. A stale or corrupted texel draws the ground at the wrong height
-- while units keep the real one, so they appear to move underneath it.
--
-- Each round deforms a patch with many small rectangles per sim frame, as explosion
-- craters do, so the draw side applies several partial texture updates per frame; then
-- it reads the texture back texel by texel and compares it with Spring.GetGroundHeight
-- at the same heightmap corners. GUI engine only (needs GL).

local SIDE = 128 -- heightmap squares per side of the deformed patch (texels read back: SIDE + 1)
local ROUNDS = 6
local BURST_FRAMES = 45
local RECTS_PER_FRAME = 24
local TOLERANCE = 0.01 -- elmos; both sides hold the same float

local function patchOrigin()
	-- whole squares, centred on the map
	return math.floor(Game.mapSizeX / 16) - SIDE / 2, math.floor(Game.mapSizeZ / 16) - SIDE / 2
end

local VS = [[
#version 130
void main() { gl_Position = gl_Vertex; }
]]
local FS = [[
#version 130
uniform sampler2D heightMapTex;
uniform ivec2 origin;
void main() {
	float h = texelFetch(heightMapTex, origin + ivec2(gl_FragCoord.xy), 0).x;
	gl_FragColor = vec4(h, 0.0, 0.0, 1.0);
}
]]

return {
	name = "heightmap_texture",
	timeout = 300,
	synced = {
		-- height changes reach the unsynced heightmap (and the texture) only where the
		-- local player's ally team has LOS (CReadMap::HeightMapUpdateLOSCheck)
		globalLos = function(allyTeam, enabled)
			Spring.SetGlobalLos(allyTeam, enabled == 1)
			return 0
		end,
		level = function(height)
			local ox, oz = patchOrigin()
			Spring.LevelHeightMap(ox * 8, oz * 8, (ox + SIDE) * 8, (oz + SIDE) * 8, height)
			return 0
		end,
		-- RECTS_PER_FRAME raises/lowers of random small rectangles inside the patch
		burst = function(seed)
			local ox, oz = patchOrigin()
			local rnd = seed
			-- Park-Miller: synced-safe and reproducible; the product stays below 2^53,
			-- so doubles hold it exactly
			local function nextRandom(n)
				rnd = (rnd * 16807) % 2147483647
				return rnd % n
			end
			for _ = 1, RECTS_PER_FRAME do
				local w, h = 2 + nextRandom(14), 2 + nextRandom(14)
				local x1, z1 = ox + nextRandom(SIDE - w), oz + nextRandom(SIDE - h)
				Spring.AdjustHeightMap(x1 * 8, z1 * 8, (x1 + w) * 8, (z1 + h) * 8, nextRandom(41) - 20)
			end
			return 0
		end,
	},
	run = function(ctx)
		local GL_R = assert(GL.RGBA32F, "GL.RGBA32F missing")
		local shader = gl.CreateShader({ vertex = VS, fragment = FS, uniformInt = { heightMapTex = 0 } })
		if not shader then
			ctx.check("shader", false, gl.GetShaderLog())
			return
		end
		local originLoc = gl.GetUniformLocation(shader, "origin")
		local n = SIDE + 1
		local fbo = gl.CreateTexture(n, n, {
			format = GL_R, border = false, min_filter = GL.NEAREST, mag_filter = GL.NEAREST,
			wrap_s = GL.CLAMP_TO_EDGE, wrap_t = GL.CLAMP_TO_EDGE, fbo = true,
		})
		if not fbo then
			ctx.check("fbo", false, "could not create a " .. n .. "x" .. n .. " RGBA32F FBO texture")
			return
		end

		local ox, oz = patchOrigin()
		local function readBack()
			local res
			ctx.atNextDraw(function()
				gl.RenderToTexture(fbo, function()
					gl.Blending(false)
					gl.DepthTest(false)
					gl.UseShader(shader)
					gl.UniformInt(originLoc, ox, oz)
					gl.Texture(0, "$heightmap")
					gl.TexRect(-1, -1, 1, 1)
					gl.Texture(0, false)
					gl.UseShader(0)
					res = gl.ReadPixels(0, 0, n, n)
					gl.Blending(true)
				end)
			end)
			return res
		end

		-- mismatching texels between the texture and the unsynced heightmap
		local function compare()
			local res = readBack()
			if not res then return nil, "no readback" end
			local bad, worst, worstAt = 0, 0, ""
			for row = 1, n do
				local r = res[row]
				local z = oz + row - 1
				for col = 1, n do
					local x = ox + col - 1
					local d = math.abs(r[col][1] - Spring.GetGroundHeight(x * 8, z * 8))
					if d > TOLERANCE then
						bad = bad + 1
						if d > worst then worst, worstAt = d, string.format("(%d,%d)", x, z) end
					end
				end
			end
			return bad, string.format("worst %.2f elmos at square %s", worst, worstAt)
		end

		-- the draw side applies queued height updates (MAX_UHM_RECTS_PER_FRAME rectangles
		-- per draw frame) after the sim step that made them
		local function settle()
			ctx.waitSimFrames(2)
			ctx.waitFrames(20)
		end
		-- distinct heights in the patch: proves the deformation took effect
		local function distinctHeights()
			local seen, count = {}, 0
			for z = oz, oz + SIDE, 4 do
				for x = ox, ox + SIDE, 4 do
					local h = Spring.GetGroundHeight(x * 8, z * 8)
					if not seen[h] then seen[h], count = true, count + 1 end
				end
			end
			return count
		end

		local allyTeam = Spring.GetMyAllyTeamID()
		local _, err = ctx.call("globalLos", allyTeam, 1)
		if err then
			ctx.check("globalLos", false, err)
			return
		end
		_, err = ctx.call("level", 100)
		if err then
			ctx.check("level", false, err)
			return
		end
		settle()
		ctx.check("levelled", distinctHeights() == 1, distinctHeights() .. " distinct heights after levelling")
		local bad, detail = compare()
		ctx.check("matches_before_deformation", bad == 0, (bad or "?") .. " of " .. n * n .. " texels differ; " .. tostring(detail))

		local totalBad, rounds = 0, {}
		for round = 1, ROUNDS do
			for f = 1, BURST_FRAMES do
				_, err = ctx.call("burst", round * 100000 + f) -- one burst per sim frame
				if err then
					ctx.check("burst", false, err)
					return
				end
			end
			settle()
			bad, detail = compare()
			rounds[#rounds + 1] = tostring(bad)
			totalBad = totalBad + (bad or n * n)
		end
		local distinct = distinctHeights()
		ctx.check("deformed", distinct > 50, distinct .. " distinct heights sampled after deformation")
		ctx.check("matches_after_deformation", totalBad == 0,
			"texels differing per round: " .. table.concat(rounds, ",") .. " of " .. n * n .. "; last round " .. tostring(detail))

		gl.DeleteTexture(fbo)
		gl.DeleteShader(shader)
		ctx.call("level", 100)
		ctx.call("globalLos", allyTeam, 0)
	end,
}
