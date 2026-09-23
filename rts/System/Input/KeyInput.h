/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef KEYBOARD_INPUT_H
#define KEYBOARD_INPUT_H

#include <vector>
#include <set>

namespace KeyInput {
	// eventKeyMods: modifier state carried by the SDL key event being handled (keysym.mod);
	// -1 samples the live SDL state instead
	void Update(int fakeMetaKey, int eventKeyMods = -1);
	void ReleaseAllKeys();

	bool IsKeyPressed(int idx);
	bool IsScanPressed(int idx);
	void SetKeyModState(int mod, bool pressed);
	bool GetKeyModState(int mod);

	// input emulation (debug.emulateKey*): keys forced down independent of hardware
	bool IsKeyEmulated(int keyCode);
	void SetKeyEmulated(int keyCode, bool pressed);
	const std::set<int>& GetEmulatedKeys();
	void ClearEmulatedKeys();

	typedef std::pair<int, bool> Key;

	const std::vector<Key>& GetPressedKeys();
	const std::vector<Key>& GetPressedScans();
}

#endif
