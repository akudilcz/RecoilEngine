/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

// When a LOS-style info texture (LOS, air LOS, radar) must be re-rendered. Shared by
// those textures and unit-tested (test/engine/Rendering/testInfoTextureUpdate.cpp).
namespace InfoTextureUpdate {
	struct State {
		bool everUpdated;     // rendered at least once
		int idleSecs;         // seconds since the texture was last sampled (GetTexture)
		bool globalLos;       // viewed ally team currently has global LOS
		bool lastGlobalLos;   // ... as of the last render
		int allyTeam;         // viewed ally team
		int lastAllyTeam;     // ... as of the last render
		bool countersChanged; // the viewed team's LOS/radar/jammer maps changed since the last render
	};

	// Textures nobody sampled for a few seconds are left alone (an inactive info mode);
	// they catch up on the next regular update once sampled again. The change counters
	// are per ally team, so a different viewed team always needs a render.
	inline bool NeedsUpdate(const State& s)
	{
		if (!s.everUpdated)
			return true;
		if (s.idleSecs > 2)
			return false;
		if (s.globalLos != s.lastGlobalLos || s.allyTeam != s.lastAllyTeam)
			return true;
		if (s.globalLos)
			return false;
		return s.countersChanged;
	}
}
