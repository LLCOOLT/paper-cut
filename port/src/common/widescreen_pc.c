/* Widescreen edge scale for full-screen quads (include/wide_x.h): 1 unless
 * the Aurora backend widens the frame (port/src/aurora/widescreen_aurora.c). */
float gPortWidescreenEdgeScale = 1.0f;

float portWidescreenEdgeScale(void) {
    return gPortWidescreenEdgeScale;
}

/* Widescreen HUD (settings: wide_hud): the field status bar's parts moved
 * out of the centred 4:3 area to the frame's edges (include/wide_x.h). */
int gPortWideHud = 0;
/* HUD scale (port/include/port/hud.h): the status bar's, about the top centre */
float gPortHudStatusScale = 1.0f;

float portWideHudDx(float xpos) {
    /* 2D is drawn into the centred 608-unit area; the frame's edges are
     * 304 * edgeScale from its centre, 304 * edgeScale / scale before the
     * status bar's scale */
    const float d = 304.0f * (gPortWidescreenEdgeScale / gPortHudStatusScale - 1.0f);
    if (gPortHudStatusScale > 1.0f && !gPortWideHud) {
        /* a larger status bar: its parts kept on screen, at the 4:3 edges */
        const float in = 304.0f * (1.0f / gPortHudStatusScale - 1.0f);
        return xpos < 380.0f ? -in : in;
    }
    if (!gPortWideHud || d <= 0.0f) {
        return 0.0f;
    }
    return xpos < 380.0f ? -d : d;
}
