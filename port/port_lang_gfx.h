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
    PORT_LANG_GFX_UNKNOWN_ITEMS,
    PORT_LANG_GFX_FILE_SELECT_LARGE,
    PORT_LANG_GFX_FILE_SELECT_DIFFICULTY,
    PORT_LANG_GFX_FILE_SELECT_OPTIONS,

    PORT_LANG_GFX_COUNT
} PortLangGfxId;

/* A rectangle of tiles: `rows` runs of `width` tiles, each starting at
 * first[row] (tile index in the graphic). */
typedef struct PortLangGfxSlot {
    u16 rows;
    u16 width;
    u16 first[4];
} PortLangGfxSlot;

/* Ops, eight u16 each (the stream ends with PLG_END):
 *   PLG_COPY  dst slot, dst x, src slot, src x, count, row mask, map: copy
 *             columns of the English graphic
 *   PLG_FILL  dst slot, dst x, src slot, src x, count, row mask, map: repeat
 *             one column
 *   PLG_GLYPH dst slot, dst x, glyph offset, count, 0, row mask, -: columns of
 *             our own, rows * 8 pixels each, two per byte (low nibble first)
 *   PLG_PIXEL dst slot, x, y, palette index, 0, -, -: one pixel (accents)
 * The row mask says which pixel rows (0-15) the op may change; map is 0xFFFF
 * or the colour map (per pixel row, old index -> new) the copied pixels go
 * through, for letters taken from a word drawn in other colours. */
enum {
    PLG_END = 0,
    PLG_COPY,
    PLG_FILL,
    PLG_GLYPH,
    PLG_PIXEL,
};

typedef struct PortLangGfxPatch {
    u32 size;        /* decompressed size of the English graphic */
    const PortLangGfxSlot* slots;
    const u8* glyphs;
    const u8 (*maps)[16][16];
    const u16* ops;
} PortLangGfxPatch;

/* Patch the English graphic `id` the game just decompressed to `dst` (a GBA
 * address or host pointer), when a port language is active. */
void Port_LangGfx_Apply(PortLangGfxId id, void* dst);
