/* frontend_pc.h for builds without SDL (the PowerPC build run under qemu):
 * there is no window, so they only run headless (--frames, --replay, --test). */
#include "frontend_pc.h"

#include <stdio.h>

bool Frontend_Open(int scale, int layout) {
    (void)scale;
    (void)layout;
    fprintf(stderr, "[PC] This build has no window: run it with --frames, --replay or --test.\n");
    return false;
}

void Frontend_Close(void) {}

uint16_t Frontend_Poll(bool* quit) {
    (void)quit;
    return 0;
}

bool Frontend_TakeLayoutChanged(void) { return false; }
bool Frontend_ShowsBottom(void) { return false; }
void Frontend_PumpTouch(void) {}

void Frontend_PresentAndPace(const uint32_t* top, const uint32_t* bottom) {
    (void)top;
    (void)bottom;
}
