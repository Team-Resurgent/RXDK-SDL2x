/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2019 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
	 claim that you wrote the original software. If you use this software
	 in a product, an acknowledgment in the product documentation would be
	 appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
	 misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_XBOX

#include "SDL_xboxvideo.h"
#include "SDL_xboxwindow.h"

//
// Nothing to do here for Xbox
// Just the stubs
//

void
XBOX_GetDisplayModes(_THIS, SDL_VideoDisplay* display)
{
}

int
XBOX_SetDisplayMode(_THIS, SDL_VideoDisplay* display, SDL_DisplayMode* mode)
{
	return 0;
}

int
XBOX_CreateWindow(_THIS, SDL_Window* window)
{
	// Not much required for Xbox here
	XBOX_PumpEvents(_this);

	// No window to setup on Xbox

	return 0;
}

int
XBOX_CreateWindowFrom(_THIS, SDL_Window* window, const void* data)
{
	return SDL_Unsupported();
}

void
XBOX_SetWindowTitle(_THIS, SDL_Window* window)
{
}

void
XBOX_SetWindowIcon(_THIS, SDL_Window* window, SDL_Surface* icon)
{
}

void
XBOX_SetWindowPosition(_THIS, SDL_Window* window)
{
}

void
XBOX_SetWindowSize(_THIS, SDL_Window* window)
{
}

void
XBOX_ShowWindow(_THIS, SDL_Window* window)
{
}

void
XBOX_HideWindow(_THIS, SDL_Window* window)
{
}

void
XBOX_RaiseWindow(_THIS, SDL_Window* window)
{
}

void
XBOX_MaximizeWindow(_THIS, SDL_Window* window)
{
}

void
XBOX_MinimizeWindow(_THIS, SDL_Window* window)
{
}

void
XBOX_RestoreWindow(_THIS, SDL_Window* window)
{
}

void
XBOX_SetWindowGrab(_THIS, SDL_Window* window, SDL_bool grabbed)
{
}

void
XBOX_DestroyWindow(_THIS, SDL_Window* window)
{
}

void XBOX_OnWindowEnter(_THIS, SDL_Window* window)
{
}

int
XBOX_SetWindowHitTest(SDL_Window* window, SDL_bool enabled)
{
	return 0;  /* just succeed, the real work is done elsewhere. */
}

void
XBOX_AcceptDragAndDrop(SDL_Window* window, SDL_bool accept)
{
}

#endif /* SDL_VIDEO_DRIVER_XBOX */

/* vi: set ts=4 sw=4 expandtab: */