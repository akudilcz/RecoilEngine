/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef MOUSE_INPUT_H
#define MOUSE_INPUT_H

#include <SDL_events.h>
#include "System/Input/InputHandler.h"

#include "System/type2.h"
#include "System/Misc/SpringTime.h"

#include <vector>

class IMouseInput
{
public:
	static IMouseInput* GetInstance(bool relModeWarp, int dragReleaseDebounceMs);
	static void FreeInstance(IMouseInput*);

	IMouseInput() = default;
	IMouseInput(bool relModeWarp, int dragReleaseDebounceMs);
	virtual ~IMouseInput();

	virtual void InstallWndCallback() {}

	int2 GetPos() const { return mousepos; }

	bool SetPos(int2 pos);
	bool WarpPos(int2 pos);
	bool SetWarpPos(int2 pos) { return (SetPos(pos) && WarpPos(pos)); }

	bool HandleSDLMouseEvent(const SDL_Event& event);
	/// delivers held-back drag releases whose debounce window has run out; called once per main-loop pass
	void DeliverExpiredReleases();

	virtual void SetWMMouseCursor(void* wmcursor) {}

protected:
	struct PendingRelease {
		bool active = false;
		int2 pos;
		spring_time deadline;
	};

	/// indexed by SDL button, same one-bottomed layout (NUM_BUTTONS + 1) as CMouseHandler::buttons
	std::vector<PendingRelease> pendingReleases;
	spring_time dragReleaseDebounce;

	int2 mousepos;
	InputHandler::HandlerTokenT inputCon;
};

extern IMouseInput* mouseInput;

#endif
