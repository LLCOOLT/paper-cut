#ifndef PORT_PAPER_H
#define PORT_PAPER_H

/* Paper looks (port/src/common/paper_pc.c, settings in
 * port/settings/features/paper_visuals.cpp, docs/PAPER_LOOKS.md): draw-only
 * options for the paper characters. None of them changes the game's state or
 * takes its random numbers, so online lockstep is untouched.
 *   paper_shadows  each character casts a shadow shaped like its cut-out, laid
 *                  on the ground its round shadow was on and stretched away
 *                  from the nearest lamp (else from above the camera's left,
 *                  as a paper theatre's footlights), instead of the round one
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
/* Once a frame (vi_aurora.c's endFrame): the settings to the backend, and the
 * frame's characters for the next frame's shadows. */
void portPaperFrame(void);

/* The backend (port/src/aurora/paper_aurora.c; headless.c's stubs): the next
 * draws as translucent shadows (on) or as themselves; smooth edges on/off. */
void portPaperGxShadow(int on);
void portPaperGxEdges(int on);

#ifdef __cplusplus
}
#endif

#endif
