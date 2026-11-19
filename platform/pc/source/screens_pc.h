#ifndef SCREENS_PC_H
#define SCREENS_PC_H

/* The PC's two screens: the game (240x160) and the bottom-screen UI (320x240,
 * port/ui/port_bottom_ui.c). F2 cycles how they are shown:
 *
 *   STACKED     one window, the game above the UI (like a 3DS)
 *   SIDE        one window, side by side
 *   TWO_WINDOWS a window each (like a Wii U: TV and GamePad)
 *   GAME_ONLY   one window, the game alone
 *
 * The mouse on the UI is the stylus. */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SCREENS_STACKED = 0,
    SCREENS_SIDE,
    SCREENS_TWO_WINDOWS,
    SCREENS_GAME_ONLY,
    SCREENS_LAYOUT_COUNT
} ScreensLayout;

bool Screens_Open(int scale, ScreensLayout layout);
void Screens_Close(void);
bool Screens_IsOpen(void);

ScreensLayout Screens_Layout(void);
void Screens_CycleLayout(void);
/* True when the current layout shows the UI. */
bool Screens_ShowsBottom(void);

/* Takes the SDL events that concern the screens (mouse, window close/resize).
 * `event` is an SDL_Event*. Returns true when the user closed the game. */
bool Screens_HandleEvent(const void* event);

/* Once per frame, before the game runs: hands the stylus to the UI the way
 * the 3DS does (every frame while held, then a release). */
void Screens_PumpTouch(void);

/* bottom may be NULL when the layout does not show it. */
void Screens_Present(const uint32_t* top, const uint32_t* bottom);

#endif /* SCREENS_PC_H */
