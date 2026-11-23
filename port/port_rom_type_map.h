#ifndef PORT_ROM_TYPE_MAP_H
#define PORT_ROM_TYPE_MAP_H

/* The ROM's typed data objects and where their 2- and 4-byte scalars are,
 * generated per region by tools/gen_rom_type_map.py from the decomp's .map
 * and the types of src/data/*.c. A big-endian host swaps those scalars once at
 * load (port_rom_swap.c, docs/wiiu-big-endian-plan.md). */

#include <stdint.h>

typedef struct {
    uint16_t offset; /* from the start of the element */
    uint8_t width;   /* 2 or 4 */
} PortRomScalar;

typedef struct {
    uint32_t size;     /* element size in bytes */
    uint32_t firstOp;  /* index into ops */
    uint32_t opCount;
} PortRomType;

typedef struct {
    uint32_t address;  /* GBA address */
    uint32_t count;    /* elements */
    uint32_t type;     /* index into types */
} PortRomObject;

typedef struct {
    const PortRomScalar* ops;
    const PortRomType* types;
    const PortRomObject* objects;
    uint32_t objectCount;
} PortRomTypeMap;

extern const PortRomTypeMap gPortRomTypeMap_eu;
extern const PortRomTypeMap gPortRomTypeMap_us;
extern const PortRomTypeMap gPortRomTypeMap_jp;

#endif /* PORT_ROM_TYPE_MAP_H */
