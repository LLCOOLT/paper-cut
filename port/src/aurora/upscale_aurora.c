/* Texture upscaling (Display > Texture upscaling): the filter Aurora gives each
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
#include <stdlib.h>
#include <string.h>
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

static void upscale2x(const unsigned char* rgba, unsigned w, unsigned h, unsigned char* out) {
    const unsigned W = w * 2;
    int x, y, q;
    for (y = 0; y < (int)h; y++) {
        for (x = 0; x < (int)w; x++) {
            const Texel p = load(rgba, w, h, x, y);
            for (q = 0; q < 4; q++) {
                const int dx = q & 1, dy = q >> 1;
                const int sx = dx ? 1 : -1, sy = dy ? 1 : -1;
                const Texel hh = load(rgba, w, h, x + sx, y);
                const Texel vv = load(rgba, w, h, x, y + sy);
                const Texel dd = load(rgba, w, h, x + sx, y + sy);
                const float dPD = diff(p, dd), dHV = diff(hh, vv);
                Texel t = mix4(p, 9.0f / 16, hh, 3.0f / 16, vv, 3.0f / 16, dd, 1.0f / 16);
                if (dPD > 0.08f && dHV < dPD * 0.5f) {
                    /* an edge across this corner: halfway from P to the H-V side at most */
                    const float k = (1.0f - 2.0f * dHV / dPD) * 0.5f;
                    const Texel edge = mix4(p, 0.5f, hh, 0.25f, vv, 0.25f, dd, 0.0f);
                    t = mix4(t, 1.0f - k, edge, k, dd, 0.0f, dd, 0.0f);
                }
                store(out + ((size_t)(y * 2 + dy) * W + (size_t)(x * 2 + dx)) * 4, t);
            }
        }
    }
}

static void upscale(const unsigned char* rgba, unsigned width, unsigned height, unsigned factor,
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

void portTextureUpscaleStart(void) {
    static const unsigned kFactor[] = {1, 2, 4};
    const int choice = *portSettingsDeclare("texture_upscale", 0, 0, 2);
    AuroraSetTextureUpscale(kFactor[choice], choice != 0 ? upscale : NULL);
}
