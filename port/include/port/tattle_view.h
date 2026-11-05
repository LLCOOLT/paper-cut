#ifndef PORT_TATTLE_VIEW_H
#define PORT_TATTLE_VIEW_H

/* The Tattle log's model viewer (port/src/aurora/tattle_view_aurora.c): in the Journal's Tattle log, with an
 * enemy's card open, the stick (or the C-stick) turns the enemy, L and R held bring it in and out, A puts it back
 * as it was; its idle animation plays. The card's portrait (src/window/win_log.c's capture) is drawn again each
 * frame for it. Only how the card looks: not online or in a replay (the menu runs on both sides there), not in
 * battle (Goombella's Tattle), not for Smorg (its many parts are scattered at random). Aurora builds only. */

#ifdef __cplusplus
extern "C" {
#endif

/* win_log.c: the Journal's card opened (on 1) or closed (0) for enemy kind id */
void portTattleViewSet(int on, int id);
/* the viewer is on (not in battle, not online, not a replay, not Smorg) */
int portTattleViewOn(void);
/* once a frame while the card is open: the controller (keyGetButton, the stick and C-stick x) */
void portTattleViewInput(unsigned int buttons, unsigned int pressed, signed char stickX, signed char substickX);
/* the turn (radians) and the zoom (times) the portrait is drawn at */
void portTattleViewGet(float* yaw, float* zoom);
/* win_log.c's capture, a tw x th picture: the scissor the zoomed model is drawn in, the frame's inside */
void portTattleViewScissor(unsigned int tw, unsigned int th, unsigned int* x, unsigned int* y, unsigned int* w,
                           unsigned int* h);
/* the frame's border on the card (its 216 x 160 picture): win_log.c draws the portrait, and the soft paper
 * edge round it (copies of it up to 1.39 times as big), only inside it, and the frame alone on it */
void portTattleViewBorder(float* left, float* top, float* right, float* bottom);
/* the line under the card saying how (the game's font and encoding) */
const char* portTattleViewHint(void);

#ifdef __cplusplus
}
#endif

#endif
