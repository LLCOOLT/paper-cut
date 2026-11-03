#ifndef PORT_OS_PC_H
#define PORT_OS_PC_H

/* Give every runnable game thread one slice and fire due alarms. Called by
 * the main thread wherever the GameCube would block (vsync, OSYieldThread). */
void portThreadsRunSlice(void);

#endif
