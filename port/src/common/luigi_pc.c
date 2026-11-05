/* Play as Luigi (port/include/port/luigi.h, docs/PLAYABLE_LUIGI.md): a
 * stand-in pose of Luigi's field model for Mario's, in the field and for
 * Mario's unit in battle. The state is in the save-state section
 * (CMakeLists.txt), so a save state keeps the pose ids it points at; each
 * frame still checks the pose is the c_luigi this file loaded. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/luigi.h"
#include "port/costume.h"
#include "port/gameplus.h"
#include "port/paper.h"
#include "port/toggles.h"
#include "driver/animdrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "pmario_sound.h"
#ifdef TTYD_SAVE_STATES
/* the game keeps pointers to the texts made here: on the save-state heap */
#define PORT_SNAPHEAP_REDIRECT
#include "port/snapheap.h"
#endif

/* Who stands in for Mario (Enhancements "Play as", the setting playable_luigi:
 * 0 Mario, 1 Luigi, 2 Peach, 3 Bowser; docs/PLAY_AS.md). Each is drawn from
 * their own field model, Mario's pose running underneath as before. */

/* Mario's animations a character has a disc version of; back: from the
 * character's back-facing model (Peach's c_peach_r) */
typedef struct {
    const char* mario;
    const char* to;
    int back;
} AnimMap;

static const AnimMap kLuigiMap[] = {
    {"M_S_1", "S_1", 0},     {"M_Z_1", "Z_1", 0},     {"M_W_1", "W_1", 0},     {"M_R_1", "R_1", 0},
    {"M_J_1A", "J_1A", 0},   {"M_J_1B", "J_1B", 0},   {"M_J_1C", "J_1B", 0},   {"M_I_2", "T_1", 0},
    {"M_I_3", "T_1", 0},     {"M_S_1R", "S_1R", 0},   {"M_Z_1R", "Z_1R", 0},   {"M_W_1R", "W_1R", 0},
    {"M_R_1R", "R_1R", 0},   {"M_J_1BR", "J_1BR", 0}, {"M_J_1CR", "J_1BR", 0}, {"M_I_2R", "T_1R", 0},
    {"M_I_3R", "T_1R", 0},
};

/* Peach (c_peach, c_peach_r): her interlude set. She never jumps there: a hop
 * is her arms-out flourish until her own jump (the pack) is there. */
static const AnimMap kPeachMap[] = {
    {"M_S_1", "P_S_1", 0},    {"M_Z_1", "P_Z_1", 0},    {"M_W_1", "P_W_1", 0},    {"M_R_1", "P_R_1", 0},
    {"M_I_2", "P_T_1", 0},    {"M_I_3", "P_T_1", 0},    {"M_J_1A", "P_A_1", 0},   {"M_J_1B", "P_A_2", 0},
    {"M_J_1C", "P_A_2", 0},   {"M_I_1", "P_I_1", 0},    {"M_I_S", "P_S_4", 0},    {"M_D_6", "P_S_4", 0},
    {"M_V_1", "P_S_5", 0},    {"M_V_2", "P_S_5", 0},    {"M_N_8", "P_S_5", 0},    {"M_X_1", "P_S_5", 0},
    {"M_S_1R", "P_S_1R", 1},  {"M_Z_1R", "P_Z_1R", 1},  {"M_W_1R", "P_W_1R", 1},  {"M_R_1R", "P_R_1R", 1},
    {"M_I_2R", "P_T_1R", 1},  {"M_I_3R", "P_T_1R", 1},  {"M_J_1BR", "P_A_1R", 1}, {"M_J_1CR", "P_A_1R", 1},
};

/* Bowser (c_koopa, his 3D interlude set; no back view: his front serves).
 * Where our pack (c_koopa_pc) has one of these names it wins; the rest are
 * his disc poses that fit (getting up, keeling over, lying dazed, asleep) */
static const AnimMap kBowserMap[] = {
    {"M_S_1", "KPA_S_1", 0},   {"M_Z_1", "KPA_Z_1", 0},   {"M_W_1", "KPA_W_1", 0},   {"M_R_1", "KPA_R_1", 0},
    {"M_J_1A", "KPA_J_1A", 0}, {"M_J_1B", "KPA_J_1B", 0}, {"M_J_1C", "KPA_J_1B", 0}, {"M_I_2", "KPA_T_1", 0},
    {"M_I_3", "KPA_T_1", 0},   {"M_D_1", "KPA_D_1", 0},   {"M_D_2", "KPA_D_2", 0},   {"M_D_6", "KPA_D_1", 0},
    {"M_H_1", "KPA_A_1B", 0},  {"M_H_4", "KPA_A_1B", 0},  {"M_H_7", "KPA_A_1B", 0},  {"M_V_1", "KPA_X_1", 0},
    {"M_V_2", "KPA_X_1", 0},   {"M_X_1", "KPA_X_1", 0},   {"M_U_3", "KPA_U_1", 0},   {"M_D_8", "KPA_D_2", 0},
    {"M_D_7", "KPA_D_4", 0},   {"M_Z_10", "KPA_Q_1", 0},  {"M_Z_11", "KPA_N_2", 0},  {"M_I_6", "KPA_N_2", 0},
    {"M_H_16", "KPA_U_1", 0},
};

/* What a character does where neither its model nor the map has Mario's
 * animation: the nearest of its plain moves, by the kind of Mario's */
typedef struct {
    const char *idle, *stand, *walk, *run, *talk, *up, *down, *hurt, *attack;
} PlainMoves;

typedef struct {
    const char* model;   /* the disc's */
    const char* pcModel; /* ours, made at startup with our animations (NULL: none yet) */
    const char* back;    /* the back-facing model (NULL: the front's R names, or the front) */
    const char* pcBack;  /* ours of it, made at startup with our animations (NULL: none) */
    const AnimMap* map;
    int mapCount;
    float scale;       /* drawn at this size in Mario's place */
    float menuScale;   /* and in the menus' figures (the pause menu, the journal map): about his height */
    int always;        /* never falls back to Mario's model */
    PlainMoves front, rear;
} PlayChar;

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int sModelReady[PORT_PLAY_AS_COUNT][2];

static const PlayChar kChars[PORT_PLAY_AS_COUNT] = {
    {NULL},
    {"c_luigi", "c_luigi_pc", NULL, NULL, kLuigiMap, COUNT(kLuigiMap), 1.0f, 1.0f, 0,
     {"Z_1", "S_1", "W_1", "R_1", "T_1", "J_1A", "J_1B", "S_1", "S_1"},
     {"Z_1R", "S_1R", "W_1R", "R_1R", "T_1R", "J_1AR", "J_1BR", "S_1R", "S_1R"}},
    {"c_peach", "c_peach_pc", "c_peach_r", "c_peach_r_pc", kPeachMap, COUNT(kPeachMap), 1.0f, 1.0f, 1,
     {"P_Z_1", "P_S_1", "P_W_1", "P_R_1", "P_T_1", "P_A_1", "P_A_2", "P_S_4", "P_A_1"},
     {"P_Z_1R", "P_S_1R", "P_W_1R", "P_R_1R", "P_T_1R", "P_A_1R", "P_A_1R", "P_S_1R", "P_A_1R"}},
    {"c_koopa", "c_koopa_pc", NULL, NULL, kBowserMap, COUNT(kBowserMap), 0.7f, 0.5f, 1,
     {"KPA_Z_1", "KPA_S_1", "KPA_W_1", "KPA_R_1", "KPA_T_1", "KPA_J_1A", "KPA_J_1B", "KPA_D_1", "KPA_A_1B"},
     {"KPA_Z_1", "KPA_S_1", "KPA_W_1", "KPA_R_1", "KPA_T_1", "KPA_J_1A", "KPA_J_1B", "KPA_D_1", "KPA_A_1B"}},
};

/* the front model (0) or the back one (1) a character's stand-in loads */
static const char* charModel(int chr, int back) {
    const PlayChar* c = &kChars[chr];
    if (back && c->back != NULL) {
        return sModelReady[chr][1] && c->pcBack != NULL ? c->pcBack : c->back;
    }
    return sModelReady[chr][0] && c->pcModel != NULL ? c->pcModel : c->model;
}

/* per place (field, battle, the menus' two figures, versus): the Mario pose
 * stood in for (-1: none), whose stand-in it is, its poses (front, back; -1:
 * not loaded), the one drawn and the animation it plays */
#define SLOTS 5
static struct {
    int source;
    int chr;
    int pose[2];
    int cur;
    char anim[60];
} sStandIn[SLOTS] = {{-1, 0, {-1, -1}, 0, ""}, {-1, 0, {-1, -1}, 0, ""}, {-1, 0, {-1, -1}, 0, ""},
                     {-1, 0, {-1, -1}, 0, ""}, {-1, 0, {-1, -1}, 0, ""}};

int portPlayAs(void) {
    int v = portGameplus("playable_luigi");
    return v > 0 && v < PORT_PLAY_AS_COUNT ? v : 0;
}

int portLuigiOn(void) {
    return portPlayAs() == PORT_PLAY_AS_LUIGI;
}

void portLuigiSetModelReady(int ready) {
    sModelReady[PORT_PLAY_AS_LUIGI][0] = ready;
}

int portLuigiModelReady(void) {
    return sModelReady[PORT_PLAY_AS_LUIGI][0];
}

void portPlayAsSetModelReady(int chr, int back, int ready) {
    if (chr > 0 && chr < PORT_PLAY_AS_COUNT) {
        sModelReady[chr][back != 0] = ready;
    }
}

#define LUIGI_MODEL charModel(PORT_PLAY_AS_LUIGI, 0)

/* A character's version of Mario's animation: one of the same name in its
 * model (ours are named as Mario's), else the disc's by its map. In battle,
 * b_mario reuses some of a_mario's names for other motions (M_H_2: a
 * battle hammer attack), so there a field animation of ours is used only
 * where Mario's two are the same animation; a battle one is named
 * "<name>@b". */
static const char* const kSameInBattle[] = {
    "M_S_3", "M_W_1", "M_R_1", "M_J_1B", "M_J_1C", "M_H_6", "M_H_6A", "M_H_6B", "M_B_1", "M_B_2",
    "M_D_1", "M_D_2", "M_I_1", "M_I_2", "M_X_1", "M_U_2", "M_C_2", "M_B_3", "M_V_2",
    "M_N_7", /* (b_mario and e_mario) */
};

static const char* findAnim(AnimationPoseData* d, const char* name) {
    size_t i;
    for (i = 0; i < (size_t)d->animCount; i++) {
        if (!strcmp(d->anims[i].name, name)) {
            return d->anims[i].name;
        }
    }
    return NULL;
}

/* Mario's animation is a back-facing one (a_mario_r's: M_S_1R, M_H_1R, ...) */
static int isBack(const char* mario) {
    size_t n = strlen(mario);
    return n > 4 && mario[n - 1] == 'R' && mario[n - 2] != '_';
}

/* the plain move nearest Mario's, by its kind (M_W_ walking, M_J_ jumping, ...) */
static const char* plainMove(const PlainMoves* m, const char* mario) {
    if (!strcmp(mario, "M_Z_1") || !strcmp(mario, "M_Z_1R")) {
        return m->idle;
    }
    if (!strncmp(mario, "M_W_", 4)) {
        return m->walk;
    }
    if (!strncmp(mario, "M_R_", 4)) {
        return m->run;
    }
    if (!strncmp(mario, "M_J_1A", 6) || !strncmp(mario, "M_J_5A", 6) || !strncmp(mario, "M_A_3A", 6)) {
        return m->up;
    }
    if (!strncmp(mario, "M_J_", 4) || !strncmp(mario, "M_A_", 4) || !strncmp(mario, "M_Y_", 4)) {
        return m->down;
    }
    if (!strncmp(mario, "M_D_", 4) || !strncmp(mario, "M_U_3", 5) || !strncmp(mario, "M_Z_", 4)) {
        return m->hurt;
    }
    if (!strncmp(mario, "M_I_2", 5) || !strncmp(mario, "M_I_3", 5) || !strncmp(mario, "M_N_B", 5)) {
        return m->talk;
    }
    if (!strncmp(mario, "M_H_", 4)) {
        return m->attack;
    }
    return m->stand;
}

static const char* charAnim(int chr, int where, int pose, int backPose, const char* mario, int* back) {
    const PlayChar* c = &kChars[chr];
    AnimationPoseData* d;
    const char* found;
    char battle[64];
    int i;
    *back = 0;
    if (mario == NULL || chr <= 0) {
        return NULL;
    }
    /* a back-facing animation of Mario's: the back model's version (ours in
     * c_peach_r_pc) */
    if (backPose >= 0 && isBack(mario) && (found = findAnim(animPoseGetAnimBaseDataPtr(backPose), mario)) != NULL) {
        *back = 1;
        return found;
    }
    if (pose >= 0) {
        d = animPoseGetAnimBaseDataPtr(pose);
        if (where == PORT_LUIGI_BATTLE || where == PORT_LUIGI_VERSUS) {
            snprintf(battle, sizeof(battle), "%s@b", mario);
            if ((found = findAnim(d, battle)) != NULL) {
                return found;
            }
            for (i = 0; i < COUNT(kSameInBattle); i++) {
                if (!strcmp(kSameInBattle[i], mario) && (found = findAnim(d, mario)) != NULL) {
                    return found;
                }
            }
        } else if ((found = findAnim(d, mario)) != NULL) {
            return found;
        }
    }
    for (i = 0; i < c->mapCount; i++) {
        if (!strcmp(c->map[i].mario, mario)) {
            *back = c->map[i].back;
            return c->map[i].to;
        }
    }
    if (!c->always || !strcmp(mario, "M_H_12")) {
        return NULL; /* (M_H_12: only the thrown hammer is drawn, right as Mario's) */
    }
    if (isBack(mario)) {
        *back = c->back != NULL;
        return plainMove(&c->rear, mario);
    }
    return plainMove(&c->front, mario);
}

/* the pool type a stand-in's pose is loaded with: field poses as Mario's
 * (shared, kept across maps), battle ones freed at the battle's end, the
 * menus' as the menus' own figures (field) */
static int poolType(int where) {
    return where == PORT_LUIGI_BATTLE || where == PORT_LUIGI_VERSUS ? 1 : where == PORT_LUIGI_FIELD ? 2 : 0;
}

/* pose is still the pose this file loaded for where: chr's model (back or front) */
static int poseValid(int where, int chr, int back, int pose) {
    AnimationPose* p;
    if (pose < 0 || chr <= 0) {
        return 0;
    }
    p = animPoseGetAnimPosePtr(pose);
    return (p->flags & ANIM_POSE_FLAG_USED) && p->typeFlags == 0 && p->type == poolType(where) &&
           !strcmp(animPoseGetAnimBaseDataPtr(pose)->fileName, charModel(chr, back));
}

/* the slot's stand-in poses go (the pick changed, or the option is off) */
static void slotFree(int where) {
    int b;
    for (b = 0; b < 2; b++) {
        if (poseValid(where, sStandIn[where].chr, b, sStandIn[where].pose[b])) {
            animPoseRelease(sStandIn[where].pose[b]);
        }
        sStandIn[where].pose[b] = -1;
    }
    sStandIn[where].source = -1;
    sStandIn[where].anim[0] = '\0';
}

void portLuigiUpdate(int where, int poseId, int allowed) {
    AnimationPose* src;
    AnimationPose* dst;
    const char* anim = NULL;
    int back = 0, b, pose;
    /* (versus: Luigi's side's unit is always Luigi) */
    int chr = where == PORT_LUIGI_VERSUS ? PORT_PLAY_AS_LUIGI : portPlayAs();

    /* the menus' figures: Mario's own model only (not 8-bit Mario's d_mario,
     * whose animations have names of Luigi's); not versus's battle unit
     * (b_mario), whose slot comes after the menus' */
    if ((where == PORT_LUIGI_MENU || where == PORT_LUIGI_LOG) && poseId >= 0 &&
        strncmp(animPoseGetAnimBaseDataPtr(poseId)->fileName, "a_mario", 7) != 0) {
        allowed = 0;
    }
    /* his costume follows this pose (Mario's or Luigi's: not Peach's, Bowser's or 8-bit Mario's) */
    portCostumeNoteMario(where, allowed && chr <= 1 && where != PORT_LUIGI_VERSUS ? poseId : -1);
    if (sStandIn[where].chr != chr) {
        slotFree(where);
        sStandIn[where].chr = chr;
    }
    for (b = 0; b < 2; b++) {
        if (!poseValid(where, chr, b, sStandIn[where].pose[b])) {
            sStandIn[where].pose[b] = -1;
        }
    }
    src = NULL;
    if (chr > 0 && allowed && poseId >= 0) {
        src = animPoseGetAnimPosePtr(poseId);
        /* (a paper form, plane, boat, spin, ..., is drawn from a capture of
         * the pose: the stand-in's while there is one, animPaperPoseDispSub) */
        if (sStandIn[where].pose[0] < 0) {
            sStandIn[where].pose[0] = animPoseEntry(charModel(chr, 0), poolType(where));
            sStandIn[where].anim[0] = '\0';
            if (sStandIn[where].pose[0] < 0) {
                sStandIn[where].pose[0] = -1;
            }
        }
        {
            const char* mario = animPoseGetCurrentAnim(poseId)->name;
            /* (the back model, loaded once a back-facing animation wants it) */
            if (mario != NULL && isBack(mario) && kChars[chr].back != NULL && sStandIn[where].pose[1] < 0) {
                sStandIn[where].pose[1] = animPoseEntry(charModel(chr, 1), poolType(where));
                sStandIn[where].anim[0] = '\0';
                if (sStandIn[where].pose[1] < 0) {
                    sStandIn[where].pose[1] = -1;
                }
            }
            anim = charAnim(chr, where, sStandIn[where].pose[0], sStandIn[where].pose[1], mario, &back);
        }
        if (anim != NULL && back && sStandIn[where].pose[1] < 0) {
            sStandIn[where].pose[1] = animPoseEntry(charModel(chr, 1), poolType(where));
            sStandIn[where].anim[0] = '\0';
            if (sStandIn[where].pose[1] < 0) {
                sStandIn[where].pose[1] = -1;
            }
        }
        if (getenv("TTYD_LUIGI_TRACE") != NULL) {
            static const char* const kWhere[SLOTS] = {"field", "battle", "menu", "journal", "versus"};
            static char last[SLOTS][80];
            char now[80];
            snprintf(now, sizeof(now), "%s %d %s%s", animPoseGetCurrentAnim(poseId)->name, chr,
                     anim != NULL ? anim : (src->paperId != -1 ? "(paper)" : "-"), back ? " (back)" : "");
            if (strcmp(now, last[where]) != 0) {
                printf("[play-as] %s: Mario %s\n", kWhere[where], now);
                fflush(stdout);
                strcpy(last[where], now);
            }
        }
    }
    pose = anim != NULL ? sStandIn[where].pose[back] : -1;
    if (pose < 0) {
        sStandIn[where].source = -1;
        if (chr <= 0) {
            slotFree(where);
        }
        return;
    }
    dst = animPoseGetAnimPosePtr(pose);
    if (sStandIn[where].source != poseId || sStandIn[where].cur != back || strcmp(sStandIn[where].anim, anim) != 0) {
        animPoseSetAnim(pose, anim, TRUE);
        strncpy(sStandIn[where].anim, anim, sizeof(sStandIn[where].anim) - 1);
        sStandIn[where].anim[sizeof(sStandIn[where].anim) - 1] = '\0';
    }
    sStandIn[where].source = poseId;
    sStandIn[where].cur = back;
    /* Mario's clock: the game sets his time to hold or scrub a pose (the spin
     * hammer's charge, events), and our versions keep Mario's timing; the
     * disc's own animations of another length keep their own clock */
    if (dst->type == src->type && (chr == PORT_PLAY_AS_LUIGI || !strncmp(anim, "M_", 2))) {
        dst->localTime = src->localTime;
    }
    dst->localTimeRate = src->localTimeRate;
    animPoseMain(pose);
    /* the paper turn as Mario's, after both were stepped */
    dst->flags = (dst->flags & ~(ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN)) |
                 (src->flags & (ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN));
    dst->peraRotY = src->peraRotY;
    dst->rotationY = src->rotationY;
    dst->peraTargetRotY = src->peraTargetRotY;
    dst->scaleZFlip = src->scaleZFlip;
}

/* the slot's stand-in pose drawn now (-1: none) */
static int slotPose(int where) {
    int cur = sStandIn[where].cur;
    return poseValid(where, sStandIn[where].chr, cur, sStandIn[where].pose[cur]) ? sStandIn[where].pose[cur] : -1;
}

static int npcStandIn(int poseId);
static void npcRelease(int poseId);

/* the material state of src as the game set it for this draw, onto dst:
 * fades, flashes, Charge's shine, the blur's alpha, an NPC's colour */
static void copyMaterial(AnimationPose* dst, const AnimationPose* src) {
    dst->materialFlag = src->materialFlag;
    dst->materialLightFlag = src->materialLightFlag;
    dst->materialColor = src->materialColor;
    dst->materialColor2 = src->materialColor2;
    dst->gxCallback = src->gxCallback;
    dst->disableDraw = src->disableDraw;
}

static void luigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale);

/* paper shadows and paper physics (port/include/port/paper.h) around the draw */
void portLuigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale) {
    portPaperDrawMtx(poseId, mtx, xluMode, rotY, scale, luigiDrawMtx);
}

static void luigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale) {
    AnimationPose* src;
    int where, pose, npc;

    if ((npc = npcStandIn(poseId)) >= 0) {
        copyMaterial(animPoseGetAnimPosePtr(npc), animPoseGetAnimPosePtr(poseId));
        animPoseDrawMtx(npc, mtx, xluMode, rotY, scale);
        return;
    }
    for (where = 0; where < SLOTS; where++) {
        /* (a battle's stand-in stays recorded after the battle freed it) */
        if (poseId >= 0 && sStandIn[where].source == poseId && (pose = slotPose(where)) >= 0 &&
            animPoseGetAnimPosePtr(poseId)->paperId == -1) {
            const PlayChar* pc = &kChars[sStandIn[where].chr];
            float k = where == PORT_LUIGI_MENU || where == PORT_LUIGI_LOG ? pc->menuScale : pc->scale;
            src = animPoseGetAnimPosePtr(poseId);
            copyMaterial(animPoseGetAnimPosePtr(pose), src); /* Mario's, as the game set it for this draw */
            if (k != 1.0f) {
                /* (about the feet: the model's origin) */
                Mtx s, m;
                PSMTXScale(s, k, k, k);
                PSMTXConcat(mtx, s, m);
                animPoseDrawMtx(pose, m, xluMode, rotY, scale);
            } else {
                animPoseDrawMtx(pose, mtx, xluMode, rotY, scale);
                if (sStandIn[where].chr == 1) {
                    portCostumeDraw(pose, poseId, mtx, xluMode, rotY, scale); /* Luigi wears it too */
                }
            }
            return;
        }
    }
    { /* a partner's skin (port/include/port/costume.h) */
        float s = 1.0f;
        const int skin = portSkinStandIn(poseId, &s);
        if (skin >= 0) {
            AnimationPose* own = animPoseGetAnimPosePtr(poseId);
            AnimationPose* in = animPoseGetAnimPosePtr(skin);
            copyMaterial(in, own);
            animPoseDrawMtx(skin, mtx, xluMode, rotY, scale * s);
            /* the paper turn the draw made, as the partner's own draw would
             * have (the game waits on it: animPoseGetPeraEnd) */
            own->rotationY = in->rotationY;
            own->peraRotY = in->peraRotY;
            own->peraTargetRotY = in->peraTargetRotY;
            return;
        }
    }
    animPoseDrawMtx(poseId, mtx, xluMode, rotY, scale);
    portCostumeDraw(poseId, poseId, mtx, xluMode, rotY, scale);
}

struct AnimationPose* portLuigiPaperPose(struct AnimationPose* pose) {
    int where, p;
    for (where = 0; where < SLOTS; where++) {
        if (sStandIn[where].source >= 0 && animPoseGetAnimPosePtr(sStandIn[where].source) == pose &&
            (p = slotPose(where)) >= 0) {
            AnimationPose* dst = animPoseGetAnimPosePtr(p);
            /* the capture draws with the pose's own material state: Mario's */
            dst->materialFlag = pose->materialFlag;
            return dst;
        }
    }
    return pose;
}

void portLuigiRelease(int poseId) {
    int where;
    npcRelease(poseId);
    portSkinRelease(poseId);
    for (where = PORT_LUIGI_MENU; where <= PORT_LUIGI_LOG; where++) {
        if (poseId >= 0 && sStandIn[where].source == poseId) {
            slotFree(where);
        }
    }
    animPoseRelease(poseId);
}

/* ------------------------------------------------------------------ text */

/* The brothers' names in each spelling, swapped both ways in one pass (so a
 * name put in is never swapped back): the name, the letter a shout draws out
 * ("MARIOOO!", "LUIGIII!"). Entries 2k and 2k + 1 are the two brothers. */
static const struct {
    const char* name;
    char stretch;
} kNamesByChar[PORT_PLAY_AS_COUNT][6] = {
    {{"Mario", 'o'}, {"Mario", 'o'}, {"MARIO", 'O'}, {"MARIO", 'O'}, {"\x83\x7D\x83\x8A\x83\x49", 0},
     {"\x83\x7D\x83\x8A\x83\x49", 0}},
    {
        {"Mario", 'o'}, {"Luigi", 'i'}, {"MARIO", 'O'}, {"LUIGI", 'I'},
        {"\x83\x7D\x83\x8A\x83\x49", 0},         /* マリオ (Shift-JIS) */
        {"\x83\x8B\x83\x43\x81\x5B\x83\x57", 0}, /* ルイージ */
    },
    /* Mario and Peach trade places (so "Princess Peach" is "Princess Mario":
     * he is the one held captive), as do Mario and Bowser */
    {
        {"Mario", 'o'}, {"Peach", 'h'}, {"MARIO", 'O'}, {"PEACH", 'H'},
        {"\x83\x7D\x83\x8A\x83\x49", 0},
        {"\x83\x73\x81\x5B\x83\x60", 0}, /* ピーチ */
    },
    {
        {"Mario", 'o'}, {"Bowser", 'r'}, {"MARIO", 'O'}, {"BOWSER", 'R'},
        {"\x83\x7D\x83\x8A\x83\x49", 0},
        {"\x83\x4E\x83\x62\x83\x70", 0}, /* クッパ */
    },
};
/* the pick the text is swapped for (portLuigiText) */
static int sTextChr = PORT_PLAY_AS_LUIGI;
#define kNames (kNamesByChar[sTextChr])
#define NAMES 6
#define NAME_IS_JP(n) ((n) >= 4)
#define NAME_IS_MARIO(n) (((n) & 1) == 0)

/* a letter of a word in the Latin languages (ASCII, Latin-1's accented ones) */
static int isLetter(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
}

static int startsWith(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* The name kNames[n] starts at s + i as a brother's name: not part of a
 * longer word or another game's or series' title ("Paper Mario", "Super Mario
 * Bros.", "MARIO SERIES THEME MUSIC", "Luigi's Mansion", マリオストーリー,
 * ルイージマンション). *end: past it and a shout's drawn-out letters, *stretch:
 * how many of those. prevWide: the character before is a double-byte one. */
static int nameAt(const char* s, size_t i, int n, int prevWide, size_t* end, int* stretch) {
    const char* name = kNames[n].name;
    size_t len = strlen(name);
    size_t k;
    const char* after;

    if (strncmp(s + i, name, len) != 0) {
        return 0;
    }
    k = i + len;
    *stretch = 0;
    if (NAME_IS_JP(n)) {
        /* ペーパーマリオ; マリオブラザーズ, マリオストーリー, マリオシリーズ, マリオネット;
         * ルイージマンション */
        static const char* const kMarioNotAfter[] = {"\x83\x75\x83\x89", "\x83\x58\x83\x67", "\x83\x56\x83\x8A",
                                                     "\x83\x6C\x83\x62"};
        size_t j;
        if (NAME_IS_MARIO(n)) {
            if (i >= 8 && memcmp(s + i - 8, "\x83\x79\x81\x5B\x83\x70\x81\x5B", 8) == 0) {
                return 0;
            }
            for (j = 0; j < sizeof(kMarioNotAfter) / sizeof(kMarioNotAfter[0]); j++) {
                if (startsWith(s + k, kMarioNotAfter[j])) {
                    return 0;
                }
            }
        } else if (sTextChr == PORT_PLAY_AS_LUIGI && startsWith(s + k, "\x83\x7D\x83\x93\x83\x56\x83\x87\x83\x93")) {
            return 0;
        } else if (sTextChr == PORT_PLAY_AS_PEACH && i >= 2 && memcmp(s + i - 2, "\x83\x58", 2) == 0) {
            return 0; /* スピーチ, a speech */
        }
        *end = k;
        return 1;
    }
    if (i > 0 && !prevWide && isLetter((unsigned char)s[i - 1])) {
        return 0; /* the end of a longer word */
    }
    if (NAME_IS_MARIO(n) && i >= 6 && (s[i - 1] == ' ' || s[i - 1] == '\n') &&
        (strncmp(s + i - 6, "Paper", 5) == 0 || strncmp(s + i - 6, "PAPER", 5) == 0)) {
        return 0; /* the game's own title */
    }
    while (s[k] == kNames[n].stretch) {
        k++;
        (*stretch)++;
    }
    if (isLetter((unsigned char)s[k])) {
        /* "Marios", "Luigis" (German's genitive) are the name; "Mariole",
         * "Marioink" are other words */
        if (*stretch != 0 || (s[k] != 's' && s[k] != 'S') || isLetter((unsigned char)s[k + 1])) {
            return 0;
        }
    }
    if (*stretch == 0) {
        after = s + k;
        if (!NAME_IS_MARIO(n)) {
            if (sTextChr != PORT_PLAY_AS_LUIGI) {
                after = ""; /* (no titles of their own in the text) */
            }
            /* Luigi's Mansion (Luigis Mansion, Luigi's|Mansion) */
            if (*after == '\'' && (after[1] == 's' || after[1] == 'S')) {
                after += 2;
            } else if (*after == 's' || *after == 'S') {
                after++;
            }
            if ((*after == ' ' || *after == '\n') && (startsWith(after + 1, "Mansion") || startsWith(after + 1, "MANSION"))) {
                return 0;
            }
        } else if (*after == ' ' || *after == '\n') {
            if (startsWith(after + 1, "Bro") || startsWith(after + 1, "BRO") || startsWith(after + 1, "SERIES")) {
                return 0; /* the Mario Bros., Super Mario Bros., the series */
            }
        }
    }
    *end = k;
    return 1;
}

static void put(char** o, const char* s, size_t len) {
    memcpy(*o, s, len);
    *o += len;
}

/* msg with the brothers' names swapped; out has room for twice msg */
static void substitute(const char* s, char* out) {
    char* o = out;
    size_t i = 0;
    int prevWide = 0;
    int n, st;
    size_t end;

    while (s[i] != '\0') {
        unsigned char c = (unsigned char)s[i];
        if (c == '<') { /* a command, <col ...>, <p>: as it is */
            while (s[i] != '\0' && s[i] != '>') {
                *o++ = s[i++];
            }
            if (s[i] == '>') {
                *o++ = s[i++];
            }
            prevWide = 0;
            continue;
        }
        for (n = 0; n < NAMES; n++) {
            if (nameAt(s, i, n, prevWide, &end, &st)) {
                int other = n ^ 1;
                put(&o, kNames[other].name, strlen(kNames[other].name));
                for (; st > 0; st--) {
                    *o++ = kNames[other].stretch;
                }
                i = end;
                break;
            }
        }
        if (n < NAMES) {
            prevWide = NAME_IS_JP(n);
            continue;
        }
        /* a double-byte character (Shift-JIS, by the game's own rule) as a whole */
        if (_ismbblead(c) && s[i + 1] != '\0') {
            *o++ = s[i++];
            *o++ = s[i++];
            prevWide = 1;
        } else {
            *o++ = s[i++];
            prevWide = 0;
        }
    }
    *o = '\0';
}


/* The texts made, by their message's text: kept for as long as the game
 * runs (the game keeps pointers to them, a message window for one), on the
 * save-state heap with the table here in the save-state section. */
typedef struct LuigiText {
    struct LuigiText* next;
    char* in;
    char* out; /* NULL: the same as in */
    int chr;   /* the pick it was made for */
} LuigiText;
static LuigiText* sTexts[1024];

const char* portLuigiText(const char* msg) {
    unsigned int hash = 2166136261u;
    const unsigned char* p;
    LuigiText* t;
    size_t len;
    char* out;

    if (msg == NULL || portPlayAs() == PORT_PLAY_AS_MARIO) {
        return msg;
    }
    sTextChr = portPlayAs();
    /* the quick test: no name in it */
    {
        int n;
        for (n = 0; n < NAMES && strstr(msg, kNames[n].name) == NULL; n++) {
        }
        if (n == NAMES) {
            return msg;
        }
    }
    for (p = (const unsigned char*)msg; *p != '\0'; p++) {
        hash = (hash ^ *p) * 16777619u;
    }
    for (t = sTexts[hash % 1024]; t != NULL; t = t->next) {
        if (t->chr == sTextChr && !strcmp(t->in, msg)) {
            return t->out != NULL ? t->out : msg;
        }
    }
    len = strlen(msg);
    t = (LuigiText*)malloc(sizeof(LuigiText));
    out = (char*)malloc(len * 2 + 1);
    if (t == NULL || out == NULL) {
        free(t);
        free(out);
        return msg;
    }
    t->in = (char*)malloc(len + 1);
    if (t->in == NULL) {
        free(t);
        free(out);
        return msg;
    }
    memcpy(t->in, msg, len + 1);
    t->chr = sTextChr;
    substitute(msg, out);
    if (!strcmp(out, msg)) {
        free(out);
        t->out = NULL;
    } else {
        char* shrunk = (char*)realloc(out, strlen(out) + 1);
        t->out = shrunk != NULL ? shrunk : out;
    }
    t->next = sTexts[hash % 1024];
    sTexts[hash % 1024] = t;
    return t->out != NULL ? t->out : msg;
}

/* ----------------------------------------------------------------- sound */

int portLuigiSfx(int id) {
    /* Mario's field sounds on a plain floor and the pick's (Luigi's NPC's: the
     * same sounds, lower, as the game plays them for him; Peach's and
     * Bowser's interlude steps and landings, Bowser's jump) */
    static const char* const kMario[4] = {"SFX_MARIO_MOVE1L", "SFX_MARIO_MOVE1R", "SFX_MARIO_JUMP1",
                                          "SFX_MARIO_LANDING1"};
    static const char* const kSfx[PORT_PLAY_AS_COUNT][4] = {
        {NULL, NULL, NULL, NULL},
        {"SFX_FLD_NPC_LUIGI_MOVE1L", "SFX_FLD_NPC_LUIGI_MOVE1R", "SFX_FLD_NPC_LUIGI_JUMP1",
         "SFX_FLD_NPC_LUIGI_LANDING1"},
        {"SFX_PEACH_MOVE1L", "SFX_PEACH_MOVE1R", NULL, "SFX_PEACH_LANDING1"},
        {"SFX_KUPPA_MOVE1", "SFX_KUPPA_MOVE2", "SFX_KUPPA_JUMP1", "SFX_KUPPA_LANDING1"},
    };
    static int ids[PORT_PLAY_AS_COUNT][4];
    static int mario[4];
    static int ready;
    int i, c, chr = portPlayAs();

    if (chr == PORT_PLAY_AS_MARIO) {
        if (!portFunOn(PORT_FUN_LUIGI_VOICE)) {
            return id;
        }
        chr = PORT_PLAY_AS_LUIGI; /* (the fun toggle: Luigi's voice alone, on Mario) */
    }
    if (!ready) {
        for (i = 0; i < 4; i++) {
            mario[i] = searchPSSFXList(kMario[i]);
            for (c = 1; c < PORT_PLAY_AS_COUNT; c++) {
                ids[c][i] = kSfx[c][i] != NULL ? searchPSSFXList(kSfx[c][i]) : 0;
            }
        }
        ready = 1;
    }
    for (i = 0; i < 4; i++) {
        /* (0: a name the table doesn't have) */
        if (mario[i] != 0 && ids[chr][i] != 0 && (id & 0x1FFF) == mario[i]) {
            if (getenv("TTYD_LUIGI_TRACE") != NULL) {
                printf("[play-as] sound %s for %s\n", kSfx[chr][i], kMario[i]);
                fflush(stdout);
            }
            return (id & ~0x1FFF) | ids[chr][i];
        }
    }
    return id;
}

/* ------------------------------------------------------- NPCs, credits */

/* Mario and the pick swap in the scenes too: an NPC drawn with the pick's
 * model (Luigi in Mario's house, Rogueport, Poshley Heights, the digest...;
 * Peach in her scenes and the ending; Bowser in his) is drawn as Mario, and
 * one drawn with a_mario (the digest's and Doopliss's Mario) as the pick. So
 * is the player in Peach's or Bowser's interludes when they are the pick
 * (mario.c): Mario plays their part. As for the player, the NPC's own pose
 * runs as before and a stand-in follows it and is drawn instead. */

/* the models a stand-in uses: Mario's front, back and event sets, the pick's */
enum { NPC_A_MARIO, NPC_A_MARIO_R, NPC_E_MARIO, NPC_LUIGI, NPC_MODELS };

/* the pick the NPC stand-ins are for */
static int sNpcChr;

static const char* npcModel(int m) {
    static const char* const kModel[] = {"a_mario", "a_mario_r", "e_mario"};
    return m == NPC_LUIGI ? charModel(sNpcChr, 0) : kModel[m];
}

/* Luigi's animations (c_luigi) to Mario's: the same moves, his talk to
 * Mario's, his scenes' gestures to the closest of Mario's event poses (by
 * what the scripts use them for: L_5/L_8 the scene's stand, L_7/L_9 its talk,
 * L_6 walking up, L_11 the dash to the door, I_1 holding up the letter; the
 * story-telling ones to a nod, talking, thinking, a look up, resolve). The
 * back-facing R ones go to a_mario_r's versions. */
static const struct {
    const char* luigi;
    const char* mario;
    int model;
} kToMario[] = {
    {"S_1", "M_S_1", NPC_A_MARIO},  {"Z_1", "M_Z_1", NPC_A_MARIO},   {"W_1", "M_W_1", NPC_A_MARIO},
    {"R_1", "M_R_1", NPC_A_MARIO},  {"T_1", "M_I_2", NPC_A_MARIO},   {"J_1A", "M_J_1A", NPC_A_MARIO},
    {"J_1B", "M_J_1B", NPC_A_MARIO}, {"S_2", "M_S_1", NPC_A_MARIO},  {"Z_2", "M_Z_1", NPC_A_MARIO},
    {"T_2", "M_I_2", NPC_A_MARIO},  {"I_1", "M_I_1", NPC_A_MARIO},   {"L_5", "M_S_1", NPC_A_MARIO},
    {"L_6", "M_W_1", NPC_A_MARIO},  {"L_7", "M_I_2", NPC_A_MARIO},   {"L_8", "M_S_1", NPC_A_MARIO},
    {"L_9", "M_I_2", NPC_A_MARIO},  {"L_11", "M_R_1", NPC_A_MARIO},  {"L_1", "M_N_2", NPC_E_MARIO},
    {"L_2", "M_N_B", NPC_E_MARIO},  {"L_3", "M_I_5", NPC_E_MARIO},   {"L_4", "M_N_3", NPC_E_MARIO},
    {"L_10", "M_I_G", NPC_E_MARIO},
};

#define NPC_SLOTS 8
/* per NPC (by its pose, + 1: 0 none): the stand-in's poses per model (+ 1),
 * the one drawn and the animation it plays */
static struct {
    int source1;
    int pose1[NPC_MODELS];
    int cur;
    char anim[24];
} sNpc[NPC_SLOTS];

static int npcPoseValid(int pose, int m, int type) {
    AnimationPose* p;
    if (pose < 0) {
        return 0;
    }
    p = animPoseGetAnimPosePtr(pose);
    return (p->flags & ANIM_POSE_FLAG_USED) && p->typeFlags == 0 && p->type == type &&
           !strcmp(animPoseGetAnimBaseDataPtr(pose)->fileName, npcModel(m));
}

/* the pick's own models an NPC (or the interlude player) may be drawn with */
static int isPickModel(const char* file) {
    switch (sNpcChr) {
    case PORT_PLAY_AS_LUIGI:
        return !strcmp(file, "c_luigi");
    case PORT_PLAY_AS_PEACH:
        return !strcmp(file, "c_peach") || !strcmp(file, "c_peach_r") || !strcmp(file, "c_peach_f");
    case PORT_PLAY_AS_BOWSER:
        return !strcmp(file, "c_koopa") || !strcmp(file, "c_koopa2");
    }
    return 0;
}

/* whom an NPC's model swaps to: NPC_A_MARIO (it is the pick), NPC_LUIGI
 * (it is Mario), -1 (neither) */
static int npcSwap(int poseId) {
    const char* file = animPoseGetAnimBaseDataPtr(poseId)->fileName;
    if (isPickModel(file)) {
        return NPC_A_MARIO;
    }
    if (!strcmp(file, "a_mario")) {
        return NPC_LUIGI;
    }
    return -1;
}

static void npcFree(int s) {
    int m;
    for (m = 0; m < NPC_MODELS; m++) {
        if (sNpc[s].pose1[m] != 0 && sNpc[s].source1 != 0 &&
            npcPoseValid(sNpc[s].pose1[m] - 1, m, animPoseGetAnimPosePtr(sNpc[s].source1 - 1)->type)) {
            animPoseRelease(sNpc[s].pose1[m] - 1);
        }
        sNpc[s].pose1[m] = 0;
    }
    sNpc[s].source1 = 0;
    sNpc[s].anim[0] = '\0';
}

static void npcRelease(int poseId) {
    int s;
    for (s = 0; s < NPC_SLOTS; s++) {
        if (poseId >= 0 && sNpc[s].source1 == poseId + 1) {
            npcFree(s);
        }
    }
}

static int npcStandIn(int poseId) {
    int s, pose;
    if (poseId < 0 || portPlayAs() != sNpcChr || sNpcChr == PORT_PLAY_AS_MARIO ||
        animPoseGetAnimPosePtr(poseId)->paperId != -1) {
        return -1;
    }
    for (s = 0; s < NPC_SLOTS; s++) {
        if (sNpc[s].source1 == poseId + 1 && sNpc[s].pose1[sNpc[s].cur] != 0) {
            pose = sNpc[s].pose1[sNpc[s].cur] - 1;
            return npcPoseValid(pose, sNpc[s].cur, animPoseGetAnimPosePtr(poseId)->type) ? pose : -1;
        }
    }
    return -1;
}

/* Peach's or Bowser's animation (P_S_1, P_O_S_1, P_S_9F, KPA_W_1, KPA2_J_1A;
 * from c_peach_r when back) to Mario's: by its kind, the letter after the
 * name's prefix: S stand, Z idle, W walk, R run, T talk, J jump, D hurt; the
 * rest stand (Peach's dance, P_O_*, too) */
static const char* toMarioByKind(const char* anim, int back, int* model) {
    const char* k = strchr(anim, '_');
    char kind = k != NULL ? k[1] : 'S';
    *model = back ? NPC_A_MARIO_R : NPC_A_MARIO;
    switch (kind) {
    case 'Z':
        return back ? "M_Z_1R" : "M_Z_1";
    case 'W':
        return back ? "M_W_1R" : "M_W_1";
    case 'R':
        return back ? "M_R_1R" : "M_R_1";
    case 'T':
        return back ? "M_I_2R" : "M_I_2";
    case 'J':
        if (!back) {
            return anim[strlen(anim) - 1] == 'A' ? "M_J_1A" : "M_J_1B";
        }
        return "M_J_1BR";
    case 'D':
        if (!back) {
            return "M_D_1";
        }
        break;
    }
    return back ? "M_S_1R" : "M_S_1";
}

/* Mario's animation for the pick's anim (file: its model): *model and the name */
static const char* toMario(const char* anim, const char* file, int* model) {
    char base[24];
    size_t len = strlen(anim), i;
    int back = len > 1 && len < sizeof(base) && anim[len - 1] == 'R';
    static char name[24];

    if (sNpcChr != PORT_PLAY_AS_LUIGI) {
        return toMarioByKind(anim, !strcmp(file, "c_peach_r"), model);
    }
    snprintf(base, sizeof(base), "%s", anim);
    if (back) {
        base[len - 1] = '\0';
    }
    for (i = 0; i < sizeof(kToMario) / sizeof(kToMario[0]); i++) {
        if (!strcmp(kToMario[i].luigi, base)) {
            if (back) {
                /* a_mario_r has the plain moves' back views; the rest stand */
                *model = NPC_A_MARIO_R;
                snprintf(name, sizeof(name), "%sR", kToMario[i].model == NPC_A_MARIO ? kToMario[i].mario : "M_S_1");
                return name;
            }
            *model = kToMario[i].model;
            return kToMario[i].mario;
        }
    }
    *model = back ? NPC_A_MARIO_R : NPC_A_MARIO;
    return back ? "M_S_1R" : "M_S_1";
}

void portLuigiNpcUpdate(int poseId) {
    AnimationPose* src;
    AnimationPose* dst;
    const char* anim;
    const char* want;
    int s, swap, model, pose, type, back, chr = portPlayAs();

    if (poseId < 0) {
        return;
    }
    if (chr != sNpcChr) {
        /* (a new pick: every stand-in of the old one goes) */
        for (s = 0; s < NPC_SLOTS; s++) {
            if (sNpc[s].source1 != 0 && (animPoseGetAnimPosePtr(sNpc[s].source1 - 1)->flags & ANIM_POSE_FLAG_USED)) {
                npcFree(s);
            }
            memset(&sNpc[s], 0, sizeof(sNpc[s]));
        }
        sNpcChr = chr;
    }
    if (chr == PORT_PLAY_AS_MARIO) {
        npcRelease(poseId);
        return;
    }
    if ((swap = npcSwap(poseId)) < 0) {
        return;
    }
    src = animPoseGetAnimPosePtr(poseId);
    type = src->type;
    for (s = 0; s < NPC_SLOTS && sNpc[s].source1 != poseId + 1; s++) {
    }
    if (s == NPC_SLOTS) {
        /* a free slot, or one whose NPC is gone (a map change frees poses
         * without a release) */
        for (s = 0; s < NPC_SLOTS; s++) {
            int old = sNpc[s].source1 - 1;
            if (old < 0 || !(animPoseGetAnimPosePtr(old)->flags & ANIM_POSE_FLAG_USED) || npcSwap(old) < 0) {
                break;
            }
        }
        if (s == NPC_SLOTS) {
            return; /* (more swapped NPCs than slots: the rest as they are) */
        }
        memset(sNpc[s].pose1, 0, sizeof(sNpc[s].pose1));
        sNpc[s].source1 = poseId + 1;
        sNpc[s].anim[0] = '\0';
    }
    anim = animPoseGetCurrentAnim(poseId)->name;
    if (swap == NPC_A_MARIO) {
        want = toMario(anim, animPoseGetAnimBaseDataPtr(poseId)->fileName, &model);
    } else {
        model = NPC_LUIGI;
        want = NULL;
    }
load:
    pose = sNpc[s].pose1[model] - 1;
    if (!npcPoseValid(pose, model, type)) {
        pose = animPoseEntry(npcModel(model), type);
        sNpc[s].pose1[model] = pose >= 0 ? pose + 1 : 0;
        sNpc[s].anim[0] = '\0';
        if (pose < 0) {
            return;
        }
    }
    if (swap == NPC_LUIGI) {
        want = charAnim(chr, PORT_LUIGI_FIELD, pose, -1, anim, &back);
        if (want == NULL || back) {
            want = kChars[chr].front.stand;
        }
    }
    if (sNpc[s].cur != model || strcmp(sNpc[s].anim, want) != 0) {
        /* (a name the model lacks: its stand) */
        if (findAnim(animPoseGetAnimBaseDataPtr(pose), want) == NULL) {
            if (model == NPC_E_MARIO) {
                model = NPC_A_MARIO; /* (e_mario has no stand) */
                want = "M_S_1";
                goto load;
            }
            want = model == NPC_LUIGI ? kChars[chr].front.stand : model == NPC_A_MARIO_R ? "M_S_1R" : "M_S_1";
        }
        if (getenv("TTYD_LUIGI_TRACE") != NULL) {
            printf("[play-as] npc %d: %s as %s %s\n", poseId, anim, npcModel(model), want);
            fflush(stdout);
        }
        animPoseSetAnim(pose, want, TRUE);
        snprintf(sNpc[s].anim, sizeof(sNpc[s].anim), "%s", want);
        sNpc[s].cur = model;
    }
    dst = animPoseGetAnimPosePtr(pose);
    dst->localTimeRate = src->localTimeRate;
    animPoseMain(pose);
    dst->flags = (dst->flags & ~(ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN)) |
                 (src->flags & (ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN));
    dst->peraRotY = src->peraRotY;
    dst->rotationY = src->rotationY;
    dst->peraTargetRotY = src->peraTargetRotY;
    dst->scaleZFlip = src->scaleZFlip;
}

void portLuigiCreditActor(const char** model, const char** anim) {
    int chr = portPlayAs();
    if (chr == PORT_PLAY_AS_MARIO) {
        return;
    }
    sNpcChr = chr;
    if (!strcmp(*model, "a_mario")) {
        *model = charModel(chr, 0);
        *anim = kChars[chr].front.walk;
    } else if (isPickModel(*model)) {
        *model = "a_mario";
        *anim = "M_W_1";
    }
}
