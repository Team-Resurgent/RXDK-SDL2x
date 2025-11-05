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

#ifdef SDL_VIDEO_DRIVER_XBOX

#include "SDL_main.h"
#include "SDL_video.h"
#include "SDL_hints.h"
#include "SDL_mouse.h"
#include "SDL_system.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"

#include "SDL_xboxvideo.h"

/* Initialization/Query functions */
static int XBOX_VideoInit(_THIS);
static void XBOX_VideoQuit(_THIS);

/* Hints */
SDL_bool g_WindowsEnableMessageLoop = SDL_TRUE;
SDL_bool g_WindowFrameUsableWhileCursorHidden = SDL_TRUE;

/* Cache the desktop/current mode we register at init so other code
   (renderer, SetDisplayMode, etc.) can reference it if needed. */
SDL_DisplayMode g_XboxDesktopMode;

/* Small helper: parse common boolean strings, fall back to default. */
static SDL_bool
ParseBoolHint(const char* v, SDL_bool defaultval)
{
    if (!v) return defaultval;

    /* Fast path for single-char 0/1 */
    if (v[1] == '\0') {
        if (v[0] == '0') return SDL_FALSE;
        if (v[0] == '1') return SDL_TRUE;
        return defaultval;
    }

    /* Case-insensitive checks */
    if (!SDL_strcasecmp(v, "true") || !SDL_strcasecmp(v, "yes") || !SDL_strcasecmp(v, "on"))  return SDL_TRUE;
    if (!SDL_strcasecmp(v, "false") || !SDL_strcasecmp(v, "no") || !SDL_strcasecmp(v, "off")) return SDL_FALSE;

    return defaultval;
}

static void SDLCALL
UpdateWindowsEnableMessageLoop(void* userdata, const char* name, const char* oldValue, const char* newValue)
{
    (void)userdata; (void)name; (void)oldValue; /* silence unused warnings */
    /* Default TRUE: Xbox generally wants its main loop running. */
    g_WindowsEnableMessageLoop = ParseBoolHint(newValue, SDL_TRUE);
}

static void SDLCALL
UpdateWindowFrameUsableWhileCursorHidden(void* userdata, const char* name, const char* oldValue, const char* newValue)
{
    (void)userdata; (void)name; (void)oldValue;
    /* Default TRUE matches your current behavior. */
    g_WindowFrameUsableWhileCursorHidden = ParseBoolHint(newValue, SDL_TRUE);
}


static void XBOX_SuspendScreenSaver(_THIS)
{
    // no screensavers on Xbox
}

/* Windows driver bootstrap functions */

static SDL_bool
XBOX_Available(void)
{
#ifdef __XBOX__
    return SDL_TRUE;
#else
    return SDL_FALSE;
#endif
}

static void
XBOX_DeleteDevice(SDL_VideoDevice* device)
{
    if (!device) return;

    SDL_VideoData* data = (SDL_VideoData*)device->driverdata;

    /* Free per-device driver data, then the device itself */
    if (data) {
        SDL_free(data);
        device->driverdata = NULL;
    }

    SDL_free(device);
}


static SDL_VideoDevice* XBOX_CreateDevice(void)
{
    SDL_VideoDevice* device = NULL;
    SDL_VideoData* data = NULL;

    /* Allocate the SDL_VideoDevice */
    device = (SDL_VideoDevice*)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }

    /* Allocate per-driver data */
    data = (SDL_VideoData*)SDL_calloc(1, sizeof(SDL_VideoData));
    if (!data) {
        SDL_free(device);
        SDL_OutOfMemory();
        return NULL;
    }

    /* Link driverdata only after successful allocations above */
    device->driverdata = data;

    /* Register hint callbacks so runtime changes take effect */
    SDL_AddHintCallback("SDL_WINDOWS_ENABLE_MESSAGELOOP", UpdateWindowsEnableMessageLoop, NULL);
    SDL_AddHintCallback("SDL_WINDOWS_FRAME_USABLE_WHILE_CURSOR_HIDDEN", UpdateWindowFrameUsableWhileCursorHidden, NULL);

    /* Core video entry points */
    device->VideoInit = XBOX_VideoInit;
    device->VideoQuit = XBOX_VideoQuit;

    /* Display queries (supply Xbox versions if you have them; else NULL is fine) */
    device->GetDisplayBounds = NULL;
    device->GetDisplayUsableBounds = NULL;
    device->GetDisplayDPI = NULL;
    device->GetDisplayModes = XBOX_GetDisplayModes;
    device->SetDisplayMode = XBOX_SetDisplayMode;
    device->PumpEvents = XBOX_PumpEvents;
    device->SuspendScreenSaver = XBOX_SuspendScreenSaver;

    /* Window management */
    device->CreateSDLWindow = XBOX_CreateWindow;
    device->CreateSDLWindowFrom = XBOX_CreateWindowFrom;  /* set to NULL if unsupported */
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
    device->SetWindowFullscreen = NULL; /* set to NULL if not implemented */
    device->SetWindowGammaRamp = NULL;
    device->GetWindowGammaRamp = NULL;
    // TODO FIX THIS
    // device->SetWindowGrab = XBOX_SetWindowGrab;
    device->DestroyWindow = XBOX_DestroyWindow;
    device->GetWindowWMInfo = NULL;

#ifndef _XBOX
    /* No GDI framebuffer path on Xbox; leave these NULL there */
    device->CreateWindowFramebuffer = NULL;
    device->UpdateWindowFramebuffer = NULL;
    device->DestroyWindowFramebuffer = NULL;
#endif

    device->OnWindowEnter = XBOX_OnWindowEnter;
    device->SetWindowHitTest = XBOX_SetWindowHitTest;
    device->AcceptDragAndDrop = XBOX_AcceptDragAndDrop;

    /* No shaped windows on Xbox */
    device->shape_driver.CreateShaper = NULL;
    device->shape_driver.SetWindowShape = NULL;
    device->shape_driver.ResizeWindowShape = NULL;

    /* No OpenGL on Xbox */
    device->GL_LoadLibrary = NULL;
    device->GL_GetProcAddress = NULL;
    device->GL_UnloadLibrary = NULL;
    device->GL_CreateContext = NULL;
    device->GL_MakeCurrent = NULL;
    device->GL_SetSwapInterval = NULL;
    device->GL_GetSwapInterval = NULL;
    device->GL_SwapWindow = NULL;
    device->GL_DeleteContext = NULL;

    /* No Vulkan on Xbox */
    device->Vulkan_LoadLibrary = NULL;
    device->Vulkan_UnloadLibrary = NULL;
    device->Vulkan_GetInstanceExtensions = NULL;
    device->Vulkan_CreateSurface = NULL;

    /* Text input / clipboard stubs (fill if your port implements them) */
    device->StartTextInput = NULL;
    device->StopTextInput = NULL;
    device->SetTextInputRect = NULL;

    device->SetClipboardText = NULL;
    device->GetClipboardText = NULL;
    device->HasClipboardText = NULL;

    /* On-screen keyboard support on Xbox */
    device->HasScreenKeyboardSupport = XBOX_HasScreenKeyboardSupport;
    device->ShowScreenKeyboard = XBOX_ShowScreenKeyboard;
    device->HideScreenKeyboard = XBOX_HideScreenKeyboard;
    device->IsScreenKeyboardShown = XBOX_IsScreenKeyboardShown;

    /* Destructor */
    device->free = XBOX_DeleteDevice;

    return device;
}

VideoBootStrap XBOX_bootstrap = {
    "Xbox",
    "SDL Xbox video driver",
    XBOX_CreateDevice,
    NULL /* no ShowMessageBox implementation */
};

int
XBOX_VideoInit(_THIS)
{
    SDL_VideoDisplay display;
    SDL_DisplayMode  current_mode;
    SDL_Window* pWindow = NULL;

    SDL_zero(current_mode);
    SDL_zero(display);

    /* Input devices */
    XBOX_InitKeyboard(_this);
    XBOX_InitMouse(_this);

    /* If a window already exists, use its size first. */
    pWindow = SDL_GetFocusWindow();
    if (pWindow) {
        current_mode.w = pWindow->w;
        current_mode.h = pWindow->h;
        /* Safe default; renderer vsync choice will govern swap interval. */
        current_mode.refresh_rate = 60;
    }
    else {
        /* Pick best available mode based on Xbox dashboard/video flags. */
        DWORD vflags = XGetVideoFlags();
        BOOL  is_pal = (XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I);

        /* Highest -> lowest preference:
           1080i -> 720p -> 480p -> PAL 576 (50/60) -> NTSC 480i */
        if (vflags & XC_VIDEO_FLAGS_HDTV_1080i) {
            /* 1080i (interlaced) */
            current_mode.w = 1920;
            current_mode.h = 1080;
            current_mode.refresh_rate = 60;
        }
        else if (vflags & XC_VIDEO_FLAGS_HDTV_720p) {
            /* 720p progressive */
            current_mode.w = 1280;
            current_mode.h = 720;
            current_mode.refresh_rate = 60;
        }
        else if (vflags & XC_VIDEO_FLAGS_HDTV_480p) {
            /* 480p progressive (NTSC) */
            current_mode.w = 720;
            current_mode.h = 480;
            current_mode.refresh_rate = 60;
        }
        else if (is_pal) {
            /* PAL SD: 576-line (50 Hz normally, 60 if PAL60 enabled) */
            current_mode.w = 720;
            current_mode.h = 576;
            current_mode.refresh_rate = (vflags & XC_VIDEO_FLAGS_PAL_60Hz) ? 60 : 50;
        }
        else {
            /* NTSC SD 480i fallback */
            current_mode.w = 640;
            current_mode.h = 480;
            current_mode.refresh_rate = 60;
        }
    }

    /* 32 bpp default; keep consistent with renderer (ARGB8888) */
    current_mode.format = SDL_PIXELFORMAT_ARGB8888;
    current_mode.driverdata = NULL;

    /* Build the single Xbox display */
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;
    display.driverdata = NULL;

    /* Register display and cache for other subsystems */
    SDL_AddVideoDisplay(&display, SDL_FALSE);
    g_XboxDesktopMode = current_mode;

    SDL_Log("Xbox maximum available display mode: %dx%d@%d (%s)",
        current_mode.w, current_mode.h, current_mode.refresh_rate,
        (current_mode.h >= 1080) ? "1080i" :
        (current_mode.h >= 720) ? "720p" :
        (current_mode.h == 576) ? "576i" : "480");

    return 0;
}


void
XBOX_VideoQuit(_THIS)
{
    /* Tear down input first (current behavior) */
    XBOX_QuitKeyboard(_this);
    XBOX_QuitMouse(_this);

    /* Remove hint callbacks and restore defaults */
    SDL_DelHintCallback("SDL_WINDOWS_ENABLE_MESSAGELOOP",
        UpdateWindowsEnableMessageLoop, NULL);
    SDL_DelHintCallback("SDL_WINDOWS_FRAME_USABLE_WHILE_CURSOR_HIDDEN",
        UpdateWindowFrameUsableWhileCursorHidden, NULL);

    g_WindowsEnableMessageLoop = SDL_TRUE;
    g_WindowFrameUsableWhileCursorHidden = SDL_TRUE;

    /* Nothing else to free here: displays are cleaned up by SDL core,
       D3D teardown happens in the renderer�s DestroyRenderer path,
       and DLL unloads happen in XBOX_DeleteDevice. */
}


#define D3D_DEBUG_INFO

SDL_bool
D3D_LoadDLL(IDirect3D8** pDirect3D8Interface)
{
    if (!pDirect3D8Interface) {
        return SDL_FALSE;
    }

    /* On OG Xbox, Direct3D8 is provided by the XDK. No DLL to load. */
    * pDirect3D8Interface = Direct3DCreate8(D3D_SDK_VERSION);
    return (*pDirect3D8Interface != NULL) ? SDL_TRUE : SDL_FALSE;
}


SDL_bool
SDL_DXGIGetOutputInfo(int displayIndex, int* adapterIndex, int* outputIndex)
{
    /* Basic arg validation for all builds */
    if (!adapterIndex) { SDL_InvalidParamError("adapterIndex"); return SDL_FALSE; }
    if (!outputIndex) { SDL_InvalidParamError("outputIndex");  return SDL_FALSE; }
    *adapterIndex = -1;
    *outputIndex = -1;

#if defined(__XBOX__) || !HAVE_DXGI_H
    /* OG Xbox (no DXGI) or builds without dxgi.h: cleanly report unsupported */
    (void)displayIndex;  /* unused */
# if !HAVE_DXGI_H && !defined(__XBOX__)
    SDL_SetError("SDL was compiled without DXGI support (missing dxgi.h)");
# else
    /* On Xbox we usually avoid spamming errors for expected-unsupported paths. */
    SDL_SetError("DXGI is not available on this platform");
# endif
    return SDL_FALSE;

#else  /* DXGI path for desktop Windows builds */

    SDL_DisplayData* pData = (SDL_DisplayData*)SDL_GetDisplayDriverData(displayIndex);
    void* pDXGIDLL = NULL;
    IDXGIFactory* pDXGIFactory = NULL;
    char* displayName = NULL;
    int              nAdapter = 0, nOutput = 0;
    IDXGIAdapter* pDXGIAdapter = NULL;
    IDXGIOutput* pDXGIOutput = NULL;

    if (!pData) {
        SDL_SetError("Invalid display index");
        return SDL_FALSE;
    }

    if (!DXGI_LoadDLL(&pDXGIDLL, &pDXGIFactory)) {
        SDL_SetError("Unable to create DXGI interface");
        return SDL_FALSE;
    }

    displayName = XBOX_StringToUTF8(pData->DeviceName);
    while (*adapterIndex == -1 && SUCCEEDED(IDXGIFactory_EnumAdapters(pDXGIFactory, nAdapter, &pDXGIAdapter))) {
        nOutput = 0;
        while (*adapterIndex == -1 && SUCCEEDED(IDXGIAdapter_EnumOutputs(pDXGIAdapter, nOutput, &pDXGIOutput))) {
            DXGI_OUTPUT_DESC outputDesc;
            if (SUCCEEDED(IDXGIOutput_GetDesc(pDXGIOutput, &outputDesc))) {
                char* outputName = XBOX_StringToUTF8(outputDesc.DeviceName);
                if (SDL_strcmp(outputName, displayName) == 0) {
                    *adapterIndex = nAdapter;
                    *outputIndex = nOutput;
                }
                SDL_free(outputName);
            }
            IDXGIOutput_Release(pDXGIOutput);
            nOutput++;
        }
        IDXGIAdapter_Release(pDXGIAdapter);
        nAdapter++;
    }
    SDL_free(displayName);

    /* Release DXGI factory & DLL */
    IDXGIFactory_Release(pDXGIFactory);
# ifndef _XBOX
    SDL_UnloadObject(pDXGIDLL);
# endif

    return (*adapterIndex != -1) ? SDL_TRUE : SDL_FALSE;

#endif /* DXGI path */
}


//
// TODO: No on screen keyboard support for Xbox atm
//

SDL_bool XBOX_HasScreenKeyboardSupport(_THIS)
{
    return SDL_FALSE;
}

void XBOX_ShowScreenKeyboard(_THIS, SDL_Window* window)
{
}

void XBOX_HideScreenKeyboard(_THIS, SDL_Window* window)
{
}

SDL_bool XBOX_IsScreenKeyboardShown(_THIS, SDL_Window* window)
{
    return SDL_FALSE;
}

#endif /* SDL_VIDEO_DRIVER_XBOX */

/* vim: set ts=4 sw=4 expandtab: */
