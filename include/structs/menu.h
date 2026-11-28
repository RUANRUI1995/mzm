#ifndef MENU_OAM_H
#define MENU_OAM_H

#include "types.h"

#define OAM_ID_CHANGED_FLAG (TRUE << 1)
#define CUTSCENE_OAM_ACTION_NONE 0

#if defined(MZM_3DS) || defined(PORT_NATIVE)
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
/* Big-endian GCC fills a bitfield unit from its most significant bit: the
 * fields of each byte are declared in reverse, so every byte holds the same
 * bits as on the GBA (ROM data, raw copies and mzm.sav read alike). */
struct MenuOamData {
    s16 yPosition;
    s16 xPosition;

    u8 unk_4;
    u8 unk_5;
    u8 unk_6;
    u8 unk_7;
    u8 animationDurationCounter;
    u8 currentAnimationFrame;
    u8 oamId;
    // B
    u8 exists:2;
    u8 notDrawn:1;
    u8 ended:1;
    u8 objMode:2;
    u8 priority:2;

    // C
    u8 :3;
    u8 rotationScaling:1;
    u8 boundBackground:4;
    s16 unk_E;
};
#else
struct MenuOamData {
    s16 yPosition;
    s16 xPosition;

    u8 unk_4;
    u8 unk_5;
    u8 unk_6;
    u8 unk_7;
    u8 animationDurationCounter;
    u8 currentAnimationFrame;
    u8 oamId;
    // B
    u16 priority:2;
    u16 objMode:2;
    u16 ended:1;
    u16 notDrawn:1;
    u16 exists:2;

    // C
    u8 boundBackground:4;
    u8 rotationScaling:1;
    s16 unk_E;
};
#endif
#else
struct MenuOamData {
    s16 yPosition;
    s16 xPosition;

    u8 unk_4;
    u8 unk_5;
    u8 unk_6;
    u8 unk_7;
    u8 animationDurationCounter;
    u8 currentAnimationFrame;
    u8 oamId;
    // B
    u16 priority:2;
    u16 objMode:2;
    u16 ended:1;
    u16 notDrawn:1;
    u16 exists:2;

    // C
    u8 boundBackground:4;
    u8 rotationScaling:1;
    s16 unk_E;
};
#endif

struct MaintainedInput {
    u8 delay;
    u8 set;
};

extern struct MaintainedInput gMaintainedInputData;
extern u16 gPrevChangedInput;

#endif /* MENU_OAM_H */
