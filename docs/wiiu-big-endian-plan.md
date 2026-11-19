# Wii U: running the port on a big-endian CPU

Issue #39. The Wii U's CPU (PowerPC "Espresso") is big-endian; the GBA, the 3DS
and x86 are little-endian. This is the plan for making the shared code work on
both, checked without a Wii U: `platform/pc` builds for 32-bit PowerPC Linux
(`make OS_TARGET=linux-ppc`) and runs under `qemu-ppc`, and its trace is compared
with the x86 one (`compare_traces.py --cross-endian`).

## Where it stands (2026-11-19)

- The PowerPC build compiles and links (static, headless, `frontend_none.c`).
- It crashes at ROM load: `Port_Build2DTextTable` (port_constructor_init.c)
  reads GBA pointers out of the ROM as native `u32`, which on PowerPC have
  their bytes reversed. Expected: nothing byte-order aware exists yet.
- Already big-endian safe: the C sound engine (`port/audio`, reads ROM data
  byte by byte and splits/joins multi-field words by value), `gba_read16/32`
  and `gba_write16/32` (compose bytes explicitly), the trace's neutral hashes.

## The problem, in three parts

1. **Typed reads of ROM data.** `port/generated` binds ~2,100 ROM tables
   (`p_NAME` pointers into the loaded ROM) that C code reads as `u16`, `u32`,
   structs and pointer arrays, and ~15,000 more objects are reached through GBA
   pointers stored in the ROM (`GBA_RESOLVE`). On PowerPC every multi-byte read
   of them is wrong.
2. **Bytes that must stay in GBA order.** VRAM tile data, compressed streams
   (LZ77/RLE), sound samples and music byte code are read as bytes; swapping
   them would break them. Palettes, OAM and tilemaps are `u16` units.
3. **Copies between the two.** DMA/CpuSet/LZ77 move data from ROM into EWRAM
   buffers that C code then reads as typed data (`gTilemap`, decompressed
   clipdata, ...) and from there into VRAM/palette/OAM, which the PPU reads.

## The approach

**A. Two byte orders, one rule.** Memory falls in two groups:

| Native (host order) | GBA order (little-endian) |
|---|---|
| the ROM copy C code reads (transcoded, B) | the pristine ROM bytes |
| EWRAM, IWRAM, host globals | VRAM, palette RAM, OAM, I/O registers |

Inside a group, copies are plain `memcpy`. A copy that crosses groups moves
**units** (DMA and CpuSet already say 16 or 32 bits) and keeps each unit's
value, so it swaps per unit, exactly what the hardware semantics are. The PPU
and `gba_read*` read the GBA-order group as little-endian explicitly (the PPU's
`mode1.c` already reads tilemaps as byte pairs; palettes and OAM need the same).

**B. A transcoded ROM.** At load, a second copy of the ROM is made in which
every typed object has its multi-byte fields swapped to host order, driven by a
type map. `GBA_RESOLVE`/`port_resolve_addr` (typed access) point into it;
`port_resolve_copy_src` (DMA/CpuSet/LZ77 sources) and byte-stream readers use
the pristine one. On little-endian hosts both are the same buffer and nothing
changes.

**C. The type map, generated.** The decomp gives everything needed:

- `mzm_{eu,us,jp}.map` give the ROM address of every named data object
  (~9,400 `s*` symbols, plus the port/generated ones) per region;
- `src/data/*.c` defines those objects with their C types; compiling them with
  devkitARM's `arm-none-eabi-gcc -g` (the extracted assets are in `data/`)
  gives each one's type in DWARF: field offsets, sizes, array counts.

A generator (`tools/gen_rom_type_map.py`, pyelftools) joins the two into a
compact table per region: `(address, type)` plus the types' scalar layouts
(offset + width of each 2- and 4-byte scalar, arrays repeated). Objects whose
element type is 1 byte (`u8` gfx, compressed data, samples) are left alone.
Objects declared `u16`/`u32` that are really byte streams (some `INCBIN_U32`
graphics) are listed by name in an exception file, found by the trace when a
picture comes out scrambled.

**D. Typed data in EWRAM after a raw copy.** About 28 call sites decompress
or copy ROM data into EWRAM and then read it as typed data. Each gets an
explicit fix-up after the copy (swap the buffer as its type says), guarded so
little-endian builds compile to nothing. The trace finds them: the first
diverging frame points at the buffer.

**E. Bitfields.** `include/oam.h` and the structs listed in the issue use
bitfields, which big-endian GCC allocates from the most significant bit. The
ones that overlay ROM data or GBA-order memory get
`__attribute__((scalar_storage_order("little-endian")))` or are rewritten as
explicit shifts, in the port's guarded copies.

**F. Saves and states.** `mzm.sav` stays the GBA's file byte for byte (issue
#37): read and written as little-endian through the existing layout code.
Save states are per platform (they already are per build).

## Order of work

1. Type map generator (C) + loader that builds the transcoded ROM (B), and the
   resolver split. Goal: PowerPC boots to the title screen.
2. Palette/OAM/VRAM unit-swapping copies and the PPU's little-endian reads (A).
   Goal: the title screen's `fb` hash matches x86.
3. Walk the sample replay frame by frame with `compare_traces.py --cross-endian`
   and `--dump`, fixing EWRAM typed buffers (D) and bitfields (E) as they show.
4. More replays (every area, menus, cutscenes, the map screen), then the
   sound (`snd` column) and the bottom screen.
5. Only then the Wii U frontend (devkitPPU + wut, SDL2 for Wii U, Cemu).

Each step keeps the little-endian builds byte-identical (their traces must not
change) and is merged on its own.
