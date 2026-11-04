/* Definitions the game takes from MSL / the Metrowerks runtime. */
#include <dolphin/types.h>

/* MSL's quiet NaN bit pattern, read as *(f32*)__float_nan. */
int __float_nan[1] = {0x7FFFFFFF};

/* The GameCube's rand (MSL: libs/msl/src/MSL_C.PPCEABI.bare.H.a/rand.c),
 * as ttyd_rand (port_prefix.h renames the game's rand): the GameCube's
 * sequence on every host (the C libraries' differ), and its state can be put
 * back (the frames Aurora draws between the game's draw text, which shakes
 * its glyphs with irand). */
static u32 sRandNext = 1;

int ttyd_rand(void) {
    sRandNext = sRandNext * 1103515245u + 12345u;
    return (int)((sRandNext >> 16) & 0x7FFF);
}

void ttyd_srand(unsigned int seed) {
    sRandNext = seed;
}

/* MSL's fdlibm sets errno on a domain error (port/include/port/fdlibm_pc.h):
 * its own, nothing reads it */
int ttyd_fdlibm_errno;

u32 portRandState(void) {
    return sRandNext;
}

void portRandSetState(u32 state) {
    sRandNext = state;
}

/* GQR5 as the game last set it (hwaccess.h HW_SET_GQR5): OSInitFastCast's
 * signed 16-bit type with scale 0 until then. */
u32 portGQR5 = 0x00070007;

/* DEMO library statistics switch (DEMOStats.c), off as in retail. */
u8 DemoStatEnable;

/* PPCHalt stops the CPU after a fatal error (US error handler): end the process. */
#include <stdlib.h>
void PPCHalt(void) {
    abort();
}
