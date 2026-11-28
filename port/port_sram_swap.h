#ifndef PORT_SRAM_SWAP_H
#define PORT_SRAM_SWAP_H

/* mzm.sav is the GBA's save file byte for byte (issue #37). In memory the save
 * (gSramMem, struct Sram) is in host order, as the game's C code writes and
 * reads it; a big-endian host swaps its scalars when the file is read or
 * written (port_sram_swap.c, docs/wiiu-big-endian-plan.md, part F). On a
 * little-endian host, and in builds without PORT_ROM_SWAP (the 3DS), these
 * do nothing. */

#include <stdint.h>

typedef struct {
    uint16_t offset; /* from the start of struct Sram */
    uint8_t width;   /* 2 or 4 */
    uint16_t count;  /* scalars back to back */
} PortSramRun;

/* port/generated/byteswap/sram_layout.c (tools/gen_sram_byteswap.py) */
extern const PortSramRun gPortSramRuns[];
extern const unsigned gPortSramRunCount;

#ifdef PORT_ROM_SWAP
/* The file's bytes (GBA order) to host order, in place. */
void Port_SramFileToHost(uint8_t* sram, uint32_t size);
/* Host order to the file's bytes, in place. */
void Port_SramHostToFile(uint8_t* sram, uint32_t size);
#else
static inline void Port_SramFileToHost(uint8_t* sram, uint32_t size) { (void)sram; (void)size; }
static inline void Port_SramHostToFile(uint8_t* sram, uint32_t size) { (void)sram; (void)size; }
#endif

#endif /* PORT_SRAM_SWAP_H */
