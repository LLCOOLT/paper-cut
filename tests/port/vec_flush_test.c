/* vec_flush_test: the vector lengths of a backend's mtx library against the console's flush.
 *
 * The console runs its FPU in non-IEEE mode, which flushes a denormal single to zero, so a vector
 * whose squared length is denormal has the length 0 there, and the game's `if (!dist)` checks see
 * a stop. Aurora's ppc_rsqrte once squared its estimate of such a square in single precision
 * (overflow: infinity) and gave PSVECMag -inf; a Puni in the Great Tree went NaN from it.
 * port/patches/aurora/0025 flushes as the console does; headless (whose PS* are libs/dolsdk2004's
 * C_* versions) flushes the same squares.
 *
 * Checked: zero, denormal and normal inputs; on Aurora, ppc_rsqrte bit for bit against the
 * unflushed version over every non-denormal float, and PSVECMag / PSVECDistance bit for bit
 * against the unflushed formulas on random vectors of normal length; on headless, VECMag against
 * sqrtf of the unflushed square. */
#include <dolphin/mtx.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef TTYD_BACKEND_AURORA
#include <dolphin/ppc_math.h>
#endif

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t bits_of(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float float_of(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static uint32_t rng = 0x12345678u;
static uint32_t next_rand(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
/* A float of random sign and exponent between 2^lo and 2^hi. */
static float rand_float(int lo, int hi) {
    int e = lo + (int)(next_rand() % (uint32_t)(hi - lo + 1));
    float m = 1.0f + (float)(next_rand() & 0x7fffff) / 8388608.0f;
    float f = ldexpf(m, e);
    return (next_rand() & 1) ? -f : f;
}

#ifdef TTYD_BACKEND_AURORA
/* ppc_rsqrte and the length formulas as they were before 0025 (no flush). */
static float old_ppc_rsqrte(float x) {
    double rsqrt_d = frsqrte((double)x);
    float nwork0 = (float)(rsqrt_d * rsqrt_d);
    float nwork1 = (float)(rsqrt_d * 0.5);
    nwork0 = fmaf(-nwork0, x, 3.0f);
    return nwork0 * nwork1;
}
static float old_square_mag(const Vec* v) {
    float xx = v->x * v->x;
    float yy = v->y * v->y;
    float xx_zz = fmaf(v->z, v->z, xx);
    return fmaf(xx_zz, 1.0f, yy);
}
static float old_mag(const Vec* v) {
    float sqmag = old_square_mag(v);
    if (sqmag == 0.0f) {
        return 0.0f;
    }
    return sqmag * old_ppc_rsqrte(sqmag);
}
static float old_distance(const Vec* a, const Vec* b) {
    float dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
    float sqdist = (dx * dx + dy * dy) + dz * dz;
    if (sqdist == 0.0f) {
        return 0.0f;
    }
    return sqdist * old_ppc_rsqrte(sqdist);
}

static void check_rsqrte(void) {
    uint64_t compared = 0, differ = 0;
    uint32_t u;
    /* Every float that isn't denormal: the same bits as before. */
    for (u = 0;; u++) {
        if ((u & 0x7f800000u) != 0 || (u & 0x007fffffu) == 0) {
            float f = float_of(u);
            uint32_t a = bits_of(ppc_rsqrte(f)), b = bits_of(old_ppc_rsqrte(f));
            compared++;
            if (a != b && differ++ < 5) {
                printf("FAIL ppc_rsqrte(%08x) = %08x, was %08x\n", u, a, b);
            }
        }
        if (u == 0xffffffffu) {
            break;
        }
    }
    if (differ) {
        failures++;
    }
    printf("ppc_rsqrte: %llu non-denormal floats, %llu differ\n", (unsigned long long)compared,
           (unsigned long long)differ);

    /* Denormals: as 0 (frsqrte(+-0) = +-inf, then inf * 0 in the Newton step: NaN), never -inf. */
    CHECK(isnan(ppc_rsqrte(0.0f)), "ppc_rsqrte(0) = %g, the console's NaN", ppc_rsqrte(0.0f));
    CHECK(isnan(ppc_rsqrte(-0.0f)), "ppc_rsqrte(-0) = %g", ppc_rsqrte(-0.0f));
    CHECK(isnan(ppc_rsqrte(FLT_MIN / 2)), "ppc_rsqrte(denormal) = %g", ppc_rsqrte(FLT_MIN / 2));
    CHECK(isnan(ppc_rsqrte(float_of(1))), "ppc_rsqrte(smallest denormal) = %g", ppc_rsqrte(float_of(1)));
    CHECK(isnan(ppc_rsqrte(float_of(0x007fffffu))), "ppc_rsqrte(largest denormal) = %g",
          ppc_rsqrte(float_of(0x007fffffu)));
    CHECK(isinf(old_ppc_rsqrte(1e-39f)) && old_ppc_rsqrte(1e-39f) < 0, "the bug: old ppc_rsqrte(1e-39) = %g",
          old_ppc_rsqrte(1e-39f));
    /* frsqrte's specials, as the 750's: +inf -> +0, -inf and negatives -> NaN. */
    CHECK(frsqrte(INFINITY) == 0.0 && !signbit(frsqrte(INFINITY)), "frsqrte(+inf) = %g", frsqrte(INFINITY));
    CHECK(isnan(frsqrte(-INFINITY)), "frsqrte(-inf) = %g", frsqrte(-INFINITY));
    CHECK(isnan(frsqrte(-1.0)), "frsqrte(-1) = %g", frsqrte(-1.0));
    CHECK(isinf(frsqrte(0.0)) && frsqrte(0.0) > 0, "frsqrte(0) = %g", frsqrte(0.0));
    CHECK(isinf(frsqrte(-0.0)) && frsqrte(-0.0) < 0, "frsqrte(-0) = %g", frsqrte(-0.0));
    CHECK(fabsf(ppc_rsqrte(4.0f) - 0.5f) < 1e-6f, "ppc_rsqrte(4) = %.9g", ppc_rsqrte(4.0f));
    /* +inf: frsqrte gives +0, then 0 * inf in the Newton step: NaN, as on the console. */
    CHECK(isnan(ppc_rsqrte(INFINITY)), "ppc_rsqrte(+inf) = %g", ppc_rsqrte(INFINITY));
}

static void check_against_old(void) {
    int i, differ = 0, compared = 0;
    for (i = 0; i < 2000000; i++) {
        /* Components from 2^-70 to 2^60: squares from denormal to near FLT_MAX. */
        Vec a = {rand_float(-70, 60), rand_float(-70, 60), rand_float(-70, 60)};
        Vec b = {rand_float(-70, 60), rand_float(-70, 60), rand_float(-70, 60)};
        float sq = old_square_mag(&a);
        if (sq >= FLT_MIN && sq <= FLT_MAX) {
            compared++;
            if (bits_of(PSVECMag(&a)) != bits_of(old_mag(&a)) && differ++ < 5) {
                printf("FAIL PSVECMag(%g, %g, %g) = %.9g, was %.9g\n", a.x, a.y, a.z, PSVECMag(&a), old_mag(&a));
            }
            if (bits_of(PSVECSquareMag(&a)) != bits_of(sq) && differ++ < 5) {
                printf("FAIL PSVECSquareMag(%g, %g, %g) changed\n", a.x, a.y, a.z);
            }
        }
        {
            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            float sqd = (dx * dx + dy * dy) + dz * dz;
            if (sqd >= FLT_MIN && sqd <= FLT_MAX) {
                compared++;
                if (bits_of(PSVECDistance(&a, &b)) != bits_of(old_distance(&a, &b)) && differ++ < 5) {
                    printf("FAIL PSVECDistance changed\n");
                }
            }
        }
    }
    if (differ) {
        failures++;
    }
    printf("PSVECMag/PSVECDistance: %d normal-length cases, %d differ\n", compared, differ);
}
#else
static void check_against_old(void) {
    int i, differ = 0, compared = 0;
    for (i = 0; i < 2000000; i++) {
        Vec a = {rand_float(-70, 60), rand_float(-70, 60), rand_float(-70, 60)};
        float sq = a.z * a.z + ((a.x * a.x) + (a.y * a.y));
        if (sq >= FLT_MIN && sq <= FLT_MAX) {
            compared++;
            if (bits_of(PSVECMag(&a)) != bits_of(sqrtf(sq)) && differ++ < 5) {
                printf("FAIL VECMag(%g, %g, %g) = %.9g, was %.9g\n", a.x, a.y, a.z, PSVECMag(&a), sqrtf(sq));
            }
        }
    }
    if (differ) {
        failures++;
    }
    printf("VECMag: %d normal-length cases, %d differ\n", compared, differ);
}
#endif

static void check_lengths(void) {
    Vec zero = {0.0f, 0.0f, 0.0f};
    Vec tiny = {1e-20f, 0.0f, 0.0f};          /* square 1e-40: denormal */
    Vec tiny3 = {-3e-20f, 2e-20f, 4e-21f};    /* the Puni's velocity, nearly stopped */
    Vec edge = {1.0842022e-19f, 0.0f, 0.0f};  /* 2^-63: square 2^-126 = FLT_MIN, normal */
    Vec v345 = {3.0f, 4.0f, 0.0f};
    Vec unit;
    float m;

    CHECK(PSVECMag(&zero) == 0.0f, "VECMag(0) = %g", PSVECMag(&zero));
    CHECK(PSVECMag(&tiny) == 0.0f, "VECMag(1e-20, 0, 0) = %g, the console's 0", PSVECMag(&tiny));
    CHECK(PSVECMag(&tiny3) == 0.0f, "VECMag(Puni's velocity) = %g, the console's 0", PSVECMag(&tiny3));
    CHECK(PSVECSquareMag(&tiny) == 0.0f, "VECSquareMag(1e-20, 0, 0) = %g", PSVECSquareMag(&tiny));
    CHECK(PSVECDistance(&tiny, &zero) == 0.0f, "VECDistance(tiny) = %g", PSVECDistance(&tiny, &zero));
    CHECK(PSVECDistance(&tiny3, &tiny) == 0.0f, "VECDistance(tiny3, tiny) = %g", PSVECDistance(&tiny3, &tiny));
    CHECK(PSVECSquareDistance(&tiny, &zero) == 0.0f, "VECSquareDistance(tiny) = %g",
          PSVECSquareDistance(&tiny, &zero));
    m = PSVECMag(&edge);
    CHECK(m > 1.08e-19f && m < 1.09e-19f, "VECMag(2^-63, 0, 0) = %g, a normal square", m);
    m = PSVECMag(&v345);
    CHECK(fabsf(m - 5.0f) < 1e-5f, "VECMag(3, 4, 0) = %.9g", m);
    m = PSVECDistance(&v345, &zero);
    CHECK(fabsf(m - 5.0f) < 1e-5f, "VECDistance((3, 4, 0), 0) = %.9g", m);
    PSVECNormalize(&v345, &unit);
    CHECK(fabsf(unit.x - 0.6f) < 1e-5f && fabsf(unit.y - 0.8f) < 1e-5f && unit.z == 0.0f,
          "VECNormalize(3, 4, 0) = (%g, %g, %g)", unit.x, unit.y, unit.z);
#ifdef TTYD_BACKEND_AURORA
    /* The console's PSVECNormalize of a zero square: frsqrte(0) = inf, the Newton step NaN, so every
     * component NaN (0 * NaN). A denormal square is that zero. Before: (-inf, NaN, NaN) mixes. */
    PSVECNormalize(&zero, &unit);
    CHECK(isnan(unit.x) && isnan(unit.y) && isnan(unit.z), "PSVECNormalize(0) = (%g, %g, %g)", unit.x, unit.y,
          unit.z);
    PSVECNormalize(&tiny, &unit);
    CHECK(isnan(unit.x) && isnan(unit.y) && isnan(unit.z), "PSVECNormalize(tiny) = (%g, %g, %g)", unit.x, unit.y,
          unit.z);
    CHECK(isinf(old_mag(&tiny)) && old_mag(&tiny) < 0, "the bug: old PSVECMag(1e-20, 0, 0) = %g", old_mag(&tiny));
#endif
}

int main(void) {
#ifdef TTYD_BACKEND_AURORA
    check_rsqrte();
#endif
    check_lengths();
    check_against_old();
    if (failures) {
        printf("vec_flush_test: %d failures\n", failures);
        return 1;
    }
    printf("vec_flush_test: ok\n");
    return 0;
}
