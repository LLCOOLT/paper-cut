/* Fun toggles (port/include/port/toggles.h): silly switches on the Modes tab.
 * Big heads, paper-thin, spinning and Cool Mode only change how models are drawn (the
 * matrices the draw uses, after anything the game keeps: world positions,
 * a paper character's turn), so they never touch the game's state. Moon
 * gravity changes play: online it is the host's, as every setting that
 * changes play. Luigi's voice is the sound alone. */
#include "port/toggles.h"
#include "port/settings.h"
#include "port/platform.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "port/paper.h"
#include <math.h>
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const kKeys[PORT_FUN_COUNT] = {"fun_big_heads", "fun_moon_gravity", "fun_paper_thin",
                                                  "fun_luigi_voice", "fun_spin", "fun_cool"};
static int* sOn[PORT_FUN_COUNT];
static unsigned sChaos; /* chaos mode's effects running now (chaos_pc.c), a bit per toggle */

#define HEAD_SCALE 1.8f
#define THIN_SCALE 0.12f
#define SPIN_DEGREES_PER_SECOND 180.0f

void portFunDeclare(void) {
    int i;
    if (sOn[0] != NULL) {
        return;
    }
    for (i = 0; i < PORT_FUN_COUNT; i++) {
        sOn[i] = portSettingsDeclare(kKeys[i], 0, 0, 1);
    }
}

int portFunOn(int which) {
    if (which < 0 || which >= PORT_FUN_COUNT) {
        return 0;
    }
    portFunDeclare();
    return (*sOn[which] || (sChaos & (1u << which))) && !portConsoleAccurate();
}

void portFunChaosSet(int which, int on) {
    if (which >= 0 && which < PORT_FUN_COUNT) {
        sChaos = on ? sChaos | (1u << which) : sChaos & ~(1u << which);
    }
}

/* the screen's own animations (fades, curtains, pipes: OFF_*) stay as they are */
static int isCharacter(const char* file) {
    return file != NULL && strncmp(file, "OFF_", 4) != 0;
}

MtxPtr portFunPoseMtx(const char* file, MtxPtr in, Mtx out) {
    const int thin = portFunOn(PORT_FUN_PAPER_THIN), spin = portFunOn(PORT_FUN_SPIN);
    Mtx m;
    if ((!thin && !spin) || !isCharacter(file)) {
        return in;
    }
    PSMTXCopy(in, out);
    if (spin) {
        /* the clock's, not the frames': the same turn at any frame rate (a draw: read without
         * moving the lockstep clock, which a read moves 1 us, and this draws per model) */
        const float seconds = (float)(OSTicksToMilliseconds((OSTime)portGameClockPeek()) % 2000000) / 1000.0f;
        PSMTXRotRad(m, 'y', MTXDegToRad(seconds * SPIN_DEGREES_PER_SECOND));
        PSMTXConcat(out, m, out);
    }
    if (thin) {
        PSMTXScale(m, THIN_SCALE, 1.0f, 1.0f); /* a model's width; its paper is already thin the other way */
        PSMTXConcat(out, m, out);
    }
    return out;
}

/* --- Big heads: which groups are a model's head --------------------------- */

/* The table tools/port/fun_heads.py makes from the disc's models: most name
 * their groups pPlaneN, so their heads are picked there by shape. Each entry
 * is a head group and the point it grows about (its neck, in the group's own
 * coordinates); a NULL group: the model has no head to grow. */
typedef struct HeadRow {
    const char* model;
    const char* group;
    float pivot[3];
} HeadRow;
static const HeadRow kHeadTable[] = {
#include "fun_heads.inc"
};

/* A model the table doesn't have: a group named for a head (or a face), in
 * English or the Japanese the models are named in; only the topmost of a
 * subtree, so a face inside a head isn't grown twice. */
static int isHeadName(const char* name) {
    static const char* const kWords[] = {"head", "atama", "kao", "face"};
    char low[64];
    size_t i;
    for (i = 0; i + 1 < sizeof(low) && name[i] != '\0'; i++) {
        low[i] = (char)tolower((unsigned char)name[i]);
    }
    low[i] = '\0';
    for (i = 0; i < sizeof(kWords) / sizeof(kWords[0]); i++) {
        if (strstr(low, kWords[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

#define HEAD_CACHE 64
#define HEADS_MAX 4
typedef struct HeadEntry {
    const AnimationPoseData* data;
    char file[64];
    int count;
    int groups[HEADS_MAX];
    const float* pivots[HEADS_MAX]; /* the table's (NULL: the group's rotation centre) */
} HeadEntry;
static HeadEntry sHeads[HEAD_CACHE];

static void findHeads(const AnimationPoseData* data, int group, int underHead, HeadEntry* e, int depth) {
    while (group >= 0 && group < data->groupCount && depth < 64) {
        const AnimPoseGroup* g = &data->groups[group];
        int head = 0;
        if (!underHead && isHeadName(g->name) && e->count < HEADS_MAX) {
            e->groups[e->count] = group;
            e->pivots[e->count++] = NULL;
            head = 1;
        }
        findHeads(data, g->childGroupId, underHead || head, e, depth + 1);
        group = g->brotherGroupId;
        depth++;
    }
}

/* the table's rows for a model (ours, made from the disc's at startup, end in
 * _pc: the disc's groups); 1 when the table settles it: its groups found, or
 * no head. A model a mod replaced, without the table's groups: 0 (by name). */
static int tableHeads(const AnimationPoseData* data, HeadEntry* e) {
    char model[64];
    size_t n, i;
    int none = 0, g;
    snprintf(model, sizeof(model), "%s", data->fileName);
    n = strlen(model);
    if (n > 3 && strcmp(model + n - 3, "_pc") == 0) {
        model[n - 3] = '\0';
    }
    for (i = 0; i < sizeof(kHeadTable) / sizeof(kHeadTable[0]); i++) {
        const HeadRow* r = &kHeadTable[i];
        if (strcmp(r->model, model) != 0) {
            continue;
        }
        if (r->group == NULL) {
            none = 1;
            continue;
        }
        for (g = 0; g < data->groupCount && e->count < HEADS_MAX; g++) {
            if (strcmp(data->groups[g].name, r->group) == 0) {
                e->groups[e->count] = g;
                e->pivots[e->count++] = r->pivot;
                break;
            }
        }
    }
    return none || e->count > 0;
}

static const HeadEntry* heads(const AnimationPoseData* data) {
    HeadEntry* e = &sHeads[((size_t)data >> 4) % HEAD_CACHE];
    if (e->data != data || strncmp(e->file, data->fileName, sizeof(e->file)) != 0) {
        memset(e, 0, sizeof(*e));
        e->data = data;
        snprintf(e->file, sizeof(e->file), "%s", data->fileName);
        if (isCharacter(data->fileName) && !tableHeads(data, e)) {
            findHeads(data, data->groupCount - 1, 0, e, 0); /* dispProc's root */
        }
        if (getenv("TTYD_FUN_TRACE") != NULL) {
            int i;
            printf("[fun] %s: %d head group(s)", data->fileName, e->count);
            for (i = 0; i < e->count; i++) {
                printf(" %s%s", data->groups[e->groups[i]].name, e->pivots[i] != NULL ? " (table)" : "");
            }
            printf("\n");
            fflush(stdout);
        }
    }
    return e;
}

int portFunHeadScale(const void* poseData, int groupId, float* scale, float pivot[3]) {
    const AnimationPoseData* data = poseData;
    const HeadEntry* e;
    int i;
    if (data == NULL || !portFunOn(PORT_FUN_BIG_HEADS)) {
        return 0;
    }
    e = heads(data);
    for (i = 0; i < e->count; i++) {
        if (e->groups[i] == groupId) {
            *scale = HEAD_SCALE;
            if (e->pivots[i] != NULL) {
                memcpy(pivot, e->pivots[i], sizeof(float) * 3);
            }
            return 1;
        }
    }
    return 0;
}

/* --- Cool Mode: sunglasses on everyone ------------------------------------ */

/* The table tools/port/fun_cool.py makes from the disc's models: per model the
 * head group the glasses sit on and their quad in that group's coordinates (the
 * middle, half the width and half the height as vectors, and the middle seen from
 * behind: before the hindmost of the head's cards, which come first then); a NULL
 * group: none (a back view, a model with its own shades, no head). */
typedef struct CoolRow {
    const char* model;
    const char* group;
    float c[3], ax[3], ay[3], back[3];
} CoolRow;
static const CoolRow kCoolTable[] = {
#include "fun_cool.inc"
};

#define COOL_CACHE 64
typedef struct CoolEntry {
    const AnimationPoseData* data;
    char file[64];
    int group; /* -1: none */
    const CoolRow* row;
} CoolEntry;
static CoolEntry sCool[COOL_CACHE];

static const CoolEntry* coolOf(const AnimationPoseData* data) {
    CoolEntry* e = &sCool[((size_t)data >> 4) % COOL_CACHE];
    if (e->data != data || strncmp(e->file, data->fileName, sizeof(e->file)) != 0) {
        char model[64];
        size_t n, i;
        int g;
        memset(e, 0, sizeof(*e));
        e->data = data;
        e->group = -1;
        snprintf(e->file, sizeof(e->file), "%s", data->fileName);
        /* ours, made from the disc's at startup (Luigi's, Peach's, Bowser's), end in _pc: the disc's groups */
        snprintf(model, sizeof(model), "%s", data->fileName);
        n = strlen(model);
        if (n > 3 && strcmp(model + n - 3, "_pc") == 0) {
            model[n - 3] = '\0';
        }
        for (i = 0; i < sizeof(kCoolTable) / sizeof(kCoolTable[0]) && isCharacter(data->fileName); i++) {
            const CoolRow* r = &kCoolTable[i];
            if (strcmp(r->model, model) != 0 || r->group == NULL) {
                continue;
            }
            for (g = 0; g < data->groupCount; g++) {
                if (strcmp(data->groups[g].name, r->group) == 0) {
                    e->group = g;
                    e->row = r;
                    break;
                }
            }
            break;
        }
        if (getenv("TTYD_FUN_TRACE") != NULL) {
            printf("[fun] %s: cool %s\n", data->fileName, e->group >= 0 ? e->row->group : "none");
            fflush(stdout);
        }
    }
    return e;
}

/* the head kept by the draw under way (one model at a time: dispProc, then the draw's end) */
static const CoolRow* sCoolRow;
static Mtx sCoolMtx;

void portFunCoolMark(const void* poseData, int groupId, MtxPtr m) {
    const AnimationPoseData* data = poseData;
    const CoolEntry* e;
    if (data == NULL || !portFunOn(PORT_FUN_COOL)) {
        return;
    }
    e = coolOf(data);
    if (e->group == groupId && e->group >= 0) {
        sCoolRow = e->row;
        PSMTXCopy(m, sCoolMtx);
    }
}

static void mulPoint(MtxPtr m, const float* v, Vec* out) {
    out->x = m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + m[0][3];
    out->y = m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2] + m[1][3];
    out->z = m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + m[2][3];
}

static void mulDir(MtxPtr m, const float* v, Vec* out) {
    out->x = m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2];
    out->y = m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2];
    out->z = m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2];
}

/* the glasses' quad about c (view space), ax and ay half its width and height, nudged toward the
 * camera over the face's cards; blended (a fade, a ghost) or alpha-tested as the model is drawn */
static void coolQuad(Vec c, Vec ax, Vec ay, GXColor tint, int blend, GXTexObj* tex) {
    Mtx id;
    Vec corner[4];
    float d, len;
    int k;
    /* a little toward the camera, over the face's cards */
    len = sqrtf(c.x * c.x + c.y * c.y + c.z * c.z);
    d = 0.3f * sqrtf(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z);
    if (len > d * 2.0f) {
        c.x -= c.x * d / len, c.y -= c.y * d / len, c.z -= c.z * d / len;
    }
    for (k = 0; k < 4; k++) {
        const float sx = (k == 1 || k == 2) ? 1.0f : -1.0f, sy = k < 2 ? 1.0f : -1.0f;
        corner[k].x = c.x + sx * ax.x + sy * ay.x;
        corner[k].y = c.y + sx * ax.y + sy * ay.y;
        corner[k].z = c.z + sx * ax.z + sy * ay.z;
    }
    PSMTXIdentity(id);
    GXLoadPosMtxImm(id, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetChanMatColor(GX_COLOR0A0, tint);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetNumIndStages(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXLoadTexObj(tex, GX_TEXMAP0);
    GXSetCullMode(GX_CULL_NONE);
    if (blend) {
        GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
    } else {
        GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
        GXSetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        GXSetAlphaCompare(GX_GEQUAL, 0x80, GX_AOP_AND, GX_ALWAYS, 0);
    }
    GXSetZCompLoc(GX_FALSE);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    for (k = 0; k < 4; k++) {
        GXPosition3f32(corner[k].x, corner[k].y, corner[k].z);
        GXTexCoord2f32((k == 1 || k == 2) ? 1.0f : 0.0f, k < 2 ? 0.0f : 1.0f);
    }
    GXEnd();
}

void portFunCoolDraw(const void* posePtr, int mode, MtxPtr root) {
    const AnimationPose* pose = posePtr;
    const CoolRow* r = sCoolRow;
    GXColor tint = {0xFF, 0xFF, 0xFF, 0xFF};
    CameraEntry* cam;
    GXTexObj tex;
    Mtx mv;
    Vec c, cb, ax, ay, n;
    sCoolRow = NULL;
    if (r == NULL || mode < 0 || pose == NULL || portPaperShadowPass() || (cam = camGetCurPtr()) == NULL ||
        !portFunCoolTexture(&tex)) {
        return;
    }
    /* the model's colour, and its fade (as the PM64 partners' sprites take them) */
    if (pose->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR) {
        tint = pose->materialColor;
    }
    if (pose->materialColor2.r != 0xFF) {
        tint.a = (u8)(tint.a * pose->materialColor2.r / 255);
    }
    PSMTXConcat(cam->view, sCoolMtx, mv);
    mulPoint(mv, r->c, &c);
    mulPoint(mv, r->back, &cb);
    mulDir(mv, r->ax, &ax);
    mulDir(mv, r->ay, &ay);
    /* Either side of the head: a paper character facing right is its own cards turned half round
     * (seen from behind, the face's picture showing through, the glasses on it), and a back view is
     * a model of its own, with no glasses (a NULL row). Turned round, the head's cards come in the
     * other order, so the glasses go before the hindmost: whichever middle is nearer the camera.
     * Only a head seen edge-on (mid-turn) has none. */
    if (cb.x * cb.x + cb.y * cb.y + cb.z * cb.z < c.x * c.x + c.y * c.y + c.z * c.z) {
        c = cb;
    }
    n.x = ax.y * ay.z - ax.z * ay.y;
    n.y = ax.z * ay.x - ax.x * ay.z;
    n.z = ax.x * ay.y - ax.y * ay.x;
    (void)root;
    if (getenv("TTYD_FUN_TRACE") != NULL) {
        static int sTraced;
        if (sTraced < 3000) {
            sTraced++;
            printf("[fun] cool %s on %s: at %.1f %.1f %.1f (head %.1f %.1f %.1f, model %.1f %.1f %.1f), w %.2f h %.2f, "
                   "facing %.2f, alpha %d, stage %d\n",
                   r->model, r->group, c.x, c.y, c.z, sCoolMtx[0][3], sCoolMtx[1][3], sCoolMtx[2][3], root[0][3], root[1][3],
                   root[2][3], sqrtf(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z),
                   sqrtf(ay.x * ay.x + ay.y * ay.y + ay.z * ay.z),
                   (n.x * c.x + n.y * c.y + n.z * c.z) /
                       (sqrtf((n.x * n.x + n.y * n.y + n.z * n.z) * (c.x * c.x + c.y * c.y + c.z * c.z)) + 1e-9f),
                   tint.a, mode);
            fflush(stdout);
        }
    }
    if (fabsf(n.x * c.x + n.y * c.y + n.z * c.z) <=
        0.05f * sqrtf((n.x * n.x + n.y * n.y + n.z * n.z) * (c.x * c.x + c.y * c.y + c.z * c.z))) {
        return;
    }
    coolQuad(c, ax, ay, tint, mode == 3 || tint.a != 0xFF, &tex);
}

void portFunCoolSprite(MtxPtr m, float x0, float x1, float y0, float y1, const float at[3], const void* tintPtr) {
    const GXColor tint = *(const GXColor*)tintPtr;
    CameraEntry* cam;
    GXTexObj tex;
    Mtx mv;
    Vec c, ax, ay;
    const float w = (x1 - x0) * at[2], ratio = 32.0f / 72.0f; /* the shades' texture is 72 x 32 */
    float p[3], vx[3] = {0.0f, 0.0f, 0.0f}, vy[3] = {0.0f, 0.0f, 0.0f};
    if (!portFunOn(PORT_FUN_COOL) || portPaperShadowPass() || (cam = camGetCurPtr()) == NULL ||
        !portFunCoolTexture(&tex)) {
        return;
    }
    p[0] = x0 + (x1 - x0) * at[0];
    p[1] = y0 + (y1 - y0) * at[1];
    p[2] = 0.5f;
    vx[0] = w * 0.5f;
    vy[1] = w * 0.5f * ratio;
    PSMTXConcat(cam->view, m, mv);
    mulPoint(mv, p, &c);
    mulDir(mv, vx, &ax);
    mulDir(mv, vy, &ay);
    coolQuad(c, ax, ay, tint, tint.a != 0xFF, &tex);
}
