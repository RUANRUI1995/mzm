# Wii U: running the port on a big-endian CPU

Issue #39. The Wii U's CPU (PowerPC "Espresso") is big-endian; the GBA, the 3DS
and x86 are little-endian. This is the plan for making the shared code work on
both, checked without a Wii U: `platform/pc` builds for 32-bit PowerPC Linux
(`make OS_TARGET=linux-ppc`) and runs under `qemu-ppc`, and its trace is compared
with the x86 one (`compare_traces.py --cross-endian`).

## Where it stands (2026-11-19)

Step 3 is in for the sample replay: the PowerPC build matches x86 on every
neutral hash (frame, VRAM, palettes, OAM, EWRAM, IWRAM, I/O) for all 2551
frames of `replays/eu_new_game_walk.txt` (title, file select, intro, Brinstar
walk). Other replays (step 4) are next.

Step 3 (what the replay found):

- Type map: per-region assets (`extract_region_assets`, the EU ROM's own
  `.inc` files instead of `include/extracted`), agbcc's struct layout
  (`-mstructure-size-boundary=32`; 4-byte alignment unless the size says
  `PACKED`), and a misplaced global now drops only the statics just before it.
  All of `src/data` is placed except part of `text_data.o`. `.gfx` objects are
  left untyped (byte streams even when declared `u32`); the tilesets' tilemaps
  (`sTileset_*_Tilemap`, declared `u8`) are typed as u16; the other `.tm`
  object, `sDoorTransitionTilemap`, is an RLE stream and stays untyped.
- `Port_RomIsTyped`'s bitmap is allocated (step 2 never did, so every copy
  read the original bytes). Into native memory, typed ROM data comes from the
  swapped copy and untyped data as raw bytes (u8 tables such as
  `sClipdataCollisionTypes`).
- Bitfields (E): `BombChain`, `BlockBehavior`, `HatchLockEvent`, `HatchData`,
  `CutsceneInfo`, `CutsceneOamData`, `Demo`, `Haze`, `MenuOamData` and
  `SectionInfo` have big-endian variants with the fields reversed within each
  byte, so their bytes match the GBA's; the type map swaps a bitfield's unit
  only when a field straddles a byte (`OamData`).
- Native buffers of graphics built in other units are listed in
  `sGfxBuffers` (port_gba_mem.c): `gAmmoDigitsGfx` and the animated tiles
  staged at `EWRAM + 0x1B000` (GBA byte order), `gMinimapTilesGfx` (u32 words,
  `ORDER_WORDS`).
- Per site: `RoomRleDecompress` swaps its u16 output pairs; the room tilemap
  updates build their two-entry words with `PORT_HALVES32`; the text renderer
  converts its u32 tile rows (`TextTileWordsToHost`).

Step 1 (the type map and the swapped ROM):

- `tools/gen_rom_type_map.py` generates `port/generated/byteswap/rom_type_map_eu.c`
  (11,046 typed objects after step 3, from the 473 `src/data` files plus
  `sSoundDataEntries`). Statics are placed from gcc's order and alignment,
  re-anchored on the map's globals. Regenerate it after a change to `src/data`
  or `include/structs`.
- `port_rom_swap.c` builds the swapped copy at load (`PORT_ROM_SWAP`, PC builds;
  little-endian hosts skip it). `gRomData` is the typed copy, `gRomDataRaw` the
  original; `gba_read*` and `port_resolve_copy_src` use the original. It also
  keeps a bitmap of the typed objects (`Port_RomIsTyped`).
- `port_read32le`/`port_read16le`/`port_rom_raw` (port_gba_mem.h) read ROM data
  as little-endian from the original bytes; the C sound engine, `MidiKey2Freq`
  and `AudioCommand_Voice`'s voice-table reads use them (the sound data is
  assembly, not in the type map).

Step 2 (the memories' byte orders, see "A" below as built):

- VRAM, palette RAM and OAM are arrays of host-order halfwords; I/O stays in
  GBA order. The game's typed u16 stores into VRAM (tilemap edits) then just
  work; the PPU reads VRAM byte `a` at `a ^ 1` (`MODE1_VRAM_BYTE`), and its
  4bpp row and colour/token pair shortcuts are byte-order aware.
- `DmaTransfer`, `BitFill`, `CpuSet` and `CpuFastSet` go through
  `Port_GbaCopy`/`Port_GbaFill` (port_gba_mem.c), which move units by value
  between orders. On little-endian hosts they are the old memmove and stores.
- `LZ77Uncomp` stores its output as halfwords when the destination holds
  units by value (VRAM, EWRAM): graphics staged in EWRAM then reach VRAM
  intact, and decompressed tilemaps and clipdata read right from C.
- `gba_read/write16/32` treat EWRAM and IWRAM as native.
- `union OamData`'s bitfields are declared from the top of each halfword down
  on big-endian hosts (`include/oam.h`).
- `CpuSet`'s flag bits were wrong (bit 24 taken as 32-bit); fixed to GBATek's
  (24 fill, 26 32-bit), which the game's only callers (32-bit fills) did not
  notice.
- The trace hashes VRAM as u16 values (same hash as before on little-endian).

Known limits of the unit rule, to fix per site when the trace finds them:
32-bit data copied as 32-bit units between EWRAM and VRAM/palette/OAM is taken
as two u16 (the common case); decompressed data that C reads as bytes, or as
u32, comes out halfword-swapped in EWRAM.

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
| EWRAM, IWRAM, host globals | I/O registers |
| VRAM, palette RAM, OAM as host-order u16 (step 2) | |

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
