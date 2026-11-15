#ifndef PORT_FLIGHT_H
#define PORT_FLIGHT_H

/* The flight recorder (Aurora backend, port/src/aurora/flight_aurora.c), GitHub #39: a phone whose GPU took
 * the whole device down (a hard reset: no crash report, no log) leaves what the run was doing all the same.
 *
 * A small record (about 6 KB) is kept on storage as the run goes and synced to it (fdatasync, Windows'
 * FlushFileBuffers) by a thread of its own, so it survives a reset: every second on a phone or tablet
 * (every 5 s on a computer), and at once at each room change, battle start and end, graphics setting
 * change, guard step and graphics phase. It holds the version, the system, the graphics API, adapter and
 * driver, the graphics settings, the run's state (on screen or in the background), the last 24 events
 * (phases, Aurora's warnings and errors, room changes, the guards' steps) and the last 30 seconds: the room,
 * the frame interval (average, worst) and work time, draws, render passes, EFB copies, uploads, textures
 * alive and their memory, bind groups, and pipelines made and queued. It goes to two files in turn
 * (flight_record_a.txt, flight_record_b.txt, each ended by "#end <seq>"): a reset halfway through a write
 * leaves the other whole. Its cost: a few hundred bytes formatted once a second on the game's thread.
 *
 * At the next start (portFlightStart, before Aurora starts) the newest whole record is kept as
 * flight_record_last.txt and looked at: a run that ended on screen with no clean exit and no crash report
 * "stopped while drawing" (portFlightStoppedOnScreen): "[flight] ..." lines log its last lines, and the
 * launcher offers Safe graphics, the other graphics API (Android) and the record to copy
 * (port/settings/features/safe_graphics.cpp). Settings > General > Crash reports copies it any time.
 *
 * $TTYD_FLIGHT=0 turns it off. $TTYD_FLIGHT_KILL_AT=<frame> (tests) ends the run there with no exit
 * handlers, as a device reset would. */

#ifdef __cplusplus
extern "C" {
#endif

/* main_aurora.c, right after portCrashReportEarly (userPath NULL: Aurora's default folder) */
void portFlightStart(const char* userPath);
/* each presented frame (display_aurora.c), with its interval and work time in ms; the launcher's frames
 * with -1 (the interval measured here) */
void portFlightFrame(float intervalMs, float workMs);
/* an event: one line in the record, written to storage now (printf's format) */
void portFlightMark(const char* format, ...);
/* Aurora's log (main_aurora.c's logCallback): its warnings and errors as events, the adapter and driver */
void portFlightLog(int level, const char* message, unsigned int len);
/* the last run stopped on screen with no clean exit and no crash report (its record says so) */
int portFlightStoppedOnScreen(void);
/* flight_record_last.txt's path, or NULL when there is none */
const char* portFlightLastPath(void);

#ifdef __cplusplus
}
#endif

#endif
