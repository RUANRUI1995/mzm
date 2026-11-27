/* Port text languages; see port_lang.h. */

#include "port_lang_texts.h"
#include "port_rom.h"
#include "structs/game_state.h"

/* port/generated/lang_pt_br.c */
extern const PortLangTexts gPortLangPtBr_EU;

static PortTextLanguage sLanguage = PORT_TEXT_LANGUAGE_GAME;

PortTextLanguage Port_Lang_Get(void)
{
    return sLanguage;
}

void Port_Lang_Set(PortTextLanguage language)
{
    if ((int)language < 0 || language >= PORT_TEXT_LANGUAGE_COUNT)
        language = PORT_TEXT_LANGUAGE_GAME;
    sLanguage = language;
}

bool Port_Lang_Available(void)
{
    return gRomRegion == PORT_ROM_REGION_EU;
}

static const PortLangTexts* ActiveTexts(void)
{
    if (sLanguage != PORT_TEXT_LANGUAGE_PT_BR || gLanguage != LANGUAGE_ENGLISH)
        return NULL;
    switch (gRomRegion) {
        case PORT_ROM_REGION_EU: return &gPortLangPtBr_EU;
        default: return NULL;
    }
}

bool Port_Lang_Active(void)
{
    return ActiveTexts() != NULL;
}

#define PORT_LANG_LOOKUP(field, count)                       \
    const PortLangTexts* texts = ActiveTexts();              \
    if (texts != NULL && id < (count) && texts->field[id])   \
        return texts->field[id];                             \
    return original

const u16* Port_Lang_Message(u32 id, const u16* original)
{
    PORT_LANG_LOOKUP(message, MESSAGE_COUNT);
}

const u16* Port_Lang_Location(u32 id, const u16* original)
{
    PORT_LANG_LOOKUP(location, LT_COUNT);
}

const u16* Port_Lang_Description(u32 id, const u16* original)
{
    PORT_LANG_LOOKUP(description, DESCRIPTION_TEXT_COUNT);
}

const u16* Port_Lang_Story(u32 id, const u16* original)
{
    PORT_LANG_LOOKUP(story, STORY_TEXT_COUNT);
}

const u16* Port_Lang_FileScreen(u32 id, const u16* original)
{
    PORT_LANG_LOOKUP(fileScreen, FILE_SCREEN_TEXT_COUNT);
}
