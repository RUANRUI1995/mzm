/* mzm.sav in the GBA's byte order on a big-endian host
 * (docs/wiiu-big-endian-plan.md, part F; port_sram_swap.h).
 *
 * The game keeps the save in gSramMem in host order: its C code fills struct
 * Sram with typed stores and reads it back the same way. The file is the
 * GBA's, so on a big-endian host every 2- and 4-byte scalar of struct Sram is
 * swapped on the way in and out (the runs come from tools/gen_sram_byteswap.py).
 *
 * The checksums need more than a swap. The game sums each record as host
 * words (u32 or u16, src/save_file.c), and a record's words hold different
 * values in the two orders wherever they pack smaller fields, so a record that
 * passes its checksum in one order would fail it in the other. The checksum
 * is rewritten as the target order sums it, keeping how far it was off: a
 * valid record stays valid, a damaged one stays damaged by the same amount,
 * and the two conversions are exact inverses. */
#include "port_sram_swap.h"
#include "port_rom.h"

#include <stddef.h>
#include <string.h>

#include "types.h"
#include "structs/save_file.h"

/* A record the game checksums: every unit of it is summed, with the checksum
 * fields reading 0 and ~0 (src/save_file.c, SRAM_GET_CHECKSUM_SIZE). */
typedef struct {
    uint32_t offset;     /* in struct Sram */
    uint32_t size;       /* sizeof the record's struct */
    uint8_t unit;        /* 2 or 4: the width it is summed in, and its checksums' */
    uint8_t iteration;   /* units per round of the game's unrolled loop */
    uint32_t checksum;   /* offset of the checksum field in the record */
    uint32_t notChecksum;
} SramRecord;

#define RECORD(field, type, unit, iteration)                                                       \
    { offsetof(struct Sram, field), sizeof(type), unit, iteration, offsetof(type, checksum),        \
      offsetof(type, notChecksum) }

static const SramRecord sRecords[] = {
    RECORD(fileScreenOptions_fileA, struct SaveFileScreenOptions, 4, 4),
    RECORD(fileScreenOptions_fileB, struct SaveFileScreenOptions, 4, 4),
    RECORD(fileScreenOptions_fileC, struct SaveFileScreenOptions, 4, 4),
    RECORD(files[0], struct SaveFile, 4, 8),
    RECORD(files[1], struct SaveFile, 4, 8),
    RECORD(files[2], struct SaveFile, 4, 8),
    RECORD(filesCopy[0], struct SaveFile, 4, 8),
    RECORD(filesCopy[1], struct SaveFile, 4, 8),
    RECORD(filesCopy[2], struct SaveFile, 4, 8),
    RECORD(mostRecentFileSave, struct SaveValue, 2, 4),
    RECORD(languagesSave[0], struct SaveValue, 2, 4),
    RECORD(languagesSave[1], struct SaveValue, 2, 4),
    RECORD(soundModeSave, struct SaveValue, 2, 4),
    RECORD(timeAttackSaves[0], struct SaveTimeAttack, 2, 4),
    RECORD(timeAttackSaves[1], struct SaveTimeAttack, 2, 4),
};

#define RECORD_COUNT (sizeof(sRecords) / sizeof(sRecords[0]))

static uint32_t ReadUnit(const uint8_t* p, uint8_t unit, int little) {
    if (unit == 2)
        return little ? (uint32_t)(p[0] | p[1] << 8) : (uint32_t)(p[0] << 8 | p[1]);
    return little ? ((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24)
                  : ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3]);
}

static void WriteUnit(uint8_t* p, uint8_t unit, int little, uint32_t value) {
    uint8_t i;
    for (i = 0; i < unit; i++)
        p[little ? i : unit - 1 - i] = (uint8_t)(value >> (8 * i));
}

/* The record's sum as the game computes it when writing (checksum fields 0 and
 * ~0), its stored checksum, and the two checksums' XOR, all in one order. */
typedef struct {
    uint32_t offBy;  /* stored checksum - sum */
    uint32_t pair;   /* checksum ^ notChecksum */
} RecordState;

static uint32_t RecordSum(const uint8_t* record, const SramRecord* r, int little, uint32_t mask) {
    const uint32_t units = r->size / r->iteration / r->unit * r->iteration;
    uint32_t i, sum = 0;
    for (i = 0; i < units; i++) {
        const uint32_t at = i * r->unit;
        if (at == r->checksum)
            continue;
        sum += at == r->notChecksum ? mask : ReadUnit(record + at, r->unit, little);
    }
    return sum & mask;
}

static void Convert(uint8_t* sram, uint32_t size, int fromLittle) {
    RecordState states[RECORD_COUNT];
    uint32_t i, k;

    if (size < sizeof(struct Sram))
        return;

    for (i = 0; i < RECORD_COUNT; i++) {
        const SramRecord* r = &sRecords[i];
        const uint8_t* record = sram + r->offset;
        const uint32_t mask = r->unit == 2 ? 0xFFFFu : 0xFFFFFFFFu;
        const uint32_t checksum = ReadUnit(record + r->checksum, r->unit, fromLittle);
        states[i].offBy = (checksum - RecordSum(record, r, fromLittle, mask)) & mask;
        states[i].pair = checksum ^ ReadUnit(record + r->notChecksum, r->unit, fromLittle);
    }

    for (i = 0; i < gPortSramRunCount; i++) {
        const PortSramRun* run = &gPortSramRuns[i];
        uint8_t* p = sram + run->offset;
        for (k = 0; k < run->count; k++, p += run->width) {
            uint8_t t = p[0];
            if (run->width == 2) {
                p[0] = p[1];
                p[1] = t;
            } else {
                p[0] = p[3];
                p[3] = t;
                t = p[1];
                p[1] = p[2];
                p[2] = t;
            }
        }
    }

    for (i = 0; i < RECORD_COUNT; i++) {
        const SramRecord* r = &sRecords[i];
        uint8_t* record = sram + r->offset;
        const uint32_t mask = r->unit == 2 ? 0xFFFFu : 0xFFFFFFFFu;
        const uint32_t checksum = (RecordSum(record, r, !fromLittle, mask) + states[i].offBy) & mask;
        WriteUnit(record + r->checksum, r->unit, !fromLittle, checksum);
        WriteUnit(record + r->notChecksum, r->unit, !fromLittle, (checksum ^ states[i].pair) & mask);
    }
}

void Port_SramFileToHost(uint8_t* sram, uint32_t size) {
    if (Port_HostIsBigEndian())
        Convert(sram, size, 1);
}

void Port_SramHostToFile(uint8_t* sram, uint32_t size) {
    if (Port_HostIsBigEndian())
        Convert(sram, size, 0);
}
