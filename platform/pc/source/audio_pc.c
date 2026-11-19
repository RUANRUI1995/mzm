/* See audio_pc.h. */
#include "audio_pc.h"

#include "types.h"
#include "structs/audio.h"
#include "port_mzm_audio_glue.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>

static SDL_AudioDeviceID sDevice;
static bool sWantDevice;
static unsigned int sDeviceRate;
static unsigned int sPendingRate;
static FILE* sWav;
static unsigned int sWavRate, sWavFrames;

/* --wav: everything the engine produces, as a 16-bit stereo WAV at the
 * engine's rate (the header is completed on shutdown). */
static void WriteLE32(FILE* f, unsigned int v) {
    unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
    fwrite(b, 1, 4, f);
}

static void WriteWavHeader(FILE* f, unsigned int rate, unsigned int frames) {
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    WriteLE32(f, 36 + frames * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    WriteLE32(f, 16);
    WriteLE32(f, 1 | (2u << 16));      /* PCM, 2 channels */
    WriteLE32(f, rate);
    WriteLE32(f, rate * 4);
    WriteLE32(f, 4 | (16u << 16));     /* block align 4, 16 bits */
    fwrite("data", 1, 4, f);
    WriteLE32(f, frames * 4);
}

bool Audio_PC_OpenWav(const char* path) {
    sWav = fopen(path, "wb");
    if (!sWav) return false;
    WriteWavHeader(sWav, 0, 0);
    return true;
}

static void WavAppend(const short* frames, unsigned int count) {
    unsigned int i;
    if (!sWav) return;
    if (!sWavRate) sWavRate = sPendingRate;
    for (i = 0; i < count * 2; ++i) {
        unsigned char b[2] = { (unsigned char)frames[i], (unsigned char)((unsigned short)frames[i] >> 8) };
        fwrite(b, 1, 2, sWav);
    }
    sWavFrames += count;
}

/* The glue is shared with the 3DS, where the engine runs on its own thread
 * under this lock. Here it runs on the game thread: nothing to lock. */
void Port_AudioStateLock_Acquire(void) {}
void Port_AudioStateLock_Release(void) {}

static void OnEngineRate(unsigned int rate) { sPendingRate = rate; }

static void OpenDevice(unsigned int rate) {
    SDL_AudioSpec want, have;
    if (sDevice) {
        SDL_CloseAudioDevice(sDevice);
        sDevice = 0;
    }
    SDL_zero(want);
    want.freq = (int)rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    sDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!sDevice) {
        fprintf(stderr, "[PC] No audio device: %s\n", SDL_GetError());
        sWantDevice = false;
        return;
    }
    sDeviceRate = rate;
    SDL_PauseAudioDevice(sDevice, 0);
}

void Audio_PC_Init(bool openDevice) {
    sWantDevice = openDevice;
    if (sWantDevice && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[PC] SDL audio: %s\n", SDL_GetError());
        sWantDevice = false;
    }
    Port_MzmAudio_InitGlue();
    Port_MzmAudio_SetRateHook(OnEngineRate);
}

void Audio_PC_Shutdown(void) {
    if (sDevice) SDL_CloseAudioDevice(sDevice);
    sDevice = 0;
    if (sWav) {
        WriteWavHeader(sWav, sWavRate, sWavFrames);
        fclose(sWav);
        sWav = NULL;
    }
}

void Audio_PC_BeginFrame(void) {
    /* The GBA's sound DMA moves its read position once per frame; the engine
     * mixes from there (Port_MzmAudio_ProduceTick does the same on the 3DS). */
    if (gMusicInfo.unk_E != 0) {
        gMusicInfo.unk_10 += gMusicInfo.unk_C;
        if (gMusicInfo.unk_10 >= gMusicInfo.unk_E) gMusicInfo.unk_10 -= gMusicInfo.unk_E;
    }
    Port_MzmAudio_InstallSoundCodeCHook();
}

void Audio_PC_EndFrame(void) {
    const unsigned int write = Port_MzmAudio_RingWriteIndex();
    unsigned int read = Port_MzmAudio_RingReadIndex();

    if (sWantDevice && sPendingRate && sPendingRate != sDeviceRate) OpenDevice(sPendingRate);

    if (sWav) {
        unsigned int r = read;
        while (r != write) {
            const unsigned int index = r % MZM_AUDIO_RING_FRAMES;
            unsigned int run = write - r;
            if (run > MZM_AUDIO_RING_FRAMES - index) run = MZM_AUDIO_RING_FRAMES - index;
            WavAppend(&gMzmAudioRing[index * 2], run);
            r += run;
        }
    }

    if (sDevice) {
        /* Keep the device about 3 frames behind the game; past ~10, drop the
         * oldest instead of letting the delay grow (the game is paced by the
         * frame timer, not by the sound card). */
        const Uint32 bytesPerFrame = 4;
        const Uint32 queued = SDL_GetQueuedAudioSize(sDevice) / bytesPerFrame;
        const bool overfull = queued > sDeviceRate / 6;
        while (read != write) {
            const unsigned int index = read % MZM_AUDIO_RING_FRAMES;
            unsigned int run = write - read;
            if (run > MZM_AUDIO_RING_FRAMES - index) run = MZM_AUDIO_RING_FRAMES - index;
            if (!overfull) SDL_QueueAudio(sDevice, &gMzmAudioRing[index * 2], run * bytesPerFrame);
            read += run;
        }
    }
    Port_MzmAudio_AdvanceReadIndex(write);
}
