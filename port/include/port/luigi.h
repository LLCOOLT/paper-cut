#ifndef PORT_LUIGI_H
#define PORT_LUIGI_H

/* Play as Luigi (Gameplay+ "playable_luigi", off by default;
 * port/src/common/luigi_pc.c, docs/PLAYABLE_LUIGI.md): Luigi's field model
 * (c_luigi) is drawn in place of Mario's pose while Mario's current animation
 * has a Luigi version (stand, idle, walk, run, jump, talk); otherwise Mario's
 * model shows, in the L Emblem's colours. Mario's poses still run as before:
 * Luigi's is a stand-in drawn instead of them. */

#include <dolphin/mtx.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_LUIGI_FIELD 0 /* Mario in the field (mario.c) */
#define PORT_LUIGI_BATTLE 1 /* Mario's unit's main part in battle (battle_disp.c) */

/* The option is on. */
int portLuigiOn(void);
/* Once a frame, after the game stepped Mario's pose poseId: Luigi stands in
 * for it (where = PORT_LUIGI_FIELD or _BATTLE) when allowed and Mario's
 * animation has a Luigi version; his pose follows Mario's. */
void portLuigiUpdate(int where, int poseId, int allowed);
/* animPoseDrawMtx, drawing Luigi's stand-in for poseId when there is one
 * (with poseId's material state as it is now). */
void portLuigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale);

#ifdef __cplusplus
}
#endif

#endif
