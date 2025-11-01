#include "../../SDL_internal.h"

#if SDL_THREAD_XBOX

#include "SDL_hints.h"
#include "SDL_thread.h"
#include "../SDL_thread_c.h"
#include "../SDL_systhread.h"
#include "SDL_systhread_c.h"

static DWORD WINAPI RunThread(LPVOID data)
{
    SDL_Thread* thread = (SDL_Thread*)data;   /*  this is the SDL thread object */
    SDL_RunThread(thread);                     /* runs user func with user data */
    return 0;
}

int SDL_SYS_CreateThread(SDL_Thread* thread, void* args)
{
    DWORD threadnum;

    /* store user args in the SDL_Thread object so SDL_RunThread can see it */
    thread->data = args;

    /* ?? pass the SDL_Thread*, NOT args */
    thread->handle = CreateThread(NULL,
        0,
        RunThread,
        thread,          /* <- this is the important change */
        0,
        &threadnum);

    if (thread->handle == NULL) {
        SDL_SetError("Not enough resources to create thread");
        return -1;
    }

    thread->threadid = (SDL_threadID)threadnum;
    return 0;
}

void SDL_SYS_SetupThread(const char* name)
{
    (void)name;
}

SDL_threadID SDL_ThreadID(void)
{
    return (SDL_threadID)GetCurrentThreadId();
}

int SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority)
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
        /* you used XBOX_SetError() but didn’t show it; fall back to SDL_SetError */
        SDL_SetError("SetThreadPriority() failed");
        return -1;
    }
    return 0;
}

void SDL_SYS_WaitThread(SDL_Thread* thread)
{
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
}

void SDL_SYS_DetachThread(SDL_Thread* thread)
{
    CloseHandle(thread->handle);
}

#endif /* SDL_THREAD_XBOX */
