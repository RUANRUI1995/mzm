/* Input replay/recording and the per-frame state trace (port_trace_pc.h).
 *
 * Replay file, plain text, one change per line, the keys hold until the next
 * line:
 *
 *     # comment
 *     0    -              no keys from frame 0
 *     120  START          START from frame 120
 *     124  -
 *     300  RIGHT+A        names joined by '+', or a hex mask (0x011)
 *
 * --record writes the same format, so a recorded run replays as is.
 *
 * Trace line, one per frame (VBlank), all values hexadecimal:
 *
 *     frame keys mode sub area room x y energy fc16 | fb vram bgpal objpal oam snd | ewram iwram io
 *
 * Everything before the second '|' is read as values, never as raw memory, so
 * it means the same on a big-endian build: that part is the cross-endian check.
 * The last group hashes raw bytes of memory whose layout follows the host's
 * byte order (game structs in EWRAM/IWRAM), so it only compares builds of the
 * same byte order. */
#include "port_trace_pc.h"

#include "types.h"
#include "gba.h"
#include "port_gba_mem.h"
#include "structs/game_state.h"
#include "structs/connection.h"
#include "structs/samus.h"
#include "structs/audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ---- Hashing (FNV-1a, 32-bit) -------------------------------------------- */

#define FNV_OFFSET 0x811C9DC5u
#define FNV_PRIME 0x01000193u

static uint32_t HashBytes(uint32_t h, const uint8_t* p, size_t n) {
    while (n--) {
        h ^= *p++;
        h *= FNV_PRIME;
    }
    return h;
}

static uint32_t HashByte(uint32_t h, uint8_t b) {
    h ^= b;
    return h * FNV_PRIME;
}

/* u16 values fed low byte first: the same hash on any host byte order. */
static uint32_t HashU16Values(const uint16_t* p, size_t count) {
    uint32_t h = FNV_OFFSET;
    while (count--) {
        uint16_t v = *p++;
        h = HashByte(h, (uint8_t)v);
        h = HashByte(h, (uint8_t)(v >> 8));
    }
    return h;
}

static uint32_t HashU32Values(const uint32_t* p, size_t count) {
    uint32_t h = FNV_OFFSET;
    while (count--) {
        uint32_t v = *p++;
        h = HashByte(h, (uint8_t)v);
        h = HashByte(h, (uint8_t)(v >> 8));
        h = HashByte(h, (uint8_t)(v >> 16));
        h = HashByte(h, (uint8_t)(v >> 24));
    }
    return h;
}

/* ---- Replay ---------------------------------------------------------------- */

typedef struct {
    uint32_t frame;
    uint16_t keys;
} ReplayEntry;

static ReplayEntry* sReplay;
static size_t sReplayCount;
static size_t sReplayCursor;

static const struct {
    const char* name;
    uint16_t mask;
} sKeyNames[] = {
    { "A", KEY_A }, { "B", KEY_B }, { "SELECT", KEY_SELECT }, { "START", KEY_START },
    { "RIGHT", KEY_RIGHT }, { "LEFT", KEY_LEFT }, { "UP", KEY_UP }, { "DOWN", KEY_DOWN },
    { "R", KEY_R }, { "L", KEY_L },
};

static bool ParseKeys(const char* text, uint16_t* out) {
    char buf[128];
    char* tok;
    char* next;
    uint16_t mask = 0;

    if (strcmp(text, "-") == 0) {
        *out = 0;
        return true;
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        char* end;
        unsigned long v = strtoul(text, &end, 16);
        if (*end != '\0' || v > 0x3FFu) return false;
        *out = (uint16_t)v;
        return true;
    }
    snprintf(buf, sizeof(buf), "%s", text);
    for (tok = buf; tok; tok = next) {
        size_t i;
        bool found = false;
        next = strchr(tok, '+');
        if (next) *next++ = '\0';
        for (i = 0; i < sizeof(sKeyNames) / sizeof(sKeyNames[0]); ++i) {
            if (strcasecmp(tok, sKeyNames[i].name) == 0) {
                mask |= sKeyNames[i].mask;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    *out = mask;
    return true;
}

static void FormatKeys(uint16_t keys, char* out, size_t size) {
    size_t i, len = 0;
    if (keys == 0) {
        snprintf(out, size, "-");
        return;
    }
    out[0] = '\0';
    for (i = 0; i < sizeof(sKeyNames) / sizeof(sKeyNames[0]); ++i) {
        if (keys & sKeyNames[i].mask) {
            len += (size_t)snprintf(out + len, len < size ? size - len : 0, "%s%s", len ? "+" : "", sKeyNames[i].name);
        }
    }
}

/* The save a replay starts from (a 'save FILE' line), relative to the replay's directory. */
static char sReplaySave[1024];

static bool SetReplaySave(const char* replayPath, const char* text) {
    const char* slash = strrchr(replayPath, '/');
    const char* backslash = strrchr(replayPath, '\\');
    size_t dirLength, nameLength;
    while (isspace((unsigned char)*text)) text++;
    nameLength = strlen(text);
    while (nameLength && isspace((unsigned char)text[nameLength - 1])) nameLength--;
    if (nameLength == 0) return false;
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    dirLength = slash && text[0] != '/' ? (size_t)(slash - replayPath + 1) : 0;
    if (dirLength + nameLength >= sizeof(sReplaySave)) return false;
    memcpy(sReplaySave, replayPath, dirLength);
    memcpy(sReplaySave + dirLength, text, nameLength);
    sReplaySave[dirLength + nameLength] = '\0';
    return true;
}

const char* Trace_ReplaySave(void) { return sReplaySave[0] ? sReplaySave : NULL; }

bool Trace_LoadReplay(const char* path) {
    FILE* f = fopen(path, "rb");
    char line[256];
    unsigned lineNo = 0;
    size_t capacity = 0;

    if (!f) {
        fprintf(stderr, "[Trace] Cannot open replay '%s'\n", path);
        return false;
    }
    while (fgets(line, sizeof(line), f)) {
        char keysText[128];
        unsigned long frame;
        char* p = line;
        lineNo++;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;
        if (strncmp(p, "save", 4) == 0 && isspace((unsigned char)p[4])) {
            if (!SetReplaySave(path, p + 5)) {
                fprintf(stderr, "[Trace] %s:%u: expected 'save <file>'\n", path, lineNo);
                fclose(f);
                return false;
            }
            continue;
        }
        if (sscanf(p, "%lu %127s", &frame, keysText) != 2) {
            fprintf(stderr, "[Trace] %s:%u: expected '<frame> <keys>'\n", path, lineNo);
            fclose(f);
            return false;
        }
        if (sReplayCount == capacity) {
            capacity = capacity ? capacity * 2 : 64;
            sReplay = (ReplayEntry*)realloc(sReplay, capacity * sizeof(*sReplay));
        }
        if (sReplayCount && frame <= sReplay[sReplayCount - 1].frame) {
            fprintf(stderr, "[Trace] %s:%u: frames must increase\n", path, lineNo);
            fclose(f);
            return false;
        }
        if (!ParseKeys(keysText, &sReplay[sReplayCount].keys)) {
            fprintf(stderr, "[Trace] %s:%u: bad keys '%s'\n", path, lineNo, keysText);
            fclose(f);
            return false;
        }
        sReplay[sReplayCount].frame = (uint32_t)frame;
        sReplayCount++;
    }
    fclose(f);
    sReplayCursor = 0;
    return true;
}

bool Trace_HasReplay(void) { return sReplay != NULL; }

uint32_t Trace_ReplayLength(void) {
    return sReplayCount ? sReplay[sReplayCount - 1].frame + 1 : 0;
}

uint16_t Trace_ReplayKeys(uint32_t frame) {
    if (!sReplayCount || frame < sReplay[0].frame) return 0;
    /* Frames only move forward; rewind just in case. */
    if (sReplayCursor >= sReplayCount || sReplay[sReplayCursor].frame > frame) sReplayCursor = 0;
    while (sReplayCursor + 1 < sReplayCount && sReplay[sReplayCursor + 1].frame <= frame) sReplayCursor++;
    return sReplay[sReplayCursor].keys;
}

/* ---- Recording ------------------------------------------------------------- */

static FILE* sRecord;
static int sLastRecordedKeys = -1;

bool Trace_OpenRecord(const char* path) {
    sRecord = fopen(path, "wb");
    if (!sRecord) {
        fprintf(stderr, "[Trace] Cannot write '%s'\n", path);
        return false;
    }
    fprintf(sRecord, "# mzm-linux input replay\n");
    return true;
}

void Trace_RecordKeys(uint32_t frame, uint16_t keys) {
    char text[96];
    if (!sRecord || (int)keys == sLastRecordedKeys) return;
    sLastRecordedKeys = keys;
    FormatKeys(keys, text, sizeof(text));
    fprintf(sRecord, "%u %s\n", (unsigned)frame, text);
}

/* ---- Trace log ------------------------------------------------------------- */

static FILE* sLog;

bool Trace_OpenLog(const char* path) {
    sLog = strcmp(path, "-") == 0 ? stdout : fopen(path, "wb");
    if (!sLog) {
        fprintf(stderr, "[Trace] Cannot write '%s'\n", path);
        return false;
    }
    fprintf(sLog, "# frame keys mode sub area room x y energy fc16 | fb vram bgpal objpal oam snd | ewram iwram io\n");
    return true;
}

bool Trace_LogEnabled(void) { return sLog != NULL; }

/* I/O registers, minus the DMA source/destination addresses: the port's DMA
 * writes host pointers there, which differ between builds and systems. */
static uint32_t HashIo(void) {
    static const uint16_t kDmaAddressRegs[] = { 0xB0, 0xBC, 0xC8, 0xD4 }; /* DMAxSAD, DAD follows */
    uint8_t io[sizeof(gIoMem)];
    size_t i;
    memcpy(io, gIoMem, sizeof(io));
    for (i = 0; i < sizeof(kDmaAddressRegs) / sizeof(kDmaAddressRegs[0]); ++i)
        memset(io + kDmaAddressRegs[i], 0, 8);
    return HashBytes(FNV_OFFSET, io, sizeof(io));
}

void Trace_LogFrame(uint32_t frame, uint16_t keys, const uint32_t* framebuffer, const uint32_t* bottom) {
    uint32_t fb;

    if (!sLog) return;
    fb = framebuffer ? HashU32Values(framebuffer, 240u * 160u) : 0;
    fprintf(sLog, "%u %03x %x %x %x %x %x %x %x %x | %08x %08x %08x %08x %08x %08x | %08x %08x %08x",
            (unsigned)frame, (unsigned)keys,
            (unsigned)gMainGameMode, (unsigned)(u16)gSubGameMode1,
            (unsigned)gCurrentArea, (unsigned)gCurrentRoom,
            (unsigned)gSamusData.xPosition, (unsigned)gSamusData.yPosition,
            (unsigned)gEquipment.currentEnergy, (unsigned)gFrameCounter16Bit,
            fb,
            HashU16Values((const uint16_t*)gVram, sizeof(gVram) / 2),
            HashU16Values(gBgPltt, 256), HashU16Values(gObjPltt, 256),
            HashU16Values(gOamMem, 0x200),
            HashBytes(FNV_OFFSET, gMusicInfo.soundRawData, sizeof(gMusicInfo.soundRawData)),
            HashBytes(FNV_OFFSET, gEwram, sizeof(gEwram)),
            HashBytes(FNV_OFFSET, gIwram, sizeof(gIwram)),
            HashIo());
    /* The bottom screen only when the UI runs, so other traces keep their lines. */
    if (bottom) fprintf(sLog, " | %08x", HashU32Values(bottom, 320u * 240u));
    fputc('\n', sLog);
}

/* ---- Screenshots ----------------------------------------------------------- */

bool Trace_DumpMemory(const char* dir, uint32_t frame) {
    const struct { const char* name; const void* data; size_t size; } regions[] = {
        { "ewram", gEwram, sizeof(gEwram) },   { "iwram", gIwram, sizeof(gIwram) },
        { "io", gIoMem, sizeof(gIoMem) },      { "vram", gVram, sizeof(gVram) },
        { "bgpal", gBgPltt, sizeof(gBgPltt) }, { "objpal", gObjPltt, sizeof(gObjPltt) },
        { "oam", gOamMem, sizeof(gOamMem) },
    };
    size_t i;
    for (i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
        char path[1024];
        FILE* f;
        snprintf(path, sizeof(path), "%s/%s_%u.bin", dir, regions[i].name, (unsigned)frame);
        f = fopen(path, "wb");
        if (!f) return false;
        fwrite(regions[i].data, 1, regions[i].size, f);
        fclose(f);
    }
    return true;
}

bool Trace_WritePpmSized(const char* path, const uint32_t* pixels, int width, int height) {
    FILE* f = fopen(path, "wb");
    int i;
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (i = 0; i < width * height; ++i) {
        uint32_t c = pixels[i]; /* ABGR8888: R in the low byte */
        uint8_t rgb[3] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return true;
}

bool Trace_WritePpm(const char* path, const uint32_t* framebuffer) {
    return Trace_WritePpmSized(path, framebuffer, 240, 160);
}

void Trace_Close(void) {
    if (sRecord) fclose(sRecord);
    if (sLog && sLog != stdout) fclose(sLog);
    else if (sLog) fflush(sLog);
    sRecord = NULL;
    sLog = NULL;
}
