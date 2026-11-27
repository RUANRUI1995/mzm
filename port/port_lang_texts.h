#pragma once
/*
 * The game side of the port languages (port_lang.h): the translated string
 * tables and the lookups src/ calls where it reads its own text tables.
 */

#include "port_lang.h"
#include "types.h"
#include "constants/text.h"
#include "location_text.h"

/* Translated strings of one language and region. NULL entries keep the
 * game's English text. */
typedef struct PortLangTexts {
    const u16* message[MESSAGE_COUNT];
    const u16* location[LT_COUNT];
    const u16* description[DESCRIPTION_TEXT_COUNT];
    const u16* story[STORY_TEXT_COUNT];
    const u16* fileScreen[FILE_SCREEN_TEXT_COUNT];
} PortLangTexts;

/* The string to draw for a game text: the translation when one is active
 * and has the entry, otherwise `original` (the game's own pointer, returned
 * unchanged). */
const u16* Port_Lang_Message(u32 id, const u16* original);
const u16* Port_Lang_Location(u32 id, const u16* original);
const u16* Port_Lang_Description(u32 id, const u16* original);
const u16* Port_Lang_Story(u32 id, const u16* original);
const u16* Port_Lang_FileScreen(u32 id, const u16* original);
