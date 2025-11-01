/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

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

#ifdef SDL_AUDIO_DRIVER_DSOUND

/* Allow access to a raw mixing buffer */

#include "SDL_timer.h"
#ifndef _XBOX
#include "SDL_loadso.h"
#endif
#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_directsound.h"
#ifndef _XBOX
#include <mmreg.h>
#endif

#ifdef HAVE_MMDEVICEAPI_H
#include "../../core/windows/SDL_immdevice.h"
#endif /* HAVE_MMDEVICEAPI_H */

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif

/* For Vista+, we can enumerate DSound devices with IMMDevice */
#ifdef HAVE_MMDEVICEAPI_H
static SDL_bool SupportsIMMDevice = SDL_FALSE;
#endif /* HAVE_MMDEVICEAPI_H */

/* DirectX function pointers for audio */
#ifndef _XBOX // We will acccess directly
static void *DSoundDLL = NULL;
typedef HRESULT(WINAPI *fnDirectSoundCreate8)(LPGUID, LPDIRECTSOUND *, LPUNKNOWN);
typedef HRESULT(WINAPI *fnDirectSoundEnumerateW)(LPDSENUMCALLBACKW, LPVOID);
typedef HRESULT(WINAPI *fnDirectSoundCaptureCreate8)(LPCGUID, LPDIRECTSOUNDCAPTURE8 *, LPUNKNOWN);
typedef HRESULT(WINAPI *fnDirectSoundCaptureEnumerateW)(LPDSENUMCALLBACKW, LPVOID);
static fnDirectSoundCreate8 pDirectSoundCreate8 = NULL;
static fnDirectSoundEnumerateW pDirectSoundEnumerateW = NULL;
static fnDirectSoundCaptureCreate8 pDirectSoundCaptureCreate8 = NULL;
static fnDirectSoundCaptureEnumerateW pDirectSoundCaptureEnumerateW = NULL;

static const GUID SDL_KSDATAFORMAT_SUBTYPE_PCM = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID SDL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
#endif



static void DSOUND_Unload(void)
{
#ifndef _XBOX	
    pDirectSoundCreate8 = NULL;
    pDirectSoundEnumerateW = NULL;
    pDirectSoundCaptureCreate8 = NULL;
    pDirectSoundCaptureEnumerateW = NULL;

    if (DSoundDLL) {
        SDL_UnloadObject(DSoundDLL);
        DSoundDLL = NULL;
    }
#endif	
}

static int DSOUND_Load(void)
{
    int loaded = 0;

    DSOUND_Unload();
#ifndef _XBOX
    DSoundDLL = SDL_LoadObject("DSOUND.DLL");
    if (!DSoundDLL) {
        SDL_SetError("DirectSound: failed to load DSOUND.DLL");
    } else {
/* Now make sure we have DirectX 8 or better... */
#define DSOUNDLOAD(f)                                  \
    {                                                  \
        p##f = (fn##f)SDL_LoadFunction(DSoundDLL, #f); \
        if (!p##f)                                     \
            loaded = 0;                                \
    }
        loaded = 1; /* will reset if necessary. */
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

static int SetDSerror(const char *function, HRESULT code)
{
    const char *error = NULL;

    /* Use #ifdef so we don't reference constants that XDK's dsound.h doesn't define */
#ifdef E_NOINTERFACE
    if (code == E_NOINTERFACE) {
        error = "Unsupported interface -- Is DirectX 8.0 or later installed?";
    } else
#endif
#ifdef DSERR_ALLOCATED
    if (code == DSERR_ALLOCATED) {
        error = "Audio device in use";
    } else
#endif
#ifdef DSERR_BADFORMAT
    if (code == DSERR_BADFORMAT) {
        error = "Unsupported audio format";
    } else
#endif
#ifdef DSERR_BUFFERLOST
    if (code == DSERR_BUFFERLOST) {
        error = "Mixing buffer was lost";
    } else
#endif
#ifdef DSERR_CONTROLUNAVAIL
    if (code == DSERR_CONTROLUNAVAIL) {
        error = "Control requested is not available";
    } else
#endif
#ifdef DSERR_INVALIDCALL
    if (code == DSERR_INVALIDCALL) {
        error = "Invalid call for the current state";
    } else
#endif
#ifdef DSERR_INVALIDPARAM
    if (code == DSERR_INVALIDPARAM) {
        error = "Invalid parameter";
    } else
#endif
#ifdef DSERR_NODRIVER
    if (code == DSERR_NODRIVER) {
        error = "No audio device found";
    } else
#endif
#ifdef DSERR_OUTOFMEMORY
    if (code == DSERR_OUTOFMEMORY) {
        error = "Out of memory";
    } else
#endif
#ifdef DSERR_PRIOLEVELNEEDED
    if (code == DSERR_PRIOLEVELNEEDED) {
        error = "Caller doesn't have priority";
    } else
#endif
#ifdef DSERR_UNSUPPORTED
    if (code == DSERR_UNSUPPORTED) {
        error = "Function not supported";
    } else
#endif
    {
        error = "Unknown DirectSound error";
    }

    /* Print the HRESULT as 0xXXXXXXXX (always 8 hex digits) */
    return SDL_SetError("%s: %s (0x%08lX)", function, error, (unsigned long)(ULONG_PTR)code);
}

static void DSOUND_FreeDeviceHandle(void *handle)
{
    SDL_free(handle);
}

static int DSOUND_GetDefaultAudioInfo(char **name, SDL_AudioSpec *spec, int iscapture)
{
#ifdef HAVE_MMDEVICEAPI_H
    if (SupportsIMMDevice) {
        return SDL_IMMDevice_GetDefaultAudioInfo(name, spec, iscapture);
    }
#endif /* HAVE_MMDEVICEAPI_H */
    return SDL_Unsupported();
}

#ifndef _XBOX	
static BOOL CALLBACK FindAllDevs(LPGUID guid, LPCWSTR desc, LPCWSTR module, LPVOID data)
{
    const int iscapture = (int)((size_t)data);
    if (guid != NULL) { /* skip default device */
        char *str = WIN_LookupAudioDeviceName(desc, guid);
        if (str) {
            LPGUID cpyguid = (LPGUID)SDL_malloc(sizeof(GUID));
            SDL_memcpy(cpyguid, guid, sizeof(GUID));

            /* Note that spec is NULL, because we are required to connect to the
             * device before getting the channel mask and output format, making
             * this information inaccessible at enumeration time
             */
            SDL_AddAudioDevice(iscapture, str, NULL, cpyguid);
            SDL_free(str); /* addfn() makes a copy of this string. */
        }
    }
    return TRUE; /* keep enumerating. */
}
#endif

static void DSOUND_DetectDevices(void)
{
#ifdef HAVE_MMDEVICEAPI_H
    if (SupportsIMMDevice) {
        SDL_IMMDevice_EnumerateEndpoints(SDL_TRUE);
    } else {
#endif /* HAVE_MMDEVICEAPI_H */
#ifndef _XBOX
        pDirectSoundCaptureEnumerateW(FindAllDevs, (void *)((size_t)1));
        pDirectSoundEnumerateW(FindAllDevs, (void *)((size_t)0));
#endif			
#ifdef HAVE_MMDEVICEAPI_H
    }
#endif /* HAVE_MMDEVICEAPI_H*/
}

static void DSOUND_WaitDevice(_THIS)
{
    DWORD status = 0;
    DWORD cursor = 0;
    DWORD junk = 0;
    HRESULT result = DS_OK;

    /* Semi-busy wait, since we have no play notification on a primary HW buffer */
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

    /* Sleep until playback advances to the next chunk */
    const DWORD chunkSize = this->spec.size;          /* bytes per “mix chunk” */
    const DWORD lastChunk = this->hidden->lastchunk;  /* index of last mixed chunk */

    while ((cursor / chunkSize) == lastChunk) {
        /* ---- OG Xbox: compute remaining time precisely; avoid tight 1ms spin ---- */
#ifdef _XBOX
        const DWORD remainingBytes   = chunkSize - (cursor % chunkSize);
        const DWORD sampleRate       = this->spec.freq;
        const DWORD channels         = this->spec.channels;
        const DWORD bytesPerSample   = (DWORD)(SDL_AUDIO_BITSIZE(this->spec.format) / 8);
        DWORD       remainingTimeMs  = 1;

        if (sampleRate && channels && bytesPerSample) {
            /* remaining ms = bytes / (bytes per second) * 1000 */
            const DWORD bytesPerSec = sampleRate * channels * bytesPerSample;
            if (bytesPerSec) {
                remainingTimeMs = (remainingBytes * 1000u) / bytesPerSec;
                if (remainingTimeMs == 0) remainingTimeMs = 1;      /* never 0 */
                if (remainingTimeMs > 10) remainingTimeMs = 10;     /* cap to keep latency snappy */
            }
        }
        SDL_Delay(remainingTimeMs);
#else
        /* Desktop path: legacy 1ms tick */
        SDL_Delay(1);
#endif

        /* Try to restore a lost sound buffer (desktop Windows semantics) */
        IDirectSoundBuffer_GetStatus(this->hidden->mixbuf, &status);
#ifndef _XBOX
        if (status & DSBSTATUS_BUFFERLOST) {
            IDirectSoundBuffer_Restore(this->hidden->mixbuf);
            IDirectSoundBuffer_GetStatus(this->hidden->mixbuf, &status);
            if (status & DSBSTATUS_BUFFERLOST) {
                break;
            }
        }
#endif

        /* If the buffer isn’t playing, try to restart it (looping) */
        if (!(status & DSBSTATUS_PLAYING)) {
            result = IDirectSoundBuffer_Play(this->hidden->mixbuf, 0, 0, DSBPLAY_LOOPING);
            if (result != DS_OK) {
#ifdef DEBUG_SOUND
                SetDSerror("DirectSound Play", result);
#endif
                return;
            }
            /* continue loop to re-check cursor/chunk */
        }

        /* Re-check current play position */
        result = IDirectSoundBuffer_GetCurrentPosition(this->hidden->mixbuf, &junk, &cursor);
        if (result != DS_OK) {
#ifdef DEBUG_SOUND
            SetDSerror("DirectSound GetCurrentPosition", result);
#endif
            return;
        }
    }
}


static void DSOUND_PlayDevice(_THIS)
{
    /* Unlock the buffer, allowing it to play */
    if (this->hidden->locked_buf) {
        IDirectSoundBuffer_Unlock(this->hidden->mixbuf,
                                  this->hidden->locked_buf,
                                  this->spec.size, NULL, 0);
    }
}

static Uint8 *DSOUND_GetDeviceBuf(_THIS)
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
        return NULL;
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
                                     (LPVOID *)&this->hidden->locked_buf,
                                     &rawlen, NULL, &junk, 0);
#ifndef _XBOX									 
    if (result == DSERR_BUFFERLOST) {
        IDirectSoundBuffer_Restore(this->hidden->mixbuf);
        result = IDirectSoundBuffer_Lock(this->hidden->mixbuf, cursor,
                                         this->spec.size,
                                         (LPVOID *)&this->hidden->locked_buf, &rawlen, NULL,
                                         &junk, 0);
    }
#endif	
    if (result != DS_OK) {
        SetDSerror("DirectSound Lock", result);
        return NULL;
    }
    return this->hidden->locked_buf;
}

static int DSOUND_CaptureFromDevice(_THIS, void *buffer, int buflen)
{
#ifndef _XBOX	
    struct SDL_PrivateAudioData *h = this->hidden;
    DWORD junk, cursor, ptr1len, ptr2len;
    VOID *ptr1, *ptr2;

    SDL_assert(buflen == this->spec.size);

    while (SDL_TRUE) {
        if (SDL_AtomicGet(&this->shutdown)) { /* in case the buffer froze... */
            SDL_memset(buffer, this->spec.silence, buflen);
            return buflen;
        }

        if (IDirectSoundCaptureBuffer_GetCurrentPosition(h->capturebuf, &junk, &cursor) != DS_OK) {
            return -1;
        }
        if ((cursor / this->spec.size) == h->lastchunk) {
            SDL_Delay(1); /* FIXME: find out how much time is left and sleep that long */
        } else {
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

static void DSOUND_FlushCapture(_THIS)
{
#ifndef _XBOX // No capture on Xbox	
    struct SDL_PrivateAudioData *h = this->hidden;
    DWORD junk, cursor;
    if (IDirectSoundCaptureBuffer_GetCurrentPosition(h->capturebuf, &junk, &cursor) == DS_OK) {
        h->lastchunk = cursor / this->spec.size;
    }
#endif	
}

static void DSOUND_CloseDevice(_THIS)
{
    if (!this->hidden) {
        return;
    }

    if (this->hidden->mixbuf) {
        /* Stop playback first */
        IDirectSoundBuffer_Stop(this->hidden->mixbuf);

#ifdef _XBOX
        /* OG Xbox: give the HW a moment to settle so Release is clean */
        DWORD status = 0;
        if (SUCCEEDED(IDirectSoundBuffer_GetStatus(this->hidden->mixbuf, &status))) {
            /* wait up to ~500ms total, 10ms steps */
            for (int i = 0; (status & DSBSTATUS_PLAYING) && i < 50; ++i) {
                SDL_Delay(10);
                IDirectSoundBuffer_GetStatus(this->hidden->mixbuf, &status);
            }
        }
#endif

        IDirectSoundBuffer_Release(this->hidden->mixbuf);
        this->hidden->mixbuf = NULL;
    }

    if (this->hidden->sound) {
        IDirectSound_Release(this->hidden->sound);
        this->hidden->sound = NULL;
    }

#ifndef _XBOX  /* No capture on OG Xbox */
    if (this->hidden->capturebuf) {
        IDirectSoundCaptureBuffer_Stop(this->hidden->capturebuf);
        IDirectSoundCaptureBuffer_Release(this->hidden->capturebuf);
        this->hidden->capturebuf = NULL;
    }

    if (this->hidden->capture) {
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
static int CreateSecondary(_THIS, const DWORD bufsize, WAVEFORMATEX *wfmt)
{
    LPDIRECTSOUND sndObj = this->hidden->sound;
    LPDIRECTSOUNDBUFFER *sndbuf = &this->hidden->mixbuf;
    HRESULT result = DS_OK;
    DSBUFFERDESC format;
    LPVOID pvAudioPtr1, pvAudioPtr2;
    DWORD dwAudioBytes1, dwAudioBytes2;

    /* Describe the secondary buffer */
    SDL_zero(format);
    format.dwSize = sizeof(format);
#ifndef _XBOX
    /* Desktop Windows: better cursor behavior + allow background focus */
    format.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
#else
    /* OG Xbox: these flags aren’t meaningful; leave at 0 */
    format.dwFlags = 0;
#endif
    format.dwBufferBytes = bufsize;
    format.lpwfxFormat = wfmt;

    /* Create it */
    result = IDirectSound_CreateSoundBuffer(sndObj, &format, sndbuf, NULL);
    if (result != DS_OK) {
        return SetDSerror("DirectSound CreateSoundBuffer", result);
    }

    /* Ensure format is applied */
    IDirectSoundBuffer_SetFormat(*sndbuf, wfmt);

    /* Pre-silence whole buffer */
    result = IDirectSoundBuffer_Lock(*sndbuf, 0, format.dwBufferBytes,
                                     (LPVOID *)&pvAudioPtr1, &dwAudioBytes1,
                                     (LPVOID *)&pvAudioPtr2, &dwAudioBytes2,
                                     DSBLOCK_ENTIREBUFFER);
    if (result == DS_OK) {
        SDL_memset(pvAudioPtr1, this->spec.silence, dwAudioBytes1);
        IDirectSoundBuffer_Unlock(*sndbuf,
                                  (LPVOID)pvAudioPtr1, dwAudioBytes1,
                                  (LPVOID)pvAudioPtr2, dwAudioBytes2);
    }

#ifdef _XBOX
    /* ---- OG Xbox: route to mixbins, set headroom/volume ---- */
    /* Choose mixbins based on channel count. You can expand this if you drive 5.1. */
    DSMIXBINVOLUMEPAIR pairs[8];
    DSMIXBINS bins;
    SDL_zero(pairs);
    SDL_zero(bins);

    switch (wfmt->nChannels) {
    case 1: /* mono -> front center */
        pairs[0].dwMixBin = DSMIXBIN_FRONT_CENTER;
        pairs[0].lVolume  = DSBVOLUME_MAX;
        bins.dwMixBinCount = 1;
        break;
    case 2: /* stereo */
        pairs[0].dwMixBin = DSMIXBIN_FRONT_LEFT;   pairs[0].lVolume = DSBVOLUME_MAX;
        pairs[1].dwMixBin = DSMIXBIN_FRONT_RIGHT;  pairs[1].lVolume = DSBVOLUME_MAX;
        bins.dwMixBinCount = 2;
        break;
    case 6: /* 5.1 (L R C LFE Ls Rs) */
        pairs[0].dwMixBin = DSMIXBIN_FRONT_LEFT;      pairs[0].lVolume = DSBVOLUME_MAX;
        pairs[1].dwMixBin = DSMIXBIN_FRONT_RIGHT;     pairs[1].lVolume = DSBVOLUME_MAX;
        pairs[2].dwMixBin = DSMIXBIN_FRONT_CENTER;    pairs[2].lVolume = DSBVOLUME_MAX;
        pairs[3].dwMixBin = DSMIXBIN_LOW_FREQUENCY;   pairs[3].lVolume = DSBVOLUME_MAX;
        pairs[4].dwMixBin = DSMIXBIN_BACK_LEFT;       pairs[4].lVolume = DSBVOLUME_MAX;
        pairs[5].dwMixBin = DSMIXBIN_BACK_RIGHT;      pairs[5].lVolume = DSBVOLUME_MAX;
        bins.dwMixBinCount = 6;
        break;
    default: /* fallback: treat as stereo */
        pairs[0].dwMixBin = DSMIXBIN_FRONT_LEFT;   pairs[0].lVolume = DSBVOLUME_MAX;
        pairs[1].dwMixBin = DSMIXBIN_FRONT_RIGHT;  pairs[1].lVolume = DSBVOLUME_MAX;
        bins.dwMixBinCount = 2;
        break;
    }

    bins.lpMixBinVolumePairs = pairs;

    /* Min headroom, explicit bins, max volume */
    IDirectSoundBuffer_SetHeadroom(*sndbuf, DSBHEADROOM_MIN);
    IDirectSoundBuffer_SetMixBins(*sndbuf, &bins);
    IDirectSoundBuffer_SetVolume(*sndbuf, DSBVOLUME_MAX);
#endif

    return 0;
}

/* This function tries to create a capture buffer, and returns the
   number of audio chunks available in the created buffer. This is for
   capture devices, not playback.
*/
static int CreateCaptureBuffer(_THIS, const DWORD bufsize, WAVEFORMATEX *wfmt)
{
#ifndef _XBOX	
    LPDIRECTSOUNDCAPTURE capture = this->hidden->capture;
    LPDIRECTSOUNDCAPTUREBUFFER *capturebuf = &this->hidden->capturebuf;
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

static int DSOUND_OpenDevice(_THIS, const char *devname)
{
    const DWORD numchunks = 8;
    HRESULT result;
    SDL_bool tried_format = SDL_FALSE;
    SDL_bool iscapture = this->iscapture;
    SDL_AudioFormat test_format;
    LPGUID guid = (LPGUID)this->handle;
    DWORD bufsize;

#ifdef _XBOX
    const int xbox_hw_freq = 48000;  /* OG Xbox mixer is 48k */
#endif

    /* Initialize all variables that we clean on shutdown */
    this->hidden = (struct SDL_PrivateAudioData *)SDL_malloc(sizeof(*this->hidden));
    if (!this->hidden) {
        return SDL_OutOfMemory();
    }
    SDL_zerop(this->hidden);

    /* Open the audio device */
    if (iscapture) {
#ifdef _XBOX
        /* No capture on OG Xbox */
        return SDL_SetError("DirectSound: Capture not supported on Xbox");
#else
        result = pDirectSoundCaptureCreate8(guid, &this->hidden->capture, NULL);
        if (result != DS_OK) {
            return SetDSerror("DirectSoundCaptureCreate8", result);
        }
#endif
    } else {
#ifndef _XBOX
        result = pDirectSoundCreate8(guid, &this->hidden->sound, NULL);
        if (result != DS_OK) {
            return SetDSerror("DirectSoundCreate8", result);
        }
        result = IDirectSound_SetCooperativeLevel(this->hidden->sound,
                                                  GetDesktopWindow(),
                                                  DSSCL_NORMAL);
        if (result != DS_OK) {
            return SetDSerror("DirectSound SetCooperativeLevel", result);
        }
#else
        /* OG Xbox: use DirectSoundCreate, no cooperative level. */
        result = DirectSoundCreate(NULL, &this->hidden->sound, NULL);
        if (result != DS_OK) {
            return SetDSerror("DirectSoundCreate", result);
        }
#endif
    }

    for (test_format = SDL_FirstAudioFormat(this->spec.format);
         test_format;
         test_format = SDL_NextAudioFormat())
    {
        switch (test_format) {
        case AUDIO_U8:
        case AUDIO_S16:
        case AUDIO_S32:
        case AUDIO_F32:
            tried_format = SDL_TRUE;

            /* App’s requested format… */
            this->spec.format = test_format;

#ifdef _XBOX
            /* …but back-end opens a hardware-friendly stream; SDL will convert. */
            this->spec.format = AUDIO_S16;
            this->spec.freq   = xbox_hw_freq;
            if (this->spec.channels != 1 && this->spec.channels != 2 && this->spec.channels != 6) {
                this->spec.channels = 2;  /* safe default */
            }
#endif
            /* Update fragment size (spec.size) */
            SDL_CalculateAudioSpec(&this->spec);

            bufsize = numchunks * this->spec.size;

#if defined(DSBSIZE_MIN) && defined(DSBSIZE_MAX)
            if ((bufsize < DSBSIZE_MIN) || (bufsize > DSBSIZE_MAX)) {
                SDL_SetError("Sound buffer size must be between %d and %d",
                             (int)((DSBSIZE_MIN < numchunks) ? 1 : DSBSIZE_MIN / numchunks),
                             (int)(DSBSIZE_MAX / numchunks));
            } else
#endif
            {
                int rc;

#ifndef _XBOX
                /* Desktop: allow WAVEFORMATEXTENSIBLE for >2 channels and/or float */
                WAVEFORMATEXTENSIBLE wfmt;
                SDL_zero(wfmt);

                if (this->spec.channels > 2) {
                    wfmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                    wfmt.Format.cbSize = sizeof(wfmt) - sizeof(WAVEFORMATEX);

                    if (SDL_AUDIO_ISFLOAT(this->spec.format)) {
                        SDL_memcpy(&wfmt.SubFormat, &SDL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof(GUID));
                    } else {
                        SDL_memcpy(&wfmt.SubFormat, &SDL_KSDATAFORMAT_SUBTYPE_PCM, sizeof(GUID));
                    }
                    wfmt.Samples.wValidBitsPerSample = SDL_AUDIO_BITSIZE(this->spec.format);

                    switch (this->spec.channels) {
                    case 3:  wfmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER; break;
                    case 4:  wfmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT; break;
                    case 5:  wfmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT; break;
                    case 6:  wfmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT; break;
                    case 7:  wfmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER; break;
                    case 8:  wfmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT; break;
                    default: SDL_assert(0 && "Unsupported channel count!"); break;
                    }
                } else if (SDL_AUDIO_ISFLOAT(this->spec.format)) {
                    wfmt.Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
                } else {
                    wfmt.Format.wFormatTag = WAVE_FORMAT_PCM;
                }

                wfmt.Format.wBitsPerSample  = SDL_AUDIO_BITSIZE(this->spec.format);
                wfmt.Format.nChannels       = this->spec.channels;
                wfmt.Format.nSamplesPerSec  = this->spec.freq;
                wfmt.Format.nBlockAlign     = wfmt.Format.nChannels * (wfmt.Format.wBitsPerSample / 8);
                wfmt.Format.nAvgBytesPerSec = wfmt.Format.nSamplesPerSec * wfmt.Format.nBlockAlign;

                rc = iscapture
                    ? CreateCaptureBuffer(this, bufsize, (WAVEFORMATEX *)&wfmt)
                    : CreateSecondary(this,   bufsize, (WAVEFORMATEX *)&wfmt);
#else
                /* OG Xbox: plain WAVEFORMATEX, PCM S16 @ 48k, channels 1/2/6 */
                WAVEFORMATEX wfmt;
                SDL_zero(wfmt);

                wfmt.wFormatTag      = WAVE_FORMAT_PCM;
                wfmt.wBitsPerSample  = 16;
                wfmt.nChannels       = this->spec.channels;  /* clamped above */
                wfmt.nSamplesPerSec  = this->spec.freq;      /* 48000 */
                wfmt.nBlockAlign     = wfmt.nChannels * (wfmt.wBitsPerSample / 8);
                wfmt.nAvgBytesPerSec = wfmt.nSamplesPerSec * wfmt.nBlockAlign;

                rc = CreateSecondary(this, bufsize, &wfmt);
#endif

                if (rc == 0) {
                    this->hidden->num_buffers = numchunks;
                    break;
                }
            }
            continue;

        default:
            continue;
        }
        break;
    }

    if (!test_format) {
        if (tried_format) {
            return -1; /* CreateSecondary/CreateCaptureBuffer set SDL error */
        }
        return SDL_SetError("%s: Unsupported audio format", "directsound");
    }

    /* Playback buffers will auto-start (or be restarted) in DSOUND_WaitDevice(). */
    return 0; /* good to go. */
}


static void DSOUND_Deinitialize(void)
{
#ifdef HAVE_MMDEVICEAPI_H
    if (SupportsIMMDevice) {
        SDL_IMMDevice_Quit();
        SupportsIMMDevice = SDL_FALSE;
    }
#endif /* HAVE_MMDEVICEAPI_H */
    DSOUND_Unload();
}

static SDL_bool DSOUND_Init(SDL_AudioDriverImpl *impl)
{
    if (!DSOUND_Load()) {
        return SDL_FALSE;
    }

#ifdef HAVE_MMDEVICEAPI_H
    SupportsIMMDevice = !(SDL_IMMDevice_Init() < 0);
#endif /* HAVE_MMDEVICEAPI_H */

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
    impl->GetDefaultAudioInfo = DSOUND_GetDefaultAudioInfo;

    impl->HasCaptureSupport = SDL_TRUE;
    impl->SupportsNonPow2Samples = SDL_TRUE;

    return SDL_FALSE; /* this audio target is available. */
}

AudioBootStrap DSOUND_bootstrap = {
    "directsound", "DirectSound", DSOUND_Init, SDL_FALSE
};

#endif /* SDL_AUDIO_DRIVER_DSOUND */

/* vi: set ts=4 sw=4 expandtab: */
