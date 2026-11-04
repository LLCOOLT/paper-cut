/* The GameCube's maths library on every host: MSL's fdlibm sources
 * (libs/msl/src/MSL_C.PPCEABI.bare.H.a: e_*.c, k_*.c, s_*.c, w_*.c), built
 * into the port with this header forced in front of each (CMakeLists.txt),
 * in place of their fdlibm.h. The game calls them for sin, cos, tan, atan,
 * atan2, asin, acos, pow, log and hypot (port_prefix.h), so every computer
 * gets the same results to the last bit, which online co-op's lockstep needs
 * (the system libraries' differ: Windows', Apple's, glibc's, bionic's), and
 * the same as the GameCube's. Exact operations (sqrt, floor, fabs, ldexp)
 * give the same everywhere and stay the system's.
 *
 * Here: fdlibm.h's declarations under ttyd_ names (no clash with the host's
 * libm), the words of a double read little-endian (fdlibm.h reads them
 * big-endian, as the GameCube's PowerPC is), and errno and NAN of their
 * own. */
#ifndef _FDLIBM_H
#define _FDLIBM_H /* the sources' own fdlibm.h is then empty */

#include <errno.h>
#include <math.h>

/* little-endian hosts (x86-64, arm64): the high word second */
#define __HI(x) *(1 + (int*)&x)
#define __LO(x) *(int*)&x
#define __HIp(x) *(1 + (int*)x)
#define __LOp(x) *(int*)x
_Static_assert(sizeof(int) == 4 && sizeof(double) == 8, "fdlibm's word access");

#define acos ttyd_acos
#define asin ttyd_asin
#define atan ttyd_atan
#define atan2 ttyd_atan2
#define copysign ttyd_copysign
#define cos ttyd_cos
#define floor ttyd_floor
#define fmod ttyd_fmod
#define frexp ttyd_frexp
#define hypot ttyd_hypot
#define ldexp ttyd_ldexp
#define log ttyd_log
#define modf ttyd_modf
#define pow ttyd_pow
#define sin ttyd_sin
#define sqrt ttyd_sqrt
#define tan ttyd_tan
#define scalbn ttyd_scalbn
#define __ieee754_sqrt ttyd___ieee754_sqrt
#define __ieee754_acos ttyd___ieee754_acos
#define __ieee754_asin ttyd___ieee754_asin
#define __ieee754_atan2 ttyd___ieee754_atan2
#define __ieee754_fmod ttyd___ieee754_fmod
#define __ieee754_hypot ttyd___ieee754_hypot
#define __ieee754_log ttyd___ieee754_log
#define __ieee754_pow ttyd___ieee754_pow
#define __ieee754_rem_pio2 ttyd___ieee754_rem_pio2
#define __kernel_sin ttyd___kernel_sin
#define __kernel_cos ttyd___kernel_cos
#define __kernel_tan ttyd___kernel_tan
#define __kernel_rem_pio2 ttyd___kernel_rem_pio2

double __ieee754_sqrt(double);
double __ieee754_acos(double);
double __ieee754_asin(double);
double __ieee754_atan2(double, double);
double __ieee754_fmod(double, double);
double __ieee754_hypot(double, double);
double __ieee754_log(double);
double __ieee754_pow(double, double);
int __ieee754_rem_pio2(double, double*);
double __kernel_sin(double, double, int);
double __kernel_cos(double, double);
double __kernel_tan(double, double, int);
int __kernel_rem_pio2(double*, double*, int, int, int, const int*);
double floor(double);
double scalbn(double, int);
double copysign(double, double);
double sqrt(double);
double atan(double);

/* fdlibm's errno: its own, the host's is a macro */
#undef errno
#define errno ttyd_fdlibm_errno
extern int errno;
#undef NAN
extern int __float_nan[];
#define NAN (*(float*)__float_nan)

#endif
