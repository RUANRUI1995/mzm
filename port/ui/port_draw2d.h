#ifndef PORT_DRAW2D_H
#define PORT_DRAW2D_H

/* The 2D primitives the bottom-screen UI (port_bottom_ui.c) draws with:
 * solid rectangles, triangles and circles, in screen pixels with a depth.
 *
 * On the 3DS they are citro2d's own calls, inlined, so the 3DS draws exactly
 * what it drew before this layer existed. Elsewhere (PC, Wii U) they draw in
 * software into a 32-bit buffer (port_draw2d_soft.c), in call order, with
 * alpha blending.
 *
 * Colours are citro2d's: R in the low byte, then G, B, A (Draw2D_Color32). */

#include <stdbool.h>
#include <stdint.h>

#ifdef __3DS__

#include <citro2d.h>

#define Draw2D_Color32(r, g, b, a) C2D_Color32((r), (g), (b), (a))

static inline void Draw2D_Rect(float x, float y, float depth, float w, float h, uint32_t color) {
    C2D_DrawRectSolid(x, y, depth, w, h, color);
}

static inline void Draw2D_Triangle(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1,
                                   float x2, float y2, uint32_t c2, float depth) {
    C2D_DrawTriangle(x0, y0, c0, x1, y1, c1, x2, y2, c2, depth);
}

/* (cx, cy) is the centre. */
static inline void Draw2D_Circle(float cx, float cy, float depth, float radius, uint32_t color) {
    C2D_DrawCircleSolid(cx, cy, depth, radius, color);
}

#else

#define Draw2D_Color32(r, g, b, a) \
    ((uint32_t)(uint8_t)(r) | ((uint32_t)(uint8_t)(g) << 8) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(a) << 24))

void Draw2D_Rect(float x, float y, float depth, float w, float h, uint32_t color);
void Draw2D_Triangle(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1,
                     float x2, float y2, uint32_t c2, float depth);
/* (cx, cy) is the centre. */
void Draw2D_Circle(float cx, float cy, float depth, float radius, uint32_t color);

/* Software target: the primitives above draw into `pixels` (Draw2D_Color32
 * layout, `pitch` pixels per row) until the next call. Clears it to
 * `clearColor` first. */
void Draw2D_SoftBegin(uint32_t* pixels, int width, int height, int pitch, uint32_t clearColor);

#endif

#endif /* PORT_DRAW2D_H */
