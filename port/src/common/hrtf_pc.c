/* Headphone 3D audio (Audio > Speakers > Headphones (3D)): the game's
 * surround mix, as for 5.1 (libs/musyx/src/musyx/runtime/hw_pc.c), heard on
 * headphones as four speakers around the head: the front pair at 30 degrees
 * left and right, the rear (MusyX's surround bus and Pro Logic II's rear pair)
 * at 110 degrees, behind.
 *
 * Each speaker reaches each ear through a small built-in head model, no
 * measured data (nothing vendored, nothing downloaded): C. P. Brown and
 * R. O. Duda's structural model ("A structural model for binaural sound
 * synthesis", IEEE Trans. Speech and Audio Processing 6(5), 1998), a sphere's
 * head shadow as a one-pole, one-zero filter whose high frequencies rise for
 * the ear facing the sound and fall for the far one, and the sphere's
 * interaural time delay; plus a gentle high-frequency roll-off for the sounds
 * behind (the outer ear's shadow), so front and back differ. */
#include <math.h>
#include <string.h>

#define HRTF_RATE 32000.0f
#define HRTF_SPEAKERS 4
#define HRTF_DELAY 32 /* samples of delay line: the sphere's largest delay is ~21 */

typedef struct Ear {
    float b0, b1, a1; /* the head shadow */
    float x1, y1;
    float delay;      /* samples */
} Ear;

typedef struct Speaker {
    Ear ear[2];
    float rear;       /* the roll-off's amount (0 in front) */
    float lp;         /* its one-pole low pass */
    float line[HRTF_DELAY];
    int at;
} Speaker;

static Speaker sSpk[HRTF_SPEAKERS];
static int sReady;

/* one ear's filter and delay for a sound at azimuth az (radians, 0 ahead,
 * positive to the right); side -1 the left ear, +1 the right */
static void earFor(Ear* e, float az, int side) {
    const float pi = 3.14159265f;
    const float a = 0.0875f, c = 343.0f;        /* head radius (m), speed of sound */
    const float beta = 2.0f * c / a;             /* the shadow's corner (rad/s) */
    const float alphaMin = 0.1f, thetaMin = 150.0f * pi / 180.0f;
    const float k = 2.0f * HRTF_RATE;            /* the bilinear transform's */
    float theta = fabsf(az - (float)side * pi / 2.0f); /* from the ear's axis */
    float alpha, t;
    if (theta > pi) {
        theta = 2.0f * pi - theta;
    }
    alpha = 1.0f + alphaMin / 2.0f + (1.0f - alphaMin / 2.0f) * cosf(theta / thetaMin * pi);
    /* H(s) = (alpha s + beta) / (s + beta) */
    e->b0 = (alpha * k + beta) / (k + beta);
    e->b1 = (beta - alpha * k) / (k + beta);
    e->a1 = (beta - k) / (k + beta);
    e->x1 = e->y1 = 0.0f;
    /* the delay to this ear: shorter facing the sound, longer around the head */
    t = theta < pi / 2.0f ? -cosf(theta) : theta - pi / 2.0f;
    e->delay = (a / c) * (1.0f + t) * HRTF_RATE;
}

static void setUp(void) {
    const float deg = 3.14159265f / 180.0f;
    static const float kAz[HRTF_SPEAKERS] = {-30.0f, 30.0f, -110.0f, 110.0f};
    int i;
    memset(sSpk, 0, sizeof(sSpk));
    for (i = 0; i < HRTF_SPEAKERS; i++) {
        earFor(&sSpk[i].ear[0], kAz[i] * deg, -1);
        earFor(&sSpk[i].ear[1], kAz[i] * deg, 1);
        sSpk[i].rear = i >= 2 ? 0.45f : 0.0f;
    }
    sReady = 1;
}

static float delayed(const Speaker* s, float d) {
    const int whole = (int)d;
    const float frac = d - (float)whole;
    const float a = s->line[(s->at - whole + HRTF_DELAY * 2) % HRTF_DELAY];
    const float b = s->line[(s->at - whole - 1 + HRTF_DELAY * 2) % HRTF_DELAY];
    return a + (b - a) * frac;
}

static float ear(Ear* e, float x) {
    const float y = e->b0 * x + e->b1 * e->x1 - e->a1 * e->y1;
    e->x1 = x;
    e->y1 = y;
    return y;
}

static short clampS16(float v) {
    return v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (short)v;
}

/* stereo: the game's front pair (interleaved s16, in place: the binaural
 * result); s, rl, rr: the surround bus and the rear pair (NULL: none) */
void portHrtfProcess(short* stereo, const int* s, const int* rl, const int* rr, unsigned frames) {
    /* the low pass behind: ~5 kHz */
    const float lpA = 0.62f;
    unsigned i;
    int k;
    if (!sReady) {
        setUp();
    }
    for (i = 0; i < frames; i++) {
        const float sur = s != NULL ? (float)s[i] : 0.0f;
        float in[HRTF_SPEAKERS], out[2] = {0.0f, 0.0f};
        in[0] = (float)stereo[2 * i];
        in[1] = (float)stereo[2 * i + 1];
        in[2] = sur + (rl != NULL ? (float)rl[i] : 0.0f);
        in[3] = sur + (rr != NULL ? (float)rr[i] : 0.0f);
        for (k = 0; k < HRTF_SPEAKERS; k++) {
            Speaker* sp = &sSpk[k];
            float x = in[k];
            if (sp->rear > 0.0f) {
                sp->lp += lpA * (x - sp->lp);
                x += sp->rear * (sp->lp - x);
            }
            sp->at = (sp->at + 1) % HRTF_DELAY;
            sp->line[sp->at] = x;
            out[0] += ear(&sp->ear[0], delayed(sp, sp->ear[0].delay));
            out[1] += ear(&sp->ear[1], delayed(sp, sp->ear[1].delay));
        }
        /* both ears hear every speaker's lows: about the stereo mix's level */
        stereo[2 * i] = clampS16(out[0] * 0.6f);
        stereo[2 * i + 1] = clampS16(out[1] * 0.6f);
    }
}
