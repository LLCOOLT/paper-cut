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
 * right (0 unless the setting is on) */
float portWideHudDx(float xpos);
#define WIDE_HUD_DX(xpos) portWideHudDx(xpos)
#else
#define WIDE_X(x) x
#define WIDE_HUD_DX(xpos) 0
#endif

#endif
