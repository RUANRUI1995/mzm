/* PC reference build (Linux and Windows): command line, ROM loading, crash
 * reports and the GBA address-range reservation. See ../README.md. */
#include "platform_pc.h"
#include "platform_pc_options.h"
#include "port_trace_pc.h"
#include "port_rom.h"
#include "port_gba_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#define chdir _chdir
#define getcwd _getcwd
#else
#include <execinfo.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern void agbmain(void);

PcOptions gPcOptions;

/* ---- GBA address ranges ------------------------------------------------------
 * port_resolve_addr() treats a value in 0x02000000-0x0A000000 or the SRAM
 * window as a GBA address to translate. A host allocation landing there would
 * be translated as if it were one (the program image is linked at 0x20000000
 * for the same reason, see the Makefile). Reserve both windows before anything
 * else is allocated, so the OS never hands them out. On Linux the default
 * layout keeps them free anyway. On 32-bit Windows the heap grows into them
 * (the ROM buffer landed on 0x0384A020-0x0404A020, over the I/O registers),
 * and the system already maps a view at 0x02000000 before main() runs, so
 * the free parts are reserved one by one. CheckHostBuffers() then verifies
 * the buffers the game does reach stayed out. */
static const struct { uintptr_t base, size; } kGbaRanges[] = {
    { 0x02000000u, 0x08000000u }, /* EWRAM .. ROM mirrors */
    { 0x0E000000u, 0x00010000u }, /* SRAM */
};

static void ReserveGbaRanges(void) {
    size_t i;
    for (i = 0; i < sizeof(kGbaRanges) / sizeof(kGbaRanges[0]); ++i) {
        const uintptr_t end = kGbaRanges[i].base + kGbaRanges[i].size;
#ifdef _WIN32
        uintptr_t addr = kGbaRanges[i].base;
        while (addr < end) {
            MEMORY_BASIC_INFORMATION mbi;
            uintptr_t regionEnd;
            if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi))) break;
            regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            if (regionEnd > end) regionEnd = end;
            if (mbi.State == MEM_FREE)
                VirtualAlloc((void*)addr, regionEnd - addr, MEM_RESERVE, PAGE_NOACCESS);
            addr = regionEnd;
        }
#else
        void* p = mmap((void*)kGbaRanges[i].base, kGbaRanges[i].size, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != (void*)kGbaRanges[i].base)
            fprintf(stderr, "[PC] Warning: could not reserve 0x%08lx-0x%08lx\n",
                    (unsigned long)kGbaRanges[i].base, (unsigned long)end);
#endif
    }
}

/* The ROM buffer (port_rom.c). On Windows the C heap is the process heap,
 * which the loader may have put at 0x02000000; allocating from the top of the
 * address space keeps the ROM out of the GBA ranges. */
void* Platform_PC_AllocRom(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
#else
    return malloc(size);
#endif
}

void Platform_PC_FreeRom(void* mem) {
#ifdef _WIN32
    VirtualFree(mem, 0, MEM_RELEASE);
#else
    free(mem);
#endif
}

static bool OverlapsGbaRange(const void* p, size_t size) {
    const uintptr_t lo = (uintptr_t)p, hi = lo + size;
    size_t i;
    for (i = 0; i < sizeof(kGbaRanges) / sizeof(kGbaRanges[0]); ++i)
        if (lo < kGbaRanges[i].base + kGbaRanges[i].size && hi > kGbaRanges[i].base) return true;
    return false;
}

/* The buffers that stand in for GBA memory and the ROM must not sit at
 * addresses the resolver reads as GBA addresses: refuse to run if one does. */
static bool CheckHostBuffers(void) {
    int stackProbe;
    const struct { const char* name; const void* p; size_t size; } kBuffers[] = {
        { "stack", &stackProbe, sizeof(stackProbe) },
        { "ROM", gRomData, gRomSize },        { "EWRAM", gEwram, sizeof(gEwram) },
        { "IWRAM", gIwram, sizeof(gIwram) },  { "I/O", gIoMem, sizeof(gIoMem) },
        { "VRAM", gVram, sizeof(gVram) },     { "SRAM", gSramMem, sizeof(gSramMem) },
    };
    size_t i;
    bool ok = true;
    for (i = 0; i < sizeof(kBuffers) / sizeof(kBuffers[0]); ++i) {
        if (OverlapsGbaRange(kBuffers[i].p, kBuffers[i].size)) {
            fprintf(stderr, "FATAL: the %s buffer is at %p, inside a GBA address range\n", kBuffers[i].name,
                    kBuffers[i].p);
            ok = false;
        }
    }
    return ok;
}

/* ---- Crash reports -------------------------------------------------------------
 * Resolve the printed addresses with: addr2line -f -e <executable> <addresses> */
#ifdef _WIN32
static LONG WINAPI HandleCrash(EXCEPTION_POINTERS* info) {
    const CONTEXT* c = info->ContextRecord;
    const DWORD* frame = (const DWORD*)c->Ebp;
    int depth;
    fprintf(stderr, "\n[PC] Fatal exception 0x%08lx at frame %u, eip 0x%08lx. Return addresses:\n",
            (unsigned long)info->ExceptionRecord->ExceptionCode, (unsigned)Platform_PC_CurrentFrame(),
            (unsigned long)c->Eip);
    /* Walk saved frame pointers; stops at the first one that does not look valid. */
    for (depth = 0; depth < 32 && frame && !IsBadReadPtr(frame, 8); ++depth) {
        fprintf(stderr, "  0x%08lx\n", (unsigned long)frame[1]);
        if ((const DWORD*)frame[0] <= frame) break;
        frame = (const DWORD*)frame[0];
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void InstallCrashHandler(void) {
    SetUnhandledExceptionFilter(HandleCrash);
}
#else
static void HandleCrash(int sig) {
    void* frames[64];
    int n = backtrace(frames, 64);
    fprintf(stderr, "\n[PC] Fatal signal %d at frame %u. Backtrace:\n", sig, (unsigned)Platform_PC_CurrentFrame());
    backtrace_symbols_fd(frames, n, 2);
    _exit(128 + sig);
}

static void InstallCrashHandler(void) {
    signal(SIGSEGV, HandleCrash);
    signal(SIGBUS, HandleCrash);
    signal(SIGFPE, HandleCrash);
    signal(SIGILL, HandleCrash);
    signal(SIGABRT, HandleCrash);
}
#endif

static void HandleInterrupt(int sig) {
    (void)sig;
    Platform_PC_Cleanup();
    exit(0);
}

/* ---- Paths ----------------------------------------------------------------------- */

static bool IsAbsolutePath(const char* p) {
#ifdef _WIN32
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
#else
    return p[0] == '/';
#endif
}

/* Output paths given relative to where the program was started, since a
 * replay run moves into its own save directory (see SaveDirForRun). */
static const char* Absolute(const char* path) {
    char cwd[1024];
    size_t len;
    char* out;
    if (IsAbsolutePath(path) || !getcwd(cwd, sizeof(cwd))) return path;
    len = strlen(cwd) + 1 + strlen(path) + 1;
    out = (char*)malloc(len);
    snprintf(out, len, "%s/%s", cwd, path);
    return out;
}

static bool MakeDir(const char* path) {
#ifdef _WIN32
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0777) == 0;
#endif
}

/* A replay or trace run must not depend on whatever mzm.sav happens to be in
 * the current directory, nor leave one behind: it gets an empty directory of
 * its own, so the game starts from a blank save. --save-dir overrides it. */
static const char* SaveDirForRun(void) {
    static char dir[1024];
    const char* base;
    unsigned attempt;
#ifdef _WIN32
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA(sizeof(tmp), tmp);
    base = n ? tmp : ".";
    for (attempt = 0; attempt < 1000; ++attempt) {
        snprintf(dir, sizeof(dir), "%smzm-pc-%lu-%u", base, (unsigned long)GetCurrentProcessId(), attempt);
        if (MakeDir(dir)) return dir;
    }
#else
    base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    for (attempt = 0; attempt < 1000; ++attempt) {
        snprintf(dir, sizeof(dir), "%s/mzm-pc-%ld-%u", base, (long)getpid(), attempt);
        if (MakeDir(dir)) return dir;
    }
#endif
    return NULL;
}

/* ---- Command line ------------------------------------------------------------------ */

static void Usage(const char* argv0) {
    printf("Usage: %s [options]\n"
           "  --rom PATH            ROM to load (default: mzm_eu.gba, then baserom.gba)\n"
           "  --headless            no window\n"
           "  --scale N             window scale (default 3)\n"
           "  --frames N            run N frames, then exit (implies --headless)\n"
           "  --test [N]            --frames N (default 300); taps A to get through menus\n"
           "                        unless --replay is given\n"
           "  --replay FILE         feed the input from FILE (implies --headless unless\n"
           "                        --window; runs to its last line unless --frames is given)\n"
           "  --window              show the window even with --replay or --frames\n"
           "  --record FILE         write the input of this run as a replay\n"
           "  --trace FILE          write one state line per frame ('-' = stdout)\n"
           "  --shot N PATH         write frame N as a PPM image (repeatable)\n"
           "  --dump N DIR          write frame N's raw GBA memory into DIR (repeatable)\n"
           "  --save-dir DIR        directory for mzm.sav (replay and trace runs default\n"
           "                        to a new empty one, so they start from a blank save)\n"
           "  --quiet               no progress lines\n"
           "See platform/pc/README.md.\n", argv0);
}

static bool ParseUInt(const char* s, uint32_t* out) {
    char* end;
    unsigned long v = strtoul(s, &end, 10);
    if (*s == '\0' || *end != '\0') return false;
    *out = (uint32_t)v;
    return true;
}

int main(int argc, char** argv) {
    const char* romPath = "mzm_eu.gba";
    const char* replayPath = NULL;
    const char* recordPath = NULL;
    const char* tracePath = NULL;
    const char* saveDir = NULL;
    bool headless = false;
    bool forceWindow = false;
    bool framesGiven = false;
    bool testMode = false;
    int i;

    ReserveGbaRanges();
    InstallCrashHandler();
    signal(SIGINT, HandleInterrupt);
    signal(SIGTERM, HandleInterrupt);
    gPcOptions.scale = 3;

    for (i = 1; i < argc; i++) {
        const char* a = argv[i];
        bool hasValue = i + 1 < argc;
        if (strcmp(a, "--test") == 0) {
            testMode = true;
            headless = true;
            gPcOptions.maxFrames = 300;
            framesGiven = true;
            if (hasValue && argv[i + 1][0] != '-') ParseUInt(argv[++i], &gPcOptions.maxFrames);
        } else if (strcmp(a, "--frames") == 0 && hasValue) {
            if (!ParseUInt(argv[++i], &gPcOptions.maxFrames)) { Usage(argv[0]); return 2; }
            framesGiven = true;
            headless = true;
        } else if (strcmp(a, "--headless") == 0) {
            headless = true;
        } else if (strcmp(a, "--window") == 0) {
            forceWindow = true;
        } else if (strcmp(a, "--scale") == 0 && hasValue) {
            uint32_t s;
            if (!ParseUInt(argv[++i], &s) || s < 1 || s > 10) { Usage(argv[0]); return 2; }
            gPcOptions.scale = (int)s;
        } else if (strcmp(a, "--rom") == 0 && hasValue) {
            romPath = argv[++i];
        } else if (strcmp(a, "--replay") == 0 && hasValue) {
            replayPath = argv[++i];
            headless = true;
        } else if (strcmp(a, "--record") == 0 && hasValue) {
            recordPath = argv[++i];
        } else if (strcmp(a, "--trace") == 0 && hasValue) {
            tracePath = argv[++i];
        } else if (strcmp(a, "--save-dir") == 0 && hasValue) {
            saveDir = argv[++i];
        } else if (strcmp(a, "--shot") == 0 && i + 2 < argc) {
            if (gPcOptions.shotCount >= PC_MAX_SHOTS ||
                !ParseUInt(argv[i + 1], &gPcOptions.shots[gPcOptions.shotCount].frame)) {
                Usage(argv[0]);
                return 2;
            }
            gPcOptions.shots[gPcOptions.shotCount].path = Absolute(argv[i + 2]);
            gPcOptions.shotCount++;
            i += 2;
        } else if (strcmp(a, "--dump") == 0 && i + 2 < argc) {
            if (gPcOptions.dumpCount >= PC_MAX_SHOTS ||
                !ParseUInt(argv[i + 1], &gPcOptions.dumps[gPcOptions.dumpCount].frame)) {
                Usage(argv[0]);
                return 2;
            }
            gPcOptions.dumps[gPcOptions.dumpCount].dir = Absolute(argv[i + 2]);
            gPcOptions.dumpCount++;
            i += 2;
        } else if (strcmp(a, "--quiet") == 0) {
            gPcOptions.quiet = true;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            Usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option '%s'\n", a);
            Usage(argv[0]);
            return 2;
        }
    }
    if (forceWindow) headless = false;

    if (replayPath) {
        if (!Trace_LoadReplay(replayPath)) return 2;
        if (!framesGiven) gPcOptions.maxFrames = Trace_ReplayLength();
    }
    gPcOptions.pulseA = testMode && !replayPath;
    if (recordPath && !Trace_OpenRecord(recordPath)) return 2;
    if (tracePath && !Trace_OpenLog(tracePath)) return 2;
    /* With the trace on stdout, keep everything else off it. */
    if (tracePath && strcmp(tracePath, "-") == 0) gPcOptions.quiet = true;
    FILE* info = gPcOptions.quiet ? stderr : stdout;

    /* 1. Load the ROM */
    {
        const char* candidatePaths[] = { romPath, "mzm_eu.gba", "baserom.gba", "../mzm_eu.gba", "../../mzm_eu.gba", NULL };
        bool romLoaded = false;
        for (i = 0; candidatePaths[i] != NULL; i++) {
            FILE* f = fopen(candidatePaths[i], "rb");
            if (f) {
                fclose(f);
                fprintf(info, "[PC] Loading ROM from '%s'...\n", candidatePaths[i]);
                if (Port_LoadRom(candidatePaths[i])) {
                    romLoaded = true;
                    break;
                }
            }
        }
        if (!romLoaded) {
            fprintf(stderr, "FATAL: Could not load Metroid Zero Mission ROM (tried %s, baserom.gba).\n", romPath);
            return 1;
        }
        fprintf(info, "[PC] ROM loaded (region %d, %u bytes at %p; EWRAM at %p)\n", gRomRegion,
                (unsigned int)gRomSize, (void*)gRomData, (void*)gEwram);
        if (!CheckHostBuffers()) return 1;
    }

    /* 2. Where mzm.sav lives (the game opens it relative to the current directory) */
    if (!saveDir && (replayPath || tracePath)) {
        saveDir = SaveDirForRun();
        if (!saveDir) {
            fprintf(stderr, "FATAL: could not create a save directory for this run\n");
            return 1;
        }
    }
    if (saveDir) {
        if (chdir(saveDir) != 0) {
            fprintf(stderr, "FATAL: cannot enter save directory '%s'\n", saveDir);
            return 1;
        }
        fprintf(info, "[PC] Save directory: %s\n", saveDir);
    }

    /* 3. Platform */
    if (!Platform_PC_Init(headless)) return 1;

    /* 4. Run; the platform layer exits once --frames is reached. */
    agbmain();

    Platform_PC_Cleanup();
    return 0;
}
