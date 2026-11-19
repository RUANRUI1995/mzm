#pragma once
#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PORT_HOST_BIG_ENDIAN 1
#else
#define PORT_HOST_BIG_ENDIAN 0
#endif

extern u8 gIoMem[0x400];       // I/O Memory (0x04000000-0x040003FF)
extern u8 gEwram[0x40000];     // EWRAM (0x02000000-0x0203FFFF)
extern u8 gIwram[0x8000];      // IWRAM (0x03000000-0x03007FFF)
extern u16 gPaletteRam[512];   // BG + OBJ palettes (0x05000000-0x050003FF)
extern u16 gBgPltt[256];       // 0x200 bytes, gPaletteRam[0..255] (linker symbol)
extern u16 gObjPltt[256];      // 0x200 bytes, gPaletteRam[256..511] (linker symbol)
extern u16 gOamMem[0x400 / 2]; // 0x400 bytes (OAM)
extern u8 gVram[0x18000];      // 96 KB VRAM GBA (0x06000000-0x06017FFF)
extern u8 gSramMem[0x10000];    // 64 KB SRAM (0x0E000000-0x0E00FFFF)

// ROM data (loaded from baserom.gba)
extern u8* gRomData;
extern u8* gRomDataRaw; /* port_rom.h */
extern u32 gRomSize;

/* The original bytes behind a host pointer into the loaded ROM. On a
 * big-endian host typed reads go to a copy with its typed data byte-swapped
 * (port_rom_swap.c); byte and little-endian reads of ROM data must use the
 * original. Pointers elsewhere are returned unchanged; on a little-endian host
 * this is the identity. */
static inline const u8* port_rom_raw(const void* p) {
    const u8* b = (const u8*)p;
    if (gRomDataRaw != gRomData && gRomData != NULL && b >= gRomData && b < gRomData + gRomSize)
        return gRomDataRaw + (b - gRomData);
    return b;
}

/* Little-endian reads that are right for ROM data on any host. */
static inline u32 port_read32le(const void* p) {
    const u8* b = port_rom_raw(p);
    return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}

static inline u16 port_read16le(const void* p) {
    const u8* b = port_rom_raw(p);
    return (u16)(b[0] | (b[1] << 8));
}

// ROM access logging (defined in port_rom.c)
void Port_LogRomAccess(u32 gba_addr, const char* caller);

void gba_write8(uint32_t addr, uint8_t v);
u8 gba_read8(uint32_t addr);
void gba_write16(uint32_t addr, uint16_t v);
u16 gba_read16(uint32_t addr);
void gba_write32(uint32_t addr, uint32_t v);
u32 gba_read32(uint32_t addr);

/*
 * gba_TryMemPtr — non-aborting address resolver.
 * Returns native pointer for known GBA ranges, NULL otherwise.
 */
static inline void* gba_TryMemPtr(uint32_t addr) {
    if (addr >= 0x02000000u && addr < 0x02040000u)
        return &gEwram[addr - 0x02000000u];
    if (addr >= 0x03000000u && addr < 0x03008000u)
        return &gIwram[addr - 0x03000000u];
    if (addr >= 0x04000000u && addr < 0x04000400u)
        return &gIoMem[addr - 0x04000000u];
    if (addr >= 0x05000000u && addr < 0x05000400u)
        return &gPaletteRam[(addr - 0x05000000u) >> 1];
    if (addr >= 0x06000000u && addr < 0x06018000u)
        return &gVram[addr - 0x06000000u];
    if (addr >= 0x07000000u && addr < 0x07000400u)
        return &gOamMem[(addr - 0x07000000u) >> 1];
    if (addr >= 0x0E000000u && addr < 0x0E010000u)
        return &gSramMem[addr - 0x0E000000u];
    if (gRomData && addr >= 0x08000000u && addr < 0x08000000u + gRomSize) {
#ifndef MZM_3DS
        Port_LogRomAccess(addr, "gba_TryMemPtr");
#endif
        return &gRomData[addr - 0x08000000u];
    }
    return NULL;
}

static inline void* gba_MemPtr(uint32_t addr) {
    void* ptr = gba_TryMemPtr(addr);
    if (ptr)
        return ptr;
#if defined(__GNUC__)
    void* caller = __builtin_return_address(0);
    fprintf(stderr, "FATAL: gba_MemPtr: invalid address 0x%08lX (called from %p)\n", (unsigned long)addr, caller);
#elif defined(_MSC_VER)
    void* caller = _ReturnAddress();
    fprintf(stderr, "FATAL: gba_MemPtr: invalid address 0x%08lX (called from %p)\n", (unsigned long)addr, caller);
#else
    fprintf(stderr, "FATAL: gba_MemPtr: invalid address 0x%08lX\n", (unsigned long)addr);
#endif
    fflush(stderr);
#ifdef MZM_3DS
    /* stderr isn't visible on real hardware -- mirror the fatal message to
     * the file log (see port_debug_log.h) so it survives past the "Fatal
     * error" screen the 3DS shows on abort(). Always on regardless of
     * PORT_VERBOSE_FRAME_LOG since this fires at most once. */
    {
        extern void Port_DebugLog(const char* msg);
        char msg[128];
        __builtin_snprintf(msg, sizeof(msg), "FATAL: gba_MemPtr: invalid address 0x%08lX (called from %p)", (unsigned long)addr, __builtin_return_address(0));
        Port_DebugLog(msg);
    }
#endif
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    __asm__ volatile("int3");
#endif
    abort();
    return NULL;
}

/* Resolve a GBA address or an already-native pointer for reads. */
#ifdef __cplusplus
extern "C" {
#endif
void* port_resolve_addr(uintptr_t val);
/* Resolve a write destination. GBA ROM addresses are never writable, so host
 * heap and stack pointers in the 0x08xxxxxx range remain native. */
void* port_resolve_write_addr(uintptr_t val);
/* Resolve a copy source while preserving known ROM-buffer and active-stack
 * pointers before interpreting numeric GBA addresses. */
const void* port_resolve_copy_src(const void* src, u32 size);

/* Copies and fills that move GBA units (DMA, CpuSet). The port's memories
 * keep two byte orders on a big-endian host (docs/wiiu-big-endian-plan.md):
 *   native     EWRAM, IWRAM, host objects, the typed ROM copy
 *   halfwords  VRAM, palette RAM and OAM: arrays of host-order u16 (what the
 *              game's typed stores use; the PPU reads VRAM byte a at a ^ 1);
 *              a 32-bit unit is its low half at the lower address
 *   GBA        I/O registers, the original ROM bytes: little-endian
 * A unit keeps its value across orders, as on the hardware. On a
 * little-endian host all three are the same and these are memmove/stores.
 * dst and src are unresolved (GBA addresses or host pointers); unit is the
 * transfer width in bytes (2 or 4). */
void Port_GbaCopy(void* dst, const void* src, u32 len, u32 unit);
void Port_GbaFill(void* dst, u32 value, u32 len, u32 unit);
/* The value of the unit at src as the GBA sees it (CpuSet's fill source). */
u32 Port_GbaReadUnit(const void* src, u32 unit);
/* The u32 that, stored natively into halfword memory (VRAM, palette, OAM,
 * EWRAM u16 arrays), puts lo at the lower address and hi above it, as
 * `lo | hi << 16` does on the GBA. */
#if PORT_HOST_BIG_ENDIAN
#define PORT_HALVES32(lo, hi) (((u32)(u16)(lo) << 16) | (u16)(hi))
#else
#define PORT_HALVES32(lo, hi) ((u32)(u16)(lo) | ((u32)(u16)(hi) << 16))
#endif

/* Tile data in VRAM and EWRAM is stored as GBA halfwords by value. Code that
 * works on it as u32 words (4bpp rows, src/text.c) sees each word's halves
 * swapped on a big-endian host; this swaps them (its own inverse, and it
 * commutes with |= and &=). A no-op on little-endian hosts. */
static inline void Port_SwapWordHalves(u32* words, u32 count) {
#if PORT_HOST_BIG_ENDIAN
    u32 i;
    for (i = 0; i < count; i++)
        words[i] = (words[i] << 16) | (words[i] >> 16);
#else
    (void)words;
    (void)count;
#endif
}

#if PORT_HOST_BIG_ENDIAN
/* Whether a resolved host pointer is in memory that holds GBA units by value
 * (everything but the I/O registers and the original ROM bytes). */
int Port_GbaMemHoldsUnits(const void* p);
#endif
#ifdef __cplusplus
}
#endif

/* Many engine functions build a pointer from a raw *_BASE macro
 * (VRAM_BASE/PALRAM_BASE/EWRAM_BASE/OAM_BASE, see include/gba/memory.h) plus
 * an offset and then dereference it directly, bypassing the WRITE_16/READ_16
 * translation macros in io.h entirely. On real GBA hardware that "just
 * works" because those addresses are memory-mapped; here they need explicit
 * translation before use. Wrap the pointer in this right after the
 * assignment: `dst = GBA_RESOLVE(VRAM_BASE + offset);`.
 * Using port_resolve_addr ensures it is safe if called on an already-resolved
 * host pointer. */
#define GBA_RESOLVE(p) ((__typeof__(p))port_resolve_addr((uintptr_t)(p)))


static inline void gba_MemClear(u32 addr, u32 size) {
    void* ptr = gba_MemPtr(addr);
    if (ptr != NULL) {
        memset(ptr, 0, size);
    }
}

static inline void gba_MemCopy(u32 srcAddr, u32 destAddr, u32 size) {
    void* src = gba_MemPtr(srcAddr);
    void* dest = gba_MemPtr(destAddr);
    if (src != NULL && dest != NULL) {
        memcpy(dest, src, size);
    }
}

static inline void port_MemCopyToGBA(const void* src, u32 destAddr, u32 size) {
    void* dest = gba_TryMemPtr(destAddr);
    if (src != NULL && dest != NULL) {
        memcpy(dest, src, size);
    }
}
