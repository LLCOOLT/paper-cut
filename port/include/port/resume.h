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
/* Autosave at room changes (port/src/aurora/autosave_aurora.c): once a
 * retrace, after portResumeFrame. gPortAutosaveAt: when the last one finished
 * writing (SDL ms, 0 none), gPortAutosaveFailed: it didn't. */
void portAutosaveFrame(void);
extern unsigned long long gPortAutosaveAt;
extern int gPortAutosaveFailed;
/* Writes the latest resume save out now (the app going to the background or
 * quitting); nothing when there's none or it's written already. */
void portResumeFlush(void);
/* Quick resume is on and has a save of this run (Restart now comes back to it). */
int portResumeHave(void);
/* Online co-op's recovery: the last clean save in memory (kept online even
 * with the setting off), its card slot and size; NULL before the first. */
const void* portResumeImage(int* slot, size_t* size);
/* ...and written out now, with the setting off too; 0 when there's none yet */
int portResumeWriteNow(void);
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
