/* Texture upscaling (Graphics > Texture upscaling): the filter Aurora gives each
 * game texture to (AuroraSetTextureUpscale, port/patches/aurora/0015), the
 * port's own.
 *
 * Each source texel P becomes four, one toward each corner. A quarter is first
 * the bilinear blend of P with its neighbours toward that corner, H (across)
 * and V (up or down), and D (diagonal): 9:3:3:1. Where H and V are alike and P
 * and D are not, an edge runs between P and that corner: the quarter is pulled
 * toward the H-V side, by how much more P and D differ than H and V do, so a
 * slanted edge comes out a line rather than a staircase. Colours are blended
 * weighted by their alpha (premultiplied), so a cut-out's transparent texels
 * don't darken its edge. Edges clamp. 4x is 2x twice. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <aurora/upscale.h>
#include "port/display.h"
#include "port/settings.h"

typedef struct {
    float r, g, b, a; /* premultiplied */
} Texel;

static Texel load(const unsigned char* rgba, unsigned w, unsigned h, int x, int y) {
    const unsigned char* p;
    Texel t;
    x = x < 0 ? 0 : x >= (int)w ? (int)w - 1 : x;
    y = y < 0 ? 0 : y >= (int)h ? (int)h - 1 : y;
    p = rgba + ((size_t)y * w + (size_t)x) * 4;
    t.a = p[3] / 255.0f;
    t.r = p[0] / 255.0f * t.a;
    t.g = p[1] / 255.0f * t.a;
    t.b = p[2] / 255.0f * t.a;
    return t;
}

static float diff(Texel x, Texel y) {
    float d = 0.0f, v;
    v = x.r - y.r;
    d += v < 0 ? -v : v;
    v = x.g - y.g;
    d += v < 0 ? -v : v;
    v = x.b - y.b;
    d += v < 0 ? -v : v;
    v = x.a - y.a;
    d += v < 0 ? -v : v;
    return d;
}

static Texel mix4(Texel p, float wp, Texel h, float wh, Texel v, float wv, Texel d, float wd) {
    Texel t;
    t.r = p.r * wp + h.r * wh + v.r * wv + d.r * wd;
    t.g = p.g * wp + h.g * wh + v.g * wv + d.g * wd;
    t.b = p.b * wp + h.b * wh + v.b * wv + d.b * wd;
    t.a = p.a * wp + h.a * wh + v.a * wv + d.a * wd;
    return t;
}

static unsigned char unit(float v) {
    v = v * 255.0f + 0.5f;
    return (unsigned char)(v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v);
}

static void store(unsigned char* out, Texel t) {
    if (t.a <= 1.0f / 512.0f) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    out[0] = unit(t.r / t.a);
    out[1] = unit(t.g / t.a);
    out[2] = unit(t.b / t.a);
    out[3] = unit(t.a);
}

/* one output quarter (P's toward the corner of H, V and D) */
static void quarter(Texel p, Texel hh, Texel vv, Texel dd, unsigned char* out) {
    const float dPD = diff(p, dd), dHV = diff(hh, vv);
    Texel t = mix4(p, 9.0f / 16, hh, 3.0f / 16, vv, 3.0f / 16, dd, 1.0f / 16);
    if (dPD > 0.08f && dHV < dPD * 0.5f) {
        /* an edge across this corner: halfway from P to the H-V side at most */
        const float k = (1.0f - 2.0f * dHV / dPD) * 0.5f;
        const Texel edge = mix4(p, 0.5f, hh, 0.25f, vv, 0.25f, dd, 0.0f);
        t = mix4(t, 1.0f - k, edge, k, dd, 0.0f, dd, 0.0f);
    }
    store(out, t);
}

/* (no memory for the texels below: each one loaded where it is used) */
static void upscale2xDirect(const unsigned char* rgba, unsigned w, unsigned h, unsigned char* out) {
    const unsigned W = w * 2;
    int x, y, q;
    for (y = 0; y < (int)h; y++) {
        for (x = 0; x < (int)w; x++) {
            const Texel p = load(rgba, w, h, x, y);
            for (q = 0; q < 4; q++) {
                const int dx = q & 1, dy = q >> 1;
                const int sx = dx ? 1 : -1, sy = dy ? 1 : -1;
                quarter(p, load(rgba, w, h, x + sx, y), load(rgba, w, h, x, y + sy), load(rgba, w, h, x + sx, y + sy),
                        out + ((size_t)(y * 2 + dy) * W + (size_t)(x * 2 + dx)) * 4);
            }
        }
    }
}

/* The texels loaded once each (premultiplied, with a clamped border a texel wide) rather than once for each of
 * the 16 quarters that read them: the same values, so the same picture, several times faster */
static void upscale2x(const unsigned char* rgba, unsigned w, unsigned h, unsigned char* out) {
    const unsigned W = w * 2;
    const size_t stride = (size_t)w + 2;
    Texel* tex = malloc(stride * ((size_t)h + 2) * sizeof(Texel));
    int x, y;
    if (tex == NULL) {
        upscale2xDirect(rgba, w, h, out);
        return;
    }
    for (y = -1; y <= (int)h; y++) {
        for (x = -1; x <= (int)w; x++) {
            tex[(size_t)(y + 1) * stride + (size_t)(x + 1)] = load(rgba, w, h, x, y);
        }
    }
    for (y = 0; y < (int)h; y++) {
        const Texel* up = tex + (size_t)y * stride + 1;
        const Texel* row = up + stride;
        const Texel* down = row + stride;
        unsigned char* top = out + (size_t)(y * 2) * W * 4;
        unsigned char* bottom = top + (size_t)W * 4;
        for (x = 0; x < (int)w; x++) {
            const Texel p = row[x];
            quarter(p, row[x - 1], up[x], up[x - 1], top + (size_t)(x * 2) * 4);
            quarter(p, row[x + 1], up[x], up[x + 1], top + (size_t)(x * 2 + 1) * 4);
            quarter(p, row[x - 1], down[x], down[x - 1], bottom + (size_t)(x * 2) * 4);
            quarter(p, row[x + 1], down[x], down[x + 1], bottom + (size_t)(x * 2 + 1) * 4);
        }
    }
    free(tex);
}

static void upscaleOnce(const unsigned char* rgba, unsigned width, unsigned height, unsigned factor,
                        unsigned char* out) {
    unsigned char* half;
    if (factor == 2) {
        upscale2x(rgba, width, height, out);
        return;
    }
    half = malloc((size_t)width * height * 16);
    if (half == NULL) {
        memset(out, 0, (size_t)width * height * factor * factor * 4);
        return;
    }
    upscale2x(rgba, width, height, half);
    upscale2x(half, width * 2, height * 2, out);
    free(half);
}

/* $TTYD_UPSCALE_LOG (tests, perf): the filter's work each frame, from whichever thread ran it: textures, texels
 * in, time, and how many of them had been upscaled before in this run (the same picture again) */
static int sLog = -1;
static SDL_AtomicInt sCalls, sAgain, sTexels, sMicros, sMaxMicros;
static SDL_SpinLock sSeenLock;
#define SEEN_SLOTS 8192
static Uint64 sSeen[SEEN_SLOTS];

static int seenBefore(const unsigned char* rgba, unsigned width, unsigned height) {
    Uint64 h = 1469598103934665603ull ^ ((Uint64)width << 32 | height);
    const size_t n = (size_t)width * height * 4;
    size_t i;
    unsigned slot, k;
    int again = 0;
    for (i = 0; i < n; i += 8) {
        Uint64 v = 0;
        memcpy(&v, rgba + i, n - i < 8 ? n - i : 8);
        h = (h ^ v) * 1099511628211ull;
        h ^= h >> 29;
    }
    h |= 1;
    SDL_LockSpinlock(&sSeenLock);
    for (k = 0, slot = (unsigned)(h % SEEN_SLOTS); k < SEEN_SLOTS; k++, slot = (slot + 1) % SEEN_SLOTS) {
        if (sSeen[slot] == h) {
            again = 1;
            break;
        }
        if (sSeen[slot] == 0) {
            sSeen[slot] = h;
            break;
        }
    }
    SDL_UnlockSpinlock(&sSeenLock);
    return again;
}

static void upscale(const unsigned char* rgba, unsigned width, unsigned height, unsigned factor,
                    unsigned char* out) {
    Uint64 from;
    int us, prev;
    if (sLog <= 0) {
        upscaleOnce(rgba, width, height, factor, out);
        return;
    }
    if (seenBefore(rgba, width, height)) {
        SDL_AddAtomicInt(&sAgain, 1);
    }
    from = SDL_GetPerformanceCounter();
    upscaleOnce(rgba, width, height, factor, out);
    us = (int)((SDL_GetPerformanceCounter() - from) * 1000000 / SDL_GetPerformanceFrequency());
    SDL_AddAtomicInt(&sCalls, 1);
    SDL_AddAtomicInt(&sTexels, (int)(width * height));
    SDL_AddAtomicInt(&sMicros, us);
    do {
        prev = SDL_GetAtomicInt(&sMaxMicros);
    } while (us > prev && !SDL_CompareAndSwapAtomicInt(&sMaxMicros, prev, us));
}

void portTextureUpscaleFrame(float workMs) {
    static unsigned long long sFrame;
    static double sTotalMs;
    static int sTotalCalls, sTotalAgain;
    static float sPeakMs;
    static double sSyncMs;
    static int sOver4;
    int calls, again, texels, us, maxUs;
    AuroraTextureUpscaleStats a;
    sFrame++;
    if (sLog <= 0) {
        return;
    }
    calls = SDL_SetAtomicInt(&sCalls, 0);
    again = SDL_SetAtomicInt(&sAgain, 0);
    texels = SDL_SetAtomicInt(&sTexels, 0);
    us = SDL_SetAtomicInt(&sMicros, 0);
    maxUs = SDL_SetAtomicInt(&sMaxMicros, 0);
    AuroraGetTextureUpscaleStats(&a);
    sTotalMs += us / 1000.0;
    sTotalCalls += calls;
    sTotalAgain += again;
    if (a.syncMicros / 1000.0f > sPeakMs) {
        sPeakMs = a.syncMicros / 1000.0f;
    }
    sSyncMs += a.syncMicros / 1000.0;
    sOver4 += a.syncMicros >= 4000;
    if (calls > 0 || a.syncMicros >= 50 || a.queued > 0 || a.published > 0) {
        printf("[upscale] frame %llu: filter %d (%d again) %d texels %.2f ms (longest %.2f), on the frame's thread "
               "%.2f ms, queued %u published %u (%u KB) waiting %u, skipped as changing %u, work %.2f ms\n",
               sFrame, calls, again, texels, us / 1000.0, maxUs / 1000.0, a.syncMicros / 1000.0, a.queued,
               a.published, (unsigned)(a.publishedBytes >> 10), a.waiting, a.dynamicSkips, (double)workMs);
    }
    if (sFrame % 600 == 0) {
        printf("[upscale] total at frame %llu: filter %d (%d again), %.1f ms; on the frame's thread %.1f ms, "
               "longest %.2f ms, %d frames 4 ms or more\n",
               sFrame, sTotalCalls, sTotalAgain, sTotalMs, sSyncMs, (double)sPeakMs, sOver4);
    }
    fflush(stdout);
}

void portTextureUpscaleSet(int choice) {
    static const unsigned kFactor[] = {1, 2, 4};
    choice = choice < 0 ? 0 : choice > 2 ? 2 : choice;
    AuroraSetTextureUpscale(kFactor[choice], choice != 0 ? upscale : NULL);
}

void portTextureUpscaleStart(void) {
    if (sLog < 0) {
        const char* v = SDL_getenv("TTYD_UPSCALE_LOG");
        sLog = v != NULL && v[0] != '\0' && v[0] != '0';
    }
    /* Tests on the virtual clock ($TTYD_FAST) wait for each frame's upscaled textures, so their captures are the
     * same whatever the machine's speed; $TTYD_UPSCALE_ASYNC=1 plays them as a player's game does */
    if (SDL_getenv("TTYD_FAST") != NULL && SDL_getenv("TTYD_UPSCALE_ASYNC") == NULL) {
        AuroraSetTextureUpscaleWait(1);
    }
    portTextureUpscaleSet(*portSettingsDeclare("texture_upscale", 0, 0, 2));
}
