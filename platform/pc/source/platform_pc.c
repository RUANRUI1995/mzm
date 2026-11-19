/* PC platform layer: display timing, input, rendering and the SDL2 window.
 * Shared by the Linux and Windows builds; SDL2 is also what a Wii U frontend
 * would use (issue #39). */
#include "platform_pc.h"
#include "platform_pc_options.h"
#include "port_trace_pc.h"
#include "port_rom.h"
#include "gba.h"
#include "port_gba_mem.h"
#include "virtuappu.h"
#include "cpu/mode1.h"
#include "structs/game_state.h"
#include "screens_pc.h"
#include "port_bottom_ui.h"
#include "port_draw2d.h"

#define SDL_MAIN_HANDLED /* main() stays ours on Windows too */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool sHeadless = false;
static bool sQuit = false;
static u16 sLiveKeys = 0;      /* keys held on the keyboard and controllers */
static u16 sFrameKeys = 0;     /* keys the game sees this frame */
static uint32_t sFrame = 0;    /* frames finished so far; the current one is sFrame */
static bool sRenderEveryFrame = false;
static uint32_t sFramebuffer[240 * 160];
static uint32_t sBottomFramebuffer[320 * 240];
static bool sBottomValid;
static bool sWindowOpen;
static SDL_GameController* sController;
static Uint64 sNextFrameDeadline;
static bool sFastForward;

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

static bool OpenWindow(void) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "[PC] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    if (!Screens_Open(gPcOptions.scale, (ScreensLayout)gPcOptions.layout)) return false;
    sWindowOpen = true;
    sNextFrameDeadline = SDL_GetPerformanceCounter();
    return true;
}

/* The bottom-screen UI (port/ui/port_bottom_ui.c) runs when something shows
 * it: a layout with the UI, or a --bottom-shot. Like the 3DS, it ticks every
 * frame and redraws only when it asks to. Off otherwise, so plain trace runs
 * stay what they were (the map view reads game data while drawing). */
static bool BottomActive(void) {
    return (sWindowOpen && Screens_ShowsBottom()) || gPcOptions.bottomShotCount > 0 || gPcOptions.bottomAlways;
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
    if (!sHeadless && !OpenWindow()) return false;
    return true;
}

void Platform_PC_Cleanup(void) {
    Trace_Close();
    if (sController) SDL_GameControllerClose(sController);
    sController = NULL;
    if (sWindowOpen) {
        Screens_Close();
        SDL_Quit();
        sWindowOpen = false;
    }
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

/* ---- Input -------------------------------------------------------------------
 * Keyboard: arrows, Z = A, X = B, A = L, S = R, Enter = START,
 * Backspace or right Shift = SELECT, hold Tab = fast forward, Esc = quit,
 * F2 = screen layout (screens_pc.h). The mouse on the bottom screen is the
 * stylus.
 * A game controller works too (first one plugged in). */

static u16 KeyForScancode(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_Z:         return KEY_A;
        case SDL_SCANCODE_X:         return KEY_B;
        case SDL_SCANCODE_A:         return KEY_L;
        case SDL_SCANCODE_S:         return KEY_R;
        case SDL_SCANCODE_RETURN:    return KEY_START;
        case SDL_SCANCODE_BACKSPACE:
        case SDL_SCANCODE_RSHIFT:    return KEY_SELECT;
        case SDL_SCANCODE_LEFT:      return KEY_LEFT;
        case SDL_SCANCODE_RIGHT:     return KEY_RIGHT;
        case SDL_SCANCODE_UP:        return KEY_UP;
        case SDL_SCANCODE_DOWN:      return KEY_DOWN;
        default:                     return 0;
    }
}

static u16 ControllerKeys(void) {
    static const struct { SDL_GameControllerButton button; u16 key; } kMap[] = {
        { SDL_CONTROLLER_BUTTON_A, KEY_A },          { SDL_CONTROLLER_BUTTON_B, KEY_B },
        { SDL_CONTROLLER_BUTTON_BACK, KEY_SELECT },  { SDL_CONTROLLER_BUTTON_START, KEY_START },
        { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, KEY_RIGHT }, { SDL_CONTROLLER_BUTTON_DPAD_LEFT, KEY_LEFT },
        { SDL_CONTROLLER_BUTTON_DPAD_UP, KEY_UP },   { SDL_CONTROLLER_BUTTON_DPAD_DOWN, KEY_DOWN },
        { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, KEY_R }, { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, KEY_L },
    };
    const int kDeadZone = 16000;
    u16 keys = 0;
    size_t i;
    if (!sController) return 0;
    for (i = 0; i < sizeof(kMap) / sizeof(kMap[0]); ++i)
        if (SDL_GameControllerGetButton(sController, kMap[i].button)) keys |= kMap[i].key;
    {
        int x = SDL_GameControllerGetAxis(sController, SDL_CONTROLLER_AXIS_LEFTX);
        int y = SDL_GameControllerGetAxis(sController, SDL_CONTROLLER_AXIS_LEFTY);
        if (x < -kDeadZone) keys |= KEY_LEFT;
        if (x > kDeadZone) keys |= KEY_RIGHT;
        if (y < -kDeadZone) keys |= KEY_UP;
        if (y > kDeadZone) keys |= KEY_DOWN;
    }
    return keys;
}

static void PollWindow(void) {
    SDL_Event ev;
    if (!sWindowOpen) return;
    while (SDL_PollEvent(&ev)) {
        if (Screens_HandleEvent(&ev)) sQuit = true;
        switch (ev.type) {
            case SDL_QUIT:
                sQuit = true;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                bool down = ev.type == SDL_KEYDOWN;
                if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE && down) sQuit = true;
                if (ev.key.keysym.scancode == SDL_SCANCODE_TAB) sFastForward = down;
                if (ev.key.keysym.scancode == SDL_SCANCODE_F2 && down && !ev.key.repeat) {
                    Screens_CycleLayout();
                    sBottomValid = false;
                }
                if (down) sLiveKeys |= KeyForScancode(ev.key.keysym.scancode);
                else sLiveKeys &= (u16)~KeyForScancode(ev.key.keysym.scancode);
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (!sController) sController = SDL_GameControllerOpen(ev.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (sController &&
                    ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(sController))) {
                    SDL_GameControllerClose(sController);
                    sController = NULL;
                }
                break;
            default:
                break;
        }
    }
}

u16 Platform_PC_PollKeys(void) {
    PollWindow();
    if (sWindowOpen && Screens_ShowsBottom()) Screens_PumpTouch();
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
        sFrameKeys = sLiveKeys | ControllerKeys();
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

/* GBA refresh: 16.78 MHz / 280896 cycles per frame = 59.73 Hz. */
static void PaceFrame(void) {
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 frameTicks = freq * 280896ull / 16777216ull;
    Uint64 now = SDL_GetPerformanceCounter();

    if (sFastForward) {
        sNextFrameDeadline = now;
        return;
    }
    sNextFrameDeadline += frameTicks;
    if (sNextFrameDeadline + frameTicks * 4 < now) sNextFrameDeadline = now; /* fell behind: don't catch up */
    while (now < sNextFrameDeadline) {
        Uint64 remainingMs = (sNextFrameDeadline - now) * 1000ull / freq;
        if (remainingMs > 1) SDL_Delay((Uint32)(remainingMs - 1));
        now = SDL_GetPerformanceCounter();
    }
}

static void ExitRun(void) {
    Platform_PC_Cleanup();
    exit(0);
}

void Platform_PC_AfterVBlank(void) {
    int i;

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

    if (sWindowOpen) {
        Screens_Present(sFramebuffer, Screens_ShowsBottom() ? sBottomFramebuffer : NULL);
        PaceFrame();
    }
}

uint32_t Platform_PC_CurrentFrame(void) { return sFrame; }
