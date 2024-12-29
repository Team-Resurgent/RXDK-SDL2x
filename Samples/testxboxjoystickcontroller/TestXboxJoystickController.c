#include "SDL.h"
#include <math.h>
#include <stdio.h>

/* If your compiler doesn't define M_PI, do so here: */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WINDOW_WIDTH  640
#define WINDOW_HEIGHT 480

/*
   Draw a circle outline by connecting small line segments.
   - centerX, centerY: circle center
   - radius: how large the circle is
   - stepDegrees: how many degrees between line segments (smaller => smoother)
*/
void DrawCircleOutline(SDL_Renderer* renderer, int centerX, int centerY,
    float radius, float stepDegrees)
{
    float prevX = centerX + radius;  // angle=0 => point is (centerX+radius, centerY)
    float prevY = centerY;

    for (float angle = stepDegrees; angle <= 360.0f; angle += stepDegrees) {
        float rad = angle * (float)M_PI / 180.0f;
        float x = centerX + radius * cosf(rad);
        float y = centerY + radius * sinf(rad);

        SDL_RenderDrawLine(renderer, (int)prevX, (int)prevY, (int)x, (int)y);

        prevX = x;
        prevY = y;
    }
}

/*
   Draw a polygon by connecting consecutive points, plus the last to the first.
   - points: array of (x,y) pairs
   - count: how many points in the array
*/
void DrawPolygonOutline(SDL_Renderer* renderer, const SDL_Point* points, int count)
{
    if (count < 2) return;

    // Draw lines between consecutive points
    for (int i = 0; i < count - 1; i++) {
        SDL_RenderDrawLine(renderer, points[i].x, points[i].y,
            points[i + 1].x, points[i + 1].y);
    }
    // Connect last back to first
    SDL_RenderDrawLine(renderer, points[count - 1].x, points[count - 1].y,
        points[0].x, points[0].y);
}

int main(int argc, char* argv[])
{
    /* 1) Initialize SDL (video) */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    /* 2) Create a window (640×480) and renderer */
    SDL_Window* window = SDL_CreateWindow("Render Shapes Demo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* Use accelerated or software as you wish; switch if you suspect GPU issues */
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_Log("Failed to create renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* 3) Main loop: draw shapes each frame until quit */
    int running = 1;
    SDL_Event event;
    while (running) {
        /* Handle events */
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }

        /* --- Start drawing --- */

        // Clear screen to black
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        /* 1) Draw a red line from top-left (0,0) to bottom-right (640,480) */
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_RenderDrawLine(renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

        /* 2) Draw a filled green rectangle in the center */
        SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
        SDL_Rect filledRect;
        filledRect.w = 100;
        filledRect.h = 80;
        filledRect.x = (WINDOW_WIDTH - filledRect.w) / 2; // center horizontally
        filledRect.y = (WINDOW_HEIGHT - filledRect.h) / 2; // center vertically
        SDL_RenderFillRect(renderer, &filledRect);

        /* 3) Draw an outlined blue rectangle in the top-left corner */
        SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
        SDL_Rect outlinedRect;
        outlinedRect.x = 20;
        outlinedRect.y = 20;
        outlinedRect.w = 80;
        outlinedRect.h = 60;
        SDL_RenderDrawRect(renderer, &outlinedRect);

        /* 4) Draw a yellow circle outline near bottom-right corner */
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255); // yellow
        int circleCenterX = WINDOW_WIDTH - 60;  // ~ 580
        int circleCenterY = WINDOW_HEIGHT - 60;  // ~ 420
        DrawCircleOutline(renderer, circleCenterX, circleCenterY, 40.0f, 4.0f);

        /* 5) Draw a white pentagon near middle-right using lines between points */
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_Point pentagon[5];
        // Let’s place it around (x=500,y=200) with a small shape
        pentagon[0].x = 500; pentagon[0].y = 180;
        pentagon[1].x = 540; pentagon[1].y = 190;
        pentagon[2].x = 560; pentagon[2].y = 220;
        pentagon[3].x = 520; pentagon[3].y = 240;
        pentagon[4].x = 480; pentagon[4].y = 220;
        DrawPolygonOutline(renderer, pentagon, 5);

        /* Present all shapes */
        SDL_RenderPresent(renderer);

        /* Delay ~16 ms => ~60 fps */
        SDL_Delay(16);
    }

    /* 4) Cleanup and quit */
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}