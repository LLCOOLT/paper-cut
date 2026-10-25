/* Widescreen edge scale for full-screen quads (include/wide_x.h): 1 unless
 * the Aurora backend widens the frame (port/src/aurora/widescreen_aurora.c). */
float gPortWidescreenEdgeScale = 1.0f;

float portWidescreenEdgeScale(void) {
    return gPortWidescreenEdgeScale;
}
