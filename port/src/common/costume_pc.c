/* Mario's costumes (port/include/port/costume.h, docs/COSTUMES.md): paper
 * versions of enemies' outfits from the enemies' own models on the player's
 * disc, drawn on Mario after him; and the poses of our own the costumes and
 * stickers use. The pose table is in the save-state section (CMakeLists.txt),
 * so a save state keeps the pose ids it points at; each use still checks the
 * pose is the one this file loaded. */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "port/costume.h"
#include "port/lua_mods.h"
#include "port/luigi.h"
#include "port/replay.h"
#include "port/settings.h"
#include "battle/battle_unit.h"
#include "driver/animdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mariost.h"

/* ------------------------------------------------------- our own poses */

#define POSES 12
#define POSE_MAGIC 0x53434350u /* "PCCS": in the pose's unused bytes, ours */
#define POSE_IDLE 600          /* frames unused before one is let go */

static struct {
    char model[24];
    char anim[24];
    int pose1;          /* + 1: 0 none */
    unsigned lastUsed;  /* gp->retraceCount */
    unsigned lastMain;
} sPose[POSES];

static unsigned now(void) {
    return gp != NULL ? gp->retraceCount : 0;
}

static int ours(int s) {
    AnimationPose* p;
    unsigned magic;
    if (sPose[s].pose1 == 0) {
        return 0;
    }
    p = animPoseGetAnimPosePtr(sPose[s].pose1 - 1);
    memcpy(&magic, p->pad_16C, sizeof(magic));
    return (p->flags & ANIM_POSE_FLAG_USED) && p->typeFlags == 0 && p->type == 2 && magic == POSE_MAGIC + (unsigned)s &&
           !strcmp(animPoseGetAnimBaseDataPtr(sPose[s].pose1 - 1)->fileName, sPose[s].model);
}

static void letGo(int s) {
    if (ours(s)) {
        AnimationPose* p = animPoseGetAnimPosePtr(sPose[s].pose1 - 1);
        memset(p->pad_16C, 0, sizeof(p->pad_16C));
        animPoseRelease(sPose[s].pose1 - 1);
    }
    sPose[s].pose1 = 0;
    sPose[s].model[0] = '\0';
}

/* room in the pose pool for the game's own (it doesn't check for a full one) */
int portCosmeticPoolRoom(void) {
    AnimationWork* w = animGetPtr();
    int i, free = 0;
    int files = 0, textures = 0;
    for (i = 0; i < w->totalPoses; i++) {
        free += !(w->poses[i].flags & ANIM_POSE_FLAG_USED);
    }
    /* and in its 64 model files and 64 texture files: animPoseEntry takes a
     * slot of each for a model not loaded yet, and crashes on a full table */
    for (i = 0; i < w->totalFiles; i++) {
        files += !w->files[i].active;
    }
    for (i = 0; i < w->totalTextures; i++) {
        textures += !w->textures[i].active;
    }
    return free >= 24 && files >= 16 && textures >= 16;
}

const char* portCosmeticFindAnim(AnimationPoseData* d, const char* name) {
    int i;
    for (i = 0; i < d->animCount; i++) {
        if (!strcmp(d->anims[i].name, name)) {
            return d->anims[i].name;
        }
    }
    return NULL;
}

/* the ones asked for and not loaded yet: loaded at the frame's start
 * (portLooksFrame), never in the middle of drawing */
#define WANTS 4
static struct {
    char model[24];
    char anim[24];
} sWant[WANTS];
static int sWants;

/* A draw: the pose if it's loaded, else -1; nothing changes (no load, no use noted, no
 * animation step), so a game that skips a draw (online, a peer's) keeps the same pose pool. */
int portCosmeticPose(const char* model, const char* anim) {
    int s;
    for (s = 0; s < POSES; s++) {
        if (sPose[s].pose1 != 0 && ours(s) && !strcmp(sPose[s].model, model) && !strcmp(sPose[s].anim, anim)) {
            return sPose[s].pose1 - 1;
        }
    }
    return -1;
}

/* The game's frame (portLooksFrame's, every game of an online session alike): this pose is
 * wanted now. Its use noted and its animation stepped once a frame, or it's loaded at this
 * frame's end of the asking (never from a draw). */
int portCosmeticPoseWant(const char* model, const char* anim) {
    const unsigned t = now();
    int s;

    for (s = 0; s < POSES; s++) {
        if (sPose[s].pose1 != 0 && !ours(s)) {
            sPose[s].pose1 = 0; /* (gone: a save state from before it, ...) */
        }
        if (sPose[s].pose1 != 0 && !strcmp(sPose[s].model, model) && !strcmp(sPose[s].anim, anim)) {
            sPose[s].lastUsed = t;
            if (sPose[s].lastMain != t) {
                sPose[s].lastMain = t;
                animPoseMain(sPose[s].pose1 - 1);
            }
            return sPose[s].pose1 - 1;
        }
    }
    if (strlen(model) >= sizeof(sWant[0].model) || strlen(anim) >= sizeof(sWant[0].anim)) {
        return -1;
    }
    for (s = 0; s < sWants; s++) {
        if (!strcmp(sWant[s].model, model) && !strcmp(sWant[s].anim, anim)) {
            return -1;
        }
    }
    if (sWants < WANTS) {
        snprintf(sWant[sWants].model, sizeof(sWant[0].model), "%s", model);
        snprintf(sWant[sWants].anim, sizeof(sWant[0].anim), "%s", anim);
        sWants++;
    }
    return -1;
}

static void loadPose(const char* model, const char* anim) {
    const unsigned t = now();
    int s, pick = -1;
    AnimationPose* p;
    unsigned magic;

    /* a free one, or one long unused, else the least recently used */
    for (s = 0; s < POSES; s++) {
        if (sPose[s].pose1 != 0 && t - sPose[s].lastUsed > POSE_IDLE) {
            letGo(s);
        }
        if (sPose[s].pose1 == 0 && pick < 0) {
            pick = s;
        }
    }
    if (pick < 0) {
        pick = 0;
        for (s = 1; s < POSES; s++) {
            if (t - sPose[s].lastUsed > t - sPose[pick].lastUsed) { /* (the oldest, wrapping) */
                pick = s;
            }
        }
        if (t - sPose[pick].lastUsed < 3) {
            return; /* (every one in use now: the rest wait, not loaded over and over) */
        }
        letGo(pick);
    }
    if (!portCosmeticPoolRoom()) {
        return;
    }
    sPose[pick].pose1 = animPoseEntry(model, 2) + 1;
    if (sPose[pick].pose1 <= 0) {
        sPose[pick].pose1 = 0;
        return;
    }
    p = animPoseGetAnimPosePtr(sPose[pick].pose1 - 1);
    magic = POSE_MAGIC + (unsigned)pick;
    memcpy(p->pad_16C, &magic, sizeof(magic));
    snprintf(sPose[pick].model, sizeof(sPose[pick].model), "%s", model);
    snprintf(sPose[pick].anim, sizeof(sPose[pick].anim), "%s", anim);
    animPoseSetAnim(sPose[pick].pose1 - 1, anim, TRUE);
    animPoseMain(sPose[pick].pose1 - 1);
    sPose[pick].lastUsed = sPose[pick].lastMain = t;
}

static void costumeWant(void);

void portLooksFrame(void) {
    int s, i;
    /* (in a game only: not the title, a map's or battle's loading) */
    const SequenceType seq = seqGetSeq();
    portSkinsFrame();
    /* what's wanted, from the game's state (never from what was drawn: a game that skips a draw
     * would load and let go of poses on other frames than the others of a session); with the
     * menu's since the last frame (the Wardrobe's stage) */
    costumeWant();
    portStickersFrame(); /* (the room's enemy stickers) */
    if (gp == NULL || (seq != SEQ_GAME && seq != SEQ_BATTLE)) {
        sWants = 0;
    }
    for (i = 0; i < sWants; i++) {
        for (s = 0; s < POSES; s++) {
            if (sPose[s].pose1 != 0 && ours(s) && !strcmp(sPose[s].model, sWant[i].model) &&
                !strcmp(sPose[s].anim, sWant[i].anim)) {
                break;
            }
        }
        if (s == POSES) {
            loadPose(sWant[i].model, sWant[i].anim);
        }
    }
    sWants = 0;
}

/* ------------------------------------------------------------ costumes */

enum { AT_HAT, AT_BODY, AT_FIGURE };

/* Each costume: its enemy (the Tattle Log's unit, or a second one of the
 * same look), the enemy's model and the animation it stands in, the model's
 * parts that make the outfit (its groups' names: the tools' export lists
 * them, docs/CHARACTERS.md), and how it sits on him: on his hat, his body
 * or his whole figure; its height as a part of his; where its middle goes,
 * in sizes of what it sits on from that one's middle (x: toward his back,
 * the models face -x; y: up); in front of him (z > 0) or behind, in his
 * model's units; and how see-through it is. */
static const struct Costume {
    const char* name;
    int unit, unit2;
    const char* model;
    const char* anim;
    const char* parts; /* comma-separated; NULL: all the model shows */
    int on;
    float size, x, y, z;
    unsigned char alpha;
} kCostumes[] = {
    {"Koopa shell", UNIT_KOOPA_TROOPA, UNIT_KP_KOOPA, "c_nokoteki", "NKT_Z_1", "koura", AT_BODY, 0.55f, 0.6f, 0.05f,
     -1.5f, 0xFF},
    {"Paratroopa wings", UNIT_PARATROOPA, UNIT_KP_PARATROOPA, "c_nokoteki", "PTP_Z_1", "hidaeihane,migihane", AT_BODY,
     0.5f, 0.3f, 0.25f, -1.6f, 0xFF},
    {"Spiky Goomba helmet", UNIT_SPIKY_GOOMBA, UNIT_HYPER_SPIKY_GOOMBA, "c_kuribo", "TGK_Z_2", "toge", AT_HAT, 0.42f,
     0.0f, 0.35f, 0.8f, 0xFF},
    {"Paragoomba wings", UNIT_PARAGOOMBA, UNIT_HYPER_PARAGOOMBA, "c_kuribo", "PTK_Z_2", "migihane,hidarihane", AT_BODY,
     0.45f, 0.3f, 0.2f, -1.6f, 0xFF},
    /* (the Boo's quiet face, K_1: no open mouth lying across him; its eyes
     * and mouth at his head, a sheet thrown over him, more sheet than wash) */
    {"Boo sheet", UNIT_BOO, 0, "c_teresa", "K_1", NULL, AT_FIGURE, 1.05f, 0.0f, 0.22f, 1.5f, 0xC8},
    {"X-Naut hood", UNIT_X_NAUT, 0, "c_zako_n", "Z_1",
     "pPlane20,pPlane16,pPlane10,pPlane9,pPlane14,pPlane18,pPlane19,pPlane15", AT_HAT, 0.5f, 0.0f, 0.0f, 0.9f, 0xFF},
    {"Bob-omb key", UNIT_BOB_OMB, UNIT_BULKY_BOB_OMB, "c_bomhey", "BOM_Z_1", "pPlane2", AT_BODY, 0.3f, 0.6f, 0.1f,
     -1.2f, 0xFF},
    {"Buzzy Beetle shell", UNIT_BUZZY_BEETLE, 0, "c_met", "MET_Z_1", "pPlane5", AT_BODY, 0.45f, 0.4f, 0.05f, -1.5f,
     0xFF},
    {"Spiny shell", UNIT_SPINY, 0, "c_togezo", "TGZ_Z_1", "pPlane4", AT_BODY, 0.5f, 0.4f, 0.1f, -1.5f, 0xFF},
};
#define COSTUMES ((int)(sizeof(kCostumes) / sizeof(kCostumes[0])))

/* Each costume on each Play as pick (docs/PLAY_AS.md): fitted to their own
 * hat (or head) and body as it is to Mario's, by their models' parts
 * (kAnchors), then nudged: its size times size, moved by x and y (in sizes of
 * what it sits on, as the costume's own); or not worn at all (hidden), where
 * it can't sit sensibly on them. Mario's and Luigi's are as the table above;
 * Peach's sit higher (her skirt is most of her body's box); Bowser wears no
 * other shell (his own spiked one is where it would go, and hides it). */
static const struct Fit {
    unsigned char hidden;
    float size, x, y;
} kFits[COSTUMES][PORT_PLAY_AS_COUNT] = {
    /* Mario, Luigi, Peach, Bowser */
    {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1.0f, 0.0f, 0.25f}, {1, 1.0f, 0.0f, 0.0f}}, /* Koopa shell (Bowser: his own) */
    {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1.0f, 0.0f, 0.0f}, {0, 1.0f, 0.0f, 0.0f}}, /* Paratroopa wings */
    {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1.0f, 0.0f, 0.15f}, {0, 1.0f, 0.0f, 0.0f}}, /* Spiky Goomba helmet */
    {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0.85f, 0.0f, 0.0f}, {0, 1.0f, 0.0f, 0.0f}}, /* Paragoomba wings */
    {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1.0f, 0.0f, 0.0f}, {0, 1.0f, 0.0f, 0.0f}}, /* Boo sheet */
    {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0.85f, 0.0f, 0.3f}, {0, 1.0f, 0.0f, 0.0f}}, /* X-Naut hood */
    {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1.0f, 0.0f, 0.2f}, {0, 1.0f, 0.0f, 0.0f}}, /* Bob-omb key */
    {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1.0f, 0.0f, 0.25f}, {1, 1.0f, 0.0f, 0.0f}}, /* Buzzy Beetle shell (Bowser: his own) */
    {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1.0f, 0.0f, 0.25f}, {1, 1.0f, 0.0f, 0.0f}}, /* Spiny shell (Bowser: his own) */
};

static const struct Fit* fitFor(int i, int pick) {
    return &kFits[i][pick >= 0 && pick < PORT_PLAY_AS_COUNT ? pick : 0];
}

static const struct Fit* fitNow(int i) {
    return fitFor(i, portPlayAs());
}

int portCostumeFits(int i) {
    return i >= 0 && i < COSTUMES && !fitNow(i)->hidden;
}

static int* costumeSetting(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("costume", 0, 0, COSTUMES);
    }
    return sOn;
}

int portCostumeCount(void) {
    return COSTUMES;
}

const char* portCostumeName(int i) {
    return i >= 0 && i < COSTUMES ? kCostumes[i].name : "?";
}

int portCostumeUnlocked(int i) {
    if (i < 0 || i >= COSTUMES || gp == NULL) {
        return 0;
    }
    if (getenv("TTYD_COSTUMES_ALL") != NULL) {
        return 1; /* (tests, screenshots) */
    }
    return swGet(GSWF_TATTLE_BASE + kCostumes[i].unit) ||
           (kCostumes[i].unit2 != 0 && swGet(GSWF_TATTLE_BASE + kCostumes[i].unit2));
}

int portCostumeCurrent(void) {
    int* on = costumeSetting();
    /* a Lua mod's choice wins while it runs (looks.set_costume); not online or in replays,
     * where a HUD mod runs per machine and the costume's pose is in the game's pool */
    const int lua = portReplayActive() ? -1 : portLuaCostumeChoice();
    if (lua >= 0) {
        return lua <= COSTUMES ? lua : 0;
    }
    return on != NULL ? *on : 0;
}

/* another player's costume (online: worn by their ghost, statesync_ghost_pc.c): its pose wanted here
   this frame, as this player's own is (the game's frame, never a draw) */
void portCostumeWantOther(int costume) {
    if (costume >= 1 && costume <= COSTUMES) {
        portCosmeticPoseWant(kCostumes[costume - 1].model, kCostumes[costume - 1].anim);
    }
}

/* the frame's (portLooksFrame): the costume worn wants its pose, whether or not Mario is drawn */
static void costumeWant(void) {
    const int i = portCostumeCurrent() - 1;
    if (i >= 0 && i < COSTUMES && portCostumeUnlocked(i)) {
        portCosmeticPoseWant(kCostumes[i].model, kCostumes[i].anim);
    }
}

void portCostumeStep(int dir) {
    int* on = costumeSetting();
    int i, c;
    if (on == NULL) {
        return;
    }
    c = *on;
    for (i = 0; i <= COSTUMES; i++) {
        c = ((c + dir) % (COSTUMES + 1) + COSTUMES + 1) % (COSTUMES + 1);
        if (c == 0 || (portCostumeUnlocked(c - 1) && portCostumeFits(c - 1))) {
            break;
        }
    }
    *on = c;
    portSettingsSave();
}

/* --- Mario's poses this frame */

#define WHERE 4 /* PORT_LUIGI_FIELD .. _LOG (not a versus battle's Luigi) */
static int sMario1[WHERE];
static unsigned sMarioAt[WHERE];

void portCostumeNoteMario(int where, int poseId) {
    if (where >= 0 && where < WHERE) {
        sMario1[where] = poseId + 1;
        sMarioAt[where] = now();
    }
}

static int isMario(int poseId) {
    int w;
    for (w = 0; w < WHERE; w++) {
        if (sMario1[w] == poseId + 1 && now() - sMarioAt[w] < 30) {
            return 1;
        }
    }
    return 0;
}

int portCostumeMarioPose(int poseId) {
    return isMario(poseId);
}

/* --- fitting */

#define MAX_GROUPS 160
static Mtx sMarioMtx[MAX_GROUPS];
static Mtx sCostumeMtx[MAX_GROUPS];
static unsigned char sMask[MAX_GROUPS];

typedef struct Box {
    float x0, y0, z0, x1, y1, z1;
    int any;
} Box;

static void boxAdd(Box* b, const Vec* v) {
    if (!b->any) {
        b->x0 = b->x1 = v->x;
        b->y0 = b->y1 = v->y;
        b->z0 = b->z1 = v->z;
        b->any = 1;
        return;
    }
    b->x0 = fminf(b->x0, v->x);
    b->x1 = fmaxf(b->x1, v->x);
    b->y0 = fminf(b->y0, v->y);
    b->y1 = fmaxf(b->y1, v->y);
    b->z0 = fminf(b->z0, v->z);
    b->z1 = fmaxf(b->z1, v->z);
}

static int drawnGroup(Mtx m) {
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 4; j++) {
            if (m[i][j] != 0.0f) {
                return 1;
            }
        }
    }
    return 0;
}

/* group g's shape's vertices as the pose has them now, by its matrix */
static void groupBox(int poseId, Mtx* mtx, int g, Box* b) {
    AnimationPose* p = animPoseGetAnimPosePtr(poseId);
    AnimationPoseData* d = animPoseGetAnimBaseDataPtr(poseId);
    const AnimPoseShape* sh;
    int v;
    if (g < 0 || g >= d->groupCount || d->groups[g].shapeId < 0 || d->groups[g].shapeId >= d->shapeCount ||
        !drawnGroup(mtx[g]) || p->vtxArrayPos == NULL) {
        return;
    }
    sh = &d->shapes[d->groups[g].shapeId];
    for (v = 0; v < sh->vtxPos.count; v++) {
        Vec w;
        PSMTXMultVec(mtx[g], &p->vtxArrayPos[sh->vtxPos.first + v], &w);
        boxAdd(b, &w);
    }
}

/* name is one of the comma-separated list's */
static int listed(const char* list, const char* name);

/* the groups a comma-separated list names, each one's box added */
static void groupsBox(int poseId, Mtx* mtx, AnimationPoseData* d, const char* names, Box* b) {
    int g;
    for (g = 0; g < d->groupCount; g++) {
        if (listed(names, d->groups[g].name)) {
            groupBox(poseId, mtx, g, b);
        }
    }
}

static int listed(const char* list, const char* name) {
    const size_t n = strlen(name);
    const char* p = list;
    while (p != NULL && *p != '\0') {
        const char* end = strchr(p, ',');
        const size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        if (len == n && !strncmp(p, name, n)) {
            return 1;
        }
        p = end != NULL ? end + 1 : NULL;
    }
    return 0;
}

/* his hat's and body's groups, by his model (Luigi's planes by their
 * drawings; Peach's head is her face and hair, her body her bodice and
 * skirt, from the front or behind; Bowser's his head and mane, and body);
 * comma-separated, the box around them all; a model not here, or without
 * them, is cut by height. (The first prefix that fits: c_peach_r before
 * c_peach; ours, c_peach_pc and the others, keep the disc's names.) */
static const struct {
    const char* prefix;
    const char* hat;
    const char* body;
} kAnchors[] = {
    {"a_mario", "Hat", "Body"},
    {"b_mario", "Hat", "Body"},
    {"e_mario", "Hat", "Body"},
    {"c_luigi", "pPlane4", "pPlane17"},
    {"c_peach_r", "pPlane2,pPlane3,pPlane14", "pPlane9,pPlane24"},
    {"c_peach", "pPlane7,pPlane3,pPlane5", "pPlane26,pPlane28"},
    {"c_koopa", "head,hear", "body"},
};

static int anchorBox(int poseId, int on, Box* out) {
    AnimationPoseData* d = animPoseGetAnimBaseDataPtr(poseId);
    Box hat = {0}, body = {0}, all = {0};
    size_t i;
    int g, n;
    Mtx id;
    PSMTXIdentity(id);
    n = portAnimPoseGroupMatrices(poseId, id, sMarioMtx, MAX_GROUPS);
    if (n <= 0) {
        return 0;
    }
    for (i = 0; i < sizeof(kAnchors) / sizeof(kAnchors[0]); i++) {
        if (!strncmp(d->fileName, kAnchors[i].prefix, strlen(kAnchors[i].prefix))) {
            groupsBox(poseId, sMarioMtx, d, kAnchors[i].hat, &hat);
            groupsBox(poseId, sMarioMtx, d, kAnchors[i].body, &body);
            break;
        }
    }
    if (!hat.any || !body.any) {
        /* by height: the top part his hat, the middle his body */
        float h;
        for (g = 0; g < n; g++) {
            groupBox(poseId, sMarioMtx, g, &all);
        }
        if (!all.any) {
            return 0;
        }
        h = all.y1 - all.y0;
        hat = all;
        hat.y0 = all.y0 + 0.62f * h;
        body = all;
        body.y0 = all.y0 + 0.15f * h;
        body.y1 = all.y0 + 0.58f * h;
    }
    if (on == AT_HAT) {
        *out = hat;
    } else if (on == AT_BODY) {
        *out = body;
    } else {
        *out = hat;
        boxAdd(out, &(Vec){body.x0, body.y0, body.z0});
        boxAdd(out, &(Vec){body.x1, body.y1, body.z1});
    }
    return 1;
}

static void costumeDraw(int i, int pick, int drawn, int source, Mtx mtx, int xluMode, float rotY, float scale);

void portCostumeDraw(int drawn, int source, Mtx mtx, int xluMode, float rotY, float scale) {
    const int i = portCostumeCurrent() - 1;
    if (i < 0 || i >= COSTUMES || drawn < 0 || source < 0 || !isMario(source) || !portCostumeUnlocked(i) ||
        !portCostumeFits(i)) {
        return; /* (one that doesn't fit the Play as pick: not worn, the Wardrobe says so) */
    }
    costumeDraw(i, portPlayAs(), drawn, source, mtx, xluMode, rotY, scale);
}

void portCostumeDrawOther(int costume, int pick, int drawn, Mtx mtx, int xluMode, float rotY, float scale) {
    const int i = costume - 1;
    if (i < 0 || i >= COSTUMES || drawn < 0 || fitFor(i, pick)->hidden) {
        return; /* (none, one this build doesn't have, or one their pick doesn't wear) */
    }
    costumeDraw(i, pick, drawn, drawn, mtx, xluMode, rotY, scale);
}

static void costumeDraw(int i, int pick, int drawn, int source, Mtx mtx, int xluMode, float rotY, float scale) {
    const struct Costume* c;
    const struct Fit* fit;
    AnimationPose* dp;
    AnimationPose* cp;
    AnimationPoseData* dd;
    AnimationPoseData* cd;
    Box at, part = {0};
    Mtx m, t, id;
    float height, k, x, z;
    int pose, g, n, back;

    if (animPoseGetAnimPosePtr(source)->paperId != -1) {
        return; /* (a paper form: a plane, a boat, ... is a picture of him) */
    }
    c = &kCostumes[i];
    fit = fitFor(i, pick);
    if ((pose = portCosmeticPose(c->model, c->anim)) < 0 || !anchorBox(drawn, c->on, &at)) {
        return;
    }
    cd = animPoseGetAnimBaseDataPtr(pose);
    PSMTXIdentity(id);
    n = portAnimPoseGroupMatrices(pose, id, sCostumeMtx, MAX_GROUPS);
    if (n <= 0) {
        return;
    }
    for (g = 0; g < n; g++) {
        sMask[g] = c->parts == NULL || listed(c->parts, cd->groups[g].name);
        if (sMask[g]) {
            groupBox(pose, sCostumeMtx, g, &part);
        }
    }
    if (!part.any || part.y1 - part.y0 < 0.01f) {
        return;
    }
    /* his height (the model's own bounds: steady as he moves) */
    dd = animPoseGetAnimBaseDataPtr(drawn);
    height = dd->bboxMax.y - dd->bboxMin.y;
    if (!(height > 4.0f && height < 200.0f)) {
        height = 23.0f;
    }
    k = c->size * fit->size * height / (part.y1 - part.y0);
    /* his back to us (Mario's back-facing model a_mario_r; Luigi's R poses
     * still show his face): his back is toward us, not to the side, so what
     * sits on it goes on him in the middle, and everything is in front (a
     * helmet, a sheet over him, a shell on his back) */
    {
        /* (Peach's back-facing model c_peach_r, and ours of it, c_peach_r_pc; Bowser has none) */
        const size_t len = strlen(dd->fileName);
        back = (len > 2 && !strcmp(dd->fileName + len - 2, "_r")) ||
               (len > 5 && !strcmp(dd->fileName + len - 5, "_r_pc"));
    }
    z = back ? fabsf(c->z) : c->z;
    x = back ? 0.0f : c->x + fit->x;

    /* as _animPoseDrawMtx turns and sizes him, then onto him */
    dp = animPoseGetAnimPosePtr(drawn);
    PSMTXCopy(mtx, m);
    if (dp->flags & ANIM_POSE_FLAG_PERA) {
        if (dp->rotationY != 0.0f) {
            PSMTXRotRad(t, 'y', 0.017453292f * dp->rotationY);
            PSMTXConcat(m, t, m);
        }
        if (dp->scaleZFlip == -1.0f) {
            PSMTXScale(t, 1.0f, 1.0f, -1.0f);
            PSMTXConcat(m, t, m);
        }
    } else if (rotY != 0.0f) {
        PSMTXRotRad(t, 'y', 0.017453292f * rotY);
        PSMTXConcat(m, t, m);
    }
    if (scale != 1.0f) {
        PSMTXScale(t, scale, scale, scale);
        PSMTXConcat(m, t, m);
    }
    PSMTXTrans(t, (at.x0 + at.x1) * 0.5f + x * (at.x1 - at.x0), (at.y0 + at.y1) * 0.5f + (c->y + fit->y) * (at.y1 - at.y0),
               z);
    PSMTXConcat(m, t, m);
    PSMTXScale(t, k, k, k);
    PSMTXConcat(m, t, m);
    PSMTXTrans(t, -(part.x0 + part.x1) * 0.5f, -(part.y0 + part.y1) * 0.5f, -(part.z0 + part.z1) * 0.5f);
    PSMTXConcat(m, t, m);

    /* his material as the game set it for this draw (fades, flashes), and
     * the costume's own see-through */
    cp = animPoseGetAnimPosePtr(pose);
    cp->flags &= ~(ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN);
    cp->materialFlag = dp->materialFlag;
    cp->materialLightFlag = dp->materialLightFlag;
    cp->materialColor = dp->materialColor;
    cp->materialColor2 = dp->materialColor2;
    cp->gxCallback = dp->gxCallback;
    cp->disableDraw = dp->disableDraw;
    if (c->alpha < 0xFF) {
        if (!(cp->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR)) {
            cp->materialFlag |= ANIM_MATERIAL_FLAG_EVT_COLOR;
            cp->materialColor = (GXColor){0xFF, 0xFF, 0xFF, 0xFF};
        }
        cp->materialColor.a = (unsigned char)(cp->materialColor.a * c->alpha / 0xFF);
    }
    gPortAnimShapeMask = sMask;
    animPoseDrawMtx(pose, m, xluMode, 0.0f, 1.0f);
    gPortAnimShapeMask = NULL;
}
