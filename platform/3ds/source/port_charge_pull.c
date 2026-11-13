#include "port_charge_pull.h"

#include <stdint.h>

#include "constants/samus.h"
#include "structs/samus.h"
#include "types.h"

extern bool Port_RA_IsHardcore(void);

enum {
    /* Sub pixels (4 a pixel): the reach and the step are sm-3ds's, 96 and 3 pixels. */
    kReach = 96 * 4,
    kStep = 3 * 4,
    /* Frames of charging before the pull starts, so a plain shot never moves the drops. */
    kChargeFrames = 16,
};

static bool sEnabled = false;

bool Port_ChargePull_Enabled(void) { return sEnabled; }
void Port_ChargePull_SetEnabled(bool on) { sEnabled = on; }

static int Toward(int delta) {
    if (delta > kStep) return kStep;
    if (delta < -kStep) return -kStep;
    return delta;
}

void Port_ChargePull_Apply(unsigned short* yPosition, unsigned short* xPosition) {
    if (!sEnabled || Port_RA_IsHardcore()) return;
    if (!(gEquipment.beamBombsActivation & BBF_CHARGE_BEAM)) return;
    if (gSamusWeaponInfo.chargeCounter < kChargeFrames) return;

    /* Samus's position is her feet: aim at the middle of her hitbox. */
    const int targetY = (int)gSamusData.yPosition + (gSamusPhysics.hitboxTop + gSamusPhysics.hitboxBottom) / 2;
    const int targetX = (int)gSamusData.xPosition;
    const int dy = targetY - (int)*yPosition;
    const int dx = targetX - (int)*xPosition;
    if (dx > kReach || dx < -kReach || dy > kReach || dy < -kReach) return;

    *yPosition = (unsigned short)((int)*yPosition + Toward(dy));
    *xPosition = (unsigned short)((int)*xPosition + Toward(dx));
}
