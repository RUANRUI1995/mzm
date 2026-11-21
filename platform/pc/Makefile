# PC reference build of the Metroid: Zero Mission port (32-bit), for Linux
# and Windows on x86, and headless big-endian PowerPC. See README.md.
#
#   make                         Linux build  -> mzm-linux
#   make OS_TARGET=windows       Windows build (MinGW cross-compiler, from
#                                Linux/WSL)   -> mzm-windows.exe + SDL2.dll
#   make OS_TARGET=linux-ppc     PowerPC build, run with qemu-ppc -> mzm-linux-ppc
#   make OS_TARGET=wiiu          Wii U build (devkitPPC + wut + SDL2 for Wii U, from
#                                devkitPro's MSYS2 shell) -> mzm-wiiu.rpx, mzm-wiiu.wuhb
#
# Deliberately 32-bit: several structs (e.g. struct SoundEntry in
# include/structs/audio.h) are overlaid directly on raw GBA ROM bytes and
# contain pointer-typed fields sized for a 32-bit target (GBA/3DS ARM are
# both 32-bit). On a 64-bit build those fields double in size and desync
# the whole struct layout from the ROM. Building 32-bit keeps pointer width
# consistent with the ROM layout, matching the 3DS (ARM32) build.

OS_TARGET ?= linux
ROOT ?= $(abspath $(CURDIR)/../..)

ifeq ($(OS_TARGET),windows)
  # The -posix variant: src/sram/sram.c saves from a pthread.
  CC := i686-w64-mingw32-gcc-posix
  TARGET := mzm-windows.exe
  BUILD := $(CURDIR)/build/windows
  # SDL2's MinGW development package (fetch_sdl2_mingw.sh downloads it).
  SDL2_VERSION ?= 2.30.12
  SDL2_MINGW ?= $(HOME)/.cache/mzm/SDL2-$(SDL2_VERSION)/i686-w64-mingw32
  SDL_CFLAGS := -I$(SDL2_MINGW)/include/SDL2
  SDL_LIBS := -L$(SDL2_MINGW)/lib -lSDL2
  # -mno-ms-bitfields: MinGW defaults to the Microsoft bitfield layout, which
  # packs the OAM attribute structs (include/oam.h) differently from GCC on
  # Linux, the 3DS and the GBA (sprite priorities came out wrong).
  ARCH_FLAGS := -mno-ms-bitfields
  # i686 Windows C symbols carry a leading underscore.
  SYM_PREFIX := _
  # The image base must stay out of the GBA address ranges, as on Linux.
  # -mconsole keeps stdout/stderr working.
  # winpthreads is linked statically, so SDL2.dll is the only DLL to ship.
  # --allow-multiple-definition: see the port/generated rule below.
  # --disable-dynamicbase: with ASLR (MinGW's default) Windows relocated the
  # image below 0x20000000 and the heap moved around between runs.
  OS_LDFLAGS := -mconsole -Wl,--image-base=0x20000000 -Wl,--disable-dynamicbase -static-libgcc \
                -Wl,-Bstatic -lpthread -Wl,-Bdynamic -Wl,--allow-multiple-definition
else ifeq ($(OS_TARGET),linux)
  CC ?= gcc
  TARGET := mzm-linux
  BUILD := $(CURDIR)/build/linux
  # The i386 SDL2 (libsdl2-dev:i386); its sdl2-config is the 64-bit one, so
  # the flags are spelled out.
  SDL_CFLAGS := -I/usr/include/SDL2 -I/usr/include/i386-linux-gnu/SDL2 -D_REENTRANT
  SDL_LIBS := -L/usr/lib/i386-linux-gnu -lSDL2
  ARCH_FLAGS := -m32
  SYM_PREFIX :=
  # -Ttext-segment: the default i386 image base, 0x08048000, sits inside the
  # GBA ROM range (0x08000000-0x09FFFFFF), so port_resolve_addr() took the
  # program's own data pointers (e.g. the scroll tables built by
  # Init_sScrollTables) for GBA ROM addresses and translated them into
  # garbage. 0x20000000 is above every GBA region.
  OS_LDFLAGS := -no-pie -Wl,-Ttext-segment=0x20000000 -lrt -lpthread
else ifeq ($(OS_TARGET),linux-ppc)
  # 32-bit big-endian PowerPC, the Wii U's byte order, run under qemu-ppc
  # (qemu-user). Headless only: no SDL, frontend_none.c. Static, so qemu needs
  # no PowerPC libraries.
  CC := powerpc-linux-gnu-gcc
  TARGET := mzm-linux-ppc
  BUILD := $(CURDIR)/build/linux-ppc
  NO_SDL := 1
  ARCH_FLAGS :=
  SYM_PREFIX :=
  # -latomic: 64-bit __atomic operations (port/ui/port_bottom_ui.c) are library
  # calls on 32-bit PowerPC.
  OS_LDFLAGS := -static -Wl,-Ttext-segment=0x20000000 -lrt -lpthread -latomic
else ifeq ($(OS_TARGET),wiiu)
  # The Wii U's Espresso: 32-bit big-endian PowerPC, as linux-ppc, with
  # devkitPPC, wut and devkitPro's SDL2 for Wii U (the packages wut, wut-tools,
  # wiiu-sdl2). wut's linker script puts the code at 0x02000000, inside the
  # GBA ranges, but only the code: data, .rodata, the stacks and the heap are
  # at 0x10000000 and above, so no data pointer looks like a GBA address.
  DEVKITPRO ?= /opt/devkitpro
  DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
  WUT_ROOT ?= $(DEVKITPRO)/wut
  export DEVKITPRO
  CC := $(DEVKITPPC)/bin/powerpc-eabi-gcc
  ELF := mzm-wiiu.elf
  TARGET := mzm-wiiu.rpx
  WUHB := mzm-wiiu.wuhb
  BUILD := $(CURDIR)/build/wiiu
  ARCH_FLAGS := -DESPRESSO -mcpu=750 -meabi -mhard-float -D__WIIU__ -D__WUT__ -I$(WUT_ROOT)/include
  OPT_FLAGS := -O2
  SDL_CFLAGS := -I$(DEVKITPRO)/portlibs/wiiu/include/SDL2 -I$(DEVKITPRO)/portlibs/wiiu/include -I$(WUT_ROOT)/include
  SDL_LIBS := -L$(DEVKITPRO)/portlibs/wiiu/lib -lSDL2 -L$(WUT_ROOT)/lib -lwut
  SYM_PREFIX :=
  OS_LDFLAGS := -specs=$(WUT_ROOT)/share/wut.specs
  OS_SRCS := $(CURDIR)/source/wiiu_support.c
else
  $(error OS_TARGET must be linux, windows, linux-ppc or wiiu)
endif

ifeq ($(NO_SDL),1)
  SDL_CFLAGS := -DPC_NO_SDL
  SDL_LIBS :=
  FRONTEND_SRCS := $(CURDIR)/source/frontend_none.c
else
  FRONTEND_SRCS := $(CURDIR)/source/frontend_sdl.c $(CURDIR)/source/screens_pc.c
endif

# No -std: the compiler's default dialect, as in the 3DS build (the port
# sources use C99+ features). Warning suppressions mirror the 3DS ones.
# -O1 on the reference builds (the traces were made with it); the Wii U, which
# has to keep up with the screen, sets OPT_FLAGS.
OPT_FLAGS ?= -O1
CFLAGS := $(ARCH_FLAGS) $(OPT_FLAGS) -g -fcommon -fno-omit-frame-pointer \
	-Wno-return-mismatch -Wno-int-conversion -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
	-Wno-maybe-uninitialized -Wno-misleading-indentation -Wno-pointer-sign -Wno-parentheses \
	-Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
	-Wno-format-overflow -Wno-array-bounds -Wno-address-of-packed-member \
	-Wno-comment -Wno-strict-aliasing -Wno-implicit-function-declaration \
	-Wno-implicit-int -Wno-builtin-declaration-mismatch -Wno-discarded-qualifiers

# Shown by the bottom-screen UI (Options footer); the 3DS derives it from the
# release branch, here the git description is enough.
PC_VERSION := $(shell git -C $(ROOT) rev-parse --short HEAD 2>/dev/null || echo unknown)

CPPFLAGS := \
	-DMZM_PORT_VERSION=\"PC-$(PC_VERSION)\" \
	-DREGION_EU \
	-DPORT_NATIVE \
	-DNON_MATCHING=1 \
	-DPLATFORM_PC \
	-DPORT_ROM_SWAP \
	-DMZM_3DS \
	-I$(ROOT) \
	-I$(ROOT)/port \
	-I$(ROOT)/port/generated/shadow \
	-I$(ROOT)/port/generated \
	-I$(ROOT)/include \
	-I$(ROOT)/port/ppu/include \
	-I$(ROOT)/port/ui \
	-I$(CURDIR)/source

# Extra EWRAM-offset symbols (see ewram_symbols.ld) injected additively via
# --defsym instead of -T, since a bare -T script (no SECTIONS/INSERT) replaces
# the default linker script wholesale.
EWRAM_DEFSYMS := $(shell sed -n 's/^\([A-Za-z0-9_]*\) = \([A-Za-z0-9_]*\)\(.*\);$$/-Wl,--defsym,$(SYM_PREFIX)\1=$(SYM_PREFIX)\2\3/p' $(CURDIR)/ewram_symbols.ld | tr -d ' ')

LDFLAGS := $(OS_LDFLAGS) $(EWRAM_DEFSYMS) $(SDL_LIBS) -lm

# C sources
SRCS_ROOT := \
	$(wildcard $(ROOT)/src/*.c) \
	$(wildcard $(ROOT)/src/cutscenes/*.c) \
	$(wildcard $(ROOT)/src/menus/*.c) \
	$(wildcard $(ROOT)/src/sprites_ai/*.c) \
	$(wildcard $(ROOT)/src/sram/*.c) \
	$(ROOT)/src/data/shortcut_pointers.c \
	$(ROOT)/src/data/music_track_data.c \
	$(ROOT)/src/data/audio.c \
	$(ROOT)/src/data/color_fading_data.c \
	$(ROOT)/src/data/cutscenes/cutscenes_data.c \
	$(ROOT)/src/data/in_game_cutscene_data.c \
	$(ROOT)/src/data/empty_datatypes.c \
	$(ROOT)/port/port_gba_symbols.c \
	$(ROOT)/port/port_rom.c \
	$(ROOT)/port/port_gba_mem.c \
	$(ROOT)/port/port_sram_legacy.c \
	$(ROOT)/port/port_bios.c \
	$(ROOT)/port/port_rom_swap.c \
	$(ROOT)/port/port_sram_swap.c \
	$(wildcard $(ROOT)/port/generated/byteswap/*.c) \
	$(ROOT)/port/port_mzm_audio_glue.c \
	$(ROOT)/port/port_psg_synth.c \
	$(ROOT)/port/audio/port_audio_engine.c \
	$(ROOT)/port/audio/port_audio_bind.c \
	$(ROOT)/port/port_debug_log.c \
	$(ROOT)/port/port_debug_files.c \
	$(ROOT)/port/port_constructor_init.c \
	$(ROOT)/port/ppu/src/virtuappu.c \
	$(ROOT)/port/ppu/src/mode1.c \
	$(filter-out %_selftest.c %_test.c, $(wildcard $(ROOT)/port/generated/*.c))

# The bottom-screen UI shared with the 3DS (port/ui). It declares the 3DS
# services it uses through platform/3ds/source headers (plain C); the PC
# versions are in source/port_services_pc.c.
SRCS_UI := \
	$(ROOT)/port/ui/port_bottom_ui.c \
	$(ROOT)/port/ui/port_ui_game.c \
	$(ROOT)/port/ui/port_draw2d_soft.c

SRCS_PC := \
	$(CURDIR)/source/main_pc.c \
	$(CURDIR)/source/platform_pc.c \
	$(CURDIR)/source/port_hooks_pc.c \
	$(CURDIR)/source/port_services_pc.c \
	$(FRONTEND_SRCS) \
	$(CURDIR)/source/audio_pc.c \
	$(CURDIR)/source/port_trace_pc.c \
	$(OS_SRCS)

OBJS_ROOT := $(patsubst $(ROOT)/%.c, $(BUILD)/root/%.o, $(SRCS_ROOT))
OBJS_UI   := $(patsubst $(ROOT)/%.c, $(BUILD)/ui/%.o, $(SRCS_UI))
OBJS_PC   := $(patsubst $(CURDIR)/%.c, $(BUILD)/pc/%.o, $(SRCS_PC))
OBJS := $(OBJS_ROOT) $(OBJS_UI) $(OBJS_PC)

UNIT_TEST_TARGET := port_gba_mem_test

.PHONY: all clean test test-unit test-psg replay-check

ifeq ($(OS_TARGET),wiiu)
all: $(TARGET) $(WUHB)

# The ELF, then the RPX the console loads, then a WUHB (the RPX bundled with
# its name, for the Aroma menu). Cemu runs either.
# The object list passes Windows' command-line limit: a response file, with
# paths relative to this directory (the native gcc reads no MSYS paths there).
$(ELF): $(OBJS)
	@echo "Linking $(OS_TARGET): $@"
	$(file >$(BUILD)/objects.rsp,$(patsubst $(CURDIR)/%,%,$^))
	@$(CC) $(CFLAGS) -o $@ @$(patsubst $(CURDIR)/%,%,$(BUILD))/objects.rsp $(LDFLAGS)

$(TARGET): $(ELF)
	@$(DEVKITPRO)/tools/bin/elf2rpl $< $@
	@echo "Build successful: $(CURDIR)/$@"

$(WUHB): $(TARGET)
	@$(DEVKITPRO)/tools/bin/wuhbtool $< $@ --name "Metroid: Zero Mission" --short-name "MZM" --author "mzm port"
	@echo "Build successful: $(CURDIR)/$@"
else
all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $(OS_TARGET): $@"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)
ifeq ($(OS_TARGET),windows)
	@cp $(SDL2_MINGW)/bin/SDL2.dll $(CURDIR)/SDL2.dll
endif
	@echo "Build successful: $(CURDIR)/$@"
endif

ifeq ($(OS_TARGET),windows)
# port/generated defines its ROM pointers as uninitialised weak globals.
# COFF turns a weak definition into a "weak external" that the MinGW linker
# does not resolve references from other objects against, so every one of
# them came out undefined. On Windows they are compiled as ordinary globals
# ("weak" expands to nothing in __attribute__((weak))). Three of them are
# defined twice (p_sDoorTransitionTilemap; p_sDma1ControlPointer/Value, also
# in src/data/audio.c), which ELF settles by weak symbol rules; the linker's
# --allow-multiple-definition does the same here (first definition wins, and
# src/ links before port/generated). The Linux build still rejects any real
# duplicate.
$(BUILD)/root/port/generated/%.o: CFLAGS += -fno-common -Dweak=
endif

$(BUILD)/root/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILD)/ui/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -I$(ROOT)/platform/3ds/source -c -o $@ $<

# The UI prints PC_VERSION: a stamp named after it rebuilds the UI when HEAD
# moves, so a binary never shows an older commit's hash.
VERSION_STAMP := $(BUILD)/version-$(PC_VERSION).stamp
$(VERSION_STAMP):
	@mkdir -p $(dir $@)
	@rm -f $(BUILD)/version-*.stamp
	@touch $@
$(BUILD)/ui/port/ui/port_bottom_ui.o: $(VERSION_STAMP)

$(BUILD)/pc/%.o: $(CURDIR)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) -I$(ROOT)/platform/3ds/source -c -o $@ $<

test: $(TARGET)
	@echo "Running headless automated test..."
	./$(TARGET) --test 300

# Runs a replay twice and checks both traces match (the build is
# deterministic). See README.md for comparing two different builds.
ROM ?= $(ROOT)/mzm_eu_baserom.gba
REPLAY ?= $(CURDIR)/replays/eu_new_game_walk.txt
replay-check: $(TARGET)
	@./$(TARGET) --rom $(ROM) --replay $(REPLAY) --trace $(BUILD)/trace-a.txt --quiet
	@./$(TARGET) --rom $(ROM) --replay $(REPLAY) --trace $(BUILD)/trace-b.txt --quiet
	@python3 $(CURDIR)/compare_traces.py $(BUILD)/trace-a.txt $(BUILD)/trace-b.txt

# Fast unit tests for the port translation layer (address resolvers, etc.),
# independent of the game engine -- no ROM needed. See
# port/port_gba_mem_test.c for what this covers and why.
$(BUILD)/$(UNIT_TEST_TARGET): $(ROOT)/port/port_gba_mem_test.c $(ROOT)/port/port_gba_mem.c
	@mkdir -p $(BUILD)
	@echo "CC (unit test) $@"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(EWRAM_DEFSYMS) -lm

test-unit: $(BUILD)/$(UNIT_TEST_TARGET)
	@echo "Running port translation layer unit tests..."
	@$(BUILD)/$(UNIT_TEST_TARGET)

# Unit tests for the software PSG synthesiser. Also ROM-free and hardware-free;
# see port/port_psg_synth_test.c for why testing this in isolation matters
# (every bug in it sounds identical from the outside: silence).
$(BUILD)/psg-synth-test: $(ROOT)/port/port_psg_synth_test.c $(ROOT)/port/port_psg_synth.c
	@mkdir -p $(BUILD)
	@echo "CC (psg test) $@"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ -lm

test-psg: $(BUILD)/psg-synth-test
	@echo "Running PSG synthesiser unit tests..."
	@$(BUILD)/psg-synth-test

clean:
	@rm -rf $(BUILD) $(TARGET) $(ELF) $(WUHB) $(if $(filter windows,$(OS_TARGET)),$(CURDIR)/SDL2.dll)
