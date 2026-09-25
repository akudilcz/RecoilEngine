/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/*
	This workaround fixes the windows slow mouse movement problem
	(happens on full-screen mode + pressing keys).
	The code hacks around the mouse input from DirectInput,
	which SDL uses in full-screen mode.
	Instead it installs a window message proc and reads input from WM_MOUSEMOVE.
	On non-windows, the normal SDL events are used for mouse input

	new:
	It also workarounds a issue with SDL+windows and hardware cursors
	(->it has to block WM_SETCURSOR),
	so it is used now always even in window mode!

	newer:
	SDL_Event struct is used for new input handling.
	Several people confirmed its working.

	Drag release debounce (MouseDragReleaseDebounce, milliseconds, 0 = off):
	a worn or bouncing mouse switch briefly opens while the button is held,
	which SDL reports as a release followed tens of milliseconds later by a
	press. Mid-drag that ends the box select / build line / formation early
	and starts a new drag from the current position. Such a release is held
	back for the debounce window; a press of the same button inside the
	window cancels both, so the drag continues. If the window runs out, the
	release is delivered at the position and with the button state it had.
	Only releases that end a drag (movement past MouseDragSelectionThreshold)
	are held: clicks and double-clicks keep zero added latency, which matters
	because a human double-click can have the same ~30 ms up-to-down gap as a
	bounce. This is libinput's "button losing contact while being held down"
	case (src/libinput-plugin-button-debounce.c, case 4.1), gated on drag
	movement instead of on a learned per-device bounce pattern, since the same
	switches misbehave on Windows where no such filter exists.
*/


#include "MouseInput.h"
#include "InputHandler.h"

#include "Game/UI/MouseHandler.h"
#include "Rendering/GlobalRendering.h"
#include "System/MainDefines.h"
#include "System/SafeUtil.h"
#include "System/Log/ILog.h"

#include <SDL_events.h>
#include <SDL_hints.h>
#include <SDL_syswm.h>


IMouseInput* mouseInput = nullptr;

IMouseInput::IMouseInput(bool relModeWarp, int dragReleaseDebounceMs)
	: pendingReleases(NUM_BUTTONS + 1)
	, dragReleaseDebounce(spring_msecs(dragReleaseDebounceMs))
{
	inputCon = input.AddHandler([this](const SDL_Event& event) { return this->HandleSDLMouseEvent(event); });
	#ifndef HEADLESS
	// Windows 10 FCU (Fall Creators Update) causes spurious SDL_MOUSEMOTION
	// events to be generated with SDL_HINT_MOUSE_RELATIVE_MODE_WARP enabled
	//
	// while Spring did not previously set this hint and SDL defaults to raw
	// input, the update also affects MMB scrolling via SDL_WarpMouseInWindow
	// (our ancient manually implemented method of achieving relative motion)
	//
	// win32 SDL hides these events in 2.0.8 only if mouse->relative_mode_warp
	// (which configures relative mouse mode to *internally* use mouse warping
	// instead of raw input and gets toggled by SDL_SetRelativeMouseMode based
	// on the hint given here); the alternative to RMW would be to *duplicate*
	// the SDL patch in WarpPos
	SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, relModeWarp? "1": "0");
	#endif
}

IMouseInput::~IMouseInput()
{
	#ifndef HEADLESS
	SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
	#endif
}


bool IMouseInput::HandleSDLMouseEvent(const SDL_Event& event)
{
	switch (event.type) {
		case SDL_MOUSEMOTION: {
			mousepos = int2(event.motion.x, event.motion.y);

			if (mouse != nullptr)
				mouse->MouseMove(mousepos.x, mousepos.y, event.motion.xrel, event.motion.yrel);

		} break;
		case SDL_MOUSEBUTTONDOWN: {
			mousepos = int2(event.button.x, event.button.y);
			const int button = event.button.button;

			// switch bounce: the held-back release and this press never happened
			if (button <= NUM_BUTTONS && pendingReleases[button].active) {
				pendingReleases[button].active = false;
				LOG("[MouseInput] debounced release+press of mouse button %d during a drag", button);
				break;
			}

			// suppress if the button is already held via input emulation
			if (mouse != nullptr && !mouse->IsButtonEmulated(button))
				mouse->MousePress(mousepos.x, mousepos.y, button);

		} break;
		case SDL_MOUSEBUTTONUP: {
			mousepos = int2(event.button.x, event.button.y);
			const int button = event.button.button;

			if (mouse == nullptr || mouse->IsButtonEmulated(button))
				break;

			const bool endsDrag =
				button <= NUM_BUTTONS &&
				mouse->buttons[button].pressed &&
				mouse->buttons[button].movement > mouse->dragSelectionThreshold;

			if (endsDrag && dragReleaseDebounce > spring_notime) {
				pendingReleases[button] = {true, mousepos, spring_gettime() + dragReleaseDebounce};
				break;
			}

			mouse->MouseRelease(mousepos.x, mousepos.y, button);
		} break;
		case SDL_MOUSEWHEEL: {
			if (mouse != nullptr)
				mouse->MouseWheel(event.wheel.y);

		} break;
		case SDL_WINDOWEVENT: {
			switch (event.window.event) {
				case SDL_WINDOWEVENT_ENTER: {
					if (mouse != nullptr)
						mouse->WindowEnter();
				} break;
				case SDL_WINDOWEVENT_LEAVE: {
					// mouse left window; set pos internally to view center-pixel to prevent endless scrolling
					mousepos = {
						globalRendering->viewPosX          + (globalRendering->viewSizeX >> 1),
						globalRendering->viewWindowOffsetY + (globalRendering->viewSizeY >> 1)
					};

					if (mouse != nullptr)
						mouse->WindowLeave();
				} break;
			}
		} break;
	}

	return false;
}

void IMouseInput::DeliverExpiredReleases()
{
	const spring_time now = spring_gettime();

	for (int button = 1; button <= NUM_BUTTONS; ++button) {
		PendingRelease& pending = pendingReleases[button];

		if (!pending.active || now < pending.deadline)
			continue;

		pending.active = false;

		if (mouse != nullptr)
			mouse->MouseRelease(pending.pos.x, pending.pos.y, button);
	}
}

//////////////////////////////////////////////////////////////////////

#if defined(_WIN32) && !defined(HEADLESS)

class CWin32MouseInput : public IMouseInput
{
public:
	static CWin32MouseInput* inst;

	LONG_PTR sdl_wndproc;
	HWND wnd;
	HCURSOR hCursor;

	static LRESULT CALLBACK SpringWndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg) {
			case WM_SETCURSOR: {
				if (inst->hCursor != nullptr) {
					const Uint16 hittest = LOWORD(lParam);

					if (hittest == HTCLIENT) {
						SetCursor(inst->hCursor);
						return TRUE;
					}
				}
			} break;
		}
		return CallWindowProc((WNDPROC)inst->sdl_wndproc, wnd, msg, wParam, lParam);
	}

	void SetWMMouseCursor(void* wmcursor)
	{
		hCursor = (HCURSOR)wmcursor;
	}

	void InstallWndCallback()
	{
		SDL_SysWMinfo info;
		SDL_VERSION(&info.version);
		if (!SDL_GetWindowWMInfo(globalRendering->GetWindow(), &info))
			return;

		wnd = info.info.win.window;

		LONG_PTR cur_wndproc = GetWindowLongPtr(wnd, GWLP_WNDPROC);

		if (cur_wndproc != (LONG_PTR)SpringWndProc) {
			sdl_wndproc = GetWindowLongPtr(wnd, GWLP_WNDPROC);
			SetWindowLongPtr(wnd, GWLP_WNDPROC, (LONG_PTR)SpringWndProc);
		}
	}

	CWin32MouseInput(bool relModeWarp, int dragReleaseDebounceMs): IMouseInput(relModeWarp, dragReleaseDebounceMs)
	{
		inst = this;
		hCursor = nullptr;
		sdl_wndproc = 0;
		wnd = 0;

		InstallWndCallback();
	}
	~CWin32MouseInput()
	{
		// reinstall the SDL window proc
		SetWindowLongPtr(wnd, GWLP_WNDPROC, sdl_wndproc);
	}
};

CWin32MouseInput* CWin32MouseInput::inst = nullptr;


alignas(CWin32MouseInput) static std::byte mouseInputMem[sizeof(CWin32MouseInput)];
#else
alignas(IMouseInput) static std::byte mouseInputMem[sizeof(IMouseInput)];
#endif



#if 1
static SDL_Event events[100];
#endif

bool IMouseInput::SetPos(int2 pos)
{
	if (!globalRendering->active)
		return false;

	// calling SDL_WarpMouse at 300fps eats ~5% cpu usage, so only update when needed
	if (pos.x == mousepos.x && pos.y == mousepos.y)
		return false;

	return (mousepos = pos, true);
}

bool IMouseInput::WarpPos(int2 pos)
{
	#if __unix__
		/* Needed for SDL2+Wayland where warping isn't allowed otherwise, works fine with X11.
		 * One would think there should be a corresponding `SDL_ShowCursor(SDL_ENABLE);` below,
		 * but apparently this prevents this work-around from working (?!). */
		SDL_ShowCursor(SDL_DISABLE);
	#endif

	SDL_WarpMouseInWindow(globalRendering->GetWindow(), pos.x, pos.y);

	// SDL_WarpMouse generates SDL_MOUSEMOTION events
	// in `middle click scrolling` those SDL generated ones would point into
	// the opposite direction the user moved the mouse, and so events would
	// cancel each other -> camera wouldn't move at all or jitter
	// need to catch the SDL generated events and delete them from its queue
	//
	// NOTE [2018]:
	//   the above comment dates back to 2010, but also describes the recent
	//   Windows 10 FCU bug with relative mode warping which similarly relies
	//   on WMIW
	#if 1
	SDL_PumpEvents();
	SDL_PeepEvents(&events[0], sizeof(events) / sizeof(events[0]), SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION);
	#else
	// should be equivalent, but for some reason is not
	SDL_FlushEvent(SDL_MOUSEMOTION);
	#endif

	return true;
}



IMouseInput* IMouseInput::GetInstance(bool relModeWarp, int dragReleaseDebounceMs)
{
	if (mouseInput == nullptr) {
#if defined(_WIN32) && !defined(HEADLESS)
		mouseInput = new (mouseInputMem) CWin32MouseInput(relModeWarp, dragReleaseDebounceMs);
#else
		mouseInput = new (mouseInputMem) IMouseInput(relModeWarp, dragReleaseDebounceMs);
#endif
	}

	return mouseInput;
}

void IMouseInput::FreeInstance(IMouseInput* mouseInp) {
	assert(mouseInp == mouseInput);
	spring::SafeDestruct(mouseInp);
	memset(mouseInputMem, 0, sizeof(mouseInputMem));
	mouseInput = nullptr;
}

