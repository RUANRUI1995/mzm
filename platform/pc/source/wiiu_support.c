/* Wii U pieces of the PC build (OS_TARGET=wiiu): where the ROM and mzm.sav
 * live, and stdout/stderr on the system console, which Cemu shows in its log
 * (Debug -> Logging -> Coreinit Logging) and a console sends to its debug
 * output. See wiiu_support.h. */
#ifdef __WIIU__
#include "wiiu_support.h"

#include <coreinit/debug.h>
#include <coreinit/spinlock.h>
#include <coreinit/time.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <unistd.h>

/* A copy of the output in the SD card's folder (WIIU_LOG_FILE), each line
 * with the seconds since start, so a console run can be read afterwards. */
static FILE* sLogFile;
static OSTime sStartTime;
static bool sAtLineStart = true;

static ssize_t ConsoleWrite(struct _reent* r, void* fd, const char* ptr, size_t len) {
    size_t i;
    (void)r;
    (void)fd;
    OSConsoleWrite(ptr, (uint32_t)len);
    if (sLogFile) {
        for (i = 0; i < len; ++i) {
            if (sAtLineStart) {
                OSTime ms = OSTicksToMilliseconds(OSGetTime() - sStartTime);
                fprintf(sLogFile, "[%5u.%03u] ", (unsigned)(ms / 1000), (unsigned)(ms % 1000));
            }
            fputc(ptr[i], sLogFile);
            sAtLineStart = ptr[i] == '\n';
        }
        fflush(sLogFile);
    }
    return (ssize_t)len;
}

static const devoptab_t kConsoleDevoptab = {
    .name = "console",
    .write_r = ConsoleWrite,
};

static void RedirectConsole(void) {
    devoptab_list[STD_OUT] = &kConsoleDevoptab;
    devoptab_list[STD_ERR] = &kConsoleDevoptab;
}

/* mkdir -p, for the SD card's folder. */
static void MakePath(const char* path) {
    char buf[256];
    size_t i, n = strlen(path);
    if (n >= sizeof(buf)) return;
    memcpy(buf, path, n + 1);
    for (i = 1; i <= n; ++i) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char c = buf[i];
            buf[i] = '\0';
            if (strchr(buf, '/') && mkdir(buf, 0777) != 0 && errno != EEXIST) { /* the volume itself fails: fine */ }
            buf[i] = c;
        }
    }
}

/* 64-bit atomics (a timestamp in port/ui/port_bottom_ui.c): the 750 has no
 * 64-bit atomic instructions and devkitPPC no libatomic, so gcc's calls land
 * here, behind one lock shared by the three cores. */
static OSSpinLock sAtomicLock;
static int sAtomicLockReady;

static void AtomicLock(void) {
    if (!sAtomicLockReady) {
        OSInitSpinLock(&sAtomicLock);
        sAtomicLockReady = 1;
    }
    OSUninterruptibleSpinLock_Acquire(&sAtomicLock);
}

static void AtomicUnlock(void) {
    OSUninterruptibleSpinLock_Release(&sAtomicLock);
}

uint64_t __atomic_load_8(const volatile void* p, int order) {
    uint64_t v;
    (void)order;
    AtomicLock();
    v = *(const volatile uint64_t*)p;
    AtomicUnlock();
    return v;
}

void __atomic_store_8(volatile void* p, uint64_t v, int order) {
    (void)order;
    AtomicLock();
    *(volatile uint64_t*)p = v;
    AtomicUnlock();
}

bool __atomic_compare_exchange_8(volatile void* p, void* expected, uint64_t desired, bool weak, int success,
                                 int failure) {
    bool ok;
    (void)weak;
    (void)success;
    (void)failure;
    AtomicLock();
    ok = *(volatile uint64_t*)p == *(uint64_t*)expected;
    if (ok)
        *(volatile uint64_t*)p = desired;
    else
        *(uint64_t*)expected = *(volatile uint64_t*)p;
    AtomicUnlock();
    return ok;
}

const char* WiiU_Init(void) {
    RedirectConsole();
    AtomicLock(); /* initialised here, before any thread starts */
    AtomicUnlock();
    sStartTime = OSGetTime();
    MakePath(WIIU_DATA_DIR);
    if (chdir(WIIU_DATA_DIR) != 0) return NULL;
    sLogFile = fopen(WIIU_LOG_FILE, "w");
    return WIIU_DATA_DIR;
}
#endif /* __WIIU__ */
