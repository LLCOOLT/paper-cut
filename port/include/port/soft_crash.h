#ifndef PORT_SOFT_CRASH_H
#define PORT_SOFT_CRASH_H

/* Softer landings after a crash (Aurora backend; docs/TECHNICAL.md, TTYD_CRASH_SAFE_EVERY and the rows by it):
 *
 * - Crash-safe snapshots (General > Crash-safe snapshots, on by default; port/src/aurora/resume_aurora.c): quick
 *   resume's clean save in memory (a real save of the file, never a save state: those hold this run's memory and
 *   are good only for the run that made them) also written every 3 minutes of play and at a room change (at most
 *   once a minute) to <user>/crashsafe_<game ID>_1.gci and _2.gci in turn, the older one replaced, each with its
 *   .txt beside it (the save file it came from, when, the map and the area's name). Written on a thread of their
 *   own, synced, whole or not at all (the .txt goes first and comes back last: a .gci with no .txt is never
 *   offered). Never while playing online, nor in a replay or ghost race, a leaderboard run, a daily seed, a bingo
 *   lockout room or on a hardcore file: crashsafe_<game ID>.txt says why ("paused=online", from when to when).
 *   After a crash (or a run that stopped on screen), the next start offers "Continue from a few minutes ago" for
 *   each (crash_report.cpp's recovery screen; port/settings/features/soft_crash.cpp otherwise).
 * - Undo a setting that crashed the game (port/settings/features/soft_crash.cpp): every settings.txt write
 *   (portSettingsSave, the launcher's page and the game's alike) notes what changed (the key, its old and new
 *   value, the time) in <user>/settings_changes.txt, the last 24. A run that crashed or stopped on screen within
 *   5 minutes of a change brings "Paper Cut closed after you changed <setting>. Undo it?" at the next start,
 *   inside the recovery screen or the Safe graphics offer, else in a window of its own: one window only.
 * - Report a problem (port/settings/features/report_problem.cpp): one zip in the user folder of what helps a
 *   report, shown before it is made, then GitHub's new-issue page filled in and the zip's folder opened.
 *
 * Tests: $TTYD_CRASH_SAFE_EVERY=<frames> the snapshots' period, $TTYD_CRASH_SAFE_ROOM_GAP=<frames> the least gap
 * between room changes' ones; $TTYD_RECOVERY_TEST=snap:<frames> (crash_report.cpp) continues from the newest;
 * $TTYD_UNDO_TEST=undo|keep:<frames> answers the undo question; $TTYD_REPORT_PROBLEM_TEST=<frames> makes the
 * report's zip (papercut-report-test.zip) then. "[crashsafe] ...", "[undo] ...", "[report] ..." log lines. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_CRASH_SAFE_KEPT 2

/* <user>/crashsafe_<gameId>_<which + 1>.gci, or its .txt (sidecar) */
void portCrashSafePath(char* out, size_t size, const char* gameId, int which, int sidecar);
/* <user>/crashsafe_<gameId>.txt: why none were written lately ("paused=<why>", "since=", "until=") */
void portCrashSafeStatusPath(char* out, size_t size, const char* gameId);

/* settings_aurora.c's portSettingsSave: settings.txt's text before and after a write (before NULL: none) */
void portSettingsChangesNote(const char* before, const char* after);
/* the writes meanwhile not noted (the undo itself, the offers' own changes) */
void portSettingsChangesQuiet(int quiet);
/* a fixed field's value address by key (settings_aurora.c), NULL for the frame rate or none */
int* portSettingsFieldValue(const char* key);

/* flight_aurora.c: when the last run's record was last written (Unix seconds), 0 unknown */
long long portFlightEndedAt(void);

/* crash_pc.c: this run's last log lines, oldest first, a line each; the length written */
size_t portCrashLogTail(char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
