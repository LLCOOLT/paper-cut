#ifndef PORT_LAUNCHER_BACK_H
#define PORT_LAUNCHER_BACK_H

/* Back to the launcher (port/src/aurora/launcher_back_aurora.c, its rows in
 * port/settings/features/launcher_back.cpp): from a game under way to the
 * launcher's screen, without closing the app. The Start menu's Settings tab
 * (General) and the settings page's General tab have it, and its binding
 * (Controls > Bindings, F2) opens its question on the settings page. It asks
 * first, in the window it was picked from, and says so when there is progress
 * since the last save.
 *
 * Then, in this order: online play left (as Leave the room, its "where now"
 * window dropped: the player asked to go); a replay's recording saved; the
 * game back to its title the game's own way (port/include/port/soft_reset.h),
 * which ends any mode as a Game Over does (a boss rush, the raid, the dojo, a
 * puzzle, versus: the lent pouch and the rest put back); once the title is
 * there and the memory card idle, quick resume's save written (the launcher's
 * Continue) and the launcher shown:
 *  - Windows, macOS, Linux (an AppImage or a Flatpak too): this program
 *    starts again with no disc, as Restart now does (port/include/port/
 *    restart.h: the new window over this one), and its launcher shows even
 *    with "Skip the launcher" on ($TTYD_LAUNCHER_BACK, read once);
 *  - iOS and Android (an app can't start itself again): the launcher runs in
 *    this process, over the title, between two of the game's frames; its Play
 *    or Continue of this version comes back to the game at the title (Continue
 *    with quick resume's save warp). $TTYD_LAUNCHER_BACK_HERE=1 does this on a
 *    desktop too (tests).
 *
 * Tests: $TTYD_LAUNCHER_BACK_AT=<frame> asks for it at that retrace (as the
 * menus' confirmed row does); $TTYD_RESTART_WAIT=1 keeps the old program
 * until the new one ends (restart.cpp), and $TTYD_LAUNCHER_SHOT_EXIT=1 ends the
 * launcher once its captures are made (launcher_shot.cpp). */

#ifdef __cplusplus
extern "C" {
#endif

/* The game has progress since the memory card was last written or read (a
 * save, a file loaded): a file is being played and played on since. */
int portLauncherBackUnsaved(void);
/* Asks for it (the menus, once confirmed); nothing while one is under way.
 * Refused (0, why in English) during a replay's playback or on the second
 * screen's copy of the Start menu. */
int portLauncherBackRequest(const char** why);
/* One is under way (the menus show it). */
int portLauncherBackBusy(void);
/* A game runs (not the launcher, not before the game's start, not a replay's
 * playback): it can be asked. */
int portLauncherBackAvailable(void);
/* Between two of the game's frames (vi_aurora.c's retrace, the frame shown and
 * the next not begun): the steps above; on iOS and Android the launcher
 * itself. */
void portLauncherBackFrame(void);

/* The launcher (launcher_ui.cpp): this start is Back to the launcher's, so it
 * shows whatever "Skip the launcher" says. Read once ($TTYD_LAUNCHER_BACK
 * taken out of the environment, so a version started from the launcher
 * doesn't inherit it). */
int portLauncherBackForced(void);

#ifdef __cplusplus
}
#endif

#endif
