#include "SDL.h"
#include <stdio.h>
#include <math.h>  // for sinf, cosf

/* If your compiler doesn't define M_PI, do so here */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- Helper Joystick name functions (unchanged) --- */
const char* GetXboxButtonName(int button) {
    switch (button) {
    case 0: return "A";
    case 1: return "B";
    case 2: return "X";
    case 3: return "Y";
    case 4: return "Black";
    case 5: return "White";
    case 6: return "Left Trigger";
    case 7: return "Right Trigger";
    case 8: return "Start";
    case 9: return "Back";
    case 10: return "Left Stick Click (L3)";
    case 11: return "Right Stick Click (R3)";
    default: return "Unknown Button";
    }
}

const char* GetHatDirection(Uint8 value) {
    switch (value) {
    case SDL_HAT_UP:        return "D-Pad Up";
    case SDL_HAT_DOWN:      return "D-Pad Down";
    case SDL_HAT_LEFT:      return "D-Pad Left";
    case SDL_HAT_RIGHT:     return "D-Pad Right";
    case SDL_HAT_LEFTUP:    return "D-Pad Up-Left";
    case SDL_HAT_LEFTDOWN:  return "D-Pad Down-Left";
    case SDL_HAT_RIGHTUP:   return "D-Pad Up-Right";
    case SDL_HAT_RIGHTDOWN: return "D-Pad Down-Right";
    case SDL_HAT_CENTERED:  return "D-Pad Centered";
    default:                return "Unknown D-Pad State";
    }
}

const char* GetAxisName(int axis) {
    switch (axis) {
    case 0: return "Left Stick X";
    case 1: return "Left Stick Y";
    case 2: return "Right Stick X";
    case 3: return "Right Stick Y";
    default: return "Unknown Axis";
    }
}

/* Window dimensions */
#define WINDOW_WIDTH  640
#define WINDOW_HEIGHT 480

/*
   Draw a circle outline by connecting small line segments around the perimeter.
   - centerX, centerY: circle center
   - radius: radius in pixels
   - stepDegrees: how many degrees between line segments (smaller => smoother)
*/
void DrawCircleOutline(SDL_Renderer* renderer, int centerX, int centerY, float radius, float stepDegrees)
{
    /* Start at angle=0 => point is (centerX+radius, centerY) */
    float prevX = centerX + radius;
    float prevY = centerY;

    /* We'll convert degrees to radians. */
    float degToRad = (float)(M_PI / 180.0);

    /* Go from stepDegrees up to 360 degrees, connecting consecutive points */
    for (float angle = stepDegrees; angle <= 360.0f; angle += stepDegrees) {
        float rad = angle * degToRad;
        float x = centerX + radius * cos(rad);
        float y = centerY + radius * sin(rad);

        SDL_RenderDrawLine(renderer, (int)prevX, (int)prevY, (int)x, (int)y);

        prevX = x;
        prevY = y;
    }
}

int main(int argc, char* argv[]) {
    /* 1) Initialize SDL (video + joystick + events).
       If your driver uses rumble, you might also do SDL_INIT_HAPTIC. */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) < 0) {
        SDL_Log("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }

    /* 2) Create a window and renderer */
    SDL_Window* window = SDL_CreateWindow("Two Circles + Full Joystick Logging (Rumble on A)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_Log("Failed to create renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    /* 3) Check for joysticks */
    int numJoysticks = SDL_NumJoysticks();
    if (numJoysticks < 1) {
        SDL_Log("No joysticks detected. Waiting for connection...\n");
    }
    else {
        SDL_Log("Joysticks found: %d\n", numJoysticks);
    }

    /* 4) Open all detected joysticks */
    for (int i = 0; i < numJoysticks; i++) {
        if (!SDL_JoystickOpen(i)) {
            SDL_Log("Failed to open joystick %d! SDL_Error: %s\n", i, SDL_GetError());
        }
        else {
            SDL_Log("Joystick %d opened: %s\n", i, SDL_JoystickNameForIndex(i));
        }
    }

    /* We'll track left stick (axes 0,1) and right stick (axes 2,3). Range ~ [-32768..32767]. */
    float leftStickX = 0.0f;
    float leftStickY = 0.0f;
    float rightStickX = 0.0f;
    float rightStickY = 0.0f;

    SDL_Event event;
    int running = 1;

    /* 5) Main loop */
    while (running) {
        /* Event handling */
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = 0;
                break;

            case SDL_JOYBUTTONDOWN:
                SDL_Log("Button %s pressed on joystick %d.\n",
                    GetXboxButtonName(event.jbutton.button),
                    event.jbutton.which);

                /* If the A button (index 0) is pressed, rumble for 1000ms */
                if (event.jbutton.button == 0) {
                    /* Grab the SDL_Joystick* for the joystick that triggered the event */
                    SDL_Joystick* joy = SDL_JoystickFromInstanceID(event.jbutton.which);
                    if (joy) {
                        Uint16 lowFreq = 0x7FFF;  // half strength low freq
                        Uint16 highFreq = 0x7FFF;  // half strength high freq
                        Uint32 duration = 1000;     // one  second

                        /* This calls XBOX_JoystickRumble() in your driver. */
                        if (SDL_JoystickRumble(joy, lowFreq, highFreq, duration) != 0) {
                            SDL_Log("Rumble failed: %s\n", SDL_GetError());
                        }
                        else {
                            SDL_Log("Rumble triggered on joystick %d for 1000 ms.\n", event.jbutton.which);
                        }
                    }
                }
                break;

            case SDL_JOYBUTTONUP:
                SDL_Log("Button %s released on joystick %d.\n",
                    GetXboxButtonName(event.jbutton.button),
                    event.jbutton.which);
                break;

            case SDL_JOYAXISMOTION:
                if (event.jaxis.value < -8000 || event.jaxis.value > 8000) {
                    SDL_Log("Axis %s moved to %d on joystick %d\n",
                        GetAxisName(event.jaxis.axis),
                        event.jaxis.value,
                        event.jaxis.which);
                }
                /* Store the values for our circles demo */
                switch (event.jaxis.axis) {
                case 0: leftStickX = (float)event.jaxis.value; break; // Left stick X
                case 1: leftStickY = (float)event.jaxis.value; break; // Left stick Y
                case 2: rightStickX = (float)event.jaxis.value; break; // Right stick X
                case 3: rightStickY = (float)event.jaxis.value; break; // Right stick Y
                }
                break;

            case SDL_JOYHATMOTION:
                SDL_Log("D-pad moved: %s on joystick %d.\n",
                    GetHatDirection(event.jhat.value),
                    event.jhat.which);
                break;

            case SDL_JOYDEVICEADDED:
                SDL_Log("Joystick %d connected.\n", event.jdevice.which);
                SDL_JoystickOpen(event.jdevice.which);
                break;

            case SDL_JOYDEVICEREMOVED:
                SDL_Log("Joystick %d disconnected.\n", event.jdevice.which);
                SDL_JoystickClose(SDL_JoystickFromInstanceID(event.jdevice.which));
                break;
            }
        }

        /* --- Render step --- */
        // Clear screen to blue
        SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // white

        float radius = 100.0f;  // circle radius
        int leftCircleX = WINDOW_WIDTH / 4;   // ~160
        int leftCircleY = WINDOW_HEIGHT / 2;  // ~240
        int rightCircleX = 3 * WINDOW_WIDTH / 4;  // ~480
        int rightCircleY = WINDOW_HEIGHT / 2;     // ~240

        DrawCircleOutline(renderer, leftCircleX, leftCircleY, radius, 4.0f);
        DrawCircleOutline(renderer, rightCircleX, rightCircleY, radius, 4.0f);

        /* Dot inside left circle: based on leftStick (axes 0,1) */
        float range = 32768.0f;
        float leftDotXOffset = (leftStickX / range) * radius;
        float leftDotYOffset = -(leftStickY / range) * radius;

        int leftDotX = (int)(leftCircleX + leftDotXOffset);
        int leftDotY = (int)(leftCircleY + leftDotYOffset);

        SDL_Rect leftDot;
        leftDot.w = 6;
        leftDot.h = 6;
        leftDot.x = leftDotX - 3;
        leftDot.y = leftDotY - 3;
        SDL_RenderFillRect(renderer, &leftDot);

        /* Dot inside right circle: based on rightStick (axes 2,3) */
        float rightDotXOffset = (rightStickX / range) * radius;
        float rightDotYOffset = -(rightStickY / range) * radius;

        int rightDotX = (int)(rightCircleX + rightDotXOffset);
        int rightDotY = (int)(rightCircleY + rightDotYOffset);

        SDL_Rect rightDot;
        rightDot.w = 6;
        rightDot.h = 6;
        rightDot.x = rightDotX - 3;
        rightDot.y = rightDotY - 3;
        SDL_RenderFillRect(renderer, &rightDot);

        // Present
        SDL_RenderPresent(renderer);

        // Small delay
        SDL_Delay(10);
    }

    /* Cleanup all opened joysticks */
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_JoystickOpen(i)) {
            SDL_JoystickClose(SDL_JoystickOpen(i));
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
