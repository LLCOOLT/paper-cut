/* Aurora build: Aurora's <dolphin/gx/GXTransform.h>, with GXProject giving
 * finite screen points only (port/src/aurora/gx_compat_aurora.c): a point on
 * the camera's plane divides by zero, and the game draws with what comes back
 * (a speech balloon's tail, a fade's circle). A NaN or infinite vertex is
 * dropped by most GPUs but drawn across the screen by some (Mesa's RADV).
 * In the game's C, a non-finite GXProject is reported with its file and line
 * ("[nan] ..."), and with $TTYD_NAN_CHECK so is every matrix it loads. */
#include_next <dolphin/gx/GXTransform.h>

#ifndef PORT_AURORA_OVERLAY_GXTRANSFORM_H
#define PORT_AURORA_OVERLAY_GXTRANSFORM_H
#ifdef __cplusplus
extern "C" {
#endif
void portGXProject(f32 x, f32 y, f32 z, const f32 mtx[3][4], const f32* pm, const f32* vp, f32* sx, f32* sy,
                   f32* sz);
#ifdef __cplusplus
}
#endif
#ifndef __cplusplus /* (the game's C; C++ and the mods' headers see the plain calls) */
void portGXProjectAt(f32 x, f32 y, f32 z, const f32 mtx[3][4], const f32* pm, const f32* vp, f32* sx, f32* sy,
                     f32* sz, const char* file, int line);
extern int portNanCheckOn;
void portNanMatrix(const char* what, const void* m, int rows, const char* file, int line);
static inline void portGXSetProjectionAt(const void* m, GXProjectionType type, const char* file, int line) {
    if (portNanCheckOn) {
        portNanMatrix("projection", m, 4, file, line);
    }
    GXSetProjection(m, type);
}
static inline void portGXLoadPosMtxImmAt(const void* m, u32 id, const char* file, int line) {
    if (portNanCheckOn) {
        portNanMatrix("position matrix", m, 3, file, line);
    }
    GXLoadPosMtxImm(m, id);
}
#define GXProject(x, y, z, mtx, pm, vp, sx, sy, sz) portGXProjectAt(x, y, z, mtx, pm, vp, sx, sy, sz, __FILE__, __LINE__)
#define GXSetProjection(m, type) portGXSetProjectionAt((m), (type), __FILE__, __LINE__)
#define GXLoadPosMtxImm(m, id) portGXLoadPosMtxImmAt((m), (id), __FILE__, __LINE__)
#else
#define GXProject portGXProject
#endif
#endif
