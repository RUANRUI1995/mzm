#ifndef DEMO_STRUCT_H
#define DEMO_STRUCT_H

#include "types.h"

#include "constants/demo.h"

#if defined(MZM_3DS) || defined(PORT_NATIVE)
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
/* Big-endian GCC fills a bitfield unit from its most significant bit: the
 * fields of each byte are declared in reverse, so every byte holds the same
 * bits as on the GBA (ROM data, raw copies and mzm.sav read alike). */
struct Demo {
    u8 number;
    boolu8 active;
    boolu8 loading:4;
    boolu8 hasBeenInit:4;
    boolu8 noDemoShuffle:4;
    boolu8 endedWithInput:4;
} __attribute__((aligned(2)));
#else
struct Demo {
    u8 number;
    boolu8 active;
    boolu16 hasBeenInit:4;
    boolu16 loading:4;
    boolu16 endedWithInput:4;
    boolu16 noDemoShuffle:4;
};
#endif
#else
struct Demo {
    u8 number;
    boolu8 active;
    boolu16 hasBeenInit:4;
    boolu16 loading:4;
    boolu16 endedWithInput:4;
    boolu16 noDemoShuffle:4;
};
#endif

struct DemoEntry {
    const u16* const pInputs;
    u16 inputsSize;
    const u16* const pDuration;
    u16 durationSize;
};

extern DemoState gDemoState;
extern struct Demo gCurrentDemo;

#define DEMO_MAX_DURATION 256

extern u16 gDemoInputData[DEMO_MAX_DURATION];
extern u16 gDemoInputDuration[DEMO_MAX_DURATION];
extern u16 gDemoInputNumber;
extern u16 gDemoInputTimer;
extern u16 gDemoInput;

#endif /* DEMO_STRUCT_H */
