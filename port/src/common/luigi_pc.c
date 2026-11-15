/* Play as Luigi (port/include/port/luigi.h, docs/PLAYABLE_LUIGI.md): a
 * stand-in pose of Luigi's field model for Mario's, in the field and for
 * Mario's unit in battle. The state is in the save-state section
 * (CMakeLists.txt), so a save state keeps the pose ids it points at; each
 * frame still checks the pose is the c_luigi this file loaded. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/luigi.h"
#include "port/brothers.h"
#include "port/costume.h"
#include "port/fire_breath.h"
#include "port/gameplus.h"
#include "port/paper.h"
#include "port/pm64.h"
#include "port/pm64_enemy.h"
#include "port/pm64_media.h"
#include "port/text_language.h"
#include "port/toggles.h"
#include "port/tr.h"
#include "port/versus.h"
#include "version.h"
#include "driver/animdrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include <dolphin/os.h>
#include "party.h"
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

/* Bowser (c_koopa, his 3D interlude set). Where our pack (c_koopa_pc) has
 * one of these names it wins; the rest are his disc poses that fit (getting
 * up, keeling over, lying dazed, asleep). The disc has no back view of him:
 * ours, c_koopa_r_pc, is his own parts as seen from behind, made over
 * c_koopa (tools/port/bowser_back.py), so his disc moves play on it too. */
static const AnimMap kBowserMap[] = {
    {"M_S_1", "KPA_S_1", 0},   {"M_Z_1", "KPA_Z_1", 0},   {"M_W_1", "KPA_W_1", 0},   {"M_R_1", "KPA_R_1", 0},
    {"M_J_1A", "KPA_J_1A", 0}, {"M_J_1B", "KPA_J_1B", 0}, {"M_J_1C", "KPA_J_1B", 0}, {"M_I_2", "KPA_T_1", 0},
    {"M_I_3", "KPA_T_1", 0},   {"M_D_1", "KPA_D_1", 0},   {"M_D_2", "KPA_D_2", 0},   {"M_D_6", "KPA_D_1", 0},
    {"M_H_1", "KPA_A_1B", 0},  {"M_H_4", "KPA_A_1B", 0},  {"M_H_7", "KPA_A_1B", 0},  {"M_V_1", "KPA_I_1", 0},
    {"M_V_2", "KPA_X_1", 0},   {"M_X_1", "KPA_X_1", 0},   {"M_U_3", "KPA_U_1", 0},   {"M_D_8", "KPA_D_2", 0},
    {"M_D_7", "KPA_D_4", 0},   {"M_Z_10", "KPA_Q_1", 0},  {"M_Z_11", "KPA_N_2", 0},  {"M_I_6", "KPA_N_2", 0},
    {"M_H_16", "KPA_U_1", 0},  {"M_S_1R", "KPA_S_1", 1}, {"M_Z_1R", "KPA_Z_1", 1},  {"M_W_1R", "KPA_W_1", 1},
    {"M_R_1R", "KPA_R_1", 1},  {"M_J_1BR", "KPA_J_1B", 1}, {"M_J_1CR", "KPA_J_1B", 1}, {"M_I_2R", "KPA_T_1", 1},
    {"M_I_3R", "KPA_T_1", 1},
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
    const char* pcBack;  /* ours of it, made at startup with our animations (NULL: none; with no
                          * back of the disc's, the back view is ours alone: Bowser's) */
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
    {"c_koopa", "c_koopa_pc", NULL, "c_koopa_r_pc", kBowserMap, COUNT(kBowserMap), 0.7f, 0.5f, 1,
     {"KPA_Z_1", "KPA_S_1", "KPA_W_1", "KPA_R_1", "KPA_T_1", "KPA_J_1A", "KPA_J_1B", "KPA_D_1", "KPA_A_1B"},
     {"KPA_Z_1", "KPA_S_1", "KPA_W_1", "KPA_R_1", "KPA_T_1", "KPA_J_1A", "KPA_J_1B", "KPA_D_1", "KPA_A_1B"}},
};

/* Bowser is never shown from behind: his back view walking away looked wrong, so Mario's back-facing
 * animations (M_W_1R, M_R_1R, M_S_1R, ...) play as his front ones (M_W_1, ...), in the field, co-op and
 * online ghosts alike (c_koopa_r_pc is still made: the character export has it) */
static int frontOnly(int chr) {
    return chr == PORT_PLAY_AS_BOWSER;
}

/* the character has a back view: the disc's, or ours made at startup */
static int hasBack(int chr) {
    const PlayChar* c = &kChars[chr];
    if (frontOnly(chr)) {
        return 0;
    }
    return c->back != NULL || (c->pcBack != NULL && sModelReady[chr][1]);
}

/* the front model (0) or the back one (1) a character's stand-in loads */
static const char* charModel(int chr, int back) {
    const PlayChar* c = &kChars[chr];
    if (back && hasBack(chr)) {
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
    /* Brothers mode: Mario and Luigi as themselves, so no pick while it's on */
    if (portBrothersSetting()) {
        return PORT_PLAY_AS_MARIO;
    }
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

/* $TTYD_LUIGI_TRACE (docs/LUIGI_ANIMS.md): the stand-ins and sounds in the log */
static int trace(void) {
    static int sTrace = -1;
    if (sTrace < 0) {
        sTrace = getenv("TTYD_LUIGI_TRACE") != NULL;
    }
    return sTrace;
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
    char front[32];
    int i;
    *back = 0;
    if (mario == NULL || chr <= 0) {
        return NULL;
    }
    if (frontOnly(chr) && isBack(mario) && strlen(mario) < sizeof(front)) {
        /* (his front view: Mario's animation of the same name without its R) */
        snprintf(front, sizeof(front), "%.*s", (int)strlen(mario) - 1, mario);
        mario = front;
    }
    /* a back-facing animation of Mario's: the back model's version (ours in
     * c_peach_r_pc) */
    if (backPose >= 0 && isBack(mario) &&
        (found = portCosmeticFindAnim(animPoseGetAnimBaseDataPtr(backPose), mario)) != NULL) {
        *back = 1;
        return found;
    }
    if (pose >= 0) {
        d = animPoseGetAnimBaseDataPtr(pose);
        if (where == PORT_LUIGI_BATTLE || where == PORT_LUIGI_VERSUS) {
            snprintf(battle, sizeof(battle), "%s@b", mario);
            if ((found = portCosmeticFindAnim(d, battle)) != NULL) {
                return found;
            }
            for (i = 0; i < COUNT(kSameInBattle); i++) {
                if (!strcmp(kSameInBattle[i], mario) && (found = portCosmeticFindAnim(d, mario)) != NULL) {
                    return found;
                }
            }
        } else if ((found = portCosmeticFindAnim(d, mario)) != NULL) {
            return found;
        }
    }
    for (i = 0; i < c->mapCount; i++) {
        if (!strcmp(c->map[i].mario, mario)) {
            *back = c->map[i].back && hasBack(chr);
            return c->map[i].to;
        }
    }
    if (!c->always || !strcmp(mario, "M_H_12")) {
        return NULL; /* (M_H_12: only the thrown hammer, drawn as the pick's by thrownDraw) */
    }
    if (isBack(mario)) {
        *back = hasBack(chr);
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

/* The safety net: anim (charAnim's pick for Mario's animation mario) is in
 * the stand-in model that will play it, else a move it has: a name the model
 * lacks (a map entry or a pack made for another disc's model) would play the
 * model's first animation, whatever it is. Peach and Bowser take the plain
 * move of Mario's kind, then their stand; Luigi, Mario's own model (NULL), as
 * for any animation of Mario's he has none of. One log line per name. Draw
 * only: the game's waits are on Mario's own pose, which runs as before. */
static const char* haveAnim(int chr, int where, int back, const char* mario, const char* anim) {
    static char logged[32][40];
    static int nLogged;
    const PlayChar* c = &kChars[chr];
    const int pose = sStandIn[where].pose[back];
    AnimationPoseData* d;
    const char* alt;
    int i;
    if (anim == NULL || pose < 0 || mario == NULL ||
        portCosmeticFindAnim(d = animPoseGetAnimBaseDataPtr(pose), anim) != NULL) {
        return anim;
    }
    alt = NULL;
    if (c->always) {
        alt = plainMove(back ? &c->rear : &c->front, mario);
        if (portCosmeticFindAnim(d, alt) == NULL) {
            alt = back ? c->rear.stand : c->front.stand;
        }
        if (portCosmeticFindAnim(d, alt) == NULL) {
            alt = NULL; /* (nothing of theirs at all: Mario's own model) */
        }
    }
    for (i = 0; i < nLogged && strcmp(logged[i], anim) != 0; i++) {
    }
    if (i == nLogged && nLogged < 32) {
        snprintf(logged[nLogged++], sizeof(logged[0]), "%s", anim);
        printf("[play-as] %s has no %s (for Mario's %s): %s instead\n", charModel(chr, back), anim, mario,
               alt != NULL ? alt : "Mario's own");
        fflush(stdout);
    }
    return alt;
}

/* The stand-in's clock (pose, playing anim) for Mario's pose src, before it
 * is stepped (tools/port/anim_parity.py checks the lengths it relies on).
 * The game sets Mario's time to hold or scrub a pose (the spin hammer's
 * charge, events), so every stand-in runs on Mario's clock. Ours (M_ names)
 * have Mario's length and loop. A disc animation of another length is fitted
 * to Mario's: where his loops, it loops on his clock; where his ends, it is
 * scaled to end on the frame his does (a loop of its own in whole turns,
 * ending as it starts) and holds there, so it is never still playing when
 * his has ended and the scene goes on. */
/* ours is Mario's animation of its name (its battle one "<name>@b" too) */
static int sameName(const char* ours, const char* mario) {
    const size_t n = mario != NULL ? strlen(mario) : 0;
    return mario != NULL && !strncmp(ours, mario, n) && (ours[n] == '\0' || ours[n] == '@');
}

static void standInClock(int pose, int src, const char* anim) {
    AnimationPose* d = animPoseGetAnimPosePtr(pose);
    AnimationPose* s = animPoseGetAnimPosePtr(src);
    const AnimData* m;
    const AnimData* o;
    float mLen, oLen, p, f;
    OSTime ms;
    int turns;

    d->localTimeRate = s->localTimeRate;
    if (d->type != s->type) {
        return;
    }
    d->localTime = s->localTime;
    m = animPoseGetCurrentAnim(src)->data;
    o = animPoseGetCurrentAnim(pose)->data;
    /* (ours of Mario's own name share his clock; one of another name, Bowser's front M_W_1 for Mario's
       M_W_1R (frontOnly), is fitted as a disc one) */
    if ((!strncmp(anim, "M_", 2) && sameName(anim, animPoseGetCurrentAnim(src)->name)) || m == NULL || o == NULL ||
        m->loopInfoCount == 0 || o->loopInfoCount == 0 || *(const s32*)&m->loopInfo[0] != 0) {
        return;
    }
    /* Mario's one-shot: how far through it he is now (after his step this frame) */
    mLen = m->loopInfo[2] - m->loopInfo[1];
    p = mLen > 0.0f ? (s->rawFrame - m->loopInfo[1]) / mLen : 1.0f;
    p = p < 0.0f ? 0.0f : p > 1.0f ? 1.0f : p;
    oLen = o->loopInfo[2] - o->loopInfo[1];
    if (*(const s32*)&o->loopInfo[0] == 0) {
        /* (past the end at the end: held on its last frame) */
        f = p >= 1.0f ? o->loopInfo[2] + 1.0f : o->loopInfo[1] + p * oLen;
    } else if (oLen > 0.0f) {
        turns = (int)(mLen / oLen + 0.5f);
        turns = turns < 1 ? 1 : turns;
        f = p >= 1.0f ? o->loopInfo[1] : o->loopInfo[1] + p * (float)turns * oLen;
    } else {
        f = o->loopInfo[1];
    }
    f = f < 0.0f ? 0.0f : f;
    /* that frame now: animPoseMain's frame is 60 * (now - localTime) / 1000 * rate */
    ms = (OSTime)OSTicksToMilliseconds(d->type == 0 ? gp->renderFieldTime : gp->renderTime);
    d->localTimeRate = 1.0f;
    d->localTime = ms - (OSTime)(f * (1000.0f / 60.0f));
}

void portLuigiUpdate(int where, int poseId, int allowed) {
    AnimationPose* src;
    AnimationPose* dst;
    const char* anim = NULL;
    int back = 0, b, pose;
    float ownFrame = -1.0f;
    int own = 0; /* Bowser's breath (port/include/port/fire_breath.h): his pose, on its own clock */
    /* (versus: Luigi's side's unit is always Luigi) */
    int chr = where == PORT_LUIGI_VERSUS ? PORT_PLAY_AS_LUIGI : portPlayAs();

    /* the menus' figures: Mario's own model only (not 8-bit Mario's d_mario,
     * whose animations have names of Luigi's); not versus's battle unit
     * (b_mario), whose slot comes after the menus' */
    if ((where == PORT_LUIGI_MENU || where == PORT_LUIGI_LOG) && poseId >= 0 &&
        strncmp(animPoseGetAnimBaseDataPtr(poseId)->fileName, "a_mario", 7) != 0) {
        allowed = 0;
    }
    /* his costume follows this pose (whoever stands in for him wears it: costume_pc.c fits it to them) */
    portCostumeNoteMario(where, allowed && where != PORT_LUIGI_VERSUS ? poseId : -1);
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
            if (mario != NULL && isBack(mario) && hasBack(chr) && sStandIn[where].pose[1] < 0) {
                sStandIn[where].pose[1] = animPoseEntry(charModel(chr, 1), poolType(where));
                sStandIn[where].anim[0] = '\0';
                if (sStandIn[where].pose[1] < 0) {
                    sStandIn[where].pose[1] = -1;
                }
            }
            anim = charAnim(chr, where, sStandIn[where].pose[0], sStandIn[where].pose[1], mario, &back);
            if (chr == PORT_PLAY_AS_BOWSER && where != PORT_LUIGI_VERSUS) {
                const char* breath = portFireBreathPose(where, mario, &ownFrame);
                if (breath != NULL) {
                    /* always his front view, in profile, also when Mario swings facing away (M_H_1R): from
                     * behind, his back view hid his snout and the flame seemed to come out of his shell */
                    back = 0;
                    anim = breath;
                    own = 1;
                }
            }
        }
        if (anim != NULL && back && sStandIn[where].pose[1] < 0) {
            sStandIn[where].pose[1] = animPoseEntry(charModel(chr, 1), poolType(where));
            sStandIn[where].anim[0] = '\0';
            if (sStandIn[where].pose[1] < 0) {
                sStandIn[where].pose[1] = -1;
            }
        }
        anim = haveAnim(chr, where, back, animPoseGetCurrentAnim(poseId)->name, anim);
        if (anim != NULL && chr == PORT_PLAY_AS_PEACH) {
            /* Peach floating: both hands on her open parasol (peach_parasol_pc.c), where her model has it */
            const char* fl = portParasolAnim(where, back);
            if (fl != NULL && sStandIn[where].pose[back] >= 0 &&
                portCosmeticFindAnim(animPoseGetAnimBaseDataPtr(sStandIn[where].pose[back]), fl) != NULL) {
                anim = fl;
            }
        }
        if (trace()) {
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
    if (own) {
        /* the breath's frame (the field's swing), or on from its start at Mario's rate (battle) */
        dst->localTimeRate = ownFrame >= 0.0f ? 1.0f : src->localTimeRate;
        if (ownFrame >= 0.0f) {
            animPoseSetLocalTime(pose, ownFrame);
        }
    } else {
        standInClock(pose, poseId, anim);
    }
    animPoseMain(pose);
    portParasolPose(pose, back, where, animPoseGetCurrentAnim(poseId)->name); /* Peach floats with it open */
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

/* The paper turn a stand-in's draw stepped, onto the pose it stands in for.
 * The turn is stepped only where a pose is drawn (animPoseDrawMtx), and the
 * game waits on the source's (animPoseGetPeraEnd: evt_npc_wait_pera, the
 * NPCs' wandering): without this an NPC drawn as someone else (Luigi's NPC
 * drawn as Mario, a Peach NPC while Peach is the pick, a Mario NPC drawn as
 * the pick) had its turn reset every frame to the NPC's never-stepped one, so
 * the stand-in barely turned and the waits for it ended at once. As a
 * partner's skin does. */
static void turnBack(AnimationPose* own, const AnimationPose* in) {
    own->rotationY = in->rotationY;
    own->peraRotY = in->peraRotY;
    own->peraTargetRotY = in->peraTargetRotY;
    own->scaleZFlip = in->scaleZFlip;
}

/* ------------------------------------------------- Hammer Throw's hammer
 *
 * The thrown hammer is Mario's unit's part 3: Mario's own model in an
 * animation that shows only the hammer (M_H_4, M_H_8, M_H_12 by rank), spun
 * and flown by the badge's script. For Luigi it stays Mario's hammer; for
 * Bowser it isn't drawn (his fireball is, fire_breath_pc.c); for Peach her
 * closed parasol is drawn in its place, spinning along the same arc, in the
 * hammer rank's colours. Draw only: the part's flight, hit and timing are
 * the badge's. */

/* the hammer's middle over the part's place: where the script spins it about (marioAttackEvent_HammerRotate) */
#define THROWN_MID 12.0f
/* the middle of Peach's parasol card along its handle, in her hammer plane (luigi_pack.py hammer_records with
 * PARASOL_CARD: 1.6 - 24/136 and 1.6 + 112/136 of its 26.35 height) */
#define PARASOL_CARD_MID 10.13f
/* a little bigger than in her hand: closed, it is a thin thing, and in flight it reads as the hammer does */
#define PARASOL_THROWN 1.3f
#define THROWN_GROUPS 256

static struct {
    int part;   /* the part's pose (-1 none) */
    int pose;   /* Peach's model for the parasol (-1 none) */
    int group;  /* its Hammer_pc group */
    int logged; /* the pick the log line was for */
    Mtx world;  /* the parasol's own matrix this draw */
} sThrown = {-1, -1, -1, -1};
static unsigned char sThrownMask[THROWN_GROUPS];

static int thrownAnim(int poseId) {
    const AnimTableEntry* a = poseId >= 0 ? animPoseGetCurrentAnim(poseId) : NULL;
    return a != NULL && a->name != NULL &&
           (!strcmp(a->name, "M_H_4") || !strcmp(a->name, "M_H_8") || !strcmp(a->name, "M_H_12"));
}

void portPlayAsThrown(int poseId, int shown, float x, float y, float z) {
    const int chr = portPlayAs();
    sThrown.part = poseId;
    shown = shown && !portVersusOn() && thrownAnim(poseId);
    portFireBreathThrown(shown, x, y + THROWN_MID, z); /* (Bowser's fireball) */
    if (!shown) {
        sThrown.logged = -1;
    } else if (sThrown.logged != chr) {
        sThrown.logged = chr;
        printf("[play-as] hammer throw: %s (%s)\n",
               chr == PORT_PLAY_AS_PEACH && portPlayAsParasol() ? "Peach's parasol"
               : chr == PORT_PLAY_AS_BOWSER                     ? "Bowser's fireball"
                                                                : "Mario's hammer",
               animPoseGetCurrentAnim(poseId)->name);
        fflush(stdout);
    }
}

void portPlayAsThrownBattleStart(void) {
    int where, b;
    sThrown.part = -1;
    sThrown.pose = -1;
    sThrown.group = -1;
    /* (the battle stand-ins' poses went with the same pool: forgotten too, not released) */
    for (where = 0; where < SLOTS; where++) {
        if (poolType(where) != poolType(PORT_LUIGI_BATTLE)) {
            continue;
        }
        for (b = 0; b < 2; b++) {
            sStandIn[where].pose[b] = -1;
        }
        sStandIn[where].source = -1;
        sStandIn[where].anim[0] = '\0';
    }
}

/* pose is one of the stand-ins' (not the thrown parasol's own) */
static int thrownIsStandIn(int pose) {
    int where, b;
    for (where = 0; where < SLOTS; where++) {
        for (b = 0; b < 2; b++) {
            if (sStandIn[where].pose[b] == pose) {
                return 1;
            }
        }
    }
    return 0;
}

/* (animdrv's per-group callback: the parasol's group drawn where the hammer is, not in her hand) */
static void thrownGroupMtx(void* user, s32 groupId, Mtx* m) {
    (void)user;
    if (groupId == sThrown.group) {
        PSMTXCopy(sThrown.world, *m);
    }
}

/* Peach's closed parasol in the thrown hammer's place: her model drawing only its Hammer_pc plane, that
 * plane put on the hammer's spin (1: drawn) */
static int thrownParasol(int poseId, Mtx mtx, int xluMode) {
    const PlayChar* pc = &kChars[PORT_PLAY_AS_PEACH];
    AnimationPoseData* d;
    AnimationPose* p;
    int pose = sThrown.pose, g, tier, back = 0;
    Mtx t;
    if (!portPlayAsParasol()) {
        return 0;
    }
    /* (her own pose only: a stand-in's pose has the same model and pool, and may hold a past battle's id) */
    if (!poseValid(PORT_LUIGI_BATTLE, PORT_PLAY_AS_PEACH, 0, pose) || thrownIsStandIn(pose)) {
        const char* anim;
        pose = sThrown.pose = animPoseEntry(charModel(PORT_PLAY_AS_PEACH, 0), poolType(PORT_LUIGI_BATTLE));
        sThrown.group = -1;
        if (pose < 0) {
            sThrown.pose = -1;
            return 0;
        }
        /* (any pose of hers with the parasol out: its plane is moved, the rest isn't drawn) */
        anim = charAnim(PORT_PLAY_AS_PEACH, PORT_LUIGI_BATTLE, pose, -1, "M_H_1C", &back);
        if (anim == NULL || portCosmeticFindAnim(animPoseGetAnimBaseDataPtr(pose), anim) == NULL) {
            return 0;
        }
        animPoseSetAnim(pose, anim, TRUE);
        sThrown.group = animPoseGetGroupIdx(pose, "Hammer_pc");
    }
    d = animPoseGetAnimBaseDataPtr(pose);
    if (sThrown.group < 0 || d == NULL || d->groupCount > THROWN_GROUPS) {
        return 0;
    }
    animPoseMain(pose);
    p = animPoseGetAnimPosePtr(pose);
    {
        const AnimPoseGroup* gr = &d->groups[sThrown.group];
        p->groupVisibility[gr->visibilityId] = 1;
        /* the closed parasol of the hammer she has (the twelve closed: four turns each, the first) */
        tier = pouchGetHammerLv();
        tier = tier < 1 ? 1 : tier > 3 ? 3 : tier;
        if (gr->shapeId >= 0 && gr->shapeId < d->shapeCount) {
            const AnimPoseShape* sh = &d->shapes[gr->shapeId];
            if (sh->shapeDrawStart >= 0 && sh->shapeDrawStart < d->drawCount) {
                const int slot = d->draws[sh->shapeDrawStart].texDataIds[0];
                if (slot >= 0 && slot < d->texDataCount) {
                    p->texAnimEntries[slot].texBindIdx = (u8)((tier - 1) * 4);
                }
            }
        }
    }
    /* on the hammer's spin: its middle where the hammer's is, at her size, the card's middle there */
    PSMTXTrans(t, 0.0f, THROWN_MID, 0.0f);
    PSMTXConcat(mtx, t, sThrown.world);
    PSMTXScale(t, pc->scale * PARASOL_THROWN, pc->scale * PARASOL_THROWN, pc->scale * PARASOL_THROWN);
    PSMTXConcat(sThrown.world, t, sThrown.world);
    PSMTXTrans(t, 0.0f, -PARASOL_CARD_MID, 0.0f);
    PSMTXConcat(sThrown.world, t, sThrown.world);
    for (g = 0; g < d->groupCount; g++) {
        sThrownMask[g] = g == sThrown.group;
    }
    copyMaterial(p, animPoseGetAnimPosePtr(poseId));
    animPoseSetDispCallBack(pose, (void*)thrownGroupMtx, NULL);
    gPortAnimShapeMask = sThrownMask;
    animPoseDrawMtx(pose, mtx, xluMode, 0.0f, 1.0f);
    gPortAnimShapeMask = NULL;
    return 1;
}

/* the thrown hammer drawn as the pick's (1: done, nothing more to draw) */
static int thrownDraw(int poseId, Mtx mtx, int xluMode) {
    if (poseId < 0 || poseId != sThrown.part || portVersusOn() || !thrownAnim(poseId)) {
        return 0;
    }
    switch (portPlayAs()) {
        case PORT_PLAY_AS_BOWSER:
            return 1; /* (his fireball flies in its place) */
        case PORT_PLAY_AS_PEACH:
            return thrownParasol(poseId, mtx, xluMode);
        default:
            return 0;
    }
}

/* paper shadows and paper physics (port/include/port/paper.h) around the draw */
void portLuigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale) {
    Mtx moved;
    if (portPm64BowVeilDefer(poseId, mtx, xluMode, rotY, scale)) {
        return; /* (Mario see-through under Bow: drawn with the translucent things, pm64_field_pc.c) */
    }
    if (poseId >= 0 && sStandIn[PORT_LUIGI_BATTLE].source == poseId) {
        /* Bowser breathing in a hammer move's place: drawn back at his breath's distance from the one
         * struck (port/include/port/fire_breath.h), his paper shadow with him; his unit stays where the
         * hammer put it */
        const float dx = portFireBreathStandOff();
        const float dy = portFireBreathLift(); /* (and up in his stomp) */
        if (dx != 0.0f || dy != 0.0f) {
            Mtx t;
            PSMTXTrans(t, dx, dy, 0.0f);
            PSMTXConcat(t, mtx, moved);
            mtx = moved;
        }
    }
    portPaperDrawMtx(poseId, mtx, xluMode, rotY, scale, luigiDrawMtx);
}

/* Bobbery held overhead (party_bomhei.c) sits 36 units over the player's
 * feet, on Mario's hands; Bowser's reach higher, so while Bowser is the pick
 * Bobbery is drawn that much higher, eased up and down as he's lifted and
 * thrown. Draw-only: his place in the game is unchanged. */
#define BOBBERY_LIFT 13.0f
static float sBobberyLift;

static float bobberyLift(int poseId, int xluMode) {
    const PartyEntry* p = partyGetPtr(0);
    const MarioWork* mario = marioGetPtr();
    float want = 0.0f;
    if (p == NULL || poseId < 0 || p->paperAnimGroup != poseId || p->currentMemberId != PARTY_MEMBER_BOBBERY) {
        return 0.0f;
    }
    if (portPlayAs() == PORT_PLAY_AS_BOWSER && mario != NULL && (mario->flags & MARIO_FLAG_IS_CARRYING_BOBBERY)) {
        want = BOBBERY_LIFT;
    }
    /* 2 units a frame, stepped at the first of his two draws a frame (party.c: xlu 2, then 1) */
    if (xluMode == 2 && sBobberyLift < want) {
        sBobberyLift = sBobberyLift + 2.0f < want ? sBobberyLift + 2.0f : want;
    } else if (xluMode == 2 && sBobberyLift > want) {
        sBobberyLift = sBobberyLift - 2.0f > want ? sBobberyLift - 2.0f : want;
    }
    return sBobberyLift;
}

static void luigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale) {
    AnimationPose* src;
    int where, pose, npc;
    Mtx lifted;
    const float lift = bobberyLift(poseId, xluMode);

    if (lift != 0.0f) {
        Mtx t;
        PSMTXTrans(t, 0.0f, lift, 0.0f);
        PSMTXConcat(t, mtx, lifted);
        mtx = lifted;
    }

    if (thrownDraw(poseId, mtx, xluMode)) {
        return; /* Hammer Throw's hammer as Peach's parasol or Bowser's fireball */
    }
    if (portPm64DrawPose(poseId, mtx, xluMode, rotY, scale) || portPm64EnemyDrawPose(poseId, mtx, xluMode, rotY, scale) ||
        portPm64MediaDrawPose(poseId, mtx, xluMode, rotY, scale)) {
        /* a Paper Mario 64 partner in a partner's place (port/include/port/pm64.h), an enemy in its host's
           (pm64_enemy.h), Rowf, a field boss, the Star Spirits, Mario's PM64 look (pm64_media.h) */
        portPm64NoteDrawn(poseId);
        return;
    }
    portPm64HostDrawCheck(poseId, xluMode); /* (none of those: never one that was, a moment ago) */
    if ((npc = npcStandIn(poseId)) >= 0) {
        copyMaterial(animPoseGetAnimPosePtr(npc), animPoseGetAnimPosePtr(poseId));
        animPoseDrawMtx(npc, mtx, xluMode, rotY, scale);
        turnBack(animPoseGetAnimPosePtr(poseId), animPoseGetAnimPosePtr(npc));
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
                portCostumeDraw(pose, poseId, m, xluMode, rotY, scale); /* the pick wears it too */
            } else {
                animPoseDrawMtx(pose, mtx, xluMode, rotY, scale);
                portCostumeDraw(pose, poseId, mtx, xluMode, rotY, scale);
            }
            turnBack(src, animPoseGetAnimPosePtr(pose));
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
    portPm64ModelRelease(poseId);
    portPm64NoteReleased(poseId);
    for (where = PORT_LUIGI_MENU; where <= PORT_LUIGI_LOG; where++) {
        if (poseId >= 0 && sStandIn[where].source == poseId) {
            slotFree(where);
        }
    }
    animPoseRelease(poseId);
}

/* ------------------------------------------------------------------ text */

/* Mario's and the pick's names in each spelling, swapped both ways in one
 * pass (so a name put in is never swapped back): the name, the letter a
 * shout draws out ("MARIOOO!", "LUIGIII!"). Entries 2k and 2k + 1 are Mario
 * and the pick; the Japanese ones in Shift-JIS (the game's text) and in UTF-8
 * (the port's settings page). Mario and Peach trade places (so "Princess
 * Peach" is "Princess Mario": he is the one held captive), as do Mario and
 * Bowser. */
typedef struct {
    const char* name;
    char stretch;
} PlayName;
#define NAMES 6
#define JP_MARIO_SJIS "\x83\x7D\x83\x8A\x83\x49" /* マリオ */
#define JP_MARIO_UTF8 "\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA"
static const PlayName kLatinNames[PORT_PLAY_AS_COUNT][4] = {
    {{"Mario", 'o'}, {"Mario", 'o'}, {"MARIO", 'O'}, {"MARIO", 'O'}},
    {{"Mario", 'o'}, {"Luigi", 'i'}, {"MARIO", 'O'}, {"LUIGI", 'I'}},
    {{"Mario", 'o'}, {"Peach", 'h'}, {"MARIO", 'O'}, {"PEACH", 'H'}},
    {{"Mario", 'o'}, {"Bowser", 'r'}, {"MARIO", 'O'}, {"BOWSER", 'R'}},
};
/* the pick's Japanese name: [chr][0] Shift-JIS, [chr][1] UTF-8 */
static const char* const kJpNames[PORT_PLAY_AS_COUNT][2] = {
    {JP_MARIO_SJIS, JP_MARIO_UTF8},
    {"\x83\x8B\x83\x43\x81\x5B\x83\x57", "\xE3\x83\xAB\xE3\x82\xA4\xE3\x83\xBC\xE3\x82\xB8"}, /* ルイージ */
    {"\x83\x73\x81\x5B\x83\x60", "\xE3\x83\x94\xE3\x83\xBC\xE3\x83\x81"},                     /* ピーチ */
    {"\x83\x4E\x83\x62\x83\x70", "\xE3\x82\xAF\xE3\x83\x83\xE3\x83\x91"},                     /* クッパ */
};
/* the Japanese words a name is part of, per encoding: before Mario
 * (ペーパーマリオ), after Mario (マリオブラザーズ, マリオストーリー, マリオシリーズ,
 * マリオネット), after Luigi (ルイージマンション), before Peach (スピーチ, a speech) */
static const char* const kJpPaper[2] = {"\x83\x79\x81\x5B\x83\x70\x81\x5B",
                                        "\xE3\x83\x9A\xE3\x83\xBC\xE3\x83\x91\xE3\x83\xBC"};
static const char* const kJpMarioNotAfter[2][4] = {
    {"\x83\x75\x83\x89", "\x83\x58\x83\x67", "\x83\x56\x83\x8A", "\x83\x6C\x83\x62"},
    {"\xE3\x83\x96\xE3\x83\xA9", "\xE3\x82\xB9\xE3\x83\x88", "\xE3\x82\xB7\xE3\x83\xAA", "\xE3\x83\x8D\xE3\x83\x83"},
};
static const char* const kJpMansion[2] = {"\x83\x7D\x83\x93\x83\x56\x83\x87\x83\x93",
                                          "\xE3\x83\x9E\xE3\x83\xB3\xE3\x82\xB7\xE3\x83\xA7\xE3\x83\xB3"};
static const char* const kJpSu[2] = {"\x83\x58", "\xE3\x82\xB9"};

/* one swap's settings: the pick, the text's encoding, its language (for the pronouns: LANG_* below) */
typedef struct {
    int chr;
    int enc; /* PORT_TEXT_GAME, _SJIS, _LATIN1, _UTF8 */
    int lang;
    const char* tag; /* the game's message's tag (its own words: kPhrases), or NULL */
} Swap;

static const char* nameOf(const Swap* w, int n) {
    if (n < 4) {
        return kLatinNames[w->chr][n].name;
    }
    if (w->enc == PORT_TEXT_LATIN1) {
        return NULL; /* (no Japanese in Latin-1) */
    }
    return n == 4 ? kJpNames[0][w->enc == PORT_TEXT_UTF8] : kJpNames[w->chr][w->enc == PORT_TEXT_UTF8];
}
#define NAME_IS_JP(n) ((n) >= 4)
#define NAME_IS_MARIO(n) (((n) & 1) == 0)

/* the bytes of the character at s (one, or a whole double-byte one) */
static size_t charLen(const Swap* w, const char* s) {
    const unsigned char c = (unsigned char)s[0];
    switch (w->enc) {
        case PORT_TEXT_GAME: /* by the game's own rule (its language) */
            return _ismbblead(c) && s[1] != '\0' ? 2 : 1;
        case PORT_TEXT_SJIS:
            return ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) && s[1] != '\0' ? 2 : 1;
        case PORT_TEXT_UTF8: {
            size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1, k;
            for (k = 1; k < n; k++) {
                if (s[k] == '\0') {
                    return k;
                }
            }
            return n;
        }
        default:
            return 1;
    }
}

/* a letter of a word in the Latin languages (ASCII, Latin-1's accented ones;
 * in UTF-8 an accented letter's first byte) */
static int isLetter(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
}

static int startsWith(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* The name n starts at s + i as Mario's or the pick's name: not part of a
 * longer word or another game's or series' title ("Paper Mario", "Super Mario
 * Bros.", "MARIO SERIES THEME MUSIC", "Luigi's Mansion", マリオストーリー,
 * ルイージマンション). *end: past it and a shout's drawn-out letters, *stretch:
 * how many of those. prevWide: the character before is a double-byte one. */
static int nameAt(const Swap* w, const char* s, size_t i, int n, int prevWide, size_t* end, int* stretch) {
    const char* name = nameOf(w, n);
    const int u = w->enc == PORT_TEXT_UTF8;
    size_t len, k;
    const char* after;
    char stretchChr;

    if (name == NULL || strncmp(s + i, name, len = strlen(name)) != 0) {
        return 0;
    }
    k = i + len;
    *stretch = 0;
    if (NAME_IS_JP(n)) {
        size_t j;
        const size_t paper = strlen(kJpPaper[u]), su = strlen(kJpSu[u]);
        if (NAME_IS_MARIO(n)) {
            if (i >= paper && memcmp(s + i - paper, kJpPaper[u], paper) == 0) {
                return 0;
            }
            for (j = 0; j < 4; j++) {
                if (startsWith(s + k, kJpMarioNotAfter[u][j])) {
                    return 0;
                }
            }
        } else if (w->chr == PORT_PLAY_AS_LUIGI && startsWith(s + k, kJpMansion[u])) {
            return 0;
        } else if (w->chr == PORT_PLAY_AS_PEACH && i >= su && memcmp(s + i - su, kJpSu[u], su) == 0) {
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
    if (!NAME_IS_MARIO(n) && w->chr == PORT_PLAY_AS_PEACH &&
        ((i >= 7 && strncmp(s + i - 7, "Peachy", 6) == 0) || startsWith(s + i + 5, " Tart") ||
         startsWith(s + i + 5, "\nTart") || startsWith(s + i + 5, "-Torte"))) {
        return 0; /* the fruit: the Peachy Peach, the Peach Tart (Peach-Torte) */
    }
    stretchChr = kLatinNames[w->chr][n].stretch;
    while (s[k] == stretchChr) {
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
            if (w->chr != PORT_PLAY_AS_LUIGI) {
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

/* --- The pronouns: Mario's follow the pick where the text names him. Only Peach's differ from his
 * (Luigi and Bowser are "he" as he is). From a mention of Mario by name, not one that speaks to him
 * ("Mario, ...", "..., Mario!"), to the end of its sentence, and on into the next one when that
 * starts with one ("Mario's HP is low. His ..."), his are hers: "Mario's attack goes up by 2, but he
 * takes double damage" reads "Peach's attack ..., but she ...". The pick's own name (made Mario's)
 * ends it: whose pronoun is whose isn't clear then. Each language its own words, the text's language
 * (the game's, or the menus' for Paper Cut's own); a word that agrees with its noun, not its owner
 * (French son, Spanish su), stays. Peach's own (she, made Mario's) stay too. */
enum { LANG_EN, LANG_DE, LANG_FR, LANG_ES, LANG_IT, LANG_JA, LANG_OTHER };
/* when a word is his: always, before a noun (a space and a letter), not before one, before a German
 * noun (a space and a capital), not French's impersonal il (il y a, il faut) */
enum { PR_ANY, PR_NOUN, PR_NOT_NOUN, PR_CAP_NOUN, PR_PERSONAL };
typedef struct {
    const char* from; /* lower case; its Capitalized and UPPER forms too */
    const char* to;
    int when;
} Pronoun;
/* (longer words first; accented ones in Latin-1 and in UTF-8, each the same encoding as its text) */
static const Pronoun kPronounsEn[] = {
    {"himself", "herself", PR_ANY}, {"him", "her", PR_ANY}, {"his", "her", PR_NOUN},
    {"his", "hers", PR_NOT_NOUN},   {"he", "she", PR_ANY},  {NULL, NULL, 0},
};
static const Pronoun kPronounsDe[] = {
    {"seinen", "ihren", PR_ANY}, {"seinem", "ihrem", PR_ANY}, {"seiner", "ihrer", PR_ANY},
    {"seines", "ihres", PR_ANY}, {"seine", "ihre", PR_ANY},   {"sein", "ihr", PR_CAP_NOUN}, /* (not "sein", to be) */
    {"ihn", "sie", PR_ANY},      {"ihm", "ihr", PR_ANY},      {"er", "sie", PR_ANY},        {NULL, NULL, 0},
};
static const Pronoun kPronounsFr[] = {
    {"lui-m\xEAme", "elle-m\xEAme", PR_ANY}, {"lui-m\xC3\xAAme", "elle-m\xC3\xAAme", PR_ANY},
    {"il", "elle", PR_PERSONAL},             {NULL, NULL, 0},
};
static const Pronoun kPronounsEs[] = {
    {"\xE9l mismo", "ella misma", PR_ANY}, {"\xC3\xA9l mismo", "ella misma", PR_ANY},
    {"\xC9l mismo", "Ella misma", PR_ANY}, {"\xC3\x89l mismo", "Ella misma", PR_ANY},
    {"\xE9l", "ella", PR_ANY},             {"\xC3\xA9l", "ella", PR_ANY},
    {"\xC9l", "Ella", PR_ANY},             {"\xC3\x89l", "Ella", PR_ANY},
    {NULL, NULL, 0},
};
static const Pronoun kPronounsIt[] = {
    {"lui stesso", "lei stessa", PR_ANY}, {"lui", "lei", PR_ANY}, {NULL, NULL, 0},
};
static const Pronoun* const kPronouns[LANG_OTHER] = {kPronounsEn, kPronounsDe, kPronounsFr,
                                                     kPronounsEs, kPronounsIt, NULL};
/* Japanese: the game's text has no kanji for "he" (its font has none: it writes カレ, and カノジョ for "she"), so
 * カレ (not カレー, curry, nor カレら, they) is カノジョ; [0] Shift-JIS, [1] UTF-8 */
static const char* const kJpHe[2] = {"\x83\x4A\x83\x8C", "\xE3\x82\xAB\xE3\x83\xAC"};
static const char* const kJpHeNot[2][2] = {{"\x81\x5B", "\x82\xE7"}, {"\xE3\x83\xBC", "\xE3\x82\x89"}};
static const char* const kJpShe[2] = {"\x83\x4A\x83\x6D\x83\x57\x83\x87", "\xE3\x82\xAB\xE3\x83\x8E\xE3\x82\xB8\xE3\x83\xA7"};
/* a sentence's end (。！？; the game's own text has no 。, its pages end them) and a name spoken to (、！ after
 * it). (UTF-8's 。 and 、, of the menus' Japanese, as bytes: patterns that are looked for, never drawn with the
 * game's font, which has neither) */
static const char kJpStopU8[] = {(char)0xE3, (char)0x80, (char)0x82, 0};
static const char kJpCommaU8[] = {(char)0xE3, (char)0x80, (char)0x81, 0};
static const char* const kJpEnds[2][3] = {{"\x81\x42", "\x81\x49", "\x81\x48"}, {kJpStopU8, "\xEF\xBC\x81", "\xEF\xBC\x9F"}};
static const char* const kJpCall[2][2] = {{"\x81\x41", "\x81\x49"}, {kJpCommaU8, "\xEF\xBC\x81"}};

static char caseOf(char c, int at, int form) {
    if (c >= 'a' && c <= 'z' && (form == 2 || (form == 1 && at == 0))) {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static int upperAt(const unsigned char* p) {
    return (p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 0xC0 && p[0] <= 0xDE && p[0] != 0xD7 && p[0] != 0xC3) ||
           (p[0] == 0xC3 && p[1] >= 0x80 && p[1] <= 0x9E && p[1] != 0x97); /* (Latin-1's, UTF-8's: Ä Ö Ü ...) */
}

/* one of the language's words for him at s + i (a word's start), put in o as hers: its length, or 0 */
static size_t pronounAt(const Swap* w, const char* s, size_t i, char** o) {
    const Pronoun* p;
    int form;
    if (w->lang < 0 || w->lang >= LANG_OTHER || kPronouns[w->lang] == NULL) {
        return 0;
    }
    for (p = kPronouns[w->lang]; p->from != NULL; p++) {
        const size_t len = strlen(p->from);
        for (form = 0; form < 3; form++) {
            const unsigned char* after = (const unsigned char*)s + i + len;
            size_t k;
            int noun;
            for (k = 0; k < len && s[i + k] == caseOf(p->from[k], (int)k, form); k++) {
            }
            if (k < len || isLetter(*after) || (w->enc == PORT_TEXT_UTF8 && *after >= 0x80 && *after < 0xC0)) {
                continue;
            }
            noun = after[0] == ' ' && isLetter(after[1]);
            if ((p->when == PR_NOUN && !noun) || (p->when == PR_NOT_NOUN && noun) ||
                (p->when == PR_CAP_NOUN && !(after[0] == ' ' && upperAt(after + 1))) ||
                (p->when == PR_PERSONAL && (startsWith((const char*)after, " y ") || startsWith((const char*)after, " faut") ||
                                            startsWith((const char*)after, " fallait") ||
                                            startsWith((const char*)after, " faudra")))) {
                continue;
            }
            for (k = 0; p->to[k] != '\0'; k++) {
                *(*o)++ = caseOf(p->to[k], (int)k, form);
            }
            return len;
        }
    }
    return 0;
}

/* --- The words around the name, and the phrases that name the player another way (docs/PLAY_AS.md, Text:
 * the rules). Tables in UTF-8, a Latin-1 text's made from them as it is read; Japanese also in Shift-JIS. A
 * space in a table's text is any one space or line break of the text's, and the text's own are kept. */
#define PK_L (1 << PORT_PLAY_AS_LUIGI)
#define PK_P (1 << PORT_PLAY_AS_PEACH)
#define PK_B (1 << PORT_PLAY_AS_BOWSER)
#define PK_PB (PK_P | PK_B)
#define PK_ALL (PK_L | PK_P | PK_B)

/* a man's word just before Mario's name (a title, "dear", "this") and Peach's for it; chain: only before a
 * word just made hers (Italian's il Signor Mario: la Signora Peach). Japanese has none before. */
typedef struct {
    int lang;
    const char* from;
    const char* to;
    int chain;
} Word;
/* a phrase for the picks (PK_*) in a language: anywhere (kPhrases: a nickname, a misremembered name, Luigi's
 * "big brother"; tag: only in that message, or those starting with it before a '*'), or just after Mario's
 * name, put in as Peach's (kAfter: "est devenu", "è arrivato"). key: a part of from in ASCII (or Japanese, in
 * each encoding) for the quick test that a text may have it ("": always) */
typedef struct {
    int picks;
    int lang;
    const char* tag;
    const char* from;
    const char* to;
    const char* fromSjis;
    const char* toSjis;
    const char* key;
    const char* keySjis;
} Phrase;
/* kBefore, kAfter and kPhrases: made from port/assets/play_as_text.txt by tools/port/playas_text_check.py tables */
#include "play_as_text_tables.inc"

/* a table's text in the text's encoding (buf: room for it) */
static const char* tableText(const Swap* w, const char* utf8, const char* sjis, char* buf, size_t size) {
    size_t n = 0;
    if (w->enc == PORT_TEXT_UTF8) {
        return utf8;
    }
    if (w->enc == PORT_TEXT_SJIS || (w->enc == PORT_TEXT_GAME && w->lang == LANG_JA)) {
        return sjis;
    }
    /* Latin-1: UTF-8's two-byte letters (U+0080..U+00FF) as one */
    for (; utf8 != NULL && *utf8 != '\0' && n + 1 < size; utf8++) {
        const unsigned char c = (unsigned char)*utf8;
        if ((c == 0xC2 || c == 0xC3) && utf8[1] != '\0') {
            buf[n++] = (char)(((c & 0x03) << 6) | ((unsigned char)utf8[1] & 0x3F));
            utf8++;
        } else {
            buf[n++] = (char)c;
        }
    }
    buf[n] = '\0';
    return utf8 != NULL ? buf : NULL;
}

/* the space or line break at s: its length (a space, a line break, the European disc's \r\n; Japanese's
 * full-width space in Shift-JIS or UTF-8), or 0 */
static size_t spaceAt(const char* s) {
    const unsigned char* u = (const unsigned char*)s;
    if (u[0] == ' ' || u[0] == '\n') {
        return 1;
    }
    if ((u[0] == '\r' && u[1] == '\n') || (u[0] == 0x81 && u[1] == 0x40)) {
        return 2;
    }
    return u[0] == 0xE3 && u[1] == 0x80 && u[2] == 0x80 ? 3 : 0;
}

/* from (in the text's encoding) at s + i, a space in it any one space or line break: its length, or 0. latin:
 * the text's word ends where from's does (a letter after it is another word) */
static size_t phraseAt(const char* s, size_t i, const char* from, int latin) {
    size_t k, j = i, n;
    if (from == NULL || from[0] == '\0') {
        return 0;
    }
    for (k = 0; from[k] != '\0'; k++) {
        if (from[k] != ' ') {
            if (s[j] != from[k]) {
                return 0;
            }
            j++;
        } else if ((n = spaceAt(s + j)) != 0 && (latin ? n < 3 && (unsigned char)s[j] != 0x81 : 1)) {
            j += n;
        } else {
            return 0;
        }
    }
    if (latin && isLetter((unsigned char)from[k - 1]) && isLetter((unsigned char)s[j])) {
        return 0;
    }
    return j - i;
}

/* to into o, each space of it the next space or line break of the text it stands for (src, srcLen) */
static void putPhrase(char** o, const char* to, const char* src, size_t srcLen) {
    size_t j = 0, n = 0;
    for (; *to != '\0'; to++) {
        if (*to != ' ') {
            *(*o)++ = *to;
            continue;
        }
        while (j < srcLen && (n = spaceAt(src + j)) == 0) {
            j++;
        }
        if (j < srcLen) {
            put(o, src + j, n);
            j += n;
        } else {
            *(*o)++ = ' ';
        }
    }
}

static int tagIs(const char* tag, const char* want) {
    const size_t n = strlen(want);
    if (tag == NULL) {
        return 0;
    }
    if (n > 0 && want[n - 1] == '*') {
        return strncmp(tag, want, n - 1) == 0;
    }
    return strcmp(tag, want) == 0;
}

/* a phrase of kPhrases for this swap at s + i (a word's start): put in o, its length, or 0; *named: it puts in
 * the pick's name (for Mario's: the pronouns after it are the pick's) */
static size_t phraseSwap(const Swap* w, const char* s, size_t i, char** o, int* named) {
    const int latin = w->lang != LANG_JA;
    size_t k;
    char from[160], to[160];
    for (k = 0; k < sizeof(kPhrases) / sizeof(kPhrases[0]); k++) {
        const Phrase* p = &kPhrases[k];
        const char* f;
        size_t len;
        if (!(p->picks & (1 << w->chr)) || (p->lang != w->lang) || (p->tag != NULL && !tagIs(w->tag, p->tag))) {
            continue;
        }
        if (latin ? ((unsigned char)p->from[0] < 0x80 && p->from[0] != s[i]) : (p->from[0] != s[i] && p->fromSjis[0] != s[i])) {
            continue; /* (the quick test: its first letter) */
        }
        f = tableText(w, p->from, p->fromSjis, from, sizeof(from));
        if ((len = phraseAt(s, i, f, latin)) != 0) {
            putPhrase(o, tableText(w, p->to, p->toSjis, to, sizeof(to)), s + i, len);
            *named = strstr(p->from, "Mario") != NULL && strstr(p->to, kLatinNames[w->chr][1].name) != NULL;
            return len;
        }
    }
    return 0;
}

/* Peach's name about to be put in for Mario's at o (after a space or a line break): the man's word before it
 * in out made hers (kBefore), and then a chain word before that one (il, del, mein, mon) */
static void beforePeach(const Swap* w, char* out, char** o) {
    char* end = *o; /* (just past the space after the word to look at) */
    int chained = 0;
    for (;;) {
        size_t k;
        char* at = NULL;
        size_t len = 0;
        const Word* b = NULL;
        char from[64], to[64], made[96];
        size_t sep;
        if (end <= out || (end[-1] != ' ' && end[-1] != '\n')) {
            return;
        }
        /* (its space: one, or the European disc's line break, two) */
        sep = end - out >= 2 && end[-1] == '\n' && end[-2] == '\r' ? 2 : 1;
        for (k = 0; k < sizeof(kBefore) / sizeof(kBefore[0]); k++) {
            const char* f;
            b = &kBefore[k];
            if (b->lang != w->lang || b->chain != chained) {
                continue;
            }
            f = tableText(w, b->from, NULL, from, sizeof(from));
            len = f != NULL ? strlen(f) : 0;
            if (len == 0 || (size_t)(end - sep - out) < len) {
                continue;
            }
            at = end - sep - len;
            if ((at > out && isLetter((unsigned char)at[-1])) || phraseAt(at, 0, f, 0) != len) {
                continue;
            }
            break;
        }
        if (k == sizeof(kBefore) / sizeof(kBefore[0])) {
            return;
        }
        {
            /* the word made hers in place, what follows it moved */
            char* m = made;
            const char* t = tableText(w, b->to, NULL, to, sizeof(to));
            size_t n;
            if (t == NULL || strlen(t) >= sizeof(made)) {
                return;
            }
            putPhrase(&m, t, at, len);
            n = (size_t)(m - made);
            memmove(at + n, at + len, (size_t)(*o - (at + len)));
            *o += (ptrdiff_t)n - (ptrdiff_t)len;
            memcpy(at, made, n);
        }
        end = at;
        chained = 1;
    }
}

/* after Peach's name, put in for Mario's: a man's words after it (kAfter) made hers, its length or 0 */
static size_t afterPeach(const Swap* w, const char* s, size_t i, char** o) {
    size_t k, t = i;
    char from[96], to[96];
    /* (past the game's commands just after the name and its line breaks: "Mario\n<wait 250>\nè un cattivo") */
    for (;;) {
        size_t u = t;
        while (s[u] == ' ' || s[u] == '\n' || s[u] == '\r') {
            u++;
        }
        if (w->enc == PORT_TEXT_UTF8 || s[u] != '<' || strchr(s + u, '>') == NULL) {
            break;
        }
        t = (size_t)(strchr(s + u, '>') - s) + 1;
    }
    for (k = 0; k < sizeof(kAfter) / sizeof(kAfter[0]); k++) {
        const Phrase* p = &kAfter[k];
        const char* f;
        const char* g;
        size_t len;
        int bare; /* (after a command, no space before the words: its own) */
        if (p->lang != w->lang) {
            continue;
        }
        f = tableText(w, p->from, p->fromSjis, from, sizeof(from));
        bare = t > i && f != NULL && f[0] == ' ' && s[t] != ' ' && s[t] != '\n' && s[t] != '\r';
        if ((len = phraseAt(s, t, bare ? f + 1 : f, w->lang != LANG_JA)) != 0) {
            g = tableText(w, p->to, p->toSjis, to, sizeof(to));
            put(o, s + i, t - i);
            putPhrase(o, bare && g[0] == ' ' ? g + 1 : g, s + t, len);
            return t - i + len;
        }
    }
    return 0;
}

/* the name at s + i .. end is spoken to, not of: "Mario, ...", "Mario!", "Thanks, Mario.", "Dear Mario," at its
 * clause's start (after a capital's word, a greeting's, punctuation other than ':'); not "If you see Mario, thank
 * him", "This is Mario, and he's...", "everyone: Mario, his friends" */
static int spokenTo(const char* s, size_t i, size_t end) {
    static const char* const kGreetings[] = {"dear", "dearest", "hey", "oh", "lieber", "liebster", "liebe", "cher",
                                             "querido", "caro", "carissimo", "hola", "ciao", "salut", "hallo"};
    size_t b = i, w;
    const char c = s[end];
    if (!(c == ',' || c == '!' || c == '?' || c == '.')) {
        return 0;
    }
    while (b > 0 && (s[b - 1] == ' ' || s[b - 1] == '\n')) {
        b--;
    }
    if (c == '.' && !(b > 0 && s[b - 1] == ',')) {
        return 0; /* (a sentence's end: only "..., Mario.") */
    }
    if (b == 0) {
        return 1;
    }
    if (s[b - 1] == ':') {
        return 0;
    }
    if (!isLetter((unsigned char)s[b - 1])) {
        return 1;
    }
    for (w = b; w > 0 && isLetter((unsigned char)s[w - 1]); w--) {
    }
    if (upperAt((const unsigned char*)s + w)) {
        return 1;
    }
    for (b = 0; b < sizeof(kGreetings) / sizeof(kGreetings[0]); b++) {
        const size_t n = strlen(kGreetings[b]);
        if (strncmp(s + w, kGreetings[b], n) == 0 && !isLetter((unsigned char)s[w + n])) {
            return 1;
        }
    }
    return 0;
}

/* s with Mario's and the pick's names swapped (and, for Peach, his pronouns hers where he is named);
 * out has room for twice s */
static void substitute(const Swap* w, const char* s, char* out) {
    char* o = out;
    size_t i = 0;
    int prevWide = 0;
    int n, st;
    size_t end;
    const int u = w->enc == PORT_TEXT_UTF8;
    const int jp = w->lang == LANG_JA && w->enc != PORT_TEXT_LATIN1;
    /* the pronouns: 0 not now; 1 in a sentence that names Mario; 2 at the next one's start */
    const int pronouns = w->chr == PORT_PLAY_AS_PEACH && w->lang >= 0 && w->lang < LANG_OTHER;
    int scope = 0;

    while (s[i] != '\0') {
        unsigned char c = (unsigned char)s[i];
        size_t cl;
        if (c == '<' && w->enc != PORT_TEXT_UTF8) { /* a command, <col ...>, <p>: as it is */
            if (startsWith(s + i, "<p>") || startsWith(s + i, "<k>")) {
                scope = scope != 0 ? 2 : 0; /* (a page turned, a key waited for: as a sentence's end) */
            }
            while (s[i] != '\0' && s[i] != '>') {
                *o++ = s[i++];
            }
            if (s[i] == '>') {
                *o++ = s[i++];
            }
            prevWide = 0;
            continue;
        }
        if (jp || i == 0 || prevWide || !isLetter((unsigned char)s[i - 1])) {
            int named = 0;
            const size_t len = phraseSwap(w, s, i, &o, &named); /* a nickname, a misremembered name (kPhrases) */
            if (len > 0) {
                if (pronouns && named) {
                    scope = 1;
                }
                i += len;
                prevWide = 0;
                continue;
            }
        }
        for (n = 0; n < NAMES; n++) {
            if (nameAt(w, s, i, n, prevWide, &end, &st)) {
                const int other = n ^ 1;
                const char* to = nameOf(w, other);
                const int asPeach = w->chr == PORT_PLAY_AS_PEACH && NAME_IS_MARIO(n);
                if (asPeach && !NAME_IS_JP(n)) {
                    beforePeach(w, out, &o); /* "Mr." "Ms.", "il Signor" "la Signora" */
                }
                put(&o, to, strlen(to));
                for (; st > 0; st--) {
                    *o++ = kLatinNames[w->chr][other].stretch;
                }
                if (pronouns && !NAME_IS_MARIO(n)) {
                    scope = 0; /* (the pick's own name: whose pronoun is whose isn't clear) */
                } else if (pronouns && NAME_IS_JP(n)) {
                    scope = startsWith(s + end, kJpCall[u][0]) || startsWith(s + end, kJpCall[u][1]) ? 0 : 1;
                } else if (pronouns) {
                    scope = spokenTo(s, i, end) ? 0 : 1;
                }
                if (asPeach) {
                    end += afterPeach(w, s, end, &o); /* "est devenu" "est devenue" */
                }
                i = end;
                break;
            }
        }
        if (n < NAMES) {
            prevWide = NAME_IS_JP(n);
            continue;
        }
        if (scope != 0 && jp) {
            /* (not the end of a longer katakana word: a katakana or ー before it) */
            const int kanaBefore = u ? i >= 3 && (unsigned char)s[i - 3] == 0xE3 &&
                                           ((unsigned char)s[i - 2] == 0x83 || ((unsigned char)s[i - 2] == 0x82 &&
                                                                                 (unsigned char)s[i - 1] >= 0xA1))
                                     : i >= 2 && ((unsigned char)s[i - 2] == 0x83 ||
                                                  ((unsigned char)s[i - 2] == 0x81 && (unsigned char)s[i - 1] == 0x5B));
            if (!kanaBefore && startsWith(s + i, kJpHe[u]) && !startsWith(s + i + strlen(kJpHe[u]), kJpHeNot[u][0]) &&
                !startsWith(s + i + strlen(kJpHe[u]), kJpHeNot[u][1])) {
                put(&o, kJpShe[u], strlen(kJpShe[u]));
                i += strlen(kJpHe[u]);
                prevWide = 1;
                continue;
            }
            for (n = 0; n < 3; n++) {
                if (startsWith(s + i, kJpEnds[u][n])) {
                    scope = scope == 1 ? 2 : 0; /* (Japanese: on through the next sentence) */
                    break;
                }
            }
        } else if (scope != 0) {
            const unsigned char before = i > 0 ? (unsigned char)s[i - 1] : 0;
            /* (after a double-byte character, a word's start; in UTF-8 an accented letter's bytes are its word's) */
            const int wordStart =
                i == 0 || (prevWide && !u) || !(isLetter(before) || (u && before >= 0x80 && before < 0xC0));
            if (wordStart && isLetter(c)) {
                const size_t len = pronounAt(w, s, i, &o);
                if (len > 0) {
                    i += len;
                    scope = 1;
                    prevWide = 0;
                    continue;
                }
                if (scope == 2) {
                    scope = 0; /* (the next sentence doesn't start with one: he is someone else's now) */
                }
            }
            if (c == '.' || c == '!' || c == '?') {
                scope = 2;
            }
        }
        /* a double-byte character as a whole */
        cl = charLen(w, s + i);
        prevWide = cl > 1;
        put(&o, s + i, cl);
        i += cl;
    }
    *o = '\0';
}

int portPlayAsText(void) {
    const int chr = portPlayAs();
    /* a versus battle is the brothers' (Mario against Luigi, whoever is the
     * pick: battle_disp.c); Brothers mode turns the pick off (portPlayAs) */
    if (portVersusOn()) {
        return PORT_PLAY_AS_MARIO;
    }
    return chr;
}

const char* portPlayAsName(void) {
    return kLatinNames[portPlayAsText()][1].name;
}

int portPlayAsHasName(const char* text, int chr) {
    int n;
    if (text == NULL || chr <= PORT_PLAY_AS_MARIO || chr >= PORT_PLAY_AS_COUNT) {
        return 0;
    }
    for (n = 0; n < 4; n++) {
        if (strstr(text, kLatinNames[chr][n].name) != NULL) {
            return 1;
        }
    }
    if (strstr(text, JP_MARIO_SJIS) != NULL || strstr(text, JP_MARIO_UTF8) != NULL ||
        strstr(text, kJpNames[chr][0]) != NULL || strstr(text, kJpNames[chr][1]) != NULL) {
        return 1;
    }
    /* a phrase that names the player another way ("Mr. Mustache", "Captain 'Stache") */
    for (n = 0; n < (int)(sizeof(kPhrases) / sizeof(kPhrases[0])); n++) {
        const Phrase* p = &kPhrases[n];
        if ((p->picks & (1 << chr)) &&
            (p->key[0] == '\0' || strstr(text, p->key) != NULL || (p->keySjis != NULL && strstr(text, p->keySjis) != NULL))) {
            return 1;
        }
    }
    return 0;
}

/* the game's text's language (LANG_*): the JP release's Japanese; the European release's chosen one
 * (mariost.c's gp->language: 0 Japanese, 1 English .. 5 Italian); the US release's English, or the
 * European disc's language it shows (port/include/port/text_language.h) */
static int gameTextLang(void) {
#if VERSION < VERSION_US
    return LANG_JA;
#elif VERSION >= VERSION_DEMO_EU
    static const int kLang[6] = {LANG_JA, LANG_EN, LANG_DE, LANG_FR, LANG_ES, LANG_IT};
    return gp != NULL && gp->language >= 0 && gp->language < 6 ? kLang[gp->language] : LANG_EN;
#else
    const char* dir = portTextLanguageDir();
    if (dir == NULL) {
        return LANG_EN;
    }
    return !strcmp(dir, "DE") ? LANG_DE : !strcmp(dir, "FR") ? LANG_FR : !strcmp(dir, "SP") ? LANG_ES
         : !strcmp(dir, "IT") ? LANG_IT : LANG_OTHER;
#endif
}

static void swapText(const char* text, char* out, int chr, int enc, int lang, const char* tag) {
    Swap w;
    w.chr = chr > PORT_PLAY_AS_MARIO && chr < PORT_PLAY_AS_COUNT ? chr : PORT_PLAY_AS_MARIO;
    w.enc = enc;
    w.lang = lang >= 0 && lang < LANG_OTHER ? lang : LANG_OTHER;
    w.tag = tag;
    substitute(&w, text, out);
}

void portPlayAsSwapTextIn(const char* text, char* out, int chr, int enc, int lang) {
    swapText(text, out, chr, enc, lang, NULL);
}

void portPlayAsSwapText(const char* text, char* out, int chr, int enc) {
    /* the game's messages: its language; Paper Cut's own: the menus' (port/include/port/tr.h: 0 English ..
       5 Japanese, the same order; a language pack's own, 6 and on, none) */
    portPlayAsSwapTextIn(text, out, chr, enc, enc == PORT_TEXT_GAME ? gameTextLang() : portTrLanguage());
}

/* $TTYD_PLAY_AS_PRONOUN_TEST=1 (tr.cpp, once): Peach's pronouns in each language, checked ("[play-as] ...") */
void portPlayAsPronounTest(void) {
    static const struct {
        int lang;
        const char* in;
        const char* want;
    } kCases[] = {
        {LANG_EN, "Mario's attack goes up by 2, but he takes double damage.",
         "Peach's attack goes up by 2, but she takes double damage."},
        {LANG_EN, "Mario's HP is low. His defense drops, and the badge is his. He rests. Koops waves at him.",
         "Peach's HP is low. Her defense drops, and the badge is hers. She rests. Koops waves at him."},
        {LANG_EN, "Mario, he went that way! Thanks, Mario. He is gone.", "Peach, he went that way! Thanks, Peach. He is gone."},
        {LANG_EN, "Mario saw Peach and he waved. MARIO HIMSELF!", "Peach saw Mario and he waved. PEACH HERSELF!"},
        {LANG_EN, "The hero, the man: Mario took them himself, and then the theme...",
         "The hero, the man: Peach took them herself, and then the theme..."},
        {LANG_DE, "Mario greift an, aber er nimmt doppelten Schaden, und sein Hut f\xC3\xA4llt. Er kann es sein.",
         "Peach greift an, aber sie nimmt doppelten Schaden, und ihr Hut f\xC3\xA4llt. Sie kann es sein."},
        {LANG_DE, "Marios Angriff steigt, aber seine Abwehr sinkt, und es freut ihn. Es ist ihm egal.",
         "Peachs Angriff steigt, aber ihre Abwehr sinkt, und es freut sie. Es ist ihm egal."},
        {LANG_FR, "Mario attaque, mais il subit le double de d\xC3\xA9g\xC3\xA2ts. Il y a un risque.",
         "Peach attaque, mais elle subit le double de d\xC3\xA9g\xC3\xA2ts. Il y a un risque."},
        {LANG_ES, "Mario ataca, pero \xC3\xA9l recibe el doble de da\xC3\xB1o. \xC3\x89l mismo lo sabe.",
         "Peach ataca, pero ella recibe el doble de da\xC3\xB1o. Ella misma lo sabe."},
        {LANG_IT, "Mario attacca, ma lui subisce il doppio dei danni.", "Peach attacca, ma lei subisce il doppio dei danni."},
        /* (the words around the name, play_as_text.txt: a title before it, agreement after it, a nickname) */
        {LANG_EN, "Hello, Mr. Mario! If you see Mario, thank him. Hi, Mr. Mustache!",
         "Hello, Ms. Peach! If you see Peach, thank her. Hi, Miss Crown!"},
        {LANG_IT, "Lei non \xC3\xA8 il Signor Mario? Mario\n\xC3\xA8 arrivato.",
         "Lei non \xC3\xA8 la Signora Peach? Peach\n\xC3\xA8 arrivata."},
        {LANG_FR, "Cher M. Mario, Mario est devenu Superstar!", "Ch\xC3\xA8re Mme Peach, Peach est devenue Superstar!"},
        /* (マリオは　カレの　ボウシを　みた！: ピーチは　カノジョの ...) */
        {LANG_JA,
         "\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA\xE3\x81\xAF\xE3\x80\x80\xE3\x82\xAB\xE3\x83\xAC\xE3\x81\xAE\xE3\x80\x80"
         "\xE3\x83\x9C\xE3\x82\xA6\xE3\x82\xB7\xE3\x82\x92\xE3\x80\x80\xE3\x81\xBF\xE3\x81\x9F\xEF\xBC\x81",
         "\xE3\x83\x94\xE3\x83\xBC\xE3\x83\x81\xE3\x81\xAF\xE3\x80\x80\xE3\x82\xAB\xE3\x83\x8E\xE3\x82\xB8\xE3\x83\xA7"
         "\xE3\x81\xAE\xE3\x80\x80\xE3\x83\x9C\xE3\x82\xA6\xE3\x82\xB7\xE3\x82\x92\xE3\x80\x80\xE3\x81\xBF\xE3\x81\x9F"
         "\xEF\xBC\x81"},
    };
    static const char* const kLangName[LANG_OTHER] = {"en", "de", "fr", "es", "it", "ja"};
    char out[512];
    char luigi[512];
    size_t k;
    int bad = 0;
    for (k = 0; k < sizeof(kCases) / sizeof(kCases[0]); k++) {
        portPlayAsSwapTextIn(kCases[k].in, out, PORT_PLAY_AS_PEACH, PORT_TEXT_UTF8, kCases[k].lang);
        portPlayAsSwapTextIn(kCases[k].in, luigi, PORT_PLAY_AS_LUIGI, PORT_TEXT_UTF8, kCases[k].lang);
        if (strcmp(out, kCases[k].want) != 0) {
            bad++;
            printf("[play-as] pronouns %s %d: FAIL: %s\n", kLangName[kCases[k].lang], (int)k, out);
        } else {
            printf("[play-as] pronouns %s %d: ok\n", kLangName[kCases[k].lang], (int)k);
        }
        if (strstr(luigi, "she") != NULL || strstr(luigi, " sie ") != NULL || strstr(luigi, "elle") != NULL ||
            strstr(luigi, " lei ") != NULL) {
            bad++;
            printf("[play-as] pronouns %s %d: FAIL: Luigi's are hers: %s\n", kLangName[kCases[k].lang], (int)k, luigi);
        }
    }
    printf("[play-as] pronoun test done: %d problems\n", bad);
    fflush(stdout);
}

/* Paper Cut's own texts that keep Mario's name whoever the pick is, by their English (tr.cpp's playAs):
 * a choice that is a name itself (the Play as choice, a side, a sticker), Play as's and the voice
 * toggle's own help (and an achievement's "playing as Luigi"), the looks and colours named after the
 * brothers, versus (always the brothers: Mario's side against Luigi's), Brothers mode (Luigi with
 * Mario, and it turns Play as off), and the model files' names. */
int portPlayAsKeepsMario(const char* english) {
    static const char* const kWhole[] = {
        "Mario", "Luigi", "Peach", "Bowser", "Fire Mario", "Luigi green",
        /* the colour slots, each named after the one it dresses (cosmetics.cpp: all four's listed together) */
        "Mario's cap and shirt", "Mario's overalls", "Luigi's cap and shirt", "Luigi's overalls", "Peach's dress",
        "Peach's crown", "Peach's hair", "Peach's gloves", "Peach's earrings", "Peach's shoes", "Bowser's shell",
        "Bowser's hair", "Bowser's skin", "Bowser's horns and spikes", "Bowser's bracelets"};
    static const char* const kPart[] = {
        "Mario's place", "Luigi's voice for Mario", "sound as Luigi's", "as his brother or his rivals",
        "Mario's side", "Luigi's side", "Mario's player", "against Luigi", "against the computer's Luigi",
        "Mario's badges", "Mario's items", "Luigi's badges", "Luigi's items", "Luigi joins Mario",
        "Luigi follows Mario", "walks Luigi", "Brothers mode's Luigi", "Mario is a_mario", "Play as", "playing as",
        "Luigi's higher jump", "While Luigi is picked",
        /* texts written for each pick already (the Wardrobe's tab and costume notes, wardrobe_aurora.c):
         * swapped again, Peach's would read "Mario, her partners" (the pick and Mario trade places) */
        "partners and the status bar look", "This outfit doesn't fit",
    };
    size_t k;
    if (english == NULL) {
        return 0;
    }
    for (k = 0; k < sizeof(kWhole) / sizeof(kWhole[0]); k++) {
        if (strcmp(english, kWhole[k]) == 0) {
            return 1;
        }
    }
    for (k = 0; k < sizeof(kPart) / sizeof(kPart[0]); k++) {
        if (strstr(english, kPart[k]) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* $TTYD_PLAY_AS_TEXT_DUMP=<file> (portTraceFrame, at the first frame, then the game exits): every text in
 * the file swapped for Luigi, Peach and Bowser by the game's own rules, into <file>.out, for
 * tools/port/playas_text_check.py. Each line of the file: kind (g the game's text: Shift-JIS in Japanese,
 * else Latin-1; u Paper Cut's own: UTF-8; what follows the letter, its source, isn't read), language (en de fr
 * es it ja), the key (the game's: file/tag, the tag for kPhrases; Paper Cut's own: its English, for the texts
 * that keep Mario's name), the text; tab apart, with \\ \n \t \r escaped. Each line of
 * <file>.out: the three swaps, tab apart, escaped the same way. */
static char* dumpUnescape(char* s) {
    char *r = s, *w = s;
    for (; *r != '\0'; r++) {
        if (*r == '\\' && r[1] != '\0') {
            r++;
            *w++ = *r == 'n' ? '\n' : *r == 't' ? '\t' : *r == 'r' ? '\r' : *r;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
    return s;
}

static void dumpEscaped(FILE* f, const char* s) {
    for (; *s != '\0'; s++) {
        if (*s == '\\' || *s == '\n' || *s == '\t' || *s == '\r') {
            fputc('\\', f);
            fputc(*s == '\n' ? 'n' : *s == '\t' ? 't' : *s == '\r' ? 'r' : '\\', f);
        } else {
            fputc(*s, f);
        }
    }
}

void portPlayAsTextDump(const char* path) {
    static const char* const kLangs[LANG_OTHER] = {"en", "de", "fr", "es", "it", "ja"};
    char outPath[1024];
    FILE* in = fopen(path, "rb");
    FILE* out;
    char* data;
    char* line;
    char* buf = NULL;
    size_t bufSize = 0;
    long size;
    int count = 0;
    snprintf(outPath, sizeof(outPath), "%s.out", path);
    out = in != NULL ? fopen(outPath, "wb") : NULL;
    if (in == NULL || out == NULL || fseek(in, 0, SEEK_END) != 0 || (size = ftell(in)) < 0 ||
        fseek(in, 0, SEEK_SET) != 0 || (data = (char*)malloc((size_t)size + 1)) == NULL) {
        printf("[play-as] text dump: can't read %s or write %s\n", path, outPath);
        fflush(stdout);
        if (in != NULL) {
            fclose(in);
        }
        if (out != NULL) {
            fclose(out);
        }
        return;
    }
    data[fread(data, 1, (size_t)size, in)] = '\0';
    fclose(in);
    for (line = data; *line != '\0';) {
        char* next = strchr(line, '\n');
        char* field[4];
        int n = 1, lang, chr;
        if (next != NULL) {
            *next++ = '\0';
        } else {
            next = line + strlen(line);
        }
        field[0] = line;
        while (n < 4 && (field[n] = strchr(field[n - 1], '\t')) != NULL) {
            *field[n]++ = '\0';
            n++;
        }
        line = next;
        if (n < 4) {
            continue;
        }
        for (lang = 0; lang < LANG_OTHER && strcmp(field[1], kLangs[lang]) != 0; lang++) {
        }
        dumpUnescape(field[2]);
        dumpUnescape(field[3]);
        if (strlen(field[3]) * 2 + 1 > bufSize) {
            bufSize = strlen(field[3]) * 2 + 1;
            free(buf);
            buf = (char*)malloc(bufSize);
            if (buf == NULL) {
                break;
            }
        }
        for (chr = PORT_PLAY_AS_LUIGI; chr <= PORT_PLAY_AS_BOWSER; chr++) {
            if (field[0][0] == 'u' && portPlayAsKeepsMario(field[2])) {
                strcpy(buf, field[3]);
            } else {
                const int enc = field[0][0] == 'u' ? PORT_TEXT_UTF8 : lang == LANG_JA ? PORT_TEXT_SJIS : PORT_TEXT_LATIN1;
                const char* tag = field[0][0] == 'g' && strchr(field[2], '/') != NULL ? strchr(field[2], '/') + 1 : NULL;
                swapText(field[3], buf, chr, enc, lang, tag);
            }
            if (chr != PORT_PLAY_AS_LUIGI) {
                fputc('\t', out);
            }
            dumpEscaped(out, buf);
        }
        fputc('\n', out);
        count++;
    }
    free(buf);
    free(data);
    fclose(out);
    printf("[play-as] text dump: %d texts swapped into %s\n", count, outPath);
    fflush(stdout);
}


/* The texts made, by their message's text: kept for as long as the game
 * runs (the game keeps pointers to them, a message window for one), on the
 * save-state heap with the table here in the save-state section. */
typedef struct LuigiText {
    struct LuigiText* next;
    char* in;
    char* out; /* NULL: the same as in */
    int chr;   /* the pick it was made for */
    char* tag; /* the message's tag, when a phrase of kPhrases is that message's own (else NULL: any message's) */
} LuigiText;
static LuigiText* sTexts[1024];

/* a phrase of kPhrases is this message's own, for chr */
static int tagHasPhrases(const char* tag, int chr) {
    size_t k;
    for (k = 0; tag != NULL && k < sizeof(kPhrases) / sizeof(kPhrases[0]); k++) {
        if ((kPhrases[k].picks & (1 << chr)) && kPhrases[k].tag != NULL && tagIs(tag, kPhrases[k].tag)) {
            return 1;
        }
    }
    return 0;
}

const char* portLuigiText(const char* msg) {
    return portLuigiTextTag(NULL, msg);
}

const char* portLuigiTextTag(const char* tag, const char* msg) {
    unsigned int hash = 2166136261u;
    const unsigned char* p;
    LuigiText* t;
    size_t len;
    char* out;
    const int chr = portPlayAsText();
    int tagged;

    if (msg == NULL || chr == PORT_PLAY_AS_MARIO) {
        return msg;
    }
    /* the quick test: no name in it */
    if (!portPlayAsHasName(msg, chr)) {
        return msg;
    }
    for (p = (const unsigned char*)msg; *p != '\0'; p++) {
        hash = (hash ^ *p) * 16777619u;
    }
    tagged = tagHasPhrases(tag, chr);
    for (t = sTexts[hash % 1024]; t != NULL; t = t->next) {
        if (t->chr == chr && !strcmp(t->in, msg) && (tagged ? t->tag != NULL && !strcmp(t->tag, tag) : t->tag == NULL)) {
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
    t->chr = chr;
    t->tag = NULL;
    if (tagged && (t->tag = (char*)malloc(strlen(tag) + 1)) != NULL) {
        memcpy(t->tag, tag, strlen(tag) + 1);
    }
    swapText(msg, out, chr, PORT_TEXT_GAME, gameTextLang(), t->tag);
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
     * Bowser's interlude steps and landings, Bowser's jump; and for the spin
     * jump, the whoosh and the thud of his ground pound in his own stages) */
#define SFX_KINDS 6
    static const char* const kMario[SFX_KINDS] = {"SFX_MARIO_MOVE1L", "SFX_MARIO_MOVE1R", "SFX_MARIO_JUMP1",
                                                  "SFX_MARIO_LANDING1", "SFX_MARIO_HIP_DROP1",
                                                  "SFX_MARIO_HIO_DROP2"};
    static const char* const kSfx[PORT_PLAY_AS_COUNT][SFX_KINDS] = {
        {NULL, NULL, NULL, NULL, NULL, NULL},
        {"SFX_FLD_NPC_LUIGI_MOVE1L", "SFX_FLD_NPC_LUIGI_MOVE1R", "SFX_FLD_NPC_LUIGI_JUMP1",
         "SFX_FLD_NPC_LUIGI_LANDING1", NULL, NULL},
        {"SFX_PEACH_MOVE1L", "SFX_PEACH_MOVE1R", NULL, "SFX_PEACH_LANDING1", NULL, NULL},
        {"SFX_KUPPA_MOVE1", "SFX_KUPPA_MOVE2", "SFX_KUPPA_JUMP1", "SFX_KUPPA_LANDING1", "SFX_KUPPA_HIP_DROP1",
         "SFX_KUPPA_HIP_DROP2"},
    };
    static int ids[PORT_PLAY_AS_COUNT][SFX_KINDS];
    static int mario[SFX_KINDS];
    static int ready;
    int i, c, chr = portPlayAs();

    if (portFireBreathMuteSfx(id)) {
        return id & ~0x1FFF; /* (entry 0: no sound, as with effects off) */
    }
    if (chr == PORT_PLAY_AS_MARIO) {
        if (!portFunOn(PORT_FUN_LUIGI_VOICE)) {
            return id;
        }
        chr = PORT_PLAY_AS_LUIGI; /* (the fun toggle: Luigi's voice alone, on Mario) */
    }
    if (!ready) {
        for (i = 0; i < SFX_KINDS; i++) {
            mario[i] = searchPSSFXList(kMario[i]);
            for (c = 1; c < PORT_PLAY_AS_COUNT; c++) {
                ids[c][i] = kSfx[c][i] != NULL ? searchPSSFXList(kSfx[c][i]) : 0;
            }
        }
        ready = 1;
    }
    for (i = 0; i < SFX_KINDS; i++) {
        /* (0: a name the table doesn't have) */
        if (mario[i] != 0 && ids[chr][i] != 0 && (id & 0x1FFF) == mario[i]) {
            if (trace()) {
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
        if (portCosmeticFindAnim(animPoseGetAnimBaseDataPtr(pose), want) == NULL) {
            if (model == NPC_E_MARIO) {
                model = NPC_A_MARIO; /* (e_mario has no stand) */
                want = "M_S_1";
                goto load;
            }
            want = model == NPC_LUIGI ? kChars[chr].front.stand : model == NPC_A_MARIO_R ? "M_S_1R" : "M_S_1";
        }
        if (trace()) {
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

/* the other player's ghost ("sync what matters" online, statesync_ghost_pc.c): drawn as their pick
 * chr, from this game's own models of it */
const char* portPlayAsGhostModel(int chr, const char* mario) {
    if (chr <= PORT_PLAY_AS_MARIO || chr >= PORT_PLAY_AS_COUNT || mario == NULL) {
        return NULL;
    }
    return charModel(chr, isBack(mario) && hasBack(chr));
}

const char* portPlayAsGhostAnim(int chr, const char* mario, int pose) {
    int back, b;
    if (chr <= PORT_PLAY_AS_MARIO || chr >= PORT_PLAY_AS_COUNT || mario == NULL) {
        return NULL;
    }
    back = isBack(mario) && hasBack(chr);
    return charAnim(chr, PORT_LUIGI_FIELD, back ? -1 : pose, back ? pose : -1, mario, &b);
}

float portPlayAsScale(int chr) {
    return chr > PORT_PLAY_AS_MARIO && chr < PORT_PLAY_AS_COUNT ? kChars[chr].scale : 1.0f;
}

float portLuigiCreditScale(const char* model) {
    const int chr = portPlayAs();
    return chr != PORT_PLAY_AS_MARIO && model != NULL && !strcmp(model, "a_mario") ? kChars[chr].scale : 1.0f;
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
