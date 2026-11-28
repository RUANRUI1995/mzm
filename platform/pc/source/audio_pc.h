#ifndef AUDIO_PC_H
#define AUDIO_PC_H

/* Sound on the PC build. The engine runs as on the GBA, once per frame from
 * agbmain (UpdateAudio, C translation in port/audio); port_mzm_audio_glue.c
 * turns each mixdown into s16 stereo (plus the PSG synthesiser) in a ring, and
 * this module hands that ring to SDL. Without a device (headless runs) the
 * engine still runs, so traces include it; the samples are just dropped. */

#include <stdbool.h>

void Audio_PC_Init(bool openDevice);
void Audio_PC_Shutdown(void);

/* --wav: also write everything the engine produces to a WAV file. */
bool Audio_PC_OpenWav(const char* path);

/* Before the game's frame: the sound DMA's position advance and the mixdown
 * hook, what Port_MzmAudio_ProduceTick does on the 3DS around UpdateAudio. */
void Audio_PC_BeginFrame(void);

/* After the game's frame: queue what the engine produced. */
void Audio_PC_EndFrame(void);

#endif /* AUDIO_PC_H */
