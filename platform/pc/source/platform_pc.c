/* PC platform layer: display timing, input, rendering, the bottom-screen UI and
 * the per-frame checks (trace, screenshots, dumps). Shared by the Linux and
 * Windows builds and by the headless PowerPC one; the window and devices are
 * behind frontend_pc.h. */
#include "platform_pc.h"
#include "platform_pc_options.h"
#include "port_trace_pc.h"
#include "port_rom.h"
#include "gba.h"
#include "port_gba_mem.h"
#include "virtuappu.h"
#include "cpu/mode1.h"
#include "structs/game_state.h"
#include "frontend_pc.h"
#include "port_bottom_ui.h"
#include "port_draw2d.h"
#include "audio_pc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool sHeadless = false;
static bool sQuit = false;
static u16 sFrameKeys = 0;     /* keys the game sees this frame */
static uint32_t sFrame = 0;    /* frames finished so far; the current one is sFrame */
static bool sRenderEveryFrame = false;
static uint32_t sFramebuffer[240 * 160];
static uint32_t sBottomFramebuffer[320 * 240];
static bool sBottomValid;
static bool sWindowOpen;

/* ---- Rendering ------------------------------------------------------------- */

static void SetupPpu(void) {
    VirtuaPPUMode1GbaMemory memory = { gIoMem, gVram, gBgPltt, gObjPltt, gOamMem };
    virtuappu_mode1_bind_gba_memory(&memory);
    virtuappu_mode1_set_output_buffer(sFramebuffer, 240);
}

static void RenderFrame(void) {
    const uint16_t dispcnt = (uint16_t)(gIoMem[0] | (gIoMem[1] << 8));
    const uint8_t gbaMode = (uint8_t)(dispcnt & 7);
    PPUMemory ppu;

    ppu.frame_width = 240;
    ppu.frame_pitch = 240;
    /* virtuappu mode 1 = tiled (GBA mode 0), 2 = affine (GBA modes 1/2). */
    ppu.mode = (gbaMode == 1 || gbaMode == 2) ? 2 : 1;
    ppu.reserved = 0;
    virtuappu_mode1_render_frame(&ppu);
}

/* The bottom-screen UI (port/ui/port_bottom_ui.c) runs when something shows
 * it: a layout with the UI, or a --bottom-shot. Like the 3DS, it ticks every
 * frame and redraws only when it asks to. Off otherwise, so plain trace runs
 * stay what they were (the map view reads game data while drawing). */
static bool BottomActive(void) {
    return Frontend_ShowsBottom() || gPcOptions.bottomShotCount > 0 || gPcOptions.bottomAlways;
}

static void RenderBottom(void) {
    Port_BottomUI_FrameTick();
    if (Port_BottomUI_WantsRedraw() || !sBottomValid) {
        Draw2D_SoftBegin(sBottomFramebuffer, 320, 240, 320, Draw2D_Color32(0, 0, 0, 255));
        Port_BottomUI_Render();
        sBottomValid = true;
    }
}

bool Platform_PC_Init(bool headless) {
    sHeadless = headless;
    SetupPpu();
    sRenderEveryFrame = !sHeadless || Trace_LogEnabled() || gPcOptions.shotCount > 0;
    if (!sHeadless) {
        if (!Frontend_Open(gPcOptions.scale, gPcOptions.layout)) return false;
        sWindowOpen = true;
    }
    Audio_PC_Init(sWindowOpen && !gPcOptions.mute);
    return true;
}

void Platform_PC_Cleanup(void) {
    /* A save still being written (src/sram/sram.c writes it from a thread) reaches mzm.sav. */
    extern void Port_FlushSramWait(void);
    Port_FlushSramWait();
    Trace_Close();
    Audio_PC_Shutdown();
    Frontend_Close();
    sWindowOpen = false;
}

/* ---- Display timing -------------------------------------------------------
 * The game busy-waits on REG_VCOUNT/REG_DISPSTAT (e.g. SetupSoundTransfer in
 * src/audio_wrappers.c, before the main loop starts). There is no display
 * here, so every read of either register advances one virtual scanline
 * (port_gba_mem.c calls this). Unlike a wall-clock thread this gives the same
 * values on every run, which the replay/trace checks need. */
#define GBA_SCANLINES_PER_FRAME 228
#define GBA_VBLANK_START_LINE 160

static u8 sScanline = GBA_VBLANK_START_LINE;

static void PublishScanline(void) {
    u16 dispstat = (u16)(gIoMem[4] | (gIoMem[5] << 8));
    if (sScanline >= GBA_VBLANK_START_LINE && sScanline != GBA_SCANLINES_PER_FRAME - 1)
        dispstat |= DSTAT_IN_VBLANK;
    else
        dispstat &= ~DSTAT_IN_VBLANK;
    gIoMem[4] = (u8)dispstat;
    gIoMem[5] = (u8)(dispstat >> 8);
    gIoMem[6] = sScanline;
    gIoMem[7] = 0;
}

void Platform_PC_ScanlineTick(void) {
    if (++sScanline >= GBA_SCANLINES_PER_FRAME) sScanline = 0;
    PublishScanline();
}

/* ---- Input ------------------------------------------------------------------- */

u16 Platform_PC_PollKeys(void) {
    const u16 liveKeys = Frontend_Poll(&sQuit);
    if (Frontend_TakeLayoutChanged()) sBottomValid = false;
    Frontend_PumpTouch();
    if (BottomActive()) {
        /* --tap: the stylus down for three frames, then lifted. */
        int t;
        for (t = 0; t < gPcOptions.tapCount; ++t) {
            const uint32_t start = gPcOptions.taps[t].frame;
            if (sFrame >= start && sFrame < start + 3)
                Port_BottomUI_HandleTouchDrag(gPcOptions.taps[t].x, gPcOptions.taps[t].y, sFrame == start);
            else if (sFrame == start + 3)
                Port_BottomUI_TouchReleased();
        }
    }
    if (Trace_HasReplay()) {
        sFrameKeys = Trace_ReplayKeys(sFrame);
    } else if (gPcOptions.pulseA) {
        /* --test without a replay: tap A every 40 frames to get through menus. */
        sFrameKeys = (sFrame % 40) < 4 ? KEY_A : 0;
    } else {
        sFrameKeys = liveKeys;
    }
    Trace_RecordKeys(sFrame, sFrameKeys);
    /* REG_KEY_INPUT is active low */
    gba_write16(REG_KEY_INPUT, (u16)((~sFrameKeys) & 0x03FF));
    return sFrameKeys;
}

/* ---- Frame ------------------------------------------------------------------ */

void Platform_PC_VBlank(void) {
    /* The game waited for VBlank: the display is at its first VBlank line. */
    sScanline = GBA_VBLANK_START_LINE;
    PublishScanline();
    Platform_PC_PollKeys();
}

static void ExitRun(void) {
    Platform_PC_Cleanup();
    exit(0);
}

void Platform_PC_AfterVBlank(void) {
    int i;

    /* The sound the engine mixed this frame, then the next frame's DMA step. */
    Audio_PC_EndFrame();
    Audio_PC_BeginFrame();

    if (sRenderEveryFrame) RenderFrame();
    if (BottomActive()) RenderBottom();
    Trace_LogFrame(sFrame, sFrameKeys, sRenderEveryFrame ? sFramebuffer : NULL);

    for (i = 0; i < gPcOptions.shotCount; ++i) {
        if (gPcOptions.shots[i].frame == sFrame) {
            if (Trace_WritePpm(gPcOptions.shots[i].path, sFramebuffer))
                fprintf(stderr, "[PC] Frame %u written to %s\n", (unsigned)sFrame, gPcOptions.shots[i].path);
            else
                fprintf(stderr, "[PC] Cannot write %s\n", gPcOptions.shots[i].path);
        }
    }

    for (i = 0; i < gPcOptions.bottomShotCount; ++i) {
        if (gPcOptions.bottomShots[i].frame == sFrame) {
            if (Trace_WritePpmSized(gPcOptions.bottomShots[i].path, sBottomFramebuffer, 320, 240))
                fprintf(stderr, "[PC] Bottom screen of frame %u written to %s\n", (unsigned)sFrame,
                        gPcOptions.bottomShots[i].path);
            else
                fprintf(stderr, "[PC] Cannot write %s\n", gPcOptions.bottomShots[i].path);
        }
    }

    for (i = 0; i < gPcOptions.dumpCount; ++i) {
        if (gPcOptions.dumps[i].frame == sFrame && !Trace_DumpMemory(gPcOptions.dumps[i].dir, sFrame))
            fprintf(stderr, "[PC] Cannot dump frame %u into %s\n", (unsigned)sFrame, gPcOptions.dumps[i].dir);
    }

    if (!gPcOptions.quiet && (sFrame % 600 == 0)) {
        printf("[PC] Frame %6u | MainGameMode: 0x%02X | SubMode1: 0x%02X\n",
               (unsigned)sFrame, (unsigned)gMainGameMode, (unsigned)(u16)gSubGameMode1);
        fflush(stdout);
    }

    sFrame++;
    if (gPcOptions.maxFrames > 0 && sFrame >= gPcOptions.maxFrames) {
        fprintf(stderr, "[PC] Ran %u frames.\n", (unsigned)sFrame);
        ExitRun();
    }
    if (sQuit) ExitRun();

    if (sWindowOpen)
        Frontend_PresentAndPace(sFramebuffer, Frontend_ShowsBottom() ? sBottomFramebuffer : NULL);
}

uint32_t Platform_PC_CurrentFrame(void) { return sFrame; }
