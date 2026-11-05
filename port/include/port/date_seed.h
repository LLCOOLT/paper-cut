#ifndef PORT_DATE_SEED_H
#define PORT_DATE_SEED_H

/* The day's seed, the same for everyone on the same day (UTC, so players in
 * different time zones share it), and a small random number generator of its
 * own to draw from it. Used by the daily shop specials and the Paper Tower's
 * day climb (port/include/port/postgame.h), the daily battle puzzle, the
 * daily seed and the raid's week. The day is the lockstep clock's
 * (portLockstepDay, os_time_aurora.c): online the host's, in a replay the
 * recording's, so every game of a session (and a replay played back on
 * another day) draws the same; the real UTC date otherwise. Never time():
 * two players' clocks differ near midnight. Header only, plain C, never the
 * game's rand(): drawing from it changes nothing the game draws. */

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned portLockstepDay(void); /* port/include/port/platform.h */

/* Days since 1970-01-01, UTC, by the lockstep clock. */
static inline unsigned portDateDay(void) {
    return portLockstepDay();
}

/* A day's seed (never 0): the day number mixed (a 32-bit integer hash). */
static inline unsigned portDateSeedOf(unsigned day) {
    unsigned x = day * 0x9E3779B9u + 0x7F4A7C15u;
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return x != 0 ? x : 1u;
}

/* Today's seed. */
static inline unsigned portDateSeed(void) {
    return portDateSeedOf(portDateDay());
}

/* The date of a day, "YYYY-MM-DD" (UTC), into out (at least 11 bytes). */
static inline void portDateText(unsigned day, char* out, size_t size) {
    const time_t t = (time_t)day * 86400;
    struct tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    strftime(out, size, "%Y-%m-%d", &tm);
}

/* A generator from a seed (xorshift32): the same seed, the same draws. */
typedef struct PortSeedRand {
    unsigned state;
} PortSeedRand;

static inline void portSeedRandInit(PortSeedRand* r, unsigned seed) {
    r->state = portDateSeedOf(seed); /* mixed, so nearby seeds draw differently */
}

/* A draw in [0, n) (0 when n is 0). */
static inline unsigned portSeedRandNext(PortSeedRand* r, unsigned n) {
    unsigned x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    return n != 0 ? x % n : 0u;
}

#ifdef __cplusplus
}
#endif

#endif
