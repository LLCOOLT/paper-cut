#ifndef PORT_MEM_GUARD_H
#define PORT_MEM_GUARD_H

/* The memory guard (Aurora backend, port/settings/features/mem_guard.cpp): phones and tablets (iOS, Android)
 * only, where the system ends an app that takes too much memory without a word (iOS's jetsam, Android's
 * low-memory killer) and no crash report is left. A computer is never touched ($TTYD_MEM_GUARD=1 turns it on
 * anywhere, for tests; =0 off on a phone).
 *
 * - At start (portMemGuardStart, main_aurora.c, after settings.txt is read): the graphics settings capped by
 *   the device's memory for the run (settings.txt keeps the player's): Texture upscaling 4x needs about 8 GB,
 *   2x about 4 GB; Supersampling 3x and 2x the same; Texture pack memory up to High, Medium, Low. A short
 *   notice says so.
 * - While playing (each presented frame, the game's or the launcher's: portMemGuardFrame): the system's memory
 *   warnings (SDL_EVENT_LOW_MEMORY: iOS's, Android's onTrimMemory/onLowMemory) while on screen, and the room
 *   left (iOS: os_proc_available_memory; Android: MemAvailable) polled every 2 s, step the run down at most
 *   every 10 s, with a short notice: 1, supersampling off, texture upscaling one step down, texture pack memory
 *   Low; 2, texture upscaling off and the picture at 480 lines; each time the decoded textures let go.
 * - A small rolling record (memory_record.txt in the user folder: the memory in use and the room left, on
 *   screen or in the background, every 10 s and at each change, the last 16): at the next start, a run that
 *   ended on screen with no clean exit and no crash report was closed by the system: "[memory] ..." lines log
 *   its record (kept as memory_record_last.txt) and a short note says so. One that ended in the background
 *   (the system's own business: an app put away is often ended) brings nothing.
 *
 * Tests: $TTYD_MEM_GUARD_RAM=<MB> the device's memory, $TTYD_MEM_GUARD_ROOM=<MB> the room left (a low one
 * steps down), $TTYD_MEM_GUARD_WARN_AT=<frame> a memory warning then, $TTYD_MEM_GUARD_KILL_AT=<frame> the run
 * ended there as the system would (no exit handlers), $TTYD_MEM_GUARD_KILL_BG=1 after going to the background
 * first, $TTYD_MEM_GUARD_GAP_MS=<ms> the least gap between steps (10000). */

#ifdef __cplusplus
extern "C" {
#endif

void portMemGuardStart(const char* userPath);
/* in a Dear ImGui frame (the settings page's overlays, the launcher's loop) */
void portMemGuardFrame(void);
/* the last run's record says the system closed it while on screen (the Safe graphics offer, safe_graphics.h) */
int portMemGuardClosedOnScreen(void);

#ifdef __cplusplus
}
#endif

#endif
