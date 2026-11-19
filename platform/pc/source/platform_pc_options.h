#ifndef PLATFORM_PC_OPTIONS_H
#define PLATFORM_PC_OPTIONS_H

#include <stdbool.h>
#include <stdint.h>

#define PC_MAX_SHOTS 32

/* Command-line options shared by main_pc.c and platform_pc.c. */
typedef struct {
    uint32_t maxFrames;   /* 0 = run until quit */
    bool pulseA;          /* --test without a replay: tap A to get through menus */
    bool quiet;           /* no periodic progress lines */
    int scale;            /* window scale */
    int shotCount;
    struct {
        uint32_t frame;
        const char* path;
    } shots[PC_MAX_SHOTS];
    int dumpCount;
    struct {
        uint32_t frame;
        const char* dir;
    } dumps[PC_MAX_SHOTS];
} PcOptions;

extern PcOptions gPcOptions;

uint32_t Platform_PC_CurrentFrame(void);

#endif /* PLATFORM_PC_OPTIONS_H */
