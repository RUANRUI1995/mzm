#pragma once
/* Port languages for the ending's lettered statistics; see port_lang_ending.c. */

#include "types.h"
#include "port_lang.h"

typedef struct PortEndingLetter {
    u16 y;              /* the sprite's y; x is 120 as in the game's tables */
    const u16* frame;   /* one-entry OAM frame: the letter and its x offset */
} PortEndingLetter;

/* For ending text set `set` (EndingImageTextSet): when a port language is
 * active, returns 1 with its letters (count may be 0: the set shows
 * nothing); otherwise 0 and the game uses its own tables. */
int Port_LangEnding_TextOam(u32 set, const PortEndingLetter** letters, u32* count);

/* The MISSION ACCOMPLISHED frame (chozodia_escape.c) of the active port
 * language, or NULL to keep the game's. */
const u16* Port_LangMissionAccomplished_Frame(void);
