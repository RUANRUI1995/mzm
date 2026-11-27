#pragma once
/*
 * Text languages the port adds to the game (issue #40: Brazilian Portuguese).
 *
 * The game only ships the languages of its ROM. A port language is drawn on
 * top of English: the game keeps running in English (gLanguage stays
 * LANGUAGE_ENGLISH, the ROM is untouched), and wherever it picks a text out of
 * its tables the port hands it the translated string instead. That keeps the
 * ROM hash and the GBA RAM exactly those of an English game, which is what
 * RetroAchievements identify the game by and read.
 *
 * The strings are compiled into the binary (port/generated/lang_<lang>.c,
 * built by tools/lang/gen_lang.py from port/lang/<lang>.txt), one copy per
 * ROM region because the US and EU fonts have different widths.
 *
 * This header only uses standard types, so the bottom screen UI and the
 * platform code (which see libctru's u32, not the game's) can include it.
 * The game's lookups are in port_lang_texts.h.
 */

#include <stdbool.h>

typedef enum PortTextLanguage {
    /* The ROM's own text in gLanguage. */
    PORT_TEXT_LANGUAGE_GAME = 0,
    /* Brazilian Portuguese over English. */
    PORT_TEXT_LANGUAGE_PT_BR,

    PORT_TEXT_LANGUAGE_COUNT
} PortTextLanguage;

/* Port language picked in the options. It is a port setting, saved in the
 * port's config file, never in the game's save or GBA RAM. */
PortTextLanguage Port_Lang_Get(void);
void Port_Lang_Set(PortTextLanguage language);

/* Whether a port language is being drawn right now: one is picked, the ROM
 * is one the translation has strings for (US or EU) and the game runs in
 * English. */
bool Port_Lang_Active(void);

/* Whether the loaded ROM can show port languages at all (the options only
 * offer them then). */
bool Port_Lang_Available(void);
