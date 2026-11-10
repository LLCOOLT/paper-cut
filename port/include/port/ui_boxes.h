#pragma once
/* Tests: does the port's 2D layer draw one box over another? (port/src/common/ui_boxes_pc.c)
 *
 * $TTYD_UI_OVERLAP=1: the sites that draw boxes of their own into the 2D layer (the players' cards and
 * notes, the co-op notices, the Start menu's help box, the first-time tips) put each box they draw this
 * frame here, in the 2D units (608 x 480 from the centre, y up) under a name "<group>:<what>". Once a
 * frame, two boxes of different groups that cross by more than a few units are logged, each pair once:
 *   [ui-overlap] <name> over <name> (<w> x <h>)
 * A scenario's log_must_not "\\[ui-overlap\\]" then fails on it. Off by default: nothing is kept or
 * measured without it. Draw-only: never the game's state or its random numbers. */
#ifdef __cplusplus
extern "C" {
#endif

/* $TTYD_UI_OVERLAP is set */
int portUiBoxOn(void);
/* a box drawn this frame: x0 < x1, y0 < y1 (any order is put right) */
void portUiBox(const char* name, float x0, float y0, float x1, float y1);
/* once a frame (system.c's pad read): this frame's boxes compared, then let go */
void portUiBoxFrame(void);

#ifdef __cplusplus
}
#endif
