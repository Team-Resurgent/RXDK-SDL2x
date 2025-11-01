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

#include "SDL_render.h"
#ifndef _XBOX
#include "SDL_system.h"
#endif

#if SDL_VIDEO_RENDER_D3D && !SDL_RENDER_DISABLED

#include "../../core/xbox/SDL_xbox.h"

#include "SDL_hints.h"
#ifndef _XBOX
#include "SDL_loadso.h"
#endif
#include "SDL_syswm.h"
#include "SDL_log.h"
#include "SDL_assert.h"
#include "../SDL_sysrender.h"
#include "../SDL_d3dmath.h"
#include "../../video/xbox/SDL_xboxvideo.h"

#if SDL_VIDEO_RENDER_D3D
#define D3D_DEBUG_INFO
#endif

//#include "SDL_shaders_d3d.h" // TODO: Used for YUV textures

typedef struct
{
	SDL_Rect viewport;
	SDL_bool viewport_dirty;
	SDL_Texture* texture;
	SDL_BlendMode blend;
	SDL_bool cliprect_enabled;
	SDL_bool cliprect_enabled_dirty;
	SDL_Rect cliprect;
	SDL_bool cliprect_dirty;
	SDL_bool is_copy_ex;
	//    LPDIRECT3DPIXELSHADER9 shader; // TODO: Used for YUV textures
} D3D_DrawStateCache;

/* Direct3D renderer implementation */

typedef struct
{
#ifndef _XBOX
	void* d3dxDLL;
	void* d3dxDLL;
#endif
	IDirect3D8* d3d;
	IDirect3DDevice8* device;
	UINT adapter;
	D3DPRESENT_PARAMETERS pparams;
	SDL_bool updateSize;
	SDL_bool beginScene;
	SDL_bool enableSeparateAlphaBlend;
	D3DTEXTUREFILTERTYPE scaleMode[8];
	IDirect3DSurface8* defaultRenderTarget;
	IDirect3DSurface8* currentRenderTarget;
	//    LPDIRECT3DPIXELSHADER8 shaders[NUM_SHADERS]; // TODO: Used for YUV textures
	LPDIRECT3DVERTEXBUFFER8 vertexBuffers[8];
	size_t vertexBufferSize[8];
	int currentVertexBuffer;
	SDL_bool reportedVboProblem;
	D3D_DrawStateCache drawstate;
} D3D_RenderData;

typedef struct
{
	SDL_bool dirty;
	int w, h;
	DWORD usage;
	Uint32 format;
	D3DFORMAT d3dfmt;
	IDirect3DTexture8* texture;
	IDirect3DTexture8* staging;
} D3D_TextureRep;

typedef struct
{
	D3D_TextureRep texture;
	D3DTEXTUREFILTERTYPE scaleMode;

	/* YV12 texture support */
	SDL_bool yuv;
	D3D_TextureRep utexture;
	D3D_TextureRep vtexture;
	Uint8* pixels;
	int pitch;
	SDL_Rect locked_rect;
} D3D_TextureData;

typedef struct
{
	float x, y, z;	/* Vertex untransformed Position */
	DWORD color;	/* Vertex color */
	float u, v;		/* Texture Coordinates */
} Vertex;

static int
D3D_SetError(const char* prefix, HRESULT result)
{
	const char* error;

	switch (result) {
	case D3DERR_WRONGTEXTUREFORMAT:
		error = "WRONGTEXTUREFORMAT";
		break;
	case D3DERR_UNSUPPORTEDCOLOROPERATION:
		error = "UNSUPPORTEDCOLOROPERATION";
		break;
	case D3DERR_UNSUPPORTEDCOLORARG:
		error = "UNSUPPORTEDCOLORARG";
		break;
	case D3DERR_UNSUPPORTEDALPHAOPERATION:
		error = "UNSUPPORTEDALPHAOPERATION";
		break;
	case D3DERR_UNSUPPORTEDALPHAARG:
		error = "UNSUPPORTEDALPHAARG";
		break;
	case D3DERR_TOOMANYOPERATIONS:
		error = "TOOMANYOPERATIONS";
		break;
	case D3DERR_CONFLICTINGTEXTUREFILTER:
		error = "CONFLICTINGTEXTUREFILTER";
		break;
	case D3DERR_UNSUPPORTEDFACTORVALUE:
		error = "UNSUPPORTEDFACTORVALUE";
		break;
	case D3DERR_CONFLICTINGRENDERSTATE:
		error = "CONFLICTINGRENDERSTATE";
		break;
	case D3DERR_UNSUPPORTEDTEXTUREFILTER:
		error = "UNSUPPORTEDTEXTUREFILTER";
		break;
	case D3DERR_CONFLICTINGTEXTUREPALETTE:
		error = "CONFLICTINGTEXTUREPALETTE";
		break;
	case D3DERR_DRIVERINTERNALERROR:
		error = "DRIVERINTERNALERROR";
		break;
	case D3DERR_NOTFOUND:
		error = "NOTFOUND";
		break;
	case D3DERR_MOREDATA:
		error = "MOREDATA";
		break;
	case D3DERR_DEVICELOST:
		error = "DEVICELOST";
		break;
	case D3DERR_DEVICENOTRESET:
		error = "DEVICENOTRESET";
		break;
	case D3DERR_NOTAVAILABLE:
		error = "NOTAVAILABLE";
		break;
	case D3DERR_OUTOFVIDEOMEMORY:
		error = "OUTOFVIDEOMEMORY";
		break;
	case D3DERR_INVALIDDEVICE:
		error = "INVALIDDEVICE";
		break;
	case D3DERR_INVALIDCALL:
		error = "INVALIDCALL";
		break;
#ifndef _XBOX
	case D3DERR_DRIVERINVALIDCALL:
		error = "DRIVERINVALIDCALL";
		break;
	case D3DERR_WASSTILLDRAWING:
		error = "WASSTILLDRAWING";
		break;
#endif
	default:
		error = "UNKNOWN";
		break;
	}
	return SDL_SetError("%s: %s", prefix, error);
}

static D3DFORMAT
PixelFormatToD3DFMT(Uint32 format)
{
	switch (format) {
	case SDL_PIXELFORMAT_RGB565:
		return D3DFMT_LIN_R5G6B5;
	case SDL_PIXELFORMAT_RGB888:
		return D3DFMT_LIN_X8R8G8B8;
	case SDL_PIXELFORMAT_ARGB8888:
		return D3DFMT_LIN_A8R8G8B8;
	case SDL_PIXELFORMAT_YV12:
	case SDL_PIXELFORMAT_IYUV:
	case SDL_PIXELFORMAT_NV12:
	case SDL_PIXELFORMAT_NV21:
		return D3DFMT_LIN_L8;
	default:
		return D3DFMT_UNKNOWN;
	}
}

static Uint32
D3DFMTToPixelFormat(D3DFORMAT format)
{
	switch (format) {
	case D3DFMT_LIN_R5G6B5:
		return SDL_PIXELFORMAT_RGB565;
	case D3DFMT_LIN_X8R8G8B8:
		return SDL_PIXELFORMAT_RGB888;
	case D3DFMT_LIN_A8R8G8B8:
		return SDL_PIXELFORMAT_ARGB8888;
	default:
		return SDL_PIXELFORMAT_UNKNOWN;
	}
}

static void
D3D_InitRenderState(D3D_RenderData* data)
{
	D3DMATRIX matrix;

	IDirect3DDevice8* device = data->device;
	IDirect3DDevice8_SetVertexShader(device, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	IDirect3DDevice8_SetVertexShader(device, 0); // Why unset again here? :/
	IDirect3DDevice8_SetRenderState(device, D3DRS_ZENABLE, D3DZB_FALSE);
	IDirect3DDevice8_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
	IDirect3DDevice8_SetRenderState(device, D3DRS_LIGHTING, FALSE);

	/* Enable color modulation by diffuse color */
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_COLOROP,
		D3DTOP_MODULATE);
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_COLORARG1,
		D3DTA_TEXTURE);
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_COLORARG2,
		D3DTA_DIFFUSE);

	/* Enable alpha modulation by diffuse alpha */
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_ALPHAOP,
		D3DTOP_MODULATE);
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1,
		D3DTA_TEXTURE);
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_ALPHAARG2,
		D3DTA_DIFFUSE);

	/* Enable separate alpha blend function, if possible */
	if (data->enableSeparateAlphaBlend) { // FIXME: Unsupported on D3D8, need workaround
		//        IDirect3DDevice8_SetRenderState(device, D3DRS_SEPARATEALPHABLENDENABLE, TRUE); //FIXME
	}

	/* Disable second texture stage, since we're done */
	IDirect3DDevice8_SetTextureStageState(device, 1, D3DTSS_COLOROP,
		D3DTOP_DISABLE);
	IDirect3DDevice8_SetTextureStageState(device, 1, D3DTSS_ALPHAOP,
		D3DTOP_DISABLE);

	/* Set an identity world and view matrix */
	SDL_zero(matrix);
	matrix.m[0][0] = 1.0f;
	matrix.m[1][1] = 1.0f;
	matrix.m[2][2] = 1.0f;
	matrix.m[3][3] = 1.0f;
	IDirect3DDevice8_SetTransform(device, D3DTS_WORLD, &matrix);
	IDirect3DDevice8_SetTransform(device, D3DTS_VIEW, &matrix);

	/* Reset our current scale mode */
	SDL_memset(data->scaleMode, 0xFF, sizeof(data->scaleMode));

	/* Start the render with beginScene */
	data->beginScene = SDL_TRUE;
}

static int D3D_Reset(SDL_Renderer* renderer);

static int
D3D_ActivateRenderer(SDL_Renderer* renderer)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	HRESULT result;

	if (data->updateSize) {
		SDL_Window* window = renderer->window;
		int w, h;
		Uint32 window_flags = SDL_GetWindowFlags(window);

		SDL_GetWindowSize(window, &w, &h);
		data->pparams.BackBufferWidth = w;
		data->pparams.BackBufferHeight = h;
		if (window_flags & SDL_WINDOW_FULLSCREEN && (window_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != SDL_WINDOW_FULLSCREEN_DESKTOP) {
			SDL_DisplayMode fullscreen_mode;
			SDL_GetWindowDisplayMode(window, &fullscreen_mode);
			data->pparams.Windowed = FALSE;
			data->pparams.BackBufferFormat = PixelFormatToD3DFMT(fullscreen_mode.format);
			data->pparams.FullScreen_RefreshRateInHz = fullscreen_mode.refresh_rate;
		}
		else {
			data->pparams.Windowed = TRUE;
			data->pparams.BackBufferFormat = D3DFMT_UNKNOWN;
			data->pparams.FullScreen_RefreshRateInHz = 0;
		}
		if (D3D_Reset(renderer) < 0) {
			return -1;
		}

		data->updateSize = SDL_FALSE;
	}
	if (data->beginScene) {
		result = IDirect3DDevice8_BeginScene(data->device);
		if (result == D3DERR_DEVICELOST) {
			if (D3D_Reset(renderer) < 0) {
				return -1;
			}
			result = IDirect3DDevice8_BeginScene(data->device);
		}
		if (FAILED(result)) {
			return D3D_SetError("BeginScene()", result);
		}
		data->beginScene = SDL_FALSE;
	}
	return 0;
}

static void
D3D_WindowEvent(SDL_Renderer* renderer, const SDL_WindowEvent* event)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;

	if (event->event == SDL_WINDOWEVENT_SIZE_CHANGED) {
		data->updateSize = SDL_TRUE;
	}
}

static D3DBLEND GetBlendFunc(SDL_BlendFactor factor)
{
	switch (factor) {
	case SDL_BLENDFACTOR_ZERO:
		return D3DBLEND_ZERO;
	case SDL_BLENDFACTOR_ONE:
		return D3DBLEND_ONE;
	case SDL_BLENDFACTOR_SRC_COLOR:
		return D3DBLEND_SRCCOLOR;
	case SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR:
		return D3DBLEND_INVSRCCOLOR;
	case SDL_BLENDFACTOR_SRC_ALPHA:
		return D3DBLEND_SRCALPHA;
	case SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:
		return D3DBLEND_INVSRCALPHA;
	case SDL_BLENDFACTOR_DST_COLOR:
		return D3DBLEND_DESTCOLOR;
	case SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR:
		return D3DBLEND_INVDESTCOLOR;
	case SDL_BLENDFACTOR_DST_ALPHA:
		return D3DBLEND_DESTALPHA;
	case SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA:
		return D3DBLEND_INVDESTALPHA;
	default:
		return (D3DBLEND)0;
	}
}

static SDL_bool
D3D_SupportsBlendMode(SDL_Renderer* renderer, SDL_BlendMode blendMode)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	SDL_BlendFactor srcColorFactor = SDL_GetBlendModeSrcColorFactor(blendMode);
	SDL_BlendFactor srcAlphaFactor = SDL_GetBlendModeSrcAlphaFactor(blendMode);
	SDL_BlendOperation colorOperation = SDL_GetBlendModeColorOperation(blendMode);
	SDL_BlendFactor dstColorFactor = SDL_GetBlendModeDstColorFactor(blendMode);
	SDL_BlendFactor dstAlphaFactor = SDL_GetBlendModeDstAlphaFactor(blendMode);
	SDL_BlendOperation alphaOperation = SDL_GetBlendModeAlphaOperation(blendMode);

	if (!GetBlendFunc(srcColorFactor) || !GetBlendFunc(srcAlphaFactor) ||
		!GetBlendFunc(dstColorFactor) || !GetBlendFunc(dstAlphaFactor)) {
		return SDL_FALSE;
	}
	if ((srcColorFactor != srcAlphaFactor || dstColorFactor != dstAlphaFactor) && !data->enableSeparateAlphaBlend) {
		return SDL_FALSE;
	}
	if (colorOperation != SDL_BLENDOPERATION_ADD || alphaOperation != SDL_BLENDOPERATION_ADD) {
		return SDL_FALSE;
	}
	return SDL_TRUE;
}

static int
D3D_CreateTextureRep(IDirect3DDevice8* device, D3D_TextureRep* texture, DWORD usage, Uint32 format, D3DFORMAT d3dfmt, int w, int h)
{
	HRESULT result;

	texture->dirty = SDL_FALSE;
	texture->w = w;
	texture->h = h;
	texture->usage = usage;
	texture->format = format;
	texture->d3dfmt = d3dfmt;

	result = IDirect3DDevice8_CreateTexture(device, w, h, 1, usage,
		PixelFormatToD3DFMT(format),
		D3DPOOL_DEFAULT, &texture->texture);

	if (FAILED(result)) {
		return D3D_SetError("CreateTexture(D3DPOOL_DEFAULT)", result);
	}
	return 0;
}

static int
D3D_CreateStagingTexture(IDirect3DDevice8* device, D3D_TextureRep* texture)
{
	HRESULT result;

	if (texture->staging == NULL) {
		result = IDirect3DDevice8_CreateTexture(device, texture->w, texture->h, 1, 0,
			texture->d3dfmt, D3DPOOL_SYSTEMMEM, &texture->staging);

		if (FAILED(result)) {
			return D3D_SetError("CreateTexture(D3DPOOL_SYSTEMMEM)", result);
		}
	}
	return 0;
}

static int
D3D_RecreateTextureRep(IDirect3DDevice8* device, D3D_TextureRep* texture)
{
	if (texture->texture) {
		IDirect3DTexture8_Release(texture->texture);
		texture->texture = NULL;
	}
	if (texture->staging) {
#ifndef _XBOX
		// We can't use this as we are uisng our own UpdateTexture()
		IDirect3DTexture8_AddDirtyRect(texture->staging, NULL);
#endif
		texture->dirty = SDL_TRUE;
	}
	return 0;
}

static int
D3D_UpdateTextureRep(IDirect3DDevice8* device, D3D_TextureRep* texture, int x, int y, int w, int h, const void* pixels, int pitch)
{
	RECT d3drect;
	D3DLOCKED_RECT locked;
	const Uint8* src;
	Uint8* dst;
	int row, length;
	HRESULT result;

	if (D3D_CreateStagingTexture(device, texture) < 0) {
		return -1;
	}

	d3drect.left = x;
	d3drect.right = x + w;
	d3drect.top = y;
	d3drect.bottom = y + h;

	result = IDirect3DTexture8_LockRect(texture->staging, 0, &locked, &d3drect, 0);
	if (FAILED(result)) {
		return D3D_SetError("LockRect()", result);
	}

	src = (const Uint8*)pixels;
	dst = (Uint8*)locked.pBits;
	length = w * SDL_BYTESPERPIXEL(texture->format);
	if (length == pitch && length == locked.Pitch) {
		SDL_memcpy(dst, src, length * h);
	}
	else {
		if (length > pitch) {
			length = pitch;
		}
		if (length > locked.Pitch) {
			length = locked.Pitch;
		}
		for (row = 0; row < h; ++row) {
			SDL_memcpy(dst, src, length);
			src += pitch;
			dst += locked.Pitch;
		}
	}
	result = IDirect3DTexture8_UnlockRect(texture->staging, 0);
	if (FAILED(result)) {
		return D3D_SetError("UnlockRect()", result);
	}
	texture->dirty = SDL_TRUE;

	return 0;
}

static void
D3D_DestroyTextureRep(D3D_TextureRep* texture)
{
	if (texture->texture) {
		IDirect3DTexture8_Release(texture->texture);
		texture->texture = NULL;
	}
	if (texture->staging) {
		IDirect3DTexture8_Release(texture->staging);
		texture->staging = NULL;
	}
}

static int
D3D_CreateTexture(SDL_Renderer* renderer, SDL_Texture* texture)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* texturedata;
	DWORD usage;

	texturedata = (D3D_TextureData*)SDL_calloc(1, sizeof(*texturedata));
	if (!texturedata) {
		return SDL_OutOfMemory();
	}
	texturedata->scaleMode = (texture->scaleMode == SDL_ScaleModeNearest) ? D3DTEXF_POINT : D3DTEXF_LINEAR;

	texture->driverdata = texturedata;

	if (texture->access == SDL_TEXTUREACCESS_TARGET) {
		usage = D3DUSAGE_RENDERTARGET;
	}
	else {
		usage = 0;
	}

	if (D3D_CreateTextureRep(data->device, &texturedata->texture, usage, texture->format, PixelFormatToD3DFMT(texture->format), texture->w, texture->h) < 0) {
		return -1;
	}

	if (texture->format == SDL_PIXELFORMAT_YV12 ||
		texture->format == SDL_PIXELFORMAT_IYUV) {
		texturedata->yuv = SDL_TRUE;

		if (D3D_CreateTextureRep(data->device, &texturedata->utexture, usage, texture->format, PixelFormatToD3DFMT(texture->format), (texture->w + 1) / 2, (texture->h + 1) / 2) < 0) {
			return -1;
		}

		if (D3D_CreateTextureRep(data->device, &texturedata->vtexture, usage, texture->format, PixelFormatToD3DFMT(texture->format), (texture->w + 1) / 2, (texture->h + 1) / 2) < 0) {
			return -1;
		}
	}
	return 0;
}

static int
D3D_RecreateTexture(SDL_Renderer* renderer, SDL_Texture* texture)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;

	if (!texturedata) {
		return 0;
	}

	if (D3D_RecreateTextureRep(data->device, &texturedata->texture) < 0) {
		return -1;
	}

	if (texturedata->yuv) {
		if (D3D_RecreateTextureRep(data->device, &texturedata->utexture) < 0) {
			return -1;
		}

		if (D3D_RecreateTextureRep(data->device, &texturedata->vtexture) < 0) {
			return -1;
		}
	}
	return 0;
}

static int
D3D_UpdateTexture(SDL_Renderer* renderer, SDL_Texture* texture,
	const SDL_Rect* rect, const void* pixels, int pitch)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;

	if (!texturedata) {
		SDL_SetError("Texture is not currently available");
		return -1;
	}

	if (D3D_UpdateTextureRep(data->device, &texturedata->texture, rect->x, rect->y, rect->w, rect->h, pixels, pitch) < 0) {
		return -1;
	}

	if (texturedata->yuv) {
		/* Skip to the correct offset into the next texture */
		pixels = (const void*)((const Uint8*)pixels + rect->h * pitch);

		if (D3D_UpdateTextureRep(data->device, texture->format == SDL_PIXELFORMAT_YV12 ? &texturedata->vtexture : &texturedata->utexture, rect->x / 2, rect->y / 2, (rect->w + 1) / 2, (rect->h + 1) / 2, pixels, (pitch + 1) / 2) < 0) {
			return -1;
		}

		/* Skip to the correct offset into the next texture */
		pixels = (const void*)((const Uint8*)pixels + ((rect->h + 1) / 2) * ((pitch + 1) / 2));
		if (D3D_UpdateTextureRep(data->device, texture->format == SDL_PIXELFORMAT_YV12 ? &texturedata->utexture : &texturedata->vtexture, rect->x / 2, (rect->y + 1) / 2, (rect->w + 1) / 2, (rect->h + 1) / 2, pixels, (pitch + 1) / 2) < 0) {
			return -1;
		}
	}
	return 0;
}

static int
D3D_UpdateTextureYUV(SDL_Renderer* renderer, SDL_Texture* texture,
	const SDL_Rect* rect,
	const Uint8* Yplane, int Ypitch,
	const Uint8* Uplane, int Upitch,
	const Uint8* Vplane, int Vpitch)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;

	if (!texturedata) {
		SDL_SetError("Texture is not currently available");
		return -1;
	}

	if (D3D_UpdateTextureRep(data->device, &texturedata->texture, rect->x, rect->y, rect->w, rect->h, Yplane, Ypitch) < 0) {
		return -1;
	}
	if (D3D_UpdateTextureRep(data->device, &texturedata->utexture, rect->x / 2, rect->y / 2, (rect->w + 1) / 2, (rect->h + 1) / 2, Uplane, Upitch) < 0) {
		return -1;
	}
	if (D3D_UpdateTextureRep(data->device, &texturedata->vtexture, rect->x / 2, rect->y / 2, (rect->w + 1) / 2, (rect->h + 1) / 2, Vplane, Vpitch) < 0) {
		return -1;
	}
	return 0;
}

static int
D3D_LockTexture(SDL_Renderer* renderer, SDL_Texture* texture,
	const SDL_Rect* rect, void** pixels, int* pitch)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;
	IDirect3DDevice8* device = data->device;

	if (!texturedata) {
		SDL_SetError("Texture is not currently available");
		return -1;
	}

	texturedata->locked_rect = *rect;

	if (texturedata->yuv) {
		/* It's more efficient to upload directly... */
		if (!texturedata->pixels) {
			texturedata->pitch = texture->w;
			texturedata->pixels = (Uint8*)SDL_malloc((texture->h * texturedata->pitch * 3) / 2);
			if (!texturedata->pixels) {
				return SDL_OutOfMemory();
			}
		}
		*pixels =
			(void*)((Uint8*)texturedata->pixels + rect->y * texturedata->pitch +
				rect->x * SDL_BYTESPERPIXEL(texture->format));
		*pitch = texturedata->pitch;
	}
	else {
		RECT d3drect;
		D3DLOCKED_RECT locked;
		HRESULT result;

		if (D3D_CreateStagingTexture(device, &texturedata->texture) < 0) {
			return -1;
		}

		d3drect.left = rect->x;
		d3drect.right = rect->x + rect->w;
		d3drect.top = rect->y;
		d3drect.bottom = rect->y + rect->h;

		result = IDirect3DTexture8_LockRect(texturedata->texture.staging, 0, &locked, &d3drect, 0);
		if (FAILED(result)) {
			return D3D_SetError("LockRect()", result);
		}
		*pixels = locked.pBits;
		*pitch = locked.Pitch;
	}
	return 0;
}

static void
D3D_UnlockTexture(SDL_Renderer* renderer, SDL_Texture* texture)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;

	if (!texturedata) {
		return;
	}

	if (texturedata->yuv) {
		const SDL_Rect* rect = &texturedata->locked_rect;
		void* pixels =
			(void*)((Uint8*)texturedata->pixels + rect->y * texturedata->pitch +
				rect->x * SDL_BYTESPERPIXEL(texture->format));
		D3D_UpdateTexture(renderer, texture, rect, pixels, texturedata->pitch);
	}
	else {
		IDirect3DTexture8_UnlockRect(texturedata->texture.staging, 0);
		texturedata->texture.dirty = SDL_TRUE;
		if (data->drawstate.texture == texture) {
			data->drawstate.texture = NULL;
		}
	}
}

int D3D8_UpdateTexture(IDirect3DTexture8* srcTexture, IDirect3DTexture8* dstTexture)
{
	LPDIRECT3DSURFACE8 srcSurface;
	D3DLOCKED_RECT srcLockrect;
	D3DLOCKED_RECT destLockrect;
	D3DSURFACE_DESC desc;
	char* srcData, * dstData;
	int result;

	result = IDirect3DTexture8_GetSurfaceLevel(srcTexture, 0, &srcSurface);
	if (FAILED(result))
		return D3D_SetError("D3D8_UpdateTexture()", result);

	result = IDirect3DSurface8_GetDesc(srcSurface, &desc);
	if (FAILED(result))
		return D3D_SetError("D3D8_UpdateTexture()", result);

	result = IDirect3DSurface8_LockRect(srcSurface, &srcLockrect, NULL, 0);
	if (FAILED(result))
		return D3D_SetError("D3D8_UpdateTexture()", result);

	result = IDirect3DTexture8_LockRect(dstTexture, 0, &destLockrect, NULL, 0);
	if (FAILED(result))
		return D3D_SetError("D3D8_UpdateTexture()", result);

	srcData = (char*)srcLockrect.pBits;
	dstData = (char*)destLockrect.pBits;

	// NOTE: DirectX 9 docs say that the UpdateTexture() function
	//       can only take textures in the same format so I think
	//       we are ok to hardcode 4 bytes for source and destination
	//       without checking they match first

#if 0 // Slow AF!!
	for (y = 0; y < (int)desc.Height; y++)
	{
		for (x = 0; x < (int)desc.Width; x++)
		{
			dstData[0] = srcData[0];
			dstData[1] = srcData[1];
			dstData[2] = srcData[2];
			dstData[3] = srcData[3];

			srcData += 4;
			dstData += 4;
		}
	}
#else // Much faster!
	memcpy(dstData, srcData, desc.Width * desc.Height * 4);
#endif

	result = IDirect3DSurface8_UnlockRect(srcSurface);
	if (FAILED(result))
		return D3D_SetError("D3D8_UpdateTexture()", result);

	result = IDirect3DSurface8_Release(srcSurface);
	if (FAILED(result))
		return D3D_SetError("D3D8_UpdateTexture()", result);

	result = IDirect3DTexture8_UnlockRect(dstTexture, 0);
	if (FAILED(result))
		return D3D_SetError("D3D8_UpdateTexture()", result);

	return result;
}

static int
D3D_SetRenderTargetInternal(SDL_Renderer* renderer, SDL_Texture* texture)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* texturedata;
	D3D_TextureRep* texturerep;
	HRESULT result;
	IDirect3DDevice8* device = data->device;

	/* Release the previous render target if it wasn't the default one */
	if (data->currentRenderTarget != NULL) {
		IDirect3DSurface8_Release(data->currentRenderTarget);
		data->currentRenderTarget = NULL;
	}

	if (texture == NULL) {
		IDirect3DDevice8_SetRenderTarget(data->device, 0, data->defaultRenderTarget);
		return 0;
	}

	texturedata = (D3D_TextureData*)texture->driverdata;
	if (!texturedata) {
		SDL_SetError("Texture is not currently available");
		return -1;
	}

	/* Make sure the render target is updated if it was locked and written to */
	texturerep = &texturedata->texture;
	if (texturerep->dirty && texturerep->staging) {
		if (!texturerep->texture) {
			result = IDirect3DDevice8_CreateTexture(device, texturerep->w, texturerep->h, 1, texturerep->usage,
				PixelFormatToD3DFMT(texturerep->format), D3DPOOL_DEFAULT, &texturerep->texture);
			if (FAILED(result)) {
				return D3D_SetError("CreateTexture(D3DPOOL_DEFAULT)", result);
			}
		}

#ifdef _XBOX
		result = D3D8_UpdateTexture(texturerep->staging, texturerep->texture);
#else
		result = IDirect3DDevice9_UpdateTexture(device, (IDirect3DBaseTexture9*)texturerep->staging, (IDirect3DBaseTexture9*)texturerep->texture);
#endif
		if (FAILED(result)) {
			return D3D_SetError("UpdateTexture()", result);
		}
		texturerep->dirty = SDL_FALSE;
	}

	result = IDirect3DTexture8_GetSurfaceLevel(texturedata->texture.texture, 0, &data->currentRenderTarget);
	if (FAILED(result)) {
		return D3D_SetError("GetSurfaceLevel()", result);
	}
	// FIXME ASAYU:
	// IDirect3DDevice8_SetRenderTarget(D3DDevice *pThis, D3DSurface *pRenderTarget, D3DSurface *pNewZStencil)
	result = IDirect3DDevice8_SetRenderTarget(data->device, 0, data->currentRenderTarget);
	if (FAILED(result)) {
		return D3D_SetError("SetRenderTarget()", result);
	}

	return 0;
}

static int
D3D_SetRenderTarget(SDL_Renderer* renderer, SDL_Texture* texture)
{
	if (D3D_ActivateRenderer(renderer) < 0) {
		return -1;
	}

	return D3D_SetRenderTargetInternal(renderer, texture);
}

static int
D3D_QueueSetViewport(SDL_Renderer* renderer, SDL_RenderCommand* cmd)
{
	return 0;  /* nothing to do in this backend. */
}

static int
D3D_QueueDrawPoints(SDL_Renderer* renderer, SDL_RenderCommand* cmd, const SDL_FPoint* points, int count)
{
	const DWORD color = D3DCOLOR_ARGB(cmd->data.draw.a, cmd->data.draw.r, cmd->data.draw.g, cmd->data.draw.b);
	const size_t vertslen = count * sizeof(Vertex);
	Vertex* verts = (Vertex*)SDL_AllocateRenderVertices(renderer, vertslen, 0, &cmd->data.draw.first);
	int i;

	if (!verts) {
		return -1;
	}

	SDL_memset(verts, '\0', vertslen);
	cmd->data.draw.count = count;

	for (i = 0; i < count; i++, verts++, points++) {
		verts->x = points->x;
		verts->y = points->y;
		verts->color = color;
	}

	return 0;
}

static int
D3D_QueueFillRects(SDL_Renderer* renderer, SDL_RenderCommand* cmd, const SDL_FRect* rects, int count)
{
	const DWORD color = D3DCOLOR_ARGB(cmd->data.draw.a, cmd->data.draw.r, cmd->data.draw.g, cmd->data.draw.b);
	const size_t vertslen = count * sizeof(Vertex) * 4;
	Vertex* verts = (Vertex*)SDL_AllocateRenderVertices(renderer, vertslen, 0, &cmd->data.draw.first);
	int i;

	if (!verts) {
		return -1;
	}

	SDL_memset(verts, '\0', vertslen);
	cmd->data.draw.count = count;

	for (i = 0; i < count; i++) {
		const SDL_FRect* rect = &rects[i];
		const float minx = rect->x;
		const float maxx = rect->x + rect->w;
		const float miny = rect->y;
		const float maxy = rect->y + rect->h;

		verts->x = minx;
		verts->y = miny;
		verts->color = color;
		verts++;

		verts->x = maxx;
		verts->y = miny;
		verts->color = color;
		verts++;

		verts->x = maxx;
		verts->y = maxy;
		verts->color = color;
		verts++;

		verts->x = minx;
		verts->y = maxy;
		verts->color = color;
		verts++;
	}

	return 0;
}

static int
D3D_QueueCopy(SDL_Renderer* renderer, SDL_RenderCommand* cmd, SDL_Texture* texture,
	const SDL_Rect* srcrect, const SDL_FRect* dstrect)
{
	const DWORD color = D3DCOLOR_ARGB(cmd->data.draw.a, cmd->data.draw.r, cmd->data.draw.g, cmd->data.draw.b);
	float minx, miny, maxx, maxy;
	float minu, maxu, minv, maxv;
	const size_t vertslen = sizeof(Vertex) * 4;
	Vertex* verts = (Vertex*)SDL_AllocateRenderVertices(renderer, vertslen, 0, &cmd->data.draw.first);

	if (!verts) {
		return -1;
	}

	cmd->data.draw.count = 1;

	minx = dstrect->x - 0.5f;
	miny = dstrect->y - 0.5f;
	maxx = dstrect->x + dstrect->w - 0.5f;
	maxy = dstrect->y + dstrect->h - 0.5f;

	// FIXME: The texture coordinates here might be wrong for when we copy
	//        using a rect destination, needs some testing, it was correct
	//        when it was set to clamp to edge before switching to linear
	//		  textures. Works fine for what I've used so far.

	/* ASAYU: While the following comment is correct according to the documentation
			  It doesn't seem to work that way with this current implementation
	*/
	/*
		minu = (float) srcrect->x / texture->w;
		maxu = (float) (srcrect->x + srcrect->w) / texture->w;
		minv = (float) srcrect->y / texture->h;
		maxv = (float) (srcrect->y + srcrect->h) / texture->h;
	*/
	minu = srcrect->x;
	maxu = srcrect->x + srcrect->w;
	minv = srcrect->y;
	maxv = srcrect->y + srcrect->h;

	verts->x = minx;
	verts->y = miny;
	verts->z = 0.0f;
	verts->color = color;
	verts->u = minu; //(float)0; //minu;
	verts->v = minv; //(float)0; //minv;
	verts++;

	verts->x = maxx;
	verts->y = miny;
	verts->z = 0.0f;
	verts->color = color;
	verts->u = maxu;//(float)texture->w; //maxu;
	verts->v = minv;//(float)0; //minv;
	verts++;

	verts->x = maxx;
	verts->y = maxy;
	verts->z = 0.0f;
	verts->color = color;
	verts->u = maxu;//(float)texture->w; //maxu;
	verts->v = maxv;//(float)texture->h; //maxv;
	verts++;

	verts->x = minx;
	verts->y = maxy;
	verts->z = 0.0f;
	verts->color = color;
	verts->u = minu;//(float)0; //minu;
	verts->v = maxv;//(float)texture->h; //maxv;
	verts++;

	return 0;
}

static int
D3D_QueueCopyEx(SDL_Renderer* renderer, SDL_RenderCommand* cmd, SDL_Texture* texture,
	const SDL_Rect* srcquad, const SDL_FRect* dstrect,
	const double angle, const SDL_FPoint* center, const SDL_RendererFlip flip)
{
	const DWORD color = D3DCOLOR_ARGB(cmd->data.draw.a, cmd->data.draw.r, cmd->data.draw.g, cmd->data.draw.b);
	float minx, miny, maxx, maxy;
	float minu, maxu, minv, maxv;
	const size_t vertslen = sizeof(Vertex) * 5;
	Vertex* verts = (Vertex*)SDL_AllocateRenderVertices(renderer, vertslen, 0, &cmd->data.draw.first);

	if (!verts) {
		return -1;
	}

	cmd->data.draw.count = 1;

	minx = -center->x;
	maxx = dstrect->w - center->x;
	miny = -center->y;
	maxy = dstrect->h - center->y;

	if (flip & SDL_FLIP_HORIZONTAL) {
		minu = (float)(srcquad->x + srcquad->w) / texture->w;
		maxu = (float)srcquad->x / texture->w;
	}
	else {
		minu = (float)srcquad->x / texture->w;
		maxu = (float)(srcquad->x + srcquad->w) / texture->w;
	}

	if (flip & SDL_FLIP_VERTICAL) {
		minv = (float)(srcquad->y + srcquad->h) / texture->h;
		maxv = (float)srcquad->y / texture->h;
	}
	else {
		minv = (float)srcquad->y / texture->h;
		maxv = (float)(srcquad->y + srcquad->h) / texture->h;
	}

	// FIXME: The texture coordinates here might be wrong for when we copy
	//        using a rect destination, needs some testing, it was correct
	//        when it was set to clamp to edge before switching to linear
	//		  textures. Works fine for what I've used so far.

	verts->x = minx;
	verts->y = miny;
	verts->z = 0.0f;
	verts->color = color;
	verts->u = verts->x; //minu;
	verts->v = verts->y; //maxv;
	verts++;

	verts->x = maxx;
	verts->y = miny;
	verts->z = 0.0f;
	verts->color = color;
	verts->u = verts->x; //minu;
	verts->v = verts->y; //maxv;
	verts++;

	verts->x = maxx;
	verts->y = maxy;
	verts->z = 0.0f;
	verts->color = color;
	verts->u = verts->x; //minu;
	verts->v = verts->y; //maxv;
	verts++;

	verts->x = minx;
	verts->y = maxy;
	verts->z = 0.0f;
	verts->color = color;
	verts->u = verts->x; //minu;
	verts->v = verts->y; //maxv;
	verts++;

	verts->x = dstrect->x + center->x - 0.5f;  /* X translation */
	verts->y = dstrect->y + center->y - 0.5f;  /* Y translation */
	verts->z = (float)(M_PI * (float)angle / 180.0f);  /* rotation */
	verts->color = 0;
	verts->u = verts->x; //0.0f;
	verts->v = verts->y; //0.0f;
	verts++;

	return 0;
}

static int
UpdateDirtyTexture(IDirect3DDevice8* device, D3D_TextureRep* texture)
{
	if (texture->dirty && texture->staging) {
		HRESULT result;
		if (!texture->texture) {
			result = IDirect3DDevice8_CreateTexture(device, texture->w, texture->h, 1, texture->usage,
				PixelFormatToD3DFMT(texture->format), D3DPOOL_DEFAULT, &texture->texture);
			if (FAILED(result)) {
				return D3D_SetError("CreateTexture(D3DPOOL_DEFAULT)", result);
			}
		}

#ifdef _XBOX
		result = D3D8_UpdateTexture(texture->staging, texture->texture);
#else//ifdef _DX9
		result = IDirect3DDevice9_UpdateTexture((IDirect3DBaseTexture9*)texture->staging, (IDirect3DBaseTexture9*)texture->texture);
#endif
		if (FAILED(result)) {
			return D3D_SetError("UpdateTexture()", result);
		}
		texture->dirty = SDL_FALSE;
	}
	return 0;
}

static int
BindTextureRep(IDirect3DDevice8* device, D3D_TextureRep* texture, DWORD sampler)
{
	HRESULT result;
	UpdateDirtyTexture(device, texture);
	result = IDirect3DDevice8_SetTexture(device, sampler, (IDirect3DBaseTexture8*)texture->texture);

	if (FAILED(result)) {
		return D3D_SetError("SetTexture()", result);
	}
	return 0;
}

static void
UpdateTextureScaleMode(D3D_RenderData* data, D3D_TextureData* texturedata, unsigned index)
{
	if (texturedata->scaleMode != data->scaleMode[index]) {
		IDirect3DDevice8_SetTextureStageState(data->device, index, D3DTSS_MINFILTER,
			texturedata->scaleMode);
		IDirect3DDevice8_SetTextureStageState(data->device, index, D3DTSS_MAGFILTER,
			texturedata->scaleMode);
		IDirect3DDevice8_SetTextureStageState(data->device, index, D3DTSS_ADDRESSU,
			D3DTADDRESS_CLAMP);
		IDirect3DDevice8_SetTextureStageState(data->device, index, D3DTSS_ADDRESSV,
			D3DTADDRESS_CLAMP);

		data->scaleMode[index] = texturedata->scaleMode;
	}
}

static int
SetupTextureState(D3D_RenderData* data, SDL_Texture* texture/*, LPDIRECT3DPIXELSHADER9 *shader*/)
{
	D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;

	//  SDL_assert(*shader == NULL); // TODO: Used for YUV textures

	if (!texturedata) {
		SDL_SetError("Texture is not currently available");
		return -1;
	}

	UpdateTextureScaleMode(data, texturedata, 0);

	if (BindTextureRep(data->device, &texturedata->texture, 0) < 0) {
		return -1;
	}

	if (texturedata->yuv) {
		switch (SDL_GetYUVConversionModeForResolution(texture->w, texture->h)) {
		case SDL_YUV_CONVERSION_JPEG:
			//      *shader = data->shaders[SHADER_YUV_JPEG]; // TODO: Used for YUV textures
			break;
		case SDL_YUV_CONVERSION_BT601:
			//       *shader = data->shaders[SHADER_YUV_BT601]; // TODO: Used for YUV textures
			break;
		case SDL_YUV_CONVERSION_BT709:
			//       *shader = data->shaders[SHADER_YUV_BT709]; // TODO: Used for YUV textures
			break;
		default:
			return SDL_SetError("Unsupported YUV conversion mode");
		}

		UpdateTextureScaleMode(data, texturedata, 1);
		UpdateTextureScaleMode(data, texturedata, 2);

		if (BindTextureRep(data->device, &texturedata->utexture, 1) < 0) {
			return -1;
		}
		if (BindTextureRep(data->device, &texturedata->vtexture, 2) < 0) {
			return -1;
		}
	}
	return 0;
}

static int
SetDrawState(D3D_RenderData* data, const SDL_RenderCommand* cmd)
{
	const SDL_bool was_copy_ex = data->drawstate.is_copy_ex;
	const SDL_bool is_copy_ex = (cmd->command == SDL_RENDERCMD_COPY_EX);
	SDL_Texture* texture = cmd->data.draw.texture;
	const SDL_BlendMode blend = cmd->data.draw.blend;

	if (texture != data->drawstate.texture) {
		D3D_TextureData* oldtexturedata = data->drawstate.texture ? (D3D_TextureData*)data->drawstate.texture->driverdata : NULL;
		D3D_TextureData* newtexturedata = texture ? (D3D_TextureData*)texture->driverdata : NULL;
		//        LPDIRECT3DPIXELSHADER8 shader = NULL; // TODO: Used for YUV textures

				/* disable any enabled textures we aren't going to use, let SetupTextureState() do the rest. */
		if (texture == NULL) {
			IDirect3DDevice8_SetTexture(data->device, 0, NULL);
		}
		if ((!newtexturedata || !newtexturedata->yuv) && (oldtexturedata && oldtexturedata->yuv)) {
			IDirect3DDevice8_SetTexture(data->device, 1, NULL);
			IDirect3DDevice8_SetTexture(data->device, 2, NULL);
		}
		if (texture && SetupTextureState(data, texture/*, &shader*/) < 0) {
			return -1;
		}

#if 0 // TODO: Used for YUV textures
		const HRESULT result = IDirect3DDevice9_SetPixelShader(data->device, shader);
		if (FAILED(result)) {
			return D3D_SetError("IDirect3DDevice9_SetPixelShader()", result);
		}
		data->drawstate.shader = shader;
	}
#endif
	data->drawstate.texture = texture;
}
 else if (texture) {
	D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;
	UpdateDirtyTexture(data->device, &texturedata->texture);
	if (texturedata->yuv) {
		UpdateDirtyTexture(data->device, &texturedata->utexture);
		UpdateDirtyTexture(data->device, &texturedata->vtexture);
	}
	}

	if (blend != data->drawstate.blend) {
		if (blend == SDL_BLENDMODE_NONE) {
			IDirect3DDevice8_SetRenderState(data->device, D3DRS_ALPHABLENDENABLE, FALSE);
		}
		else {
			IDirect3DDevice8_SetRenderState(data->device, D3DRS_ALPHABLENDENABLE, TRUE);
			IDirect3DDevice8_SetRenderState(data->device, D3DRS_SRCBLEND,
				GetBlendFunc(SDL_GetBlendModeSrcColorFactor(blend)));
			IDirect3DDevice8_SetRenderState(data->device, D3DRS_DESTBLEND,
				GetBlendFunc(SDL_GetBlendModeDstColorFactor(blend)));
			if (data->enableSeparateAlphaBlend) {
#ifndef _XBOX // TODO: Unsupported on D3D8, need a workaround
				IDirect3DDevice9_SetRenderState(data->device, D3DRS_SRCBLENDALPHA,
					GetBlendFunc(SDL_GetBlendModeSrcAlphaFactor(blend)));
				IDirect3DDevice9_SetRenderState(data->device, D3DRS_DESTBLENDALPHA,
					GetBlendFunc(SDL_GetBlendModeDstAlphaFactor(blend)));
#endif
			}
		}

		data->drawstate.blend = blend;
	}

	if (is_copy_ex != was_copy_ex) {
		if (!is_copy_ex) {  /* SDL_RENDERCMD_COPY_EX will set this, we only want to reset it here if necessary. */
			const Float4X4 d3dmatrix = MatrixIdentity();
			IDirect3DDevice8_SetTransform(data->device, D3DTS_VIEW, (D3DMATRIX*)&d3dmatrix);
		}
		data->drawstate.is_copy_ex = is_copy_ex;
	}

	if (data->drawstate.viewport_dirty) {
		const SDL_Rect* viewport = &data->drawstate.viewport;
		const D3DVIEWPORT8 d3dviewport = { viewport->x, viewport->y, viewport->w, viewport->h, 0.0f, 1.0f };
		IDirect3DDevice8_SetViewport(data->device, &d3dviewport);

		/* Set an orthographic projection matrix */
		if (viewport->w && viewport->h) {
			D3DMATRIX d3dmatrix;
			SDL_zero(d3dmatrix);
			d3dmatrix.m[0][0] = 2.0f / viewport->w;
			d3dmatrix.m[1][1] = -2.0f / viewport->h;
			d3dmatrix.m[2][2] = 1.0f;
			d3dmatrix.m[3][0] = -1.0f;
			d3dmatrix.m[3][1] = 1.0f;
			d3dmatrix.m[3][3] = 1.0f;
			IDirect3DDevice8_SetTransform(data->device, D3DTS_PROJECTION, &d3dmatrix);
		}

		data->drawstate.viewport_dirty = SDL_FALSE;
	}

	if (data->drawstate.cliprect_enabled_dirty) {
#ifndef _XBOX // The Xbox extension before D3D9 does not need this
		IDirect3DDevice9_SetRenderState(data->device, D3DRS_SCISSORTESTENABLE, data->drawstate.cliprect_enabled ? TRUE : FALSE);
#endif
		data->drawstate.cliprect_enabled_dirty = SDL_FALSE;
	}

	if (data->drawstate.cliprect_dirty) {
		const SDL_Rect* viewport = &data->drawstate.viewport;
		const SDL_Rect* rect = &data->drawstate.cliprect;
#ifdef _XBOX
		const D3DRECT d3drect = { viewport->x + rect->x, viewport->y + rect->y, viewport->x + rect->x + rect->w, viewport->y + rect->y + rect->h };
		IDirect3DDevice8_SetScissors(data->device, 1, FALSE, &d3drect);
#else
		const RECT d3drect = { viewport->x + rect->x, viewport->y + rect->y, viewport->x + rect->x + rect->w, viewport->y + rect->y + rect->h };
		IDirect3DDevice9_SetScissorRect(data->device, &d3drect);
#endif
		data->drawstate.cliprect_dirty = SDL_FALSE;
	}

	return 0;
}

static int
D3D_RunCommandQueue(SDL_Renderer* renderer, SDL_RenderCommand* cmd, void* vertices, size_t vertsize)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	const int vboidx = data->currentVertexBuffer;
	LPDIRECT3DVERTEXBUFFER8 vbo = NULL;
	const SDL_bool istarget = renderer->target != NULL;
	size_t i;

	if (D3D_ActivateRenderer(renderer) < 0) {
		return -1;
	}

	/* upload the new VBO data for this set of commands. */
	vbo = data->vertexBuffers[vboidx];
	if (!vbo || (data->vertexBufferSize[vboidx] < vertsize)) {
		const DWORD usage = D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY;
		const DWORD fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
		if (vbo) {
			IDirect3DVertexBuffer8_Release(vbo);
		}

		if (FAILED(IDirect3DDevice8_CreateVertexBuffer(data->device, (UINT)vertsize, usage, fvf, D3DPOOL_DEFAULT, &vbo))) {
			vbo = NULL;
		}
		data->vertexBuffers[vboidx] = vbo;
		data->vertexBufferSize[vboidx] = vbo ? vertsize : 0;
	}

	if (vbo) {
		void* ptr;
		if (FAILED(IDirect3DVertexBuffer8_Lock(vbo, 0, (UINT)vertsize, (BYTE**)&ptr, D3DLOCK_NOOVERWRITE))) {
			vbo = NULL;  /* oh well, we'll do immediate mode drawing.  :(  */
		}
		else {
			SDL_memcpy(ptr, vertices, vertsize);
			if (FAILED(IDirect3DVertexBuffer8_Unlock(vbo))) {
				vbo = NULL;  /* oh well, we'll do immediate mode drawing.  :(  */
			}
		}
	}

	/* cycle through a few VBOs so D3D has some time with the data before we replace it. */
	if (vbo) {
		data->currentVertexBuffer++;
		if (data->currentVertexBuffer >= SDL_arraysize(data->vertexBuffers)) {
			data->currentVertexBuffer = 0;
		}
	}
	else if (!data->reportedVboProblem) {
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL failed to get a vertex buffer for this Direct3D 9 rendering batch!");
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dropping back to a slower method.");
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "This might be a brief hiccup, but if performance is bad, this is probably why.");
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "This error will not be logged again for this renderer.");
		data->reportedVboProblem = SDL_TRUE;
	}

	IDirect3DDevice8_SetVertexShader(data->device, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	IDirect3DDevice8_SetStreamSource(data->device, 0, vbo, sizeof(Vertex));

	while (cmd) {
		switch (cmd->command) {
		case SDL_RENDERCMD_SETDRAWCOLOR: {
			/* currently this is sent with each vertex, but if we move to
			   shaders, we can put this in a uniform here and reduce vertex
			   buffer bandwidth */
			break;
		}

		case SDL_RENDERCMD_SETVIEWPORT: {
			SDL_Rect* viewport = &data->drawstate.viewport;
			if (SDL_memcmp(viewport, &cmd->data.viewport.rect, sizeof(SDL_Rect)) != 0) {
				SDL_memcpy(viewport, &cmd->data.viewport.rect, sizeof(SDL_Rect));
				data->drawstate.viewport_dirty = SDL_TRUE;
			}
			break;
		}

		case SDL_RENDERCMD_SETCLIPRECT: {
			const SDL_Rect* rect = &cmd->data.cliprect.rect;
			if (data->drawstate.cliprect_enabled != cmd->data.cliprect.enabled) {
				data->drawstate.cliprect_enabled = cmd->data.cliprect.enabled;
				data->drawstate.cliprect_enabled_dirty = SDL_TRUE;
			}

			if (SDL_memcmp(&data->drawstate.cliprect, rect, sizeof(SDL_Rect)) != 0) {
				SDL_memcpy(&data->drawstate.cliprect, rect, sizeof(SDL_Rect));
				data->drawstate.cliprect_dirty = SDL_TRUE;
			}
			break;
		}

		case SDL_RENDERCMD_CLEAR: {
			const DWORD color = D3DCOLOR_ARGB(cmd->data.color.a, cmd->data.color.r, cmd->data.color.g, cmd->data.color.b);
			const SDL_Rect* viewport = &data->drawstate.viewport;
			const int backw = istarget ? renderer->target->w : data->pparams.BackBufferWidth;
			const int backh = istarget ? renderer->target->h : data->pparams.BackBufferHeight;

			if (data->drawstate.cliprect_enabled) {
#ifndef _XBOX // Not required, the Xbox scissor test we use gets reset on present
				IDirect3DDevice9_SetRenderState(data->device, D3DRS_SCISSORTESTENABLE, FALSE);
#endif
				data->drawstate.cliprect_enabled_dirty = SDL_TRUE;
			}

			/* Don't reset the viewport if we don't have to! */
			if (!viewport->x && !viewport->y && (viewport->w == backw) && (viewport->h == backh)) {
				IDirect3DDevice8_Clear(data->device, 0, NULL, D3DCLEAR_TARGET, color, 0.0f, 0);
			}
			else {
				/* Clear is defined to clear the entire render target */
				const D3DVIEWPORT8 wholeviewport = { 0, 0, backw, backh, 0.0f, 1.0f };
				IDirect3DDevice8_SetViewport(data->device, &wholeviewport);
				data->drawstate.viewport_dirty = SDL_TRUE;
				IDirect3DDevice8_Clear(data->device, 0, NULL, D3DCLEAR_TARGET, color, 0.0f, 0);
			}

			break;
		}

		case SDL_RENDERCMD_DRAW_POINTS: {
			const size_t count = cmd->data.draw.count;
			const size_t first = cmd->data.draw.first;
			SetDrawState(data, cmd);
			if (vbo) {
				IDirect3DDevice8_DrawPrimitive(data->device, D3DPT_POINTLIST, (UINT)(first / sizeof(Vertex)), (UINT)count);
			}
			else {
				const Vertex* verts = (Vertex*)(((Uint8*)vertices) + first);
				IDirect3DDevice8_DrawPrimitiveUP(data->device, D3DPT_POINTLIST, (UINT)count, verts, sizeof(Vertex));
			}
			break;
		}

		case SDL_RENDERCMD_DRAW_LINES: {
			const size_t count = cmd->data.draw.count;
			const size_t first = cmd->data.draw.first;
			const Vertex* verts = (Vertex*)(((Uint8*)vertices) + first);

			/* DirectX 9 has the same line rasterization semantics as GDI,
			   so we need to close the endpoint of the line with a second draw call. */
			const SDL_bool close_endpoint = ((count == 2) || (verts[0].x != verts[count - 1].x) || (verts[0].y != verts[count - 1].y));

			SetDrawState(data, cmd);

			if (vbo) {
				IDirect3DDevice8_DrawPrimitive(data->device, D3DPT_LINESTRIP, (UINT)(first / sizeof(Vertex)), (UINT)(count - 1));
				if (close_endpoint) {
					IDirect3DDevice8_DrawPrimitive(data->device, D3DPT_POINTLIST, (UINT)((first / sizeof(Vertex)) + (count - 1)), 1);
				}
			}
			else {
				IDirect3DDevice8_DrawPrimitiveUP(data->device, D3DPT_LINESTRIP, (UINT)(count - 1), verts, sizeof(Vertex));
				if (close_endpoint) {
					IDirect3DDevice8_DrawPrimitiveUP(data->device, D3DPT_POINTLIST, 1, &verts[count - 1], sizeof(Vertex));
				}
			}
			break;
		}

		case SDL_RENDERCMD_FILL_RECTS: {
			const size_t count = cmd->data.draw.count;
			const size_t first = cmd->data.draw.first;
			SetDrawState(data, cmd);
			if (vbo) {
				size_t offset = 0;
				for (i = 0; i < count; ++i, offset += 4) {
					IDirect3DDevice8_DrawPrimitive(data->device, D3DPT_TRIANGLEFAN, (UINT)((first / sizeof(Vertex)) + offset), 2);
				}
			}
			else {
				const Vertex* verts = (Vertex*)(((Uint8*)vertices) + first);
				for (i = 0; i < count; ++i, verts += 4) {
					IDirect3DDevice8_DrawPrimitiveUP(data->device, D3DPT_TRIANGLEFAN, 2, verts, sizeof(Vertex));
				}
			}
			break;
		}

		case SDL_RENDERCMD_COPY: {
			const size_t count = cmd->data.draw.count;
			const size_t first = cmd->data.draw.first;
			SetDrawState(data, cmd);
			if (vbo) {
				size_t offset = 0;
				for (i = 0; i < count; ++i, offset += 4) {
					IDirect3DDevice8_DrawPrimitive(data->device, D3DPT_TRIANGLEFAN, (UINT)((first / sizeof(Vertex)) + offset), 2);
				}
			}
			else {
				const Vertex* verts = (Vertex*)(((Uint8*)vertices) + first);
				for (i = 0; i < count; ++i, verts += 4) {
					IDirect3DDevice8_DrawPrimitiveUP(data->device, D3DPT_TRIANGLEFAN, 2, verts, sizeof(Vertex));
				}
			}
			break;
		}

		case SDL_RENDERCMD_COPY_EX: {
			const size_t first = cmd->data.draw.first;
			const Vertex* verts = (Vertex*)(((Uint8*)vertices) + first);
			const Vertex* transvert = verts + 4;
			const float translatex = transvert->x;
			const float translatey = transvert->y;
			const float rotation = transvert->z;
			const Float4X4 d3dmatrix = MatrixMultiply(MatrixRotationZ(rotation), MatrixTranslation(translatex, translatey, 0));
			SetDrawState(data, cmd);

			IDirect3DDevice8_SetTransform(data->device, D3DTS_VIEW, (D3DMATRIX*)&d3dmatrix);

			if (vbo) {
				IDirect3DDevice8_DrawPrimitive(data->device, D3DPT_TRIANGLEFAN, (UINT)(first / sizeof(Vertex)), 2);
			}
			else {
				IDirect3DDevice8_DrawPrimitiveUP(data->device, D3DPT_TRIANGLEFAN, 2, verts, sizeof(Vertex));
			}
			break;
		}

		case SDL_RENDERCMD_NO_OP:
			break;
		}

		cmd = cmd->next;
	}

	return 0;
}

static int
D3D_RenderReadPixels(SDL_Renderer* renderer, const SDL_Rect* rect,
	Uint32 format, void* pixels, int pitch)
{
#if 0 // TODO: Not needed urgently
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3DSURFACE_DESC desc;
	LPDIRECT3DSURFACE8 backBuffer;
	LPDIRECT3DSURFACE8 surface;
	RECT d3drect;
	D3DLOCKED_RECT locked;
	HRESULT result;

	if (data->currentRenderTarget) {
		backBuffer = data->currentRenderTarget;
	}
	else {
		backBuffer = data->defaultRenderTarget;
	}

	result = IDirect3DSurface8_GetDesc(backBuffer, &desc);
	if (FAILED(result)) {
		IDirect3DSurface8_Release(backBuffer);
		return D3D_SetError("GetDesc()", result);
	}

	result = IDirect3DDevice8_CreateOffscreenPlainSurface(data->device, desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &surface, NULL);
	if (FAILED(result)) {
		IDirect3DSurface8_Release(backBuffer);
		return D3D_SetError("CreateOffscreenPlainSurface()", result);
	}

	result = IDirect3DDevice8_GetRenderTargetData(data->device, backBuffer, surface);
	if (FAILED(result)) {
		IDirect3DSurface9_Release(surface);
		IDirect3DSurface9_Release(backBuffer);
		return D3D_SetError("GetRenderTargetData()", result);
	}

	d3drect.left = rect->x;
	d3drect.right = rect->x + rect->w;
	d3drect.top = rect->y;
	d3drect.bottom = rect->y + rect->h;

	result = IDirect3DSurface8_LockRect(surface, &locked, &d3drect, D3DLOCK_READONLY);
	if (FAILED(result)) {
		IDirect3DSurface9_Release(surface);
		IDirect3DSurface9_Release(backBuffer);
		return D3D_SetError("LockRect()", result);
	}

	SDL_ConvertPixels(rect->w, rect->h,
		D3DFMTToPixelFormat(desc.Format), locked.pBits, locked.Pitch,
		format, pixels, pitch);

	IDirect3DSurface8_UnlockRect(surface);
	IDirect3DSurface8_Release(surface);

	return 0;
#endif
	return -1;
}

static void
D3D_RenderPresent(SDL_Renderer* renderer)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	HRESULT result;

	if (!data->beginScene) {
		IDirect3DDevice8_EndScene(data->device);
		data->beginScene = SDL_TRUE;
	}

#ifndef _XBOX
	result = IDirect3DDevice8_TestCooperativeLevel(data->device);
	if (result == D3DERR_DEVICELOST) {
		/* We'll reset later */
		return;
	}
	if (result == D3DERR_DEVICENOTRESET) {
		D3D_Reset(renderer);
	}
#endif

	result = IDirect3DDevice8_Present(data->device, NULL, NULL, NULL, NULL);
	if (FAILED(result)) {
		D3D_SetError("Present()", result);
	}
}

static void
D3D_DestroyTexture(SDL_Renderer* renderer, SDL_Texture* texture)
{
	D3D_RenderData* renderdata = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* data = (D3D_TextureData*)texture->driverdata;

	if (renderdata->drawstate.texture == texture) {
		renderdata->drawstate.texture = NULL;
	}

	if (!data) {
		return;
	}

	D3D_DestroyTextureRep(&data->texture);
	D3D_DestroyTextureRep(&data->utexture);
	D3D_DestroyTextureRep(&data->vtexture);
	SDL_free(data->pixels);
	SDL_free(data);
	texture->driverdata = NULL;
}

static void
D3D_DestroyRenderer(SDL_Renderer* renderer)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;

	if (data) {
		int i;

		/* Release the render target */
		if (data->defaultRenderTarget) {
			IDirect3DSurface8_Release(data->defaultRenderTarget);
			data->defaultRenderTarget = NULL;
		}
		if (data->currentRenderTarget != NULL) {
			IDirect3DSurface8_Release(data->currentRenderTarget);
			data->currentRenderTarget = NULL;
		}
#if 0 //TODO: used for YUV textures
		for (i = 0; i < SDL_arraysize(data->shaders); ++i) {
			if (data->shaders[i]) {
				IDirect3DPixelShader9_Release(data->shaders[i]);
				data->shaders[i] = NULL;
			}
		}
#endif
		/* Release all vertex buffers */
		for (i = 0; i < SDL_arraysize(data->vertexBuffers); ++i) {
			if (data->vertexBuffers[i]) {
				IDirect3DVertexBuffer8_Release(data->vertexBuffers[i]);
			}
			data->vertexBuffers[i] = NULL;
		}
		if (data->device) {
			IDirect3DDevice8_Release(data->device);
			data->device = NULL;
		}
		if (data->d3d) {
			IDirect3D8_Release(data->d3d);
#ifndef _XBOX
			SDL_UnloadObject(data->d3dDLL);
#endif
		}
		SDL_free(data);
	}
	SDL_free(renderer);
}

static int
D3D_Reset(SDL_Renderer* renderer)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	const Float4X4 d3dmatrix = MatrixIdentity();
	HRESULT result;
	SDL_Texture* texture;
	int i;

	/* Release the default render target before reset */
	if (data->defaultRenderTarget) {
		IDirect3DSurface8_Release(data->defaultRenderTarget);
		data->defaultRenderTarget = NULL;
	}
	if (data->currentRenderTarget != NULL) {
		IDirect3DSurface8_Release(data->currentRenderTarget);
		data->currentRenderTarget = NULL;
	}

	/* Release application render targets */
	for (texture = renderer->textures; texture; texture = texture->next) {
		if (texture->access == SDL_TEXTUREACCESS_TARGET) {
			D3D_DestroyTexture(renderer, texture);
		}
		else {
			D3D_RecreateTexture(renderer, texture);
		}
	}

	/* Release all vertex buffers */
	for (i = 0; i < SDL_arraysize(data->vertexBuffers); ++i) {
		if (data->vertexBuffers[i]) {
			IDirect3DVertexBuffer8_Release(data->vertexBuffers[i]);
		}
		data->vertexBuffers[i] = NULL;
	}

	result = IDirect3DDevice8_Reset(data->device, &data->pparams);
	if (FAILED(result)) {
		if (result == D3DERR_DEVICELOST) {
			/* Don't worry about it, we'll reset later... */
			return 0;
		}
		else {
			return D3D_SetError("Reset()", result);
		}
	}

	/* Allocate application render targets */
	for (texture = renderer->textures; texture; texture = texture->next) {
		if (texture->access == SDL_TEXTUREACCESS_TARGET) {
			D3D_CreateTexture(renderer, texture);
		}
	}

	IDirect3DDevice8_GetRenderTarget(data->device,/* 0,*/ &data->defaultRenderTarget);
	D3D_InitRenderState(data);
	D3D_SetRenderTargetInternal(renderer, renderer->target);
	data->drawstate.viewport_dirty = SDL_TRUE;
	data->drawstate.cliprect_dirty = SDL_TRUE;
	data->drawstate.cliprect_enabled_dirty = SDL_TRUE;
	data->drawstate.texture = NULL;
	//    data->drawstate.shader = NULL; // TODO: Used for YUV textures
	data->drawstate.blend = SDL_BLENDMODE_INVALID;
	data->drawstate.is_copy_ex = SDL_FALSE;
	IDirect3DDevice8_SetTransform(data->device, D3DTS_VIEW, (D3DMATRIX*)&d3dmatrix);

	/* Let the application know that render targets were reset */
	{
		SDL_Event event;
		event.type = SDL_RENDER_TARGETS_RESET;
		SDL_PushEvent(&event);
	}

	return 0;
}

SDL_Renderer*
D3D_CreateRenderer(SDL_Window* window, Uint32 flags)
{
	SDL_Renderer* renderer;
	D3D_RenderData* data;
	HRESULT result;
	D3DPRESENT_PARAMETERS pparams;
	D3DCAPS8 caps;
#ifndef _XBOX
	IDirect3DSwapChain9* chain;
	SDL_SysWMinfo windowinfo;
	int displayIndex;
	SDL_DisplayMode fullscreen_mode;
	Uint32 window_flags;
#endif
	DWORD device_flags;
	int w, h;
#ifdef _XBOX
	DWORD vidflags;
#endif

	renderer = (SDL_Renderer*)SDL_calloc(1, sizeof(*renderer));
	if (!renderer) {
		SDL_OutOfMemory();
		return NULL;
	}

	data = (D3D_RenderData*)SDL_calloc(1, sizeof(*data));
	if (!data) {
		SDL_free(renderer);
		SDL_OutOfMemory();
		return NULL;
	}

	if (!D3D_LoadDLL(/*&data->d3dDLL,*/ &data->d3d)) {
		SDL_free(renderer);
		SDL_free(data);
		SDL_SetError("Unable to create Direct3D interface");
		return NULL;
	}

	renderer->WindowEvent = D3D_WindowEvent;
	renderer->SupportsBlendMode = D3D_SupportsBlendMode;
	renderer->CreateTexture = D3D_CreateTexture;
	renderer->UpdateTexture = D3D_UpdateTexture;
	renderer->UpdateTextureYUV = D3D_UpdateTextureYUV;
	renderer->LockTexture = D3D_LockTexture;
	renderer->UnlockTexture = D3D_UnlockTexture;
	renderer->SetRenderTarget = D3D_SetRenderTarget;
	renderer->QueueSetViewport = D3D_QueueSetViewport;
	renderer->QueueSetDrawColor = D3D_QueueSetViewport;  /* SetViewport and SetDrawColor are (currently) no-ops. */
	renderer->QueueDrawPoints = D3D_QueueDrawPoints;
	renderer->QueueDrawLines = D3D_QueueDrawPoints;  /* lines and points queue vertices the same way. */
	renderer->QueueFillRects = D3D_QueueFillRects;
	renderer->QueueCopy = D3D_QueueCopy;
	renderer->QueueCopyEx = D3D_QueueCopyEx;
	renderer->RunCommandQueue = D3D_RunCommandQueue;
	renderer->RenderReadPixels = D3D_RenderReadPixels;
	renderer->RenderPresent = D3D_RenderPresent;
	renderer->DestroyTexture = D3D_DestroyTexture;
	renderer->DestroyRenderer = D3D_DestroyRenderer;
	renderer->info = D3D_RenderDriver.info;
	renderer->info.flags = (SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
	renderer->driverdata = data;
#ifndef _XBOX
	SDL_VERSION(&windowinfo.version);
	SDL_GetWindowWMInfo(window, &windowinfo);
	window_flags = SDL_GetWindowFlags(window);
	SDL_GetWindowDisplayMode(window, &fullscreen_mode);
#endif
	SDL_GetWindowSize(window, &w, &h);

	SDL_zero(pparams);
	pparams.BackBufferWidth = w;
	pparams.BackBufferHeight = h;
	pparams.BackBufferCount = 1;
	pparams.SwapEffect = D3DSWAPEFFECT_DISCARD;
#ifndef _XBOX
	pparams.hDeviceWindow = windowinfo.info.win.window;

	if (window_flags & SDL_WINDOW_FULLSCREEN && (window_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != SDL_WINDOW_FULLSCREEN_DESKTOP) {
		pparams.Windowed = FALSE;
		pparams.BackBufferFormat = PixelFormatToD3DFMT(fullscreen_mode.format);
		pparams.FullScreen_RefreshRateInHz = fullscreen_mode.refresh_rate;
	}
	else {
		pparams.Windowed = TRUE;
		pparams.BackBufferFormat = D3DFMT_UNKNOWN;
		pparams.FullScreen_RefreshRateInHz = 0;
	}
	if (flags & SDL_RENDERER_PRESENTVSYNC) {
		pparams.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
	}
	else {
		pparams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	}

	/* Get the adapter for the display that the window is on */
	displayIndex = SDL_GetWindowDisplayIndex(window);
	data->adapter = SDL_Direct3D9GetAdapterIndex(displayIndex);

	IDirect3D9_GetDeviceCaps(data->d3d, data->adapter, D3DDEVTYPE_HAL, &caps);

	device_flags = D3DCREATE_FPU_PRESERVE;
	if (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) {
		device_flags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
	}
	else {
		device_flags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
	}

	if (SDL_GetHintBoolean(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, SDL_FALSE)) {
		device_flags |= D3DCREATE_MULTITHREADED;
	}

#else //_XBOX
	device_flags = D3DCREATE_HARDWARE_VERTEXPROCESSING;

	pparams.EnableAutoDepthStencil = TRUE;
	pparams.AutoDepthStencilFormat = D3DFMT_D16;
	pparams.hDeviceWindow = NULL;
	pparams.Windowed = FALSE;
	pparams.BackBufferFormat = D3DFMT_LIN_X8R8G8B8;
	pparams.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;

	vidflags = XGetVideoFlags();

	if (pparams.BackBufferWidth > 640 && ((float)pparams.BackBufferHeight / (float)pparams.BackBufferWidth != 0.75) ||
		((pparams.BackBufferWidth == 720) && (pparams.BackBufferHeight == 576))) // Widescreen
	{
		pparams.Flags |= D3DPRESENTFLAG_WIDESCREEN;
	}

	if (XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I) // PAL user
	{
		if (vidflags & XC_VIDEO_FLAGS_PAL_60Hz) // PAL 60 user
			pparams.FullScreen_RefreshRateInHz = 60;
		else
			pparams.FullScreen_RefreshRateInHz = 50;
	}

	if (vidflags & XC_VIDEO_FLAGS_HDTV_480p)
		pparams.Flags |= D3DPRESENTFLAG_PROGRESSIVE;
	else
		pparams.Flags |= D3DPRESENTFLAG_INTERLACED;
#endif

	result = IDirect3D8_CreateDevice(data->d3d, 0,
		D3DDEVTYPE_HAL,
		NULL/*pparams.hDeviceWindow*/,
		device_flags,
		&pparams, &data->device);

	if (FAILED(result)) {
		D3D_DestroyRenderer(renderer);
		D3D_SetError("CreateDevice()", result);
		return NULL;
	}

#ifndef _XBOX
	/* Get presentation parameters to fill info */
	result = IDirect3DDevice9_GetSwapChain(data->device, 0, &chain);
	if (FAILED(result)) {
		D3D_DestroyRenderer(renderer);
		D3D_SetError("GetSwapChain()", result);
		return NULL;
	}
	result = IDirect3DSwapChain9_GetPresentParameters(chain, &pparams);
	if (FAILED(result)) {
		IDirect3DSwapChain9_Release(chain);
		D3D_DestroyRenderer(renderer);
		D3D_SetError("GetPresentParameters()", result);
		return NULL;
	}
	IDirect3DSwapChain9_Release(chain);
	if (pparams.PresentationInterval == D3DPRESENT_INTERVAL_ONE) {
		renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
	}
#endif
	data->pparams = pparams;

	IDirect3DDevice8_GetDeviceCaps(data->device, &caps);
	renderer->info.max_texture_width = caps.MaxTextureWidth;
	renderer->info.max_texture_height = caps.MaxTextureHeight;

#ifndef _XBOX // TODO: enableSeparateAlphaBlend = FALSE by default
	if (caps.NumSimultaneousRTs >= 2) {
		renderer->info.flags |= SDL_RENDERER_TARGETTEXTURE;
	}
	if (caps.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND) {
		data->enableSeparateAlphaBlend = SDL_TRUE;
	}
#endif

	/* Store the default render target */
	IDirect3DDevice8_GetRenderTarget(data->device,/* 0,*/ &data->defaultRenderTarget);
	data->currentRenderTarget = NULL;

	/* Set up parameters for rendering */
	D3D_InitRenderState(data);

#if 0 // TODO: Shader for YUV textures
	if (caps.MaxSimultaneousTextures >= 3) {
		int i;
		for (i = 0; i < SDL_arraysize(data->shaders); ++i) {
			result = D3D9_CreatePixelShader(data->device, (D3D9_Shader)i, &data->shaders[i]);
			if (FAILED(result)) {
				D3D_SetError("CreatePixelShader()", result);
			}
		}
		if (data->shaders[SHADER_YUV_JPEG] && data->shaders[SHADER_YUV_BT601] && data->shaders[SHADER_YUV_BT709]) {
			renderer->info.texture_formats[renderer->info.num_texture_formats++] = SDL_PIXELFORMAT_YV12;
			renderer->info.texture_formats[renderer->info.num_texture_formats++] = SDL_PIXELFORMAT_IYUV;
		}
	}
#endif

	data->drawstate.blend = SDL_BLENDMODE_INVALID;

	return renderer;
}

SDL_RenderDriver D3D_RenderDriver = {
	D3D_CreateRenderer,
	{
	 "direct3d",
	 (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE),
	 1,
	 {SDL_PIXELFORMAT_ARGB8888},
	 0,
	 0}
};
#endif /* SDL_VIDEO_RENDER_D3D && !SDL_RENDER_DISABLED */

#ifdef __XBOX__
/* This function needs to always exist on Windows, for the Dynamic API. */
IDirect3DDevice8*
SDL_RenderGetD3D9Device(SDL_Renderer* renderer)
{
	IDirect3DDevice8* device = NULL;

#if SDL_VIDEO_RENDER_D3D && !SDL_RENDER_DISABLED
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;

	/* Make sure that this is a D3D renderer */
	if (renderer->DestroyRenderer != D3D_DestroyRenderer) {
		SDL_SetError("Renderer is not a D3D renderer");
		return NULL;
	}

	device = data->device;
	if (device) {
		IDirect3DDevice8_AddRef(device);
	}
#endif /* SDL_VIDEO_RENDER_D3D && !SDL_RENDER_DISABLED */

	return device;
}
#endif /* __XBOX__ */

/* vi: set ts=4 sw=4 expandtab: */