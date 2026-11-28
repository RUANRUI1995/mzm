#ifndef PORT_UI_PLATFORM_H
#define PORT_UI_PLATFORM_H

/* What the bottom-screen UI (port_bottom_ui.c) asks of the system it runs on,
 * besides drawing (port_draw2d.h). Each platform implements these:
 * platform/3ds/source/port_ui_platform_3ds.c, platform/pc/source/... */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Milliseconds from an arbitrary start; only differences are used. */
uint64_t PortUi_NowMs(void);

/* Battery level 0..5 and whether it is charging. False when the system has
 * no battery to report (the UI then draws an empty gauge). */
bool PortUi_PollBattery(uint8_t* level, bool* charging);

/* Wi-Fi signal 0..3. */
int PortUi_WifiStrength(void);

/* Local time of `t`, like localtime_r. */
bool PortUi_LocalTime(time_t t, struct tm* out);

#endif /* PORT_UI_PLATFORM_H */
