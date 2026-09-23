/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "System/Input/KeyInput.h"

#include <SDL_keyboard.h>

#include <catch_amalgamated.hpp>

// At low frame rates a whole shift-drag (press shift, drag, release mouse, release
// shift) can arrive in one SDL event batch. SDL_GetModState() then already reflects
// the final release while the earlier mouse events are handled, which broke
// drag-building. KeyInput::Update must use the modifier snapshot carried by the event.
// No window or real keyboard exists here, so the live SDL state has no modifiers:
// any modifier seen below can only come from the event snapshot. (Only the modifier
// state is checked: without SDL video init the keymap is empty, so the per-key
// vector has no SDLK_LSHIFT entry to mirror it.)

TEST_CASE("KeyInput uses the event's modifier snapshot over the live SDL state")
{
	KeyInput::Update(0, KMOD_LSHIFT);
	CHECK(KeyInput::GetKeyModState(KMOD_SHIFT));
	CHECK_FALSE(KeyInput::GetKeyModState(KMOD_CTRL));

	KeyInput::Update(0, KMOD_LCTRL | KMOD_LALT);
	CHECK(KeyInput::GetKeyModState(KMOD_CTRL));
	CHECK(KeyInput::GetKeyModState(KMOD_ALT));
	CHECK_FALSE(KeyInput::GetKeyModState(KMOD_SHIFT));
}

TEST_CASE("KeyInput samples the live SDL state without an event snapshot")
{
	KeyInput::Update(0, KMOD_LSHIFT);
	REQUIRE(KeyInput::GetKeyModState(KMOD_SHIFT));

	// -1: no event snapshot; the (empty) live state wins
	KeyInput::Update(0, -1);
	CHECK_FALSE(KeyInput::GetKeyModState(KMOD_SHIFT));
}
