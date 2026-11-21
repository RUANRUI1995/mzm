# PC reference build (Linux and Windows)

A build of the port for 32-bit x86 Linux and Windows, used as a **reference**:
it runs the same game code as the 3DS port, renders with the same software PPU
(`port/ppu`), and is deterministic, so two builds fed the same input can be
compared frame by frame. It exists for the Wii U port (issue #39): a big-endian
build is checked against this one long before Cemu or a console are involved.
The window, keyboard and controller go through SDL2, which a Wii U frontend
can use too.

The bottom screen is the 3DS one (`port/ui/port_bottom_ui.c`, shared), drawn
in software (`port/ui/port_draw2d_soft.c`). The sound engine is the C
translation of the assembly (`port/audio`, see below). None of the 3DS-only
features are here: Wide view, save states, RetroAchievements, the updater, FTP
(`source/port_hooks_pc.c` and `source/port_services_pc.c` answer for them as
"off" / "not available").

## Sound

The engine's assembly (`port/asm/audio_internal.s`, `asm/soundcode.s`) is ARM
code; the 3DS assembles it, the PC uses its C translation,
`port/audio/port_audio_engine.c`, written to give the same result bit for bit.
It runs as on the GBA, once per frame from the game loop, so it is part of the
deterministic run and of the trace (`snd`). `port/port_mzm_audio_glue.c` (shared
with the 3DS) adds the PSG channels and turns each mixdown into 16-bit stereo;
`source/audio_pc.c` queues it to SDL at the engine's rate. `--mute` keeps the
engine running without a device; `--wav PATH` records it.

The 3DS can check the C translation against the assembly while it plays:
`make DEBUG_TOOLS=1 AUDIO_LOCKSTEP=1` in `platform/3ds` wraps every routine,
runs both on the same memory and writes any difference to
`debug/audio-lockstep.txt` (`port/audio/port_audio_lockstep.c`).

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

### Wii U

The same sources with devkitPPC, wut and devkitPro's SDL2 for Wii U, built
from devkitPro's MSYS2 shell (issue #39, `docs/wiiu-big-endian-plan.md`):

```sh
dkp-pacman -S wut wut-tools wiiu-sdl2 wiiu-pkg-config   # once
make -j OS_TARGET=wiiu           # -> mzm-wiiu.rpx and mzm-wiiu.wuhb
```

On the SD card, `wiiu/apps/mzm/` holds the ROM (`mzm_eu.gba`), and the game
writes `mzm.sav` and `mzm-log.txt` (stdout and stderr, each line timed)
there. The game is on the TV and the bottom-screen UI on the GamePad, whose
touch screen is the stylus (`source/screens_pc.c`). A `mzm-replay.txt` in that
folder (a replay, with its `save` file next to it) drives the game instead of
the controls, on screen; its saves go to `replay-run/` and it writes
`mzm-trace.txt`, which compares with a PC trace of the same replay run with
`--bottom` (`compare_traces.py --cross-endian`).

Cemu runs either file (`Cemu.exe -g mzm-wiiu.rpx`; the SD card is Cemu's
`sdcard` folder). What differs from the PC builds: no address-range
reservation (wut puts only the code at 0x02000000, every data address is at
0x10000000 and above), the PPU renders on the three cores (a thread pinned to
cores 0 and 2 beside the main one on core 1, as the 3DS does with its cores)
with its per-thread line buffers as a copy per render thread, picked by the
stack pointer, since an RPX has no thread-local storage, the 64-bit atomics in `source/wiiu_support.c`, `-O2`,
and the TV window skips its buffer swap (`SDL_WINDOW_WIIU_PREVENT_SWAP`) so
that the two windows wait for one screen refresh, not two. The link goes
through a response file (Windows' command-line limit).

On Cemu its traces match x86 (the Kraid area and the Ridley fight replays,
every frame). Speed there: the game logic takes under 1 ms a frame and the
PPU most of the rest (the Ridley fight, the heaviest scene measured, 9-12 ms);
the intro, the title screen, the demos, the Kraid area and the Ridley fight
with its spawn cutscene all run at the GBA's 59.7 fps. Not measured on a
console yet.

## Play

```sh
./mzm-linux --rom ../../mzm_eu_baserom.gba          # on WSL the window shows through WSLg
mzm-windows.exe --rom ..\..\mzm_eu_baserom.gba      # SDL2.dll next to the .exe
```

Arrows, Z = A, X = B, A = L, S = R, Enter = START, Backspace or right Shift =
SELECT, hold Tab = fast forward, Esc = quit. A game controller works too.
`mzm.sav` is read and written in the current directory (`--save-dir` changes
it). It is the GBA's save file byte for byte, on any host (a big-endian one
converts it as it reads and writes it, `port/port_sram_swap.c`), so a save
from an emulator, a cartridge or the 3DS port can be copied in as it is.

### The second screen

F2 cycles how the game and the bottom screen are shown (`--layout` picks the
first one):

| Layout | |
|---|---|
| `stacked` | one window, the game above the bottom screen, like a 3DS (default) |
| `side` | one window, side by side |
| `two-windows` | a window each, like a Wii U's TV and GamePad |
| `game-only` | the game alone |

The mouse on the bottom screen is the stylus (click, drag). The tabs work as on
the 3DS; what needs a 3DS service (save states, achievements, updates) shows as
empty or does nothing, and the Options settings are kept in memory only.

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
| `--dump N DIR` | write frame N's raw GBA memory (`ewram_N.bin`, `io_N.bin`, ...), the sprites, sprite set and events into DIR |
| `--layout NAME` | first screen layout: `stacked`, `side`, `two-windows`, `game-only` |
| `--bottom` | run the bottom-screen UI even without a window |
| `--bottom-shot N PATH` | write frame N's bottom screen (320x240) as a PPM image (repeatable) |
| `--tap N X Y` | touch the bottom screen at (X, Y) on frame N, for three frames (repeatable) |
| `--save-dir DIR` | where `mzm.sav` lives; replay and trace runs default to a new empty directory, so they always start from a blank save |
| `--load-save FILE` | start from a copy of FILE as `mzm.sav` (in the run's save directory) |
| `--mute` | no sound device (the engine still runs) |
| `--wav PATH` | also write the sound to a WAV file |
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

A line `save FILE` makes the replay start from that save (a path relative to
the replay; the same as `--load-save`).

`replays/eu_new_game_walk.txt` starts a new game on the EU ROM and walks around
the first room; `replays/eu_title_demos.txt` leaves the title screen alone, so
the attract demos play; `replays/eu_pause_map.txt` adds the map and status
screens to the walk. `replays/eu_area_<area>.txt` resume a save in Kraid,
Norfair, Ridley, Tourian, Crateria and Chozodia and move around for a while;
`replays/eu_boss_<boss>.txt` fight Kraid, Ridley, Mother Brain and Mecha
Ridley; `replays/eu_cutscene_<name>.txt` go through a door or ride an elevator
that plays a cutscene (Kraid rising, before the Ruins Test, Mecha Ridley sees
Samus, the Mother Brain close-up, entering Tourian, Ridley landing, Ridley in
space); `eu_cutscene_escape_ship.txt` takes the escape ship to the credits and
the final screen, `eu_cutscene_gunship.txt` the gunship off Zebes. `--record`
writes the same format, so a recorded run replays as is.

### Saves for replays

`replays/saves/eu_new_game.sav` is what the walk leaves (file A, a new game).
The others were made from it with `tools/mzm_save.py`, which edits a save:

```sh
python3 tools/mzm_save.py show replays/saves/eu_kraid.sav
python3 tools/mzm_save.py doors --area ridley          # the area's doors and their rooms
python3 tools/mzm_save.py warp IN.sav OUT.sav --area ridley --door 29 --items all     --event GRAVITY_SUIT_OBTAINED
python3 tools/mzm_save.py warp IN.sav OUT.sav --area brinstar --door 57 --items all --y 639
```

`warp` makes file A resume in the room of a door, Samus where that door lets
her out (or at `--x`/`--y`: an elevator's door is the shaft under its pad), with
every item and the events asked for, and writes the checksums back. The rest
of the game state is a new game's (no events, nothing collected): bosses are
there, Ridley only with `GRAVITY_SUIT_OBTAINED`. It needs devkitARM (it reads
the save's layout from its gcc) and runs on the Windows side here.

## Traces

```
frame keys mode sub area room x y energy fc16 | fb vram bgpal objpal oam snd | ewram iwram io [| bottom]
```

- **values**: game variables read as numbers.
- **neutral hashes**: the rendered frame, VRAM, palettes, OAM and the sound
  (`snd`: the 8-bit PCM buffers the engine mixed into, `gMusicInfo.soundRawData`).
  VRAM, palettes and OAM are hashed as `u16` values (what they hold on any
  host, see `port/port_gba_mem.h`), the frame as pixel values, the sound as
  bytes, so these mean the same on a big-endian host.
- **host hashes**: raw EWRAM, IWRAM and I/O bytes (without the DMA address
  registers, which hold host pointers in the port). Their layout follows the
  host byte order, so they only compare builds of the same byte order.
- **bottom**: the 320x240 bottom screen, as pixel values, only on runs where
  the bottom-screen UI runs (see below).

```sh
make replay-check          # the same replay twice: the build is deterministic
python3 compare_traces.py expected.txt actual.txt [--cross-endian]
```

The bottom-screen UI only runs when something shows it (a layout with it,
`--bottom`, `--bottom-shot`): drawing the map reads game data, so a trace with
it on is not comparable to one with it off. For example, the five tabs, by
tapping the tab bar (icons centred at x = 19, 53, 87, 121, 155, y = 13):

```sh
./mzm-linux --replay replays/eu_new_game_walk.txt --frames 1700 --trace t.txt \
  --tap 1600 53 13 --bottom-shot 1610 status.ppm --tap 1620 87 13 --bottom-shot 1630 states.ppm
```

A run fed by a replay or writing a trace takes the UI's clock (12:00 at the
first frame), timer and battery from the frame count, so its bottom screen is
the same at any hour and on any host. The Options footer prints the commit
(`PC-<hash>`), so traces of two different commits differ from the frame that
tab opens.

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
