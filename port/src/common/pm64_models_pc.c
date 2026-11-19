/* The Paper Mario 64 partners in their own TTYD models (the setting "Partners' look"; port/include/port/pm64.h,
 * docs/ROM_LINK.md): the Thousand-Year Door's disc has a paper model of each of the eight (c_kurio Goombario ..
 * c_pokopi Lakilester), made for cameos, which stand and talk and little else. At startup each is made again from
 * the player's disc with every move a partner needs added by a recipe of ours (anim_recipe.h; the recipes:
 * tools/port/pm64_partner_motions.py) and served as a disc file (a/c_kurio_pc ..). With "Partners' look" on the
 * Thousand-Year Door paper (the default), the partner in a host's place is drawn as that model, a stand-in pose
 * following the host's (as a partner skin is, skin_pc.c), in the animation of what the partner is doing
 * (pm64_partner_pc.c picks it); on the sprites, or a model that couldn't be made, the ROM's sprite as before. The
 * stand-ins' table is in the save-state section (CMakeLists.txt); each use checks the pose is still the model's. */
#include "port/file_pc.h"
#include "port/anim_check.h"
#include "port/anim_recipe.h"
#include "port/costume.h"
#include "port/settings.h"
#include "port/mods.h"
#include "port/pm64.h"
#include "port/replay.h"

#include <dolphin/dvd.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/mapdrv.h"
#include "port/paper.h"
#include "port/paper_box.h"
#include "mario/mariost.h"

#define LOG(...) (printf(__VA_ARGS__), putchar('\n'), fflush(stdout))

#include "pm64_goombario_recipe.inc"
#include "pm64_kooper_recipe.inc"
#include "pm64_bombette_recipe.inc"
#include "pm64_parakarry_recipe.inc"
#include "pm64_watt_recipe.inc"
#include "pm64_sushie_recipe.inc"
#include "pm64_lakilester_recipe.inc"
#include "pm64_bow_recipe.inc"

/* the per-part poses (tools/port/pm64_poses.py write): a recipe baked on top of the motions', one animation a
 * sprite row ("PF06" field row 6, "PB12" battle row 12), and the rows' table */
typedef struct {
    unsigned char battle, row;
    const char* anim;
    short frames;
    unsigned char loop, exact, sprite; /* (exact: the partner's own mark; sprite: a sprite in play, the owner's list or
                                          not exact and not approved) */
    int firstEvent, nEvents;
    int firstExtra, nExtras;
    int firstPart, nParts;
    unsigned char tint[4]; /* the row's colour on the model (alpha 0: none) */
    int firstRule, nRules;
} Pm64PoseRow;
typedef struct {
    short f;
    const char* what;
} Pm64PoseEvent;
/* a row's step keys besides the joints': a texture slot's frame (value added to the model's own texture bind), a
 * group shown (value 1) or hidden (0) (POSE_SHOW; target: its visibility index), a tint (value 0xRRGGBB; -1 none) */
enum { POSE_TEX, POSE_SHOW, POSE_HIDE, POSE_TINT };
/* a recolour rule on the model's own textures (the poses' "recolor" and "texture_edits"; a row's rules in order,
 * the first a texel matches is applied, none: it stays): the texels of shape `shape` (-1 every shape) that are
 * inside rect (when & 1: box x y w h, texture pixels from the top left), inside / outside the ellipse (2 / 4: ell
 * cx cy rx ry), of luma lumaMin..lumaMax (8; 0.299 r + 0.587 g + 0.114 b) and near one of near[] within tol (16)
 * become: act 0 rgb, 1 clear, 2 the texture's own most common colour, 3 grey by the curve gray[] (luma in, grey
 * out, piecewise linear, clamped), 4 the colour of the nearest of the sprite's palette palFrom (-1: component
 * comp's raster's own) in palette palTo (-2: the component's this frame) */
enum { RULE_RECT = 1, RULE_IN_ELLIPSE = 2, RULE_OUT_ELLIPSE = 4, RULE_LUMA = 8, RULE_NEAR = 16 };
enum { ACT_RGB, ACT_CLEAR, ACT_BASE, ACT_GRAY, ACT_PALETTE };
typedef struct {
    short shape;
    unsigned char when, act;
    float box[4], ell[4];
    short lumaMin, lumaMax;
    unsigned char nNear, tol;
    unsigned char nearC[4][3];
    unsigned char rgb[3];
    unsigned char nGray;
    unsigned char gray[8][2];
    signed char comp;
    short palFrom, palTo;
    unsigned char pre; /* a texture_edits rule: the first pass (the edited texels then go through the row's) */
} Pm64PoseRule;
typedef struct {
    short f;
    unsigned char kind;
    short target;
    int value;
} Pm64PoseExtra;
/* the sprite's own pixels on the model (the poses' "sprite_parts"; step keys, a track each, keys in f order): from
 * frame f, the sprite's raster (raster >= 0; -1: what sprite component `component` shows this frame), palette (-1
 * its own; -2 the component's this frame), the rect of it (w 0: all), only the pixels of the palette indices in
 * mask (0: all) and those of `edge` touching a mask pixel and none of `notNear`; drawn either
 *   shape >= 0: as that plane's texture, over its own texture area uv[4] (u0 v0 u1 v1), or
 *   shape -1: as a quad of its own in the model's space, the rect's top left at at[], px[] model units a pixel,
 *     at depth z, turned rz degrees about its middle
 * flags: 1 mirrored across, 2 nothing drawn from this key (the plane's own texture again) */
enum { PART_FLIP = 1, PART_OFF = 2 };
typedef struct {
    short f, track, shape, raster;
    signed char component;
    unsigned char flags;
    short palette;
    unsigned short mask, edge, notNear;
    short rect[4];
    float uv[4];
    float at[2], px[2], z, rz;
    short joint;      /* a quad following this joint (its turn, squash, place): -1 the model's own space */
    float rest[3][4]; /* (then: the model space to the joint's at the row's rest, for the corners) */
    unsigned char followMode; /* 0 all of the joint's move, 1 its place only, 2 its place and size (no turn: a
                                 drawing that carries its own lean) */
} Pm64PosePart;
typedef struct {
    const PortRecipe* recipe; /* NULL: no poses yet */
    const Pm64PoseRow* rows;
    int nRows;
    const Pm64PoseEvent* events;
    const Pm64PoseExtra* extras;
    const Pm64PosePart* parts;
    const Pm64PoseRule* rules;
} Pm64Poses;
#include "pm64_goombario_poses.inc"
#include "pm64_kooper_poses.inc"
#include "pm64_bombette_poses.inc"
#include "pm64_parakarry_poses.inc"
#include "pm64_watt_poses.inc"
#include "pm64_sushie_poses.inc"
#include "pm64_lakilester_poses.inc"
#include "pm64_bow_poses.inc"

/* by PM64 partner number (PM64_PARTNER_GOOMBARIO 1 .. PM64_PARTNER_BOW 8) */
static const struct {
    const char *base, *name;
    const PortRecipe* recipe;
    const Pm64Poses* poses;
} kModels[PM64_PARTNER_COUNT] = {
    {NULL, NULL, NULL, NULL},
    {"c_kurio", "c_kurio_pc", &kPm64GoombarioRecipe, &kPm64GoombarioPoses},
    {"c_kameki", "c_kameki_pc", &kPm64KooperRecipe, &kPm64KooperPoses},
    {"c_pinky", "c_pinky_pc", &kPm64BombetteRecipe, &kPm64BombettePoses},
    {"c_paretta", "c_paretta_pc", &kPm64ParakarryRecipe, &kPm64ParakarryPoses},
    {"c_akarin", "c_akarin_pc", &kPm64WattRecipe, &kPm64WattPoses},
    {"c_opuku", "c_opuku_pc", &kPm64SushieRecipe, &kPm64SushiePoses},
    {"c_pokopi", "c_pokopi_pc", &kPm64LakilesterRecipe, &kPm64LakilesterPoses},
    {"c_resaresa", "c_resaresa_pc", &kPm64BowRecipe, &kPm64BowPoses},
};
static unsigned char sMade[PM64_PARTNER_COUNT];
static unsigned char sPosed[PM64_PARTNER_COUNT]; /* its poses baked on */

static uint8_t* readDisc(const char* path, uint32_t* size) {
    DVDFileInfo info;
    uint8_t* data;
    uint32_t len;
    if (!DVDOpen(path, &info)) {
        return NULL;
    }
    *size = info.length;
    len = (*size + 31) & ~31u;
    data = malloc(len);
    if (data != NULL && DVDReadPrio(&info, data, (s32)len, 0, 2) < 0) {
        free(data);
        data = NULL;
    }
    DVDClose(&info);
    return data;
}

/* another of the disc's models, for a recipe's from_model (anim_recipe.h: malloc'd, the maker frees it) */
static uint8_t* loadModel(const char* model, uint32_t* size) {
    char path[80];
    snprintf(path, sizeof(path), "a/%s", model);
    return readDisc(path, size);
}

static int recipeAnim(const char* name, void* user) {
    const int p = (int)(intptr_t)user;
    return portAnimRecipeHas(kModels[p].recipe, name) ||
           (kModels[p].poses->recipe != NULL && portAnimRecipeHas(kModels[p].poses->recipe, name));
}

/* the poses' recipe on the made model (a second pass of the maker on its own output) */
static int posesBake(int p, uint8_t** out, uint32_t* outSize) {
    uint8_t* posed = NULL;
    uint32_t posedSize = 0;
    char why[200];
    if (!portAnimRecipeMake(*out, *outSize, kModels[p].poses->recipe, kModels[p].name, &posed, &posedSize, why,
                            sizeof(why))) {
        LOG("[pm64] poses: couldn't bake %s's poses (%s); its rows stay sprites", kModels[p].name, why);
        return 0;
    }
    if (!portAnimCheckModel(posed, posedSize, recipeAnim, (void*)(intptr_t)p, why, sizeof(why))) {
        LOG("[pm64] poses: %s with its poses fails its check (%s); its rows stay sprites", kModels[p].name, why);
        free(posed);
        return 0;
    }
    free(*out);
    *out = posed;
    *outSize = posedSize;
    sPosed[p] = 1;
    LOG("[pm64] poses: %s: %d rows posed", kModels[p].name, kModels[p].poses->nRows);
    return 1;
}

/* made once a Paper Mario ROM is linked (at the start, or later from the Paper Mario 64 page: portPm64ModelsFrame);
 * with none, nothing of Paper Mario 64 is made or logged (modes_master_quest) */
static int sModelsWait;

/* This release ships without the paper models (to the next): the sprites for everyone, a saved choice of the
 * models and $TTYD_PM64_POSES alike; the setting's row is gone (pm64_link.cpp). 1 brings them back. */
#define PM64_PAPER_LOOK 0

static void modelsMake(void) {
    int p, made = 0;
    portAnimRecipeSetLoader(loadModel); /* (a recipe's or a pose's "from_model": Sushie from c_puku) */
    for (p = 1; p < PM64_PARTNER_COUNT; p++) {
        uint32_t size = 0, outSize = 0;
        uint8_t* disc;
        uint8_t* out = NULL;
        char path[64], why[200];
        sMade[p] = 0;
        snprintf(path, sizeof(path), "a/%s", kModels[p].base);
        if ((disc = readDisc(path, &size)) == NULL) {
            LOG("[pm64] models: the disc's %s is missing; that partner stays the ROM's sprite", kModels[p].base);
            continue;
        }
        sPosed[p] = 0;
        if (!portAnimRecipeMake(disc, size, kModels[p].recipe, kModels[p].name, &out, &outSize, why, sizeof(why))) {
            LOG("[pm64] models: couldn't make %s (%s)", kModels[p].name, why);
        } else if (kModels[p].poses->recipe != NULL && kModels[p].poses->nRows > 0) {
            posesBake(p, &out, &outSize); /* (if it can't: the model without them, its rows the sprite's) */
        }
        if (out != NULL && !portAnimCheckModel(out, outSize, recipeAnim, (void*)(intptr_t)p, why, sizeof(why))) {
            LOG("[pm64] models: %s fails its check (%s)", kModels[p].name, why);
        } else if (out != NULL) {
            snprintf(path, sizeof(path), "/a/%s", kModels[p].name);
            portModsSetHostFile(path, out, outSize);
            sMade[p] = 1;
            made++;
        }
        free(out);
        free(disc);
    }
    LOG("[pm64] models: %d of %d partners' TTYD models made from the disc", made, PM64_PARTNER_COUNT - 1);
}

void portPm64ModelsInit(void) {
    if (!PM64_PAPER_LOOK) {
        return; /* (the sprites only: nothing made, below) */
    }
    if (!portPm64Linked()) {
        sModelsWait = 1;
        return;
    }
    modelsMake();
}

/* The setting "Partners' look" (pm64_link.cpp, pm64_partners_look): 0 the Thousand-Year Door paper (these models,
 * the default), 1 the Paper Mario 64 sprites. $TTYD_PM64_POSES (tests) overrides it: 1 the models, 0 the sprites. */
static int modelsLook(void) {
#if !PM64_PAPER_LOOK
    return 0;
#else
    static int sEnv = -2;
    static int* sSet;
    if (sEnv == -2) {
        const char* e = getenv("TTYD_PM64_POSES");
        sEnv = e != NULL && e[0] != 0 ? atoi(e) > 0 : -1;
    }
    if (sEnv >= 0) {
        return sEnv;
    }
    if (sSet == NULL) {
        sSet = portSettingsDeclare("pm64_partners_look", 0, 0, 1);
    }
    return sSet == NULL || *sSet == 0;
#endif
}

/* In replays, none: the stand-in is a pose in the game's pool (as a skin's, skin_pc.c) */
const char* portPm64PartnerModel(int p) {
    if (p <= 0 || p >= PM64_PARTNER_COUNT || !sMade[p] || !modelsLook() || portReplayActive()) {
        return NULL;
    }
    return kModels[p].name;
}

/* --- the stand-ins */

#define STANDS 8
static struct {
    int source1; /* the host's pose + 1 (0: none) */
    int pose1;   /* the model's + 1 */
    int p;
    char anim[16];
    unsigned stepped;
} sStand[STANDS];

/* this frame's sprite-pixel parts of each stand-in: planes (portPm64ShapeTex: the model's draw asks) and quads
 * (quadsDraw, after the model), and its tint */
#define PARTS_MAX 12
static struct {
    int n, nq;
    struct {
        int shape;
        GXTexObj tex;
        Mtx mtx;
    } part[PARTS_MAX];
    struct {
        GXTexObj tex;
        float s0, t0, s1, t1;
        float x[4], y[4], z;
        int joint;  /* -1: on the model's matrix; else that group's world matrix times rest */
        Mtx rest;
        int mode;   /* (followMode) */
        float cx, cy; /* its middle, model space */
    } quad[PARTS_MAX];
    int tint; /* 0xRRGGBB, -1 none */
    const AnimationPose* pose;
    const Pm64PoseRow* row;     /* this frame's row (its recolour rules) */
    int recolor;                /* it has rules */
    uint8_t from[16][4], to[16][4]; /* (ACT_PALETTE's two palettes this frame, the row's first such rule's) */
    unsigned key;               /* (their hash: the recoloured textures' cache) */
} sLook[STANDS];

/* the recoloured textures, made on first use and kept (the model's texture, the palettes): GX RGBA8 tiles */
#define RECOLORED_MAX 192
static struct {
    const void* src;
    int fmt, w, h, shape;
    const Pm64PoseRow* row;
    unsigned key;
    uint8_t* tex;
} sRecolored[RECOLORED_MAX];
static int sRecoloredN;

static int lumaOf(const uint8_t* c) {
    return (int)(0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2] + 0.5f);
}

static int ruleMatches(const Pm64PoseRule* r, const uint8_t* c, float x, float y) {
    if ((r->when & RULE_RECT) && (x < r->box[0] || y < r->box[1] || x >= r->box[0] + r->box[2] ||
                                  y >= r->box[1] + r->box[3])) {
        return 0;
    }
    if (r->when & (RULE_IN_ELLIPSE | RULE_OUT_ELLIPSE)) {
        const float dx = (x - r->ell[0]) / (r->ell[2] > 0.0f ? r->ell[2] : 1.0f);
        const float dy = (y - r->ell[1]) / (r->ell[3] > 0.0f ? r->ell[3] : 1.0f);
        const int in = dx * dx + dy * dy <= 1.0f;
        if (((r->when & RULE_IN_ELLIPSE) && !in) || ((r->when & RULE_OUT_ELLIPSE) && in)) {
            return 0;
        }
    }
    if (r->when & RULE_LUMA) {
        const int l = lumaOf(c);
        if (l < r->lumaMin || l > r->lumaMax) {
            return 0;
        }
    }
    if (r->when & RULE_NEAR) {
        int k, hit = 0;
        for (k = 0; k < r->nNear && !hit; k++) {
            const int dr = c[0] - r->nearC[k][0], dg = c[1] - r->nearC[k][1], db = c[2] - r->nearC[k][2];
            hit = dr * dr + dg * dg + db * db <= (int)r->tol * r->tol;
        }
        if (!hit) {
            return 0;
        }
    }
    return 1;
}

/* the model's texture t of shape `shape` with this frame's row's rules, made on first use and kept: GX RGBA8 */
static const void* recolored(int s, int p, const PortBoxTex* t, int shape) {
    const Pm64PoseRow* row = sLook[s].row;
    const Pm64PoseRule* rules = kModels[p].poses->rules + (row != NULL ? row->firstRule : 0);
    uint8_t* rgba;
    uint8_t* out;
    uint8_t base[3] = {0, 0, 0};
    int i, k, tx, ty, x, y, any = 0;
    const int tw = (t->width + 3) / 4, th = (t->height + 3) / 4;
    if (row == NULL) {
        return NULL;
    }
    for (k = 0; k < row->nRules; k++) {
        any |= rules[k].shape < 0 || rules[k].shape == shape;
    }
    if (!any) {
        return NULL;
    }
    for (i = 0; i < sRecoloredN; i++) {
        if (sRecolored[i].src == t->data && sRecolored[i].fmt == t->format && sRecolored[i].w == t->width &&
            sRecolored[i].h == t->height && sRecolored[i].key == sLook[s].key && sRecolored[i].row == row &&
            sRecolored[i].shape == shape) {
            return sRecolored[i].tex;
        }
    }
    if (sRecoloredN >= RECOLORED_MAX || t->tlut != NULL) {
        return NULL;
    }
    rgba = (uint8_t*)malloc((size_t)t->width * t->height * 4);
    out = (uint8_t*)calloc((size_t)tw * th, 64);
    if (rgba == NULL || out == NULL || !portPm64DecodeTexture(t->data, t->format, t->width, t->height, rgba)) {
        free(rgba);
        free(out);
        return NULL;
    }
    {
        /* (the texture's most common opaque colour, by 5 bits a channel: ACT_BASE) */
        static unsigned short count[32768];
        int best = -1;
        memset(count, 0, sizeof(count));
        for (i = 0; i < t->width * t->height; i++) {
            const uint8_t* c = rgba + (size_t)i * 4;
            if (c[3] >= 128) {
                const int q = (c[0] >> 3) << 10 | (c[1] >> 3) << 5 | (c[2] >> 3);
                if (count[q] < 65535) {
                    count[q]++;
                }
                if (best < 0 || count[q] > count[best]) {
                    best = q;
                }
            }
        }
        if (best >= 0) {
            base[0] = (uint8_t)(((best >> 10) & 31) * 255 / 31);
            base[1] = (uint8_t)(((best >> 5) & 31) * 255 / 31);
            base[2] = (uint8_t)((best & 31) * 255 / 31);
        }
    }
    for (i = 0; i < t->width * t->height; i++) {
        uint8_t* c = rgba + (size_t)i * 4;
        const float fx = (float)(i % t->width) + 0.5f, fy = (float)(i / t->width) + 0.5f;
        if (c[3] == 0) {
            continue;
        }
        int pass;
        for (pass = 0; pass < 2; pass++)
        for (k = 0; k < row->nRules; k++) {
            const Pm64PoseRule* r = &rules[k];
            if (c[3] == 0) {
                break; /* (cleared by the first pass) */
            }
            if ((int)r->pre != (pass == 0) || (r->shape >= 0 && r->shape != shape) || !ruleMatches(r, c, fx, fy)) {
                continue;
            }
            switch (r->act) {
            case ACT_RGB:
                c[0] = r->rgb[0];
                c[1] = r->rgb[1];
                c[2] = r->rgb[2];
                break;
            case ACT_CLEAR:
                c[0] = c[1] = c[2] = c[3] = 0;
                break;
            case ACT_BASE:
                c[0] = base[0];
                c[1] = base[1];
                c[2] = base[2];
                break;
            case ACT_GRAY: {
                const int l = lumaOf(c);
                int g = r->nGray > 0 ? r->gray[0][1] : l, q;
                for (q = 0; q < r->nGray; q++) {
                    if (l <= r->gray[q][0]) {
                        g = q == 0 ? r->gray[0][1]
                                   : r->gray[q - 1][1] + (r->gray[q][1] - r->gray[q - 1][1]) * (l - r->gray[q - 1][0]) /
                                                             (r->gray[q][0] - r->gray[q - 1][0] ? r->gray[q][0] - r->gray[q - 1][0] : 1);
                        break;
                    }
                    g = r->gray[q][1];
                }
                c[0] = c[1] = c[2] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
                break;
            }
            case ACT_PALETTE: {
                int q, best = -1, bestD = 1 << 30;
                for (q = 0; q < 16; q++) {
                    const int dr = c[0] - sLook[s].from[q][0], dg = c[1] - sLook[s].from[q][1],
                              db = c[2] - sLook[s].from[q][2];
                    const int d = dr * dr + dg * dg + db * db;
                    if (sLook[s].from[q][3] != 0 && d < bestD) {
                        bestD = d;
                        best = q;
                    }
                }
                if (best >= 0) {
                    c[0] = sLook[s].to[best][0];
                    c[1] = sLook[s].to[best][1];
                    c[2] = sLook[s].to[best][2];
                }
                break;
            }
            }
            break; /* (the first rule it matches, in each pass: texture_edits', then the row's) */
        }
    }
    /* GX RGBA8: each 4x4 tile is 32 bytes of alpha-red pairs, then 32 of green-blue */
    for (ty = 0; ty < th; ty++) {
        for (tx = 0; tx < tw; tx++) {
            uint8_t* tile = out + ((size_t)ty * tw + tx) * 64;
            for (y = 0; y < 4; y++) {
                for (x = 0; x < 4; x++) {
                    const int px = tx * 4 + x, py = ty * 4 + y, q = y * 4 + x;
                    if (px < t->width && py < t->height) {
                        const uint8_t* c = rgba + ((size_t)py * t->width + px) * 4;
                        tile[q * 2] = c[3];
                        tile[q * 2 + 1] = c[0];
                        tile[32 + q * 2] = c[1];
                        tile[32 + q * 2 + 1] = c[2];
                    }
                }
            }
        }
    }
    free(rgba);
    sRecolored[sRecoloredN].src = t->data;
    sRecolored[sRecoloredN].fmt = t->format;
    sRecolored[sRecoloredN].w = t->width;
    sRecolored[sRecoloredN].h = t->height;
    sRecolored[sRecoloredN].row = row;
    sRecolored[sRecoloredN].shape = shape;
    sRecolored[sRecoloredN].key = sLook[s].key;
    sRecolored[sRecoloredN].tex = out;
    sRecoloredN++;
    return out;
}

static int standValid(int s) {
    AnimationPose* d;
    AnimationPose* src;
    if (sStand[s].pose1 == 0 || sStand[s].source1 == 0 || sStand[s].p <= 0 || sStand[s].p >= PM64_PARTNER_COUNT) {
        return 0;
    }
    d = animPoseGetAnimPosePtr(sStand[s].pose1 - 1);
    src = animPoseGetAnimPosePtr(sStand[s].source1 - 1);
    return (d->flags & ANIM_POSE_FLAG_USED) && d->typeFlags == 0 && d->type == src->type &&
           !strcmp(animPoseGetAnimBaseDataPtr(sStand[s].pose1 - 1)->fileName, kModels[sStand[s].p].name);
}

static void standFree(int s) {
    if (standValid(s)) {
        animPoseRelease(sStand[s].pose1 - 1);
    }
    sStand[s].pose1 = 0;
    sStand[s].source1 = 0;
    sStand[s].anim[0] = '\0';
    sLook[s].n = 0;
    sLook[s].nq = 0;
    sLook[s].tint = -1;
    sLook[s].pose = NULL;
}

#define WANTS 4
static struct {
    int source1, p;
} sWant[WANTS];
static int sWants;

/* each frame, before any draw: the stand-ins asked for while drawing loaded (never while drawing) */
void portPm64ModelsFrame(void) {
    int w, s;
    if (sModelsWait && portPm64Linked()) {
        sModelsWait = 0;
        modelsMake();
    }
    for (w = 0; w < sWants; w++) {
        const int poseId = sWant[w].source1 - 1;
        const int p = sWant[w].p;
        AnimationPose* src;
        int pose;
        if (poseId < 0 || portPm64PartnerModel(p) == NULL) {
            continue;
        }
        src = animPoseGetAnimPosePtr(poseId);
        if (!(src->flags & ANIM_POSE_FLAG_USED) || src->typeFlags != 0) {
            continue;
        }
        for (s = 0; s < STANDS && sStand[s].source1 != poseId + 1; s++) {
        }
        if (s < STANDS) {
            continue; /* (asked twice) */
        }
        /* a free slot, or one whose host's pose is gone */
        for (s = 0; s < STANDS; s++) {
            const int old = sStand[s].source1 - 1;
            if (old < 0 || !(animPoseGetAnimPosePtr(old)->flags & ANIM_POSE_FLAG_USED) || !standValid(s)) {
                standFree(s);
                break;
            }
        }
        if (s == STANDS || !portCosmeticPoolRoom()) {
            continue;
        }
        pose = animPoseEntry(kModels[p].name, src->type);
        if (pose < 0) {
            continue;
        }
        if (portPm64PosesOn()) {
            animPoseWorldMatrixEvalOn(pose); /* (the sprite parts that follow a joint read its matrix) */
        }
        sStand[s].source1 = poseId + 1;
        sStand[s].pose1 = pose + 1;
        sStand[s].p = p;
        sStand[s].anim[0] = '\0';
        sStand[s].stepped = 0;
    }
    sWants = 0;
}

static const Pm64PoseRow* poseRowByAnim(int p, const char* anim) {
    int i;
    if (p <= 0 || p >= PM64_PARTNER_COUNT || !sPosed[p] || anim == NULL) {
        return NULL;
    }
    for (i = 0; i < kModels[p].poses->nRows; i++) {
        if (!strcmp(kModels[p].poses->rows[i].anim, anim)) {
            return &kModels[p].poses->rows[i];
        }
    }
    return NULL;
}

/* the key of a step track (keys n from k0, by f) in force at frame: the last at or before it; before the first, a
 * loop's last (it wraps), else none (-1). key(i) gives key i's frame. */
#define STEP_AT(out, n, fOf, frame, loop)                                                                          \
    do {                                                                                                           \
        int i_;                                                                                                    \
        (out) = -1;                                                                                                \
        for (i_ = 0; i_ < (n); i_++) {                                                                             \
            if ((float)(fOf(i_)) <= (frame)) {                                                                     \
                (out) = i_;                                                                                        \
            }                                                                                                      \
        }                                                                                                          \
        if ((out) < 0 && (loop) && (n) > 0) {                                                                      \
            (out) = (n) - 1;                                                                                       \
        }                                                                                                          \
    } while (0)

/* the row's extras and parts at this frame, after the animation's own (animPoseMain): texture frames and groups on
 * the pose's buffers, the tint and the sprite-pixel parts kept for this frame's draws */
static void lookStep(int s, int p, int pose, const char* anim, float frame, const Pm64Anim* sprite) {
    const Pm64PoseRow* r = frame >= 0.0f ? poseRowByAnim(p, anim) : NULL;
    AnimationPose* dst = animPoseGetAnimPosePtr(pose);
    AnimationPoseData* dd = animPoseGetAnimBaseDataPtr(pose);
    int i, j, k;
    sLook[s].n = 0;
    sLook[s].nq = 0;
    sLook[s].tint = -1;
    sLook[s].pose = dst;
    sLook[s].recolor = 0;
    sLook[s].row = r;
    sLook[s].key = 0;
    if (r == NULL) {
        return;
    }
    sLook[s].recolor = r->nRules > 0;
    for (i = 0; i < r->nRules; i++) {
        const Pm64PoseRule* q = &kModels[p].poses->rules[r->firstRule + i];
        int raster, fromPal, toPal, n;
        if (q->act != ACT_PALETTE) {
            continue;
        }
        /* (the first palette rule's palettes, this frame: burnt while the sprite draws burnt) */
        if (sprite == NULL || sprite->sprite == NULL || q->comp < 0 || q->comp >= sprite->count ||
            (raster = sprite->part[q->comp].raster) < 0) {
            break;
        }
        fromPal = q->palFrom >= 0 ? q->palFrom : portPm64SpriteRasterPalette(sprite->sprite, raster);
        toPal = q->palTo >= 0 ? q->palTo
                : sprite->part[q->comp].palette >= 0 ? sprite->part[q->comp].palette
                                                    : portPm64SpriteRasterPalette(sprite->sprite, raster);
        if (fromPal < 0 || toPal < 0 || !portPm64SpritePaletteRGBA(sprite->sprite, fromPal, sLook[s].from) ||
            !portPm64SpritePaletteRGBA(sprite->sprite, toPal, sLook[s].to)) {
            break;
        }
        if (fromPal == toPal) {
            memcpy(sLook[s].to, sLook[s].from, sizeof(sLook[s].to));
        }
        sLook[s].key = 2166136261u;
        for (n = 0; n < 16 * 4; n++) {
            sLook[s].key = (sLook[s].key ^ sLook[s].from[n / 4][n % 4]) * 16777619u;
            sLook[s].key = (sLook[s].key ^ sLook[s].to[n / 4][n % 4]) * 16777619u;
        }
        break;
    }
    if (r->tint[3] != 0) {
        sLook[s].tint = (r->tint[0] << 16) | (r->tint[1] << 8) | r->tint[2];
    }
    /* the extras: each (kind, target) a step track, its keys in f order in the table */
    for (i = 0; i < r->nExtras; i = j) {
        const Pm64PoseExtra* e = &kModels[p].poses->extras[r->firstExtra + i];
        const Pm64PoseExtra* at;
        for (j = i + 1; j < r->nExtras && e[j - i].kind == e->kind && e[j - i].target == e->target; j++) {
        }
#define EXTRA_F(q) (e[q].f)
        STEP_AT(k, j - i, EXTRA_F, frame, r->loop);
#undef EXTRA_F
        if (k < 0) {
            continue;
        }
        at = &e[k];
        switch (at->kind) {
        case POSE_TEX:
            if (at->target >= 0 && (u32)at->target < dd->bufferTexAnimNum && dst->texAnimEntries != NULL) {
                dst->texAnimEntries[at->target].texBindIdx =
                    (u8)(dd->texAnimEntries[at->target].texBindIdx + at->value);
            }
            break;
        case POSE_SHOW:
        case POSE_HIDE:
            if (at->target >= 0 && (u32)at->target < dd->bufferVisibilityNum && dst->groupVisibility != NULL) {
                dst->groupVisibility[at->target] = (s8)(at->value != 0 ? 1 : 0);
            }
            break;
        case POSE_TINT:
            sLook[s].tint = at->value;
            break;
        }
    }
    /* the sprite parts: each track's key in force */
    for (i = 0; i < r->nParts; i = j) {
        const Pm64PosePart* q = &kModels[p].poses->parts[r->firstPart + i];
        const Pm64PosePart* at;
        const void* data;
        int raster, palette, w, h, tw, th;
        float rx, ry, rw, rh;
        for (j = i + 1; j < r->nParts && q[j - i].track == q->track; j++) {
        }
#define PART_F(n) (q[n].f)
        STEP_AT(k, j - i, PART_F, frame, r->loop);
#undef PART_F
        if (k < 0 || (q[k].flags & PART_OFF) || sprite == NULL || sprite->sprite == NULL) {
            continue;
        }
        at = &q[k];
        if (at->shape >= 0 ? sLook[s].n >= PARTS_MAX : sLook[s].nq >= PARTS_MAX) {
            continue;
        }
        raster = at->raster;
        palette = at->palette == -2 ? -1 : at->palette;
        if (raster < 0) {
            if (at->component < 0 || at->component >= sprite->count) {
                continue;
            }
            raster = sprite->part[at->component].raster;
            if (at->palette == -2) {
                palette = sprite->part[at->component].palette;
            }
        }
        if (raster < 0 || !portPm64SpriteRaster(sprite->sprite, raster, &w, &h, NULL) || w <= 0 || h <= 0 ||
            (data = portPm64RasterTextureCut(sprite->sprite, raster, palette, at->mask, at->edge, at->notNear, &tw,
                                             &th)) == NULL ||
            tw <= 0 || th <= 0) {
            continue;
        }
        rx = (float)at->rect[0];
        ry = (float)at->rect[1];
        rw = at->rect[2] > 0 ? (float)at->rect[2] : (float)w;
        rh = at->rect[3] > 0 ? (float)at->rect[3] : (float)h;
        if (at->shape >= 0) {
            /* the plane's (u, v) over its texture area onto the rect: s = a u + b, t = c v + d (in the raster's
               texture, its pixels from its top left) */
            const float du = at->uv[2] - at->uv[0], dv = at->uv[3] - at->uv[1];
            float a, b, c, d;
            if (du == 0.0f || dv == 0.0f) {
                continue;
            }
            a = rw / (du * (float)tw);
            b = rx / (float)tw - a * at->uv[0];
            if (at->flags & PART_FLIP) {
                b = rx / (float)tw + a * at->uv[2];
                a = -a;
            }
            c = rh / (dv * (float)th);
            d = ry / (float)th - c * at->uv[1];
            k = sLook[s].n++;
            sLook[s].part[k].shape = at->shape;
            GXInitTexObj(&sLook[s].part[k].tex, (void*)data, (u16)tw, (u16)th, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP,
                         GX_FALSE);
            GXInitTexObjLOD(&sLook[s].part[k].tex, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE,
                            GX_ANISO_1);
            PSMTXIdentity(sLook[s].part[k].mtx);
            sLook[s].part[k].mtx[0][0] = a;
            sLook[s].part[k].mtx[0][3] = b;
            sLook[s].part[k].mtx[1][1] = c;
            sLook[s].part[k].mtx[1][3] = d;
        } else {
            /* a quad of its own: the rect's corners in the model's space (top left at, px a pixel), turned about
               its middle */
            const float w0 = rw * at->px[0], h0 = rh * at->px[1];
            const float cx = at->at[0] + 0.5f * w0, cy = at->at[1] - 0.5f * h0;
            const float c = cosf(0.017453292f * at->rz), sn = sinf(0.017453292f * at->rz);
            const float ox[4] = {-0.5f * w0, 0.5f * w0, 0.5f * w0, -0.5f * w0};
            const float oy[4] = {0.5f * h0, 0.5f * h0, -0.5f * h0, -0.5f * h0};
            int n;
            k = sLook[s].nq++;
            GXInitTexObj(&sLook[s].quad[k].tex, (void*)data, (u16)tw, (u16)th, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP,
                         GX_FALSE);
            GXInitTexObjLOD(&sLook[s].quad[k].tex, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE,
                            GX_ANISO_1);
            sLook[s].quad[k].s0 = rx / (float)tw;
            sLook[s].quad[k].s1 = (rx + rw) / (float)tw;
            if (at->flags & PART_FLIP) {
                sLook[s].quad[k].s0 = (rx + rw) / (float)tw;
                sLook[s].quad[k].s1 = rx / (float)tw;
            }
            sLook[s].quad[k].t0 = ry / (float)th;
            sLook[s].quad[k].t1 = (ry + rh) / (float)th;
            for (n = 0; n < 4; n++) {
                sLook[s].quad[k].x[n] = cx + c * ox[n] - sn * oy[n];
                sLook[s].quad[k].y[n] = cy + sn * ox[n] + c * oy[n];
            }
            sLook[s].quad[k].z = at->z;
            sLook[s].quad[k].joint = at->joint;
            sLook[s].quad[k].mode = at->followMode;
            sLook[s].quad[k].cx = cx;
            sLook[s].quad[k].cy = cy;
            for (n = 0; n < 3; n++) {
                sLook[s].quad[k].rest[n][0] = at->rest[n][0];
                sLook[s].quad[k].rest[n][1] = at->rest[n][1];
                sLook[s].quad[k].rest[n][2] = at->rest[n][2];
                sLook[s].quad[k].rest[n][3] = at->rest[n][3];
            }
        }
    }
}

/* the stand-in's sprite quads (lookStep), on m: the model's own matrix (its place, turn and size), lit as the
 * model is and in its material's colour (a tint, the host's fire blackening, a fade); alpha-tested pixels */
static void quadsDraw(int s, Mtx m, const AnimationPose* pose) {
    CameraEntry* cam = camGetCurPtr();
    Mtx mv, nrm;
    Vec at = {0.0f, 0.0f, 0.0f};
    GXColor tint = {0xFF, 0xFF, 0xFF, 0xFF};
    int k, n;
    if (cam == NULL || sLook[s].nq <= 0) {
        return;
    }
    if (pose->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR) {
        tint = pose->materialColor;
    }
    if (pose->materialColor2.r != 0xFF) {
        tint.a = (u8)(tint.a * pose->materialColor2.r / 255);
    }
    PSMTXMultVec(m, &at, &at);
    mapSetMaterialLight(pose->materialLightFlag | ANIM_MATERIAL_LIGHT_NO_VTX_COLOR, at);
    PSMTXConcat(cam->view, m, mv);
    GXLoadPosMtxImm(mv, GX_PNMTX0);
    PSMTXInvXpose(mv, nrm);
    GXLoadNrmMtxImm(nrm, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_NRM, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetNumIndStages(0);
    GXSetNumTevStages(2);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevDirect(GX_TEVSTAGE1);
    GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevKColor(GX_KCOLOR0, tint);
    GXSetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
    GXSetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_K0_A);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
    GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_KONST, GX_CA_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetAlphaCompare(GX_GREATER, 8, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZCompLoc(GX_FALSE);
    GXSetZMode(GX_TRUE, GX_LEQUAL, tint.a == 0xFF ? GX_TRUE : GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetCullMode(GX_CULL_NONE);
    for (k = 0; k < sLook[s].nq; k++) {
        const float su[4] = {sLook[s].quad[k].s0, sLook[s].quad[k].s1, sLook[s].quad[k].s1, sLook[s].quad[k].s0};
        const float sv[4] = {sLook[s].quad[k].t0, sLook[s].quad[k].t0, sLook[s].quad[k].t1, sLook[s].quad[k].t1};
        const int j = sLook[s].quad[k].joint;
        if (j >= 0 && (pose->flags & ANIM_POSE_FLAG_WORLD_MATRICES) && pose->worldMatrices != NULL &&
            j < pose->numWorldMatrices) {
            /* (following a joint: its world matrix this draw, from the row's rest: it turns and squashes along) */
            Mtx w;
            PSMTXConcat((*pose->worldMatrices)[j], sLook[s].quad[k].rest, w);
            if (sLook[s].quad[k].mode != 0) {
                /* (its place only, or with its size: the model's own axes, moved where the joint takes the quad's
                   middle, scaled about it as the joint's axes are against the model's) */
                Vec c0 = {sLook[s].quad[k].cx, sLook[s].quad[k].cy, sLook[s].quad[k].z}, cw, cm;
                Mtx f, t;
                PSMTXMultVec(w, &c0, &cw);
                PSMTXMultVec(m, &c0, &cm);
                PSMTXCopy(m, f);
                if (sLook[s].quad[k].mode == 2) {
                    int ax;
                    float sc[3];
                    for (ax = 0; ax < 3; ax++) {
                        const float lw = sqrtf(w[0][ax] * w[0][ax] + w[1][ax] * w[1][ax] + w[2][ax] * w[2][ax]);
                        const float lm = sqrtf(m[0][ax] * m[0][ax] + m[1][ax] * m[1][ax] + m[2][ax] * m[2][ax]);
                        sc[ax] = lm > 0.0f ? lw / lm : 1.0f;
                    }
                    PSMTXTrans(t, c0.x, c0.y, c0.z);
                    PSMTXConcat(f, t, f);
                    PSMTXScale(t, sc[0], sc[1], sc[2]);
                    PSMTXConcat(f, t, f);
                    PSMTXTrans(t, -c0.x, -c0.y, -c0.z);
                    PSMTXConcat(f, t, f);
                }
                f[0][3] += cw.x - cm.x;
                f[1][3] += cw.y - cm.y;
                f[2][3] += cw.z - cm.z;
                PSMTXCopy(f, w);
            }
            PSMTXConcat(cam->view, w, mv);
        } else {
            PSMTXConcat(cam->view, m, mv);
        }
        GXLoadPosMtxImm(mv, GX_PNMTX0);
        PSMTXInvXpose(mv, nrm);
        GXLoadNrmMtxImm(nrm, GX_PNMTX0);
        GXLoadTexObj(&sLook[s].quad[k].tex, GX_TEXMAP0);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        for (n = 0; n < 4; n++) {
            GXPosition3f32(sLook[s].quad[k].x[n], sLook[s].quad[k].y[n], sLook[s].quad[k].z);
            GXNormal3f32(0.0f, 0.0f, 1.0f);
            GXTexCoord2f32(su[n], sv[n]);
        }
        GXEnd();
    }
}

/* animdrv.c's shape draw (TARGET_PC), after the shape's own textures are loaded: shape shapeId of pose drawn in the
 * PM64 sprite's pixels this frame? Then its texture map 0 and texture coordinate 0 are that (1). Only a shape with
 * one texture (map 0, coordinate 0). */
int portPm64ShapeTex(const void* pose, int shapeId, int texCount, const void* boxTex) {
    const PortBoxTex* t = (const PortBoxTex*)boxTex;
    int s, k;
    if (pose == NULL || texCount != 1) {
        return 0;
    }
    for (s = 0; s < STANDS; s++) {
        if ((sLook[s].n > 0 || sLook[s].recolor) && sLook[s].pose == (const AnimationPose*)pose &&
            sStand[s].pose1 != 0) {
            const void* data;
            for (k = 0; k < sLook[s].n; k++) {
                if (sLook[s].part[k].shape == shapeId) {
                    GXLoadTexObj(&sLook[s].part[k].tex, GX_TEXMAP0);
                    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
                    GXLoadTexMtxImm(sLook[s].part[k].mtx, GX_TEXMTX0, GX_MTX2x4);
                    return 1;
                }
            }
            /* the body recoloured as the sprite's palette changes (burnt): its own texture, its own mapping */
            if (sLook[s].recolor && t != NULL && t->hasTex && (data = recolored(s, sStand[s].p, t, shapeId)) != NULL) {
                GXTexObj obj;
                GXInitTexObj(&obj, (void*)data, (u16)t->width, (u16)t->height, GX_TF_RGBA8, (GXTexWrapMode)t->wrapS,
                             (GXTexWrapMode)t->wrapT, GX_FALSE);
                GXInitTexObjLOD(&obj, t->linear ? GX_LINEAR : GX_NEAR, t->linear ? GX_LINEAR : GX_NEAR, 0.0f, 0.0f,
                                0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
                GXLoadTexObj(&obj, GX_TEXMAP0);
                return 1;
            }
        }
    }
    return 0;
}

/* frame >= 0: the model's clock set to it (whole sprite frames); else period as portPm64ModelDraw's */
static int modelDraw(int poseId, int p, const char* anim, float (*mtx)[4], int xluMode, float rotY, float scale,
                     float period, float frame, float alpha, int flags, const Pm64Anim* sprite) {
    AnimationPose* src;
    AnimationPose* dst;
    AnimationPoseData* dd;
    unsigned t;
    int s, pose;
    if (poseId < 0 || portPm64PartnerModel(p) == NULL) {
        return 0;
    }
    src = animPoseGetAnimPosePtr(poseId);
    for (s = 0; s < STANDS && sStand[s].source1 != poseId + 1; s++) {
    }
    if (s < STANDS && (!standValid(s) || sStand[s].p != p)) {
        standFree(s);
        s = STANDS;
    }
    if (s == STANDS) {
        /* loaded at the next frame's start (portPm64ModelsFrame); this frame the sprite */
        for (s = 0; s < sWants && sWant[s].source1 != poseId + 1; s++) {
        }
        if (s == sWants && sWants < WANTS) {
            sWant[sWants].source1 = poseId + 1;
            sWant[sWants].p = p;
            sWants++;
        }
        return 0;
    }
    pose = sStand[s].pose1 - 1;
    dst = animPoseGetAnimPosePtr(pose);
    dd = animPoseGetAnimBaseDataPtr(pose);
    /* the animation, once a frame (a name the model hasn't got: its stand) */
    t = gp != NULL ? gp->retraceCount + 1 : 1;
    if (sStand[s].stepped != t) {
        sStand[s].stepped = t;
        if (anim == NULL || portCosmeticFindAnim(dd, anim) == NULL) {
            anim = "S_1";
        }
        if (strcmp(sStand[s].anim, anim) != 0) {
            animPoseSetAnim(pose, anim, TRUE);
            snprintf(sStand[s].anim, sizeof(sStand[s].anim), "%s", anim);
        }
        /* one loop of the model's in one of the PM64 sprite's (period frames: its own timing), at the host's rate;
           or (the poses) the sprite row's own frame: its clock set so the model shows that frame (a millisecond
           over it: the clock is whole milliseconds, and a frame's step key must be reached, not missed) */
        dst->localTimeRate = src->localTimeRate;
        if (frame >= 0.0f) {
            const OSTime ms = (OSTime)ceilf(frame * (1000.0f / 60.0f)) + (frame > 0.0f ? 1 : 0);
            dst->localTimeRate = 1.0f;
            dst->localTime = animTimeGetTime(dst->type) - ms;
        } else if (period > 0.0f && dst->animId >= 0 && dst->animId < dd->animCount && dd->anims[dst->animId].data != NULL &&
            dd->anims[dst->animId].data->loopInfoCount > 0) {
            const float len = dd->anims[dst->animId].data->loopInfo[2];
            if (len > 0.0f) {
                dst->localTimeRate = src->localTimeRate * len / period;
            }
        }
        animPoseMain(pose);
        lookStep(s, p, pose, anim, frame, sprite);
    }
    /* the host's material for this draw, and its paper turn (stepped by this draw, then given back: the game
       waits on the host's, animPoseGetPeraEnd) */
    dst->materialFlag = src->materialFlag;
    dst->materialLightFlag = src->materialLightFlag;
    dst->materialColor = src->materialColor;
    dst->materialColor2 = src->materialColor2;
    if (alpha < 1.0f) {
        /* see-through (Bow's Outta Sight): the material's own colour, its alpha lowered */
        if (!(dst->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR)) {
            dst->materialColor = (GXColor){0xFF, 0xFF, 0xFF, 0xFF};
        }
        dst->materialFlag |= ANIM_MATERIAL_FLAG_EVT_COLOR;
        dst->materialColor.a = (u8)((float)dst->materialColor.a * (alpha < 0.0f ? 0.0f : alpha));
    }
    if (sLook[s].recolor && (dst->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR) && dst->materialColor.a == 0xFF) {
        dst->materialFlag &= ~ANIM_MATERIAL_FLAG_EVT_COLOR; /* (recoloured as PM64 draws it: no host blackening) */
    }
    if (sLook[s].tint >= 0 && !(src->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR)) {
        /* (the row's tint: PM64's dark burnt drawing; a host's own colour, its fire blackening, wins) */
        const u8 a = (dst->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR) ? dst->materialColor.a : 0xFF;
        dst->materialColor = (GXColor){(u8)(sLook[s].tint >> 16), (u8)(sLook[s].tint >> 8), (u8)sLook[s].tint, a};
        dst->materialFlag |= ANIM_MATERIAL_FLAG_EVT_COLOR;
    }
    dst->gxCallback = src->gxCallback;
    dst->disableDraw = src->disableDraw;
    if (flags & PM64_MODEL_FIXED) {
        /* (unturned: facing -x, as the PM64 sprites are drawn; the host's paper turn left alone) */
        dst->flags &= ~(ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN);
        dst->peraRotY = 0.0f;
        dst->rotationY = 0.0f;
        dst->peraTargetRotY = 0.0f;
        dst->scaleZFlip = 1.0f;
        animPoseDrawMtx(pose, mtx, xluMode, 0.0f, scale);
        if (xluMode == 1 && !portPaperShadowPass()) {
            Mtx q, t;
            PSMTXScale(t, scale, scale, scale);
            PSMTXConcat(mtx, t, q);
            quadsDraw(s, q, dst);
        }
        return 1;
    }
    dst->flags = (dst->flags & ~(ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN)) |
                 (src->flags & (ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN));
    dst->peraRotY = src->peraRotY;
    dst->rotationY = src->rotationY;
    dst->peraTargetRotY = src->peraTargetRotY;
    dst->scaleZFlip = src->scaleZFlip;
    animPoseDrawMtx(pose, mtx, xluMode, rotY, scale);
    if (xluMode == 1 && !portPaperShadowPass() && sLook[s].nq > 0) {
        /* (the model's own turn: its paper turn's angle, or rotY unturned by paper; mirrored as it is) */
        Mtx q, t;
        PSMTXRotRad(t, 'y', 0.017453292f * ((dst->flags & ANIM_POSE_FLAG_PERA) ? dst->rotationY : rotY));
        PSMTXConcat(mtx, t, q);
        if (dst->scaleZFlip < 0.0f) {
            PSMTXScale(t, 1.0f, 1.0f, -1.0f);
            PSMTXConcat(q, t, q);
        }
        PSMTXScale(t, scale, scale, scale);
        PSMTXConcat(q, t, q);
        quadsDraw(s, q, dst);
    }
    src->rotationY = dst->rotationY;
    src->peraRotY = dst->peraRotY;
    src->peraTargetRotY = dst->peraTargetRotY;
    src->scaleZFlip = dst->scaleZFlip;
    return 1;
}

int portPm64ModelDraw(int poseId, int p, const char* anim, float (*mtx)[4], int xluMode, float rotY, float scale,
                      float period, float alpha) {
    return modelDraw(poseId, p, anim, mtx, xluMode, rotY, scale, period, -1.0f, alpha, 0, NULL);
}

int portPm64ModelDrawAt(int poseId, int p, const char* anim, float (*mtx)[4], int xluMode, float rotY, float scale,
                        float period, float frame, float alpha, int flags, const Pm64Anim* sprite) {
    return modelDraw(poseId, p, anim, mtx, xluMode, rotY, scale, period, frame, alpha, flags, sprite);
}

/* --- the per-part poses */

int portPm64PosesOn(void) {
    return modelsLook();
}

static const Pm64PoseRow* poseRow(int p, int battle, int row) {
    int i;
    if (p <= 0 || p >= PM64_PARTNER_COUNT || !sPosed[p]) {
        return NULL;
    }
    for (i = 0; i < kModels[p].poses->nRows; i++) {
        const Pm64PoseRow* r = &kModels[p].poses->rows[i];
        if (r->battle == (battle ? 1 : 0) && r->row == row) {
            return r;
        }
    }
    return NULL;
}

int portPm64PoseRowAt(int p, int i, int* battle, int* row) {
    if (p <= 0 || p >= PM64_PARTNER_COUNT || !sPosed[p] || i < 0 || i >= kModels[p].poses->nRows) {
        return 0;
    }
    *battle = kModels[p].poses->rows[i].battle;
    *row = kModels[p].poses->rows[i].row;
    return 1;
}

const char* portPm64PoseAnim(int p, int battle, int row, int* frames, int* loop, int* sprite) {
    const Pm64PoseRow* r = poseRow(p, battle, row);
    if (r == NULL) {
        return NULL;
    }
    if (frames != NULL) {
        *frames = r->frames;
    }
    if (loop != NULL) {
        *loop = r->loop;
    }
    if (sprite != NULL) {
        *sprite = r->sprite;
    }
    return r->anim;
}

void portPm64PoseEvents(int p, int battle, int row, float from, float to) {
    const Pm64PoseRow* r = poseRow(p, battle, row);
    const char* log = getenv("TTYD_PM64_LOG");
    int i;
    if (r == NULL || log == NULL || atoi(log) <= 0) {
        return;
    }
    for (i = 0; i < r->nEvents; i++) {
        const Pm64PoseEvent* e = &kModels[p].poses->events[r->firstEvent + i];
        if ((float)e->f > from && (float)e->f <= to) {
            LOG("[pm64] pose event: %s %s at frame %d: %s", kModels[p].name, r->anim, e->f, e->what);
        }
    }
}

void portPm64ModelRelease(int poseId) {
    int s;
    for (s = 0; s < STANDS; s++) {
        if (poseId >= 0 && sStand[s].source1 == poseId + 1) {
            standFree(s);
        }
    }
}
