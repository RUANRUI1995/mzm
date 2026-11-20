# Wii U: running the port on a big-endian CPU

Issue #39. The Wii U's CPU (PowerPC "Espresso") is big-endian; the GBA, the 3DS
and x86 are little-endian. This is the plan for making the shared code work on
both, checked without a Wii U: `platform/pc` builds for 32-bit PowerPC Linux
(`make OS_TARGET=linux-ppc`) and runs under `qemu-ppc`, and its trace is compared
with the x86 one (`compare_traces.py --cross-endian`).

## Where it stands (2026-11-20)

The PowerPC build matches x86 on the game values and every neutral hash
(frame, VRAM, palettes, OAM, sound mix) for all frames of every replay in
`platform/pc/replays`:

- `eu_new_game_walk.txt` (2551 frames): title, file select, intro, a walk in
  the first Brinstar room.
- `eu_title_demos.txt` (8000 frames): the intro left to run and the attract
  demos in Brinstar, Norfair and Kraid.
- `eu_pause_map.txt` (3400 frames): the walk, then the map and status
  screens.
- `eu_area_<area>.txt` (1800 frames each): a save resumed in Kraid, Norfair,
  Ridley, Tourian, Crateria and Chozodia, every item, a walk through a few
  rooms (shots, bombs, missiles, enemies).
- `eu_boss_kraid.txt` (3100 frames): the Kraid fight.

And `mzm.sav` is the same file on both: written by either, read by either
(F, below).

Part F (saves):

- `mzm.sav` stays the GBA's bytes. The game keeps the save in host order in
  `gSramMem`; `port/port_sram_swap.c` swaps struct Sram's scalars when the
  file is read and written (`SramWriteAtomic`, `ReadSramFile`), from a table
  of runs that `tools/gen_sram_byteswap.py` takes from devkitARM's DWARF
  (`port/generated/byteswap/sram_layout.c`; `--check` says if it is stale).
- The checksums are rewritten as the target byte order sums them (the game
  sums each record as host u32 or u16 words, which pack smaller fields
  differently in the two orders), keeping how far off a damaged one was, so
  validity survives the conversion both ways.
- The PC build never read `mzm.sav` back (only the 3DS called
  `Port_LoadSram`), and quit without waiting for the save thread; both fixed.
- Replays can start from a save (`save FILE` line, `--load-save`), and
  `tools/mzm_save.py` makes saves in any room (`warp`).
- Not done: a 64 KB save from a 3DS build before issue #37 (the "legacy"
  layout, `port/port_sram_legacy.c`) is converted with native reads, so a
  big-endian host would misread one; it is read right on little-endian hosts.

Step 4 so far (what the new replays found):

- `SamusUpdateEnvironmentalEffect` read the env effects' frame table as host
  pointers: its entries are GBA addresses (`RESOLVE_SAMUS_PTR`). The x86 build
  crashed on the first splash in a demo; it is likely the "corrupted slot" the
  3DS guards against since 2f43d826.
- Byte data copied into buffers read by value: the intro's fuzz palette (u8
  tables used as colours) and a demo's minimap and hatch bit sets (u8 arrays
  in `struct SaveDemo`, u32 in RAM), with `Port_SwapHalfwordBytes` and
  `Port_SwapWordBytes` (port_gba_mem.h).
- A backwards sample's start read the sample length natively; sound data is
  not in the type map, so `port_read32le`.

The bottom screen: with `--bottom` the trace gets a fourth group, the bottom
screen's hash, and PowerPC matches x86 on every frame of the walk, three area
replays going through the five tabs (map, status, save states, achievements,
options), the pause map, the Kraid fight and the demos. It needed no fix in
`port/ui`; what differed was the run, not the byte order: the UI's clock,
timer and battery now come from the frame count in a replay or traced run,
and the UI is rebuilt when HEAD moves (it printed a stale commit hash).

Bosses and cutscenes: the saves made from the walk's file had the SRAM's
0xFF fill for world data (the file was never saved), so every event was set
and every boss dead; `mzm_save.py warp` now gives such a file a new game's
world data, sets events (`--event`, Ridley needs `GRAVITY_SUIT_OBTAINED`)
and puts Samus on an elevator's pad (`--y`). PowerPC matches x86 on the four
boss fights (Kraid, Ridley, Mother Brain, Mecha Ridley, with a game over) and
on seven cutscenes started by a door or an elevator (Kraid rising, before the
Ruins Test, Mecha Ridley sees Samus, the Mother Brain close-up, entering
Tourian, Ridley landing, Ridley in space). The fix: two cutscenes read
`sRandomNumberTable` out of its bounds (a negative or past-255 index), so the
GBA reads the ROM objects next to it, `sSineTable` and `sIntrTable`, which a
big-endian host keeps swapped; those reads take the original bytes
(`Port_RomRawBytes`, port_rom.h).

The escapes and the ending: `eu_cutscene_escape_ship` gets into the escape
ship (Samus in the blue ship, the Chozodia escape, the credits and the final
screen, 13500 frames) and `eu_cutscene_gunship` into the gunship (the Zebes
escape and the story text after it). Both crashed the x86 build first, on
GBA addresses read as host pointers: the Chozodia escape's explosion
(`sHaze_PowerBomb_WindowValuesPointers`, resolved in haze.c but not there)
and the ending's text OAM (`oamFramePointers`, copied from the ROM). On the
3DS those addresses fall in the app's heap, so they likely drew garbage
rather than crash. Then the Zebes escape read `sTourianEscape_47cf50` one
entry past its end, into the pointer table after it, whose halves a
big-endian host swaps (`Port_RomRawU16`). PowerPC matches x86 on both.
The Ridley spawn cutscene is in the Ridley fight replay.

Not covered: the statue opening (Kraid or Ridley has to die) and the fully
powered suit (the Ruins Test has to be won), which need a real fight. In the
gunship replay Samus falls through the ship after a hop and only gets in
once her y wraps around, about 4600 frames later; it is deterministic, but
not how a player gets there.

Next: the Wii U frontend.

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
