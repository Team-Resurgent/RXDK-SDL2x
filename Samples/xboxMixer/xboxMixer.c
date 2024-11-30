#include <SDL.h>
#include <SDL_mixer.h>

typedef enum{
	false,
	true
} bool;

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

SDL_Texture* LoadTextureFromFile(SDL_Renderer* Renderer, char* filepath){
	SDL_Surface* LoadingSurface;
	SDL_Texture* ReturnTexture;

	LoadingSurface = SDL_LoadBMP(filepath);
	ReturnTexture = SDL_CreateTextureFromSurface(Renderer, LoadingSurface);
	free(LoadingSurface);

	return ReturnTexture;
}

int main(int argc, char* argv[]){
	SDL_Window* MainWindow;
	SDL_Renderer* Renderer;
	SDL_Texture* Background;
	Mix_Music* Music;
	
	/* Init SDL */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
		return -1;

	/* Init SDL_Mixer */
	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == -1)
		return -1;
	
	/* Create Render and Window */
	MainWindow = SDL_CreateWindow("xboxMixer", 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
	Renderer = SDL_CreateRenderer(MainWindow, -1, SDL_RENDERER_ACCELERATED);

	/* Load Assets */
	Background = LoadTextureFromFile(Renderer, "D:\\texture.bmp");
	Music = Mix_LoadMUS("D:\\Deja_Vu.wav");
	
	/* Play Music */
	Mix_PlayMusic(Music, -1);

	/* Main GameLoop */
	while (true){
		SDL_RenderCopy(Renderer, Background, NULL, NULL);
		SDL_RenderPresent(Renderer);
	}
	
	return 0;
}