/* Widescreen edge scale for full-screen quads (include/wide_x.h): 1 unless
 * the Aurora backend widens the frame (port/src/aurora/widescreen_aurora.c). */
float gPortWidescreenEdgeScale = 1.0f;

float portWidescreenEdgeScale(void) {
    return gPortWidescreenEdgeScale;
}

/* Widescreen HUD (settings: wide_hud): the field status bar's parts moved
 * out of the centred 4:3 area to the frame's edges (include/wide_x.h). */
int gPortWideHud = 0;

float portWideHudDx(float xpos) {
    /* 2D is drawn into the centred 608-unit area; the frame's edges are
     * 304 * edgeScale from its centre */
    const float d = 304.0f * (gPortWidescreenEdgeScale - 1.0f);
    if (!gPortWideHud || d <= 0.0f) {
        return 0.0f;
    }
    return xpos < 380.0f ? -d : d;
}
