/* Byte-swaps the ROM's typed data for a big-endian host
 * (docs/wiiu-big-endian-plan.md, part B).
 *
 * The ROM is little-endian. C code reads its tables and structs natively
 * (GBA_RESOLVE, the port/generated p_ tables), so on a big-endian host those
 * reads need every 2- and 4-byte scalar reversed. The type map
 * (port_rom_type_map.h, generated per region by tools/gen_rom_type_map.py)
 * says where they are. The pristine bytes stay available in gRomDataRaw for
 * what reads the ROM as bytes or GBA-order units (gba_read16/32, DMA and
 * decompression sources). On a little-endian host none of this runs. */
#include "port_rom.h"
#include "port_rom_type_map.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* One bit per ROM halfword: set where a typed object with 2- or 4-byte
 * scalars lies. Copies into native memory read those from the swapped copy
 * and everything else (graphics, compressed data) as GBA units. */
static uint8_t* sTypedBits;
static uint32_t sTypedSize;

static void MarkTyped(uint32_t offset, uint32_t length) {
    uint32_t h;
    for (h = offset >> 1; h < ((offset + length + 1) >> 1); h++)
        sTypedBits[h >> 3] |= (uint8_t)(1u << (h & 7));
}

int Port_RomIsTyped(uint32_t offset) {
    uint32_t h = offset >> 1;
    if (sTypedBits == NULL || offset >= sTypedSize)
        return 0;
    return (sTypedBits[h >> 3] >> (h & 7)) & 1;
}

int Port_HostIsBigEndian(void) {
    const uint16_t probe = 1;
    return *(const uint8_t*)&probe == 0;
}

static void Swap2(uint8_t* p) {
    uint8_t t = p[0];
    p[0] = p[1];
    p[1] = t;
}

static void Swap4(uint8_t* p) {
    uint8_t t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = t;
}

static const PortRomTypeMap* MapForRegion(int region) {
    switch (region) {
        case PORT_ROM_REGION_EU: return &gPortRomTypeMap_eu;
        case PORT_ROM_REGION_US: return &gPortRomTypeMap_us;
        case PORT_ROM_REGION_JP: return &gPortRomTypeMap_jp;
        default: return NULL;
    }
}

int Port_RomSwap_Apply(uint8_t* rom, uint32_t size, int region) {
    const PortRomTypeMap* map = MapForRegion(region);
    uint32_t i, swapped = 0, outside = 0;

    if (map == NULL) {
        fprintf(stderr, "[ROM] No type map for this region: typed ROM data stays little-endian\n");
        return 0;
    }
    free(sTypedBits);
    sTypedSize = size;
    sTypedBits = (uint8_t*)calloc((size / 2 + 7) / 8, 1);
    if (sTypedBits == NULL)
        fprintf(stderr, "[ROM] No memory for the typed-data bitmap: copies read the original bytes\n");
    for (i = 0; i < map->objectCount; i++) {
        const PortRomObject* object = &map->objects[i];
        const PortRomType* type = &map->types[object->type];
        const uint32_t offset = object->address - 0x08000000u;
        uint32_t e, k;
        if (offset + type->size * object->count > size) {
            outside++;
            continue;
        }
        if (type->opCount != 0 && sTypedBits != NULL)
            MarkTyped(offset, type->size * object->count);
        for (e = 0; e < object->count; e++) {
            uint8_t* element = rom + offset + e * type->size;
            for (k = 0; k < type->opCount; k++) {
                const PortRomScalar* op = &map->ops[type->firstOp + k];
                if (op->width == 2)
                    Swap2(element + op->offset);
                else
                    Swap4(element + op->offset);
                swapped++;
            }
        }
    }
    fprintf(stderr, "[ROM] Byte-swapped %u scalars in %u typed objects%s\n", (unsigned)swapped,
            (unsigned)map->objectCount, outside ? " (some past the end of the ROM were skipped)" : "");
    return 1;
}
