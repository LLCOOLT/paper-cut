#ifndef PORT_PAPER_H
#define PORT_PAPER_H

/* Paper looks (port/src/common/paper_pc.c, settings in
 * port/settings/features/paper_visuals.cpp, docs/PAPER_LOOKS.md): draw-only
 * options for the paper characters. None of them changes the game's state or
 * takes its random numbers, so online lockstep is untouched.
 *   paper_shadows  each character casts a soft shadow shaped like its
 *                  cut-out, laid on the ground its round shadow was on (still
 *                  under them in a jump), instead of the round one: the
 *                  cut-out flattened onto the floor in front of its feet,
 *                  turned left or right by the map's lamps (else the stage
 *                  light), drawn as a few spread taps for a soft edge. 2
 *                  (High): longer, darker and sharper, with a contact shadow
 *                  at the feet
 *   paper_physics  0 off, 1 subtle, 2 lively: the cut-outs lean into fast
 *                  moves, flutter on quick turns and squash on hard landings
 *   paper_edges    smooth cut-out edges (Aurora's paper patch; with MSAA as
 *                  coverage, else faded into the blend) */

#include <dolphin/mtx.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ShadowEntry;

/* The characters' draw (animPoseDrawMtx through portLuigiDrawMtx and the
 * partners' party.c): the paper shadow first, then draw with the paper
 * physics' matrix. */
typedef void (*PortPaperDraw)(int poseId, Mtx mtx, int xluMode, float rotY, float scale);
void portPaperDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale, PortPaperDraw draw);
/* animdrv.c: the draw under way is a paper shadow's (it leaves the pose's
 * paper turn as it was: the game reads the turn, and the character's own draw
 * steps it) */
int portPaperShadowPass(void);
/* shadowdrv.c's shadowDisp on the 3D camera (the round shadows' turn, after
 * the ground): the shadows of the characters drawn so far this frame */
void portPaperShadowTurn(void);
/* shadowdrv.c: a round shadow left out, a paper one drawn for its owner */
int portPaperShadowSkip(const struct ShadowEntry* entry);
/* The light the paper shadows would fall from at (x, y, z): dir, a unit
 * direction pointing down along the light, in world space (the lamps' blend,
 * else the stage light). For the screen-space lighting's contact shadows
 * (port/src/aurora/rt_lighting_aurora.c). */
void portPaperSceneLight(float x, float y, float z, float* dir);
/* Once a frame (vi_aurora.c's endFrame): the settings to the backend, and the
 * frame's characters for the next frame's shadows. */
void portPaperFrame(void);

/* The backend (port/src/aurora/paper_aurora.c; headless.c's stubs): the next
 * draws as translucent shadows of this strength (0: as themselves), soft (0-1:
 * a penumbra widening and fading away from feetView, the feet in view space,
 * over length; feetView may be NULL when soft is 0); smooth edges on/off. */
void portPaperGxShadow(float strength, float soft, const float* feetView, float length);
void portPaperGxEdges(int on);

#ifdef __cplusplus
}
#endif

#endif
