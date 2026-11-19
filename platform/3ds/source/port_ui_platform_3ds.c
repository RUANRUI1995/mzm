/* port_ui_platform.h on the 3DS: the system clock, ptm:u for the battery and
 * the Wi-Fi strength the HOME menu shows. */
#include "port_ui_platform.h"

#include <3ds.h>

uint64_t PortUi_NowMs(void) { return osGetTime(); }

bool PortUi_PollBattery(uint8_t* level, bool* charging) {
    /* ptm:u may not be up yet the first times this runs (service bring-up
     * order), so keep retrying the init until it takes. */
    static bool sPtmuReady = false;
    u8 lvl = 5, chg = 0;
    if (!sPtmuReady && R_SUCCEEDED(ptmuInit())) sPtmuReady = true;
    if (!sPtmuReady) return false;
    if (R_SUCCEEDED(PTMU_GetBatteryLevel(&lvl))) *level = lvl > 5 ? 5 : lvl;
    if (R_SUCCEEDED(PTMU_GetBatteryChargeState(&chg))) *charging = (chg != 0);
    return true;
}

int PortUi_WifiStrength(void) { return (int)osGetWifiStrength(); }

bool PortUi_LocalTime(time_t t, struct tm* out) { return localtime_r(&t, out) != NULL; }
