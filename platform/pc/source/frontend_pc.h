#ifndef FRONTEND_PC_H
#define FRONTEND_PC_H

/* The window, keyboard, controller and frame pacing of the PC build.
 * frontend_sdl.c is the real one; frontend_none.c is for builds without SDL
 * (the PowerPC build run under qemu), which only run headless. The rest of
 * the platform layer (platform_pc.c) does not know which one it has. */

#include <stdbool.h>
#include <stdint.h>

/* Opens the window(s). False (with a message) when there is no display. */
bool Frontend_Open(int scale, int layout);
void Frontend_Close(void);

/* Handles pending events. Returns the GBA keys held on the keyboard and the
 * controller; sets *quit when the window was closed or Esc pressed. */
uint16_t Frontend_Poll(bool* quit);

/* True once after F2 changed the screen layout (the UI must be redrawn). */
bool Frontend_TakeLayoutChanged(void);

/* True when the current layout shows the bottom-screen UI. */
bool Frontend_ShowsBottom(void);

/* Once per frame: the mouse on the bottom screen, as the stylus. */
void Frontend_PumpTouch(void);

/* Shows the frame and waits for the next one (59.73 Hz, or no wait while
 * fast-forwarding). bottom is NULL when the layout does not show it. */
void Frontend_PresentAndPace(const uint32_t* top, const uint32_t* bottom);

#endif /* FRONTEND_PC_H */
