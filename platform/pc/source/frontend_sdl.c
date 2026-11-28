/* frontend_pc.h with SDL2: the window(s) (screens_pc.c), keyboard, the first
 * game controller and frame pacing.
 *
 * Keyboard: arrows, Z = A, X = B, A = L, S = R, Enter = START,
 * Backspace or right Shift = SELECT, hold Tab = fast forward, Esc = quit,
 * F2 = screen layout (screens_pc.h). The mouse on the bottom screen is the
 * stylus. */
#include "frontend_pc.h"
#include "screens_pc.h"

#include "types.h"
#include "gba.h"

#define SDL_MAIN_HANDLED /* main() stays ours on Windows too */
#include <SDL.h>

#include <stdio.h>

static bool sOpen;
static uint16_t sKeyboardKeys;
static SDL_GameController* sController;
static Uint64 sNextFrameDeadline;
static bool sFastForward;
static bool sLayoutChanged;

bool Frontend_Open(int scale, int layout) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "[PC] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    if (!Screens_Open(scale, (ScreensLayout)layout)) return false;
    sOpen = true;
    sNextFrameDeadline = SDL_GetPerformanceCounter();
    return true;
}

void Frontend_Close(void) {
    if (sController) SDL_GameControllerClose(sController);
    sController = NULL;
    if (sOpen) {
        Screens_Close();
        SDL_Quit();
        sOpen = false;
    }
}

static uint16_t KeyForScancode(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_Z:         return KEY_A;
        case SDL_SCANCODE_X:         return KEY_B;
        case SDL_SCANCODE_A:         return KEY_L;
        case SDL_SCANCODE_S:         return KEY_R;
        case SDL_SCANCODE_RETURN:    return KEY_START;
        case SDL_SCANCODE_BACKSPACE:
        case SDL_SCANCODE_RSHIFT:    return KEY_SELECT;
        case SDL_SCANCODE_LEFT:      return KEY_LEFT;
        case SDL_SCANCODE_RIGHT:     return KEY_RIGHT;
        case SDL_SCANCODE_UP:        return KEY_UP;
        case SDL_SCANCODE_DOWN:      return KEY_DOWN;
        default:                     return 0;
    }
}

static uint16_t ControllerKeys(void) {
    static const struct { SDL_GameControllerButton button; uint16_t key; } kMap[] = {
        { SDL_CONTROLLER_BUTTON_A, KEY_A },          { SDL_CONTROLLER_BUTTON_B, KEY_B },
        { SDL_CONTROLLER_BUTTON_BACK, KEY_SELECT },  { SDL_CONTROLLER_BUTTON_START, KEY_START },
        { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, KEY_RIGHT }, { SDL_CONTROLLER_BUTTON_DPAD_LEFT, KEY_LEFT },
        { SDL_CONTROLLER_BUTTON_DPAD_UP, KEY_UP },   { SDL_CONTROLLER_BUTTON_DPAD_DOWN, KEY_DOWN },
        { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, KEY_R }, { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, KEY_L },
    };
    const int kDeadZone = 16000;
    uint16_t keys = 0;
    size_t i;
    if (!sController) return 0;
    for (i = 0; i < sizeof(kMap) / sizeof(kMap[0]); ++i)
        if (SDL_GameControllerGetButton(sController, kMap[i].button)) keys |= kMap[i].key;
    {
        int x = SDL_GameControllerGetAxis(sController, SDL_CONTROLLER_AXIS_LEFTX);
        int y = SDL_GameControllerGetAxis(sController, SDL_CONTROLLER_AXIS_LEFTY);
        if (x < -kDeadZone) keys |= KEY_LEFT;
        if (x > kDeadZone) keys |= KEY_RIGHT;
        if (y < -kDeadZone) keys |= KEY_UP;
        if (y > kDeadZone) keys |= KEY_DOWN;
    }
    return keys;
}

uint16_t Frontend_Poll(bool* quit) {
    SDL_Event ev;
    if (!sOpen) return 0;
    while (SDL_PollEvent(&ev)) {
        if (Screens_HandleEvent(&ev)) *quit = true;
        switch (ev.type) {
            case SDL_QUIT:
                *quit = true;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                bool down = ev.type == SDL_KEYDOWN;
                if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE && down) *quit = true;
                if (ev.key.keysym.scancode == SDL_SCANCODE_TAB) sFastForward = down;
                if (ev.key.keysym.scancode == SDL_SCANCODE_F2 && down && !ev.key.repeat) {
                    Screens_CycleLayout();
                    sLayoutChanged = true;
                }
                if (down) sKeyboardKeys |= KeyForScancode(ev.key.keysym.scancode);
                else sKeyboardKeys &= (uint16_t)~KeyForScancode(ev.key.keysym.scancode);
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (!sController) sController = SDL_GameControllerOpen(ev.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (sController &&
                    ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(sController))) {
                    SDL_GameControllerClose(sController);
                    sController = NULL;
                }
                break;
            default:
                break;
        }
    }
    return sKeyboardKeys | ControllerKeys();
}

bool Frontend_TakeLayoutChanged(void) {
    bool changed = sLayoutChanged;
    sLayoutChanged = false;
    return changed;
}

bool Frontend_ShowsBottom(void) { return sOpen && Screens_ShowsBottom(); }

void Frontend_PumpTouch(void) {
    if (Frontend_ShowsBottom()) Screens_PumpTouch();
}

/* GBA refresh: 16.78 MHz / 280896 cycles per frame = 59.73 Hz. */
static void PaceFrame(void) {
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 frameTicks = freq * 280896ull / 16777216ull;
    Uint64 now = SDL_GetPerformanceCounter();

    if (sFastForward) {
        sNextFrameDeadline = now;
        return;
    }
    sNextFrameDeadline += frameTicks;
    if (sNextFrameDeadline + frameTicks * 4 < now) sNextFrameDeadline = now; /* fell behind: don't catch up */
    while (now < sNextFrameDeadline) {
        Uint64 remainingMs = (sNextFrameDeadline - now) * 1000ull / freq;
        if (remainingMs > 1) SDL_Delay((Uint32)(remainingMs - 1));
        now = SDL_GetPerformanceCounter();
    }
}

void Frontend_PresentAndPace(const uint32_t* top, const uint32_t* bottom) {
    if (!sOpen) return;
    Screens_Present(top, bottom);
    PaceFrame();
}
