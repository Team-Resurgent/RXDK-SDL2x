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

#if SDL_THREAD_XBOX

/* Win32 thread management routines for SDL */

#include "SDL_hints.h"
#include "SDL_thread.h"
#include "../SDL_thread_c.h"
#include "../SDL_systhread.h"
#include "SDL_systhread_c.h"

static DWORD WINAPI RunThread(LPVOID data)
{
	SDL_RunThread(data);
	return(0);
}

int SDL_SYS_CreateThread(SDL_Thread* thread, void* args)
{
	DWORD threadnum;

	thread->handle = CreateThread(NULL, 0, RunThread, args, 0, &threadnum);
	if (thread->handle == NULL) {
		SDL_SetError("Not enough resources to create thread");
		return(-1);
	}

	return 0;
}

void
SDL_SYS_SetupThread(const char* name)
{
	return;
}

SDL_threadID
SDL_ThreadID(void)
{
	return ((SDL_threadID)GetCurrentThreadId());
}

int
SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority)
{
	int value;

	if (priority == SDL_THREAD_PRIORITY_LOW) {
		value = THREAD_PRIORITY_LOWEST;
	}
	else if (priority == SDL_THREAD_PRIORITY_HIGH) {
		value = THREAD_PRIORITY_HIGHEST;
	}
	else if (priority == SDL_THREAD_PRIORITY_TIME_CRITICAL) {
		value = THREAD_PRIORITY_TIME_CRITICAL;
	}
	else {
		value = THREAD_PRIORITY_NORMAL;
	}
	if (!SetThreadPriority(GetCurrentThread(), value)) {
		return XBOX_SetError("SetThreadPriority()");
	}
	return 0;
}

void
SDL_SYS_WaitThread(SDL_Thread* thread)
{
	WaitForSingleObject(thread->handle, INFINITE);
	CloseHandle(thread->handle);
}

void
SDL_SYS_DetachThread(SDL_Thread* thread)
{
	CloseHandle(thread->handle);
}

#endif /* SDL_THREAD_XBOX */

/* vi: set ts=4 sw=4 expandtab: */