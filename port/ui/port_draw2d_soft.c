/* Software version of the port_draw2d.h primitives, for the platforms without
 * citro2d. A pixel is drawn when its centre falls inside the shape, the same
 * coverage rule a GPU rasterizer uses, and shapes are blended over what is
 * already there in call order. Depth is ignored. */
#ifndef __3DS__

#include "port_draw2d.h"

#include <math.h>
#include <stddef.h>

static uint32_t* sPixels;
static int sWidth, sHeight, sPitch;

void Draw2D_SoftBegin(uint32_t* pixels, int width, int height, int pitch, uint32_t clearColor) {
    int x, y;
    sPixels = pixels;
    sWidth = width;
    sHeight = height;
    sPitch = pitch;
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x) pixels[(size_t)y * pitch + x] = clearColor;
}

static inline void Blend(uint32_t* dst, uint32_t src) {
    const uint32_t a = src >> 24;
    uint32_t d, r, g, b;
    if (a == 255) {
        *dst = src;
        return;
    }
    if (a == 0) return;
    d = *dst;
    r = ((src & 0xFF) * a + (d & 0xFF) * (255 - a) + 127) / 255;
    g = (((src >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * (255 - a) + 127) / 255;
    b = (((src >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * (255 - a) + 127) / 255;
    *dst = r | (g << 8) | (b << 16) | (d & 0xFF000000u);
}

/* First pixel whose centre is at or after v. */
static inline int FirstPixel(float v) { return (int)ceilf(v - 0.5f); }

void Draw2D_Rect(float x, float y, float depth, float w, float h, uint32_t color) {
    int x0 = FirstPixel(x), x1 = FirstPixel(x + w);
    int y0 = FirstPixel(y), y1 = FirstPixel(y + h);
    int px, py;
    (void)depth;
    if (!sPixels || w <= 0.0f || h <= 0.0f) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sWidth) x1 = sWidth;
    if (y1 > sHeight) y1 = sHeight;
    for (py = y0; py < y1; ++py) {
        uint32_t* row = sPixels + (size_t)py * sPitch;
        for (px = x0; px < x1; ++px) Blend(&row[px], color);
    }
}

static inline float Edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static inline uint32_t Lerp3(uint32_t c0, uint32_t c1, uint32_t c2, float w0, float w1, float w2) {
    uint32_t out = 0;
    int shift;
    for (shift = 0; shift < 32; shift += 8) {
        float v = ((c0 >> shift) & 0xFF) * w0 + ((c1 >> shift) & 0xFF) * w1 + ((c2 >> shift) & 0xFF) * w2;
        int iv = (int)(v + 0.5f);
        if (iv < 0) iv = 0;
        if (iv > 255) iv = 255;
        out |= (uint32_t)iv << shift;
    }
    return out;
}

void Draw2D_Triangle(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1,
                     float x2, float y2, uint32_t c2, float depth) {
    const float area = Edge(x0, y0, x1, y1, x2, y2);
    const bool flat = (c0 == c1 && c1 == c2);
    float minX = x0, maxX = x0, minY = y0, maxY = y0;
    int px, py, ix0, ix1, iy0, iy1;
    (void)depth;
    if (!sPixels || area == 0.0f) return;
    if (x1 < minX) minX = x1;
    if (x2 < minX) minX = x2;
    if (x1 > maxX) maxX = x1;
    if (x2 > maxX) maxX = x2;
    if (y1 < minY) minY = y1;
    if (y2 < minY) minY = y2;
    if (y1 > maxY) maxY = y1;
    if (y2 > maxY) maxY = y2;
    ix0 = FirstPixel(minX);
    ix1 = FirstPixel(maxX);
    iy0 = FirstPixel(minY);
    iy1 = FirstPixel(maxY);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > sWidth) ix1 = sWidth;
    if (iy1 > sHeight) iy1 = sHeight;
    for (py = iy0; py < iy1; ++py) {
        const float cy = (float)py + 0.5f;
        for (px = ix0; px < ix1; ++px) {
            const float cx = (float)px + 0.5f;
            float w0 = Edge(x1, y1, x2, y2, cx, cy) / area;
            float w1 = Edge(x2, y2, x0, y0, cx, cy) / area;
            float w2 = Edge(x0, y0, x1, y1, cx, cy) / area;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            Blend(&sPixels[(size_t)py * sPitch + px], flat ? c0 : Lerp3(c0, c1, c2, w0, w1, w2));
        }
    }
}

void Draw2D_Circle(float cx, float cy, float depth, float radius, uint32_t color) {
    const float r2 = radius * radius;
    int px, py;
    int ix0 = FirstPixel(cx - radius), ix1 = FirstPixel(cx + radius);
    int iy0 = FirstPixel(cy - radius), iy1 = FirstPixel(cy + radius);
    (void)depth;
    if (!sPixels || radius <= 0.0f) return;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > sWidth) ix1 = sWidth;
    if (iy1 > sHeight) iy1 = sHeight;
    for (py = iy0; py < iy1; ++py) {
        const float dy = (float)py + 0.5f - cy;
        for (px = ix0; px < ix1; ++px) {
            const float dx = (float)px + 0.5f - cx;
            if (dx * dx + dy * dy <= r2) Blend(&sPixels[(size_t)py * sPitch + px], color);
        }
    }
}

#endif /* !__3DS__ */
