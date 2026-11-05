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
#define QUEUED 192
typedef struct PaperShadow {
    int poseId, xluMode;
    float rotY, scale;
    Mtx mtx;
    PortPaperDraw draw;
} PaperShadow;
static PaperShadow sQueued[QUEUED];
static int sQueuedCount;
static int sShadowsDone; /* this frame's shadow turn has come */

static void drawShadow(int poseId, Mtx mtx, int xluMode, float rotY, float scale, PortPaperDraw draw) {
    portPaperGxShadow(1);
    sShadowPass = 1;
    draw(poseId, mtx, xluMode, rotY, scale);
    sShadowPass = 0;
    portPaperGxShadow(0);
}

static void declare(void) {
    if (sShadows == NULL) {
        sShadows = portSettingsDeclare("paper_shadows", 0, 0, 1);
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

/* The light the shadow falls from at feet (x, y, z), as a unit direction (down): the nearest lamp above,
 * else a fixed one for the camera: from above, its left and a little in front, as a paper theatre's
 * footlights set to one side. */
static void lightDir(float x, float y, float z, float* l) {
    float best = 300.0f * 300.0f, len, h;
    int found = 0;
    for (int i = 0, n = lightGetEntryNum(); i < n && i < LIGHT_ENTRY_COUNT; i++) {
        const LightEntry* e = lightNumberToPtr(i);
        float dx, dy, dz, d;
        if (e == NULL || !(e->flags & (LIGHT_POINT | LIGHT_SPOT)) || (e->flags & (LIGHT_AMBIENT | LIGHT_DIRECTIONAL))) {
            continue;
        }
        dx = x - e->position.x;
        dy = y + 25.0f - e->position.y;
        dz = z - e->position.z;
        d = dx * dx + dz * dz + dy * dy;
        if (dy < -10.0f && d < best) {
            best = d;
            l[0] = dx;
            l[1] = dy;
            l[2] = dz;
            found = 1;
        }
    }
    if (!found) {
        float fx, fz;
        cameraForward(&fx, &fz);
        /* from above, the camera's left and a little in front of it: the shadow
         * falls back and to the right, so the cut-out (facing the camera) lies
         * down as its own shape beside the character. Straight away from the
         * camera it fell behind them, edge-on to the view: a sliver. (The
         * camera's right along the ground is (-fz, fx).) */
        l[0] = -fz * 0.65f + fx * 0.5f;
        l[1] = -1.0f;
        l[2] = fx * 0.65f + fz * 0.5f;
    }
    /* not longer than twice the height */
    h = sqrtf(l[0] * l[0] + l[2] * l[2]);
    if (h > -l[1] * 2.0f) {
        l[0] *= -l[1] * 2.0f / h;
        l[2] *= -l[1] * 2.0f / h;
    }
    len = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    l[0] /= len;
    l[1] /= len;
    l[2] /= len;
}

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
    if (!portInterpInBetween()) {
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
    if (*sShadows != 0 && groundUnder(x, y, z, &ground)) {
        float l[3];
        lightDir(x, y, z, l);
        shadowMtx(l, ground, real, shadow);
        if (sShadowsDone) {
            drawShadow(poseId, shadow, xluMode, rotY, scale, draw);
        } else if (sQueuedCount < QUEUED) {
            PaperShadow* q = &sQueued[sQueuedCount++];
            q->poseId = poseId;
            q->xluMode = xluMode;
            q->rotY = rotY;
            q->scale = scale;
            PSMTXCopy(shadow, q->mtx);
            q->draw = draw;
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
        PaperShadow* q = &sQueued[i];
        drawShadow(q->poseId, q->mtx, q->xluMode, q->rotY, q->scale, q->draw);
    }
    sQueuedCount = 0;
    sShadowsDone = 1;
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

void portPaperFrame(void) {
    declare();
    portPaperGxEdges(*sEdges);
    sFrame++;
    sQueuedCount = 0; /* (a frame without the shadow turn draws none) */
    sShadowsDone = 0;
    sCur ^= 1;
    sCharCount[sCur] = 0;
}
