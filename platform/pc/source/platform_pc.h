#ifndef PLATFORM_PC_H
#define PLATFORM_PC_H

#include "types.h"
#include <stdbool.h>

/* Returns false if a window was asked for and could not be opened. */
bool Platform_PC_Init(bool headless);
void Platform_PC_Cleanup(void);
void Platform_PC_VBlank(void);
void Platform_PC_AfterVBlank(void);
void Platform_PC_ScanlineTick(void);
u16  Platform_PC_PollKeys(void);

#endif /* PLATFORM_PC_H */
