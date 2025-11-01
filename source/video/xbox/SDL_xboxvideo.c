/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2019 Sam Lantinga <slouken@libsdl.org>

  Updated for SDL 2.32.x-era video-driver struct shape
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_XBOX

#include "SDL_main.h"
#include "SDL_video.h"
#include "SDL_hints.h"
#include "SDL_mouse.h"
#include "SDL_system.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"

#include "SDL_xboxvideo.h"

/* ========================================================================== */
/* platform detection that works for XDK and nxdk                             */
#if defined(__XBOX__) || defined(_XBOX)
#define SDL_XBOX_PLATFORM 1
#else
#define SDL_XBOX_PLATFORM 0
#endif
/* ========================================================================== */

/* Initialization/Query functions */
static int  XBOX_VideoInit(_THIS);
static void XBOX_VideoQuit(_THIS);

/* NEW (2.0.16+ style) callbacks we must provide */
static void XBOX_SetWindowMouseGrab(_THIS, SDL_Window* window, SDL_bool grabbed);
static void XBOX_SetWindowKeyboardGrab(_THIS, SDL_Window* window, SDL_bool grabbed);
static void XBOX_SetWindowMouseRect(_THIS, SDL_Window* window);

/* Hints */
SDL_bool g_WindowsEnableMessageLoop = SDL_TRUE;
SDL_bool g_WindowFrameUsableWhileCursorHidden = SDL_TRUE;

static void SDLCALL
UpdateWindowsEnableMessageLoop(void* userdata, const char* name, const char* oldValue, const char* newValue)
{
    (void)userdata; (void)name; (void)oldValue;
    if (newValue && *newValue == '0') {
        g_WindowsEnableMessageLoop = SDL_FALSE;
    }
    else {
        g_WindowsEnableMessageLoop = SDL_TRUE;
    }
}

static void SDLCALL
UpdateWindowFrameUsableWhileCursorHidden(void* userdata, const char* name, const char* oldValue, const char* newValue)
{
    (void)userdata; (void)name; (void)oldValue;
    if (newValue && *newValue == '0') {
        g_WindowFrameUsableWhileCursorHidden = SDL_FALSE;
    }
    else {
        g_WindowFrameUsableWhileCursorHidden = SDL_TRUE;
    }
}

static void XBOX_SuspendScreenSaver(_THIS)
{
    /* no screensavers on Xbox */
    (void)_this;
}

/* -------------------------------------------------------------------------- */
/* Xbox driver bootstrap functions                                            */

static SDL_bool
XBOX_Available(void)
{
    /* old code returned int, but SDL expects SDL_bool now */
#if SDL_XBOX_PLATFORM
    return SDL_TRUE;
#else
    return SDL_FALSE;
#endif
}

static void
XBOX_DeleteDevice(SDL_VideoDevice* device)
{
    if (!device) {
        return;
    }

    SDL_VideoData* data = (SDL_VideoData*)device->driverdata;

#if !SDL_XBOX_PLATFORM
    SDL_UnregisterApp();

    if (data && data->userDLL) {
        SDL_UnloadObject(data->userDLL);
    }
    if (data && data->shcoreDLL) {
        SDL_UnloadObject(data->shcoreDLL);
    }
#endif

    if (data) {
        SDL_free(data);
    }

    SDL_free(device);
}

static SDL_VideoDevice*
XBOX_CreateDevice(int devindex)
{
    (void)devindex;

    SDL_VideoDevice* device;
    SDL_VideoData* data;

#if !SDL_XBOX_PLATFORM
    /* Windows host builds only */
    SDL_RegisterApp(NULL, 0, NULL);
#endif

    device = (SDL_VideoDevice*)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (device) {
        data = (SDL_VideoData*)SDL_calloc(1, sizeof(SDL_VideoData));
    }
    else {
        data = NULL;
    }
    if (!device || !data) {
        SDL_free(device);
        SDL_OutOfMemory();
        return NULL;
    }

    device->driverdata = data;

#if !SDL_XBOX_PLATFORM
    /* optional Win32 dynamic APIs */
    data->userDLL = SDL_LoadObject("USER32.DLL");
    if (data->userDLL) {
        data->CloseTouchInputHandle = (BOOL(WINAPI*)(HTOUCHINPUT)) SDL_LoadFunction(data->userDLL, "CloseTouchInputHandle");
        data->GetTouchInputInfo = (BOOL(WINAPI*)(HTOUCHINPUT, UINT, PTOUCHINPUT, int)) SDL_LoadFunction(data->userDLL, "GetTouchInputInfo");
        data->RegisterTouchWindow = (BOOL(WINAPI*)(HWND, ULONG)) SDL_LoadFunction(data->userDLL, "RegisterTouchWindow");
    }
    else {
        SDL_ClearError();
    }

    data->shcoreDLL = SDL_LoadObject("SHCORE.DLL");
    if (data->shcoreDLL) {
        data->GetDpiForMonitor = (HRESULT(WINAPI*)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*)) SDL_LoadFunction(data->shcoreDLL, "GetDpiForMonitor");
    }
    else {
        SDL_ClearError();
    }
#endif /* !SDL_XBOX_PLATFORM */

    /* --- core video entry points --- */
    device->VideoInit = XBOX_VideoInit;
    device->VideoQuit = XBOX_VideoQuit;
    device->GetDisplayBounds = NULL;
    device->GetDisplayUsableBounds = NULL;
    device->GetDisplayDPI = NULL;
    device->GetDisplayModes = XBOX_GetDisplayModes;
    device->SetDisplayMode = XBOX_SetDisplayMode;
    device->PumpEvents = XBOX_PumpEvents;
    device->SuspendScreenSaver = XBOX_SuspendScreenSaver;

    /* --- window management --- */
    device->CreateSDLWindow = XBOX_CreateWindow;
    device->CreateSDLWindowFrom = XBOX_CreateWindowFrom;
    device->SetWindowTitle = XBOX_SetWindowTitle;
    device->SetWindowIcon = XBOX_SetWindowIcon;
    device->SetWindowPosition = XBOX_SetWindowPosition;
    device->SetWindowSize = XBOX_SetWindowSize;
    device->GetWindowBordersSize = NULL;
    device->SetWindowMinimumSize = NULL;
    device->SetWindowMaximumSize = NULL;
    device->SetWindowOpacity = NULL;
    device->ShowWindow = XBOX_ShowWindow;
    device->HideWindow = XBOX_HideWindow;
    device->RaiseWindow = XBOX_RaiseWindow;
    device->MaximizeWindow = XBOX_MaximizeWindow;
    device->MinimizeWindow = XBOX_MinimizeWindow;
    device->RestoreWindow = XBOX_RestoreWindow;
    device->SetWindowBordered = NULL;
    device->SetWindowResizable = NULL;
    device->SetWindowFullscreen = NULL;
    device->SetWindowGammaRamp = NULL;
    device->GetWindowGammaRamp = NULL;
    /* OLD: device->SetWindowGrab = XBOX_SetWindowGrab; // was commented out */

    /* --- NEW in newer SDL2: must be present (even as stubs) --- */
    device->SetWindowMouseGrab = XBOX_SetWindowMouseGrab;
    device->SetWindowKeyboardGrab = XBOX_SetWindowKeyboardGrab;
    device->SetWindowMouseRect = XBOX_SetWindowMouseRect;

    device->DestroyWindow = XBOX_DestroyWindow;
    device->GetWindowWMInfo = NULL;

#if !SDL_XBOX_PLATFORM
    /* no GDI on Xbox; leave these NULL on real hardware */
    device->CreateWindowFramebuffer = NULL;
    device->UpdateWindowFramebuffer = NULL;
    device->DestroyWindowFramebuffer = NULL;
#endif

    device->OnWindowEnter = XBOX_OnWindowEnter;
    device->SetWindowHitTest = XBOX_SetWindowHitTest;
    device->AcceptDragAndDrop = XBOX_AcceptDragAndDrop;

    /* no shaped windows on Xbox */
    device->shape_driver.CreateShaper = NULL;
    device->shape_driver.SetWindowShape = NULL;
    device->shape_driver.ResizeWindowShape = NULL;

    /* no OpenGL */
    device->GL_LoadLibrary = NULL;
    device->GL_GetProcAddress = NULL;
    device->GL_UnloadLibrary = NULL;
    device->GL_CreateContext = NULL;
    device->GL_MakeCurrent = NULL;
    device->GL_SetSwapInterval = NULL;
    device->GL_GetSwapInterval = NULL;
    device->GL_SwapWindow = NULL;
    device->GL_DeleteContext = NULL;

    /* no Vulkan */
    device->Vulkan_LoadLibrary = NULL;
    device->Vulkan_UnloadLibrary = NULL;
    device->Vulkan_GetInstanceExtensions = NULL;
    device->Vulkan_CreateSurface = NULL;

    /* text / clipboard stubs */
    device->StartTextInput = NULL;
    device->StopTextInput = NULL;
    device->SetTextInputRect = NULL;

    device->SetClipboardText = NULL;
    device->GetClipboardText = NULL;
    device->HasClipboardText = NULL;

    /* on-screen keyboard stubs */
    device->HasScreenKeyboardSupport = XBOX_HasScreenKeyboardSupport;
    device->ShowScreenKeyboard = XBOX_ShowScreenKeyboard;
    device->HideScreenKeyboard = XBOX_HideScreenKeyboard;
    device->IsScreenKeyboardShown = XBOX_IsScreenKeyboardShown;

    device->free = XBOX_DeleteDevice;

    return device;
}

/* this must match the struct order for SDL 2.x */
VideoBootStrap XBOX_bootstrap = {
    "xbox", "SDL Xbox video driver", XBOX_CreateDevice
};

/* -------------------------------------------------------------------------- */
/* video init / quit                                                          */

int
XBOX_VideoInit(_THIS)
{
    SDL_VideoDisplay display;
    SDL_DisplayMode current_mode;
    SDL_Window* pWindow = NULL;
    SDL_zero(current_mode);

    XBOX_InitKeyboard(_this);
    XBOX_InitMouse(_this);

    pWindow = SDL_GetFocusWindow();

    if (pWindow) {
        current_mode.w = pWindow->w;
        current_mode.h = pWindow->h;
    }
    else {
#if SDL_XBOX_PLATFORM
        /* you can query XGetVideoFlags here if you like; keeping your old 640x480 default */
        current_mode.w = 640;
        current_mode.h = 480;
#else
        current_mode.w = 640;
        current_mode.h = 480;
#endif
    }
    current_mode.refresh_rate = 60;

    /* 32 bpp for default */
    current_mode.format = SDL_PIXELFORMAT_ABGR8888;
    current_mode.driverdata = NULL;

    SDL_zero(display);
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;
    display.driverdata = NULL;

    SDL_AddVideoDisplay(&display, SDL_FALSE);

    /* register hint callbacks so runtime changes take effect, like your older code wanted */
    SDL_AddHintCallback("SDL_WINDOWS_ENABLE_MESSAGELOOP", UpdateWindowsEnableMessageLoop, NULL);
    SDL_AddHintCallback("SDL_WINDOWS_FRAME_USABLE_WHILE_CURSOR_HIDDEN", UpdateWindowFrameUsableWhileCursorHidden, NULL);

    return 0;
}

void
XBOX_VideoQuit(_THIS)
{
    XBOX_QuitKeyboard(_this);
    XBOX_QuitMouse(_this);

    SDL_DelHintCallback("SDL_WINDOWS_ENABLE_MESSAGELOOP", UpdateWindowsEnableMessageLoop, NULL);
    SDL_DelHintCallback("SDL_WINDOWS_FRAME_USABLE_WHILE_CURSOR_HIDDEN", UpdateWindowFrameUsableWhileCursorHidden, NULL);
}

/* -------------------------------------------------------------------------- */
/* D3D / DXGI bits: left mostly as-is, just guard with SDL_XBOX_PLATFORM      */

#define D3D_DEBUG_INFO

SDL_bool
D3D_LoadDLL(/*void **pD3DDLL,*/ IDirect3D8** pDirect3D8Interface)
{
#if SDL_XBOX_PLATFORM
    /* OG Xbox: Direct3D8 comes from the XDK, no DLL */
    * pDirect3D8Interface = Direct3DCreate8(D3D_SDK_VERSION);
    return (*pDirect3D8Interface != NULL) ? SDL_TRUE : SDL_FALSE;
#else
    /* your old Windows-host path left here, but simplified would be better */
    /* ... keep your original Windows code if you actually use it ... */
    * pDirect3D8Interface = Direct3DCreate8(D3D_SDK_VERSION);
    return (*pDirect3D8Interface != NULL) ? SDL_TRUE : SDL_FALSE;
#endif
}

#if !SDL_XBOX_PLATFORM
/* your SDL_Direct3D9GetAdapterIndex(...) and SDL_DXGIGetOutputInfo(...) can stay the same,
   2.32.10 still has these symbols and patterns for Windows */
#endif

   /* -------------------------------------------------------------------------- */
   /* new SDL 2.32.x-required stubs                                              */

static void
XBOX_SetWindowMouseGrab(_THIS, SDL_Window* window, SDL_bool grabbed)
{
    /* single fullscreen Xbox ? nothing to do */
    (void)_this; (void)window; (void)grabbed;
}

static void
XBOX_SetWindowKeyboardGrab(_THIS, SDL_Window* window, SDL_bool grabbed)
{
    /* keyboard is always “grabbed” on Xbox */
    (void)_this; (void)window; (void)grabbed;
}

static void
XBOX_SetWindowMouseRect(_THIS, SDL_Window* window)
{
    /* we don't support confining mouse to rect on Xbox */
    (void)_this; (void)window;
}

/* -------------------------------------------------------------------------- */
/* on-screen keyboard stubs                                                   */

SDL_bool XBOX_HasScreenKeyboardSupport(_THIS)
{
    (void)_this;
    return SDL_FALSE;
}

void XBOX_ShowScreenKeyboard(_THIS, SDL_Window* window)
{
    (void)_this; (void)window;
}

void XBOX_HideScreenKeyboard(_THIS, SDL_Window* window)
{
    (void)_this; (void)window;
}

SDL_bool XBOX_IsScreenKeyboardShown(_THIS, SDL_Window* window)
{
    (void)_this; (void)window;
    return SDL_FALSE;
}

#endif /* SDL_VIDEO_DRIVER_XBOX */

/* vim: set ts=4 sw=4 expandtab: */
