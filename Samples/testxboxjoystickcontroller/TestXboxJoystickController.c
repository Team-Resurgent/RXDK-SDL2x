#include "SDL.h"
#include <stdio.h>


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


int main(int argc, char* argv[]) {
    // Initialize SDL with joystick support
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) < 0) {
        SDL_Log("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }

    // Wait for at least one controller
    if (SDL_NumJoysticks() < 1) {
        SDL_Log("No joysticks detected. Waiting for connection...\n");
    }
    else {
        SDL_Log("Joysticks found: %d\n", SDL_NumJoysticks());
    }

    // Open all detected joysticks
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_JoystickOpen(i)) {
            SDL_Log("Failed to open joystick %d! SDL_Error: %s\n", i, SDL_GetError());
        }
        else {
            SDL_Log("Joystick %d opened: %s\n", i, SDL_JoystickNameForIndex(i));
        }
    }

    SDL_Event event;
    int running = 1;

    // Main event loop to listen for joystick input
    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_JOYBUTTONDOWN:
                SDL_Log("Button %s pressed on joystick %d.\n",
                    GetXboxButtonName(event.jbutton.button), event.jbutton.which);
                break;

            case SDL_JOYBUTTONUP:
                SDL_Log("Button %s released on joystick %d.\n",
                    GetXboxButtonName(event.jbutton.button), event.jbutton.which);
                break;

            case SDL_JOYAXISMOTION:
                if (event.jaxis.value < -8000 || event.jaxis.value > 8000) {
                    SDL_Log("Axis %s moved to %d on joystick %d\n",
                        GetAxisName(event.jaxis.axis), event.jaxis.value, event.jaxis.which);
                }
                break;


            case SDL_JOYHATMOTION:
                SDL_Log("D-pad moved: %s on joystick %d.\n",
                    GetHatDirection(event.jhat.value), event.jhat.which);
                break;

            case SDL_JOYDEVICEADDED:
                SDL_Log("Joystick %d connected.\n", event.jdevice.which);
                SDL_JoystickOpen(event.jdevice.which);
                break;

            case SDL_JOYDEVICEREMOVED:
                SDL_Log("Joystick %d disconnected.\n", event.jdevice.which);
                SDL_JoystickClose(SDL_JoystickFromInstanceID(event.jdevice.which));
                break;

            case SDL_QUIT:
                running = 0;
                break;
            }
        }

        SDL_Delay(10);  // Add a small delay to avoid CPU spikes
    }

    // Cleanup and quit SDL
    SDL_Quit();
    return 0;
}