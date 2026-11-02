#ifndef PORT_RESUME_H
#define PORT_RESUME_H

/* Quick resume (port/src/aurora/resume_aurora.c): the game kept as a save of
 * its own while playing, written when the app goes to the background or
 * quits, for the launcher's Continue. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Once a retrace (vi_aurora.c). */
void portResumeFrame(void);
/* Writes the latest resume save out now (the app going to the background or
 * quitting); nothing when there's none or it's written already. */
void portResumeFlush(void);
/* <user>/resume_<gameId>.gci, or with sidecar its .txt (the save file it
 * came from, 0-3). */
void portResumePath(char* out, size_t size, const char* gameId, int sidecar);
/* The save at path loaded as card slot (0-3) at the next chance, through the
 * save warps (port/src/common/warp_pc.c): the launcher's Continue. */
void portWarpRequestResume(const char* path, int slot);

#ifdef __cplusplus
}
#endif

#endif
