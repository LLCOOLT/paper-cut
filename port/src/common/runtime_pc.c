/* Definitions the game takes from MSL / the Metrowerks runtime. */
#include <dolphin/types.h>

/* MSL's quiet NaN bit pattern, read as *(f32*)__float_nan. */
int __float_nan[1] = {0x7FFFFFFF};

/* DEMO library statistics switch (DEMOStats.c), off as in retail. */
u8 DemoStatEnable;

/* PPCHalt stops the CPU after a fatal error (US error handler): end the process. */
#include <stdlib.h>
void PPCHalt(void) {
    abort();
}
