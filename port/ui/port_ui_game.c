/* Game-state helpers for the bottom-screen UI (port_bottom_ui.c) that read the
 * decomp's own data, shared by every platform. */
#include "types.h"
#include "gba.h"
#include "constants/block.h"
#include "structs/bg_clip.h"

/* Per-area, per-tank-type collected counts for the bottom-screen collectibles
 * breakdown (port/ui/port_bottom_ui.c's RenderCollectiblesModal). The map's
 * "obtained item" bitmap (gMinimapTilesWithObtainedItems) only says a tile
 * had *something* collected, not what -- BgClipSetItemAsCollected (bg_clip.c)
 * is the thing that actually knows the type, and it logs every pickup (tanks
 * and major items alike) into gItemsCollected[area][0..gNumberOfItemsCollected[area]),
 * so walk that log and tally by type instead. Major items (ITEM_TYPE_ABILITY)
 * are logged too but deliberately not counted here -- this is a tank/ammo
 * breakdown, matching the per-area totals table (sTotalTanksTable) it's
 * compared against. */
void Port_GetAreaItemTypeCounts(int area, int* outEnergy, int* outMissile, int* outSuper, int* outPowerBomb) {
    *outEnergy = 0;
    *outMissile = 0;
    *outSuper = 0;
    *outPowerBomb = 0;
    if (area < 0 || area >= MAX_AMOUNT_OF_AREAS) return;

    int count = gNumberOfItemsCollected[area];
    if (count > MAX_AMOUNT_OF_ITEMS_PER_AREA) count = MAX_AMOUNT_OF_ITEMS_PER_AREA;
    for (int i = 0; i < count; i++) {
        switch (gItemsCollected[area][i].type) {
            case ITEM_TYPE_ENERGY: (*outEnergy)++; break;
            case ITEM_TYPE_MISSILE: (*outMissile)++; break;
            case ITEM_TYPE_SUPER_MISSILE: (*outSuper)++; break;
            case ITEM_TYPE_POWER_BOMB: (*outPowerBomb)++; break;
            default: break;
        }
    }
}
