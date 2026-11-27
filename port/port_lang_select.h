#pragma once
/* Port languages in the EU language select; see port_lang_select.c. */

#include "types.h"
#include "port_lang.h"

/* After the game loads the language select's graphics and screenmap: add
 * the port languages' entries (only with a ROM that offers them). */
void Port_LangSelect_Setup(void);

/* Screenmap offset of the Português entry's highlight row, like the game's
 * sLanguageSelectLanguageTileTableOffsets. */
u16 Port_LangSelect_RowOffset(void);
