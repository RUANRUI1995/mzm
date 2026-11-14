#ifndef PORT_SRAM_LEGACY_H
#define PORT_SRAM_LEGACY_H

#include "types.h"

/* Saves written by the 3DS port before issue #37 used modern GCC's struct alignment instead of
 * the GBA's, so their struct Sram was 48 bytes shorter and every file inside it shifted
 * (port_gba_struct_layout.h). The port now keeps the GBA's layout, and mzm.sav is a 32 KB GBA save;
 * these convert the old ones. */

/* How many of the six save files (files[0..2], filesCopy[0..2]) pass the game's checksum when
 * `sram` is read with the legacy layout (legacy = TRUE) or the current, GBA one. */
s32 Port_SramLegacy_ValidFiles(const u8* sram, u32 size, boolu8 legacy);

/* Converts a struct Sram in the legacy layout (`legacy`, at least LEGACY_SRAM_SIZE bytes) into the
 * current one in `out` (`outSize` bytes, at least sizeof(struct Sram); the GBA's padding is 0xFF,
 * as erased SRAM, and the bytes past struct Sram are left as they are). The blocks whose checksum
 * was valid get it computed again for their new bytes; the others are moved as they are, so an
 * empty or corrupt file stays so. `legacy` and `out` must not overlap. */
void Port_SramLegacy_Convert(const u8* legacy, u8* out, u32 outSize);

/* For a save state from before issue #37 (port_save_state.c): converts the struct Sram at `sram`
 * (`size` bytes) in place, but only when it reads as the legacy layout -- more of its files pass
 * the checksum that way than read as the current one (`strict`), or at least as many (not
 * `strict`, for gSramMem, which holds nothing else). Returns TRUE when it converted. */
boolu8 Port_SramLegacy_ConvertInPlace(u8* sram, u32 size, boolu8 strict);

#endif
