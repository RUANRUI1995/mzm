# PC reference build (Linux and Windows)

A build of the port for 32-bit x86 Linux and Windows, used as a **reference**:
it runs the same game code as the 3DS port, renders with the same software PPU
(`port/ppu`), and is deterministic, so two builds fed the same input can be
compared frame by frame. It exists for the Wii U port (issue #39): a big-endian
build is checked against this one long before Cemu or a console are involved.
The window, keyboard and controller go through SDL2, which a Wii U frontend
can use too.

There is no audio (`PORT_NATIVE_AUDIO_STUBS`) and no 3DS feature (Wide view,
bottom screen, save states; `source/port_hooks_pc.c` stubs them as "off").

## Build

Both builds are made from Linux or WSL.

```sh
# once: 32-bit SDL2 and the MinGW cross-compiler
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install gcc-multilib libsdl2-dev:i386 gcc-mingw-w64-i686

cd platform/pc
./fetch_sdl2_mingw.sh            # once: SDL2 for MinGW, into ~/.cache/mzm
make -j                          # -> mzm-linux
make -j OS_TARGET=windows        # -> mzm-windows.exe + SDL2.dll
```

`fetch_sdl2_mingw.sh` downloads SDL2's MinGW package from SDL's GitHub
releases and checks it against `sdl2_mingw.sha256`.

## Play

```sh
./mzm-linux --rom ../../mzm_eu_baserom.gba          # on WSL the window shows through WSLg
mzm-windows.exe --rom ..\..\mzm_eu_baserom.gba      # SDL2.dll next to the .exe
```

Arrows, Z = A, X = B, A = L, S = R, Enter = START, Backspace or right Shift =
SELECT, hold Tab = fast forward, Esc = quit. A game controller works too.
`mzm.sav` is read and written in the current directory (`--save-dir` changes
it).

## Options

| Option | |
|---|---|
| `--rom PATH` | ROM to load (EU is what has been tried) |
| `--scale N` | window scale (default 3) |
| `--headless` | no window |
| `--window` | show the window even with `--replay` or `--frames` |
| `--frames N` | run N frames, then exit; implies `--headless` |
| `--test [N]` | `--frames N` (default 300), tapping A every 40 frames to get through the menus |
| `--replay FILE` | feed the input from FILE; runs to its last line unless `--frames` is given |
| `--record FILE` | write the input of this run as a replay |
| `--trace FILE` | one state line per frame (`-` = stdout) |
| `--shot N PATH` | write frame N as a PPM image (repeatable) |
| `--dump N DIR` | write frame N's raw GBA memory (`ewram_N.bin`, `io_N.bin`, ...) into DIR |
| `--save-dir DIR` | where `mzm.sav` lives; replay and trace runs default to a new empty directory, so they always start from a blank save |
| `--quiet` | no progress lines |

A frame is one VBlank. Without a display, the VCOUNT/DISPSTAT registers
advance one scanline per read instead of following a clock, so a run never
depends on timing. On a crash the program prints the return addresses; resolve
them with `addr2line -f -e mzm-linux <addresses>` (or the `.exe`).

## Replays

Plain text, one change per line; the keys hold until the next line:

```
# comment
0     -           no keys
120   START
124   -
1450  RIGHT+A     names joined by '+' (A B SELECT START RIGHT LEFT UP DOWN R L)
1500  0x011       or a hex GBA key mask
```

`replays/eu_new_game_walk.txt` starts a new game on the EU ROM and walks around
the first room. `--record` writes the same format, so a recorded run replays as
is.

## Traces

```
frame keys mode sub area room x y energy fc16 | fb vram bgpal objpal oam | ewram iwram io
```

- **values**: game variables read as numbers.
- **neutral hashes**: the rendered frame, VRAM, palettes and OAM. Palettes and
  OAM are hashed as `u16` values, the frame as pixel values, so these mean the
  same on a big-endian host. VRAM is hashed as bytes: it only matches across
  byte orders if the big-endian build keeps VRAM in GBA (little-endian) order,
  which is what the PPU reads.
- **host hashes**: raw EWRAM, IWRAM and I/O bytes (without the DMA address
  registers, which hold host pointers in the port). Their layout follows the
  host byte order, so they only compare builds of the same byte order.

```sh
make replay-check          # the same replay twice: the build is deterministic
python3 compare_traces.py expected.txt actual.txt [--cross-endian]
```

`compare_traces.py` prints the first frame and column that differ; with
`--cross-endian` it ignores the host hashes. `--dump` on that frame, in both
builds, shows which bytes.

**Linux vs Windows.** Both are little-endian x86, so the same replay gives
byte-identical traces, host hashes included. A difference means the platform
layer or the compiler changed what the game does. Finding the first ones fixed:
the BG and OBJ palettes not being one block (`port/port_gba_mem.c`), the
Windows heap sitting inside the GBA address ranges, and MinGW's Microsoft
bitfield layout.

## Memory layout notes

`port_resolve_addr()` reads any value in 0x02000000-0x0A000000 (and the SRAM
window) as a GBA address. So:

- the program image is linked at 0x20000000 (the i386 default, 0x08048000, is
  inside the ROM range);
- `main_pc.c` reserves the free parts of those ranges at startup, allocates the
  ROM buffer itself (`Platform_PC_AllocRom`; on Windows the C heap can sit at
  0x02000000), and refuses to run if a GBA-memory buffer, the ROM or the stack
  ended up inside them.
