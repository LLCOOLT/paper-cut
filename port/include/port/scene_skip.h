#ifndef PORT_SCENE_SKIP_H
#define PORT_SCENE_SKIP_H

/* Skip any scene, and fly through text (port/src/common/scene_skip_pc.c; the
 * settings' rows and the ring: port/settings/features/scene_skip.cpp). Both
 * off by default: with them off the game is the original.
 *
 * "skip_any_scene": while a scene runs (an event holds Mario: no control, in
 * the field, no menu, choice or name entry open, and no script reading the
 * pad), holding the game's START (a controller's Start/+, the keyboard's key
 * for it, the on-screen START) fills a ring; full, the scene runs on to its
 * end as fast as it can, undrawn and silent: its scripts run, so whatever it
 * gives or sets is given and set. Its text pages turn by themselves (the A a
 * player would press). It stops, back to normal, at a choice or a menu (never
 * answered for the player), a battle (which starts as ever), a name entry, a
 * script that reads the pad (a minigame, a command), and when Mario has
 * control again. B pressed while it runs (any pad) stops it there, the scene
 * playing on as ever; that B isn't passed on. Not online, not in replays.
 *
 * "text_hold_skip": B held in a conversation (in the field, Mario held by it)
 * turns its pages as fast as they print, up to the next choice. A tap of B is
 * still the original's (the page at once, or the next). */

#ifdef __cplusplus
extern "C" {
#endif

/* makeKey (system.c), once a frame after the pads are read: the hold, the
 * skip's state, and the presses it adds (gp->button*, port 0). */
void portSceneSkipFrame(void);
/* A script (evt_key_get_*) or a minigame's own code read the pad: the scene
 * listens to the player, so it isn't skipped (and a skip under way stops). */
void portSceneSkipListening(void);
/* A scene's own "press a button to go on" outside the message windows (the
 * opening's title logo, muj_20; a coin reward's window, evt_sub.c): 1 when a
 * skip under way presses it this frame. */
int portSceneSkipAdvance(void);

/* Aurora, while a skip runs: the game's speed (speed_aurora.c; 100 when not
 * skipping), whether this frame goes undrawn (battery_aurora.c's undrawn
 * path), and the sound muted (settings_aurora.c's master gain). */
int portSceneSkipSpeedPct(void);
int portSceneSkipUndrawn(void);
int portSceneSkipMuted(void);

/* The ring (scene_skip.cpp): 0 none; 1 START held in a scene (fill: 0..1);
 * 2 skipping. */
int portSceneSkipRing(float* fill);

#ifdef __cplusplus
}
#endif

#endif
