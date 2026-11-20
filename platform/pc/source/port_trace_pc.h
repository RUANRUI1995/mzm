#ifndef PORT_TRACE_LINUX_H
#define PORT_TRACE_LINUX_H

/* Reproducible runs for the Linux reference build: input replay/recording and
 * a per-frame state trace. Two builds fed the same replay must write the same
 * trace; the first line that differs is the first frame that diverged. See
 * platform/pc/README.md. */

#include <stdbool.h>
#include <stdint.h>

/* Loads a replay file. Returns false (and prints why) on a bad file. */
bool Trace_LoadReplay(const char* path);
bool Trace_HasReplay(void);
/* The save file the loaded replay starts from (its 'save FILE' line), or NULL. */
const char* Trace_ReplaySave(void);
/* Keys (GBA KEY_* mask, 1 = pressed) the replay holds on this frame. */
uint16_t Trace_ReplayKeys(uint32_t frame);
/* Frame after the replay's last line, 0 without a replay. */
uint32_t Trace_ReplayLength(void);

bool Trace_OpenRecord(const char* path);
void Trace_RecordKeys(uint32_t frame, uint16_t keys);

bool Trace_OpenLog(const char* path);
bool Trace_LogEnabled(void);
/* Appends this frame's line. framebuffer: 240x160 ABGR8888 pixels, or NULL. */
void Trace_LogFrame(uint32_t frame, uint16_t keys, const uint32_t* framebuffer);

bool Trace_WritePpm(const char* path, const uint32_t* framebuffer);
/* Any size, e.g. the 320x240 bottom screen. */
bool Trace_WritePpmSized(const char* path, const uint32_t* pixels, int width, int height);

/* Writes the raw GBA memory of this frame (ewram, iwram, io, vram, bgpal,
 * objpal, oam) as <dir>/<region>_<frame>.bin, to find which bytes a
 * diverging trace column covers. */
bool Trace_DumpMemory(const char* dir, uint32_t frame);

void Trace_Close(void);

#endif /* PORT_TRACE_LINUX_H */
