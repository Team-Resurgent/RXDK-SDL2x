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

static void SDLCALL
UpdateWindowsEnableMessageLoop(void* userdata, const char* name, const char* oldValue, const char* newValue)
{
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
	if (newValue && *newValue == '0') {
		g_WindowFrameUsableWhileCursorHidden = SDL_FALSE;
	}
	else {
		g_WindowFrameUsableWhileCursorHidden = SDL_TRUE;
	}
}

static void XBOX_SuspendScreenSaver(_THIS)
{
	// no screensavers on Xbox
}

/* Windows driver bootstrap functions */

static int
XBOX_Available(void)
{
	return (1);
}

static void
XBOX_DeleteDevice(SDL_VideoDevice* device)
{
	SDL_VideoData* data = (SDL_VideoData*)device->driverdata;

#ifndef _XBOX
	SDL_UnregisterApp();

	if (data->userDLL) {
		SDL_UnloadObject(data->userDLL);
	}
	if (data->shcoreDLL) {
		SDL_UnloadObject(data->shcoreDLL);
	}
#endif

	if (data)
	{
		SDL_free(device->driverdata);
		SDL_free(device);
	}
}

static SDL_VideoDevice*
XBOX_CreateDevice(int devindex)
{
	SDL_VideoDevice* device;
	SDL_VideoData* data;

#ifndef _XBOX
	SDL_RegisterApp(NULL, 0, NULL);
#endif

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice*)SDL_calloc(1, sizeof(SDL_VideoDevice));
	if (device) {
		data = (struct SDL_VideoData*)SDL_calloc(1, sizeof(SDL_VideoData));
	}
	else {
		data = NULL;
	}
	if (!data) {
		SDL_free(device);
		SDL_OutOfMemory();
		return NULL;
	}
	device->driverdata = data;

#ifndef _XBOX
	data->userDLL = SDL_LoadObject("USER32.DLL");
	if (data->userDLL) {
		data->CloseTouchInputHandle = (BOOL(WINAPI*)(HTOUCHINPUT)) SDL_LoadFunction(data->userDLL, "CloseTouchInputHandle");
		data->GetTouchInputInfo = (BOOL(WINAPI*)(HTOUCHINPUT, UINT, PTOUCHINPUT, int)) SDL_LoadFunction(data->userDLL, "GetTouchInputInfo");
		data->RegisterTouchWindow = (BOOL(WINAPI*)(HWND, ULONG)) SDL_LoadFunction(data->userDLL, "RegisterTouchWindow");
	}
	else {
		SDL_ClearError();
	}
#endif

#ifndef _XBOX
	data->shcoreDLL = SDL_LoadObject("SHCORE.DLL");
	if (data->shcoreDLL) {
		data->GetDpiForMonitor = (HRESULT(WINAPI*)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*)) SDL_LoadFunction(data->shcoreDLL, "GetDpiForMonitor");
	}
	else {
		SDL_ClearError();
	}
#endif

	/* Set the function pointers */
	device->VideoInit = XBOX_VideoInit;
	device->VideoQuit = XBOX_VideoQuit;
	device->GetDisplayBounds = NULL;
	device->GetDisplayUsableBounds = NULL;
	device->GetDisplayDPI = NULL;
	device->GetDisplayModes = XBOX_GetDisplayModes;
	device->SetDisplayMode = XBOX_SetDisplayMode;
	device->PumpEvents = XBOX_PumpEvents;
	device->SuspendScreenSaver = XBOX_SuspendScreenSaver;

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
	device->SetWindowGrab = XBOX_SetWindowGrab;
	device->DestroyWindow = XBOX_DestroyWindow;
	device->GetWindowWMInfo = NULL;

#ifndef _XBOX // We don't have GDI so use D3D8 software emulation (set in SDL_VideoInit)
	device->CreateWindowFramebuffer = NULL;
	device->UpdateWindowFramebuffer = NULL;
	device->DestroyWindowFramebuffer = NULL;
#endif
	device->OnWindowEnter = XBOX_OnWindowEnter;
	device->SetWindowHitTest = XBOX_SetWindowHitTest;
	device->AcceptDragAndDrop = XBOX_AcceptDragAndDrop;

	// No window shapes on Xbox
	device->shape_driver.CreateShaper = NULL;
	device->shape_driver.SetWindowShape = NULL;
	device->shape_driver.ResizeWindowShape = NULL;

	// We dont have OpenGL on Xbox
	device->GL_LoadLibrary = NULL;
	device->GL_GetProcAddress = NULL;
	device->GL_UnloadLibrary = NULL;
	device->GL_CreateContext = NULL;
	device->GL_MakeCurrent = NULL;
	device->GL_SetSwapInterval = NULL;
	device->GL_GetSwapInterval = NULL;
	device->GL_SwapWindow = NULL;
	device->GL_DeleteContext = NULL;

	// No Vulkan on Xbox either
	device->Vulkan_LoadLibrary = NULL;
	device->Vulkan_UnloadLibrary = NULL;
	device->Vulkan_GetInstanceExtensions = NULL;
	device->Vulkan_CreateSurface = NULL;

	//TODO: Needed for DiabloX port
	device->StartTextInput = NULL;
	device->StopTextInput = NULL;
	device->SetTextInputRect = NULL;

	device->SetClipboardText = NULL;
	device->GetClipboardText = NULL;
	device->HasClipboardText = NULL;

	device->HasScreenKeyboardSupport = XBOX_HasScreenKeyboardSupport;
	device->ShowScreenKeyboard = XBOX_ShowScreenKeyboard;
	device->HideScreenKeyboard = XBOX_HideScreenKeyboard;
	device->IsScreenKeyboardShown = XBOX_IsScreenKeyboardShown;

	device->free = XBOX_DeleteDevice;

	return device;
}

VideoBootStrap XBOX_bootstrap = {
	"Xbox", "SDL Xbox video driver", XBOX_Available, XBOX_CreateDevice
};

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

	if (pWindow)
	{
		current_mode.w = pWindow->w;
		current_mode.h = pWindow->h;
	}
	else
	{
		current_mode.w = 640;
		current_mode.h = 480;
	}
	current_mode.refresh_rate = 60;

	/* 32 bpp for default */
	current_mode.format = SDL_PIXELFORMAT_ABGR8888;
	current_mode.driverdata = NULL;

	SDL_zero(display);
	display.desktop_mode = current_mode;
	display.current_mode = current_mode;
	display.driverdata = NULL;

	SDL_AddVideoDisplay(&display);

	return 0;
}

void
XBOX_VideoQuit(_THIS)
{
	XBOX_QuitKeyboard(_this);
	XBOX_QuitMouse(_this);
}

#define D3D_DEBUG_INFO

SDL_bool
D3D_LoadDLL(/*void **pD3DDLL,*/ IDirect3D8** pDirect3D8Interface)
{
#ifndef _XBOX
	* pD3DDLL = SDL_LoadObject("D3D9.DLL");
	if (*pD3DDLL) {
		typedef IDirect3D8* (WINAPI* Direct3DCreate9_t) (UINT SDKVersion);
		Direct3DCreate9_t Direct3DCreate9Func;
#endif

#ifdef USE_D3D9EX
		typedef HRESULT(WINAPI* Direct3DCreate9Ex_t)(UINT SDKVersion, IDirect3D9Ex** ppD3D);
		Direct3DCreate9Ex_t Direct3DCreate9ExFunc;

		Direct3DCreate9ExFunc = (Direct3DCreate9Ex_t)SDL_LoadFunction(*pD3DDLL, "Direct3DCreate9Ex");
		if (Direct3DCreate9ExFunc) {
			IDirect3D9Ex* pDirect3D9ExInterface;
			HRESULT hr = Direct3DCreate9ExFunc(D3D_SDK_VERSION, &pDirect3D9ExInterface);
			if (SUCCEEDED(hr)) {
				const GUID IDirect3D9_GUID = { 0x81bdcbca, 0x64d4, 0x426d, { 0xae, 0x8d, 0xad, 0x1, 0x47, 0xf4, 0x27, 0x5c } };
				hr = IDirect3D9Ex_QueryInterface(pDirect3D9ExInterface, &IDirect3D9_GUID, (void**)pDirect3D9Interface);
				IDirect3D9Ex_Release(pDirect3D9ExInterface);
				if (SUCCEEDED(hr)) {
					return SDL_TRUE;
				}
			}
		}
#endif /* USE_D3D9EX */

#ifndef _XBOX
		Direct3DCreate9Func = (Direct3DCreate9_t)SDL_LoadFunction(*pD3DDLL, "Direct3DCreate9");
		if (Direct3DCreate9Func) {
#endif
			* pDirect3D8Interface = Direct3DCreate8(D3D_SDK_VERSION);
			if (*pDirect3D8Interface) {
				return SDL_TRUE;
			}
#ifndef _XBOX
		}
		SDL_UnloadObject(*pD3DDLL);
		*pD3DDLL = NULL;
	}
#endif
	* pDirect3D8Interface = NULL;
	return SDL_FALSE;
}

#ifndef _XBOX
int
SDL_Direct3D9GetAdapterIndex(int displayIndex)
{
	void* pD3DDLL;
	IDirect3D9* pD3D;
	if (!D3D_LoadDLL(&pD3DDLL, &pD3D)) {
		SDL_SetError("Unable to create Direct3D interface");
		return D3DADAPTER_DEFAULT;
	}
	else {
		SDL_DisplayData* pData = (SDL_DisplayData*)SDL_GetDisplayDriverData(displayIndex);
		int adapterIndex = D3DADAPTER_DEFAULT;

		if (!pData) {
			SDL_SetError("Invalid display index");
			adapterIndex = -1; /* make sure we return something invalid */
		}
		else {
			char* displayName = XBOX_StringToUTF8(pData->DeviceName);
			unsigned int count = IDirect3D9_GetAdapterCount(pD3D);
			unsigned int i;
			for (i = 0; i < count; i++) {
				D3DADAPTER_IDENTIFIER9 id;
				IDirect3D9_GetAdapterIdentifier(pD3D, i, 0, &id);

				if (SDL_strcmp(id.DeviceName, displayName) == 0) {
					adapterIndex = i;
					break;
				}
			}
			SDL_free(displayName);
		}

		/* free up the D3D stuff we inited */
		IDirect3D9_Release(pD3D);
		SDL_UnloadObject(pD3DDLL);

		return adapterIndex;
	}
}
#endif

#if HAVE_DXGI_H
#define CINTERFACE
#define COBJMACROS
#include <dxgi.h>

static SDL_bool
DXGI_LoadDLL(void** pDXGIDLL, IDXGIFactory** pDXGIFactory)
{
	*pDXGIDLL = SDL_LoadObject("DXGI.DLL");
	if (*pDXGIDLL) {
		HRESULT(WINAPI * CreateDXGI)(REFIID riid, void** ppFactory);

		CreateDXGI =
			(HRESULT(WINAPI*) (REFIID, void**)) SDL_LoadFunction(*pDXGIDLL,
				"CreateDXGIFactory");
		if (CreateDXGI) {
			GUID dxgiGUID = { 0x7b7166ec,0x21c7,0x44ae,{0xb2,0x1a,0xc9,0xae,0x32,0x1a,0xe3,0x69} };
			if (!SUCCEEDED(CreateDXGI(&dxgiGUID, (void**)pDXGIFactory))) {
				*pDXGIFactory = NULL;
			}
		}
		if (!*pDXGIFactory) {
			SDL_UnloadObject(*pDXGIDLL);
			*pDXGIDLL = NULL;
			return SDL_FALSE;
		}

		return SDL_TRUE;
	}
	else {
		*pDXGIFactory = NULL;
		return SDL_FALSE;
	}
}
#endif

SDL_bool
SDL_DXGIGetOutputInfo(int displayIndex, int* adapterIndex, int* outputIndex)
{
#if !HAVE_DXGI_H
	if (adapterIndex) *adapterIndex = -1;
	if (outputIndex) *outputIndex = -1;
	SDL_SetError("SDL was compiled without DXGI support due to missing dxgi.h header");
	return SDL_FALSE;
#else
	SDL_DisplayData* pData = (SDL_DisplayData*)SDL_GetDisplayDriverData(displayIndex);
	void* pDXGIDLL;
	char* displayName;
	int nAdapter, nOutput;
	IDXGIFactory* pDXGIFactory;
	IDXGIAdapter* pDXGIAdapter;
	IDXGIOutput* pDXGIOutput;

	if (!adapterIndex) {
		SDL_InvalidParamError("adapterIndex");
		return SDL_FALSE;
	}

	if (!outputIndex) {
		SDL_InvalidParamError("outputIndex");
		return SDL_FALSE;
	}

	*adapterIndex = -1;
	*outputIndex = -1;

	if (!pData) {
		SDL_SetError("Invalid display index");
		return SDL_FALSE;
	}

	if (!DXGI_LoadDLL(&pDXGIDLL, &pDXGIFactory)) {
		SDL_SetError("Unable to create DXGI interface");
		return SDL_FALSE;
	}

	displayName = XBOX_StringToUTF8(pData->DeviceName);
	nAdapter = 0;
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

	/* free up the DXGI factory */
	IDXGIFactory_Release(pDXGIFactory);
#ifndef _XBOX
	SDL_UnloadObject(pDXGIDLL);
#endif
	if (*adapterIndex == -1) {
		return SDL_FALSE;
	}
	else {
		return SDL_TRUE;
	}
#endif
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