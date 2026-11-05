/* Partner skins (port/include/port/costume.h, docs/COSTUMES.md): a character
 * pack from the user folder's skins/ (made into a/pc_skin_<n> at startup,
 * port/src/aurora/skins_aurora.c) drawn in a partner's place. As for Play as
 * Luigi, the partner's own pose runs as before and a stand-in of the skin
 * follows it, playing the skin's animation of the same name (or the closest:
 * the same move without the model's prefix, its first variant, standing),
 * and portLuigiDrawMtx draws it instead. The stand-ins' table is in the
 * save-state section (CMakeLists.txt); each use checks the pose is still the
 * skin's. */
#include <stdio.h>
#include <string.h>
#include "port/costume.h"
#include "port/lua_mods.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "driver/animdrv.h"
#include "mario/mariost.h"

#define SKINS 32
static struct {
    char name[48];
    char model[24];
} sSkins[SKINS];
static int sSkinCount;

void portSkinAdd(const char* name, const char* model) {
    if (sSkinCount < SKINS) {
        snprintf(sSkins[sSkinCount].name, sizeof(sSkins[0].name), "%s", name);
        snprintf(sSkins[sSkinCount].model, sizeof(sSkins[0].model), "%s", model);
        sSkinCount++;
    }
}

int portSkinCount(void) {
    return sSkinCount;
}

const char* portSkinName(int i) {
    return i >= 0 && i < sSkinCount ? sSkins[i].name : "?";
}

const char* portSkinModel(int i) {
    return i >= 0 && i < sSkinCount ? sSkins[i].model : "";
}

/* --- the choice, per partner: the skin's name's hash (0: their own), so a
 * skin added or taken out doesn't move the others' */

static int nameHash(const char* s) {
    unsigned h = 2166136261u;
    for (; *s != '\0'; s++) {
        h = (h ^ (unsigned char)*s) * 16777619u;
    }
    h &= 0x7FFFFFFF;
    return h != 0 ? (int)h : 1;
}

static int* skinSetting(int party) {
    static int* sSet[8];
    static char sKeys[8][24];
    if (party < 1 || party > 7) {
        return NULL;
    }
    if (sSet[party] == NULL) {
        snprintf(sKeys[party], sizeof(sKeys[party]), "partner_skin_%d", party);
        sSet[party] = portSettingsDeclare(sKeys[party], 0, 0, 0x7FFFFFFF);
    }
    return sSet[party];
}

/* Online and in replays, none: a skin is the user folder's own (the other
 * player may not have it), and its stand-in is a pose in the game's pool and
 * heap whose draw steps the partner's paper turn (luigi_pc.c), which the game
 * waits on. */
static int skinsOff(void) {
    return portOnlineActive() || portReplayActive();
}

int portSkinOf(int party) {
    const int* set = skinSetting(party);
    const int lua = portLuaSkinChoice(party); /* a Lua mod's choice wins while it runs (looks.set_skin) */
    int i;
    if (skinsOff()) {
        return -1; /* a mod's look choice too: a skin's pose is per machine */
    }
    if (lua != -2) {
        return lua >= 0 && lua < sSkinCount ? lua : -1;
    }
    if (set == NULL || *set == 0) {
        return -1;
    }
    for (i = 0; i < sSkinCount; i++) {
        if (nameHash(sSkins[i].name) == *set) {
            return i;
        }
    }
    return -1;
}

void portSkinStep(int party, int dir) {
    int* set = skinSetting(party);
    int k;
    if (set == NULL || sSkinCount == 0 || skinsOff()) {
        return; /* (the list is this player's own: the same input would step each to another) */
    }
    /* their own (-1), then each skin */
    k = portSkinOf(party) + 1 + dir;
    k = (k % (sSkinCount + 1) + sSkinCount + 1) % (sSkinCount + 1) - 1;
    *set = k < 0 ? 0 : nameHash(sSkins[k].name);
    portSettingsSave();
}

/* --- the partners' models */

static int partyOf(const char* file) {
    static const char* const kModel[8] = {NULL, "c_pkr", "c_pnk", "c_bomt_n", NULL, "c_windy", "c_vivian", "c_tyutyu"};
    int p;
    /* Yoshi: c_babyyoshi, c_babyyoshi2 .. 7 (his colours) */
    if (!strncmp(file, "c_babyyoshi", 11) && (file[11] == '\0' || (file[11] >= '2' && file[11] <= '7' && file[12] == '\0'))) {
        return 4;
    }
    for (p = 1; p < 8; p++) {
        if (kModel[p] != NULL && !strcmp(file, kModel[p])) {
            return p;
        }
    }
    return 0;
}

/* --- the stand-ins */

#define STANDS 8
static struct {
    int source1; /* the partner's pose + 1 (0: none) */
    int pose1;   /* the skin's + 1 */
    int skin;
    char anim[60];
    unsigned stepped;
} sStand[STANDS];

static int standValid(int s) {
    AnimationPose* p;
    AnimationPose* src;
    if (sStand[s].pose1 == 0 || sStand[s].source1 == 0 || sStand[s].skin < 0 || sStand[s].skin >= sSkinCount) {
        return 0;
    }
    p = animPoseGetAnimPosePtr(sStand[s].pose1 - 1);
    src = animPoseGetAnimPosePtr(sStand[s].source1 - 1);
    return (p->flags & ANIM_POSE_FLAG_USED) && p->typeFlags == 0 && p->type == src->type &&
           !strcmp(animPoseGetAnimBaseDataPtr(sStand[s].pose1 - 1)->fileName, sSkins[sStand[s].skin].model);
}

static void standFree(int s) {
    if (standValid(s)) {
        animPoseRelease(sStand[s].pose1 - 1);
    }
    sStand[s].pose1 = 0;
    sStand[s].source1 = 0;
    sStand[s].anim[0] = '\0';
}

void portSkinRelease(int poseId) {
    int s;
    for (s = 0; s < STANDS; s++) {
        if (poseId >= 0 && sStand[s].source1 == poseId + 1) {
            standFree(s);
        }
    }
}

static const char* findAnim(AnimationPoseData* d, const char* name) {
    int i;
    for (i = 0; i < d->animCount; i++) {
        if (!strcmp(d->anims[i].name, name)) {
            return d->anims[i].name;
        }
    }
    return NULL;
}

/* one of d's whose name is move or ends in "_" move ("W_1": W_1, KUR_W_1) */
static const char* findMove(AnimationPoseData* d, const char* move) {
    const size_t n = strlen(move);
    int i;
    const char* found = findAnim(d, move);
    if (found != NULL) {
        return found;
    }
    for (i = 0; i < d->animCount; i++) {
        const char* a = d->anims[i].name;
        const size_t len = strlen(a);
        if (len > n && a[len - n - 1] == '_' && !strcmp(a + len - n, move)) {
            return a;
        }
    }
    return NULL;
}

/* the skin's animation for the partner's want ("PKR_W_1") */
static const char* skinAnim(AnimationPoseData* d, const char* want) {
    const char* move;
    const char* found;
    char first[16];
    if ((found = findAnim(d, want)) != NULL) {
        return found;
    }
    /* without the model's prefix: PKR_W_1 -> W_1 */
    move = strchr(want, '_');
    move = move != NULL && strchr(move + 1, '_') != NULL ? move + 1 : want;
    if ((found = findMove(d, move)) != NULL) {
        return found;
    }
    /* its first variant: W_2 -> W_1 */
    if (move[0] != '\0' && move[1] == '_') {
        snprintf(first, sizeof(first), "%c_1", move[0]);
        if ((found = findMove(d, first)) != NULL) {
            return found;
        }
    }
    if ((found = findMove(d, "S_1")) != NULL) {
        return found;
    }
    return d->animCount > 0 ? d->anims[0].name : NULL;
}

static int poolRoom(void) {
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

#define WANTS 4
static int sWant1[WANTS]; /* partners' poses + 1 whose stand-ins are to be loaded */
static int sWants;

void portSkinsFrame(void) {
    int w, s;
    for (w = 0; w < sWants; w++) {
        const int poseId = sWant1[w] - 1;
        AnimationPose* src;
        int skin, party, pose;
        if (poseId < 0) {
            continue;
        }
        src = animPoseGetAnimPosePtr(poseId);
        if (!(src->flags & ANIM_POSE_FLAG_USED) || src->typeFlags != 0 ||
            (party = partyOf(animPoseGetAnimBaseDataPtr(poseId)->fileName)) == 0 || (skin = portSkinOf(party)) < 0) {
            continue;
        }
        for (s = 0; s < STANDS && sStand[s].source1 != poseId + 1; s++) {
        }
        if (s < STANDS) {
            continue; /* (asked twice) */
        }
        /* a free slot, or one whose partner's pose is gone */
        for (s = 0; s < STANDS; s++) {
            const int old = sStand[s].source1 - 1;
            if (old < 0 || !(animPoseGetAnimPosePtr(old)->flags & ANIM_POSE_FLAG_USED) ||
                partyOf(animPoseGetAnimBaseDataPtr(old)->fileName) == 0) {
                standFree(s);
                break;
            }
        }
        if (s == STANDS || !poolRoom()) {
            continue;
        }
        pose = animPoseEntry(sSkins[skin].model, src->type);
        if (pose < 0) {
            continue;
        }
        sStand[s].source1 = poseId + 1;
        sStand[s].pose1 = pose + 1;
        sStand[s].skin = skin;
        sStand[s].anim[0] = '\0';
        sStand[s].stepped = 0;
    }
    sWants = 0;
}

int portSkinStandIn(int poseId, float* scale) {
    AnimationPose* src;
    AnimationPose* dst;
    AnimationPoseData* sd;
    AnimationPoseData* dd;
    const char* anim;
    unsigned t;
    int s, skin, party, pose;
    float hs, hd;

    if (poseId < 0 || sSkinCount == 0) {
        return -1;
    }
    src = animPoseGetAnimPosePtr(poseId);
    if (src->paperId != -1 || src->typeFlags != 0) {
        return -1; /* (a paper form: a picture of the pose) */
    }
    sd = animPoseGetAnimBaseDataPtr(poseId);
    if ((party = partyOf(sd->fileName)) == 0) {
        return -1;
    }
    skin = portSkinOf(party);
    for (s = 0; s < STANDS && sStand[s].source1 != poseId + 1; s++) {
    }
    if (skin < 0) {
        if (s < STANDS) {
            standFree(s);
        }
        return -1;
    }
    if (s < STANDS && (!standValid(s) || sStand[s].skin != skin)) {
        standFree(s);
        s = STANDS;
    }
    if (s == STANDS) {
        /* loaded at the next frame's start (portSkinsFrame): never while drawing */
        for (s = 0; s < sWants && sWant1[s] != poseId + 1; s++) {
        }
        if (s == sWants && sWants < WANTS) {
            sWant1[sWants++] = poseId + 1;
        }
        return -1;
    }
    pose = sStand[s].pose1 - 1;
    dst = animPoseGetAnimPosePtr(pose);
    dd = animPoseGetAnimBaseDataPtr(pose);
    /* following the partner's animation, once a frame */
    t = gp != NULL ? gp->retraceCount + 1 : 1;
    if (sStand[s].stepped != t) {
        sStand[s].stepped = t;
        anim = skinAnim(dd, animPoseGetCurrentAnim(poseId)->name);
        if (anim != NULL && strcmp(sStand[s].anim, anim) != 0) {
            animPoseSetAnim(pose, anim, TRUE);
            snprintf(sStand[s].anim, sizeof(sStand[s].anim), "%s", anim);
        }
        dst->localTimeRate = src->localTimeRate;
        animPoseMain(pose);
    }
    /* the paper turn as the partner's */
    dst->flags = (dst->flags & ~(ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN)) |
                 (src->flags & (ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN));
    dst->peraRotY = src->peraRotY;
    dst->rotationY = src->rotationY;
    dst->peraTargetRotY = src->peraTargetRotY;
    dst->scaleZFlip = src->scaleZFlip;
    /* the partner's height */
    hs = sd->bboxMax.y - sd->bboxMin.y;
    hd = dd->bboxMax.y - dd->bboxMin.y;
    *scale = hs > 1.0f && hd > 1.0f ? hs / hd : 1.0f;
    if (*scale < 0.25f || *scale > 4.0f) {
        *scale = 1.0f;
    }
    return pose;
}
