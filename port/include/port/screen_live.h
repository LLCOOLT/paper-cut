#ifndef PORT_SCREEN_LIVE_H
#define PORT_SCREEN_LIVE_H

#include <stddef.h>

/* Whether the field is on screen now (fadedrv.c): the field sequence, no map change under way, the 3D
 * camera and the map drawn, no fade still going and none left covering the picture (a colour, iris or
 * spot fade out, the pipe's black, the white-out). 1 live, 0 not; out (size bytes) says what is up, as
 * "live" or "black: the 3D camera is off". The tests' `where screen` (input_pc.c) and the co-op guest's
 * arrival in the host's room (statesync_coop_pc.inc) log it, so a run can tell a black screen. Reads
 * only. */
int portScreenLive(char* out, size_t size);

#endif
