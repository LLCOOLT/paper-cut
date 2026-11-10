#ifndef PORT_COOP_CAMERA_H
#define PORT_COOP_CAMERA_H

/* Co-op's camera and how far players 2 to 4 may roam (port/src/common/coop_camera_pc.c; Multiplayer >
 * Camera, setting "coop_camera": 0 Fit everyone, the default, 1 Follow Mario; docs/PLAYERS.md).
 *
 * On one screen (the couch) the field camera pans toward the group of characters this computer's
 * players walk and pulls back a little (at most a quarter more distance) so everyone stays in the
 * picture, inside the room's own camera road, which still places the camera; scenes and scripted
 * cameras keep the game's own. A player who walks to the picture's edge is held there; one the
 * picture left behind (Mario walked on) is walked back toward Mario, and comes back to his side only
 * when that gets nowhere. Online, a guest's screen follows the guest's own character in the host's
 * room, and the host's game lets that character roam the whole room. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PartyEntry;

/* Fit everyone (the setting's default) */
int portCoopCameraFit(void);

/* marioUpdateCam (mario_cam.c), after it set the camera's target from Mario: the target moved toward
 * the couch's group (Fit everyone), toward this guest's own character (online), or else toward Luigi
 * as Brothers mode always did (portBrothersCamera). y moves too unless followY is 0. */
void portCoopCamera(float* x, float* y, float* z, int followY);
/* cam3dMain (camdrv.c): the point whose floor picks the room's camera road (Mario's, as the game
 * has it), moved as the target was, so a guest's camera takes the road where its character is */
void portCoopCameraProbe(float* x, float* y, float* z);
/* cam3dMain, the road's camera placed: pulled back from at, eased, by up to a quarter, to fit the
 * couch's group; less where the road holds the camera off the target (the room's edge) */
void portCoopCameraZoom(float* posX, float* posY, float* posZ, float atX, float atY, float atZ, float targetX,
                        float targetZ);

/* --- how far they roam (second_player_pc.c, brothers_pc.c) --- */

/* second_player_pc.c: where the characters this computer's players walk now are (Luigi and the
 * helpers they move; not Mario, not an online guest's), at most max; the count */
int portCoopFramePoints(float (*out)[3], int max);

/* player (1 to 3: players 2 to 4) is an online guest's: its leash is the room */
int portCoopRoams(int player);
/* how far outside the picture (with its margins) the point is: > 0 outside, the largest overshoot in
 * the 608x480 picture's units, or past the reach from Mario in world units (the only check where no
 * picture is drawn: headless, replays) */
float portCoopEdgeExcess(float x, float y, float z);
/* slot's character (0 Luigi, 1 to 3 the helpers) has fallen: in deep water or a pit, in the air for
 * seconds, or far below where it last stood; call once a field frame while its player walks it */
int portCoopFallen(int slot, const struct PartyEntry* party);
/* the character a player walks was put back at Mario's side: its fall watch starts again */
void portCoopFallReset(int slot);
/* the camera yaw a player's stick turns by: an online guest's own camera's (it sends it with its
 * controller), else fallback (Mario's) */
float portCoopStickYaw(int player, float fallback);
/* how far a player's (port 1-3's) stick, at clamped magnitude mag, is pushed: 0 at the dead zone to 1 at its full
 * tilt (70, or a short stick's own learned full tilt: second_player_pc.c); their character's speed scale */
float portCoopStickTilt(int player, float mag);
/* whether that stick at mag counts as a run (Mario's past 55, or a short stick's full tilt) */
int portCoopStickRuns(int player, float mag);

/* --- online (statesync_coop_pc.inc) --- */

/* a guest: its camera's yaw for its controller's message (0: none to send) */
uint16_t portCoopCameraYawWire(void);
/* the host: seat's guest's camera yaw as its controller's message had it */
void portCoopCameraSeatYaw(int seat, uint16_t wire);
/* statesync_coop_pc.inc, a guest: where its own character is drawn now (seat 2 the partner or Luigi,
 * seats 3 and 4 the host's follower and companion); 0 when there's none in the host's room */
int portStateSyncCoopOwnPose(float* x, float* y, float* z);

#ifdef __cplusplus
}
#endif

#endif
