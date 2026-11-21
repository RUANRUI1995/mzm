#ifndef WIIU_SUPPORT_H
#define WIIU_SUPPORT_H

/* The Wii U build (OS_TARGET=wiiu, wiiu_support.c). */

/* The SD card folder that holds the ROM (mzm_eu.gba) and the save (mzm.sav). */
#define WIIU_DATA_DIR "fs:/vol/external01/wiiu/apps/mzm"
/* stdout and stderr of the last run, in that folder, each line timed. */
#define WIIU_LOG_FILE "mzm-log.txt"
/* When this replay (platform/pc/replays format) is in that folder, it drives
 * the game instead of the controls. */
#define WIIU_REPLAY_FILE "mzm-replay.txt"
/* The trace (platform/pc/README.md, "Traces") such a run writes there. */
#define WIIU_TRACE_FILE "mzm-trace.txt"

/* Sends stdout/stderr to the system console and WIIU_LOG_FILE, creates
 * WIIU_DATA_DIR and moves into it. Returns the folder, or NULL when there is
 * no SD card. */
const char* WiiU_Init(void);

#endif /* WIIU_SUPPORT_H */
