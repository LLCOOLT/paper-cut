/* Widescreen edge scale for full-screen quads (include/wide_x.h): 1 unless
 * the Aurora backend widens the frame (port/src/aurora/widescreen_aurora.c). */
float gPortWidescreenEdgeScale = 1.0f;

float portWidescreenEdgeScale(void) {
    return gPortWidescreenEdgeScale;
}

/* Widescreen HUD at the edges (settings: wide_hud, on by default): the HUD's
 * parts moved out of the centred 4:3 area to the frame's edges
 * (include/wide_x.h). The frame is the picture as drawn: inside the safe area
 * when the picture keeps clear of a notch (port/src/aurora/layout_aurora.c),
 * narrowed where a scene's set ends (widescreen_aurora.c), so the HUD follows
 * both. At 4:3 with the HUD at its own size every shift here is 0. */
int gPortWideHud = 1;
/* HUD scale (port/include/port/hud.h): the status bar's, about the top centre */
float gPortHudStatusScale = 1.0f;

/* How far the status bar's left group moves left (the right group right), in
 * the bar's own units (before its scale); negative: in */
static float statusShift(void) {
    /* 2D is drawn into the centred 608-unit area; the frame's edges are
     * 304 * edgeScale from its centre, 304 * edgeScale / scale before the
     * status bar's scale */
    const float d = 304.0f * (gPortWidescreenEdgeScale / gPortHudStatusScale - 1.0f);
    if (!gPortWideHud) {
        /* a larger status bar: its parts kept on screen, at the 4:3 edges */
        return gPortHudStatusScale > 1.0f ? 304.0f * (1.0f / gPortHudStatusScale - 1.0f) : 0.0f;
    }
    /* at the frame's edges: out on a wider frame, in for a bar larger than
     * the frame (a smaller one at 4:3 stays as the game lays it out) */
    return d > 0.0f || gPortHudStatusScale > 1.0f ? d : 0.0f;
}

float portWideHudDx(float xpos) {
    const float d = statusShift();
    return xpos < 380.0f ? -d : d;
}

/* The Star Power row (its star, banner and gauge, under FP and the Star
 * Points): spread out, it goes with the right group (Star Points, coins),
 * clear of the partner's HP row at the left, its banner's right end under the
 * Star Points and coins banner's (80 units further right than the game has
 * it), so the right group keeps one edge; otherwise (4:3, the setting off, a
 * larger bar kept on screen) it stays with the left group, as the game lays
 * it out. */
float portWideHudSpDx(void) {
    const float d = statusShift();
    return gPortWideHud && d > 0.0f ? d + 80.0f : -d;
}

/* The BINGO cards' small row (battle_break_slot.c: at the 4:3 frame's right, y 82-114 from the top, right of
 * the Star Power row where the game lays it out): with that row spread to the right group (portWideHudSpDx),
 * its gauge can lie over the cards (the bar lower on the screen, as it slides in); then they go down under the
 * gauge as drawn (the rows' bottom, statuswindow.c, reaches about 12 units past the gauge's foot), else 0. */
float portHudStatusBarBottom(void); /* statuswindow.c */
float portWideHudBingoDy(void) {
    const float d = statusShift();
    float dy;
    if (!(gPortWideHud && d > 0.0f)) {
        return 0.0f;
    }
    dy = portHudStatusBarBottom() - 12.0f + 3.0f - 82.0f;
    return dy > 0.0f ? dy : 0.0f;
}

/* A HUD part outside the status bar, drawn in the 2D camera's units at the
 * game's own size (the battle's audience count and earned Star Points, the
 * countdown): moved to the frame's left (right 0) or right edge by as much as
 * the frame is wider than 4:3 */
float portWideHudEdgeDx(int right) {
    const float d = 304.0f * (gPortWidescreenEdgeScale - 1.0f);
    if (!gPortWideHud || d <= 0.0f) {
        return 0.0f;
    }
    return right ? d : -d;
}
