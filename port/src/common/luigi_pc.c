/* Play as Luigi (port/include/port/luigi.h, docs/PLAYABLE_LUIGI.md): a
 * stand-in pose of Luigi's field model for Mario's, in the field and for
 * Mario's unit in battle. The state is in the save-state section
 * (CMakeLists.txt), so a save state keeps the pose ids it points at; each
 * frame still checks the pose is the c_luigi this file loaded. */
#include <string.h>
#include "port/luigi.h"
#include "port/gameplus.h"
#include "driver/animdrv.h"

/* Mario's animations (field a_mario / a_mario_r, battle b_mario) Luigi has a
 * version of; the R names are the back-facing ones. */
static const char* const kMap[][2] = {
    {"M_S_1", "S_1"},     {"M_Z_1", "Z_1"},     {"M_W_1", "W_1"},     {"M_R_1", "R_1"},
    {"M_J_1A", "J_1A"},   {"M_J_1B", "J_1B"},   {"M_J_1C", "J_1B"},   {"M_I_2", "T_1"},
    {"M_I_3", "T_1"},     {"M_S_1R", "S_1R"},   {"M_Z_1R", "Z_1R"},   {"M_W_1R", "W_1R"},
    {"M_R_1R", "R_1R"},   {"M_J_1BR", "J_1BR"}, {"M_J_1CR", "J_1BR"}, {"M_I_2R", "T_1R"},
    {"M_I_3R", "T_1R"},
};

#define LUIGI_MODEL "c_luigi"

/* per place (field, battle): the Mario pose stood in for (-1: none), Luigi's
 * pose (-1: not loaded) and the animation it plays */
static struct {
    int source;
    int pose;
    const char* anim;
} sStandIn[2] = {{-1, -1, NULL}, {-1, -1, NULL}};

int portLuigiOn(void) {
    return portGameplus("playable_luigi");
}

static const char* luigiAnim(const char* mario) {
    size_t i;
    if (mario == NULL) {
        return NULL;
    }
    for (i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++) {
        if (!strcmp(kMap[i][0], mario)) {
            return kMap[i][1];
        }
    }
    return NULL;
}

/* the pool type Luigi's pose is loaded with: field poses as Mario's (shared,
 * kept across maps), battle ones freed at the battle's end */
static int poolType(int where) {
    return where == PORT_LUIGI_BATTLE ? 1 : 2;
}

/* pose is still the Luigi pose this file loaded for where */
static int poseValid(int where, int pose) {
    AnimationPose* p;
    if (pose < 0) {
        return 0;
    }
    p = animPoseGetAnimPosePtr(pose);
    return (p->flags & ANIM_POSE_FLAG_USED) && p->typeFlags == 0 && p->type == poolType(where) &&
           !strcmp(animPoseGetAnimBaseDataPtr(pose)->fileName, LUIGI_MODEL);
}

void portLuigiUpdate(int where, int poseId, int allowed) {
    AnimationPose* src;
    AnimationPose* dst;
    const char* anim = NULL;
    int on = portLuigiOn();

    if (!poseValid(where, sStandIn[where].pose)) {
        sStandIn[where].pose = -1;
    }
    if (on && allowed && poseId >= 0) {
        src = animPoseGetAnimPosePtr(poseId);
        /* a paper form (plane, boat, tube, ...) is drawn from Mario's pose */
        if (src->paperId == -1) {
            anim = luigiAnim(animPoseGetCurrentAnim(poseId)->name);
        }
    }
    if (anim == NULL) {
        sStandIn[where].source = -1;
        if (!on && sStandIn[where].pose >= 0) {
            animPoseRelease(sStandIn[where].pose);
            sStandIn[where].pose = -1;
        }
        return;
    }
    if (sStandIn[where].pose < 0) {
        sStandIn[where].pose = animPoseEntry(LUIGI_MODEL, poolType(where));
        sStandIn[where].anim = NULL;
        if (sStandIn[where].pose < 0) {
            sStandIn[where].pose = -1;
            sStandIn[where].source = -1;
            return;
        }
    }
    dst = animPoseGetAnimPosePtr(sStandIn[where].pose);
    if (sStandIn[where].source != poseId || sStandIn[where].anim != anim) {
        animPoseSetAnim(sStandIn[where].pose, anim, TRUE);
        sStandIn[where].anim = anim;
    }
    sStandIn[where].source = poseId;
    dst->localTimeRate = src->localTimeRate;
    animPoseMain(sStandIn[where].pose);
    /* the paper turn as Mario's, after both were stepped */
    dst->flags = (dst->flags & ~(ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN)) |
                 (src->flags & (ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN));
    dst->peraRotY = src->peraRotY;
    dst->rotationY = src->rotationY;
    dst->peraTargetRotY = src->peraTargetRotY;
    dst->scaleZFlip = src->scaleZFlip;
}

void portLuigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale) {
    AnimationPose* src;
    AnimationPose* dst;
    int where;

    for (where = 0; where < 2; where++) {
        /* (a battle's stand-in stays recorded after the battle freed it) */
        if (poseId >= 0 && sStandIn[where].source == poseId && poseValid(where, sStandIn[where].pose)) {
            src = animPoseGetAnimPosePtr(poseId);
            dst = animPoseGetAnimPosePtr(sStandIn[where].pose);
            /* fades, flashes, Charge's shine and the blur's alpha: Mario's
             * material as the game set it for this draw */
            dst->materialFlag = src->materialFlag;
            dst->materialLightFlag = src->materialLightFlag;
            dst->materialColor = src->materialColor;
            dst->materialColor2 = src->materialColor2;
            dst->gxCallback = src->gxCallback;
            dst->disableDraw = src->disableDraw;
            animPoseDrawMtx(sStandIn[where].pose, mtx, xluMode, rotY, scale);
            return;
        }
    }
    animPoseDrawMtx(poseId, mtx, xluMode, rotY, scale);
}
