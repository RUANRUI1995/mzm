#ifndef PORT_GBA_STRUCT_LAYOUT_H
#define PORT_GBA_STRUCT_LAYOUT_H

/* agbcc, the GBA build's compiler, aligns every struct to 4 bytes and rounds its size up to a
 * multiple of 4 (-mstructure-size-boundary=32); a modern ARM compiler aligns a struct only to its
 * widest member. The structs that end up in the save file (struct Sram, include/structs/save_file.h)
 * are marked with PORT_GBA_STRUCT_ALIGN so that the save has the GBA's layout byte for byte, and
 * mzm.sav is the same file an emulator or a cartridge writes (issue #37).
 *
 * That needs 4-byte pointers: struct EnvironmentalEffect, in the save, holds one. A 64-bit host
 * (the Linux build on x86-64) cannot have the GBA's layout, so PORT_GBA_SAVE_LAYOUT is 0 there and
 * it keeps the save as before (all of gSramMem, its own layout).
 *
 * tools/gen_sram_legacy_map.py compiles the old layout with PORT_LEGACY_SRAM_LAYOUT defined, to
 * build the table that converts the saves written before this change (port/port_sram_legacy.c). */
#ifdef PORT_LEGACY_SRAM_LAYOUT
#define PORT_GBA_STRUCT_ALIGN
#else
#define PORT_GBA_STRUCT_ALIGN __attribute__((aligned(4)))
#endif

#if !defined(PORT_LEGACY_SRAM_LAYOUT) && __SIZEOF_POINTER__ == 4
#define PORT_GBA_SAVE_LAYOUT 1
#else
#define PORT_GBA_SAVE_LAYOUT 0
#endif

#endif
