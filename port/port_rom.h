#pragma once
#include "types.h"
#include <stddef.h>

/*
 * ROM loading and GBA-address resolution for the native (3DS) port.
 *
 * Unlike the GBA build (which compiles the src/data/ C files directly into
 * the binary via the extracted/ includes), the native port does NOT bake
 * copyrighted assets into the distributed binary. Instead, the user supplies
 * their own legally-dumped ROM at runtime; this file loads it into gRomData
 * and resolves GBA ROM addresses (0x08000000+) to native pointers into that
 * buffer. The per-symbol offset tables that make this work for every
 * src/data/ symbol are generated from the matching build's .map file —
 * see tools/gen_port_rom_offsets.py and docs/3ds-port-rom-loading.md.
 */

typedef enum PortRomRegion {
    PORT_ROM_REGION_UNKNOWN = 0,
    PORT_ROM_REGION_US,
    PORT_ROM_REGION_EU,
    PORT_ROM_REGION_JP,
    /* iQue (China) release, game code BMXC. A separate localisation rather
     * than a patch: its data blocks sit at different offsets and carry a
     * Chinese font, so it has its own offset tables. Its text is a u16
     * character-id stream like the JP build's, hence REGION_IS_JP() being
     * true for it in include/region.h until the text system is separated. */
    PORT_ROM_REGION_CN,
} PortRomRegion;

/* ROM data buffer (also declared in port_gba_mem.h to avoid a circular
 * include; both declarations must stay in sync). */
extern u8* gRomData;
/* The ROM exactly as loaded. The same buffer as gRomData, except on a
 * big-endian host, where gRomData has its typed data byte-swapped
 * (port_rom_swap.c) and this keeps the original bytes for byte and GBA-order
 * reads (gba_read16/32, DMA and decompression sources). */
extern u8* gRomDataRaw;
extern u32 gRomSize;

/* port_rom_swap.c */
int Port_HostIsBigEndian(void);
int Port_RomSwap_Apply(u8* rom, u32 size, int region);
/* Whether ROM offset lies in a typed object that the swap changed
 * (big-endian hosts; 0 elsewhere). */
int Port_RomIsTyped(u32 offset);
u32 Port_RomSize(void);

/* Region of the currently loaded ROM. PORT_ROM_REGION_UNKNOWN until a
 * successful Port_LoadRom(). */
extern PortRomRegion gRomRegion;

/*
 * Load and validate a ROM file. Reads the 4-byte game code at header offset
 * 0xAC (BMXE=US, BMXP=EU, BMXJ=JP, BMXC=CN; matches Makefile GAME_CODE) to
 * detect the region, sets gRomRegion accordingly, and populates
 * gRomData/gRomSize.
 *
 * Returns 1 on success, 0 on failure (file not found, wrong game code, or
 * allocation failure). Does not verify the SHA-1 — callers that need a
 * stricter check (e.g. to distinguish a clean ROM from a romhack sharing the
 * same game code) should hash gRomData themselves against
 * mzm_{us,eu,jp,cn}.sha1.
 */
int Port_LoadRom(const char* path);

/* Human-readable label for gRomRegion ("US", "EU", "JP", "CN", "?"). */
const char* Port_RomRegionLabel(PortRomRegion region);

/*
 * Clamp gLanguage to a value valid for the loaded ROM's region (needed when
 * a save is shared between region builds). Returns 1 if gLanguage changed.
 */
int Port_ClampLanguageToRegion(void);

/*
 * Resolve a GBA ROM address (0x08000000-0x09FFFFFF) to a native pointer
 * into gRomData. Returns NULL if gRomData isn't loaded or the address is
 * out of range.
 */
static inline void* Port_ResolveRomData(u32 gbaAddr) {
    if (gRomData != NULL && gbaAddr >= 0x08000000u && gbaAddr < 0x08000000u + gRomSize) {
        return &gRomData[gbaAddr - 0x08000000u];
    }
    return NULL;
}

/* A pointer into gRomData at the same place in gRomDataRaw, the ROM's own
 * bytes (other pointers come back as they are). For game code that indexes a
 * byte table past its bounds, into the object next to it: the GBA reads that
 * object's bytes as they are in the ROM, which a big-endian host keeps
 * swapped in gRomData when the object is typed. */
static inline const u8* Port_RomRawBytes(const void* p) {
    const u8* b = (const u8*)p;
    if (gRomData != NULL && gRomDataRaw != NULL && b >= gRomData && b < gRomData + gRomSize)
        return gRomDataRaw + (b - gRomData);
    return b;
}

/* The halfword at p (a pointer into gRomData, maybe past the end of its
 * table) as the GBA reads it: little-endian, from the ROM's own bytes. */
static inline u16 Port_RomRawU16(const void* p) {
    const u8* b = Port_RomRawBytes(p);
    return (u16)(b[0] | b[1] << 8);
}

/* ROM access logging - logs unique ROM addresses accessed at runtime.
 * Declared here (not just in port_gba_mem.h) because port_rom.c owns the
 * implementation. */
void Port_LogRomAccess(u32 gba_addr, const char* caller);
void Port_PrintRomAccessSummary(void);
