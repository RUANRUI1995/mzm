#pragma once
/*
 * Port languages (port_lang.h) for the text the game draws as graphics.
 *
 * Right after the game decompresses an English graphic that carries words
 * (the pause screen's item names, the file select menus...), the port
 * rearranges it into the translated words: it copies columns of letters
 * from the English graphic itself, fills with its background and draws the
 * few letters the English words lack. The patches are made by
 * tools/lang/gen_lang_gfx.py from port/lang/<lang>_gfx.py into
 * port/generated/lang_<lang>_gfx.c; they hold no ROM art.
 */

#include "types.h"
#include "port_lang.h"
#include "port_rom.h"

typedef enum PortLangGfxId {
    PORT_LANG_GFX_EQUIPMENT_NAMES,
    PORT_LANG_GFX_MENU_NAMES,

    PORT_LANG_GFX_COUNT
} PortLangGfxId;

/* A rectangle of tiles: `rows` runs of `width` tiles, each starting at
 * first[row] (tile index in the graphic). */
typedef struct PortLangGfxSlot {
    u16 rows;
    u16 width;
    u16 first[4];
} PortLangGfxSlot;

/* Ops, seven u16 each, the last one a mask of the pixel rows (0-15) the op
 * may change (the stream ends with PLG_END):
 *   PLG_COPY  dst slot, dst x, src slot, src x, count: copy columns of the
 *             English graphic
 *   PLG_FILL  dst slot, dst x, src slot, src x, count: repeat one column
 *   PLG_GLYPH dst slot, dst x, glyph offset, count, 0: columns of our own,
 *             rows * 8 pixels each, two per byte (low nibble first) */
enum {
    PLG_END = 0,
    PLG_COPY,
    PLG_FILL,
    PLG_GLYPH,
};

typedef struct PortLangGfxPatch {
    u32 size;        /* decompressed size of the English graphic */
    u32 regionMask;  /* 1 << PortRomRegion of the ROMs it was made for */
    const PortLangGfxSlot* slots;
    const u8* glyphs;
    const u16* ops;
} PortLangGfxPatch;

/* Patch the English graphic `id` the game just decompressed to `dst` (a GBA
 * address or host pointer), when a port language is active. */
void Port_LangGfx_Apply(PortLangGfxId id, void* dst);
