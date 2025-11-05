#include <SDL.h>
#include <SDL_version.h>
#include <SDL_mixer.h>

#ifndef __cplusplus
typedef int bool;
#define true  1
#define false 0
#endif

#define LOGICAL_W 640
#define LOGICAL_H 480
// which joystick button toggles stretch?
// match your driver mapping: XBOX_JOYSTICK_START = 6
#define TOGGLE_BUTTON_INDEX 6

SDL_Texture* LoadTextureFromFile(SDL_Renderer* renderer, const char* filepath)
{
    SDL_Surface* surf = SDL_LoadBMP(filepath);
    if (!surf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_LoadBMP('%s') failed: %s", filepath, SDL_GetError());
        return NULL;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

// build a destination rect that preserves aspect ratio of srcW x srcH
// inside outW x outH, centered, letterboxed/pillarboxed as needed.
static SDL_Rect MakeAspectPreservingDst(int outW, int outH, int srcW, int srcH)
{
    const float srcAspect = (float)srcW / (float)srcH;
    const float dstAspect = (float)outW / (float)outH;
    SDL_Rect dst;

    if (srcAspect > dstAspect) {
        // limit by width
        dst.w = outW;
        dst.h = (int)(outW / srcAspect);
        dst.x = 0;
        dst.y = (outH - dst.h) / 2;
    }
    else {
        // limit by height (4:3 -> 16:9 hits this path, so black bars left/right)
        dst.h = outH;
        dst.w = (int)(outH * srcAspect);
        dst.x = (outW - dst.w) / 2;
        dst.y = 0;
    }
    return dst;
}

int main(int argc, char* argv[])
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return -1;
    }

    /* ask SDL_mixer for MP3 support explicitly */
    int initted = Mix_Init(MIX_INIT_MP3);
    if ((initted & MIX_INIT_MP3) == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_mixer: MP3 support not available: %s", Mix_GetError());
        /* can continue without MP3 */
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == -1) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Mix_OpenAudio failed: %s", Mix_GetError());
        return -1;
    }

    // Window size gets overridden by your Xbox video backend anyway.
    SDL_Window* win = SDL_CreateWindow("xboxMixer", 0, 0,
        LOGICAL_W, LOGICAL_H,
        SDL_WINDOW_SHOWN);

    SDL_Renderer* ren = SDL_CreateRenderer(
        win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");  // linear scaling looks nicer

    SDL_Texture* bg = LoadTextureFromFile(ren, "D:\\Lose_my_breath.bmp");

    // try to load and play music
    const char* music_path = "D:\\lose-my-breath.wav";
    Mix_Music* music = Mix_LoadMUS(music_path);
    if (!music) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Mix_LoadMUS('%s') FAILED: %s", music_path, Mix_GetError());
    }
    else {
        if (Mix_PlayMusic(music, -1) == -1) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Mix_PlayMusic failed: %s", Mix_GetError());
        }
        else {
            SDL_Log("Mix_PlayMusic: playing '%s'", music_path);
        }
    }

    /* your Xbox DSOUND calls */
    SDL_XboxDSound_SetStereo(0, 0);

    // --- NEW: open the first joystick (controller on port 0) ---
    SDL_Joystick* js = NULL;
    if (SDL_NumJoysticks() > 0) {
        js = SDL_JoystickOpen(0);
        if (!js) {
            SDL_Log("Failed to open joystick 0: %s", SDL_GetError());
        }
        else {
            SDL_Log("Opened joystick 0: %s (%d buttons, %d axes)",
                SDL_JoystickName(js),
                SDL_JoystickNumButtons(js),
                SDL_JoystickNumAxes(js));
        }
    }
    else {
        SDL_Log("No joysticks detected.");
    }

    // query the actual output mode (what Xbox is really scanning out)
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    SDL_Log("Display mode reported: %dx%d @ %dHz", dm.w, dm.h, dm.refresh_rate);

    // precompute rects for pillarboxed and stretched
    SDL_Rect dst_aspect = MakeAspectPreservingDst(dm.w, dm.h, LOGICAL_W, LOGICAL_H);
    SDL_Rect dst_stretch = (SDL_Rect){ 0, 0, dm.w, dm.h };

    bool running = true;
    bool stretch = false;

    // track button state for edge detection so we only toggle once per press
    Uint8 prev_toggle_btn = 0;

    while (running) {
        // process SDL events (quit, etc.)
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
        }

        // poll joystick and look for toggle button press
        if (js && SDL_JoystickGetAttached(js)) {
            // read current button state for our chosen toggle button
            Uint8 cur_btn = SDL_JoystickGetButton(js, TOGGLE_BUTTON_INDEX);

            // rising edge: was up, now down -> toggle
            if (cur_btn && !prev_toggle_btn) {
                stretch = !stretch;
                SDL_Log("stretch is now %s", stretch ? "ON (full screen)" : "OFF (preserve aspect)");
            }

            prev_toggle_btn = cur_btn;
        }

        // draw frame
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);  // black background for pillarbox bars
        SDL_RenderSetViewport(ren, NULL);          // full target
        SDL_RenderClear(ren);

        if (bg) {
            const SDL_Rect* dst = stretch ? &dst_stretch : &dst_aspect;
            SDL_RenderCopy(ren, bg, NULL, dst);
        }

        SDL_RenderPresent(ren);

        SDL_Delay(16);  // ~60fps pacing
    }

    // cleanup (not strictly needed on Xbox exit, but good habit)
    if (js) {
        SDL_JoystickClose(js);
    }

    return 0;
}
