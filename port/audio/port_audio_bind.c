/* The official names of the sound engine routines, for the platforms that do
 * not assemble port/asm/audio_internal.s and asm/soundcode.s (PC, Wii U): each
 * one is its C translation in port_audio_engine.c. The 3DS keeps the assembly. */
#include "port_audio_engine.h"
#include "audio/track_internal.h"

u8 InitTrack(struct TrackData* pTrack, const u32* pHeader) { return AudioC_InitTrack(pTrack, pHeader); }
void StopMusicOrSound(struct TrackData* pTrack) { AudioC_StopMusicOrSound(pTrack); }
void ResetTrack(struct TrackData* pTrack) { AudioC_ResetTrack(pTrack); }
u32 unk_4cfc(struct TrackData* pTrack) { return AudioC_unk_4cfc(pTrack); }
void unk_4d1c(struct TrackData* pTrack, struct TrackVariables* pVariables) { AudioC_unk_4d1c(pTrack, pVariables); }
void UpdateAudio(void) { AudioC_UpdateAudio(); }
void unk_4e10(struct TrackVariables* pVariables) { AudioC_unk_4e10(pVariables); }
void unk_4eb4(struct TrackVariables* pVariables) { AudioC_unk_4eb4(pVariables); }
void unk_4f10(struct TrackVariables* pVariables) { AudioC_unk_4f10(pVariables); }
void unk_4f8c(struct SoundChannel* pChannel, struct TrackVariables* pVariables, s32 priority) {
    AudioC_unk_4f8c(pChannel, pVariables, priority);
}
void AudioCommand_Goto(struct TrackVariables* pVariables) { AudioC_AudioCommand_Goto(pVariables); }
void AudioCommand_PatternPlay(struct TrackVariables* pVariables) { AudioC_AudioCommand_PatternPlay(pVariables); }
void UploadSampleToWaveRam(const u32* pSample) { AudioC_UploadSampleToWaveRam(pSample); }
void unk_5104(struct PsgSoundData* pSound) { AudioC_unk_5104(pSound); }

/* include/audio_wrappers.h declares these as void(void) (they are only ever
 * taken as addresses, for gSoundCode*Pointer); their real signatures are the
 * SoundCode*Func_T ones, which is what these are. */
void CallSoundCodeA(struct SoundChannel* pChannel, u32* pMix, u8 count) { AudioC_SoundCodeA(pChannel, pMix, count); }
u8* CallSoundCodeB(u32* pSrc1, u32* pSrc2, u32* pDst, u8 count) { return AudioC_SoundCodeB(pSrc1, pSrc2, pDst, count); }
u8* CallSoundCodeC(u32* pDst, u32* pMix, u8 count) { return AudioC_SoundCodeC(pDst, pMix, count); }
u32 CallGetNoteFrequency(u32 frequency, u32 pitch) { return AudioC_GetNoteFrequency(frequency, pitch); }
void sub_08004ad8(void) { AudioC_sub_08004ad8(); }
