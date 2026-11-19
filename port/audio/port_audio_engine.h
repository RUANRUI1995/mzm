#ifndef PORT_AUDIO_ENGINE_H
#define PORT_AUDIO_ENGINE_H

/* C versions of the sound engine routines that are still assembly in the
 * decompilation: port/asm/audio_internal.s (the sequencer side) and
 * asm/soundcode.s (the PCM mixer). See port_audio_engine.c.
 *
 * They are named AudioC_* so they can live next to the assembly ones: the
 * 3DS keeps the assembly and can check these against it (port_audio_lockstep.c);
 * the platforms without ARM code (PC, Wii U) get the official names from
 * port_audio_bind.c. */

#include "types.h"
#include "structs/audio.h"

u8 AudioC_InitTrack(struct TrackData* pTrack, const u32* pHeader);
void AudioC_StopMusicOrSound(struct TrackData* pTrack);
void AudioC_ResetTrack(struct TrackData* pTrack);
u32 AudioC_unk_4cfc(struct TrackData* pTrack);
void AudioC_unk_4d1c(struct TrackData* pTrack, struct TrackVariables* pVariables);
void AudioC_UpdateAudio(void);
void AudioC_unk_4e10(struct TrackVariables* pVariables);
void AudioC_unk_4eb4(struct TrackVariables* pVariables);
void AudioC_unk_4f10(struct TrackVariables* pVariables);
void AudioC_unk_4f8c(struct SoundChannel* pChannel, struct TrackVariables* pVariables, s32 priority);
void AudioC_AudioCommand_Goto(struct TrackVariables* pVariables);
void AudioC_AudioCommand_PatternPlay(struct TrackVariables* pVariables);
void AudioC_UploadSampleToWaveRam(const u32* pSample);
void AudioC_unk_5104(struct PsgSoundData* pSound);

/* The mixer (asm/soundcode.s). Same signatures as SoundCode{A,B,C}Func_T. */
void AudioC_SoundCodeA(struct SoundChannel* pChannel, u32* pMix, u8 count);
u8* AudioC_SoundCodeB(u32* pSrc1, u32* pSrc2, u32* pDst, u8 count);
u8* AudioC_SoundCodeC(u32* pDst, u32* pMix, u8 count);
u32 AudioC_GetNoteFrequency(u32 frequency, u32 pitch);
void AudioC_sub_08004ad8(void);

#endif /* PORT_AUDIO_ENGINE_H */
