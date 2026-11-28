#ifndef PORT_BOTTOM_UI_ZH_H
#define PORT_BOTTOM_UI_ZH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The bottom screen's text is written in English and translated at draw time:
 * every string the UI draws goes through Port_BottomUi_Zh, which returns the
 * Chinese for it when the catalogue has an entry and the string otherwise.
 * That keeps the layout code free of `? :` chains and leaves anything not yet
 * translated in English instead of blank. */
const char* Port_BottomUi_Zh(const char* en);

/* Whether to translate at all. Set once at start-up from the port config: a
 * Chinese ROM turns it on by itself, and the config can force it either way. */
bool Port_BottomUi_ZhActive(void);
void Port_BottomUi_ZhSetActive(bool on);

/* The 12x12 glyph for a code point, or NULL when the font has none. Only the
 * characters port_bottom_ui_zh.c uses are in it (tools/ui-font-zh/gen.py bakes
 * them), so a new Chinese string needs that tool run again. */
const uint8_t* Port_BottomUiZhGlyph(uint32_t codePoint);

enum {
    /* A Chinese glyph is twice a Latin cell: the 5x7 font advances 6 pixels,
     * this one 12, and both are drawn from the same top edge. */
    kZhGlyphW = 12,
    kZhGlyphH = 12,
    kZhAdvance = 12
};

#ifdef __cplusplus
}
#endif

#endif /* PORT_BOTTOM_UI_ZH_H */
