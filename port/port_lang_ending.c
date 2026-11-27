/*
 * Port languages (port_lang.h) for the ending's statistics: CLEAR TIME and
 * YOUR RATE FOR COLLECTING ITEMS IS are spelt with one sprite per letter,
 * each pointing at a letter of the alphabet in the ending's text graphic
 * (sEndingImageText*Gfx, OBJ tiles 0x100 and up). For a port language the
 * port spells its own words with the same alphabet and hands the game
 * those sprites instead (ending_and_gallery.c, EndingImageLoadTextOam).
 */

#include "port_lang_ending.h"

#include <string.h>

#include "oam.h"

/* OBJ tile of each letter of the alphabet (16x16 sprites; I is 8x16). The
 * alphabet has no K, Q or W. */
static u16 LetterTile(char c)
{
    static const char row1[] = "ABCDEFGHIJLMNOPR";
    static const char row2[] = "STVUXYdZ";  /* d: the D' of the alphabet */
    const char* p;

    if ((p = strchr(row1, c)) != NULL)
        return (u16)(0x100 + (p - row1) * 2);
    if ((p = strchr(row2, c)) != NULL)
        return (u16)(0x140 + (p - row2) * 2);
    return 0;
}

/* Horizontal advance of a letter, as the English words space them. */
static s32 LetterAdvance(char c)
{
    if (c == 'I')
        return 6;
    if (c == 'M')
        return 10;
    if (c == ' ')
        return 9;
    return 9;
}

#define MAX_LETTERS 32

typedef struct LineSet {
    const char* text;
    u16 y;
    u16 frames[MAX_LETTERS][OAM_DATA_SIZE(1)];
    PortEndingLetter letters[MAX_LETTERS];
    u32 count;
} LineSet;

/* PT-BR: the time on one line, then the items line (no YOUR RATE FOR line,
 * as in the game's Spanish); the same words as the full lines that replace
 * the letters once they have dropped in (port/lang/pt_br_gfx.py). */
static LineSet sPtBrClearTime = { "TEMPO FINAL", 40 };
static LineSet sPtBrCollecting = { "ITENS COLETADOS", 108 };

static void BuildLine(LineSet* set)
{
    s32 width = 0;
    s32 x;
    const char* c;

    if (set->count != 0)
        return;
    for (c = set->text; *c; c++)
        width += LetterAdvance(*c);
    /* centred on the sprites' anchor, x = 120 */
    x = -width / 2;
    for (c = set->text; *c && set->count < MAX_LETTERS; c++) {
        if (*c != ' ' && LetterTile(*c) != 0) {
            u16* f = set->frames[set->count];
            const u16 entry[3] = { OAM_ENTRY(x, -16, OAM_DIMS_16x16, OAM_NO_FLIP, 0, 0, 0) };
            const u16 thin[3] = { OAM_ENTRY(x, -16, OAM_DIMS_8x16, OAM_NO_FLIP, 0, 0, 0) };
            const u16* e = (*c == 'I') ? thin : entry;
            f[0] = 1;
            f[1] = e[0];
            f[2] = e[1];
            f[3] = (u16)(e[2] | LetterTile(*c));
            set->letters[set->count].y = set->y;
            set->letters[set->count].frame = f;
            set->count++;
        }
        x += LetterAdvance(*c);
    }
}

int Port_LangEnding_TextOam(u32 set, const PortEndingLetter** letters, u32* count)
{
    LineSet* line;

    if (!Port_Lang_Active())
        return 0;
    switch (set) {
        case 0: /* ENDING_IMAGE_OAM_SET_CLEAR_TIME */
            line = &sPtBrClearTime;
            break;
        case 2: /* ENDING_IMAGE_OAM_SET_COLLECTING */
            line = &sPtBrCollecting;
            break;
        default: /* YOUR RATE FOR: nothing, as in Spanish */
            *letters = NULL;
            *count = 0;
            return 1;
    }
    BuildLine(line);
    *letters = line->letters;
    *count = line->count;
    return 1;
}

/* MISSAO / CUMPRIDA! over the MISSION ACCOMPLISHED alphabet
 * (sChozodiaEscapeMissionAccomplishedLettersGfx), spaced like the English
 * frame: 16x16 letters (I and ! are 8x16), and the tilde the PT-BR patch
 * draws into tile 0x1E as an 8x8 sprite over the A. */
#define MA_A 0x00
#define MA_C 0x02
#define MA_D 0x04
#define MA_I 0x0A
#define MA_EXCL 0x0B
#define MA_M 0x0F
#define MA_O 0x13
#define MA_P 0x15
#define MA_R 0x17
#define MA_S 0x40
#define MA_U 0x44
#define MA_TILDE 0x1E

static const u16 sMissionAccomplishedPtBr[OAM_DATA_SIZE(16)] = {
    16,
    /* MISSAO */
    OAM_ENTRY(-35, -24, OAM_DIMS_16x16, OAM_NO_FLIP, MA_M, 0, 0),
    OAM_ENTRY(-22, -24, OAM_DIMS_8x16, OAM_NO_FLIP, MA_I, 0, 0),
    OAM_ENTRY(-13, -24, OAM_DIMS_16x16, OAM_NO_FLIP, MA_S, 0, 0),
    OAM_ENTRY(0, -24, OAM_DIMS_16x16, OAM_NO_FLIP, MA_S, 0, 0),
    OAM_ENTRY(12, -24, OAM_DIMS_16x16, OAM_NO_FLIP, MA_A, 0, 0),
    OAM_ENTRY(13, -24, OAM_DIMS_8x8, OAM_NO_FLIP, MA_TILDE, 0, 0),
    OAM_ENTRY(24, -24, OAM_DIMS_16x16, OAM_NO_FLIP, MA_O, 0, 0),
    /* CUMPRIDA! */
    OAM_ENTRY(-49, 0, OAM_DIMS_16x16, OAM_NO_FLIP, MA_C, 0, 0),
    OAM_ENTRY(-37, 0, OAM_DIMS_16x16, OAM_NO_FLIP, MA_U, 0, 0),
    OAM_ENTRY(-25, 0, OAM_DIMS_16x16, OAM_NO_FLIP, MA_M, 0, 0),
    OAM_ENTRY(-12, 0, OAM_DIMS_16x16, OAM_NO_FLIP, MA_P, 0, 0),
    OAM_ENTRY(0, 0, OAM_DIMS_16x16, OAM_NO_FLIP, MA_R, 0, 0),
    OAM_ENTRY(12, 0, OAM_DIMS_8x16, OAM_NO_FLIP, MA_I, 0, 0),
    OAM_ENTRY(21, 0, OAM_DIMS_16x16, OAM_NO_FLIP, MA_D, 0, 0),
    OAM_ENTRY(32, 0, OAM_DIMS_16x16, OAM_NO_FLIP, MA_A, 0, 0),
    OAM_ENTRY(45, 0, OAM_DIMS_8x16, OAM_NO_FLIP, MA_EXCL, 0, 0),
};

const u16* Port_LangMissionAccomplished_Frame(void)
{
    return Port_Lang_Active() ? sMissionAccomplishedPtBr : NULL;
}

/* NOVO / RECORDE: the PT-BR patch lays the sign out over 80 pixels (tiles
 * 0x16-0x1F of the numbers graphic) instead of the English 56, so this frame
 * covers them: 16 pixel rows of 32+32+16, then 8 rows of the same widths. */
static const u16 sNewRecordPtBr[OAM_DATA_SIZE(6)] = {
    6,
    OAM_ENTRY(-38, -16, OAM_DIMS_32x16, OAM_NO_FLIP, 0x16, 5, 0),
    OAM_ENTRY(-6, -16, OAM_DIMS_32x16, OAM_NO_FLIP, 0x1A, 5, 0),
    OAM_ENTRY(26, -16, OAM_DIMS_16x16, OAM_NO_FLIP, 0x1E, 5, 0),
    OAM_ENTRY(-38, 0, OAM_DIMS_32x8, OAM_NO_FLIP, 0x56, 5, 0),
    OAM_ENTRY(-6, 0, OAM_DIMS_32x8, OAM_NO_FLIP, 0x5A, 5, 0),
    OAM_ENTRY(26, 0, OAM_DIMS_16x8, OAM_NO_FLIP, 0x5E, 5, 0),
};

const u16* Port_LangNewRecord_Frame(void)
{
    return Port_Lang_Active() ? sNewRecordPtBr : NULL;
}
