#include "port_gba_mem.h"
#include "port_rom.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

u8 gIoMem[0x400];
u8 gEwram[0x40000];
u8 gIwram[0x8000];
/* BG and OBJ palettes, one block as on the GBA: game code copies and clears
 * all 0x400 bytes from 0x05000000 in one go (e.g. the title screen fade,
 * unk_76710). gBgPltt and gObjPltt are its two halves, defined by the linker
 * (ewram_symbols.ld). As two separate arrays they were only adjacent by luck
 * of the link order; when they were not, the copy missed the OBJ palette and
 * the clear overwrote whatever followed gBgPltt. */
u16 gPaletteRam[512] __attribute__((aligned(4)));
u16 gOamMem[0x400 / 2];
u8 gVram[0x18000];
u8 gSramMem[0x10000];

/* VRAM, like palette RAM and OAM, is an array of host-order halfwords
 * (port_gba_mem.h): the GBA's byte at offset o is the host's byte o ^ 1 on a
 * big-endian host. */
#define VRAM_BYTE(off) ((off) ^ (PORT_HOST_BIG_ENDIAN ? 1u : 0u))

#if PORT_HOST_BIG_ENDIAN
/* EWRAM and IWRAM hold host-order data on a big-endian host (the game's C
 * code reads them with typed loads), so 16/32-bit accesses by GBA address
 * are native there. NULL outside them. */
static u8* NativeRamPtr(uint32_t addr, u32 size) {
    if (addr >= 0x02000000u && addr + size <= 0x02040000u)
        return &gEwram[addr - 0x02000000u];
    if (addr >= 0x03000000u && addr + size <= 0x03008000u)
        return &gIwram[addr - 0x03000000u];
    return NULL;
}
#define NATIVE_RAM_WRITE(addr, v)                         \
    do {                                                  \
        u8* nativeRam_ = NativeRamPtr((addr), sizeof(v)); \
        if (nativeRam_) {                                 \
            memcpy(nativeRam_, &(v), sizeof(v));          \
            return;                                       \
        }                                                 \
    } while (0)
#define NATIVE_RAM_READ(addr, type)                          \
    do {                                                     \
        u8* nativeRam_ = NativeRamPtr((addr), sizeof(type)); \
        if (nativeRam_) {                                    \
            type value_;                                     \
            memcpy(&value_, nativeRam_, sizeof(type));       \
            return value_;                                   \
        }                                                    \
    } while (0)
#else
#define NATIVE_RAM_WRITE(addr, v) do { } while (0)
#define NATIVE_RAM_READ(addr, type) do { } while (0)
#endif

void gba_write8(uint32_t addr, uint8_t v) {
    if (addr >= 0x02000000u && addr < 0x02040000u) {
        gEwram[addr - 0x02000000u] = v;
        return;
    }
    if (addr >= 0x03000000u && addr < 0x03008000u) {
        gIwram[addr - 0x03000000u] = v;
        return;
    }
    if (addr >= 0x04000000u && addr < 0x04000400u) {
        gIoMem[addr - 0x04000000u] = v;
        return;
    }
    if (addr >= 0x06000000u && addr < 0x06018000u) {
        gVram[VRAM_BYTE(addr - 0x06000000u)] = v;
        return;
    }
    if (addr >= 0x0E000000u && addr < 0x0E010000u) {
        gSramMem[addr - 0x0E000000u] = v;
        return;
    }

    printf("gba_write8: unimplemented for address 0x%08lX\n", (unsigned long)addr);
}

#if defined(PLATFORM_PC)
/* The Linux build has no display timing: each read of VCOUNT or DISPSTAT
 * advances a virtual scanline instead (platform_pc.c), so the game's busy
 * waits on them end after the same number of reads on every run. */
/* Weak: the unit test links this file without the platform layer. */
extern void Platform_PC_ScanlineTick(void) __attribute__((weak));
#define LINUX_TIMING_READ(addr) \
    do { \
        if (((addr) == 0x04000004u || (addr) == 0x04000006u) && Platform_PC_ScanlineTick) \
            Platform_PC_ScanlineTick(); \
    } while (0)
#else
#define LINUX_TIMING_READ(addr) do { } while (0)
#endif

u8 gba_read8(uint32_t addr) {
    LINUX_TIMING_READ(addr);
    if (addr >= 0x02000000u && addr < 0x02040000u)
        return gEwram[addr - 0x02000000u];
    if (addr >= 0x03000000u && addr < 0x03008000u)
        return gIwram[addr - 0x03000000u];
    if (addr >= 0x04000000u && addr < 0x04000400u)
        return gIoMem[addr - 0x04000000u];
    if (addr >= 0x06000000u && addr < 0x06018000u)
        return gVram[VRAM_BYTE(addr - 0x06000000u)];
    if (addr >= 0x0E000000u && addr < 0x0E010000u)
        return gSramMem[addr - 0x0E000000u];
    if (gRomData && addr >= 0x08000000u && addr < 0x08000000u + gRomSize) {
#ifndef MZM_3DS
        Port_LogRomAccess(addr, "gba_read8");
#endif
        return gRomDataRaw[addr - 0x08000000u];
    }

    printf("gba_read8: unimplemented for address 0x%08lX\n", (unsigned long)addr);
    return 0;
}

void gba_write16(uint32_t addr, uint16_t v) {
    NATIVE_RAM_WRITE(addr, v);
    if (addr >= 0x04000000u && addr < 0x040003FFu) {
        gIoMem[addr - 0x04000000u] = v & 0xFF;
        gIoMem[addr - 0x04000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    if (addr >= 0x02000000u && addr < 0x0203FFFFu) {
        gEwram[addr - 0x02000000u] = v & 0xFF;
        gEwram[addr - 0x02000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    if (addr >= 0x03000000u && addr < 0x03007FFFu) {
        gIwram[addr - 0x03000000u] = v & 0xFF;
        gIwram[addr - 0x03000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    // BG palette
    if (addr >= 0x05000000u && addr < 0x050001FFu) {
        gBgPltt[(addr - 0x05000000u) >> 1] = v;
        return;
    }
    // OBJ palette
    if (addr >= 0x05000200u && addr < 0x050003FFu) {
        gObjPltt[(addr - 0x05000200u) >> 1] = v;
        return;
    }
    // VRAM
    if (addr >= 0x06000000u && addr < 0x06017FFFu) {
        u32 off = addr - 0x06000000u;
        gVram[VRAM_BYTE(off)] = v & 0xFF;
        gVram[VRAM_BYTE(off + 1)] = (v >> 8) & 0xFF;
        return;
    }
    // OAM
    if (addr >= 0x07000000u && addr < 0x070003FFu) {
        gOamMem[(addr - 0x07000000u) >> 1] = v;
        return;
    }
    if (addr >= 0x0E000000u && addr < 0x0E00FFFEu) {
        u32 off = addr - 0x0E000000u;
        gSramMem[off] = v & 0xFF;
        gSramMem[off + 1] = (v >> 8) & 0xFF;
        return;
    }
    printf("gba_write16: unimplemented for address 0x%08lX\n", (unsigned long)addr);
}

u16 gba_read16(uint32_t addr) {
    LINUX_TIMING_READ(addr);
    NATIVE_RAM_READ(addr, u16);
    if (addr >= 0x04000000u && addr < 0x040003FFu)
        return gIoMem[addr - 0x04000000u] | (gIoMem[addr - 0x04000000u + 1] << 8);
    if (addr >= 0x02000000u && addr < 0x0203FFFFu) {
        u32 off = addr - 0x02000000u;
        return gEwram[off] | (gEwram[off + 1] << 8);
    }
    if (addr >= 0x03000000u && addr < 0x03007FFFu) {
        u32 off = addr - 0x03000000u;
        return gIwram[off] | (gIwram[off + 1] << 8);
    }
    // BG palette
    if (addr >= 0x05000000u && addr < 0x050001FFu)
        return gBgPltt[(addr - 0x05000000u) >> 1];
    // OBJ palette
    if (addr >= 0x05000200u && addr < 0x050003FFu)
        return gObjPltt[(addr - 0x05000200u) >> 1];
    // VRAM
    if (addr >= 0x06000000u && addr < 0x06017FFFu) {
        u32 off = addr - 0x06000000u;
        return gVram[VRAM_BYTE(off)] | (gVram[VRAM_BYTE(off + 1)] << 8);
    }
    // OAM
    if (addr >= 0x07000000u && addr < 0x070003FFu)
        return gOamMem[(addr - 0x07000000u) >> 1];
    if (addr >= 0x0E000000u && addr < 0x0E00FFFEu) {
        u32 off = addr - 0x0E000000u;
        return gSramMem[off] | (gSramMem[off + 1] << 8);
    }
    if (gRomData && addr >= 0x08000000u) {
#ifndef MZM_3DS
        Port_LogRomAccess(addr, "gba_read16");
#endif
        u32 off = addr - 0x08000000u;
        if (off + 1 < gRomSize) {
            return gRomDataRaw[off] | (gRomDataRaw[off + 1] << 8);
        }
    }

    printf("gba_read16: unimplemented for address 0x%08lX\n", (unsigned long)addr);
    return 0;
}

void gba_write32(uint32_t addr, uint32_t v) {
    NATIVE_RAM_WRITE(addr, v);
    if (addr >= 0x04000000u && addr < 0x040003FDu) {
        gIoMem[addr - 0x04000000u] = v & 0xFF;
        gIoMem[addr - 0x04000000u + 1] = (v >> 8) & 0xFF;
        gIoMem[addr - 0x04000000u + 2] = (v >> 16) & 0xFF;
        gIoMem[addr - 0x04000000u + 3] = (v >> 24) & 0xFF;
        return;
    }
    // BG palette (0x05000000 - 0x050001FF)
    if (addr >= 0x05000000u && addr < 0x050001FEu) {
        gBgPltt[(addr - 0x05000000u) >> 1] = v & 0xFFFF;
        gBgPltt[((addr - 0x05000000u) >> 1) + 1] = (v >> 16) & 0xFFFF;
        return;
    }
    // BG/OBJ palette boundary straddle
    if (addr == 0x050001FEu) {
        gBgPltt[0xFF] = v & 0xFFFF;
        gObjPltt[0] = (v >> 16) & 0xFFFF;
        return;
    }
    // OBJ palette (0x05000200 - 0x050003FF)
    if (addr >= 0x05000200u && addr < 0x050003FEu) {
        gObjPltt[(addr - 0x05000200u) >> 1] = v & 0xFFFF;
        gObjPltt[((addr - 0x05000200u) >> 1) + 1] = (v >> 16) & 0xFFFF;
        return;
    }
    if (addr >= 0x06000000u && addr < 0x06017FFDu) {
        u32 off = addr - 0x06000000u;
        gVram[VRAM_BYTE(off)] = v & 0xFF;
        gVram[VRAM_BYTE(off + 1)] = (v >> 8) & 0xFF;
        gVram[VRAM_BYTE(off + 2)] = (v >> 16) & 0xFF;
        gVram[VRAM_BYTE(off + 3)] = (v >> 24) & 0xFF;
        return;
    }
    if (addr >= 0x07000000u && addr < 0x070003FDu) {
        gOamMem[(addr - 0x07000000u) >> 1] = v & 0xFFFF;
        gOamMem[(addr - 0x07000000u + 2) >> 1] = (v >> 16) & 0xFFFF;
        return;
    }
    if (addr >= 0x02000000u && addr < 0x0203FFFDu) {
        u32 off = addr - 0x02000000u;
        gEwram[off] = v & 0xFF;
        gEwram[off + 1] = (v >> 8) & 0xFF;
        gEwram[off + 2] = (v >> 16) & 0xFF;
        gEwram[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    if (addr >= 0x03000000u && addr < 0x03007FFDu) {
        u32 off = addr - 0x03000000u;
        gIwram[off] = v & 0xFF;
        gIwram[off + 1] = (v >> 8) & 0xFF;
        gIwram[off + 2] = (v >> 16) & 0xFF;
        gIwram[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    if (addr >= 0x0E000000u && addr < 0x0E00FFFDu) {
        u32 off = addr - 0x0E000000u;
        gSramMem[off] = v & 0xFF;
        gSramMem[off + 1] = (v >> 8) & 0xFF;
        gSramMem[off + 2] = (v >> 16) & 0xFF;
        gSramMem[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    printf("gba_write32: unimplemented for address 0x%08lX\n", (unsigned long)addr);
}

u32 gba_read32(uint32_t addr) {
    NATIVE_RAM_READ(addr, u32);
    if (addr >= 0x04000000u && addr < 0x040003FDu)
        return gIoMem[addr - 0x04000000u] | (gIoMem[addr - 0x04000000u + 1] << 8) |
               (gIoMem[addr - 0x04000000u + 2] << 16) | (gIoMem[addr - 0x04000000u + 3] << 24);
    if (addr >= 0x05000000u && addr < 0x050001FEu)
        return gBgPltt[(addr - 0x05000000u) >> 1] | (gBgPltt[((addr - 0x05000000u) >> 1) + 1] << 16);
    if (addr >= 0x05000200u && addr < 0x050003FEu)
        return gObjPltt[(addr - 0x05000200u) >> 1] | (gObjPltt[((addr - 0x05000200u) >> 1) + 1] << 16);
    if (addr >= 0x06000000u && addr < 0x06017FFDu) {
        u32 off = addr - 0x06000000u;
        return gVram[VRAM_BYTE(off)] | (gVram[VRAM_BYTE(off + 1)] << 8) | (gVram[VRAM_BYTE(off + 2)] << 16) |
               ((u32)gVram[VRAM_BYTE(off + 3)] << 24);
    }
    if (addr >= 0x07000000u && addr < 0x070003FDu)
        return gOamMem[(addr - 0x07000000u) >> 1] | (gOamMem[(addr - 0x07000000u + 2) >> 1] << 16);
    if (addr >= 0x02000000u && addr < 0x0203FFFDu) {
        u32 off = addr - 0x02000000u;
        return gEwram[off] | (gEwram[off + 1] << 8) | (gEwram[off + 2] << 16) | (gEwram[off + 3] << 24);
    }
    if (addr >= 0x03000000u && addr < 0x03007FFDu) {
        u32 off = addr - 0x03000000u;
        return gIwram[off] | (gIwram[off + 1] << 8) | (gIwram[off + 2] << 16) | (gIwram[off + 3] << 24);
    }
    if (addr >= 0x0E000000u && addr < 0x0E00FFFDu) {
        u32 off = addr - 0x0E000000u;
        return gSramMem[off] | (gSramMem[off + 1] << 8) | (gSramMem[off + 2] << 16) | (gSramMem[off + 3] << 24);
    }
    if (gRomData && addr >= 0x08000000u) {
#ifndef MZM_3DS
        Port_LogRomAccess(addr, "gba_read32");
#endif
        u32 off = addr - 0x08000000u;
        if (off + 3 < gRomSize) {
            return gRomDataRaw[off] | (gRomDataRaw[off + 1] << 8) | (gRomDataRaw[off + 2] << 16) |
                   ((u32)gRomDataRaw[off + 3] << 24);
        }
    }

    printf("gba_read32: unimplemented for address 0x%08lX\n", (unsigned long)addr);
    return 0;
}

/* On the 3DS specifically, malloc() can (and does -- confirmed on hardware,
 * gRomData observed at 0x0800eb20) hand back addresses that numerically fall
 * inside the very same 0x08000000-0x0A000000 window this file treats as "raw
 * GBA ROM address, needs translating through gba_TryMemPtr()". gRomData
 * itself is exactly such an allocation, so any ALREADY-RESOLVED pointer into
 * it (e.g. a shim macro like sLanguageSelectGfx, which is
 * *p_sLanguageSelectGfx = gRomData + romOffset) numerically collides with
 * that heuristic. Passing such a pointer back through port_resolve_addr()
 * translates it a SECOND time as if it were a raw GBA address, landing on
 * the wrong ROM bytes entirely -- this was a real, reproduced-on-hardware
 * bug: Lz77Uncomp (port_bios.c) read a bogus decompressed-size from a
 * mistranslated sLanguageSelectGfx pointer and walked its output past the
 * whole program's BSS before faulting (see docs/3ds-port-status-*.md and the
 * commit that added this check). Doesn't reproduce on Linux/x86_64, where
 * malloc() addresses never land in that range.
 *
 * A value inside [gRomData, gRomData+gRomSize) is unambiguous: only
 * already-resolved ROM pointers can equal it (no code constructs a raw GBA
 * address from gRomData's host pointer value), so checking this first and
 * returning unchanged is always correct, on every platform. */
static bool IsWithinRomDataBuffer(uintptr_t val) {
    return (gRomData != NULL && val >= (uintptr_t)gRomData && val < (uintptr_t)gRomData + gRomSize) ||
           (gRomDataRaw != NULL && val >= (uintptr_t)gRomDataRaw && val < (uintptr_t)gRomDataRaw + gRomSize);
}

#ifdef MZM_3DS
/* True if the numeric value falls in a numeric range this port treats as a
 * raw GBA address (i.e. it could be a real GBA address to translate, or a
 * host pointer that merely collides numerically with one). */
static bool InGbaNumericRange(uintptr_t val) {
    return (val >= 0x02000000u && val < 0x0A000000u) ||
           (val >= 0x0E000000u && val < 0x0E010000u);
}

/* Host stack pointers can land numerically inside GBA ranges (especially the
 * ROM range 0x08000000..0x08000000+gRomSize, which overlaps the 3DS
 * main-thread stack). Any value that is both in a GBA numeric range AND an
 * active stack address is unambiguously a native host pointer, never a raw
 * GBA address. */
static bool IsActiveStackPtr(uintptr_t val) {
    if (!InGbaNumericRange(val))
        return false;
    extern int Platform3DS_IsActiveStackAddress(uintptr_t value);
    return Platform3DS_IsActiveStackAddress(val) != 0;
}
#else
static bool InGbaNumericRange(uintptr_t val) {
    (void)val;
    return false;
}
static bool IsActiveStackPtr(uintptr_t val) {
    (void)val;
    return false;
}
#endif

void* port_resolve_addr(uintptr_t val)
{
    if (IsWithinRomDataBuffer(val)) {
        return (void*)val;
    }
    /* A value that is currently on the host stack is unambiguously a native
     * pointer, never a raw GBA address -- check this before gba_TryMemPtr,
     * which would otherwise happily "resolve" it into ROM (this is exactly
     * how mzm.sav ended up all zeros on hardware: a live stack address
     * numerically inside the ROM range got translated a second time). */
    if (IsActiveStackPtr(val)) {
        return (void*)val;
    }
    /* GBA-range values (EWRAM/IWRAM/IO/palette/VRAM/OAM/ROM/SRAM) are
     * address-mapped through gba_TryMemPtr; anything outside that window is
     * already a native host pointer (e.g. a local scalar or struct whose
     * address happens to get passed through the same code path as a real
     * GBA address) and is returned unchanged. */
    if (InGbaNumericRange(val)) {
        void* p = gba_TryMemPtr((uint32_t)val);
        if (p) {
            return p;
        }
    }
    return (void*)val;
}

void* port_resolve_write_addr(uintptr_t val) {
    if (IsWithinRomDataBuffer(val)) {
        return (void*)val;
    }
    if (IsActiveStackPtr(val)) {
        return (void*)val;
    }
    /* 0x08000000 and above (except 0x0E000000 SRAM) is read-only cartridge
     * space on GBA. A write destination in that numeric range is therefore a
     * native host pointer, most commonly a 3DS stack or heap object. */
    if ((val >= 0x02000000u && val < 0x08000000u) || (val >= 0x0E000000u && val < 0x0E010000u)) {
        void* p = gba_TryMemPtr((uint32_t)val);
        if (p) return p;
    }
    return (void*)val;
}

const void* port_resolve_copy_src(const void* src, u32 size) {
    const u8* resolved;
    (void)size;
    if (IsActiveStackPtr((uintptr_t)src)) {
        return src;
    }
    resolved = (const u8*)port_resolve_addr((uintptr_t)src);
    /* Copies take the ROM's own bytes: on a big-endian host gRomData is the
     * byte-swapped copy for typed reads, gRomDataRaw the original. */
    if (gRomDataRaw != gRomData && gRomData != NULL && resolved >= gRomData && resolved < gRomData + gRomSize)
        return gRomDataRaw + (resolved - gRomData);
    return resolved;
}

/* ---- Unit copies and fills (port_gba_mem.h) ------------------------------ */

#if PORT_HOST_BIG_ENDIAN
/* ORDER_WORDS: native memory the game fills with u32 words of tile pixels
 * (word value: the GBA's bytes 0-3, little-endian) and copies to VRAM in
 * 16-bit units; a halfword of it is its word's low or high half. */
enum { ORDER_NATIVE, ORDER_HALFWORDS, ORDER_GBA, ORDER_WORDS };

/* Host buffers of graphics that the game builds in units other than
 * halfwords and then copies to VRAM in 16-bit units: those copies convert per
 * unit. The buffers are 4-byte aligned. */
extern u8 gAmmoDigitsGfx[64];
extern u32 gMinimapTilesGfx[5 * 24];

static const struct {
    const u8* start;
    u32 size;
    int order;
} sGfxBuffers[] = {
    { gAmmoDigitsGfx, sizeof(gAmmoDigitsGfx), ORDER_GBA },                     /* src/hud.c, bytes */
    { (const u8*)gMinimapTilesGfx, sizeof(gMinimapTilesGfx), ORDER_WORDS },  /* src/minimap.c, u32 rows */
    { gEwram + 0x1B000, 16 * 0x80, ORDER_GBA },  /* ANIMATED_GFX_EWRAM_BASE: animated tiles from the ROM, 16 slots */
};

static int PortMemOrder(const u8* p) {
    u32 i;
    for (i = 0; i < sizeof(sGfxBuffers) / sizeof(sGfxBuffers[0]); i++) {
        if (p >= sGfxBuffers[i].start && p < sGfxBuffers[i].start + sGfxBuffers[i].size)
            return sGfxBuffers[i].order;
    }
    if (p >= (const u8*)gPaletteRam && p < (const u8*)gPaletteRam + sizeof(gPaletteRam))
        return ORDER_HALFWORDS;
    if (p >= (const u8*)gOamMem && p < (const u8*)gOamMem + sizeof(gOamMem))
        return ORDER_HALFWORDS;
    if (p >= gVram && p < gVram + sizeof(gVram))
        return ORDER_HALFWORDS;
    if (p >= gIoMem && p < gIoMem + sizeof(gIoMem))
        return ORDER_GBA;
    if (gRomDataRaw != NULL && p >= gRomDataRaw && p < gRomDataRaw + gRomSize)
        return ORDER_GBA;
    return ORDER_NATIVE;
}

static u32 UnitRead(int order, const u8* p, u32 unit) {
    if (order == ORDER_WORDS && unit == 2) {
        u32 word;
        memcpy(&word, (const u8*)((uintptr_t)p & ~(uintptr_t)3), 4);
        return ((uintptr_t)p & 2) ? word >> 16 : word & 0xFFFF;
    }
    if (order == ORDER_GBA) {
        if (unit == 2)
            return (u32)p[0] | ((u32)p[1] << 8);
        return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
    }
    if (unit == 2) {
        u16 v;
        memcpy(&v, p, 2);
        return v;
    }
    if (order == ORDER_HALFWORDS) {
        u16 h[2];
        memcpy(h, p, 4);
        return (u32)h[0] | ((u32)h[1] << 16);
    }
    {
        u32 v;
        memcpy(&v, p, 4);
        return v;
    }
}

static void UnitWrite(int order, u8* p, u32 unit, u32 v) {
    if (order == ORDER_WORDS && unit == 2) {
        u32 word;
        u8* base = (u8*)((uintptr_t)p & ~(uintptr_t)3);
        memcpy(&word, base, 4);
        word = ((uintptr_t)p & 2) ? (word & 0xFFFF) | (v << 16) : (word & 0xFFFF0000u) | (v & 0xFFFF);
        memcpy(base, &word, 4);
        return;
    }
    if (order == ORDER_GBA) {
        p[0] = (u8)v;
        p[1] = (u8)(v >> 8);
        if (unit == 4) {
            p[2] = (u8)(v >> 16);
            p[3] = (u8)(v >> 24);
        }
        return;
    }
    if (unit == 2) {
        u16 h = (u16)v;
        memcpy(p, &h, 2);
        return;
    }
    if (order == ORDER_HALFWORDS) {
        u16 h[2] = { (u16)v, (u16)(v >> 16) };
        memcpy(p, h, 4);
        return;
    }
    memcpy(p, &v, 4);
}

int Port_GbaMemHoldsUnits(const void* p) {
    return PortMemOrder((const u8*)p) != ORDER_GBA;
}

/* The source and its order for a copy to dstOrder. Into native memory, a
 * typed ROM object comes from the swapped copy (same type on both sides, the
 * usual case) and everything else from the original bytes as they are: byte
 * tables (clipdata types, ...) that C code reads as u8. Graphics staged in a
 * native buffer on their way to VRAM need that buffer in sGfxBuffers. Into
 * the other memories, ROM data is read as GBA units. */
static const u8* CopySource(const void* src, int dstOrder, int* order) {
    const u8* typed = (const u8*)port_resolve_addr((uintptr_t)src);
    if (gRomData != NULL && typed >= gRomData && typed < gRomData + gRomSize) {
        if (dstOrder == ORDER_NATIVE) {
            *order = ORDER_NATIVE;
            return Port_RomIsTyped((u32)(typed - gRomData)) ? typed : gRomDataRaw + (typed - gRomData);
        }
        *order = ORDER_GBA;
        return gRomDataRaw + (typed - gRomData);
    }
    *order = PortMemOrder(typed);
    return typed;
}

static int SameLayout(int a, int b, u32 unit) {
    if (a == b)
        return 1;
    /* Native and halfword memory agree on 16-bit units, and a 32-bit copy
     * between them is taken to move u16 data (palettes, OAM). */
    (void)unit;
    return a != ORDER_GBA && b != ORDER_GBA && a != ORDER_WORDS && b != ORDER_WORDS;
}

void Port_GbaCopy(void* dst, const void* src, u32 len, u32 unit) {
    u8* d;
    const u8* s;
    int dOrder, sOrder;
    u32 i;
    if (len == 0 || !src || !dst)
        return;
    d = (u8*)port_resolve_write_addr((uintptr_t)dst);
    if (!d)
        return;
    dOrder = PortMemOrder(d);
    s = CopySource(src, dOrder, &sOrder);
    if (!s)
        return;
    if ((unit != 2 && unit != 4) || SameLayout(dOrder, sOrder, unit)) {
        memmove(d, s, len);
        return;
    }
    for (i = 0; i + unit <= len; i += unit)
        UnitWrite(dOrder, d + i, unit, UnitRead(sOrder, s + i, unit));
}

void Port_GbaFill(void* dst, u32 value, u32 len, u32 unit) {
    u8* d;
    int order;
    u32 i;
    if (len == 0 || !dst)
        return;
    d = (u8*)port_resolve_write_addr((uintptr_t)dst);
    if (!d)
        return;
    if (value == 0) {
        memset(d, 0, len);
        return;
    }
    if (unit != 2 && unit != 4) {
        memset(d, (int)value, len);
        return;
    }
    order = PortMemOrder(d);
    for (i = 0; i + unit <= len; i += unit)
        UnitWrite(order, d + i, unit, value);
}

u32 Port_GbaReadUnit(const void* src, u32 unit) {
    int order;
    const u8* s = CopySource(src, ORDER_GBA, &order);
    if (!s)
        return 0;
    return UnitRead(order, s, unit);
}

#else /* little-endian host: one byte order everywhere */

void Port_GbaCopy(void* dst, const void* src, u32 len, u32 unit) {
    const void* s;
    void* d;
    (void)unit;
    if (len == 0 || !src || !dst)
        return;
    s = port_resolve_copy_src(src, len);
    d = port_resolve_write_addr((uintptr_t)dst);
    if (s && d)
        memmove(d, s, len);
}

void Port_GbaFill(void* dst, u32 value, u32 len, u32 unit) {
    u32 i;
    void* d;
    if (len == 0 || !dst)
        return;
    d = port_resolve_write_addr((uintptr_t)dst);
    if (!d)
        return;
    if (value == 0) {
        memset(d, 0, len);
    } else if (unit == 2) {
        u16* p = (u16*)d;
        for (i = 0; i < len / 2; i++)
            p[i] = (u16)value;
    } else if (unit == 4) {
        u32* p = (u32*)d;
        for (i = 0; i < len / 4; i++)
            p[i] = value;
    } else {
        memset(d, (int)value, len);
    }
}

u32 Port_GbaReadUnit(const void* src, u32 unit) {
    const u8* s = (const u8*)port_resolve_copy_src(src, unit);
    if (!s)
        return 0;
    if (unit == 2)
        return *(const u16*)s;
    return *(const u32*)s;
}
#endif
