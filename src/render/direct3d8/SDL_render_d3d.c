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

	/* Fixed-function vertex format: XYZ + diffuse + 1 set of texcoords */
	IDirect3DDevice8_SetVertexShader(device, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	/* Disable depth, cull, lighting; typical 2D overlay setup */
	IDirect3DDevice8_SetRenderState(device, D3DRS_ZENABLE, D3DZB_FALSE);
	IDirect3DDevice8_SetRenderState(device, D3DRS_ZWRITEENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
	IDirect3DDevice8_SetRenderState(device, D3DRS_LIGHTING, FALSE);

	/* Proper per-pixel alpha blending (needed for SDL_ttf glyphs) */
	IDirect3DDevice8_SetRenderState(device, D3DRS_ALPHABLENDENABLE, TRUE);
	IDirect3DDevice8_SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	IDirect3DDevice8_SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	IDirect3DDevice8_SetRenderState(device, D3DRS_ALPHATESTENABLE, FALSE);

	/* Stage 0: modulate texture with vertex color (and alpha) */
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	IDirect3DDevice8_SetTextureStageState(device, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	/* Stage 1 off */
	IDirect3DDevice8_SetTextureStageState(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	IDirect3DDevice8_SetTextureStageState(device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	/* Identity world + view (we draw in screen space with a 0.5f bias in vertices) */
	SDL_zero(matrix);
	matrix.m[0][0] = 1.0f;
	matrix.m[1][1] = 1.0f;
	matrix.m[2][2] = 1.0f;
	matrix.m[3][3] = 1.0f;
	IDirect3DDevice8_SetTransform(device, D3DTS_WORLD, &matrix);
	IDirect3DDevice8_SetTransform(device, D3DTS_VIEW, &matrix);

	/* Reset cached scale modes (actual filter/address is set per texture) */
	SDL_memset(data->scaleMode, 0xFF, sizeof(data->scaleMode));

	/* Begin the first scene on demand */
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

	/* Mark device params dirty on any size-change signal */
	if (event->event == SDL_WINDOWEVENT_SIZE_CHANGED ||
		event->event == SDL_WINDOWEVENT_RESIZED) {
		data->updateSize = SDL_TRUE;
	}
}

static D3DBLEND
GetBlendFunc(SDL_BlendFactor factor)
{
	switch (factor) {
	case SDL_BLENDFACTOR_ZERO:                  return D3DBLEND_ZERO;
	case SDL_BLENDFACTOR_ONE:                   return D3DBLEND_ONE;
	case SDL_BLENDFACTOR_SRC_COLOR:             return D3DBLEND_SRCCOLOR;
	case SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR:   return D3DBLEND_INVSRCCOLOR;
	case SDL_BLENDFACTOR_SRC_ALPHA:             return D3DBLEND_SRCALPHA;
	case SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:   return D3DBLEND_INVSRCALPHA;
	case SDL_BLENDFACTOR_DST_COLOR:             return D3DBLEND_DESTCOLOR;
	case SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR:   return D3DBLEND_INVDESTCOLOR;
	case SDL_BLENDFACTOR_DST_ALPHA:             return D3DBLEND_DESTALPHA;
	case SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA:   return D3DBLEND_INVDESTALPHA;
	default:
		/* Fall back to a benign value instead of 0 (undefined) */
		return D3DBLEND_ONE;
	}
}


static SDL_bool
D3D_SupportsBlendMode(SDL_Renderer* renderer, SDL_BlendMode blendMode)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;

	/* Query SDL's requested blend components */
	SDL_BlendFactor srcColorFactor = SDL_GetBlendModeSrcColorFactor(blendMode);
	SDL_BlendFactor srcAlphaFactor = SDL_GetBlendModeSrcAlphaFactor(blendMode);
	SDL_BlendFactor dstColorFactor = SDL_GetBlendModeDstColorFactor(blendMode);
	SDL_BlendFactor dstAlphaFactor = SDL_GetBlendModeDstAlphaFactor(blendMode);
	SDL_BlendOperation colorOperation = SDL_GetBlendModeColorOperation(blendMode);
	SDL_BlendOperation alphaOperation = SDL_GetBlendModeAlphaOperation(blendMode);

	/* D3D8 supports only ADD */
	if (colorOperation != SDL_BLENDOPERATION_ADD || alphaOperation != SDL_BLENDOPERATION_ADD) {
		return SDL_FALSE;
	}

	/* Helper lambda-equivalent for OG C: inline check macro */
#define FACTOR_SUPPORTED(f) \
        ((f) == SDL_BLENDFACTOR_ZERO || \
         (f) == SDL_BLENDFACTOR_ONE  || \
         (f) == SDL_BLENDFACTOR_SRC_COLOR || \
         (f) == SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR || \
         (f) == SDL_BLENDFACTOR_SRC_ALPHA || \
         (f) == SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA || \
         (f) == SDL_BLENDFACTOR_DST_COLOR || \
         (f) == SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR || \
         (f) == SDL_BLENDFACTOR_DST_ALPHA || \
         (f) == SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA)

	/* All four factors must be representable in D3D8 */
	if (!FACTOR_SUPPORTED(srcColorFactor) ||
		!FACTOR_SUPPORTED(srcAlphaFactor) ||
		!FACTOR_SUPPORTED(dstColorFactor) ||
		!FACTOR_SUPPORTED(dstAlphaFactor)) {
		return SDL_FALSE;
	}

	/* D3D8 lacks separate alpha blend state; require matching src/dst factors */
	if ((srcColorFactor != srcAlphaFactor || dstColorFactor != dstAlphaFactor) &&
		!data->enableSeparateAlphaBlend) {
		return SDL_FALSE;
	}

	return SDL_TRUE;
}


static int
D3D_CreateTextureRep(IDirect3DDevice8* device, D3D_TextureRep* texture,
	DWORD usage, Uint32 format, D3DFORMAT d3dfmt, int w, int h)
{
	HRESULT result;

	if (w <= 0) w = 1;
	if (h <= 0) h = 1;

	texture->dirty = SDL_FALSE;
	texture->w = w;
	texture->h = h;
	texture->usage = usage;
	texture->format = format;
	texture->d3dfmt = d3dfmt;

	/* Use the caller-provided d3dfmt (do NOT recompute from 'format' here). */
	result = IDirect3DDevice8_CreateTexture(device,
		w, h,
		1,                 /* levels */
		usage,             /* e.g. 0 or D3DUSAGE_RENDERTARGET/DYNAMIC */
		d3dfmt,            /* <-- respect caller-chosen format */
		D3DPOOL_DEFAULT,   /* your reset path recreates these, OK */
		&texture->texture);

	if (FAILED(result)) {
		/* Optional fallback for alpha-capable streams (uncomment if helpful)
		if (d3dfmt != D3DFMT_A8R8G8B8) {
			result = IDirect3DDevice8_CreateTexture(device, w, h, 1, usage,
													D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
													&texture->texture);
		}
		if (FAILED(result)) { */
		char msg[128];
		_snprintf(msg, sizeof(msg), "CreateTexture %dx%d fmt=0x%08x (DEFAULT)",
			w, h, (unsigned int)d3dfmt);
		msg[sizeof(msg) - 1] = '\0';
		return D3D_SetError(msg, result);
		/* } */
	}

	return 0;
}


static int
D3D_CreateStagingTexture(IDirect3DDevice8* device, D3D_TextureRep* texture)
{
	HRESULT result;
	int w = texture->w;
	int h = texture->h;
	D3DFORMAT fmt = texture->d3dfmt;

	/* Guard against bad sizes */
	if (w <= 0) w = 1;
	if (h <= 0) h = 1;

	/* If an existing staging texture is the wrong size/format, recreate it */
	if (texture->staging) {
		D3DSURFACE_DESC desc;
		IDirect3DTexture8_GetLevelDesc(texture->staging, 0, &desc);
		if ((int)desc.Width != w || (int)desc.Height != h || desc.Format != fmt) {
			IDirect3DTexture8_Release(texture->staging);
			texture->staging = NULL;
		}
	}

	if (texture->staging == NULL) {
		/* Staging is CPU-side (SYSTEMMEM), 1 mip level, usage 0 */
		result = IDirect3DDevice8_CreateTexture(device,
			w, h,
			1,                  /* levels */
			0,                  /* usage */
			fmt,                /* must match default tex for UpdateTexture */
			D3DPOOL_SYSTEMMEM,
			&texture->staging);
		if (FAILED(result)) {
			char msg[128];
			_snprintf(msg, sizeof(msg), "CreateTexture SYSTEMMEM %dx%d fmt=0x%08x",
				w, h, (unsigned int)fmt);
			msg[sizeof(msg) - 1] = '\0';
			return D3D_SetError(msg, result);
		}
	}

	return 0;
}



static int
D3D_RecreateTextureRep(IDirect3DDevice8* device, D3D_TextureRep* texture)
{
	/* Drop the default-pool GPU texture; it will be recreated by caller later. */
	if (texture->texture) {
		IDirect3DTexture8_Release(texture->texture);
		texture->texture = NULL;
	}

	/* For the staging (SYSTEMMEM) copy:
	   - If size/format no longer match what we want, release it so
		 D3D_CreateStagingTexture() will rebuild a correct one next time.
	   - Otherwise keep it (and on non-Xbox, mark dirty to force reupload). */
	if (texture->staging) {
		D3DSURFACE_DESC desc;
		IDirect3DTexture8_GetLevelDesc(texture->staging, 0, &desc);

		if ((int)desc.Width != texture->w ||
			(int)desc.Height != texture->h ||
			desc.Format != texture->d3dfmt) {
			/* Staging no longer matches; drop it so it will be recreated. */
			IDirect3DTexture8_Release(texture->staging);
			texture->staging = NULL;
		}
		else {
#ifndef _XBOX
			/* We are using our own UpdateTexture(), but keeping this here is harmless
			   when the staging texture is retained. */
			IDirect3DTexture8_AddDirtyRect(texture->staging, NULL);
#endif
		}
	}

	/* Force the next upload to refresh the GPU texture */
	texture->dirty = SDL_TRUE;
	return 0;
}


static int
D3D_UpdateTextureRep(IDirect3DDevice8* device, D3D_TextureRep* texture,
	int x, int y, int w, int h,
	const void* pixels, int pitch)
{
	RECT d3drect;
	D3DLOCKED_RECT locked;
	const Uint8* src;
	Uint8* dst;
	int row, length;
	HRESULT result;
	int texw = texture->w;
	int texh = texture->h;
	int bpp = SDL_BYTESPERPIXEL(texture->format);

	/* Nothing to do or invalid input */
	if (!pixels || w <= 0 || h <= 0 || bpp <= 0) {
		return 0;
	}

	/* Ensure staging texture exists and matches size/format */
	if (D3D_CreateStagingTexture(device, texture) < 0) {
		return -1;
	}

	/* Clip upload rectangle to texture bounds */
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > texw) w = texw - x;
	if (y + h > texh) h = texh - y;
	if (w <= 0 || h <= 0) {
		return 0; /* fully clipped, treat as success */
	}

	d3drect.left = x;
	d3drect.top = y;
	d3drect.right = x + w;
	d3drect.bottom = y + h;

	result = IDirect3DTexture8_LockRect(texture->staging, 0, &locked, &d3drect, 0);
	if (FAILED(result)) {
		return D3D_SetError("LockRect()", result);
	}

	src = (const Uint8*)pixels;
	dst = (Uint8*)locked.pBits;

	/* Number of bytes per row we actually copy */
	/* Start with width*bpp, clamp to both source and dest pitch */
	length = w * bpp;
	if (pitch > 0 && length > pitch)        length = pitch;
	if (locked.Pitch > 0 && length > (int)locked.Pitch) length = (int)locked.Pitch;

	if (length <= 0) {
		/* Avoid divide-by-zero or negative copies */
		IDirect3DTexture8_UnlockRect(texture->staging, 0);
		return 0;
	}

	if (pitch == (int)locked.Pitch && length == w * bpp) {
		/* Tight copy � single memcpy */
		SDL_memcpy(dst, src, (size_t)length * (size_t)h);
	}
	else {
		/* Row-by-row copy, respecting both pitches */
		for (row = 0; row < h; ++row) {
			SDL_memcpy(dst, src, (size_t)length);
			src += pitch;
			dst += locked.Pitch;
		}
	}

	result = IDirect3DTexture8_UnlockRect(texture->staging, 0);
	if (FAILED(result)) {
		return D3D_SetError("UnlockRect()", result);
	}

	/* Mark default-pool texture as needing an upload */
	texture->dirty = SDL_TRUE;
	return 0;
}


static void
D3D_DestroyTextureRep(D3D_TextureRep* texture)
{
	if (!texture) {
		return;
	}

	if (texture->texture) {
		IDirect3DTexture8_Release(texture->texture);
		texture->texture = NULL;
	}

	if (texture->staging) {
		IDirect3DTexture8_Release(texture->staging);
		texture->staging = NULL;
	}

	/* No pending uploads after destruction */
	texture->dirty = SDL_FALSE;
}


static int
D3D_CreateTexture(SDL_Renderer* renderer, SDL_Texture* texture)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* texturedata;
	DWORD usage;
	D3DFORMAT d3dfmt;
	int w = texture->w;
	int h = texture->h;

	/* Allocate driver data */
	texturedata = (D3D_TextureData*)SDL_calloc(1, sizeof(*texturedata));
	if (!texturedata) {
		return SDL_OutOfMemory();
	}

	/* Map SDL scale mode to D3D8 filter; default to LINEAR unless explicitly Nearest */
	texturedata->scaleMode = (texture->scaleMode == SDL_ScaleModeNearest) ? D3DTEXF_POINT : D3DTEXF_LINEAR;
	texture->driverdata = texturedata;

	/* Resolve D3D format once from SDL pixel format */
	d3dfmt = PixelFormatToD3DFMT(texture->format);
	if (d3dfmt == (D3DFORMAT)0 || w <= 0 || h <= 0) {
		/* Defensive error path with context */
		char msg[128];
		_snprintf(msg, sizeof(msg), "CreateTexture invalid fmt=0x%08x %dx%d", (unsigned int)texture->format, w, h);
		msg[sizeof(msg) - 1] = '\0';
		SDL_free(texturedata);
		texture->driverdata = NULL;
		return D3D_SetError(msg, D3DERR_INVALIDCALL);
	}

	/* Usage (render target or not) */
	usage = (texture->access == SDL_TEXTUREACCESS_TARGET) ? D3DUSAGE_RENDERTARGET : 0;

	/* Create main texture rep */
	if (D3D_CreateTextureRep(data->device, &texturedata->texture, usage,
		texture->format, d3dfmt, w, h) < 0) {
		SDL_free(texturedata);
		texture->driverdata = NULL;
		return -1;
	}

	/* Planar YUV textures: create U and V planes at half res.
	   NOTE: We keep using the same d3dfmt resolver for consistency with your pipeline. */
	if (texture->format == SDL_PIXELFORMAT_YV12 ||
		texture->format == SDL_PIXELFORMAT_IYUV) {
		int hw = (w + 1) / 2;
		int hh = (h + 1) / 2;

		texturedata->yuv = SDL_TRUE;

		if (D3D_CreateTextureRep(data->device, &texturedata->utexture, usage,
			texture->format, d3dfmt, hw, hh) < 0) {
			D3D_DestroyTextureRep(&texturedata->texture);
			SDL_free(texturedata);
			texture->driverdata = NULL;
			return -1;
		}

		if (D3D_CreateTextureRep(data->device, &texturedata->vtexture, usage,
			texture->format, d3dfmt, hw, hh) < 0) {
			D3D_DestroyTextureRep(&texturedata->utexture);
			D3D_DestroyTextureRep(&texturedata->texture);
			SDL_free(texturedata);
			texture->driverdata = NULL;
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

	/* Main texture: drop and recreate default-pool resource */
	if (D3D_RecreateTextureRep(data->device, &texturedata->texture) < 0) {
		return -1;
	}
	if (D3D_CreateTextureRep(data->device,
		&texturedata->texture,
		texturedata->texture.usage,
		texturedata->texture.format,
		texturedata->texture.d3dfmt,
		texturedata->texture.w,
		texturedata->texture.h) < 0) {
		return -1;
	}
	/* Mark dirty so next upload (if any) will refresh contents */
	texturedata->texture.dirty = SDL_TRUE;

	if (texturedata->yuv) {
		/* U plane */
		if (D3D_RecreateTextureRep(data->device, &texturedata->utexture) < 0) {
			return -1;
		}
		if (D3D_CreateTextureRep(data->device,
			&texturedata->utexture,
			texturedata->utexture.usage,
			texturedata->utexture.format,
			texturedata->utexture.d3dfmt,
			texturedata->utexture.w,
			texturedata->utexture.h) < 0) {
			return -1;
		}
		texturedata->utexture.dirty = SDL_TRUE;

		/* V plane */
		if (D3D_RecreateTextureRep(data->device, &texturedata->vtexture) < 0) {
			return -1;
		}
		if (D3D_CreateTextureRep(data->device,
			&texturedata->vtexture,
			texturedata->vtexture.usage,
			texturedata->vtexture.format,
			texturedata->vtexture.d3dfmt,
			texturedata->vtexture.w,
			texturedata->vtexture.h) < 0) {
			return -1;
		}
		texturedata->vtexture.dirty = SDL_TRUE;
	}

	return 0;
}


static int
D3D_UpdateTexture(SDL_Renderer* renderer, SDL_Texture* texture,
	const SDL_Rect* rect, const void* pixels, int pitch)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;

	SDL_Rect r;              /* working rect (clipped) */
	int texw, texh;

	if (!texturedata) {
		SDL_SetError("Texture is not currently available");
		return -1;
	}
	if (!pixels) {
		/* Nothing to upload; treat as success */
		return 0;
	}

	texw = texture->w;
	texh = texture->h;

	/* If caller passes NULL rect, update the whole texture */
	if (rect == NULL) {
		r.x = 0; r.y = 0; r.w = texw; r.h = texh;
	}
	else {
		r = *rect;
	}

	/* Clip update rectangle to texture bounds */
	if (r.x < 0) { r.w += r.x; r.x = 0; }
	if (r.y < 0) { r.h += r.y; r.y = 0; }
	if (r.x + r.w > texw) r.w = texw - r.x;
	if (r.y + r.h > texh) r.h = texh - r.y;

	if (r.w <= 0 || r.h <= 0) {
		return 0; /* fully clipped, nothing to do */
	}

	/* ---- Packed/RGBA path ---- */
	if (!texturedata->yuv) {
		/* Upload the (possibly clipped) rect to the default Y/RGBA plane */
		if (D3D_UpdateTextureRep(data->device, &texturedata->texture,
			r.x, r.y, r.w, r.h,
			pixels, pitch) < 0) {
			return -1;
		}
		return 0;
	}

	/* ---- Planar YUV420 path (YV12/IYUV) ----
	   Layout in memory provided by SDL:
		 - Y plane: pitch bytes per row, height rows
		 - then U and V planes at half resolution:
			 IYUV: Y, U, V
			 YV12: Y, V, U
	   We mirror your original logic but add guards.
	*/
	{
		const Uint8* base = (const Uint8*)pixels;

		/* Advance to start of chroma-U/V source data (after Y plane of 'r.h' rows). */
		const Uint8* src_after_Y = base + (size_t)r.h * (size_t)pitch;

		/* Dimensions/pitch for chroma planes */
		const int chroma_w = (r.w + 1) / 2;
		const int chroma_h = (r.h + 1) / 2;
		const int chroma_pitch = (pitch + 1) / 2;

		D3D_TextureRep* first_chroma =
			(texture->format == SDL_PIXELFORMAT_YV12) ? &texturedata->vtexture
			: &texturedata->utexture;
		D3D_TextureRep* second_chroma =
			(texture->format == SDL_PIXELFORMAT_YV12) ? &texturedata->utexture
			: &texturedata->vtexture;

		/* Upload Y plane (packed path above already handled non-YUV) */
		if (D3D_UpdateTextureRep(data->device, &texturedata->texture,
			r.x, r.y, r.w, r.h,
			base, pitch) < 0) {
			return -1;
		}

		/* Upload first chroma plane at half res */
		if (chroma_w > 0 && chroma_h > 0) {
			if (D3D_UpdateTextureRep(data->device, first_chroma,
				r.x / 2, r.y / 2,
				chroma_w, chroma_h,
				src_after_Y, chroma_pitch) < 0) {
				return -1;
			}
		}

		/* Advance pointer past the first chroma plane rows we just uploaded */
		{
			const Uint8* src_second = src_after_Y + (size_t)chroma_h * (size_t)chroma_pitch;

			/* Upload second chroma plane */
			if (chroma_w > 0 && chroma_h > 0) {
				if (D3D_UpdateTextureRep(data->device, second_chroma,
					r.x / 2, (r.y + 1) / 2,
					chroma_w, chroma_h,
					src_second, chroma_pitch) < 0) {
					return -1;
				}
			}
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

	SDL_Rect r;   /* working, clipped rect */
	int texw, texh;

	if (!texturedata) {
		SDL_SetError("Texture is not currently available");
		return -1;
	}
	if (!Yplane || !Uplane || !Vplane) {
		/* missing plane(s) � nothing to upload */
		return 0;
	}

	texw = texture->w;
	texh = texture->h;

	/* NULL rect => full texture */
	if (rect == NULL) {
		r.x = 0; r.y = 0; r.w = texw; r.h = texh;
	}
	else {
		r = *rect;
	}

	/* Clip to bounds */
	if (r.x < 0) { r.w += r.x; r.x = 0; }
	if (r.y < 0) { r.h += r.y; r.y = 0; }
	if (r.x + r.w > texw) r.w = texw - r.x;
	if (r.y + r.h > texh) r.h = texh - r.y;

	if (r.w <= 0 || r.h <= 0) {
		return 0; /* fully clipped */
	}

	/* Upload Y plane (full res) */
	if (D3D_UpdateTextureRep(data->device, &texturedata->texture,
		r.x, r.y, r.w, r.h,
		Yplane, Ypitch) < 0) {
		return -1;
	}

	/* Chroma planes (4:2:0) � half resolution, rounded up */
	{
		const int cx = r.x / 2;
		const int cy = r.y / 2;
		const int cw = (r.w + 1) / 2;
		const int ch = (r.h + 1) / 2;

		if (cw > 0 && ch > 0) {
			if (D3D_UpdateTextureRep(data->device, &texturedata->utexture,
				cx, cy, cw, ch,
				Uplane, Upitch) < 0) {
				return -1;
			}
			if (D3D_UpdateTextureRep(data->device, &texturedata->vtexture,
				cx, cy, cw, ch,
				Vplane, Vpitch) < 0) {
				return -1;
			}
		}
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

	SDL_Rect r;  /* working rect (clipped) */
	int texw, texh;

	if (!pixels || !pitch) {
		SDL_SetError("Invalid output pointers");
		return -1;
	}
	*pixels = NULL;
	*pitch = 0;

	if (!texturedata) {
		SDL_SetError("Texture is not currently available");
		return -1;
	}

	texw = texture->w;
	texh = texture->h;

	/* Allow NULL rect => lock full texture */
	if (rect == NULL) {
		r.x = 0; r.y = 0; r.w = texw; r.h = texh;
	}
	else {
		r = *rect;
	}

	/* Clip to bounds (avoid invalid LockRect rectangles) */
	if (r.x < 0) { r.w += r.x; r.x = 0; }
	if (r.y < 0) { r.h += r.y; r.y = 0; }
	if (r.x + r.w > texw) r.w = texw - r.x;
	if (r.y + r.h > texh) r.h = texh - r.y;

	if (r.w <= 0 || r.h <= 0) {
		/* Nothing to lock; treat as success */
		texturedata->locked_rect = r;
		return 0;
	}

	texturedata->locked_rect = r;

	if (texturedata->yuv) {
		/* Planar 4:2:0 lock: we give caller a CPU buffer for the Y plane region.
		   They will later call UpdateTexture(YUV) to push to GPU. */

		   /* Ensure a suitably-sized CPU buffer exists; pitch = full texture width. */
		const int y_pitch = texw;
		const size_t needed = (size_t)texh * (size_t)y_pitch * 3 / 2; /* Y + U + V */
		if (!texturedata->pixels || texturedata->pitch != y_pitch) {
			if (texturedata->pixels) {
				SDL_free(texturedata->pixels);
				texturedata->pixels = NULL;
			}
			texturedata->pixels = (Uint8*)SDL_malloc(needed ? needed : 1);
			if (!texturedata->pixels) {
				texturedata->pitch = 0;
				return SDL_OutOfMemory();
			}
			texturedata->pitch = y_pitch;
		}

		/* Return pointer/pitch for the requested Y subrect */
		/* For planar formats, Y is 1 byte per pixel, so offset = y*pitch + x. */
		*pixels = (void*)((Uint8*)texturedata->pixels + (size_t)r.y * (size_t)texturedata->pitch + (size_t)r.x);
		*pitch = texturedata->pitch;
		return 0;
	}
	else {
		/* Packed/RGBA path: lock the staging texture subrect and hand back the pointer */
		RECT d3drect;
		D3DLOCKED_RECT locked;
		HRESULT hr;

		if (D3D_CreateStagingTexture(device, &texturedata->texture) < 0) {
			return -1;
		}

		d3drect.left = r.x;
		d3drect.top = r.y;
		d3drect.right = r.x + r.w;
		d3drect.bottom = r.y + r.h;

		/* No flags: we want to preserve content outside the locked rectangle */
		hr = IDirect3DTexture8_LockRect(texturedata->texture.staging, 0, &locked, &d3drect, 0);
		if (FAILED(hr)) {
			return D3D_SetError("LockRect()", hr);
		}

		*pixels = locked.pBits;
		*pitch = (int)locked.Pitch;
		return 0;
	}
}


static void
D3D_UnlockTexture(SDL_Renderer* renderer, SDL_Texture* texture)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;

	if (!texturedata) {
		return;
	}

	/* Ensure locked_rect is sane; if caller passed NULL to Lock, we stored full size */
	{
		SDL_Rect r = texturedata->locked_rect;
		int texw = texture->w;
		int texh = texture->h;

		if (r.w <= 0 || r.h <= 0) {
			return; /* nothing was actually locked */
		}
		if (r.x < 0) { r.w += r.x; r.x = 0; }
		if (r.y < 0) { r.h += r.y; r.y = 0; }
		if (r.x + r.w > texw) r.w = texw - r.x;
		if (r.y + r.h > texh) r.h = texh - r.y;
		if (r.w <= 0 || r.h <= 0) {
			return;
		}
		texturedata->locked_rect = r; /* keep clipped rect */
	}

	if (texturedata->yuv) {
		/* Caller wrote into a compact YUV buffer that starts at the Y subrect
		   (Y rows first, then U subrect rows, then V subrect rows),
		   with Y pitch == full texture width. That�s the layout that
		   D3D_UpdateTexture(...) expects for YUV in your codepath. */

		const SDL_Rect* rect = &texturedata->locked_rect;
		void* pixels = NULL;
		int pitch = 0;

		if (!texturedata->pixels || texturedata->pitch <= 0) {
			/* Nothing to upload */
			return;
		}

		/* Base pointer to the Y subrect start within our CPU buffer */
		pixels = (void*)((Uint8*)texturedata->pixels +
			(size_t)rect->y * (size_t)texturedata->pitch +
			(size_t)rect->x /* 1 byte per Y sample */);

		pitch = texturedata->pitch;

		/* Push the compact rect to GPU (this will also upload U/V from the same
		   compact buffer using the offsets inside D3D_UpdateTexture) */
		D3D_UpdateTexture(renderer, texture, rect, pixels, pitch);
		return;
	}
	else {
		/* Packed / RGBA path: unlock staging if it was locked in LockTexture. */
		/* We can�t query lock state on D3D8; assume we locked if staging exists. */
		if (texturedata->texture.staging) {
			IDirect3DTexture8_UnlockRect(texturedata->texture.staging, 0);
			texturedata->texture.dirty = SDL_TRUE;

			/* If this texture was bound, force rebind next draw to avoid
			   sampling stale contents while dirty. */
			if (data->drawstate.texture == texture) {
				data->drawstate.texture = NULL;
			}
		}
		return;
	}
}

static int D3D8_UpdateTexture(IDirect3DTexture8* srcTexture, IDirect3DTexture8* dstTexture)
{
	LPDIRECT3DSURFACE8 srcSurface = NULL;
	LPDIRECT3DSURFACE8 dstSurface = NULL;
	IDirect3DDevice8* device = NULL;
	HRESULT hr;

	/* level 0 surfaces of source (SYSTEMMEM) and dest (DEFAULT) */
	hr = IDirect3DTexture8_GetSurfaceLevel(srcTexture, 0, &srcSurface);
	if (FAILED(hr)) return D3D_SetError("GetSurfaceLevel(src)", hr);

	hr = IDirect3DTexture8_GetSurfaceLevel(dstTexture, 0, &dstSurface);
	if (FAILED(hr)) {
		IDirect3DSurface8_Release(srcSurface);
		return D3D_SetError("GetSurfaceLevel(dst)", hr);
	}

	/* get the device from the destination resource */
	hr = IDirect3DBaseTexture8_GetDevice((IDirect3DBaseTexture8*)dstTexture, &device);
	if (FAILED(hr)) {
		IDirect3DSurface8_Release(dstSurface);
		IDirect3DSurface8_Release(srcSurface);
		return D3D_SetError("GetDevice(dst)", hr);
	}

	/* let D3D handle linear->swizzled copy */
	hr = IDirect3DDevice8_CopyRects(device,
		srcSurface, NULL, 0,   /* whole source surface */
		dstSurface, NULL);     /* whole destination surface */

	/* release refs */
	IDirect3DDevice8_Release(device);
	IDirect3DSurface8_Release(dstSurface);
	IDirect3DSurface8_Release(srcSurface);

	if (FAILED(hr))
		return D3D_SetError("CopyRects()", hr);

	return D3D_OK;
}




static int
D3D_SetRenderTargetInternal(SDL_Renderer* renderer, SDL_Texture* texture)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	IDirect3DDevice8* device = data->device;
	HRESULT           result;

	/* Release previous non-default RT surface */
	if (data->currentRenderTarget != NULL) {
		IDirect3DSurface8_Release(data->currentRenderTarget);
		data->currentRenderTarget = NULL;
	}

	/* Bind the default backbuffer if texture == NULL */
	if (texture == NULL) {
		/* D3D8 signature: SetRenderTarget(IDirect3DSurface8* rt, IDirect3DSurface8* zstencil) */
		result = IDirect3DDevice8_SetRenderTarget(device, data->defaultRenderTarget, NULL);
		if (FAILED(result)) {
			return D3D_SetError("SetRenderTarget(default)", result);
		}
		return 0;
	}

	/* Validate texture driverdata */
	{
		D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;
		D3D_TextureRep* texturerep;

		if (!texturedata) {
			SDL_SetError("Texture is not currently available");
			return -1;
		}

		/* If CPU-side staging has newer contents, push to default-pool texture now */
		texturerep = &texturedata->texture;
		if (texturerep->dirty && texturerep->staging) {
			if (!texturerep->texture) {
				/* Use the resolved D3D format already stored on the rep */
				result = IDirect3DDevice8_CreateTexture(device,
					texturerep->w, texturerep->h,
					1, texturerep->usage,
					texturerep->d3dfmt,   /* <-- use d3dfmt */
					D3DPOOL_DEFAULT,
					&texturerep->texture);
				if (FAILED(result)) {
					return D3D_SetError("CreateTexture(D3DPOOL_DEFAULT)", result);
				}
			}

#ifdef _XBOX
			result = D3D8_UpdateTexture(texturerep->staging, texturerep->texture);
#else
			result = IDirect3DDevice9_UpdateTexture(device,
				(IDirect3DBaseTexture9*)texturerep->staging,
				(IDirect3DBaseTexture9*)texturerep->texture);
#endif
			if (FAILED(result)) {
				return D3D_SetError("UpdateTexture()", result);
			}
			texturerep->dirty = SDL_FALSE;
		}

		/* Acquire level-0 surface to bind as render target */
		result = IDirect3DTexture8_GetSurfaceLevel(texturedata->texture.texture, 0, &data->currentRenderTarget);
		if (FAILED(result)) {
			return D3D_SetError("GetSurfaceLevel()", result);
		}

		/* D3D8 signature: SetRenderTarget(rt, zstencil). No stream index. */
		result = IDirect3DDevice8_SetRenderTarget(device, data->currentRenderTarget, NULL);
		if (FAILED(result)) {
			/* Clean up the grabbed surface if binding failed */
			IDirect3DSurface8_Release(data->currentRenderTarget);
			data->currentRenderTarget = NULL;
			return D3D_SetError("SetRenderTarget(texture)", result);
		}
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
D3D_QueueDrawPoints(SDL_Renderer* renderer, SDL_RenderCommand* cmd,
	const SDL_FPoint* points, int count)
{
	const DWORD color = D3DCOLOR_ARGB(cmd->data.draw.a,
		cmd->data.draw.r,
		cmd->data.draw.g,
		cmd->data.draw.b);
	const size_t vertslen = (size_t)count * sizeof(Vertex);
	Vertex* verts;

	if (count <= 0) {
		cmd->data.draw.count = 0;
		return 0;
	}

	verts = (Vertex*)SDL_AllocateRenderVertices(renderer, vertslen, 0, &cmd->data.draw.first);
	if (!verts) {
		return -1;
	}

	cmd->data.draw.count = count;

	/* D3D8 raster rules: bias by -0.5 to hit pixel centers */
	for (int i = 0; i < count; i++, verts++, points++) {
		verts->x = points->x - 0.5f;
		verts->y = points->y - 0.5f;
		verts->z = 0.0f;
		verts->color = color;
		verts->u = 0.0f;
		verts->v = 0.0f;
	}

	return 0;
}


static int
D3D_QueueFillRects(SDL_Renderer* renderer, SDL_RenderCommand* cmd,
	const SDL_FRect* rects, int count)
{
	const DWORD color = D3DCOLOR_ARGB(cmd->data.draw.a,
		cmd->data.draw.r,
		cmd->data.draw.g,
		cmd->data.draw.b);
	if (count <= 0) {
		cmd->data.draw.count = 0;
		return 0;
	}

	const size_t vertslen = (size_t)count * sizeof(Vertex) * 4;
	Vertex* verts = (Vertex*)SDL_AllocateRenderVertices(renderer, vertslen, 0, &cmd->data.draw.first);
	if (!verts) {
		return -1;
	}

	cmd->data.draw.count = count;

	for (int i = 0; i < count; i++) {
		const SDL_FRect* rect = &rects[i];
		const float minx = rect->x - 0.5f;
		const float miny = rect->y - 0.5f;
		const float maxx = rect->x + rect->w - 0.5f;
		const float maxy = rect->y + rect->h - 0.5f;

		/* TL */
		verts->x = minx; verts->y = miny; verts->z = 0.0f;
		verts->color = color; verts->u = 0.0f; verts->v = 0.0f; verts++;

		/* TR */
		verts->x = maxx; verts->y = miny; verts->z = 0.0f;
		verts->color = color; verts->u = 0.0f; verts->v = 0.0f; verts++;

		/* BR */
		verts->x = maxx; verts->y = maxy; verts->z = 0.0f;
		verts->color = color; verts->u = 0.0f; verts->v = 0.0f; verts++;

		/* BL */
		verts->x = minx; verts->y = maxy; verts->z = 0.0f;
		verts->color = color; verts->u = 0.0f; verts->v = 0.0f; verts++;
	}

	return 0;
}


static int
D3D_QueueCopy(SDL_Renderer* renderer, SDL_RenderCommand* cmd, SDL_Texture* texture,
	const SDL_Rect* srcrect, const SDL_FRect* dstrect)
{
	const DWORD color = D3DCOLOR_ARGB(cmd->data.draw.a,
		cmd->data.draw.r,
		cmd->data.draw.g,
		cmd->data.draw.b);

	if (!texture || !dstrect || dstrect->w <= 0.0f || dstrect->h <= 0.0f) {
		cmd->data.draw.count = 0;
		return 0; /* nothing to draw */
	}

	float minx, miny, maxx, maxy;
	float minu, maxu, minv, maxv;

	const size_t vertslen = sizeof(Vertex) * 4;
	Vertex* verts = (Vertex*)SDL_AllocateRenderVertices(renderer, vertslen, 0, &cmd->data.draw.first);
	if (!verts) return -1;

	cmd->data.draw.count = 1;

	/* screen-space quad (D3D half-texel bias) */
	minx = dstrect->x - 0.5f;
	miny = dstrect->y - 0.5f;
	maxx = dstrect->x + dstrect->w - 0.5f;
	maxy = dstrect->y + dstrect->h - 0.5f;

	/* pixel-space UVs with 0.5f guard */
	if (srcrect) {
		if (srcrect->w <= 0 || srcrect->h <= 0) {
			cmd->data.draw.count = 0;
			return 0;
		}
		minu = (float)srcrect->x + 0.5f;
		maxu = (float)(srcrect->x + srcrect->w) - 0.5f;
		minv = (float)srcrect->y + 0.5f;
		maxv = (float)(srcrect->y + srcrect->h) - 0.5f;
		if (maxu < minu) maxu = minu;
		if (maxv < minv) maxv = minv;
	}
	else {
		minu = 0.5f;
		minv = 0.5f;
		maxu = (float)texture->w - 0.5f;
		maxv = (float)texture->h - 0.5f;
	}

	/* TL */
	verts->x = minx; verts->y = miny; verts->z = 0.0f; verts->color = color;
	verts->u = minu;  verts->v = minv;  verts++;

	/* TR */
	verts->x = maxx; verts->y = miny; verts->z = 0.0f; verts->color = color;
	verts->u = maxu;  verts->v = minv;  verts++;

	/* BR */
	verts->x = maxx; verts->y = maxy; verts->z = 0.0f; verts->color = color;
	verts->u = maxu;  verts->v = maxv;  verts++;

	/* BL */
	verts->x = minx; verts->y = maxy; verts->z = 0.0f; verts->color = color;
	verts->u = minu;  verts->v = maxv;  verts++;

	return 0;
}





static int
D3D_QueueCopyEx(SDL_Renderer* renderer, SDL_RenderCommand* cmd, SDL_Texture* texture,
	const SDL_Rect* srcquad, const SDL_FRect* dstrect,
	const double angle, const SDL_FPoint* center, const SDL_RendererFlip flip)
{
	const DWORD color = D3DCOLOR_ARGB(cmd->data.draw.a,
		cmd->data.draw.r,
		cmd->data.draw.g,
		cmd->data.draw.b);
	float minx, miny, maxx, maxy;
	float minu, maxu, minv, maxv;
	SDL_FPoint ctr = { 0.0f, 0.0f };

	if (!texture || !srcquad || !dstrect || dstrect->w <= 0.0f || dstrect->h <= 0.0f) {
		cmd->data.draw.count = 0;
		return 0;
	}
	if (center) {
		ctr = *center;
	}

	/* allocate 4 quad verts + 1 control vert */
	const size_t vertslen = sizeof(Vertex) * 5;
	Vertex* verts = (Vertex*)SDL_AllocateRenderVertices(renderer, vertslen, 0, &cmd->data.draw.first);
	if (!verts) {
		return -1;
	}
	cmd->data.draw.count = 1;

	/* Local-space quad around the rotation center (no -0.5 bias here) */
	minx = -ctr.x;
	maxx = dstrect->w - ctr.x;
	miny = -ctr.y;
	maxy = dstrect->h - ctr.y;

	/* Pixel-space UVs with half-texel guards */
	minu = (float)srcquad->x + 0.5f;
	maxu = (float)(srcquad->x + srcquad->w) - 0.5f;
	minv = (float)srcquad->y + 0.5f;
	maxv = (float)(srcquad->y + srcquad->h) - 0.5f;

	/* Apply flips by swapping min/max */
	if (flip & SDL_FLIP_HORIZONTAL) {
		float t = minu; minu = maxu; maxu = t;
	}
	if (flip & SDL_FLIP_VERTICAL) {
		float t = minv; minv = maxv; maxv = t;
	}

	/* TL */
	verts->x = minx; verts->y = miny; verts->z = 0.0f; verts->color = color;
	verts->u = minu; verts->v = minv; verts++;

	/* TR */
	verts->x = maxx; verts->y = miny; verts->z = 0.0f; verts->color = color;
	verts->u = maxu; verts->v = minv; verts++;

	/* BR */
	verts->x = maxx; verts->y = maxy; verts->z = 0.0f; verts->color = color;
	verts->u = maxu; verts->v = maxv; verts++;

	/* BL */
	verts->x = minx; verts->y = maxy; verts->z = 0.0f; verts->color = color;
	verts->u = minu; verts->v = maxv; verts++;

	/* Control vertex: translation (with -0.5 pixel-center bias) and angle (radians in z) */
	verts->x = dstrect->x + ctr.x - 0.5f;   /* X translate */
	verts->y = dstrect->y + ctr.y - 0.5f;   /* Y translate */
	verts->z = (float)(angle * (M_PI / 180.0));  /* rotation in radians */
	verts->color = 0;
	verts->u = 0.0f;   /* not used */
	verts->v = 0.0f;   /* not used */
	verts++;

	return 0;
}

static int
UpdateDirtyTexture(IDirect3DDevice8* device, D3D_TextureRep* texture)
{
	HRESULT result;

	/* Nothing to do if there's no staging or nothing marked dirty */
	if (!texture || !texture->staging || !texture->dirty) {
		return 0;
	}

	/* (Re)create the default-pool texture if missing, using the resolved D3D format */
	if (!texture->texture) {
		result = IDirect3DDevice8_CreateTexture(device,
			texture->w, texture->h,
			1,                       /* levels */
			texture->usage,          /* 0 or D3DUSAGE_RENDERTARGET, etc. */
			texture->d3dfmt,         /* <-- use stored d3dfmt, do NOT recompute */
			D3DPOOL_DEFAULT,
			&texture->texture);
		if (FAILED(result)) {
			return D3D_SetError("CreateTexture(D3DPOOL_DEFAULT)", result);
		}
	}
	else {
		/* Defensive: ensure size/format still match between staging and default textures */
		D3DSURFACE_DESC sdesc, ddesc;
		if (SUCCEEDED(IDirect3DTexture8_GetLevelDesc(texture->staging, 0, &sdesc)) &&
			SUCCEEDED(IDirect3DTexture8_GetLevelDesc(texture->texture, 0, &ddesc))) {
			if ((sdesc.Width != ddesc.Width) ||
				(sdesc.Height != ddesc.Height) ||
				(sdesc.Format != ddesc.Format)) {
				/* Rebuild default-pool texture to match staging */
				IDirect3DTexture8_Release(texture->texture);
				texture->texture = NULL;

				result = IDirect3DDevice8_CreateTexture(device,
					texture->w, texture->h,
					1, texture->usage,
					texture->d3dfmt,
					D3DPOOL_DEFAULT,
					&texture->texture);
				if (FAILED(result)) {
					return D3D_SetError("CreateTexture(D3DPOOL_DEFAULT)", result);
				}
			}
		}
	}

#ifdef _XBOX
	/* Our D3D8 row-by-row copy that respects pitch. */
	result = D3D8_UpdateTexture(texture->staging, texture->texture);
#else
	/* Correct D3D9 signature requires the device pointer. */
	result = IDirect3DDevice9_UpdateTexture((IDirect3DDevice9*)device,
		(IDirect3DBaseTexture9*)texture->staging,
		(IDirect3DBaseTexture9*)texture->texture);
#endif
	if (FAILED(result)) {
		return D3D_SetError("UpdateTexture()", result);
	}

	texture->dirty = SDL_FALSE;
	return 0;
}


static int
BindTextureRep(IDirect3DDevice8* device, D3D_TextureRep* texture, DWORD sampler)
{
	HRESULT hr;

	if (!device || !texture) {
		return D3D_SetError("BindTextureRep(): invalid args", D3DERR_INVALIDCALL);
	}

	/* Push CPU staging to GPU default-pool if needed (also (re)creates GPU tex). */
	if (UpdateDirtyTexture(device, texture) < 0) {
		return -1;  /* error already set */
	}

	/* If there is still no GPU texture to bind, fail clearly. */
	if (!texture->texture) {
		return D3D_SetError("BindTextureRep(): no GPU texture", D3DERR_INVALIDCALL);
	}

	/* D3D8: 'sampler' is the texture stage index. */
	hr = IDirect3DDevice8_SetTexture(device, sampler, (IDirect3DBaseTexture8*)texture->texture);
	if (FAILED(hr)) {
		return D3D_SetError("SetTexture()", hr);
	}

	return 0;
}


static void
UpdateTextureScaleMode(D3D_RenderData* data, D3D_TextureData* texturedata, unsigned index)
{
	if (!data || !texturedata) return;

	/* Apply min/mag filter if changed */
	if (texturedata->scaleMode != data->scaleMode[index]) {
		IDirect3DDevice8_SetTextureStageState(data->device, index, D3DTSS_MINFILTER,
			texturedata->scaleMode);   /* D3DTEXF_POINT or D3DTEXF_LINEAR */
		IDirect3DDevice8_SetTextureStageState(data->device, index, D3DTSS_MAGFILTER,
			texturedata->scaleMode);
		data->scaleMode[index] = texturedata->scaleMode;
	}

	/* Always enforce safe addressing + no mips for 2D/UI text */
	IDirect3DDevice8_SetTextureStageState(data->device, index, D3DTSS_MIPFILTER,
		D3DTEXF_NONE);                 /* no mipmaps */
	IDirect3DDevice8_SetTextureStageState(data->device, index, D3DTSS_ADDRESSU,
		D3DTADDRESS_CLAMP);
	IDirect3DDevice8_SetTextureStageState(data->device, index, D3DTSS_ADDRESSV,
		D3DTADDRESS_CLAMP);
}


static int
SetupTextureState(D3D_RenderData* data, SDL_Texture* texture /*, LPDIRECT3DPIXELSHADER9 *shader*/)
{
	D3D_TextureData* texturedata = (D3D_TextureData*)texture->driverdata;

	// SDL_assert(*shader == NULL); // TODO: Used for YUV textures

	if (!texturedata) {
		SDL_SetError("Texture is not currently available");
		return -1;
	}

	/* Stage 0: main texture */
	UpdateTextureScaleMode(data, texturedata, 0);
	if (BindTextureRep(data->device, &texturedata->texture, 0) < 0) {
		return -1;
	}

	if (texturedata->yuv) {
		/* Choose YUV conversion shader if/when you wire shaders up later
		   (kept as comments to match your current code). */
		switch (SDL_GetYUVConversionModeForResolution(texture->w, texture->h)) {
		case SDL_YUV_CONVERSION_JPEG:
			/* *shader = data->shaders[SHADER_YUV_JPEG];  */
			break;
		case SDL_YUV_CONVERSION_BT601:
			/* *shader = data->shaders[SHADER_YUV_BT601]; */
			break;
		case SDL_YUV_CONVERSION_BT709:
			/* *shader = data->shaders[SHADER_YUV_BT709]; */
			break;
		default:
			return SDL_SetError("Unsupported YUV conversion mode");
		}

		/* Stages 1 & 2: U and V planes */
		UpdateTextureScaleMode(data, texturedata, 1);
		UpdateTextureScaleMode(data, texturedata, 2);

		if (BindTextureRep(data->device, &texturedata->utexture, 1) < 0) {
			return -1;
		}
		if (BindTextureRep(data->device, &texturedata->vtexture, 2) < 0) {
			return -1;
		}

		/* Make sure stages 1/2 are enabled for sampling (stage 0 ops already set globally) */
		IDirect3DDevice8_SetTextureStageState(data->device, 1, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		IDirect3DDevice8_SetTextureStageState(data->device, 1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		IDirect3DDevice8_SetTextureStageState(data->device, 1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		IDirect3DDevice8_SetTextureStageState(data->device, 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

		IDirect3DDevice8_SetTextureStageState(data->device, 2, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		IDirect3DDevice8_SetTextureStageState(data->device, 2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		IDirect3DDevice8_SetTextureStageState(data->device, 2, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		IDirect3DDevice8_SetTextureStageState(data->device, 2, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	}
	else {
		/* Not YUV: explicitly unbind stages 1 & 2 and disable them so prior YUV draws
		   don't leak into this draw. */
		IDirect3DDevice8_SetTexture(data->device, 1, NULL);
		IDirect3DDevice8_SetTexture(data->device, 2, NULL);

		IDirect3DDevice8_SetTextureStageState(data->device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		IDirect3DDevice8_SetTextureStageState(data->device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

		IDirect3DDevice8_SetTextureStageState(data->device, 2, D3DTSS_COLOROP, D3DTOP_DISABLE);
		IDirect3DDevice8_SetTextureStageState(data->device, 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
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

	/* --- Texture binding/state --- */
	if (texture != data->drawstate.texture) {
		D3D_TextureData* oldtex = data->drawstate.texture ? (D3D_TextureData*)data->drawstate.texture->driverdata : NULL;
		D3D_TextureData* newtex = texture ? (D3D_TextureData*)texture->driverdata : NULL;

		/* Disable stages we won�t use; SetupTextureState() will (re)bind what we need. */
		if (texture == NULL) {
			IDirect3DDevice8_SetTexture(data->device, 0, NULL);
		}
		/* If we�re switching from YUV to non-YUV, explicitly unbind U/V */
		if ((!newtex || !newtex->yuv) && (oldtex && oldtex->yuv)) {
			IDirect3DDevice8_SetTexture(data->device, 1, NULL);
			IDirect3DDevice8_SetTexture(data->device, 2, NULL);
			IDirect3DDevice8_SetTextureStageState(data->device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			IDirect3DDevice8_SetTextureStageState(data->device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			IDirect3DDevice8_SetTextureStageState(data->device, 2, D3DTSS_COLOROP, D3DTOP_DISABLE);
			IDirect3DDevice8_SetTextureStageState(data->device, 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}

		if (texture) {
			if (SetupTextureState(data, texture /*, &shader */) < 0) {
				return -1;
			}
		}

		data->drawstate.texture = texture;
	}
	else if (texture) {
		/* Same texture as last draw: make sure any dirty staging gets pushed. */
		D3D_TextureData* texdata = (D3D_TextureData*)texture->driverdata;
		UpdateDirtyTexture(data->device, &texdata->texture);
		if (texdata->yuv) {
			UpdateDirtyTexture(data->device, &texdata->utexture);
			UpdateDirtyTexture(data->device, &texdata->vtexture);
		}
	}

	/* --- Blend state --- */
	if (blend != data->drawstate.blend) {
		if (blend == SDL_BLENDMODE_NONE) {
			IDirect3DDevice8_SetRenderState(data->device, D3DRS_ALPHABLENDENABLE, FALSE);
		}
		else {
			IDirect3DDevice8_SetRenderState(data->device, D3DRS_ALPHABLENDENABLE, TRUE);

			/* D3D8 can�t set separate alpha factors; we validated support earlier.
			   Use the color factors for SRCBLEND/DSTBLEND. */
			IDirect3DDevice8_SetRenderState(data->device, D3DRS_SRCBLEND,
				GetBlendFunc(SDL_GetBlendModeSrcColorFactor(blend)));
			IDirect3DDevice8_SetRenderState(data->device, D3DRS_DESTBLEND,
				GetBlendFunc(SDL_GetBlendModeDstColorFactor(blend)));
		}
		data->drawstate.blend = blend;
	}

	/* --- COPY_EX view transform toggle --- */
	if (is_copy_ex != was_copy_ex) {
		if (!is_copy_ex) { /* COPY_EX executor sets a custom view; reset when leaving it. */
			const Float4X4 m = MatrixIdentity();
			IDirect3DDevice8_SetTransform(data->device, D3DTS_VIEW, (const D3DMATRIX*)&m);
		}
		data->drawstate.is_copy_ex = is_copy_ex;
	}

	/* --- Viewport / projection --- */
	if (data->drawstate.viewport_dirty) {
		const SDL_Rect* vp = &data->drawstate.viewport;
		const D3DVIEWPORT8 d3dvp = (D3DVIEWPORT8){ (DWORD)vp->x, (DWORD)vp->y, (DWORD)vp->w, (DWORD)vp->h, 0.0f, 1.0f };
		IDirect3DDevice8_SetViewport(data->device, &d3dvp);

		if (vp->w && vp->h) {
			D3DMATRIX proj; SDL_zero(proj);
			proj.m[0][0] = 2.0f / (float)vp->w;
			proj.m[1][1] = -2.0f / (float)vp->h;
			proj.m[2][2] = 1.0f;
			proj.m[3][0] = -1.0f;
			proj.m[3][1] = 1.0f;
			proj.m[3][3] = 1.0f;
			IDirect3DDevice8_SetTransform(data->device, D3DTS_PROJECTION, &proj);
		}
		data->drawstate.viewport_dirty = SDL_FALSE;
	}

	/* --- Scissor enable --- */
	if (data->drawstate.cliprect_enabled_dirty) {
		/* On Xbox D3D8 there isn�t a generic SCISSORTESTENABLE; SetScissors call controls it. */
		/* Nothing to do here; we�ll apply the rect below. */
		data->drawstate.cliprect_enabled_dirty = SDL_FALSE;
	}

	/* --- Scissor rect --- */
	if (data->drawstate.cliprect_dirty) {
#ifdef _XBOX
		const SDL_Rect* vp = &data->drawstate.viewport;
		const SDL_Rect* rect = &data->drawstate.cliprect;
		const D3DRECT d3drect = { vp->x + rect->x, vp->y + rect->y,
								  vp->x + rect->x + rect->w, vp->y + rect->y + rect->h };
		/* Enable = TRUE when width/height > 0, otherwise disable */
		const BOOL enable = (rect->w > 0 && rect->h > 0) ? TRUE : FALSE;
		IDirect3DDevice8_SetScissors(data->device, enable ? 1 : 0, FALSE, enable ? &d3drect : NULL);
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
	const SDL_bool istarget = (renderer->target != NULL);
	size_t i;

	if (D3D_ActivateRenderer(renderer) < 0) {
		return -1;
	}

	/* Create / resize the per-batch dynamic vertex buffer if needed */
	vbo = data->vertexBuffers[vboidx];
	if (!vbo || (data->vertexBufferSize[vboidx] < vertsize)) {
		const DWORD usage = D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY;
		const DWORD fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
		if (vbo) {
			IDirect3DVertexBuffer8_Release(vbo);
			vbo = NULL;
		}
		// TODO: Figure out why vertsize is 0
		if (vertsize > 0 && SUCCEEDED(IDirect3DDevice8_CreateVertexBuffer(data->device, (UINT)vertsize, usage, fvf, D3DPOOL_DEFAULT, &vbo))) {
			data->vertexBuffers[vboidx] = vbo;
			data->vertexBufferSize[vboidx] = vertsize;
		}
		else {
			data->vertexBuffers[vboidx] = NULL;
			data->vertexBufferSize[vboidx] = 0;
		}
	}

	/* Upload this batch�s vertices */
	if (vbo) {
		void* ptr = NULL;
		/* Some Xbox D3D8 SDKs don't define D3DLOCK_DISCARD. Fall back to 0. */
		const DWORD lockFlags =
#ifdef D3DLOCK_DISCARD
			D3DLOCK_DISCARD
#else
			0
#endif
			;

		if (SUCCEEDED(IDirect3DVertexBuffer8_Lock(vbo, 0, (UINT)vertsize, (BYTE**)&ptr, lockFlags))) {
			SDL_memcpy(ptr, vertices, vertsize);
			if (FAILED(IDirect3DVertexBuffer8_Unlock(vbo))) {
				vbo = NULL; /* fallback to UP */
			}
		}
		else {
			vbo = NULL; /* fallback to UP */
		}
	}

	/* Cycle VBOs to give the GPU time with the data before reuse */
	if (vbo) {
		data->currentVertexBuffer++;
		if (data->currentVertexBuffer >= SDL_arraysize(data->vertexBuffers)) {
			data->currentVertexBuffer = 0;
		}
	}
	else if (!data->reportedVboProblem) {
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL failed to get a vertex buffer for this Direct3D 8 rendering batch!");
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dropping back to a slower method.");
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "This might be a brief hiccup, but if performance is bad, this is probably why.");
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "This error will not be logged again for this renderer.");
		data->reportedVboProblem = SDL_TRUE;
	}

	/* Bind FVF and stream; when vbo==NULL, DrawPrimitiveUP path ignores this. */
	IDirect3DDevice8_SetVertexShader(data->device, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	IDirect3DDevice8_SetStreamSource(data->device, 0, vbo, sizeof(Vertex));

	while (cmd) {
		switch (cmd->command) {
		case SDL_RENDERCMD_SETDRAWCOLOR:
			/* color is carried per-vertex in this backend */
			break;

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
				/* On Xbox, SetScissors state is reset by Present; we just mark dirty here. */
				data->drawstate.cliprect_enabled_dirty = SDL_TRUE;
			}

			/* Clear the entire RT unless the viewport already covers it */
			if (!viewport->x && !viewport->y && (viewport->w == backw) && (viewport->h == backh)) {
				IDirect3DDevice8_Clear(data->device, 0, NULL, D3DCLEAR_TARGET, color, 0.0f, 0);
			}
			else {
				const D3DVIEWPORT8 wholeviewport = (D3DVIEWPORT8){ 0, 0, (DWORD)backw, (DWORD)backh, 0.0f, 1.0f };
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
				const Vertex* verts = (const Vertex*)(((const Uint8*)vertices) + first);
				IDirect3DDevice8_DrawPrimitiveUP(data->device, D3DPT_POINTLIST, (UINT)count, verts, sizeof(Vertex));
			}
			break;
		}

		case SDL_RENDERCMD_DRAW_LINES: {
			const size_t count = cmd->data.draw.count;
			const size_t first = cmd->data.draw.first;
			const Vertex* verts = (const Vertex*)(((const Uint8*)vertices) + first);
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
				const Vertex* verts = (const Vertex*)(((const Uint8*)vertices) + first);
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
				const Vertex* verts = (const Vertex*)(((const Uint8*)vertices) + first);
				for (i = 0; i < count; ++i, verts += 4) {
					IDirect3DDevice8_DrawPrimitiveUP(data->device, D3DPT_TRIANGLEFAN, 2, verts, sizeof(Vertex));
				}
			}
			break;
		}

		case SDL_RENDERCMD_COPY_EX: {
			const size_t first = cmd->data.draw.first;
			const Vertex* verts = (const Vertex*)(((const Uint8*)vertices) + first);
			const Vertex* transvert = verts + 4;
			const float translatex = transvert->x;
			const float translatey = transvert->y;
			const float rotation = transvert->z;
			const Float4X4 d3dmatrix = MatrixMultiply(MatrixRotationZ(rotation), MatrixTranslation(translatex, translatey, 0.0f));
			SetDrawState(data, cmd);
			IDirect3DDevice8_SetTransform(data->device, D3DTS_VIEW, (const D3DMATRIX*)&d3dmatrix);
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
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	IDirect3DSurface8* srcRT = NULL;
	IDirect3DSurface8* sysmemSurf = NULL;
	D3DSURFACE_DESC desc;
	RECT d3drect;
	D3DLOCKED_RECT locked;
	HRESULT hr;

	if (!renderer || !rect || !pixels) {
		return SDL_SetError("D3D_RenderReadPixels: invalid args");
	}

	/* Choose the active render target (texture RT or default backbuffer). */
	srcRT = data->currentRenderTarget ? data->currentRenderTarget : data->defaultRenderTarget;
	if (!srcRT) {
		return SDL_SetError("D3D_RenderReadPixels: no render target");
	}

	/* Balance lifetime since these surfaces are owned by the renderer. */
	IDirect3DSurface8_AddRef(srcRT);

	hr = IDirect3DSurface8_GetDesc(srcRT, &desc);
	if (FAILED(hr)) {
		IDirect3DSurface8_Release(srcRT);
		return D3D_SetError("GetDesc()", hr);
	}

	/* Create a system-memory image surface (D3D8; Xbox has CreateImageSurface). */
	hr = IDirect3DDevice8_CreateImageSurface(data->device, desc.Width, desc.Height, desc.Format, &sysmemSurf);
	if (FAILED(hr)) {
		IDirect3DSurface8_Release(srcRT);
		return D3D_SetError("CreateImageSurface()", hr);
	}

	/* Copy the whole RT to system memory; simplest path, fast enough for readback. */
	hr = IDirect3DDevice8_CopyRects(data->device,
		srcRT,           /* pSourceSurface */
		NULL, 0,         /* copy all */
		sysmemSurf,      /* pDestinationSurface */
		NULL);           /* same positions */
	IDirect3DSurface8_Release(srcRT);
	if (FAILED(hr)) {
		IDirect3DSurface8_Release(sysmemSurf);
		return D3D_SetError("CopyRects()", hr);
	}

	/* Lock just the requested rect in the sysmem surface. */
	d3drect.left = rect->x;
	d3drect.top = rect->y;
	d3drect.right = rect->x + rect->w;
	d3drect.bottom = rect->y + rect->h;

	hr = IDirect3DSurface8_LockRect(sysmemSurf, &locked, &d3drect, D3DLOCK_READONLY);
	if (FAILED(hr)) {
		IDirect3DSurface8_Release(sysmemSurf);
		return D3D_SetError("LockRect()", hr);
	}

	/* Convert/copy into caller�s buffer. */
	SDL_ConvertPixels(rect->w, rect->h,
		D3DFMTToPixelFormat(desc.Format), locked.pBits, locked.Pitch,
		format, pixels, pitch);

	IDirect3DSurface8_UnlockRect(sysmemSurf);
	IDirect3DSurface8_Release(sysmemSurf);
	return 0;
}


static void
D3D_RenderPresent(SDL_Renderer* renderer)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	HRESULT hr;

	/* If we started a scene this frame, end it now. */
	if (!data->beginScene) {
		hr = IDirect3DDevice8_EndScene(data->device);
		if (FAILED(hr)) {
			D3D_SetError("EndScene()", hr);
			data->beginScene = SDL_TRUE;  /* keep internal state consistent */
			return;                        /* don't try to Present */
		}
		data->beginScene = SDL_TRUE;
	}

	/* If rendering to a texture (offscreen RT), do NOT Present. */
	if (renderer->target != NULL) {
		return;
	}

#ifndef _XBOX
	/* PC D3D8/9 device-loss handling (not used on OG Xbox). */
	hr = IDirect3DDevice8_TestCooperativeLevel(data->device);
	if (hr == D3DERR_DEVICELOST) {
		return;  /* will reset later */
	}
	if (hr == D3DERR_DEVICENOTRESET) {
		D3D_Reset(renderer);
	}
#endif

	hr = IDirect3DDevice8_Present(data->device, NULL, NULL, NULL, NULL);
	if (FAILED(hr)) {
		D3D_SetError("Present()", hr);
	}

#ifdef _XBOX
	/* Present resets scissor; mark dirty so next frame reapplies it. */
	data->drawstate.cliprect_enabled_dirty = SDL_TRUE;
	data->drawstate.cliprect_dirty = SDL_TRUE;
#endif
}


static void
D3D_DestroyTexture(SDL_Renderer* renderer, SDL_Texture* texture)
{
	D3D_RenderData* renderdata = (D3D_RenderData*)renderer->driverdata;
	D3D_TextureData* data = (D3D_TextureData*)texture->driverdata;

	if (!texture) return;

	/* If this texture is currently the render target, restore default RT first. */
	if (renderer->target == texture) {
		D3D_SetRenderTarget(renderer, NULL);  /* ignores errors here on purpose */
		renderer->target = NULL;
	}

	/* If bound for drawing, unbind from device stages to avoid dangling refs. */
	if (renderdata) {
		if (renderdata->drawstate.texture == texture) {
			/* Stage 0 */
			IDirect3DDevice8_SetTexture(renderdata->device, 0, NULL);
			renderdata->drawstate.texture = NULL;
		}
		if (data && data->yuv) {
			/* Stages 1 & 2 may be bound from previous YUV draws. */
			IDirect3DDevice8_SetTexture(renderdata->device, 1, NULL);
			IDirect3DDevice8_SetTexture(renderdata->device, 2, NULL);
			IDirect3DDevice8_SetTextureStageState(renderdata->device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			IDirect3DDevice8_SetTextureStageState(renderdata->device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			IDirect3DDevice8_SetTextureStageState(renderdata->device, 2, D3DTSS_COLOROP, D3DTOP_DISABLE);
			IDirect3DDevice8_SetTextureStageState(renderdata->device, 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}
	}

	if (!data) {
		texture->driverdata = NULL;
		return;
	}

	/* Release GPU objects (default-pool + system-mem staging). */
	D3D_DestroyTextureRep(&data->texture);
	D3D_DestroyTextureRep(&data->utexture);
	D3D_DestroyTextureRep(&data->vtexture);

	/* Free CPU staging buffer used by Lock/Unlock for YUV. */
	SDL_free(data->pixels);

	SDL_free(data);
	texture->driverdata = NULL;
}


static void
D3D_DestroyRenderer(SDL_Renderer* renderer)
{
	if (!renderer) return;

	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;

	if (data) {
		int i;

		/* If we�re mid-frame, close it cleanly. */
		if (!data->beginScene && data->device) {
			IDirect3DDevice8_EndScene(data->device);
			data->beginScene = SDL_TRUE;
		}

		/* Unbind textures/stages so nothing holds extra refs. */
		if (data->device) {
			IDirect3DDevice8_SetTexture(data->device, 0, NULL);
			IDirect3DDevice8_SetTexture(data->device, 1, NULL);
			IDirect3DDevice8_SetTexture(data->device, 2, NULL);
			IDirect3DDevice8_SetTextureStageState(data->device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			IDirect3DDevice8_SetTextureStageState(data->device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			IDirect3DDevice8_SetTextureStageState(data->device, 2, D3DTSS_COLOROP, D3DTOP_DISABLE);
			IDirect3DDevice8_SetTextureStageState(data->device, 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}

		/* Release any render targets we own. */
		if (data->currentRenderTarget) {
			IDirect3DSurface8_Release(data->currentRenderTarget);
			data->currentRenderTarget = NULL;
		}
		if (data->defaultRenderTarget) {
			IDirect3DSurface8_Release(data->defaultRenderTarget);
			data->defaultRenderTarget = NULL;
		}

#if 0 /* (kept for completeness; D3D8 doesn�t use these in your build) */
		for (i = 0; i < SDL_arraysize(data->shaders); ++i) {
			if (data->shaders[i]) {
				IDirect3DPixelShader9_Release(data->shaders[i]);
				data->shaders[i] = NULL;
			}
		}
#endif

		/* Release VBOs. */
		for (i = 0; i < (int)SDL_arraysize(data->vertexBuffers); ++i) {
			if (data->vertexBuffers[i]) {
				IDirect3DVertexBuffer8_Release(data->vertexBuffers[i]);
				data->vertexBuffers[i] = NULL;
			}
			data->vertexBufferSize[i] = 0;
		}
		data->currentVertexBuffer = 0;

		/* Release device and D3D */
		if (data->device) {
			IDirect3DDevice8_Release(data->device);
			data->device = NULL;
		}
		if (data->d3d) {
			IDirect3D8_Release(data->d3d);
#ifndef _XBOX
			if (data->d3dDLL) {
				SDL_UnloadObject(data->d3dDLL);
				data->d3dDLL = NULL;
			}
#endif
			data->d3d = NULL;
		}

		SDL_free(data);
		renderer->driverdata = NULL;
	}

	SDL_free(renderer);
}


static int
D3D_Reset(SDL_Renderer* renderer)
{
	D3D_RenderData* data = (D3D_RenderData*)renderer->driverdata;
	const Float4X4 viewIdent = MatrixIdentity();
	HRESULT result;
	SDL_Texture* texture;
	int i;

	/* Release current/default render targets before Reset. */
	if (data->currentRenderTarget) {
		IDirect3DSurface8_Release(data->currentRenderTarget);
		data->currentRenderTarget = NULL;
	}
	if (data->defaultRenderTarget) {
		IDirect3DSurface8_Release(data->defaultRenderTarget);
		data->defaultRenderTarget = NULL;
	}

	/* Textures: destroy render targets; non-RT textures drop their DEFAULT-pool object and stay dirty. */
	for (texture = renderer->textures; texture; texture = texture->next) {
		if (texture->access == SDL_TEXTUREACCESS_TARGET) {
			D3D_DestroyTexture(renderer, texture);
		}
		else {
			/* Releases DEFAULT-pool GPU tex and marks staging dirty if present. */
			D3D_RecreateTexture(renderer, texture);
		}
	}

	/* Release all dynamic vertex buffers. */
	for (i = 0; i < (int)SDL_arraysize(data->vertexBuffers); ++i) {
		if (data->vertexBuffers[i]) {
			IDirect3DVertexBuffer8_Release(data->vertexBuffers[i]);
			data->vertexBuffers[i] = NULL;
		}
		data->vertexBufferSize[i] = 0;
	}
	data->currentVertexBuffer = 0;
	data->reportedVboProblem = SDL_FALSE;

	/* Reset the device with the current present params. */
	result = IDirect3DDevice8_Reset(data->device, &data->pparams);
	if (FAILED(result)) {
		if (result == D3DERR_DEVICELOST) {
			/* Will try again later. */
			return 0;
		}
		return D3D_SetError("Reset()", result);
	}

	/* Recreate application render targets (DEFAULT pool) now that the device is reset. */
	for (texture = renderer->textures; texture; texture = texture->next) {
		if (texture->access == SDL_TEXTUREACCESS_TARGET) {
			if (D3D_CreateTexture(renderer, texture) < 0) {
				/* keep going; error already set, but try to recover best we can */
			}
		}
	}

	/* Fetch the new backbuffer surface as our default RT. */
	result = IDirect3DDevice8_GetRenderTarget(data->device, &data->defaultRenderTarget);
	if (FAILED(result)) {
		return D3D_SetError("GetRenderTarget()", result);
	}

	/* Re-init render state and bind the correct RT (NULL = backbuffer). */
	D3D_InitRenderState(data);
	if (D3D_SetRenderTargetInternal(renderer, renderer->target) < 0) {
		/* fall back to backbuffer if binding failed */
		D3D_SetRenderTargetInternal(renderer, NULL);
	}

	/* Mark state dirty so next draw reapplies viewport/scissor/projection. */
	data->drawstate.viewport_dirty = SDL_TRUE;
	data->drawstate.cliprect_dirty = SDL_TRUE;
	data->drawstate.cliprect_enabled_dirty = SDL_TRUE;

	data->drawstate.texture = NULL;
	data->drawstate.blend = SDL_BLENDMODE_INVALID;
	data->drawstate.is_copy_ex = SDL_FALSE;

	/* Reset view matrix to identity (COPY_EX will override when needed). */
	IDirect3DDevice8_SetTransform(data->device, D3DTS_VIEW, (const D3DMATRIX*)&viewIdent);

	/* Let the app know RTs were reset. */
	{
		SDL_Event event;
		event.type = SDL_RENDER_TARGETS_RESET;
		SDL_PushEvent(&event);
	}

	return 0;
}

#ifdef _XBOX
/* -----------------------------------------------------------------------
   FinalizeXboxMode
   -----------------------------------------------------------------------
   Given an already-chosen BackBufferWidth/Height, set the ONLY valid
   combination of flags and refresh rate for OG Xbox, based on XDK rules
   and the user�s dashboard settings.

   XDK rules encoded:
   - 720p  (1280x720)      > PROGRESSIVE + WIDESCREEN, 60 Hz
   - 1080i (1920x1080)     > INTERLACED + WIDESCREEN, 60 Hz
   - 576i  (720x576)       > INTERLACED only, 50 Hz (PAL-60 does NOT apply to 576)
   - 480p/i (640x480/720x480) > 60 Hz; PROGRESSIVE only if 480p is enabled
	 WIDESCREEN only for 720x480 (anamorphic) when dashboard WIDESCREEN is ON
   - WIDESCREEN affects aspect signalling, not buffer size
------------------------------------------------------------------------ */
static void FinalizeXboxMode(D3DPRESENT_PARAMETERS* p)
{
	DWORD vf = XGetVideoFlags();
	const BOOL ws = (vf & XC_VIDEO_FLAGS_WIDESCREEN) != 0;
	const BOOL allow480p = (vf & XC_VIDEO_FLAGS_HDTV_480p) != 0;
	const BOOL allow720p = (vf & XC_VIDEO_FLAGS_HDTV_720p) != 0;  /* info only */
	const BOOL allow1080i = (vf & XC_VIDEO_FLAGS_HDTV_1080i) != 0;  /* info only */
	const BOOL pal = (XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I);

	/* Start from a clean slate so no stale bits sneak in. */
	p->Flags = 0;

	if (p->BackBufferWidth == 1280 && p->BackBufferHeight == 720) {
		/* 720p */
		p->Flags |= D3DPRESENTFLAG_PROGRESSIVE;
		if (ws) p->Flags |= D3DPRESENTFLAG_WIDESCREEN;
		p->FullScreen_RefreshRateInHz = 60;
	}
	else if (p->BackBufferWidth == 1920 && p->BackBufferHeight == 1080) {
		/* 1080i */
		p->Flags |= D3DPRESENTFLAG_INTERLACED;
		if (ws) p->Flags |= D3DPRESENTFLAG_WIDESCREEN;
		p->FullScreen_RefreshRateInHz = 60;
	}
	else if (p->BackBufferWidth == 720 && p->BackBufferHeight == 576) {
		/* 576i (PAL) � always 50 Hz; PAL-60 does NOT support 576. */
		p->Flags |= D3DPRESENTFLAG_INTERLACED;
		if (ws) p->Flags |= D3DPRESENTFLAG_WIDESCREEN;   /* anamorphic WS */
		p->FullScreen_RefreshRateInHz = 50;              /* critical per XDK */
	}
	else if ((p->BackBufferWidth == 640 && p->BackBufferHeight == 480) ||
		(p->BackBufferWidth == 720 && p->BackBufferHeight == 480)) {
		/* 480 family (NTSC or PAL-60) at 60 Hz */
		if (allow480p) p->Flags |= D3DPRESENTFLAG_PROGRESSIVE;  /* 480p */
		else            p->Flags |= D3DPRESENTFLAG_INTERLACED;   /* 480i */
		if (ws && p->BackBufferWidth == 720) {
			p->Flags |= D3DPRESENTFLAG_WIDESCREEN;               /* 720x480 anamorphic */
		}
		p->FullScreen_RefreshRateInHz = 60;
	}
	else {
		/* Any odd size (debug/dev) > conservative default. */
		p->Flags |= D3DPRESENTFLAG_INTERLACED;
		p->FullScreen_RefreshRateInHz = 60;
	}

	/* Required on Xbox */
	p->BackBufferFormat = D3DFMT_LIN_X8R8G8B8;
	p->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;

	/* --- Safety checks (debug; keep or #ifdef as needed) ----------------- */
	/* Never progressive for 1080/576 */
	if ((p->BackBufferHeight == 1080 || p->BackBufferHeight == 576) &&
		(p->Flags & D3DPRESENTFLAG_PROGRESSIVE)) {
		SDL_Log("WARN: illegal progressive for %u-line mode; forcing interlaced\n",
			(unsigned)p->BackBufferHeight);
		p->Flags &= ~D3DPRESENTFLAG_PROGRESSIVE;
		p->Flags |= D3DPRESENTFLAG_INTERLACED;
	}
	/* 576 must be 50 Hz */
	if (p->BackBufferHeight == 576 && p->FullScreen_RefreshRateInHz != 50) {
		SDL_Log("WARN: 576 must be 50 Hz; overriding\n");
		p->FullScreen_RefreshRateInHz = 50;
	}
	/* Never mark 640-wide SD as widescreen (covers 640x480 and 640x576) */
	if ((p->BackBufferWidth == 640) && (p->Flags & D3DPRESENTFLAG_WIDESCREEN)) {
		SDL_Log("WARN: 640-wide cannot be widescreen; clearing WS flag\n");
		p->Flags &= ~D3DPRESENTFLAG_WIDESCREEN;
	}
	/* Optional: progressive only valid for 480p or 720p */
	if ((p->Flags & D3DPRESENTFLAG_PROGRESSIVE) &&
		!(p->BackBufferHeight == 480 || p->BackBufferHeight == 720)) {
		SDL_Log("WARN: progressive set on non-480/720; forcing interlaced\n");
		p->Flags &= ~D3DPRESENTFLAG_PROGRESSIVE;
		p->Flags |= D3DPRESENTFLAG_INTERLACED;
	}

	SDL_Log("FINAL MODE: %ux%u flags=0x%08x @ %u Hz  (WS=%d 480p=%d 720p=%d 1080i=%d PAL=%d)\n",
		(unsigned)p->BackBufferWidth, (unsigned)p->BackBufferHeight,
		(unsigned)p->Flags, (unsigned)p->FullScreen_RefreshRateInHz,
		(int)ws, (int)allow480p, (int)allow720p, (int)allow1080i, (int)pal);
}
#endif

int
D3D_CreateRenderer(SDL_Renderer* renderer, SDL_Window* window, Uint32 flags)
{
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
	int i;

	data = (D3D_RenderData*)SDL_calloc(1, sizeof(*data));
	if (!data) { SDL_free(renderer); SDL_OutOfMemory(); return -1; }

	if (!D3D_LoadDLL(/*&data->d3dDLL,*/ &data->d3d)) {
		SDL_free(renderer); SDL_free(data);
		SDL_SetError("Unable to create Direct3D interface\n");
		return -1;
	}

	/* Hook renderer callbacks */
	renderer->WindowEvent = D3D_WindowEvent;
	renderer->SupportsBlendMode = D3D_SupportsBlendMode;
	renderer->CreateTexture = D3D_CreateTexture;
	renderer->UpdateTexture = D3D_UpdateTexture;
	renderer->UpdateTextureYUV = D3D_UpdateTextureYUV;
	renderer->LockTexture = D3D_LockTexture;
	renderer->UnlockTexture = D3D_UnlockTexture;
	renderer->SetRenderTarget = D3D_SetRenderTarget;
	renderer->QueueSetViewport = D3D_QueueSetViewport;
	renderer->QueueSetDrawColor = D3D_QueueSetViewport;  /* no-op */
	renderer->QueueDrawPoints = D3D_QueueDrawPoints;
	renderer->QueueDrawLines = D3D_QueueDrawPoints;   /* shared */
	renderer->QueueFillRects = D3D_QueueFillRects;
	renderer->QueueCopy = D3D_QueueCopy;
	renderer->QueueCopyEx = D3D_QueueCopyEx;
	renderer->RunCommandQueue = D3D_RunCommandQueue;
	renderer->RenderReadPixels = D3D_RenderReadPixels;
	renderer->RenderPresent = D3D_RenderPresent;
	renderer->DestroyTexture = D3D_DestroyTexture;
	renderer->DestroyRenderer = D3D_DestroyRenderer;

	/* Base driver info */
	renderer->info = D3D_RenderDriver.info;
	renderer->info.flags = (SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);

	renderer->driverdata = data;
	renderer->window = window;

#ifndef _XBOX
	SDL_VERSION(&windowinfo.version);
	SDL_GetWindowWMInfo(window, &windowinfo);
	window_flags = SDL_GetWindowFlags(window);
	SDL_GetWindowDisplayMode(window, &fullscreen_mode);
#endif

	SDL_GetWindowSize(window, &w, &h);

	SDL_zero(pparams);
	pparams.BackBufferWidth = (UINT)w;
	pparams.BackBufferHeight = (UINT)h;
	pparams.BackBufferCount = 1;
	pparams.SwapEffect = D3DSWAPEFFECT_DISCARD;

#ifndef _XBOX
	pparams.hDeviceWindow = windowinfo.info.win.window;

	if (window_flags & SDL_WINDOW_FULLSCREEN &&
		(window_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != SDL_WINDOW_FULLSCREEN_DESKTOP) {
		pparams.Windowed = FALSE;
		pparams.BackBufferFormat = PixelFormatToD3DFMT(fullscreen_mode.format);
		pparams.FullScreen_RefreshRateInHz = fullscreen_mode.refresh_rate;
	}
	else {
		pparams.Windowed = TRUE;
		pparams.BackBufferFormat = D3DFMT_UNKNOWN;
		pparams.FullScreen_RefreshRateInHz = 0;
	}
	pparams.PresentationInterval = (flags & SDL_RENDERER_PRESENTVSYNC) ?
		D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

	/* Choose adapter for window's display */
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

#else  /* _XBOX */
	device_flags = D3DCREATE_HARDWARE_VERTEXPROCESSING;

	/* Xbox defaults */
	pparams.EnableAutoDepthStencil = TRUE;
	pparams.AutoDepthStencilFormat = D3DFMT_D16;
	pparams.hDeviceWindow = NULL;
	pparams.Windowed = FALSE;
	pparams.BackBufferFormat = D3DFMT_LIN_X8R8G8B8;   /* linear backbuffer for Present */
	pparams.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;
	pparams.MultiSampleType = D3DMULTISAMPLE_NONE;

	/* Honor the app's requested size if allowed; otherwise fall back to best supported. */
	{
		vidflags = XGetVideoFlags();
		const SDL_bool is_pal = (XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I);

		int reqw = 0, reqh = 0;
		SDL_GetWindowSize(window, &reqw, &reqh);

		const SDL_bool allow1080i = (vidflags & XC_VIDEO_FLAGS_HDTV_1080i) ? SDL_TRUE : SDL_FALSE;
		const SDL_bool allow720p = (vidflags & XC_VIDEO_FLAGS_HDTV_720p) ? SDL_TRUE : SDL_FALSE;
		const SDL_bool allow480p = (vidflags & XC_VIDEO_FLAGS_HDTV_480p) ? SDL_TRUE : SDL_FALSE;
		const SDL_bool has_pal60 = (vidflags & XC_VIDEO_FLAGS_PAL_60Hz) ? SDL_TRUE : SDL_FALSE;

		const SDL_bool want1080 = (reqw == 1920 && reqh == 1080);
		const SDL_bool want720 = (reqw == 1280 && reqh == 720);
		const SDL_bool want480 = ((reqw == 720 && reqh == 480) || (reqw == 640 && reqh == 480));

		SDL_bool supported = SDL_FALSE;

		/* 1) Honor the app's request if the dashboard allows it */
		if (want480 && allow480p) { pparams.BackBufferWidth = 720;  pparams.BackBufferHeight = 480;  supported = SDL_TRUE; }
		else if (want720 && allow720p) { pparams.BackBufferWidth = 1280; pparams.BackBufferHeight = 720;  supported = SDL_TRUE; }
		else if (want1080 && allow1080i) { pparams.BackBufferWidth = 1920; pparams.BackBufferHeight = 1080; supported = SDL_TRUE; }

		/* 2) Otherwise pick the best supported mode */
		if (!supported) {
			UINT ow = pparams.BackBufferWidth, oh = pparams.BackBufferHeight;
			if (allow720p) {
				pparams.BackBufferWidth = 1280; pparams.BackBufferHeight = 720;
			}
			else if (allow1080i) {
				pparams.BackBufferWidth = 1920; pparams.BackBufferHeight = 1080;
			}
			else if (allow480p) {
				pparams.BackBufferWidth = 720;  pparams.BackBufferHeight = 480;
			}
			else if (has_pal60) {
				pparams.BackBufferWidth = 720;  pparams.BackBufferHeight = 480;   /* 480i @ 60; PAL*/
			}
			else if (is_pal) {
				pparams.BackBufferWidth = 720;  pparams.BackBufferHeight = 576;   /* 576i @ 50 */
			}
			else {
				pparams.BackBufferWidth = 640;  pparams.BackBufferHeight = 480;   /* 480i @ 60 */
			}
			SDL_Log("Xbox D3D: requested %dx%d not permitted; falling back to %ux%u\n",
				reqw, reqh, (unsigned)pparams.BackBufferWidth, (unsigned)pparams.BackBufferHeight);
		}
	}

	/* Finalize flags/rate/format per XDK rules (single source of truth) */
	FinalizeXboxMode(&pparams);

#endif /* _XBOX */

	result = IDirect3D8_CreateDevice(data->d3d, 0,
		D3DDEVTYPE_HAL,
		NULL /* pparams.hDeviceWindow */,
		device_flags,
		&pparams, &data->device);
	if (FAILED(result)) {
		D3D_DestroyRenderer(renderer);
		D3D_SetError("CreateDevice()", result);
		return -1;
	}

#ifdef _XBOX
	/* Keep SDL's window size consistent with the actual backbuffer */
	SDL_SetWindowSize(window, (int)pparams.BackBufferWidth, (int)pparams.BackBufferHeight);
#endif

#ifndef _XBOX
	/* Fill some info from the actual present parameters. */
	result = IDirect3DDevice9_GetSwapChain(data->device, 0, &chain);
	if (FAILED(result)) { D3D_DestroyRenderer(renderer); D3D_SetError("GetSwapChain()", result); return -1; }
	result = IDirect3DSwapChain9_GetPresentParameters(chain, &pparams);
	IDirect3DSwapChain9_Release(chain);
	if (FAILED(result)) { D3D_DestroyRenderer(renderer); D3D_SetError("GetPresentParameters()", result); return -1; }
	if (pparams.PresentationInterval == D3DPRESENT_INTERVAL_ONE) {
		renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
	}
#else
	/* Xbox: reflect vsync choice back to info */
	if (pparams.FullScreen_PresentationInterval == D3DPRESENT_INTERVAL_ONE) {
		renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
	}
#endif

	data->pparams = pparams;

	/* Device caps and texture limits */
	IDirect3DDevice8_GetDeviceCaps(data->device, &caps);
	renderer->info.max_texture_width = caps.MaxTextureWidth;
	renderer->info.max_texture_height = caps.MaxTextureHeight;

#ifndef _XBOX
	if (caps.NumSimultaneousRTs >= 2) {
		renderer->info.flags |= SDL_RENDERER_TARGETTEXTURE;
	}
	if (caps.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND) {
		data->enableSeparateAlphaBlend = SDL_TRUE;
	}
#endif

	/* Start with no cached VB state. */
	for (i = 0; i < (int)SDL_arraysize(data->vertexBuffers); ++i) {
		data->vertexBuffers[i] = NULL;
		data->vertexBufferSize[i] = 0;
	}
	data->currentVertexBuffer = 0;
	data->reportedVboProblem = SDL_FALSE;

	/* Grab default RT (backbuffer), set initial render state. */
	IDirect3DDevice8_GetRenderTarget(data->device, &data->defaultRenderTarget);
	data->currentRenderTarget = NULL;

	D3D_InitRenderState(data);

	/* Mark drawstate dirty so first frame re-applies viewport/scissor/projection. */
	data->drawstate.viewport_dirty = SDL_TRUE;
	data->drawstate.cliprect_dirty = SDL_TRUE;
	data->drawstate.cliprect_enabled_dirty = SDL_TRUE;
	data->drawstate.texture = NULL;
	data->drawstate.blend = SDL_BLENDMODE_INVALID;
	data->drawstate.is_copy_ex = SDL_FALSE;

	return 0;
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