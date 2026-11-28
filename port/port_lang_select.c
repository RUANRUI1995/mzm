/*
 * Port languages (port_lang.h) in the EU language select (soft_reset.c).
 *
 * The screen lists the ROM's five languages, two tile rows each, on BG3
 * (screenmap at VRAM 0xF800, tiles from VRAM 0). The port adds
 * "Português" as a sixth entry under Español: its letters are composed into
 * free tiles by the PT-BR graphics patch (PORT_LANG_GFX_LANGUAGE_SELECT) and
 * this file writes them into the screenmap. The game picks it as language
 * index LANGUAGE_COUNT (see soft_reset.c).
 */

#include "port_lang_select.h"

#include "gba.h"
#include "port_gba_mem.h"
#include "port_lang_gfx.h"

/* Rows 15 and 16 of the screenmap, from column 12 like the other entries;
 * tiles 0x41-0x48 (top) and 0x49-0x50 (bottom), palette 15 = not selected. */
#define PT_ROW 15
#define PT_COLUMN 12
#define PT_TILES 8
#define PT_TOP_TILE 0x41
#define PT_BOTTOM_TILE 0x49

void Port_LangSelect_Setup(void)
{
    u16* map;
    u32 i;

    if (!Port_Lang_Available())
        return;
    Port_LangGfx_ApplyAlways(PORT_LANG_GFX_LANGUAGE_SELECT, (void*)VRAM_BASE);
    map = (u16*)port_resolve_write_addr((uintptr_t)(VRAM_BASE + 0xF800));
    for (i = 0; i < PT_TILES; i++) {
        map[PT_ROW * 32 + PT_COLUMN + i] = (u16)(0xF000 | (PT_TOP_TILE + i));
        map[(PT_ROW + 1) * 32 + PT_COLUMN + i] = (u16)(0xF000 | (PT_BOTTOM_TILE + i));
    }
}

u16 Port_LangSelect_RowOffset(void)
{
    /* Same layout as sLanguageSelectLanguageTileTableOffsets: row * 32 + 8. */
    return PT_ROW * 32 + 8;
}
