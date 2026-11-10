#ifndef WIDE_X_H
#define WIDE_X_H

/* PC port, widescreen: the x of a full-screen quad's edges, drawn for the
 * 608-wide frame, moved out to the edges of the widened frame
 * (port/include/port/widescreen.h). WIDE_X for x centred on 0 (-304..304).
 * The GameCube build gets the same tokens. */
#ifdef TARGET_PC
float portWidescreenEdgeScale(void);
#define WIDE_X(x) ((x) * portWidescreenEdgeScale())
/* Widescreen HUD: how far a status bar part at xpos (its left-based x,
 * 0-608) moves toward its edge: left of the star points, left; the rest,
 * right (0 at 4:3 with the HUD at its own size, and with the setting off).
 * WIDE_HUD_SP_DX: the Star Power row's (with the right group when spread).
 * WIDE_HUD_EDGE_DX(right): a HUD part outside the status bar at the game's
 * own size, to the left (0) or right edge (port/src/common/widescreen_pc.c) */
float portWideHudDx(float xpos);
float portWideHudSpDx(void);
float portWideHudEdgeDx(int right);
/* WIDE_HUD_BINGO_DY(): how far the BINGO cards' small row goes down, under the
 * Star Power gauge spread over it (0 when it isn't) */
float portWideHudBingoDy(void);
/* the cards' row drawn this frame, its foot below units under the screen's top: the
 * port's boxes at the top go under it (statuswindow.c, port/overlay_place.h) */
void portHudCardsDrawn(float below);
#define WIDE_HUD_DX(xpos) portWideHudDx(xpos)
#define WIDE_HUD_SP_DX() portWideHudSpDx()
#define WIDE_HUD_BINGO_DY() portWideHudBingoDy()
#define WIDE_HUD_EDGE_DX(right) portWideHudEdgeDx(right)
/* EFB copies of the frame (port/src/aurora/widescreen_aurora.c). A copy of
 * 0,0,608,480 is of the whole frame, widened or not, at its real size.
 * portScreenSpaceBegin/End: around a draw of such a copy back over the frame
 * from a camera that squeezes x for widescreen or flips it (mirror mode), so
 * the draw covers the frame as the copy did. portWidescreenScaleX: that
 * squeeze (negative: flipped) for a projection the game computes itself.
 * portGameEfbX / portTexCopySrcGame: an x (a copy's rect) measured in the
 * game's own 4:3 picture, as the current camera draws it, in the frame.
 * All of it is the identity with the game's 4:3 frame and no mirror. */
float portWidescreenScaleX(void);
void portScreenSpaceBegin(void);
void portScreenSpaceEnd(void);
float portGameEfbX(float x);
void portTexCopySrcGame(unsigned short left, unsigned short top, unsigned short wd, unsigned short ht);
/* Mirror mode's words in the 3D (damage and recovery numbers, "Nice!",
 * speech bubbles): while the current camera is mirrored, model (a world
 * matrix whose origin is the words' middle) is turned so the words land where
 * the mirror puts that middle but read as they are, and the projection's flip
 * is held off until portMirrorReadableEnd (its widescreen squeeze kept).
 * Returns 0 (nothing done) when the camera isn't mirrored. */
int portMirrorReadable(float (*model)[4]);
void portMirrorReadableEnd(int on);
/* The same hold on the projection's flip alone, for a draw in the 2D
 * camera's own units on a flipped camera ("END OF CHAPTER"). */
int portMirrorHold(void);
/* The other way: a world model drawn from an unflipped camera (the 2D one)
 * over the flipped picture (Kiss Thief's Ms. Mowz over its grey copy) is
 * flipped as the 3D camera flips it, until portMirrorReadableEnd. */
int portMirrorAs3D(void);
/* A 3D view inside a picture (the opening storybook's zoom, dmo_00):
 * portWideFrameStretch is how much wider than the game's 4:3 the frame is
 * (1: not widened); portWideView3DStretch(k) gives the 3D camera an x scale
 * of 1/k from its next frame (0: back to the frame's), for a viewport the
 * game measured in its 4:3 picture widened by k; portWideSqueezeBegin/End
 * around a draw in the 2D camera's units on that camera puts the 2D
 * camera's squeeze back meanwhile (nests). All of it does nothing with the
 * game's 4:3 frame. */
float portWideFrameStretch(void);
void portWideView3DStretch(float stretch);
void portWideSqueezeBegin(void);
void portWideSqueezeEnd(void);
#else
#define WIDE_X(x) x
#define WIDE_HUD_DX(xpos) 0
#define WIDE_HUD_SP_DX() 0
#define WIDE_HUD_BINGO_DY() 0
#define WIDE_HUD_EDGE_DX(right) 0
#endif

#endif
