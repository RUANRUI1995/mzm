/* See screens_pc.h. */
#include "screens_pc.h"

#include "port_bottom_ui.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>

#define TOP_W 240
#define TOP_H 160
#define BOTTOM_W 320
#define BOTTOM_H 240

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* top;     /* NULL when this window does not show the game */
    SDL_Texture* bottom;  /* NULL when this window does not show the UI */
    SDL_Rect topRect, bottomRect; /* where each one is drawn, in window pixels */
} ScreenWindow;

static ScreenWindow sMain, sSecond;
static ScreensLayout sLayout;
static int sScale;

/* Stylus: the mouse button went down on the UI and is still held. */
static bool sTouchHeld, sTouchNew, sTouchReleased;
static int sTouchX, sTouchY;

static const char* const kLayoutNames[SCREENS_LAYOUT_COUNT] = {
    "stacked", "side by side", "two windows", "game only",
};

/* The canvas (in GBA pixels) a single window lays out for this layout. */
static void CanvasSize(ScreensLayout layout, int* w, int* h) {
    switch (layout) {
        case SCREENS_STACKED: *w = BOTTOM_W; *h = TOP_H + BOTTOM_H; break;
        case SCREENS_SIDE:    *w = TOP_W + BOTTOM_W; *h = BOTTOM_H; break;
        default:              *w = TOP_W; *h = TOP_H; break;
    }
}

static bool CreateWindow(ScreenWindow* sw, const char* title, int w, int h, Uint32 flags, bool withTop,
                         bool withBottom) {
    sw->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!sw->window) goto fail;
    sw->renderer = SDL_CreateRenderer(sw->window, -1, 0);
    if (!sw->renderer) goto fail;
    /* Both buffers are 0xAABBGGRR words: ABGR8888 in SDL's packed-format terms. */
    if (withTop) {
        sw->top = SDL_CreateTexture(sw->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, TOP_W, TOP_H);
        if (!sw->top) goto fail;
    }
    if (withBottom) {
        sw->bottom = SDL_CreateTexture(sw->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                                       BOTTOM_W, BOTTOM_H);
        if (!sw->bottom) goto fail;
    }
    return true;
fail:
    fprintf(stderr, "[PC] Cannot create the window: %s\n", SDL_GetError());
    return false;
}

static void DestroyWindow(ScreenWindow* sw) {
    if (sw->top) SDL_DestroyTexture(sw->top);
    if (sw->bottom) SDL_DestroyTexture(sw->bottom);
    if (sw->renderer) SDL_DestroyRenderer(sw->renderer);
    if (sw->window) SDL_DestroyWindow(sw->window);
    sw->top = sw->bottom = NULL;
    sw->renderer = NULL;
    sw->window = NULL;
}

static bool CreateLayout(ScreensLayout layout) {
    int cw, ch;
    bool ok;
    sLayout = layout;
    CanvasSize(layout, &cw, &ch);
#ifdef __WIIU__
    /* The TV (1280x720) and the GamePad (854x480). Two windows: the game on
     * the TV, the UI on the GamePad; otherwise one window shown on both. */
    (void)cw;
    (void)ch;
    /* With two windows each present would swap and wait for the screen's
     * refresh, halving the frame rate: the TV's skips the swap, and the
     * GamePad's, presented after it (Screens_Present), swaps both. */
    ok = CreateWindow(&sMain, "Metroid: Zero Mission", 1280, 720,
                      layout == SCREENS_TWO_WINDOWS ? SDL_WINDOW_WIIU_TV_ONLY | SDL_WINDOW_WIIU_PREVENT_SWAP : 0,
                      true, layout == SCREENS_STACKED || layout == SCREENS_SIDE);
    if (ok && layout == SCREENS_TWO_WINDOWS)
        ok = CreateWindow(&sSecond, "Bottom screen", 854, 480, SDL_WINDOW_WIIU_GAMEPAD_ONLY, false, true);
#else
    ok = CreateWindow(&sMain, "Metroid: Zero Mission (PC reference build)", cw * sScale, ch * sScale,
                      SDL_WINDOW_RESIZABLE, true, layout == SCREENS_STACKED || layout == SCREENS_SIDE);
    if (ok && layout == SCREENS_TWO_WINDOWS) {
        ok = CreateWindow(&sSecond, "Bottom screen", BOTTOM_W * sScale, BOTTOM_H * sScale, SDL_WINDOW_RESIZABLE,
                          false, true);
        if (ok) {
            int x, y;
            SDL_GetWindowPosition(sMain.window, &x, &y);
            SDL_SetWindowPosition(sSecond.window, x + TOP_W * sScale + 16, y);
        }
    }
#endif
    printf("[PC] Screens: %s (F2 changes it)\n", kLayoutNames[layout]);
    fflush(stdout);
    return ok;
}

bool Screens_Open(int scale, ScreensLayout layout) {
    sScale = scale;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
#ifdef __WIIU__
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0"); /* the touch screen arrives as SDL_FINGER* */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
#endif
    return CreateLayout(layout);
}

void Screens_Close(void) {
    DestroyWindow(&sSecond);
    DestroyWindow(&sMain);
}

bool Screens_IsOpen(void) { return sMain.window != NULL; }
ScreensLayout Screens_Layout(void) { return sLayout; }
bool Screens_ShowsBottom(void) { return sMain.window && sLayout != SCREENS_GAME_ONLY; }

void Screens_CycleLayout(void) {
    ScreensLayout next = (ScreensLayout)((sLayout + 1) % SCREENS_LAYOUT_COUNT);
    if (sTouchHeld) sTouchReleased = true;
    sTouchHeld = false;
    Screens_Close();
    CreateLayout(next);
    Port_BottomUI_MarkDirty();
}

/* Integer-scaled placement of the canvas inside the window, centred. */
static void PlaceRects(ScreenWindow* sw, bool second) {
    int ww, wh, cw, ch, scale, ox, oy;
    SDL_GetRendererOutputSize(sw->renderer, &ww, &wh);
    if (second) {
        cw = BOTTOM_W;
        ch = BOTTOM_H;
    } else {
        CanvasSize(sLayout, &cw, &ch);
    }
    scale = ww / cw < wh / ch ? ww / cw : wh / ch;
    if (scale < 1) scale = 1;
    ox = (ww - cw * scale) / 2;
    oy = (wh - ch * scale) / 2;
    if (second) {
        sw->bottomRect = (SDL_Rect){ ox, oy, BOTTOM_W * scale, BOTTOM_H * scale };
        return;
    }
    switch (sLayout) {
        case SCREENS_STACKED:
            sw->topRect = (SDL_Rect){ ox + (BOTTOM_W - TOP_W) / 2 * scale, oy, TOP_W * scale, TOP_H * scale };
            sw->bottomRect = (SDL_Rect){ ox, oy + TOP_H * scale, BOTTOM_W * scale, BOTTOM_H * scale };
            break;
        case SCREENS_SIDE:
            sw->topRect = (SDL_Rect){ ox, oy + (BOTTOM_H - TOP_H) / 2 * scale, TOP_W * scale, TOP_H * scale };
            sw->bottomRect = (SDL_Rect){ ox + TOP_W * scale, oy, BOTTOM_W * scale, BOTTOM_H * scale };
            break;
        default:
            sw->topRect = (SDL_Rect){ ox, oy, TOP_W * scale, TOP_H * scale };
            break;
    }
}

/* Window pixels -> UI pixels; false when outside the UI. */
static bool ToBottom(Uint32 windowId, int wx, int wy, int* bx, int* by, bool clamp) {
    const ScreenWindow* sw = NULL;
    int ww, wh, lw, lh;
    float sx = 1.0f, sy = 1.0f;
    if (sMain.window && windowId == SDL_GetWindowID(sMain.window) && sMain.bottom) sw = &sMain;
    if (sSecond.window && windowId == SDL_GetWindowID(sSecond.window)) sw = &sSecond;
    if (!sw || sw->bottomRect.w == 0) return false;
    /* Mouse coordinates are in window points; the rects in output pixels. */
    SDL_GetWindowSize(sw->window, &lw, &lh);
    SDL_GetRendererOutputSize(sw->renderer, &ww, &wh);
    if (lw > 0 && lh > 0) {
        sx = (float)ww / (float)lw;
        sy = (float)wh / (float)lh;
    }
    *bx = (int)(((float)wx * sx - (float)sw->bottomRect.x) * BOTTOM_W / (float)sw->bottomRect.w);
    *by = (int)(((float)wy * sy - (float)sw->bottomRect.y) * BOTTOM_H / (float)sw->bottomRect.h);
    if (clamp) {
        if (*bx < 0) *bx = 0;
        if (*by < 0) *by = 0;
        if (*bx >= BOTTOM_W) *bx = BOTTOM_W - 1;
        if (*by >= BOTTOM_H) *by = BOTTOM_H - 1;
        return true;
    }
    return *bx >= 0 && *by >= 0 && *bx < BOTTOM_W && *by < BOTTOM_H;
}

bool Screens_HandleEvent(const void* event) {
    const SDL_Event* ev = (const SDL_Event*)event;
    int bx, by;
    switch (ev->type) {
        case SDL_MOUSEBUTTONDOWN:
            if (ev->button.button == SDL_BUTTON_LEFT &&
                ToBottom(ev->button.windowID, ev->button.x, ev->button.y, &bx, &by, false)) {
                sTouchHeld = true;
                sTouchNew = true;
                sTouchX = bx;
                sTouchY = by;
            }
            break;
        case SDL_MOUSEMOTION:
            if (sTouchHeld && ToBottom(ev->motion.windowID, ev->motion.x, ev->motion.y, &bx, &by, true)) {
                sTouchX = bx;
                sTouchY = by;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (ev->button.button == SDL_BUTTON_LEFT && sTouchHeld) {
                sTouchHeld = false;
                sTouchReleased = true;
            }
            break;
#ifdef __WIIU__
        /* The GamePad's touch screen: positions from 0 to 1 over the GamePad,
         * which shows the UI's window (or the only one). */
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION: {
            const ScreenWindow* pad = sSecond.window ? &sSecond : &sMain;
            int lw, lh;
            bool down = ev->type == SDL_FINGERDOWN;
            if (!pad->window) break;
            SDL_GetWindowSize(pad->window, &lw, &lh);
            if (ToBottom(SDL_GetWindowID(pad->window), (int)(ev->tfinger.x * lw), (int)(ev->tfinger.y * lh), &bx,
                         &by, !down)) {
                if (down) {
                    sTouchHeld = true;
                    sTouchNew = true;
                }
                if (sTouchHeld) {
                    sTouchX = bx;
                    sTouchY = by;
                }
            }
            break;
        }
        case SDL_FINGERUP:
            if (sTouchHeld) {
                sTouchHeld = false;
                sTouchReleased = true;
            }
            break;
#endif
        case SDL_WINDOWEVENT:
            if (ev->window.event == SDL_WINDOWEVENT_CLOSE) return true;
            if (ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) Port_BottomUI_MarkDirty();
            break;
        default:
            break;
    }
    return false;
}

void Screens_PumpTouch(void) {
    if (sTouchHeld) {
        Port_BottomUI_HandleTouchDrag(sTouchX, sTouchY, sTouchNew);
        sTouchNew = false;
    } else if (sTouchReleased) {
        Port_BottomUI_TouchReleased();
    }
    sTouchReleased = false;
}

static void PresentWindow(ScreenWindow* sw, bool second, const uint32_t* top, const uint32_t* bottom) {
    if (!sw->window) return;
    PlaceRects(sw, second);
    SDL_SetRenderDrawColor(sw->renderer, 0, 0, 0, 255);
    SDL_RenderClear(sw->renderer);
    if (sw->top && top) {
        SDL_UpdateTexture(sw->top, NULL, top, TOP_W * (int)sizeof(uint32_t));
        SDL_RenderCopy(sw->renderer, sw->top, NULL, &sw->topRect);
    }
    if (sw->bottom && bottom) {
        SDL_UpdateTexture(sw->bottom, NULL, bottom, BOTTOM_W * (int)sizeof(uint32_t));
        SDL_RenderCopy(sw->renderer, sw->bottom, NULL, &sw->bottomRect);
    }
    SDL_RenderPresent(sw->renderer);
}

void Screens_Present(const uint32_t* top, const uint32_t* bottom) {
    PresentWindow(&sMain, false, top, bottom);
    PresentWindow(&sSecond, true, top, bottom);
}
