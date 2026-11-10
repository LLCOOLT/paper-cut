/* "Sync what matters" (port/include/port/statesync.h): the other players drawn
 * in this game, as ghosts: each one's character's model (the one their game
 * draws, "a_mario", or a partner's) in their pose, where their game says they
 * are, moved smoothly (statesync_pc.c interpolates). Poses of our own in the
 * game's pose pool, made from the game's frame (never from a draw), let go
 * when the ghost leaves the room; their draw is matrix and pose only, as the
 * partner's (party.c partyDisp) and Mario's (mario.c marioDisp). Draw-only:
 * nothing of the game's changes, no random numbers are taken.
 *
 * Up to three other players, four slots each: the player, the partner, the
 * player's Play as pick (Luigi, Peach, Bowser), drawn from this game's own
 * models of it, as their game stands it in for Mario (luigi_pc.c's maps):
 * drawn instead of Mario's model while it has a version of his animation
 * (Luigi lacks a few: Mario's model shows then), and Brothers mode's Luigi
 * when he walks in their field. The player wears their costume (fitted to
 * their pick, costume_pc.c). A pose is taken only while the pool has room
 * (portCosmeticPoolRoom): with a full pool, a ghost waits. ghost_see_through
 * (this player's own setting) draws them all see-through. */
#include "driver/animdrv.h"
#include "driver/dispdrv.h"
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "port/costume.h"
#include "port/luigi.h"
#include "port/pm64.h"
#include "port/pm64_media.h"
#include "port/settings.h"
#include "port/statesync.h"
#include <dolphin/mtx.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GHOST_MAGIC 0x47485354u /* "GHST" */
/* per player: the player, the partner, the player's pick standing in, Brothers mode's Luigi, and (the
   host's, for co-op guests) the follower and the companion */
#define PER 6
#define PARTNER 1
#define STAND_IN 2
#define BROTHER 3
#define FOLLOWER 4
#define COMPANION 5
#define SEE_THROUGH_ALPHA 0x90
#define OTHERS (SS_PLAYERS - 1)
#define SLOTS (OTHERS * PER)

typedef struct Ghost {
    int pose1; /* the pose + 1, 0 none */
    char model[24];
    char anim[32];
    SsGhostPose at;
    float scale;    /* in Mario's place: the pick's size */
    unsigned shown; /* gp->retraceCount of the last draw asked for */
} Ghost;

static Ghost sGhost[SLOTS];

static int* seeThrough(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("ghost_see_through", 0, 0, 1);
    }
    return sOn;
}

int portStateSyncGhostSeeThrough(void) {
    return seeThrough() != NULL && *seeThrough() != 0;
}

void portStateSyncGhostSetSeeThrough(int on) {
    if (seeThrough() != NULL) {
        *seeThrough() = on != 0;
        portSettingsSave();
    }
}

/* the slot's own for ssGhostDraw's and ssGhostHide's which */
static int slotOf(int which) {
    return which == 2 ? BROTHER : which == 3 ? FOLLOWER : which == 4 ? COMPANION : which;
}

static int ours(int w) {
    AnimationPose* p;
    unsigned magic;
    if (sGhost[w].pose1 == 0) {
        return 0;
    }
    p = animPoseGetAnimPosePtr(sGhost[w].pose1 - 1);
    memcpy(&magic, p->pad_16C, sizeof(magic));
    return (p->flags & ANIM_POSE_FLAG_USED) && magic == GHOST_MAGIC + (unsigned)w &&
           !strcmp(animPoseGetAnimBaseDataPtr(sGhost[w].pose1 - 1)->fileName, sGhost[w].model);
}

static void letGo(int w) {
    if (ours(w)) {
        AnimationPose* p = animPoseGetAnimPosePtr(sGhost[w].pose1 - 1);
        memset(p->pad_16C, 0, sizeof(p->pad_16C));
        animPoseRelease(sGhost[w].pose1 - 1);
    }
    sGhost[w].pose1 = 0;
    sGhost[w].model[0] = sGhost[w].anim[0] = '\0';
}

static int hasAnim(int pose, const char* anim) {
    AnimationPoseData* d = animPoseGetAnimBaseDataPtr(pose);
    int i;
    for (i = 0; i < d->animCount; i++) {
        if (!strcmp(d->anims[i].name, anim)) {
            return 1;
        }
    }
    return 0;
}

static void ghostDisp(CameraId camId, void* param) {
    const int w = (int)(intptr_t)param;
    Ghost* g = &sGhost[w];
    MarioWork* mario = marioGetPtr();
    Mtx m, t;
    float scale, dir;
    (void)camId;
    if (!ours(w) || mario == NULL) {
        return;
    }
    scale = (w % PER != 0 && w % PER != STAND_IN ? 2.0f : marioGetScale()) * g->scale;
    PSMTXScale(m, scale, scale, scale);
    /* (the other game's number: fmodf, never a loop a huge one would spin forever) */
    dir = fmodf(g->at.dir, 360.0f);
    if (dir < 0.0f) {
        dir += 360.0f;
    }
    if (dir >= 90.0f && dir <= 270.0f) {
        PSMTXScale(t, 1.0f, 1.0f, -1.0f);
        PSMTXConcat(t, m, m);
    }
    PSMTXRotRad(t, 'y', 0.017453292f * (g->at.dir - mario->targetYaw));
    PSMTXConcat(t, m, m);
    PSMTXTrans(t, g->at.x, g->at.y, g->at.z);
    PSMTXConcat(t, m, m);
    {
        /* see-through: the pose's material colour's alpha (a costume takes the pose's material) */
        AnimationPose* p = animPoseGetAnimPosePtr(g->pose1 - 1);
        if (portStateSyncGhostSeeThrough()) {
            p->materialFlag |= ANIM_MATERIAL_FLAG_EVT_COLOR;
            p->materialColor = (GXColor){0xFF, 0xFF, 0xFF, SEE_THROUGH_ALPHA};
        } else if (p->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR) {
            p->materialFlag &= ~ANIM_MATERIAL_FLAG_EVT_COLOR;
            p->materialColor = (GXColor){0xFF, 0xFF, 0xFF, 0xFF};
        }
    }
    {
        /* their costume: on their pick standing in (fitted to it), or on Mario's own field model */
        const int wears = g->at.costume > 0 && (w % PER == STAND_IN || (w % PER == 0 && !strncmp(g->model, "a_mario", 7)));
        const int pick = w % PER == STAND_IN ? g->at.chr : 0;
        const int helper = w % PER == PARTNER || w % PER == FOLLOWER || w % PER == COMPANION;
        /* a co-op guest: the host's partner, follower and companion are the characters its players play, and
           its own walk of its seat's one wears this computer's skin for it (port/costume.h): the host's word of
           them wears it too, so a hold never swaps the look */
        float skinScale = 1.0f;
        const int skin = helper && g->at.pm64 <= 0 && portStateSyncCoopGuest() ? portSkinStandIn(g->pose1 - 1, &skinScale)
                                                                                : -1;
        int xlu;
        for (xlu = 2; xlu >= 1; xlu--) {
            if (skin >= 0) {
                AnimationPose* own = animPoseGetAnimPosePtr(g->pose1 - 1);
                AnimationPose* in = animPoseGetAnimPosePtr(skin);
                in->materialFlag = own->materialFlag; /* (see-through as the ghost is) */
                in->materialColor = own->materialColor;
                animPoseDrawMtx(skin, m, xlu, 0.0f, skinScale);
                continue;
            }
            /* their Paper Mario 64 partner in their partner's place (or the host's follower's or companion's,
               a co-op guest's): drawn as it */
            if (helper && g->at.pm64 > 0 &&
                portPm64DrawPoseAs(g->pose1 - 1, m, xlu, 0.0f, 1.0f, g->at.pm64)) {
                portPm64NoteDrawn(g->pose1 - 1);
                continue;
            }
            portPm64HostDrawCheck(g->pose1 - 1, xlu); /* (never their PM64 partner's host a moment after it) */
            animPoseDrawMtx(g->pose1 - 1, m, xlu, 0.0f, 1.0f);
            if (wears) {
                portCostumeDrawOther(g->at.costume, pick, g->pose1 - 1, m, xlu, 0.0f, 1.0f);
            }
        }
    }
}

/* a model name from the other game fit to load: a file of a/ by its name alone (no path, not a texture's
   "name-") */
static int modelNameOk(const char* m) {
    const size_t n = strlen(m);
    return n > 0 && strpbrk(m, "/\\:") == NULL && strstr(m, "..") == NULL && m[n - 1] != '-';
}

/* slot w holds model (loaded now when it didn't): 0 when the pool has no room */
static int load(int w, const char* model) {
    Ghost* g = &sGhost[w];
    if (!modelNameOk(model)) {
        return 0;
    }
    if (g->pose1 != 0 && !ours(w)) {
        g->pose1 = 0; /* freed with the room's poses */
    }
    if (g->pose1 != 0 && strcmp(g->model, model) != 0) {
        letGo(w);
    }
    if (g->pose1 == 0) {
        int id;
        AnimationPose* p;
        if (!portCosmeticPoolRoom()) {
            return 0;
        }
        id = animPoseEntry(model, 2);
        if (id < 0) {
            return 0;
        }
        p = animPoseGetAnimPosePtr(id);
        {
            const unsigned magic = GHOST_MAGIC + (unsigned)w;
            memcpy(p->pad_16C, &magic, sizeof(magic));
        }
        g->pose1 = id + 1;
        snprintf(g->model, sizeof(g->model), "%s", model);
        g->anim[0] = '\0';
        ssLog("ghost: other %d's %s model %s loaded (pose %d)", w / PER,
              w % PER == PARTNER    ? "partner's"
              : w % PER == STAND_IN ? "pick's"
              : w % PER == BROTHER  ? "Luigi's"
              : w % PER == FOLLOWER ? "follower's"
              : w % PER == COMPANION ? "companion's"
                                    : "player's",
              g->model, id);
    }
    return 1;
}

/* slot w (loaded) plays anim, stepped once this frame, and is drawn at pose */
static void show(int w, const char* anim, const SsGhostPose* pose, float scale) {
    Ghost* g = &sGhost[w];
    Vec v;
    if (strcmp(g->anim, anim) != 0 && anim[0] && hasAnim(g->pose1 - 1, anim)) {
        animPoseSetAnim(g->pose1 - 1, anim, 1);
        snprintf(g->anim, sizeof(g->anim), "%s", anim);
    }
    if (g->shown != gp->retraceCount) {
        animPoseMain(g->pose1 - 1);
    }
    g->shown = gp->retraceCount;
    g->at = *pose;
    g->scale = scale;
    v.x = pose->x;
    v.y = pose->y;
    v.z = pose->z;
    dispEntry(CAMERA_3D, dispCalcZ(&v), 1, ghostDisp, (void*)(intptr_t)w);
}

void ssGhostDraw(int other, int which, const SsGhostPose* pose) {
    const int base = other * PER;
    if (other < 0 || other >= OTHERS || which < 0 || which > 4) {
        return;
    }
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle || pose->model[0] == '\0') {
        return;
    }
    if (memchr(pose->model, '\0', sizeof(pose->model)) == NULL || memchr(pose->anim, '\0', sizeof(pose->anim)) == NULL) {
        return; /* (the other game's names, unterminated: not drawn) */
    }
    /* their pick, standing in for Mario's field model (not a scene's own, e_mario) */
    if (which == 0 && pose->chr > 0 && !strncmp(pose->model, "a_mario", 7)) {
        const char* model = portPlayAsGhostModel(pose->chr, pose->anim);
        const char* anim;
        if (model != NULL && load(base + STAND_IN, model) &&
            (anim = portPlayAsGhostAnim(pose->chr, pose->anim, sGhost[base + STAND_IN].pose1 - 1)) != NULL &&
            hasAnim(sGhost[base + STAND_IN].pose1 - 1, anim)) {
            show(base + STAND_IN, anim, pose, portPlayAsScale(pose->chr));
            return;
        }
    } else if (which == 0 && sGhost[base + STAND_IN].pose1 != 0) {
        letGo(base + STAND_IN);
    }
    if (load(base + slotOf(which), pose->model)) {
        show(base + slotOf(which), pose->anim, pose, 1.0f);
    }
}

void ssGhostHide(int other, int which) {
    int w;
    for (w = 0; w < SLOTS; w++) {
        const int k = w % PER;
        if (sGhost[w].pose1 == 0 || (other >= 0 && w / PER != other)) {
            continue;
        }
        if (which < 0 || slotOf(which) == k || (which == 0 && k == STAND_IN)) {
            letGo(w);
        }
    }
}
