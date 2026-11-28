/* Checks the C sound engine (port_audio_engine.c) against the assembly while
 * the 3DS plays (build with `make AUDIO_LOCKSTEP=1`).
 *
 * Every routine of port/asm/audio_internal.s and asm/soundcode.s is linked
 * with --wrap, so the game's calls land here. Each wrapper copies the memory
 * the routine may change, runs the assembly, keeps its result, puts the copy
 * back, runs the C version, and compares the two results byte for byte. The
 * game always continues with the assembly's result, so a difference cannot
 * change what is played. Differences and per-routine counts go to
 * <game folder>/debug/audio-lockstep.txt (the first few in detail, then a
 * summary every ~10 s of sound).
 *
 * Calls made inside the assembly itself (UpdateAudio -> InitTrack in the same
 * file) are not wrapped; the outer call's comparison covers them. */
#ifdef PORT_AUDIO_LOCKSTEP

#include "port_audio_engine.h"

#include "gba.h"
#include "audio.h"
#include "audio/track_internal.h"
#include "data/audio.h"
#include "port_gba_mem.h"
#include "port_paths.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Set while the C copy of UpdateAudio runs, so the mixdown capture
 * (port_mzm_audio_glue.c) does not queue the same sound twice. */
volatile int gPortAudioShadowRun;

/* ---- The assembly, under --wrap ------------------------------------------------- */

extern u8 __real_InitTrack(struct TrackData* pTrack, const u32* pHeader);
extern void __real_StopMusicOrSound(struct TrackData* pTrack);
extern void __real_ResetTrack(struct TrackData* pTrack);
extern u32 __real_unk_4cfc(struct TrackData* pTrack);
extern void __real_unk_4d1c(struct TrackData* pTrack, struct TrackVariables* pVariables);
extern void __real_UpdateAudio(void);
extern void __real_unk_4e10(struct TrackVariables* pVariables);
extern void __real_unk_4eb4(struct TrackVariables* pVariables);
extern void __real_unk_4f10(struct TrackVariables* pVariables);
extern void __real_unk_4f8c(struct SoundChannel* pChannel, struct TrackVariables* pVariables, s32 priority);
extern void __real_AudioCommand_Goto(struct TrackVariables* pVariables);
extern void __real_AudioCommand_PatternPlay(struct TrackVariables* pVariables);
extern void __real_UploadSampleToWaveRam(const u32* pSample);
extern void __real_unk_5104(struct PsgSoundData* pSound);
extern void __real_CallSoundCodeA(struct SoundChannel* pChannel, u32* pMix, u8 count);
extern u8* __real_CallSoundCodeB(u32* pSrc1, u32* pSrc2, u32* pDst, u8 count);
extern u8* __real_CallSoundCodeC(u32* pDst, u32* pMix, u8 count);
extern u32 __real_CallGetNoteFrequency(u32 frequency, u32 pitch);

/* ---- Bookkeeping ------------------------------------------------------------------ */

enum {
    F_InitTrack, F_StopMusicOrSound, F_ResetTrack, F_unk_4cfc, F_unk_4d1c, F_UpdateAudio,
    F_unk_4e10, F_unk_4eb4, F_unk_4f10, F_unk_4f8c, F_Goto, F_PatternPlay, F_UploadSample,
    F_unk_5104, F_SoundCodeA, F_SoundCodeB, F_SoundCodeC, F_GetNoteFrequency, F_COUNT
};
static const char* const kNames[F_COUNT] = {
    "InitTrack", "StopMusicOrSound", "ResetTrack", "unk_4cfc", "unk_4d1c", "UpdateAudio",
    "unk_4e10", "unk_4eb4", "unk_4f10", "unk_4f8c", "AudioCommand_Goto", "AudioCommand_PatternPlay",
    "UploadSampleToWaveRam", "unk_5104", "SoundCodeA", "SoundCodeB", "SoundCodeC", "GetNoteFrequency",
};
static uint32_t sCalls[F_COUNT], sMismatches[F_COUNT];
static uint32_t sDetailsWritten, sUpdateTicks;
#define MAX_DETAILS 40

#define LOG_PATH PORT_DEBUG_DIR "/audio-lockstep.txt"

static void WriteSummary(void) {
    FILE* f = fopen(LOG_PATH, "a");
    int i;
    if (!f) return;
    fprintf(f, "summary after %lu UpdateAudio ticks:\n", (unsigned long)sUpdateTicks);
    for (i = 0; i < F_COUNT; i++)
        fprintf(f, "  %-26s calls %8lu  mismatches %lu\n", kNames[i], (unsigned long)sCalls[i],
                (unsigned long)sMismatches[i]);
    fclose(f);
}

typedef struct {
    const char* name;
    void* p;
    size_t size;
} Region;

#define MAX_REGIONS 24
#define MAX_DEPTH 3
#define POOL_BYTES (24 * 1024)
static uint8_t sBefore[MAX_DEPTH][POOL_BYTES];
static uint8_t sAfterReal[MAX_DEPTH][POOL_BYTES];
static int sDepth;

static void LogMismatch(int fn, const Region* r, size_t offset, const uint8_t* real, const uint8_t* c) {
    FILE* f;
    size_t k;
    if (sDetailsWritten >= MAX_DETAILS) return;
    sDetailsWritten++;
    f = fopen(LOG_PATH, "a");
    if (!f) return;
    fprintf(f, "MISMATCH %s: %s +0x%zx\n  asm:", kNames[fn], r->name, offset);
    for (k = offset & ~(size_t)15; k < (offset & ~(size_t)15) + 32 && k < r->size; k++) fprintf(f, " %02x", real[k]);
    fprintf(f, "\n  C:  ");
    for (k = offset & ~(size_t)15; k < (offset & ~(size_t)15) + 32 && k < r->size; k++) fprintf(f, " %02x", c[k]);
    fprintf(f, "\n");
    fclose(f);
}

/* Runs the assembly then the C version over the same starting memory and
 * compares. `real` and `shadow` are the two calls; they store any return value
 * in *retReal / *retC. */
typedef void (*Runner)(void* ctx, uintptr_t* ret);

static uintptr_t Lockstep(int fn, Region* regions, int count, Runner real, Runner shadow, void* ctx) {
    uintptr_t retReal = 0, retC = 0;
    size_t used = 0;
    int i, depth = sDepth;
    bool differs = false;

    sCalls[fn]++;
    if (depth >= MAX_DEPTH) {
        real(ctx, &retReal);
        return retReal;
    }
    for (i = 0; i < count; i++) used += regions[i].size;
    if (used > POOL_BYTES) {
        real(ctx, &retReal);
        return retReal;
    }

    sDepth++;
    used = 0;
    for (i = 0; i < count; i++) {
        memcpy(&sBefore[depth][used], regions[i].p, regions[i].size);
        used += regions[i].size;
    }
    real(ctx, &retReal);
    used = 0;
    for (i = 0; i < count; i++) {
        memcpy(&sAfterReal[depth][used], regions[i].p, regions[i].size);
        memcpy(regions[i].p, &sBefore[depth][used], regions[i].size);
        used += regions[i].size;
    }
    shadow(ctx, &retC);
    used = 0;
    for (i = 0; i < count; i++) {
        const uint8_t* a = &sAfterReal[depth][used];
        const uint8_t* c = (const uint8_t*)regions[i].p;
        if (memcmp(a, c, regions[i].size) != 0) {
            size_t k = 0;
            while (a[k] == c[k]) k++;
            if (!differs) LogMismatch(fn, &regions[i], k, a, c);
            differs = true;
        }
        /* The game goes on with the assembly's result. */
        memcpy(regions[i].p, a, regions[i].size);
        used += regions[i].size;
    }
    if (retReal != retC && !differs) {
        Region r = { "return value", &retReal, sizeof(retReal) };
        LogMismatch(fn, &r, 0, (const uint8_t*)&retReal, (const uint8_t*)&retC);
        differs = true;
    }
    if (differs) sMismatches[fn]++;
    sDepth--;
    return retReal;
}

/* All the engine's state, for the routines that walk tracks and channels. */
static int AddEngineState(Region* r, int n) {
    int i;
    r[n++] = (Region){ "gMusicInfo", &gMusicInfo, sizeof(gMusicInfo) };
    r[n++] = (Region){ "gPsgSounds", gPsgSounds, sizeof(gPsgSounds) };
    r[n++] = (Region){ "gSoundQueue", gSoundQueue, sizeof(struct SoundQueue) * 9 };
    r[n++] = (Region){ "I/O sound registers", &gIoMem[0x60], 0x40 };
    for (i = 0; i < 9; i++) {
        const struct TrackGroupROMData* g = &sMusicTrackDataRom[i];
        r[n++] = (Region){ "TrackData", g->pTrack, sizeof(struct TrackData) };
        r[n++] = (Region){ "TrackVariables", g->pVariables, (size_t)g->maxAmountOfTracks * 0x50 };
    }
    return n;
}

/* ---- Wrappers ------------------------------------------------------------------------ */

typedef struct {
    void* a;
    void* b;
    uintptr_t c;
    uintptr_t d;
} Args;

#define RUNNERS(name, realCall, cCall)                                                         \
    static void Real_##name(void* ctx, uintptr_t* ret) { Args* x = (Args*)ctx; (void)x; realCall; } \
    static void C_##name(void* ctx, uintptr_t* ret) { Args* x = (Args*)ctx; (void)x; cCall; }

RUNNERS(InitTrack, *ret = __real_InitTrack(x->a, x->b), *ret = AudioC_InitTrack(x->a, x->b))
RUNNERS(Stop, (void)ret; __real_StopMusicOrSound(x->a), (void)ret; AudioC_StopMusicOrSound(x->a))
RUNNERS(Reset, (void)ret; __real_ResetTrack(x->a), (void)ret; AudioC_ResetTrack(x->a))
RUNNERS(4cfc, *ret = __real_unk_4cfc(x->a), *ret = AudioC_unk_4cfc(x->a))
RUNNERS(4d1c, (void)ret; __real_unk_4d1c(x->a, x->b), (void)ret; AudioC_unk_4d1c(x->a, x->b))
RUNNERS(4e10, (void)ret; __real_unk_4e10(x->a), (void)ret; AudioC_unk_4e10(x->a))
RUNNERS(4eb4, (void)ret; __real_unk_4eb4(x->a), (void)ret; AudioC_unk_4eb4(x->a))
RUNNERS(4f10, (void)ret; __real_unk_4f10(x->a), (void)ret; AudioC_unk_4f10(x->a))
RUNNERS(4f8c, (void)ret; __real_unk_4f8c(x->a, x->b, (s32)x->c), (void)ret; AudioC_unk_4f8c(x->a, x->b, (s32)x->c))
RUNNERS(Goto, (void)ret; __real_AudioCommand_Goto(x->a), (void)ret; AudioC_AudioCommand_Goto(x->a))
RUNNERS(Pattern, (void)ret; __real_AudioCommand_PatternPlay(x->a), (void)ret; AudioC_AudioCommand_PatternPlay(x->a))
RUNNERS(Upload, (void)ret; __real_UploadSampleToWaveRam(x->a), (void)ret; AudioC_UploadSampleToWaveRam(x->a))
RUNNERS(5104, (void)ret; __real_unk_5104(x->a), (void)ret; AudioC_unk_5104(x->a))
RUNNERS(A, (void)ret; __real_CallSoundCodeA(x->a, x->b, (u8)x->c), (void)ret; AudioC_SoundCodeA(x->a, x->b, (u8)x->c))
RUNNERS(B, *ret = (uintptr_t)__real_CallSoundCodeB(x->a, x->b, (u32*)x->d, (u8)x->c),
        *ret = (uintptr_t)AudioC_SoundCodeB(x->a, x->b, (u32*)x->d, (u8)x->c))
RUNNERS(C, *ret = (uintptr_t)__real_CallSoundCodeC(x->a, x->b, (u8)x->c),
        *ret = (uintptr_t)AudioC_SoundCodeC(x->a, x->b, (u8)x->c))
RUNNERS(Freq, *ret = __real_CallGetNoteFrequency((u32)x->c, (u32)x->d), *ret = AudioC_GetNoteFrequency((u32)x->c, (u32)x->d))

static void Real_Update(void* ctx, uintptr_t* ret) { (void)ctx; (void)ret; __real_UpdateAudio(); }
static void C_Update(void* ctx, uintptr_t* ret) {
    (void)ctx;
    (void)ret;
    gPortAudioShadowRun = 1;
    AudioC_UpdateAudio();
    gPortAudioShadowRun = 0;
}

u8 __wrap_InitTrack(struct TrackData* pTrack, const u32* pHeader) {
    Region r[MAX_REGIONS];
    Args a = { pTrack, (void*)pHeader, 0, 0 };
    return (u8)Lockstep(F_InitTrack, r, AddEngineState(r, 0), Real_InitTrack, C_InitTrack, &a);
}

void __wrap_StopMusicOrSound(struct TrackData* pTrack) {
    Region r[MAX_REGIONS];
    Args a = { pTrack, 0, 0, 0 };
    Lockstep(F_StopMusicOrSound, r, AddEngineState(r, 0), Real_Stop, C_Stop, &a);
}

void __wrap_ResetTrack(struct TrackData* pTrack) {
    Region r[MAX_REGIONS];
    Args a = { pTrack, 0, 0, 0 };
    Lockstep(F_ResetTrack, r, AddEngineState(r, 0), Real_Reset, C_Reset, &a);
}

u32 __wrap_unk_4cfc(struct TrackData* pTrack) {
    Region r[1] = { { "TrackData", pTrack, sizeof(*pTrack) } };
    Args a = { pTrack, 0, 0, 0 };
    return (u32)Lockstep(F_unk_4cfc, r, 1, Real_4cfc, C_4cfc, &a);
}

void __wrap_unk_4d1c(struct TrackData* pTrack, struct TrackVariables* pVariables) {
    Region r[2] = { { "TrackData", pTrack, sizeof(*pTrack) }, { "TrackVariables", pVariables, 0x50 } };
    Args a = { pTrack, pVariables, 0, 0 };
    Lockstep(F_unk_4d1c, r, 2, Real_4d1c, C_4d1c, &a);
}

void __wrap_UpdateAudio(void) {
    Region r[MAX_REGIONS];
    sUpdateTicks++;
    Lockstep(F_UpdateAudio, r, AddEngineState(r, 0), Real_Update, C_Update, NULL);
    if ((sUpdateTicks % 600) == 0) WriteSummary();
}

void __wrap_unk_4e10(struct TrackVariables* pVariables) {
    Region r[2] = { { "TrackVariables", pVariables, 0x50 }, { "I/O sound registers", &gIoMem[0x60], 0x40 } };
    Args a = { pVariables, 0, 0, 0 };
    Lockstep(F_unk_4e10, r, 2, Real_4e10, C_4e10, &a);
}

void __wrap_unk_4eb4(struct TrackVariables* pVariables) {
    Region r[1] = { { "TrackVariables", pVariables, 0x50 } };
    Args a = { pVariables, 0, 0, 0 };
    Lockstep(F_unk_4eb4, r, 1, Real_4eb4, C_4eb4, &a);
}

void __wrap_unk_4f10(struct TrackVariables* pVariables) {
    Region r[1] = { { "TrackVariables", pVariables, 0x50 } };
    Args a = { pVariables, 0, 0, 0 };
    Lockstep(F_unk_4f10, r, 1, Real_4f10, C_4f10, &a);
}

void __wrap_unk_4f8c(struct SoundChannel* pChannel, struct TrackVariables* pVariables, s32 priority) {
    Region r[MAX_REGIONS];
    Args a = { pChannel, pVariables, (uintptr_t)priority, 0 };
    Lockstep(F_unk_4f8c, r, AddEngineState(r, 0), Real_4f8c, C_4f8c, &a);
}

void __wrap_AudioCommand_Goto(struct TrackVariables* pVariables) {
    Region r[1] = { { "TrackVariables", pVariables, 0x50 } };
    Args a = { pVariables, 0, 0, 0 };
    Lockstep(F_Goto, r, 1, Real_Goto, C_Goto, &a);
}

void __wrap_AudioCommand_PatternPlay(struct TrackVariables* pVariables) {
    Region r[1] = { { "TrackVariables", pVariables, 0x50 } };
    Args a = { pVariables, 0, 0, 0 };
    Lockstep(F_PatternPlay, r, 1, Real_Pattern, C_Pattern, &a);
}

void __wrap_UploadSampleToWaveRam(const u32* pSample) {
    Region r[1] = { { "I/O sound registers", &gIoMem[0x60], 0x40 } };
    Args a = { (void*)pSample, 0, 0, 0 };
    Lockstep(F_UploadSample, r, 1, Real_Upload, C_Upload, &a);
}

void __wrap_unk_5104(struct PsgSoundData* pSound) {
    Region r[2] = { { "PsgSoundData", pSound, sizeof(*pSound) }, { "I/O sound registers", &gIoMem[0x60], 0x40 } };
    Args a = { pSound, 0, 0, 0 };
    Lockstep(F_unk_5104, r, 2, Real_5104, C_5104, &a);
}

void __wrap_CallSoundCodeA(struct SoundChannel* pChannel, u32* pMix, u8 count) {
    Region r[2] = { { "SoundChannel", pChannel, sizeof(*pChannel) }, { "mix buffer", pMix, (size_t)count * 32 } };
    Args a = { pChannel, pMix, count, 0 };
    Lockstep(F_SoundCodeA, r, 2, Real_A, C_A, &a);
}

u8* __wrap_CallSoundCodeB(u32* pSrc1, u32* pSrc2, u32* pDst, u8 count) {
    Region r[1] = { { "reverb buffer", pDst, (size_t)count * 16 } };
    Args a = { pSrc1, pSrc2, count, (uintptr_t)pDst };
    return (u8*)Lockstep(F_SoundCodeB, r, 1, Real_B, C_B, &a);
}

u8* __wrap_CallSoundCodeC(u32* pDst, u32* pMix, u8 count) {
    Region r[2] = { { "PCM left", pDst, (size_t)count * 4 }, { "PCM right", (u8*)pDst + 0x600, (size_t)count * 4 } };
    Args a = { pDst, pMix, count, 0 };
    return (u8*)Lockstep(F_SoundCodeC, r, 2, Real_C, C_C, &a);
}

u32 __wrap_CallGetNoteFrequency(u32 frequency, u32 pitch) {
    Args a = { 0, 0, frequency, pitch };
    return (u32)Lockstep(F_GetNoteFrequency, NULL, 0, Real_Freq, C_Freq, &a);
}

#endif /* PORT_AUDIO_LOCKSTEP */
