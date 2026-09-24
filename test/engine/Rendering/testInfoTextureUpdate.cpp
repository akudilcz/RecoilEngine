/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "Rendering/Map/InfoTexture/Modern/InfoTextureUpdate.h"

#include <catch_amalgamated.hpp>

using InfoTextureUpdate::NeedsUpdate;
using InfoTextureUpdate::State;

// a texture rendered for ally team 0 without global LOS, sampled just now, nothing changed
static State Settled() { return State{true, 0, false, false, 0, 0, false}; }

TEST_CASE("InfoTextureUpdate: first use always renders")
{
	State s = Settled();
	s.everUpdated = false;
	s.idleSecs = 100;
	CHECK(NeedsUpdate(s));
}

TEST_CASE("InfoTextureUpdate: unchanged state skips, a map change renders")
{
	State s = Settled();
	CHECK_FALSE(NeedsUpdate(s));
	s.countersChanged = true;
	CHECK(NeedsUpdate(s));
}

TEST_CASE("InfoTextureUpdate: switching the viewed ally team renders even when counters match")
{
	// the change counters are per ally team; equal values across teams mean nothing
	State s = Settled();
	s.allyTeam = 1;
	CHECK(NeedsUpdate(s));

	s.globalLos = s.lastGlobalLos = true; // also with global LOS on both sides
	CHECK(NeedsUpdate(s));
}

TEST_CASE("InfoTextureUpdate: global LOS")
{
	State s = Settled();
	s.globalLos = true;          // toggled on
	CHECK(NeedsUpdate(s));
	s.lastGlobalLos = true;      // steady: the full-view texture needs no refresh
	s.countersChanged = true;
	CHECK_FALSE(NeedsUpdate(s));
}

TEST_CASE("InfoTextureUpdate: textures nobody samples are left alone")
{
	State s = Settled();
	s.idleSecs = 3;
	s.countersChanged = true;
	s.allyTeam = 2;
	CHECK_FALSE(NeedsUpdate(s));
	s.idleSecs = 2;
	CHECK(NeedsUpdate(s));
}
