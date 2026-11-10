/* Paper looks (port/include/port/paper.h): paper shadows and paper physics
 * for the characters' cut-outs, at their draw. Draw-only: the matrices here
 * go to the drawing alone (poses whose draw writes world positions or
 * matrices the game reads, and paper-overlay poses, are left as they are),
 * the shadow draw comes before the character's own so anything the draw
 * leaves behind is the real draw's, and the state kept here (per pose: last
 * place, springs) is this file's own, never the game's, and takes no random
 * numbers. */
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <dolphin/vi.h>
#include "port/paper.h"
#include "port/platform.h"
#include "port/settings.h"
#include "port/toggles.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/lightdrv.h"
#include "driver/shadowdrv.h"

extern CharShadowWork* cswp; /* shadowdrv.c */

#define POSES 64
#define CHARS 64

typedef struct PaperPose {
    int pose;           /* -1 free */
    unsigned frame;     /* the game frame (retrace) last stepped */
    unsigned seen;      /* the drawn frame last drawn */
    float x, y, z;      /* feet, last game frame */
    float vy;           /* last frame's fall */
    float rot;          /* rotationY, last frame */
    float lx, lz;       /* lean: shear of x and z per unit of height */
    float lvx, lvz;     /* its spring's speed */
    float squash, squashV;
    float flutter, flutterV;
} PaperPose;

static PaperPose sPoses[POSES];
static unsigned sFrame = 1; /* drawn frames (in-between ones too) */
static int* sShadows;
static int* sPhysics;
static int* sEdges;
/* feet of the characters given a paper shadow: this frame's and last frame's */
static float sChars[2][CHARS][3];
static int sCharCount[2];
static int sCur;
static int sShadowPass; /* portPaperShadowPass */

/* The shadows of the characters drawn with the opaque and cut-out things
 * (dispdrv's modes 0 and 1, sorted near to far among the map's own): drawn
 * later, at the round shadows' turn (mode 9, shadowdrv.c's shadowDisp), once
 * the whole ground is down. Drawn at once they could come before the floor
 * under them, whose draw then failed their depth (the clear colour showed
 * through them). Characters drawn after that turn (translucent ones) draw
 * theirs at once. */
#define QUEUED 640 /* taps: up to 10 a character */
typedef struct PaperShadow {
    int poseId, xluMode;
    float rotY, scale;
    Mtx mtx;
    PortPaperDraw draw;
    float strength, soft; /* portPaperGxShadow's */
    float feet[3], length;
} PaperShadow;
static PaperShadow sQueued[QUEUED];
static int sQueuedCount;
static int sShadowsDone; /* this frame's shadow turn has come */

static void drawShadow(const PaperShadow* s) {
    portPaperGxShadow(s->strength, s->soft, s->feet, s->length);
    sShadowPass = 1;
    s->draw(s->poseId, (MtxPtr)s->mtx, s->xluMode, s->rotY, s->scale);
    sShadowPass = 0;
    portPaperGxShadow(0.0f, 0.0f, NULL, 0.0f);
}

static void declare(void) {
    if (sShadows == NULL) {
        sShadows = portSettingsDeclare("paper_shadows", 0, 0, 2);
        sPhysics = portSettingsDeclare("paper_physics", 0, 0, 2);
        sEdges = portSettingsDeclare("paper_edges", 0, 0, 1);
        memset(sPoses, 0, sizeof(sPoses));
        for (int i = 0; i < POSES; i++) {
            sPoses[i].pose = -1;
        }
    }
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static float wrap180(float a) {
    while (a > 180.0f) {
        a -= 360.0f;
    }
    while (a < -180.0f) {
        a += 360.0f;
    }
    return a;
}

/* the pose's slot: its own, else a free one, else the one unseen longest */
static PaperPose* slotFor(int poseId) {
    PaperPose* oldest = &sPoses[0];
    for (int i = 0; i < POSES; i++) {
        if (sPoses[i].pose == poseId) {
            return &sPoses[i];
        }
    }
    for (int i = 0; i < POSES; i++) {
        if (sPoses[i].pose == -1) {
            oldest = &sPoses[i];
            break;
        }
        if (sPoses[i].seen < oldest->seen) {
            oldest = &sPoses[i];
        }
    }
    memset(oldest, 0, sizeof(*oldest));
    oldest->pose = poseId;
    return oldest;
}

/* the camera's look along the ground (unit), +z when level */
static void cameraForward(float* fx, float* fz) {
    CameraEntry* cam = camGetPtr(CAMERA_3D);
    float x = 0.0f, z = 1.0f, len;
    if (cam != NULL) {
        x = cam->cameraAt.x - cam->cameraPos.x;
        z = cam->cameraAt.z - cam->cameraPos.z;
    }
    len = sqrtf(x * x + z * z);
    if (len < 1e-3f) {
        x = 0.0f;
        z = 1.0f;
        len = 1.0f;
    }
    *fx = x / len;
    *fz = z / len;
}

/* One step of the pose's springs (once a game frame, at the game's 60 Hz: the
 * display's in-between frames hold them, so the motion is the same at any
 * display rate) from where its feet are now. */
static void step(PaperPose* p, const AnimationPose* pose, float x, float y, float z, float strength, unsigned frame) {
    float vx = x - p->x, vy = y - p->y, vz = z - p->z;
    float tx, tz, len, dRot;
    if (p->frame == 0 || p->frame + 4 < frame || vx * vx + vy * vy + vz * vz > 40.0f * 40.0f) {
        /* new, unseen for a while, or moved too far for a step (a warp, the pose reused) */
        memset(&p->vy, 0, sizeof(*p) - offsetof(PaperPose, vy));
        p->rot = pose->rotationY;
        vx = vy = vz = 0.0f;
    }
    /* lean: the top trails the move, a spring after it (so it sways back on a stop) */
    tx = -vx * 0.035f * strength;
    tz = -vz * 0.035f * strength;
    len = sqrtf(tx * tx + tz * tz);
    if (len > 0.3f * strength) {
        tx *= 0.3f * strength / len;
        tz *= 0.3f * strength / len;
    }
    p->lvx += (tx - p->lx) * 0.22f - p->lvx * 0.3f;
    p->lvz += (tz - p->lz) * 0.22f - p->lvz * 0.3f;
    p->lx += p->lvx;
    p->lz += p->lvz;
    /* a hard landing: a fast fall stopped */
    if (p->vy < -3.0f && vy > -0.5f) {
        p->squashV += fminf((-p->vy - 3.0f) * 0.03f, 0.2f) * strength;
    }
    p->squashV += -p->squash * 0.3f - p->squashV * 0.28f;
    p->squash = clampf(p->squash + p->squashV, -0.25f, 0.3f);
    /* a quick turn: the paper flutters toward and away from the camera */
    dRot = wrap180(pose->rotationY - p->rot);
    p->flutterV += clampf(dRot, -60.0f, 60.0f) * 0.0025f * strength;
    p->flutterV += -p->flutter * 0.25f - p->flutterV * 0.25f;
    p->flutter = clampf(p->flutter + p->flutterV, -0.3f, 0.3f);
    p->x = x;
    p->y = y;
    p->z = z;
    p->vy = vy;
    p->rot = pose->rotationY;
    p->frame = frame;
}

/* out = the pose's physics about its feet (x, y, z: where they are drawn, an
 * in-between frame's too), then in */
static void physicsMtx(const PaperPose* p, float x, float y, float z, Mtx in, Mtx out) {
    float fx, fz, sx, sz, s = p->squash;
    Mtx m;
    cameraForward(&fx, &fz);
    sx = p->lx + p->flutter * fx;
    sz = p->lz + p->flutter * fz;
    /* x' = x + (x-Fx)s/2 + (y-Fy)sx;  y' = y - (y-Fy)s;  z' likewise */
    m[0][0] = 1.0f + s * 0.5f;
    m[0][1] = sx;
    m[0][2] = 0.0f;
    m[0][3] = -x * s * 0.5f - y * sx;
    m[1][0] = 0.0f;
    m[1][1] = 1.0f - s;
    m[1][2] = 0.0f;
    m[1][3] = y * s;
    m[2][0] = 0.0f;
    m[2][1] = sz;
    m[2][2] = 1.0f + s * 0.5f;
    m[2][3] = -z * s * 0.5f - y * sz;
    PSMTXConcat(m, in, out);
}

/* The round shadow placed this frame under feet (x, y, z): its ground height, or 0 if none. */
static int groundUnder(float x, float y, float z, float* ground) {
    const ShadowEntry* best = NULL;
    float bestD = 25.0f * 25.0f;
    for (int i = 0; i < cswp->numEntries; i++) {
        const ShadowEntry* e = &cswp->entries[i];
        float dx, dz, d;
        if (e->size == 0.0f || e->mode > 2 || !(e->flags & SHADOW_FLAG_PLACED) || e->camId != CAMERA_3D) {
            continue;
        }
        dx = e->pos.x - x;
        dz = e->pos.z - z;
        d = dx * dx + dz * dz;
        if (d < bestD && e->mtx[1][3] < y + 10.0f) {
            bestD = d;
            best = e;
        }
    }
    if (best == NULL) {
        return 0;
    }
    *ground = best->mtx[1][3] - 0.8f; /* placed one unit over the ground; the cut-out a little nearer it */
    return 1;
}

/* A light a shadow falls from: its direction (down, unit) and how strongly it lights the character. */
typedef struct ShadowLight {
    float l[3];
    float w;
} ShadowLight;

static float smooth01(float a, float b, float v) {
    float t = clampf((v - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* a map light's aim: its rotation (degrees) applied to -z, as mapdrv.c's light setup does */
static void lightAim(const LightEntry* e, float* out) {
    Mtx rx, ry, rz;
    Vec dir = {0.0f, 0.0f, -1.0f};
    PSMTXRotRad(rx, 'x', 0.017453292f * e->rotation.x);
    PSMTXRotRad(ry, 'y', 0.017453292f * e->rotation.y);
    PSMTXRotRad(rz, 'z', 0.017453292f * e->rotation.z);
    PSMTXConcat(rz, ry, ry);
    PSMTXConcat(ry, rx, rx);
    PSMTXMultVecSR(rx, &dir, &dir);
    out[0] = dir.x;
    out[1] = dir.y;
    out[2] = dir.z;
}

/* The two map lights lighting a character most (feet x, y, z; height top): out[0] the strongest. Point lights by
 * their distance falloff, the lamps aimed as a cone (the game's LIGHT_DIRECTIONAL: a position and an aim) inside
 * their cone, the far ones (LIGHT_SPOT: one direction everywhere, as the sun) at a fixed weight; each eased in
 * and out over a range, so walking between lamps turns the shadow instead of snapping it. Returns how many. */
static int mapLights(float x, float y, float z, float top, ShadowLight* out) {
    int count = 0;
    out[0].w = out[1].w = 0.0f;
    for (int i = 0; i < LIGHT_ENTRY_COUNT; i++) {
        const LightEntry* e = lightNumberToPtr(i);
        ShadowLight s;
        float lum, d;
        if (e == NULL || (e->flags & LIGHT_AMBIENT) || !(e->flags & (LIGHT_POINT | LIGHT_SPOT | LIGHT_DIRECTIONAL))) {
            continue;
        }
        lum = (0.30f * e->color.r + 0.59f * e->color.g + 0.11f * e->color.b) / 255.0f;
        if (e->flags & LIGHT_SPOT) {
            lightAim(e, s.l);
            /* only one shining down */
            s.w = lum * 0.5f * smooth01(-0.15f, -0.4f, s.l[1]);
        } else {
            s.l[0] = x - e->position.x;
            s.l[1] = y + top - e->position.y;
            s.l[2] = z - e->position.z;
            d = sqrtf(s.l[0] * s.l[0] + s.l[1] * s.l[1] + s.l[2] * s.l[2]);
            if (d < 1.0f) {
                continue;
            }
            s.l[0] /= d;
            s.l[1] /= d;
            s.l[2] /= d;
            /* above the character's top, and near enough */
            s.w = lum * smooth01(-2.0f, -12.0f, s.l[1] * d) * smooth01(420.0f, 260.0f, d);
            if (e->flags & LIGHT_POINT) {
                const float k = e->attenDistCoef.x + e->attenDistCoef.y * d + e->attenDistCoef.z * d * d;
                if (!(e->flags & LIGHT_NO_DIST_ATTEN) && k > 1.0f) {
                    s.w /= k;
                }
            } else {
                /* a cone: its aim against the way to the character, eased at the rim */
                float aim[3], c, rim;
                lightAim(e, aim);
                c = aim[0] * s.l[0] + aim[1] * s.l[1] + aim[2] * s.l[2];
                rim = cosf(0.017453292f * clampf(e->spotAngle * 0.5f, 1.0f, 89.0f));
                s.w *= smooth01(rim - 0.05f, rim + 0.05f, c);
            }
        }
        if (s.w <= 1e-3f) {
            continue;
        }
        if (s.w > out[0].w) {
            out[1] = out[0];
            out[0] = s;
        } else if (s.w > out[1].w) {
            out[1] = s;
        }
        count++;
    }
    return count < 2 ? count : 2;
}

/* The fixed light for the camera: from above, its left and a little in front of it: the shadow falls back and to
 * the right, so the cut-out (facing the camera) lies down as its own shape beside the character. Straight away
 * from the camera it fell behind them, edge-on to the view: a sliver. (The camera's right along the ground is
 * (-fz, fx).) */
static void stageLight(float* l) {
    float fx, fz, len;
    cameraForward(&fx, &fz);
    l[0] = -fz * 0.65f + fx * 0.5f;
    l[1] = -1.0f;
    l[2] = fx * 0.65f + fz * 0.5f;
    len = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    l[0] /= len;
    l[1] /= len;
    l[2] /= len;
}

/* l (unit, down) kept steep enough that the shadow is not longer than maxLen times the height; unit again */
static void limitLength(float* l, float maxLen) {
    float h, len;
    if (l[1] > -0.05f) {
        l[1] = -0.05f;
    }
    h = sqrtf(l[0] * l[0] + l[2] * l[2]);
    if (h > -l[1] * maxLen) {
        l[0] *= -l[1] * maxLen / h;
        l[2] *= -l[1] * maxLen / h;
    }
    len = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    l[0] /= len;
    l[1] /= len;
    l[2] /= len;
}

/* The light a character's shadow falls from (feet x, y, z; height top): l, unit, down along it. The lamps around,
 * blended by how strongly each lights them, with the stage light as a floor, so it turns smoothly as they walk;
 * aimed at the top of the character, a lamp close above is steep and one off to the side shallow. */
static void sceneLight(float x, float y, float z, float top, float* l) {
    ShadowLight lamps[2];
    const int n = mapLights(x, y, z, top, lamps);
    const float floorW = 0.12f;
    float sum = floorW;
    float stage[3];
    stageLight(stage);
    for (int k = 0; k < 3; k++) {
        l[k] = stage[k] * floorW;
    }
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 3; k++) {
            l[k] += lamps[i].l[k] * lamps[i].w;
        }
        sum += lamps[i].w;
    }
    for (int k = 0; k < 3; k++) {
        l[k] /= sum;
    }
    limitLength(l, 2.0f);
}

/* A shadow is drawn as a few taps: the cut-out flattened onto the ground along a light turned a little (turn,
 * degrees), longer or shorter (length, times the shadow's), and nudged along the ground (right, toward the camera;
 * world units). Where they all overlap it is darkest; their edges, spread apart, make a soft edge (a lamp's
 * penumbra: the turns and lengths widen it away from the feet, the nudges at the feet too). */
typedef struct ShadowTap {
    float turn, length, right, toward;
} ShadowTap;
typedef struct ShadowPart {
    float length; /* along the ground, times the character's height */
    float dark;   /* where every tap overlaps */
    float soft;   /* portPaperGxShadow's: the edge widening and fading away from the feet */
    int taps;
    ShadowTap tap[6];
} ShadowPart;
typedef struct ShadowLook {
    ShadowPart cast;    /* the cut-out's shadow, falling away from the light */
    ShadowPart contact; /* a short dark one at the feet (High) */
} ShadowLook;
static const ShadowLook kLooks[2] = {
    /* On: soft all over */
    {{1.4f,
      0.65f,
      0.8f,
      6,
      {{0.0f, 1.0f, 0.0f, 0.0f},
       {5.0f, 1.03f, 1.0f, 0.0f},
       {-5.0f, 1.03f, -1.0f, 0.0f},
       {2.5f, 0.95f, 0.0f, 0.8f},
       {-2.5f, 0.95f, 0.0f, -0.8f},
       {0.0f, 1.07f, 0.5f, 0.4f}}},
     {0.0f, 0.0f, 0.0f, 0, {{0.0f}}}},
    /* High: longer, darker and sharper, a little softer toward its end, dark where the feet touch the ground */
    {{1.9f, 0.75f, 0.35f, 3, {{0.0f, 1.0f, 0.0f, 0.0f}, {2.0f, 1.02f, 0.4f, 0.0f}, {-2.0f, 1.02f, -0.4f, 0.0f}}},
     {0.22f,
      0.45f,
      0.0f,
      4,
      {{0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 1.2f, 0.0f}, {0.0f, 1.0f, -1.2f, 0.0f}, {0.0f, 1.0f, 0.0f, -1.0f}}}},
};

/* out = the draw flattened along l onto the ground at height g, then in (kept a hair from singular for the
 * normals the draw derives) */
static void shadowMtx(const float* l, float g, Mtx in, Mtx out) {
    const float ax = l[0] / l[1], az = l[2] / l[1], keep = 0.002f;
    Mtx m;
    m[0][0] = 1.0f;
    m[0][1] = -ax;
    m[0][2] = 0.0f;
    m[0][3] = ax * g;
    m[1][0] = 0.0f;
    m[1][1] = keep;
    m[1][2] = 0.0f;
    m[1][3] = g * (1.0f - keep);
    m[2][0] = 0.0f;
    m[2][1] = -az;
    m[2][2] = 1.0f;
    m[2][3] = az * g;
    PSMTXConcat(m, in, out);
}

/* Where a character's shadow falls (feet x, y, z; height top): d, unit, along the ground. The lamps' blend steers
 * it left or right, but it always comes toward the camera: a paper cut-out faces the camera, so a shadow falling
 * away from it lies behind the character, hidden and edge-on (a sliver); cast forward it lies down on the floor
 * in front of their feet as the cut-out's own shape. */
static void shadowDir(float x, float y, float z, float top, float* d) {
    float l[3], fx, fz, hx, hz, h, r, f;
    cameraForward(&fx, &fz);
    sceneLight(x, y, z, top, l);
    hx = l[0];
    hz = l[2];
    h = sqrtf(hx * hx + hz * hz);
    /* across the screen, as a share: the camera's right is (-fz, fx); the stage light's when straight down */
    r = h > 0.05f ? (-fz * hx + fx * hz) / h : 0.3f;
    r = clampf(r, -0.35f, 0.35f);
    f = -sqrtf(1.0f - r * r);
    d[0] = -fz * r + fx * f;
    d[1] = fx * r + fz * f;
}

void portPaperDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale, PortPaperDraw draw) {
    AnimationPose* pose;
    PaperPose* p;
    Mtx real, shadow;
    float x, y, z, ground;
    float strength;

    declare();
    if ((*sShadows == 0 && *sPhysics == 0) || poseId < 0 || camGetCurNo() != CAMERA_3D) {
        draw(poseId, mtx, xluMode, rotY, scale);
        return;
    }
    pose = animPoseGetAnimPosePtr(poseId);
    if (pose == NULL || pose->paperId != -1 ||
        (pose->flags & (ANIM_POSE_FLAG_WORLD_POSITIONS | ANIM_POSE_FLAG_WORLD_MATRICES))) {
        draw(poseId, mtx, xluMode, rotY, scale);
        return;
    }
    x = mtx[0][3];
    y = mtx[1][3];
    z = mtx[2][3];
    p = slotFor(poseId);
    p->seen = sFrame;
    strength = *sPhysics == 2 ? 1.0f : 0.5f;
    if (portInterpStepsHere()) { /* (stereo's first eye steps it, once a frame: the second sees the same) */
        const unsigned frame = VIGetRetraceCount() + 1; /* (0: a new slot) */
        if (p->frame != frame) {
            step(p, pose, x, y, z, *sPhysics != 0 ? strength : 0.0f, frame);
        }
    }
    if (*sPhysics != 0) {
        physicsMtx(p, x, y, z, mtx, real);
    } else {
        PSMTXCopy(mtx, real);
    }
    {
        static int sTold; /* each choice, once (the tests' signal) */
        if (*sShadows != 0 && sTold != *sShadows) {
            sTold = *sShadows;
            printf("[paper] shadows %d\n", *sShadows);
        }
    }
    if (*sShadows != 0 && groundUnder(x, y, z, &ground)) {
        const ShadowLook* look = &kLooks[*sShadows == 2 ? 1 : 0];
        /* the character's height: about 32 units at its drawn scale */
        const float top =
            clampf(32.0f * sqrtf(mtx[0][1] * mtx[0][1] + mtx[1][1] * mtx[1][1] + mtx[2][1] * mtx[2][1]), 8.0f, 200.0f);
        /* off the ground (a jump, a fall): the shadow stays under them as if they stood there, smaller and fainter */
        const float lift = smooth01(0.0f, 90.0f, y - ground);
        const float shrink = 1.0f - 0.25f * lift, fade = 1.0f - 0.4f * lift;
        float d[2], fx, fz, feet[3] = {0.0f, 0.0f, 0.0f};
        Mtx grounded;
        CameraEntry* cam = camGetPtr(CAMERA_3D);
        shadowDir(x, y, z, top, d);
        cameraForward(&fx, &fz);
        PSMTXCopy(real, grounded);
        grounded[1][3] -= y - ground;
        if (cam != NULL) {
            /* the feet in view space, where the shadow is darkest and sharpest */
            Vec at = {x, ground, z}, v = {0.0f, 0.0f, 0.0f};
            PSMTXMultVec(cam->view, &at, &v);
            feet[0] = v.x;
            feet[1] = v.y;
            feet[2] = v.z;
        }
        for (int part = 0; part < 2; part++) {
            const ShadowPart* sp = part == 0 ? &look->cast : &look->contact;
            /* each tap's share, so that where all of them overlap the shadow is sp->dark */
            const float a = sp->taps > 0 ? 1.0f - powf(1.0f - sp->dark * fade, 1.0f / (float)sp->taps) : 0.0f;
            for (int i = 0; i < sp->taps; i++) {
                const ShadowTap* t = &sp->tap[i];
                PaperShadow one, *q = sShadowsDone ? &one : sQueuedCount < QUEUED ? &sQueued[sQueuedCount++] : NULL;
                const float c = cosf(0.017453292f * t->turn), s = sinf(0.017453292f * t->turn);
                const float len = sp->length * t->length * shrink;
                const float l[3] = {(d[0] * c - d[1] * s) * len, -1.0f, (d[0] * s + d[1] * c) * len};
                /* the tap's nudge along the ground: right (-fz, fx) and toward the camera (-fx, -fz); all of them
                 * tucked a little under the feet, so the shadow meets the drawn feet (above the cut-out's bottom) */
                const float toward = t->toward - 0.06f * top;
                const float ox = -fz * t->right - fx * toward, oz = fx * t->right - fz * toward;
                Mtx m;
                if (q == NULL) {
                    break;
                }
                shadowMtx(l, ground, grounded, shadow);
                /* shrunk about the feet, then nudged */
                memset(m, 0, sizeof(m));
                m[0][0] = shrink;
                m[0][3] = x * (1.0f - shrink) + ox;
                m[1][1] = 1.0f;
                m[2][2] = shrink;
                m[2][3] = z * (1.0f - shrink) + oz;
                PSMTXConcat(m, shadow, q->mtx);
                q->poseId = poseId;
                q->xluMode = xluMode;
                q->rotY = rotY;
                q->scale = scale;
                q->draw = draw;
                q->strength = a;
                q->soft = cam != NULL ? sp->soft : 0.0f;
                q->feet[0] = feet[0];
                q->feet[1] = feet[1];
                q->feet[2] = feet[2];
                /* from the feet to the tip of the head's shadow, and a little past */
                q->length = top * (len + 0.3f);
                if (sShadowsDone) {
                    drawShadow(q);
                }
            }
        }
        if (sCharCount[sCur] < CHARS) {
            float* c = sChars[sCur][sCharCount[sCur]++];
            c[0] = x;
            c[1] = y;
            c[2] = z;
        }
    }
    draw(poseId, real, xluMode, rotY, scale);
}

void portPaperShadowTurn(void) {
    int i;
    declare();
    if (sShadowsDone) {
        return;
    }
    for (i = 0; i < sQueuedCount; i++) {
        drawShadow(&sQueued[i]);
    }
    sQueuedCount = 0;
    sShadowsDone = 1;
}

void portPaperSceneLight(float x, float y, float z, float* dir) {
    declare();
    sceneLight(x, y, z, 32.0f, dir);
}

int portPaperShadowPass(void) {
    return sShadowPass;
}

int portPaperShadowSkip(const struct ShadowEntry* entry) {
    const int last = sCur ^ 1;
    declare();
    if (*sShadows == 0 || entry->camId != CAMERA_3D || entry->mode > 2 || !(entry->flags & SHADOW_FLAG_PLACED)) {
        return 0;
    }
    /* a character near it had a paper shadow last frame (and this one's, if drawn already) */
    for (int k = 0; k < 2; k++) {
        const int list = k == 0 ? last : sCur;
        for (int i = 0; i < sCharCount[list]; i++) {
            const float dx = sChars[list][i][0] - entry->pos.x, dz = sChars[list][i][2] - entry->pos.z;
            if (dx * dx + dz * dz < 25.0f * 25.0f && entry->mtx[1][3] < sChars[list][i][1] + 10.0f) {
                return 1;
            }
        }
    }
    return 0;
}

/* Stereoscopic 3D's second eye (stereo_aurora.c): this frame's shadows queued and drawn again, its characters
 * listed again. */
void portPaperEyeBegin(void) {
    sQueuedCount = 0;
    sShadowsDone = 0;
    sCharCount[sCur] = 0;
}

void portPaperFrame(void) {
    declare();
    portPaperGxEdges(*sEdges);
    sFrame++;
    sQueuedCount = 0; /* (a frame without the shadow turn draws none) */
    sShadowsDone = 0;
    sCur ^= 1;
    sCharCount[sCur] = 0;
}
