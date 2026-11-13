#ifndef PORT_CHARGE_PULL_H
#define PORT_CHARGE_PULL_H

#include <stdbool.h>

/* CHARGE PULLS DROPS (after ../sm-3ds's charge pull, issue #37): while the charge beam charges,
 * the energy and ammo drops enemies leave near Samus fly to her; touching her picks them up as
 * always (src/sprites_ai/enemy_drop.c). Off by default (mzm3ds.ini `charge_pull`, OPTIONS ->
 * CONTROLS); off in RetroAchievements hardcore, like the other assists. */
bool Port_ChargePull_Enabled(void);
void Port_ChargePull_SetEnabled(bool on);

/* Called by an idle enemy drop once per frame, once it can be picked up: moves it a step towards
 * Samus when the option is on, the charge beam is charging and the drop is close enough. */
void Port_ChargePull_Apply(unsigned short* yPosition, unsigned short* xPosition);

#endif
