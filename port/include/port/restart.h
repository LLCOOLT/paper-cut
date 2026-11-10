#ifndef PORT_RESTART_H
#define PORT_RESTART_H

/* Restart now (port/settings/features/restart.cpp): the settings that apply
 * from the next start (PORT_SETTING_NEXT_START) without quitting and
 * reopening. The game's own process starts again with the same disc, and
 * quick resume (port/include/port/resume.h) brings the player back where they
 * were. A phone or tablet app can't start itself again: there the player is
 * asked to close and reopen it. */

#ifdef __cplusplus
extern "C" {
#endif

/* This program can start itself again (not on iOS or Android). */
int portRestartSupported(void);
/* A "next start" setting's value (an int of the settings, plain or declared)
 * differs from what it was when this run began; with NULL: any of them. */
int portRestartPendingFor(const int* value);
int portRestartPending(void);
/* Restarts as soon as it is safe (the memory card not being written), from
 * the next frame: resume 1 brings the player back where they were (quick
 * resume's save), 0 starts the game fresh (a card changed under it: quick
 * resume's save from before is dropped, so Continue can't undo it). Settings
 * are saved first. Nothing on a platform that can't restart. */
void portRestartRequest(int resume);
/* The same, with whenIdle run first, once the card is idle (a save tool writing
 * a card file the running game has open): it returns 0 to not restart after
 * all (it failed; nothing was changed). */
void portRestartRequestWith(int resume, int (*whenIdle)(void));
/* Back to the launcher (port/include/port/launcher_back.h), its last step at the
 * title: this program started again with no disc, on its launcher (quick
 * resume's save written for its Continue), and this one ended. Returns 0 only
 * when it couldn't (the reason in the log; this one plays on), or on a
 * platform that can't restart. */
int portRestartToLauncher(void);
/* A restart is waiting for the card. */
int portRestartWaiting(void);
/* The settings page's "Restart now" (and on iOS/Android the "Close and reopen
 * the app" note) after a setting, for Dear ImGui code (C++ only). */
void portRestartButton(void);

/* After starting another Paper Cut program that takes over (a restart, the
 * launcher's Play of another version, an update): this one ends, its window
 * hidden first. On Linux in an AppImage or a Flatpak it becomes a small waiter
 * instead (the same executable, re-run), so the mount or sandbox the new one
 * runs from outlives this one. Never returns. */
struct SDL_Process;
void portEndForStarted(struct SDL_Process* process);
/* The waiter: first thing at start (main_aurora.c); returns when not one. */
void portWaitForStartedIfAsked(void);

#ifdef __cplusplus
}
#endif

#endif
