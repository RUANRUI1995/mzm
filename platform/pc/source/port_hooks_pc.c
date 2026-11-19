/* Neutral versions of the 3DS port hooks that guarded code in src/ calls.
 *
 * The Linux build defines MZM_3DS so it runs the same port paths as the 3DS,
 * but it has none of the 3DS-only features behind these hooks (Wide view,
 * bottom-screen UI, save states, overlay text). Each stub here is what the
 * 3DS build does with that feature switched off, so game logic behaves the
 * same as a 3DS with default settings. */
#include <stdbool.h>

/* Wide view off: no margins, no OAM position tags. */
int Port_WideMarginSubPixelX(void) { return 0; }
int Port_WideMarginSubPixelY(void) { return 0; }
void Port_Wide_NoteSlots(int firstSlot, int endSlot, int originY, int originX) {
    (void)firstSlot; (void)endSlot; (void)originY; (void)originX;
}

/* Overlay text is drawn by the 3DS renderer only. */
void Port_OverlayText_NoteEscapeOam(int firstSlot, int endSlot) {
    (void)firstSlot; (void)endSlot;
}

/* Debug "force effect" toggles live in the 3DS debug menu; none are set. */
bool PortPpuMzm_DebugGetForceEffect(unsigned smfBit) { (void)smfBit; return false; }

/* No save states on this platform. */
void Port_SaveState_ServicePending(void) {}

/* The bottom-screen messages (Port_SaveFile_ShowFailed, ...) come from the
 * shared UI, port/ui/port_bottom_ui.c. */
