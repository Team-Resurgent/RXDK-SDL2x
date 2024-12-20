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

#if SDL_AUDIO_DRIVER_DSOUND

/* Allow access to a raw mixing buffer */

#include "SDL_assert.h"
#include "SDL_timer.h"
#ifndef _XBOX
#include "SDL_loadso.h"
#endif
#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_directsound.h"

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif

/* DirectX function pointers for audio */
#ifndef _XBOX // We will acccess directly
static void* DSoundDLL = NULL;
typedef HRESULT(WINAPI* fnDirectSoundCreate8)(LPGUID, LPDIRECTSOUND*, LPUNKNOWN);
typedef HRESULT(WINAPI* fnDirectSoundEnumerateW)(LPDSENUMCALLBACKW, LPVOID);
typedef HRESULT(WINAPI* fnDirectSoundCaptureCreate8)(LPCGUID, LPDIRECTSOUNDCAPTURE8*, LPUNKNOWN);
typedef HRESULT(WINAPI* fnDirectSoundCaptureEnumerateW)(LPDSENUMCALLBACKW, LPVOID);
static fnDirectSoundCreate8 pDirectSoundCreate8 = NULL;
static fnDirectSoundEnumerateW pDirectSoundEnumerateW = NULL;
static fnDirectSoundCaptureCreate8 pDirectSoundCaptureCreate8 = NULL;
static fnDirectSoundCaptureEnumerateW pDirectSoundCaptureEnumerateW = NULL;
#endif

static void
DSOUND_Unload(void)
{
#ifndef _XBOX
	pDirectSoundCreate8 = NULL;
	pDirectSoundEnumerateW = NULL;
	pDirectSoundCaptureCreate8 = NULL;
	pDirectSoundCaptureEnumerateW = NULL;

	if (DSoundDLL != NULL) {
		SDL_UnloadObject(DSoundDLL);
		DSoundDLL = NULL;
	}
#endif
}

static int
DSOUND_Load(void)
{
	int loaded = 0;

	DSOUND_Unload();

#ifndef _XBOX
	DSoundDLL = SDL_LoadObject("DSOUND.DLL");
	if (DSoundDLL == NULL) {
		SDL_SetError("DirectSound: failed to load DSOUND.DLL");
	}
	else {
		/* Now make sure we have DirectX 8 or better... */
#define DSOUNDLOAD(f) { \
            p##f = (fn##f) SDL_LoadFunction(DSoundDLL, #f); \
            if (!p##f) loaded = 0; \
        }
		loaded = 1;  /* will reset if necessary. */
		DSOUNDLOAD(DirectSoundCreate8);
		DSOUNDLOAD(DirectSoundEnumerateW);
		DSOUNDLOAD(DirectSoundCaptureCreate8);
		DSOUNDLOAD(DirectSoundCaptureEnumerateW);
#undef DSOUNDLOAD

		if (!loaded) {
			SDL_SetError("DirectSound: System doesn't appear to have DX8.");
		}
	}

	if (!loaded) {
		DSOUND_Unload();
	}

	return loaded;
#else
	return 1;
#endif
}

static int
SetDSerror(const char* function, int code)
{
	static const char* error;
	static char errbuf[1024];

	errbuf[0] = 0;
	switch (code) {
	case E_NOINTERFACE:
		error = "Unsupported interface -- Is DirectX 8.0 or later installed?";
		break;

	case DSERR_CONTROLUNAVAIL:
		error = "Control requested is not available";
		break;
	case DSERR_INVALIDCALL:
		error = "Invalid call for the current state";
		break;
	case DSERR_NODRIVER:
		error = "No audio device found";
		break;
	case DSERR_OUTOFMEMORY:
		error = "Out of memory";
		break;
#ifndef _XBOX
	case DSERR_PRIOLEVELNEEDED:
		error = "Caller doesn't have priority";
		break;
	case DSERR_ALLOCATED:
		error = "Audio device in use";
		break;
	case DSERR_BADFORMAT:
		error = "Unsupported audio format";
		break;
	case DSERR_BUFFERLOST:
		error = "Mixing buffer was lost";
		break;
	case DSERR_INVALIDPARAM:
		error = "Invalid parameter";
		break;
#endif
	case DSERR_UNSUPPORTED:
		error = "Function not supported";
		break;
	default:
		SDL_snprintf(errbuf, SDL_arraysize(errbuf),
			"%s: Unknown DirectSound error: 0x%x", function, code);
		break;
	}
	if (!errbuf[0]) {
		SDL_snprintf(errbuf, SDL_arraysize(errbuf), "%s: %s", function,
			error);
	}
	return SDL_SetError("%s", errbuf);
}

static void
DSOUND_FreeDeviceHandle(void* handle)
{
	SDL_free(handle);
}

#ifndef _XBOX
static BOOL CALLBACK
FindAllDevs(LPGUID guid, LPCWSTR desc, LPCWSTR module, LPVOID data)
{
	const int iscapture = (int)((size_t)data);
	if (guid != NULL) {  /* skip default device */
		char* str = WIN_LookupAudioDeviceName(desc, guid);
		if (str != NULL) {
			LPGUID cpyguid = (LPGUID)SDL_malloc(sizeof(GUID));
			SDL_memcpy(cpyguid, guid, sizeof(GUID));
			SDL_AddAudioDevice(iscapture, str, cpyguid);
			SDL_free(str);  /* addfn() makes a copy of this string. */
		}
	}
	return TRUE;  /* keep enumerating. */
}
#endif

static void
DSOUND_DetectDevices(void)
{
#ifndef _XBOX
	pDirectSoundCaptureEnumerateW(FindAllDevs, (void*)((size_t)1));
	pDirectSoundEnumerateW(FindAllDevs, (void*)((size_t)0));
#endif
}

static void
DSOUND_WaitDevice(_THIS)
{
	DWORD status = 0;
	DWORD cursor = 0;
	DWORD junk = 0;
	HRESULT result = DS_OK;

	// Get the current position in the buffer
	result = IDirectSoundBuffer_GetCurrentPosition(this->hidden->mixbuf, &junk, &cursor);
	if (result != DS_OK) {
#ifndef _XBOX
		if (result == DSERR_BUFFERLOST) {
			IDirectSoundBuffer_Restore(this->hidden->mixbuf);
		}
#endif
#ifdef DEBUG_SOUND
		SetDSerror("DirectSound GetCurrentPosition", result);
#endif
		return;
	}

	// Calculate the chunk size and playback position
	DWORD chunkSize = this->spec.size;
	DWORD lastChunk = this->hidden->lastchunk;

	// Wait until the playback moves to the next chunk
	while ((cursor / chunkSize) == lastChunk) {
		// Calculate remaining time in the current chunk
		DWORD remainingBytes = chunkSize - (cursor % chunkSize);
		DWORD sampleRate = this->spec.freq;
		DWORD channels = this->spec.channels;
		DWORD bytesPerSample = SDL_AUDIO_BITSIZE(this->spec.format) / 8;

		DWORD remainingTimeMs = (remainingBytes * 1000) / (sampleRate * channels * bytesPerSample);

		// Delay for the remaining time or a minimum of 1 ms
		SDL_Delay(remainingTimeMs > 1 ? remainingTimeMs : 1);

		// Check buffer status
		IDirectSoundBuffer_GetStatus(this->hidden->mixbuf, &status);
#ifndef _XBOX
		if ((status & DSBSTATUS_BUFFERLOST)) {
			IDirectSoundBuffer_Restore(this->hidden->mixbuf);
			IDirectSoundBuffer_GetStatus(this->hidden->mixbuf, &status);
			if ((status & DSBSTATUS_BUFFERLOST)) {
				break;
			}
		}
#endif
		// If the buffer is not playing, try to restart it
		if (!(status & DSBSTATUS_PLAYING)) {
			result = IDirectSoundBuffer_Play(this->hidden->mixbuf, 0, 0, DSBPLAY_LOOPING);
			if (result != DS_OK) {
#ifdef DEBUG_SOUND
				SetDSerror("DirectSound Play", result);
#endif
				return;
			}
		}

		// Update the cursor position
		result = IDirectSoundBuffer_GetCurrentPosition(this->hidden->mixbuf, &junk, &cursor);
		if (result != DS_OK) {
			SetDSerror("DirectSound GetCurrentPosition", result);
			return;
		}
	}
}

static void
DSOUND_PlayDevice(_THIS)
{
	/* Unlock the buffer, allowing it to play */
	if (this->hidden->locked_buf) {
		IDirectSoundBuffer_Unlock(this->hidden->mixbuf,
			this->hidden->locked_buf,
			this->spec.size, NULL, 0);
	}
}

static Uint8*
DSOUND_GetDeviceBuf(_THIS)
{
	DWORD cursor = 0;
	DWORD junk = 0;
	HRESULT result = DS_OK;
	DWORD rawlen = 0;

	/* Figure out which blocks to fill next */
	this->hidden->locked_buf = NULL;
	result = IDirectSoundBuffer_GetCurrentPosition(this->hidden->mixbuf,
		&junk, &cursor);
#ifndef _XBOX
	if (result == DSERR_BUFFERLOST) {
		IDirectSoundBuffer_Restore(this->hidden->mixbuf);
		result = IDirectSoundBuffer_GetCurrentPosition(this->hidden->mixbuf,
			&junk, &cursor);
	}
#endif
	if (result != DS_OK) {
		SetDSerror("DirectSound GetCurrentPosition", result);
		return (NULL);
	}
	cursor /= this->spec.size;
#ifdef DEBUG_SOUND
	/* Detect audio dropouts */
	{
		DWORD spot = cursor;
		if (spot < this->hidden->lastchunk) {
			spot += this->hidden->num_buffers;
		}
		if (spot > this->hidden->lastchunk + 1) {
			fprintf(stderr, "Audio dropout, missed %d fragments\n",
				(spot - (this->hidden->lastchunk + 1)));
		}
	}
#endif
	this->hidden->lastchunk = cursor;
	cursor = (cursor + 1) % this->hidden->num_buffers;
	cursor *= this->spec.size;

	/* Lock the audio buffer */
	result = IDirectSoundBuffer_Lock(this->hidden->mixbuf, cursor,
		this->spec.size,
		(LPVOID*)&this->hidden->locked_buf,
		&rawlen, NULL, /*&junk*/NULL, 0);
#ifndef _XBOX
	if (result == DSERR_BUFFERLOST) {
		IDirectSoundBuffer_Restore(this->hidden->mixbuf);
		result = IDirectSoundBuffer_Lock(this->hidden->mixbuf, cursor,
			this->spec.size,
			(LPVOID*)&this->
			hidden->locked_buf, &rawlen, NULL,
			&junk, 0);
	}
#endif
	if (result != DS_OK) {
		SetDSerror("DirectSound Lock", result);
		return (NULL);
	}
	return (this->hidden->locked_buf);
}

static int
DSOUND_CaptureFromDevice(_THIS, void* buffer, int buflen)
{
#ifndef _XBOX
	struct SDL_PrivateAudioData* h = this->hidden;
	DWORD junk, cursor, ptr1len, ptr2len;
	VOID* ptr1, * ptr2;

	SDL_assert(buflen == this->spec.size);

	while (SDL_TRUE) {
		if (SDL_AtomicGet(&this->shutdown)) {  /* in case the buffer froze... */
			SDL_memset(buffer, this->spec.silence, buflen);
			return buflen;
		}

		if (IDirectSoundCaptureBuffer_GetCurrentPosition(h->capturebuf, &junk, &cursor) != DS_OK) {
			return -1;
		}
		if ((cursor / this->spec.size) == h->lastchunk) {
			SDL_Delay(1);  /* FIXME: find out how much time is left and sleep that long */
		}
		else {
			break;
		}
	}

	if (IDirectSoundCaptureBuffer_Lock(h->capturebuf, h->lastchunk * this->spec.size, this->spec.size, &ptr1, &ptr1len, &ptr2, &ptr2len, 0) != DS_OK) {
		return -1;
	}

	SDL_assert(ptr1len == this->spec.size);
	SDL_assert(ptr2 == NULL);
	SDL_assert(ptr2len == 0);

	SDL_memcpy(buffer, ptr1, ptr1len);

	if (IDirectSoundCaptureBuffer_Unlock(h->capturebuf, ptr1, ptr1len, ptr2, ptr2len) != DS_OK) {
		return -1;
	}

	h->lastchunk = (h->lastchunk + 1) % h->num_buffers;

	return ptr1len;
#else
	return 0;
#endif //_XBOX
}

static void
DSOUND_FlushCapture(_THIS)
{
#ifndef _XBOX // No capture on Xbox
	struct SDL_PrivateAudioData* h = this->hidden;
	DWORD junk, cursor;
	if (IDirectSoundCaptureBuffer_GetCurrentPosition(h->capturebuf, &junk, &cursor) == DS_OK) {
		h->lastchunk = cursor / this->spec.size;
	}
#endif
}

static void
DSOUND_CloseDevice(_THIS)
{
	if (this->hidden->mixbuf != NULL) {
		// Ensure the buffer is stopped before release
		IDirectSoundBuffer_Stop(this->hidden->mixbuf);

		// Wait for the buffer to stop (if necessary)
		DWORD status = 0;
		IDirectSoundBuffer_GetStatus(this->hidden->mixbuf, &status);
		while (status & DSBSTATUS_PLAYING) {
			SDL_Delay(10);
			IDirectSoundBuffer_GetStatus(this->hidden->mixbuf, &status);
		}

		// Release the buffer
		IDirectSoundBuffer_Release(this->hidden->mixbuf);
		this->hidden->mixbuf = NULL;
	}
	if (this->hidden->sound != NULL) {
		IDirectSound_Release(this->hidden->sound);
		this->hidden->sound = NULL;
	}
#ifndef _XBOX // No capture on Xbox
	if (this->hidden->capturebuf != NULL) {
		IDirectSoundCaptureBuffer_Stop(this->hidden->capturebuf);
		IDirectSoundCaptureBuffer_Release(this->hidden->capturebuf);
		this->hidden->capturebuf = NULL;
	}
	if (this->hidden->capture != NULL) {
		IDirectSoundCapture_Release(this->hidden->capture);
		this->hidden->capture = NULL;
	}
#endif
	SDL_free(this->hidden);
	this->hidden = NULL;
}

/* This function tries to create a secondary audio buffer, and returns the
   number of audio chunks available in the created buffer. This is for
   playback devices, not capture.
*/
static int
CreateSecondary(_THIS, const DWORD bufsize, WAVEFORMATEX* wfmt)
{
	LPDIRECTSOUND sndObj = this->hidden->sound;
	LPDIRECTSOUNDBUFFER* sndbuf = &this->hidden->mixbuf;
	HRESULT result = DS_OK;
	DSBUFFERDESC format;
	LPVOID pvAudioPtr1, pvAudioPtr2;
	DWORD dwAudioBytes1, dwAudioBytes2;
#ifdef _XBOX
	DSMIXBINVOLUMEPAIR dsmbvp[8] = {
   {DSMIXBIN_FRONT_LEFT, DSBVOLUME_MAX},
   {DSMIXBIN_FRONT_RIGHT, DSBVOLUME_MAX},
   {DSMIXBIN_FRONT_CENTER, DSBVOLUME_MAX},
   {DSMIXBIN_FRONT_CENTER, DSBVOLUME_MAX},
   {DSMIXBIN_BACK_LEFT, DSBVOLUME_MAX},
   {DSMIXBIN_BACK_RIGHT, DSBVOLUME_MAX},
   {DSMIXBIN_LOW_FREQUENCY, DSBVOLUME_MAX},
   {DSMIXBIN_LOW_FREQUENCY, DSBVOLUME_MAX} };

	DSMIXBINS dsmb;
#endif

	/* Try to create the secondary buffer */
	SDL_zero(format);
	format.dwSize = sizeof(format);
#ifndef _XBOX
	format.dwFlags = DSBCAPS_GETCURRENTPOSITION2;
	format.dwFlags |= DSBCAPS_GLOBALFOCUS;
#endif
	format.dwBufferBytes = bufsize;
	format.lpwfxFormat = wfmt;
	result = IDirectSound_CreateSoundBuffer(sndObj, &format, sndbuf, NULL);
	if (result != DS_OK) {
		return SetDSerror("DirectSound CreateSoundBuffer", result);
	}
	IDirectSoundBuffer_SetFormat(*sndbuf, wfmt);

	/* Silence the initial audio buffer */
	result = IDirectSoundBuffer_Lock(*sndbuf, 0, format.dwBufferBytes,
		(LPVOID*)&pvAudioPtr1, &dwAudioBytes1,
		(LPVOID*)&pvAudioPtr2, &dwAudioBytes2,
		DSBLOCK_ENTIREBUFFER);
	if (result == DS_OK) {
		SDL_memset(pvAudioPtr1, this->spec.silence, dwAudioBytes1);
		IDirectSoundBuffer_Unlock(*sndbuf,
			(LPVOID)pvAudioPtr1, dwAudioBytes1,
			(LPVOID)pvAudioPtr2, dwAudioBytes2);
	}

#ifdef _XBOX
	dsmb.dwMixBinCount = 8;
	dsmb.lpMixBinVolumePairs = dsmbvp;

	IDirectSoundBuffer_SetHeadroom(*sndbuf, DSBHEADROOM_MIN);
	IDirectSoundBuffer_SetMixBins(*sndbuf, &dsmb);

	IDirectSoundBuffer_SetVolume(*sndbuf, DSBVOLUME_MAX);
#endif

	/* We're ready to go */
	return 0;
}

/* This function tries to create a capture buffer, and returns the
   number of audio chunks available in the created buffer. This is for
   capture devices, not playback.
*/
static int
CreateCaptureBuffer(_THIS, const DWORD bufsize, WAVEFORMATEX* wfmt)
{
#ifndef _XBOX
	LPDIRECTSOUNDCAPTURE capture = this->hidden->capture;
	LPDIRECTSOUNDCAPTUREBUFFER* capturebuf = &this->hidden->capturebuf;
	DSCBUFFERDESC format;
	HRESULT result;

	SDL_zero(format);
	format.dwSize = sizeof(format);
	format.dwFlags = DSCBCAPS_WAVEMAPPED;
	format.dwBufferBytes = bufsize;
	format.lpwfxFormat = wfmt;

	result = IDirectSoundCapture_CreateCaptureBuffer(capture, &format, capturebuf, NULL);
	if (result != DS_OK) {
		return SetDSerror("DirectSound CreateCaptureBuffer", result);
	}

	result = IDirectSoundCaptureBuffer_Start(*capturebuf, DSCBSTART_LOOPING);
	if (result != DS_OK) {
		IDirectSoundCaptureBuffer_Release(*capturebuf);
		return SetDSerror("DirectSound Start", result);
	}

#if 0
	/* presumably this starts at zero, but just in case... */
	result = IDirectSoundCaptureBuffer_GetCurrentPosition(*capturebuf, &junk, &cursor);
	if (result != DS_OK) {
		IDirectSoundCaptureBuffer_Stop(*capturebuf);
		IDirectSoundCaptureBuffer_Release(*capturebuf);
		return SetDSerror("DirectSound GetCurrentPosition", result);
	}

	this->hidden->lastchunk = cursor / this->spec.size;
#endif
#endif //_XBOX

	return 0;
}

static int
DSOUND_OpenDevice(_THIS, void* handle, const char* devname, int iscapture)
{
	const DWORD numchunks = 8;
	HRESULT result;
	SDL_bool valid_format = SDL_FALSE;
	SDL_bool tried_format = SDL_FALSE;
	SDL_AudioFormat test_format = SDL_FirstAudioFormat(this->spec.format);
	LPGUID guid = (LPGUID)handle;
	DWORD bufsize;

	/* Initialize all variables that we clean on shutdown */
	this->hidden = (struct SDL_PrivateAudioData*)
		SDL_malloc((sizeof * this->hidden));
	if (this->hidden == NULL) {
		return SDL_OutOfMemory();
	}
	SDL_zerop(this->hidden);

	/* Open the audio device */
	if (iscapture) {
#ifdef _XBOX
		return SDL_SetError("DirectSound: Capture not supported on Xbox");
#else
		result = pDirectSoundCaptureCreate8(guid, &this->hidden->capture, NULL);
		if (result != DS_OK) {
			return SetDSerror("DirectSoundCaptureCreate8", result);
		}
#endif
	}
	else {
		result = DirectSoundCreate(NULL, &this->hidden->sound, NULL);
		if (result != DS_OK) {
			return SetDSerror("DirectSoundCreate8", result);
		}
#ifndef _XBOX
		result = IDirectSound_SetCooperativeLevel(this->hidden->sound,
			GetDesktopWindow(),
			DSSCL_NORMAL);
		if (result != DS_OK) {
			return SetDSerror("DirectSound SetCooperativeLevel", result);
		}
#endif
	}

	while ((!valid_format) && (test_format)) {
		switch (test_format) {
		case AUDIO_U8:
		case AUDIO_S16:
		case AUDIO_S32:
		case AUDIO_F32:
			tried_format = SDL_TRUE;

			this->spec.format = test_format;

			/* Update the fragment size as size in bytes */
			SDL_CalculateAudioSpec(&this->spec);

			bufsize = numchunks * this->spec.size;
			if ((bufsize < DSBSIZE_MIN) || (bufsize > DSBSIZE_MAX)) {
				SDL_SetError("Sound buffer size must be between %d and %d",
					(int)((DSBSIZE_MIN < numchunks) ? 1 : DSBSIZE_MIN / numchunks),
					(int)(DSBSIZE_MAX / numchunks));
			}
			else {
				int rc;
				WAVEFORMATEX wfmt;
				SDL_zero(wfmt);
				if (SDL_AUDIO_ISFLOAT(this->spec.format)) {
					wfmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
				}
				else {
					wfmt.wFormatTag = WAVE_FORMAT_PCM;
				}

				wfmt.wBitsPerSample = SDL_AUDIO_BITSIZE(this->spec.format);
				wfmt.nChannels = this->spec.channels;
				wfmt.nSamplesPerSec = this->spec.freq;
				wfmt.nBlockAlign = wfmt.nChannels * (wfmt.wBitsPerSample / 8);
				wfmt.nAvgBytesPerSec = wfmt.nSamplesPerSec * wfmt.nBlockAlign;

				rc = iscapture ? CreateCaptureBuffer(this, bufsize, &wfmt) : CreateSecondary(this, bufsize, &wfmt);
				if (rc == 0) {
					this->hidden->num_buffers = numchunks;
					valid_format = SDL_TRUE;
				}
			}
			break;
		}
		test_format = SDL_NextAudioFormat();
	}

	if (!valid_format) {
		if (tried_format) {
			return -1;  /* CreateSecondary() should have called SDL_SetError(). */
		}
		return SDL_SetError("DirectSound: Unsupported audio format");
	}

	/* Playback buffers will auto-start playing in DSOUND_WaitDevice() */

	return 0;                   /* good to go. */
}

static void
DSOUND_Deinitialize(void)
{
	DSOUND_Unload();
}

static int
DSOUND_Init(SDL_AudioDriverImpl* impl)
{
	if (!DSOUND_Load()) {
		return 0;
	}

	/* Set the function pointers */
	impl->DetectDevices = DSOUND_DetectDevices;
	impl->OpenDevice = DSOUND_OpenDevice;
	impl->PlayDevice = DSOUND_PlayDevice;
	impl->WaitDevice = DSOUND_WaitDevice;
	impl->GetDeviceBuf = DSOUND_GetDeviceBuf;
	impl->CaptureFromDevice = DSOUND_CaptureFromDevice;
	impl->FlushCapture = DSOUND_FlushCapture;
	impl->CloseDevice = DSOUND_CloseDevice;
	impl->FreeDeviceHandle = DSOUND_FreeDeviceHandle;
	impl->Deinitialize = DSOUND_Deinitialize;

	impl->HasCaptureSupport = SDL_FALSE;

	return 1;   /* this audio target is available. */
}

AudioBootStrap DSOUND_bootstrap = {
	"directsound", "DirectSound", DSOUND_Init, 0
};

#endif /* SDL_AUDIO_DRIVER_DSOUND */

/* vi: set ts=4 sw=4 expandtab: */