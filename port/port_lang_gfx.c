/* Port language patches for graphics with text; see port_lang_gfx.h. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "port_lang_gfx.h"
#include "port_gba_mem.h"

/* port/generated/lang_pt_br_gfx.c: one table per ROM region (the English
 * graphics are not always the same); an entry without ops is not patched */
extern const PortLangGfxPatch gPortLangGfxPtBr_EU[PORT_LANG_GFX_COUNT];
extern const PortLangGfxPatch gPortLangGfxPtBr_US[PORT_LANG_GFX_COUNT];

/* VRAM holds GBA halfwords in host order: on a big-endian host the two
 * bytes of each halfword are swapped. */
static u32 sByteSwap;

static u8 GetPixel(const u8* buf, u32 tile, u32 x, u32 y)
{
    u8 b = buf[(tile * 32 + y * 4 + x / 2) ^ sByteSwap];
    return (x & 1) ? (b >> 4) : (b & 15);
}

static void SetPixel(u8* buf, u32 tile, u32 x, u32 y, u8 v)
{
    u8* p = &buf[(tile * 32 + y * 4 + x / 2) ^ sByteSwap];
    *p = (x & 1) ? (u8)((*p & 0x0F) | (v << 4)) : (u8)((*p & 0xF0) | v);
}

/* Column x of a slot: row r, pixel y of that row. */
static u8 GetColumnPixel(const u8* buf, const PortLangGfxSlot* s, u32 x, u32 r, u32 y)
{
    return GetPixel(buf, s->first[r] + x / 8, x % 8, y);
}

static u32 sRowMask;

/* Pixel row r * 8 + y of a slot is changed only if the op's row mask has
 * its bit (rows past the 16th always are). */
static void SetColumnPixel(u8* buf, const PortLangGfxSlot* s, u32 x, u32 r, u32 y, u8 v)
{
    u32 row = r * 8 + y;
    if (row < 16 && !(sRowMask & (1u << row)))
        return;
    SetPixel(buf, s->first[r] + x / 8, x % 8, y, v);
}

void Port_LangGfx_Apply(PortLangGfxId id, void* dstAddr)
{
    const PortLangGfxPatch* patch;
    const u16* op;
    u8* dst;
    u8* snap;

    if (!Port_Lang_Active() || (u32)id >= PORT_LANG_GFX_COUNT)
        return;
    if (gRomRegion == PORT_ROM_REGION_EU)
        patch = &gPortLangGfxPtBr_EU[id];
    else if (gRomRegion == PORT_ROM_REGION_US)
        patch = &gPortLangGfxPtBr_US[id];
    else
        return;
    if (patch->ops == NULL)
        return;

    dst = (u8*)port_resolve_write_addr((uintptr_t)dstAddr);
    snap = (u8*)malloc(patch->size);
    if (dst == NULL || snap == NULL) {
        free(snap);
        return;
    }
    memcpy(snap, dst, patch->size);
    sByteSwap = Port_HostIsBigEndian() ? 1 : 0;

    for (op = patch->ops; op[0] != PLG_END; op += 8) {
        const PortLangGfxSlot* d = &patch->slots[op[1]];
        u32 dx = op[2];
        u32 count = op[4 + (op[0] == PLG_GLYPH ? 0 : 1)];
        u32 i, r, y;

        sRowMask = op[6];

        if (op[0] == PLG_PIXEL) {
            /* op[2]: x, op[3]: y in the slot, op[4]: palette index */
            sRowMask = 0xFFFF;
            SetColumnPixel(dst, d, op[2], op[3] / 8, op[3] % 8, (u8)op[4]);
        } else if (op[0] == PLG_GLYPH) {
            /* op[3]: byte offset in glyphs, op[4]: columns */
            const u8* g = &patch->glyphs[op[3]];
            for (i = 0; i < count; i++) {
                for (r = 0; r < d->rows; r++) {
                    for (y = 0; y < 8; y++) {
                        u32 n = r * 8 + y;
                        u8 v = (n & 1) ? (g[n / 2] >> 4) : (g[n / 2] & 15);
                        SetColumnPixel(dst, d, dx + i, r, y, v);
                    }
                }
                g += d->rows * 4;
            }
        } else {
            const PortLangGfxSlot* s = &patch->slots[op[3]];
            const u8 (*map)[16] = (op[7] != 0xFFFF && patch->maps != NULL) ? patch->maps[op[7]] : NULL;
            u32 sx = op[4];
            for (i = 0; i < count; i++) {
                u32 from = (op[0] == PLG_COPY) ? sx + i : sx;
                for (r = 0; r < d->rows; r++) {
                    for (y = 0; y < 8; y++) {
                        u8 v = GetColumnPixel(snap, s, from, r, y);
                        u32 row = r * 8 + y;
                        if (map != NULL)
                            v = map[row < 16 ? row : 15][v];
                        SetColumnPixel(dst, d, dx + i, r, y, v);
                    }
                }
            }
        }
    }
    free(snap);
}
