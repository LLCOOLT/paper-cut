#ifndef PORT_PING_H
#define PORT_PING_H
/* Co-op pings (port/src/common/ping_pc.c, port/settings/features/pings.cpp):
 * either player marks a spot or an enemy with a paper arrow that both see.
 *
 * - Controller: hold the Ping binding (Controls > Bindings; Share/Capture by
 *   default, G on the keyboard), aim with the C-stick (a cursor from the
 *   player's own character: Mario for player 1, the partner for player 2),
 *   let go to place it. A quick press marks where the character stands, or
 *   the enemy nearest in front of it.
 * - Mouse: a click on the picture with no menu up (or the middle button at
 *   any time). Touch: a tap on the picture away from the on-screen controls.
 *
 * A ping is input, not game state: it travels in the controller's own
 * state, so online lockstep carries it to the other game for the same frame,
 * replays record it and watchers see it. It rides in bits the game never
 * reads (PADStatus.button's 0x0080, analogA and analogB, the top four bits
 * of Aurora's extButton): the spot's x and z in steps of 4 units. Right
 * after the pad read (after online's exchange and the replay's recording)
 * those bits are taken off again, so the game gets exactly the input it
 * would have without the ping, and the fingerprints are untouched. The
 * arrow is drawn without the game's font (no random numbers drawn). */

#ifdef __cplusplus
extern "C" {
#endif

struct PADStatus;

/* src/sdk/DEMOPad.c: this game's own pings put in its players' pads (before
 * online co-op's exchange), then everyone's taken off them (after the
 * replay's) */
void portPingPad(struct PADStatus* pads);
void portPingTakePad(struct PADStatus* pads);
/* src/system.c, each frame: the arrows' ages, the enemies they follow, the
 * drawing */
void portPingFrame(void);

/* a click or tap at (nx, ny), 0-1 across the picture: player 1's ping
 * there (pings.cpp, touch_overlay.cpp) */
void portPingClick(float nx, float ny);
/* the setting "coop_pings" (on by default) */
int portPingsOn(void);

#ifdef __cplusplus
}
#endif

#endif
