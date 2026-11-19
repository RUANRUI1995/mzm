/* C translation of the sound engine routines that are still assembly:
 * port/asm/audio_internal.s (sequencer helpers, Thumb) and asm/soundcode.s
 * (the PCM mixer, ARM).
 *
 * The goal is the same result as the assembly, bit for bit, quirks included,
 * so that a port without ARM code (PC, Wii U) sounds like the 3DS, and so the
 * 3DS can check these against the assembly while it runs
 * (port_audio_lockstep.c). Comments name the instruction behind anything that
 * looks odd. Where the assembly reads or writes several byte fields with one
 * 32-bit access, the word is built from (or split into) the fields, least
 * significant byte first: the same value on a little-endian host, and still
 * right on a big-endian one. ROM data (sound headers, voice tables, samples)
 * is read byte by byte for the same reason.
 *
 * Not derived from any other implementation; the Switch port
 * (JJTapiaXX/Metroid-Zero-Mission-NX) was looked at for the overall shape. */
#include "port_audio_engine.h"

#include "gba.h"
#include "macros.h"
#include "audio.h"
#include "audio/track_internal.h"
#include "data/audio.h"
#include "port_gba_mem.h"

#include <stdint.h>
#include <string.h>

/* C functions of the engine the assembly calls (src/audio.c, src/audio_wrappers.c,
 * port/port_bios.c). */
extern void UpdateTrack(struct TrackData* pTrack);
extern void UpdatePsgSounds(void);
extern void UpdateMusic(void);
extern void DoSoundAction(u32 action);
extern void unk_20a4(struct SoundChannel* pChannel);
extern void ClearRegistersForPsg(struct PsgSoundData* pSound, u8 channel);
extern u32 MidiKey2Freq(u32* waveData, u8 midiKey, u8 fineAdjust);

#define TRACK_VARIABLES_SIZE 0x50 /* o_TrackVariables_size: the assembly clears this many bytes per track */
#define PCM_HALF 0x600            /* left/right halves of musicRawData / soundRawData */

/* ROM data is read through port_read*le: the original ROM bytes, whatever the
 * host's byte order and whether the big-endian ROM copy swapped them. */
static inline u32 Read32LE(const u8* p) { return port_read32le(p); }
static inline u16 Read16LE(const u8* p) { return port_read16le(p); }

static inline void Write16LE(u8* p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static inline void* Resolve(u32 gbaAddress) { return port_resolve_addr((uintptr_t)gbaAddress); }

/* ---- Track setup and teardown (audio_internal.s) ------------------------------ */

/* Shared by ResetTrack and StopMusicOrSound (they inline the same loop). */
static void ReleaseTrackChannels(struct TrackData* pTrack) {
    u32 count;
    struct TrackVariables* pVariables;

    pTrack->flags = 1;
    pTrack->currentTrack = 0;
    count = pTrack->amountOfTracks;
    pVariables = pTrack->pVariables;
    /* b to the body first, then `subs r6, #1; beq`: a count of 0 would run
     * 256 times in u8 arithmetic on the GBA (r6 is a full register there, so
     * 2^32); never reached with a real track, kept as a do-while. */
    do {
        struct PsgSoundData* pPsg = pVariables->pSoundPsg;
        struct SoundChannel* pChannel;

        if (pPsg != NULL) {
            ClearRegistersForPsg(pPsg, (u8)((pVariables->channel & 7) - 1));
            pPsg->unk_0 = 0;
            pPsg->pVariables = NULL;
        }
        for (pChannel = pVariables->pChannel; pChannel != NULL;) {
            struct SoundChannel* pNext = pChannel->pChannel2;
            pChannel->unk_0 = 0;
            pChannel->pVariables = NULL;
            pChannel->pChannel2 = NULL;
            pChannel->pChannel1 = NULL;
            pChannel = pNext;
        }
        pVariables->pSoundPsg = NULL;
        pVariables->pChannel = NULL;
        pVariables++;
    } while (--count != 0);
}

void AudioC_ResetTrack(struct TrackData* pTrack) {
    if (pTrack->unk_1E & 1) return;
    if (!(pTrack->flags & 2)) return;
    ReleaseTrackChannels(pTrack);
}

void AudioC_StopMusicOrSound(struct TrackData* pTrack) {
    if (pTrack->occupied) return;
    pTrack->occupied = TRUE;
    if (!(pTrack->unk_1E & 1) && (pTrack->flags & 2)) ReleaseTrackChannels(pTrack);
    pTrack->occupied = FALSE;
}

u8 AudioC_InitTrack(struct TrackData* pTrack, const u32* pHeader) {
    /* pHeader is a raw GBA ROM address; TrackData.pHeader keeps it raw (C code
     * compares it with fresh reads of sSoundDataEntries[].pHeader). */
    const u8* header = (const u8*)Resolve((u32)(uintptr_t)pHeader);
    u32 word0;
    u32 count;
    u32 i;
    struct TrackVariables* pVariables;

    if (pTrack->occupied) return 0;
    pTrack->occupied = TRUE;
    if (pTrack->unk_1E & 1) goto done;

    word0 = Read32LE(header);
    if ((u8)word0 == 0) {
        AudioC_ResetTrack(pTrack);
        pTrack->flags = 0;
        goto done;
    }

    if (pTrack->flags & 2) {
        /* A playing track with unk_1D set keeps playing against a lower
         * priority (header byte 2) sound: return 1. */
        if (pTrack->unk_1D != 0 && (s32)pTrack->unk_3 > (s32)((word0 >> 16) & 0xFF)) {
            pTrack->occupied = FALSE;
            return 1;
        }
        AudioC_ResetTrack(pTrack);
    }

    /* `str (word0 << 8) | 2` over flags..unk_3. */
    pTrack->flags = 2;
    pTrack->amountOfTracks = (u8)word0;
    pTrack->undefined_2 = (u8)(word0 >> 8);
    pTrack->unk_3 = (u8)(word0 >> 16);
    pTrack->pVoice = (struct Voice*)Resolve(Read32LE(header + 4));
    pTrack->pHeader = (const u8*)pHeader;
    pTrack->unk_C = 0x100; /* unk_E is left as it was */
    if (word0 >> 31) DoSoundAction(word0 >> 24);

    count = (u8)word0;
    pVariables = pTrack->pVariables;
    memset(pVariables, 0, TRACK_VARIABLES_SIZE * count); /* BitFill(3, 0, ..., 0x50 * count, 16) */
    for (i = 0; i < count; i++) {
        /* There is no clamp to maxAmountOfTracks here, as in the assembly. */
        pVariables->unk_0 = 1;
        /* strh 0x0C02 at bendRange, str 0x00404000 (0x40 << 16 | 0x40 << 8) at volume */
        pVariables->bendRange = 2;
        pVariables->unk_1B = 0xC;
        pVariables->volume = 0;
        pVariables->unk_5 = 0x40;
        pVariables->unk_6 = 0x40;
        pVariables->unk_7 = 0;
        pVariables->pRawData = (const u8*)Resolve(Read32LE(header + 8 + i * 4));
        pVariables = (struct TrackVariables*)((u8*)pVariables + TRACK_VARIABLES_SIZE);
    }

done:
    pTrack->occupied = FALSE;
    return 0;
}

/* ---- Tempo -------------------------------------------------------------------- */

u32 AudioC_unk_4cfc(struct TrackData* pTrack) {
    /* (unk_C | unk_E << 16) + (that << 16), upper half: unk_E + unk_C. */
    u32 total = (u32)(pTrack->unk_E + pTrack->unk_C) & 0xFFFF;
    u32 result = 0;
    if (total & 0xFF00) {
        result = total >> 8;
        total &= 0xFF;
    }
    pTrack->unk_E = (u16)total;
    return result;
}

void AudioC_unk_4d1c(struct TrackData* pTrack, struct TrackVariables* pVariables) {
    u32 tempo = (u32)pVariables->pRawData[1] << 1;
    pVariables->pRawData += 2;
    pTrack->unk_A = (u16)tempo;
    if (tempo == 0x96)
        pTrack->unk_C = 0x100;
    else
        pTrack->unk_C = (u16)(((tempo << 8) * 0x1B4Eu) >> 20);
    pTrack->unk_E = 0;
}

/* ---- Per-frame update ----------------------------------------------------------- */

void AudioC_UpdateAudio(void) {
    const struct TrackGroupROMData* pGroup = sMusicTrackDataRom;
    struct SoundQueue* pQueue = gSoundQueue;
    u32 count = (u32)(uintptr_t)gNumMusicPlayers;
    u32 priority;

    do {
        if (pQueue->exists) {
            AudioC_InitTrack(pGroup->pTrack, (const u32*)pQueue->pHeader);
            /* `str r0` over exists..fadingTimer, then pHeader: the whole entry. */
            pQueue->exists = 0;
            pQueue->priority = 0;
            pQueue->fadingTimer = 0;
            pQueue->pHeader = NULL;
        }
        pGroup++;
        pQueue++;
    } while (--count != 0);

    pGroup = sMusicTrackDataRom;
    priority = gMusicInfo.priority;
    if (priority & 0x80) {
        if ((priority & 0x7F) != 4) {
            /* Every player but the first. */
            pGroup++;
            count = (u32)(uintptr_t)gNumMusicPlayers - 1;
            goto update_all;
        }
    } else if (priority == 4) {
        /* Only the first or the second player, as unk_20 says (no NULL check). */
        if (gMusicInfo.unk_20 != 0) pGroup++;
        if (pGroup->pTrack->flags & 2) UpdateTrack(pGroup->pTrack);
    } else {
        count = (u32)(uintptr_t)gNumMusicPlayers;
    update_all:
        do {
            struct TrackData* pTrack = pGroup->pTrack;
            if (pTrack != NULL && (pTrack->flags & 2)) UpdateTrack(pTrack);
            pGroup++;
        } while (--count != 0);
    }

    if (!gMusicInfo.occupied) {
        gMusicInfo.occupied = TRUE;
        UpdatePsgSounds();
        UpdateMusic();
    }
    gMusicInfo.occupied = FALSE;
}

/* ---- Note setup ---------------------------------------------------------------- */

void AudioC_unk_4e10(struct TrackVariables* pVariables) {
    /* pSample2 is the voice group, a host pointer into the ROM (AudioCommand_Voice
     * resolved it); envelope2 holds the host pointer to a keysplit's key map. */
    const u8* pVoice = port_rom_raw(pVariables->pSample2); /* read as ROM bytes */
    u32 word;
    u32 type;

    if ((pVariables->unk_0 & 0xF0) == 0x80) {
        /* Drum kit: one voice per key. */
        u32 pan;
        pVoice += pVariables->unk_1 * 12;
        pVariables->unk_35 = pVoice[1];
        pan = pVoice[3];
        if (pan == 0)
            pVariables->unk_6 = 0x40;
        else if ((s32)pan - 0x80 >= 0) /* subs; bmi */
            pVariables->unk_6 = (u8)(pan - 0x80);
    } else {
        /* Keysplit: the key map picks the voice. */
        const u8* pKeyMap;
        memcpy(&pKeyMap, &pVariables->envelope2, sizeof(pKeyMap));
        pKeyMap = port_rom_raw(pKeyMap);
        pVoice += pKeyMap[pVariables->unk_1] * 12;
    }

    word = Read32LE(pVoice);
    pVariables->channel = (u8)word;
    pVariables->unk_36 = (u8)(word >> 16); /* lsls #8; lsrs #24: byte 2 */
    type = word & 7;
    if (type == 0) {
        pVariables->pSample1 = (u32*)Resolve(Read32LE(pVoice + 4));
    } else if ((s32)type <= 2) {
        /* Square waves: duty in byte 3, the register value in pSample1's low byte. */
        u32 duty = word >> 24;
        if ((duty & 0x80) || !(duty & 0x70)) duty = 8;
        pVariables->unk_37 = (u8)duty;
        pVariables->pSample1 =
            (u32*)(((uintptr_t)pVariables->pSample1 & ~(uintptr_t)0xFF) | (u8)(Read32LE(pVoice + 4) << 6));
    } else if (type == 3) {
        AudioC_UploadSampleToWaveRam((const u32*)Resolve(Read32LE(pVoice + 4)));
    } else if (type == 4) {
        /* `strb` of the noise setting into pSample1: only its least significant
         * byte (GetNoteDelay reads it back as (u8)(u32)pSample1). */
        pVariables->pSample1 =
            (u32*)(((uintptr_t)pVariables->pSample1 & ~(uintptr_t)0xFF) | (u8)(Read32LE(pVoice + 4) << 3));
    }
    memcpy(&pVariables->envelope1, pVoice + 8, sizeof(pVariables->envelope1));
}

void AudioC_unk_4eb4(struct TrackVariables* pVariables) {
    /* r2: the word at keyShift (keyShift, unk_1D, tune, unk_1F). */
    const u32 tuneWord = (u32)pVariables->keyShift | ((u32)pVariables->unk_1D << 8) |
                         ((u32)pVariables->tune << 16) | ((u32)pVariables->unk_1F << 24);
    const u32 lfoWord = (u32)pVariables->lfoSpeed | ((u32)pVariables->modulationDepth << 8) |
                        ((u32)pVariables->modulationType << 16) | ((u32)(u8)pVariables->unk_13 << 24);
    s32 bend = pVariables->pitchBend;
    s32 value;

    if (bend > 0) bend++;
    value = bend * pVariables->bendRange * 4;
    value += (s32)(s8)tuneWord * 256;                 /* keyShift */
    value += (s32)(s8)(tuneWord >> 8) * 256;          /* unk_1D */
    value += ((s32)(tuneWord << 8) >> 22) * 4;        /* asr #22 of tune:unk_1D, then lsl #2 */
    value += (s32)(tuneWord >> 24);                   /* unk_1F */

    if ((lfoWord & 0xFF) && (lfoWord & 0xFF00) && ((lfoWord >> 16) & 0xFF) == 0)
        value += ((s32)lfoWord >> 24) * 4 * 12;       /* unk_13, LFO on pitch */

    pVariables->unk_17 = (s8)(value >> 8);
    pVariables->unk_18 = (u8)value;
}

void AudioC_unk_4f10(struct TrackVariables* pVariables) {
    const u32 volWord = (u32)pVariables->volume | ((u32)pVariables->unk_5 << 8) |
                        ((u32)pVariables->unk_6 << 16) | ((u32)pVariables->unk_7 << 24);
    const u32 modHalf = (u32)pVariables->modulationType | ((u32)(u8)pVariables->unk_13 << 8);
    s32 volume = (s32)(((volWord & 0xFF) * ((volWord >> 8) & 0xFF)) >> 5);
    s32 pan;
    u32 left, right;

    if ((modHalf & 0xFF) == 1) {
        volume = volume * ((s32)(s8)(modHalf >> 8) + 0x41);
        volume >>= 6;
        if (volume >= 0xFF) volume = 0xFF;
    }

    pan = (s32)volWord >> 24;              /* unk_7, signed */
    pan += (s32)((volWord >> 16) & 0xFF);  /* unk_6 */
    pan -= 0x40;
    if (pan >= 0x3F)
        pan = 0x3F;
    else if (pan < -0x40)
        pan = 0x40; /* `rsbs r1, r4, #0` with r4 = -64: +64, not -64 (as the original) */

    if ((modHalf & 0xFF) == 2) {
        pan += (s8)(modHalf >> 8);
        if (pan >= 0x3F)
            pan = 0x3F;
        else if (pan + 0x40 < 0)
            pan = -0x40;
    }

    /* lsrs: logical shifts of the products. */
    left = ((u32)(pan + 0x40) * (u32)volume) >> 7;
    right = ((u32)(0x40 - pan) * (u32)volume) >> 7;
    /* `strh (left | right << 8)` over unk_8, unk_9: left can spill into unk_9. */
    {
        u32 half = (left | (right << 8)) & 0xFFFF;
        pVariables->unk_8 = (u8)half;
        pVariables->unk_9 = (u8)(half >> 8);
    }
}

void AudioC_unk_4f8c(struct SoundChannel* pChannel, struct TrackVariables* pVariables, s32 priority) {
    u32 word;
    u32 key;
    s32 note;
    u32 frequency;

    pChannel->unk_13 |= 2;
    unk_20a4(pChannel);

    if (pVariables->pChannel != NULL) pVariables->pChannel->pChannel1 = pChannel;
    pChannel->pChannel2 = pVariables->pChannel;
    pChannel->pChannel1 = NULL;
    pChannel->pVariables = pVariables;
    pVariables->pChannel = pChannel;

    /* `str 1 | channel << 8 | priority << 16 | unk_6 << 24` over unk_0..unk_3. */
    word = 1u | ((u32)pVariables->channel << 8) | ((u32)priority << 16) | ((u32)pVariables->unk_6 << 24);
    pChannel->unk_0 = (u8)word;
    pChannel->unk_1 = (u8)(word >> 8);
    pChannel->unk_2 = (u8)(word >> 16);
    pChannel->unk_3 = (u8)(word >> 24);

    pChannel->envelope = pVariables->envelope1;
    pChannel->unk_C = pVariables->unk_C;
    pChannel->unk_D = pVariables->unk_D;
    pChannel->unk_E = pVariables->unk_E;
    pChannel->unk_F = pVariables->unk_F;

    pChannel->pSample = pVariables->pSample1;
    pChannel->pData = (u8*)pVariables->pSample1 + 0x10;
    pChannel->pSize = (u32*)((u8*)pVariables->pSample1 + 0xC);

    key = pVariables->unk_1;
    pChannel->unk_6 = (u8)key;
    if ((pVariables->unk_0 & 0xF0) == 0x80) key = pVariables->unk_35;
    pChannel->unk_7 = (u8)key;

    note = (s32)pVariables->unk_17 + (s32)key;
    if (note < 0)
        note = 0;
    else if (note > 0x7F)
        note = 0x7F;

    frequency = MidiKey2Freq(pVariables->pSample1, (u8)note, pVariables->unk_18);
    if (frequency == gMusicInfo.sampleRate)
        pChannel->unk_1C = 0x4000;
    else
        pChannel->unk_1C = AudioC_GetNoteFrequency(frequency, gMusicInfo.pitch);
}

/* ---- Track commands ------------------------------------------------------------- */

void AudioC_AudioCommand_Goto(struct TrackVariables* pVariables) {
    /* The target is a GBA ROM address, read whatever its alignment. */
    pVariables->pRawData = (const u8*)Resolve(Read32LE(pVariables->pRawData + 1));
}

void AudioC_AudioCommand_PatternPlay(struct TrackVariables* pVariables) {
    const u8* pReturn = pVariables->pRawData + 5;
    pVariables->pRawData = (const u8*)Resolve(Read32LE(pVariables->pRawData + 1));
    /* First free slot of three; with all three in use, nothing is stored. */
    if (pVariables->patternStartPointers[0] == NULL)
        pVariables->patternStartPointers[0] = pReturn;
    else if (pVariables->patternStartPointers[1] == NULL)
        pVariables->patternStartPointers[1] = pReturn;
    else if (pVariables->patternStartPointers[2] == NULL)
        pVariables->patternStartPointers[2] = pReturn;
}

/* ---- PSG registers (written into the emulated I/O block, as the port's assembly does) */

void AudioC_UploadSampleToWaveRam(const u32* pSample) {
    gIoMem[0x70] = 0x40; /* REG_SOUND3CNT_L: wave RAM bank select while writing */
    memcpy(&gIoMem[0x90], port_rom_raw(pSample), 16);
    gIoMem[0x70] = 0;
}

void AudioC_unk_5104(struct PsgSoundData* pSound) {
    u32 frequency = pSound->unk_14;
    const u32 channel = pSound->pVariables->channel;
    /* r3: the halfword unk_11 | unk_12 << 8 */
    const u32 settings = (u32)pSound->unk_11 | ((u32)pSound->unk_12 << 8);

    if ((s32)channel > 8) {
        const u32 bias = gIoMem[0x89] & 0xC0; /* SOUNDBIAS high byte: resolution */
        if (bias < 0x40) {
            frequency += 2;
            frequency &= 0xC7FE;
        } else if (bias < 0x80) {
            frequency += 1;
            frequency &= 0xC7FE;
        }
    }

    switch (channel & 7) {
        case 1:
            gIoMem[0x60] = pSound->unk_10;
            Write16LE(&gIoMem[0x62], settings);
            Write16LE(&gIoMem[0x64], frequency);
            Write16LE(&gIoMem[0x64], frequency); /* written twice, as the original */
            break;
        case 2:
            Write16LE(&gIoMem[0x68], settings);
            Write16LE(&gIoMem[0x6C], frequency);
            break;
        case 3:
            gIoMem[0x70] = pSound->unk_10;
            Write16LE(&gIoMem[0x72], settings);
            Write16LE(&gIoMem[0x74], frequency);
            break;
        case 4:
            Write16LE(&gIoMem[0x78], settings);
            Write16LE(&gIoMem[0x7C], frequency);
            break;
        default:
            break;
    }
    pSound->unk_14 = (u16)(frequency & 0x7FFF);
}

/* ---- The mixer (soundcode.s) ------------------------------------------------------ */

u32 AudioC_GetNoteFrequency(u32 frequency, u32 pitch) {
    /* umull; (hi << 14) + (lo >> 18) */
    return (u32)(((uint64_t)frequency * pitch) >> 18);
}

/* One interpolated sample at fixed-point position `pos` (18.14), added to the
 * L/R pair at pMix. The assembly shifts the (possibly negative) product with
 * `lsr`, so the u32 arithmetic below is on purpose: the extra high bits it
 * adds only reach bits SoundCodeC drops. */
static inline void MixInterpolated(u32* pMix, const s8* pData, u32 pos, u32 volL, u32 volR) {
    const u32 index = pos >> 14;
    const s32 s0 = pData[index];
    const s32 s1 = pData[index + 1];
    const u32 product = (u32)(s1 - s0) * (pos & 0x3FFF);
    const u32 sample = (u32)s0 + (product >> 14);
    pMix[0] += sample * volL;
    pMix[1] += sample * volR;
}

static inline void MixDirect(u32* pMix, const s8* pData, u32 pos, u32 volL, u32 volR) {
    const u32 sample = (u32)(s32)pData[pos];
    pMix[0] += sample * volL;
    pMix[1] += sample * volR;
}

/* Mixes `count` groups of four samples of the channel into pMix (L, R words).
 * Three variants as in the assembly: interpolated (the default), 0x08 one
 * sample per output sample, and 0x20 backwards. A non-looping sample that
 * reaches its end turns the channel off (unk_0 = 0) and does not store the
 * position. */
void AudioC_SoundCodeA(struct SoundChannel* pChannel, u32* pMix, u8 count) {
    const u8* pHeader = (const u8*)pChannel->pSample;
    const s8* pData = (const s8*)port_rom_raw(pChannel->pData);
    const u32 volL = pChannel->unk_11;
    const u32 volR = pChannel->unk_12;
    const u32 length = Read32LE(pHeader + 0xC);
    u32 pos = pChannel->unk_18;
    u32 groups = count;

    if (pChannel->unk_1 == 0x08) {
        const u32 loops = Read16LE(pHeader + 2);
        const u32 loopStart = Read32LE(pHeader + 8);
        int k;
        do {
            if ((s32)(pos + 4) < (s32)length) {
                /* All four inside the sample. */
                MixDirect(pMix, pData, pos, volL, volR);
                MixDirect(pMix + 2, pData, pos + 1, volL, volR);
                MixDirect(pMix + 4, pData, pos + 2, volL, volR);
                MixDirect(pMix + 6, pData, pos + 3, volL, volR);
                pMix += 8;
                pos += 4;
            } else {
                for (k = 0; k < 4; k++) {
                    MixDirect(pMix, pData, pos, volL, volR);
                    pMix += 2;
                    pos++;
                    if (pos == length) {
                        if (loops == 0) goto stop;
                        pos = loopStart;
                    }
                }
            }
        } while (--groups != 0);
    } else if (pChannel->unk_1 == 0x20) {
        const u32 step = (pChannel->unk_1C ^ 0xFFFFFFFFu) + 1; /* -unk_1C */
        int k;
        do {
            if ((s32)(pos + step * 4) >> 14 > 0) {
                for (k = 0; k < 4; k++) {
                    MixInterpolated(pMix, pData, pos, volL, volR);
                    pMix += 2;
                    pos += step;
                }
            } else {
                /* Near the start: stop once the index reaches 0. Three checked
                 * samples, then the fourth and the channel ends. */
                for (k = 0; k < 3; k++) {
                    MixInterpolated(pMix, pData, pos, volL, volR);
                    pMix += 2;
                    pos += step;
                    if ((s32)(pos >> 14) <= 0) goto stop;
                }
                MixInterpolated(pMix, pData, pos, volL, volR);
                goto stop;
            }
        } while (--groups != 0);
    } else {
        const u32 step = pChannel->unk_1C;
        const u32 loops = Read16LE(pHeader + 2);
        const u32 loopBack = Read32LE(pHeader + 8) - length; /* lr: loop start - length */
        int k;
        do {
            if ((s32)length > (s32)((pos + step * 4) >> 14)) { /* subs; bgt */
                /* lbl_080046d0: the four samples stay inside the sample. */
                for (k = 0; k < 4; k++) {
                    MixInterpolated(pMix, pData, pos, volL, volR);
                    pMix += 2;
                    pos += step;
                }
            } else if (loops == 0) {
                /* Reaches the end: three checked samples, then the fourth and stop. */
                for (k = 0; k < 3; k++) {
                    MixInterpolated(pMix, pData, pos, volL, volR);
                    pMix += 2;
                    pos += step;
                    if ((s32)(pos >> 14) >= (s32)length) goto stop;
                }
                MixInterpolated(pMix, pData, pos, volL, volR);
                goto stop;
            } else {
                /* Looping: after each of the first three, wrap and finish the
                 * group unchecked (lbl_08004700/30/60); the fourth always wraps. */
                for (k = 0; k < 4; k++) {
                    MixInterpolated(pMix, pData, pos, volL, volR);
                    pMix += 2;
                    pos += step;
                    if (k == 3 || (s32)(pos >> 14) >= (s32)length) {
                        pos = (pos & 0x3FFF) + (((pos >> 14) + loopBack) << 14);
                        for (k++; k < 4; k++) {
                            MixInterpolated(pMix, pData, pos, volL, volR);
                            pMix += 2;
                            pos += step;
                        }
                    }
                }
            }
        } while (--groups != 0);
    }
    pChannel->unk_18 = pos;
    return;

stop:
    pChannel->unk_0 = 0;
}

/* Reverb: each output pair is the four inputs (both buffers, both halves)
 * summed, times the reverb level, / 512 rounded toward zero, << 7. Two samples
 * per step, `count` steps. Returns the end of pDst. */
u8* AudioC_SoundCodeB(u32* pSrc1, u32* pSrc2, u32* pDst, u8 count) {
    const s8* a = (const s8*)pSrc1;
    const s8* b = (const s8*)pSrc2;
    const s32 reverb = gMusicInfo.reverb;
    u32 steps = count;

    do {
        s32 first = a[PCM_HALF] + a[0] + b[0] + b[PCM_HALF];
        s32 second = a[1] + b[1] + a[1 + PCM_HALF] + b[1 + PCM_HALF];
        first *= reverb;
        second *= reverb;
        /* asrs #9; addmi #1 */
        first = (first >> 9) + (first < 0 ? 1 : 0);
        second = (second >> 9) + (second < 0 ? 1 : 0);
        pDst[0] = (u32)first << 7;
        pDst[1] = (u32)first << 7;
        pDst[2] = (u32)second << 7;
        pDst[3] = (u32)second << 7;
        pDst += 4;
        a += 2;
        b += 2;
    } while (--steps != 0);
    return (u8*)pDst;
}

/* Down-mix: bits 7..15 of each L/R word index sArray_808cce2, giving the
 * unsigned 8-bit PCM the GBA plays. Left goes to pDst, right to pDst + 0x600;
 * four samples per step. Returns the end of pMix. */
u8* AudioC_SoundCodeC(u32* pDst, u32* pMix, u8 count) {
    u8* left = (u8*)pDst;
    u8* right = left + PCM_HALF;
    u32 steps = count;
    int k;

    do {
        for (k = 0; k < 4; k++) {
            left[k] = sArray_808cce2[(pMix[k * 2] >> 7) & 0x1FF];
            right[k] = sArray_808cce2[(pMix[k * 2 + 1] >> 7) & 0x1FF];
        }
        left += 4;
        right += 4;
        pMix += 8;
    } while (--steps != 0);
    return (u8*)pMix;
}

/* The GBA's per-VBlank DMA restart for the sound FIFOs. Nothing calls it in the
 * port (the FIFOs do not exist); only its counter is kept. */
void AudioC_sub_08004ad8(void) {
    u8 next = (u8)(gMusicInfo.unk_10 + 1);
    if (next == gMusicInfo.unk_E) next = 0;
    gMusicInfo.unk_10 = next;
}
