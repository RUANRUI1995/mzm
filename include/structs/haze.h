#ifndef HAZE_STRUCT_H
#define HAZE_STRUCT_H

#include "types.h"

#include "constants/haze.h"

typedef u32 (*HazeFunc_T)(void);

#if defined(MZM_3DS) || defined(PORT_NATIVE)
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
/* Big-endian GCC fills a bitfield unit from its most significant bit: the
 * fields of each byte are declared in reverse, so every byte holds the same
 * bits as on the GBA (ROM data, raw copies and mzm.sav read alike). */
struct Haze {
    void* pAffected;
    u16 unk_4;
    u8 size;
    u8 active:1;
    u8 enabled:7;
};
#else
struct Haze {
    void* pAffected;
    u16 unk_4;
    u8 size;
    u8 enabled:7;
    u8 active:1;
};
#endif
#else
struct Haze {
    void* pAffected;
    u16 unk_4;
    u8 size;
    u8 enabled:7;
    u8 active:1;
};
#endif

struct HazeLoop {
    s16 timer;
    u8 unk_2;
    u8 unk_3;
};

extern struct Haze gHazeInfo;
extern struct HazeLoop gHazeLoops[3];

extern u8 gUnk_3005728;
extern u8 gUnk_3005729;

extern u8 gUnk_3005514;

extern HazeFunc_T gHazeProcessCodePointer;

extern HazeValue gCurrentHazeValue;

#ifdef USE_EWRAM_SYMBOLS
extern u16 gHazeValues[1280];
extern u16 gPreviousHazeValues[1280];
#else
#define gHazeValues CAST_TO_ARRAY(u16, [1280], EWRAM_BASE + 0x26300)
#define gPreviousHazeValues CAST_TO_ARRAY(u16, [1280], EWRAM_BASE + 0x26D00)
#endif /* USE_EWRAM_SYMBOLS */

#endif /* HAZE_STRUCT_H */
