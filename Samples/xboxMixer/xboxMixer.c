#include <SDL.h>
#include <SDL_version.h>
#include <SDL_mixer.h>

#ifndef __cplusplus
typedef int bool;
#define true  1
#define false 0
#endif

#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 480

void LogSDLVersions(void)
{
    SDL_version compiled;
    SDL_version linked;

    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);

    SDL_Log("SDL compiled against: %u.%u.%u", compiled.major, compiled.minor, compiled.patch);
    SDL_Log("SDL linked (runtime): %u.%u.%u", linked.major, linked.minor, linked.patch);
    SDL_Log("SDL revision: %s", SDL_GetRevision());
}

void LogSDLMixerVersions(void)
{
    const SDL_version* linked = Mix_Linked_Version();
    SDL_Log("SDL_mixer compiled against: %d.%d.%d",
        SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_PATCHLEVEL);
    SDL_Log("SDL_mixer linked (runtime): %d.%d.%d",
        linked->major, linked->minor, linked->patch);
}

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

int main(int argc, char* argv[])
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return -1;
    }

    LogSDLVersions();
    LogSDLMixerVersions();

    /* ask SDL_mixer for MP3 support explicitly */
    int initted = Mix_Init(MIX_INIT_MP3);
    if ((initted & MIX_INIT_MP3) == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_mixer: MP3 support not available: %s", Mix_GetError());
        /* you can continue, but MP3 won't load */
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == -1) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Mix_OpenAudio failed: %s", Mix_GetError());
        return -1;
    }

    SDL_Window* win = SDL_CreateWindow("xboxMixer", 0, 0,
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    SDL_Texture* bg = LoadTextureFromFile(ren, "D:\\Lose_my_breath.bmp");

    /* try to load the music */
    const char* music_path = "D:\\No Scrubs.mp3";
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

    /* optional: see what decoders we actually have */
    {
        int i, n = Mix_GetNumMusicDecoders();
        SDL_Log("music decoders:");
        for (i = 0; i < n; ++i) {
            SDL_Log("  %s", Mix_GetMusicDecoder(i));
        }
    }

    /* your Xbox DSOUND calls */
    SDL_XboxDSound_SetStereo(0, 0);

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
        }

        SDL_RenderClear(ren);
        if (bg) SDL_RenderCopy(ren, bg, NULL, NULL);
        SDL_RenderPresent(ren);

        SDL_Delay(16);
    }

    return 0;
}
