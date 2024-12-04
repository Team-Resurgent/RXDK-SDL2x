#include <xtl.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "SDL_test_common.h"

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480
#define NUM_STARS 1000
#define MAX_DEPTH 2000

static SDLTest_CommonState* state;
int done;

typedef struct {
	float x, y, z;       // 3D position of the star
	Uint8 r, g, b;       // Star color
} Star;

Star stars[NUM_STARS];

// Initialize a star with random position and color
void InitStar(Star* star) {
	star->x = (float)(rand() % (WINDOW_WIDTH * 2) - WINDOW_WIDTH);
	star->y = (float)(rand() % (WINDOW_HEIGHT * 2) - WINDOW_HEIGHT);
	star->z = (float)(rand() % MAX_DEPTH + 1);
	star->r = rand() % 256;
	star->g = rand() % 256;
	star->b = rand() % 256;
}

// Initialize all stars
void InitStars() {
	for (int i = 0; i < NUM_STARS; ++i) {
		InitStar(&stars[i]);
	}
}

// Update the position of a star
void UpdateStar(Star* star) {
	star->z -= 10; // Move the star closer (simulate depth)

	// If the star moves past the viewer, reset it
	if (star->z <= 0) {
		InitStar(star);
		star->z = MAX_DEPTH; // Reset at the farthest depth
	}
}

// Update all stars
void UpdateStars() {
	for (int i = 0; i < NUM_STARS; ++i) {
		UpdateStar(&stars[i]);
	}
}

// Draw a single star
void DrawStar(SDL_Renderer* renderer, Star* star) {
	// Project 3D coordinates to 2D screen space
	int screen_x = (int)((star->x / star->z) * 200 +
		(WINDOW_WIDTH / 2));
	int screen_y = (int)((star->y / star->z) * 200 + (WINDOW_HEIGHT / 2));

	// Scale the star's size based on its depth
	int size = (int)((1.0f - star->z / MAX_DEPTH) * 5);

	// Set the star's color
	SDL_SetRenderDrawColor(renderer, star->r, star->g, star->b, 255);

	// Draw the star as a rectangle
	SDL_Rect rect = { screen_x - size / 2, screen_y - size / 2, size, size };
	SDL_RenderFillRect(renderer, &rect);
}

// Draw all stars
void DrawStars(SDL_Renderer* renderer) {
	for (int i = 0; i < NUM_STARS; ++i) {
		DrawStar(renderer, &stars[i]);
	}
}

// Main rendering loop
void loop() {
	SDL_Event event;

	// Handle events
	while (SDL_PollEvent(&event)) {
		SDLTest_CommonEvent(state, &event, &done);
	}

	for (int i = 0; i < state->num_windows; ++i) {
		SDL_Renderer* renderer = state->renderers[i];
		if (state->windows[i] == NULL) {
			continue;
		}

		// Update the simulation
		UpdateStars();

		// Clear the screen (black background)
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);

		// Draw the stars
		DrawStars(renderer);

		// Present the renderer
		SDL_RenderPresent(renderer);
	}
}

// Main entry point
int main(int argc, char* argv[]) {
	srand((unsigned int)time(NULL));

	// Initialize SDLTest framework
	state = SDLTest_CommonCreateState(argv, SDL_INIT_VIDEO);
	if (!state) {
		return 1;
	}
	if (!SDLTest_CommonInit(state)) {
		SDLTest_CommonQuit(state);
		return 2;
	}

	// Initialize stars
	InitStars();

	// Main render loop
	done = 0;
	while (!done) {
		loop();
	}

	// Clean up and quit
	SDLTest_CommonQuit(state);
	return 0;
}