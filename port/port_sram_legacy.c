#include "port_sram_legacy.h"

#include <stdlib.h>
#include <string.h>

#include "structs/save_file.h"

#include "port_sram_legacy_map.inc"

/* The game's checksums (src/save_file.c): the sum of the block's words, its checksum fields
 * included, must equal the checksum, and the checksum must be the complement of notChecksum.
 * The number of words is sizeof / 8 / 4 rounded down, times 8 (u32 blocks), or sizeof / 4 / 2
 * times 4 (u16 blocks) -- for the legacy SaveFile, 4632 bytes, that leaves its last 24 out. */
static u32 Sum32(const u8* block, u32 size)
{
    u32 words = (size / 8 / sizeof(u32)) * 8;
    u32 sum = 0;
    u32 i;
    for (i = 0; i < words; i++)
    {
        u32 w;
        memcpy(&w, block + i * sizeof(u32), sizeof(u32));
        sum += w;
    }
    return sum;
}

static u16 Sum16(const u8* block, u32 size)
{
    u32 halves = (size / 4 / sizeof(u16)) * 4;
    u16 sum = 0;
    u32 i;
    for (i = 0; i < halves; i++)
    {
        u16 h;
        memcpy(&h, block + i * sizeof(u16), sizeof(u16));
        sum += h;
    }
    return sum;
}

static boolu8 Valid32(const u8* block, u32 size, u32 checksumOffset)
{
    u32 checksum, notChecksum;
    memcpy(&checksum, block + checksumOffset, sizeof(u32));
    memcpy(&notChecksum, block + checksumOffset + sizeof(u32), sizeof(u32));
    return Sum32(block, size) == checksum && checksum == ~notChecksum;
}

/* SramRead_TimeAttack only compares the sum with the checksum. */
static boolu8 Valid16(const u8* block, u32 size, u32 checksumOffset)
{
    u16 checksum;
    memcpy(&checksum, block + checksumOffset, sizeof(u16));
    return Sum16(block, size) == checksum;
}

/* As the game writes them: the fields at 0 and ~0 while summing, then the result. */
static void Seal32(u8* block, u32 size, u32 checksumOffset)
{
    u32 checksum = 0, notChecksum = ~0u;
    memcpy(block + checksumOffset, &checksum, sizeof(u32));
    memcpy(block + checksumOffset + sizeof(u32), &notChecksum, sizeof(u32));
    checksum = Sum32(block, size);
    notChecksum = ~checksum;
    memcpy(block + checksumOffset, &checksum, sizeof(u32));
    memcpy(block + checksumOffset + sizeof(u32), &notChecksum, sizeof(u32));
}

static void Seal16(u8* block, u32 size, u32 checksumOffset)
{
    u16 checksum = 0, notChecksum = (u16)~0u;
    memcpy(block + checksumOffset, &checksum, sizeof(u16));
    memcpy(block + checksumOffset + sizeof(u16), &notChecksum, sizeof(u16));
    checksum = Sum16(block, size);
    notChecksum = (u16)~checksum;
    memcpy(block + checksumOffset, &checksum, sizeof(u16));
    memcpy(block + checksumOffset + sizeof(u16), &notChecksum, sizeof(u16));
}

s32 Port_SramLegacy_ValidFiles(const u8* sram, u32 size, boolu8 legacy)
{
    const u32 fileSize = legacy ? LEGACY_SAVE_FILE_SIZE : (u32)sizeof(struct SaveFile);
    s32 valid = 0;
    u32 i;
    for (i = 0; i < ARRAY_SIZE(sLegacySaveFiles); i++)
    {
        const u32 at = legacy ? sLegacySaveFiles[i].legacy : sLegacySaveFiles[i].current;
        if (at + fileSize <= size && Valid32(sram + at, fileSize, SAVE_FILE_CHECKSUM_OFFSET))
            valid++;
    }
    return valid;
}

void Port_SramLegacy_Convert(const u8* legacy, u8* out, u32 outSize)
{
    u32 i;

    /* What is not data (the GBA's padding) reads as erased SRAM; past struct Sram nothing changes. */
    (void)outSize;
    memset(out, 0xFF, sizeof(struct Sram));
    for (i = 0; i < ARRAY_SIZE(sLegacySramRuns); i++)
        memcpy(out + sLegacySramRuns[i].current, legacy + sLegacySramRuns[i].legacy, sLegacySramRuns[i].size);

    for (i = 0; i < ARRAY_SIZE(sLegacySaveFiles); i++)
    {
        if (Valid32(legacy + sLegacySaveFiles[i].legacy, LEGACY_SAVE_FILE_SIZE, SAVE_FILE_CHECKSUM_OFFSET))
            Seal32(out + sLegacySaveFiles[i].current, sizeof(struct SaveFile), SAVE_FILE_CHECKSUM_OFFSET);
    }
    for (i = 0; i < ARRAY_SIZE(sLegacyTimeAttacks); i++)
    {
        if (Valid16(legacy + sLegacyTimeAttacks[i].legacy, LEGACY_SAVE_TIME_ATTACK_SIZE,
                    SAVE_TIME_ATTACK_CHECKSUM_OFFSET))
            Seal16(out + sLegacyTimeAttacks[i].current, sizeof(struct SaveTimeAttack),
                   SAVE_TIME_ATTACK_CHECKSUM_OFFSET);
    }
}

boolu8 Port_SramLegacy_ConvertInPlace(u8* sram, u32 size, boolu8 strict)
{
    const s32 legacyValid = Port_SramLegacy_ValidFiles(sram, size, TRUE);
    const s32 currentValid = Port_SramLegacy_ValidFiles(sram, size, FALSE);
    u8* copy;

    if (size < LEGACY_SRAM_SIZE || size < sizeof(struct Sram))
        return FALSE;
    if (strict ? legacyValid <= currentValid : legacyValid < currentValid)
        return FALSE;
    copy = (u8*)malloc(size);
    if (!copy)
        return FALSE;
    memcpy(copy, sram, size);
    Port_SramLegacy_Convert(copy, sram, size);
    free(copy);
    return TRUE;
}
