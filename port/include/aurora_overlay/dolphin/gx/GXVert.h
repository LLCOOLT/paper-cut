/* Aurora build: Aurora's <dolphin/gx/GXVert.h>, with GXWGFifo made a compile
 * error. Aurora has no write-gather pipe: its GXWGFifo is the GameCube's
 * hardware address (0xCC008000), so a store through it crashes. Vertex data
 * goes through the GXPosition / GXColor / GXTexCoord calls instead. */
#include_next <dolphin/gx/GXVert.h>

#ifndef PORT_AURORA_OVERLAY_GXVERT_H
#define PORT_AURORA_OVERLAY_GXVERT_H
#undef GXWGFifo
extern volatile PPCWGPipe portAuroraNoWGPipe
    __attribute__((unavailable("no write-gather pipe on Aurora: use the GXPosition/GXColor/GXTexCoord calls")));
#define GXWGFifo portAuroraNoWGPipe

/* $TTYD_NAN_CHECK (gx_compat_aurora.c): each position, normal and texture
 * coordinate the game sends checked for NaN and infinity, and reported with
 * the file and line that sent it ("[nan] ..."). Off: one load and a branch. */
#ifndef __cplusplus /* (the game's C; C++ and the mods' headers see Aurora's own calls) */
extern int portNanCheckOn;
void portNanVertex(const char* what, f32 x, f32 y, f32 z, const char* file, int line);
static inline int portNanBad(f32 v) { /* NaN or infinity, by its bits (whatever the float flags) */
    union { f32 f; u32 u; } b;
    b.f = v;
    return (b.u & 0x7F800000u) == 0x7F800000u;
}
static inline void portGXPosition3f32(f32 x, f32 y, f32 z, const char* file, int line) {
    if (portNanCheckOn && (portNanBad(x) | portNanBad(y) | portNanBad(z))) {
        portNanVertex("position", x, y, z, file, line);
    }
    GXPosition3f32(x, y, z);
}
static inline void portGXPosition2f32(f32 x, f32 y, const char* file, int line) {
    if (portNanCheckOn && (portNanBad(x) | portNanBad(y))) {
        portNanVertex("position", x, y, 0.0f, file, line);
    }
    GXPosition2f32(x, y);
}
static inline void portGXNormal3f32(f32 x, f32 y, f32 z, const char* file, int line) {
    if (portNanCheckOn && (portNanBad(x) | portNanBad(y) | portNanBad(z))) {
        portNanVertex("normal", x, y, z, file, line);
    }
    GXNormal3f32(x, y, z);
}
static inline void portGXTexCoord2f32(f32 u, f32 v, const char* file, int line) {
    if (portNanCheckOn && (portNanBad(u) | portNanBad(v))) {
        portNanVertex("texcoord", u, v, 0.0f, file, line);
    }
    GXTexCoord2f32(u, v);
}
#define GXPosition3f32(x, y, z) portGXPosition3f32((x), (y), (z), __FILE__, __LINE__)
#define GXPosition2f32(x, y) portGXPosition2f32((x), (y), __FILE__, __LINE__)
#define GXNormal3f32(x, y, z) portGXNormal3f32((x), (y), (z), __FILE__, __LINE__)
#define GXTexCoord2f32(u, v) portGXTexCoord2f32((u), (v), __FILE__, __LINE__)
#endif
#endif
