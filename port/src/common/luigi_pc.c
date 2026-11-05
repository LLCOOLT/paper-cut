/* Play as Luigi (port/include/port/luigi.h, docs/PLAYABLE_LUIGI.md): a
 * stand-in pose of Luigi's field model for Mario's, in the field and for
 * Mario's unit in battle. The state is in the save-state section
 * (CMakeLists.txt), so a save state keeps the pose ids it points at; each
 * frame still checks the pose is the c_luigi this file loaded. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/luigi.h"
#include "port/gameplus.h"
#include "driver/animdrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "pmario_sound.h"
#ifdef TTYD_SAVE_STATES
/* the game keeps pointers to the texts made here: on the save-state heap */
#define PORT_SNAPHEAP_REDIRECT
#include "port/snapheap.h"
#endif

/* Mario's animations (field a_mario / a_mario_r, battle b_mario) Luigi has a
 * version of; the R names are the back-facing ones. */
static const char* const kMap[][2] = {
    {"M_S_1", "S_1"},     {"M_Z_1", "Z_1"},     {"M_W_1", "W_1"},     {"M_R_1", "R_1"},
    {"M_J_1A", "J_1A"},   {"M_J_1B", "J_1B"},   {"M_J_1C", "J_1B"},   {"M_I_2", "T_1"},
    {"M_I_3", "T_1"},     {"M_S_1R", "S_1R"},   {"M_Z_1R", "Z_1R"},   {"M_W_1R", "W_1R"},
    {"M_R_1R", "R_1R"},   {"M_J_1BR", "J_1BR"}, {"M_J_1CR", "J_1BR"}, {"M_I_2R", "T_1R"},
    {"M_I_3R", "T_1R"},
};

/* the disc's model, or ours made from it at startup with our animations and
 * his hammer (port/src/aurora/luigi_model_aurora.c, docs/LUIGI_ANIMS.md) */
#define LUIGI_MODEL (sModelReady ? "c_luigi_pc" : "c_luigi")

static int sModelReady;

/* per place (field, battle, the menus' two figures): the Mario pose stood in
 * for (-1: none), Luigi's pose (-1: not loaded) and the animation it plays */
#define SLOTS 5
static struct {
    int source;
    int pose;
    char anim[60];
} sStandIn[SLOTS] = {{-1, -1, ""}, {-1, -1, ""}, {-1, -1, ""}, {-1, -1, ""}, {-1, -1, ""}};

int portLuigiOn(void) {
    return portGameplus("playable_luigi");
}

void portLuigiSetModelReady(int ready) {
    sModelReady = ready;
}

int portLuigiModelReady(void) {
    return sModelReady;
}

/* Luigi's version of Mario's animation: one of the same name in Luigi's
 * model (ours are named as Mario's), else the disc's by kMap. In battle,
 * b_mario reuses some of a_mario's names for other motions (M_H_2: a
 * battle hammer attack), so there a field animation of Luigi's is used only
 * where Mario's two are the same animation; a battle one of Luigi's is
 * named "<name>@b". */
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

static const char* luigiAnim(int where, int pose, const char* mario) {
    AnimationPoseData* d;
    const char* found;
    char battle[64];
    size_t i;
    if (mario == NULL) {
        return NULL;
    }
    if (pose >= 0) {
        d = animPoseGetAnimBaseDataPtr(pose);
        if (where == PORT_LUIGI_BATTLE || where == PORT_LUIGI_VERSUS) {
            snprintf(battle, sizeof(battle), "%s@b", mario);
            if ((found = findAnim(d, battle)) != NULL) {
                return found;
            }
            for (i = 0; i < sizeof(kSameInBattle) / sizeof(kSameInBattle[0]); i++) {
                if (!strcmp(kSameInBattle[i], mario) && (found = findAnim(d, mario)) != NULL) {
                    return found;
                }
            }
        } else if ((found = findAnim(d, mario)) != NULL) {
            return found;
        }
    }
    for (i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++) {
        if (!strcmp(kMap[i][0], mario)) {
            return kMap[i][1];
        }
    }
    return NULL;
}

/* the pool type Luigi's pose is loaded with: field poses as Mario's (shared,
 * kept across maps), battle ones freed at the battle's end, the menus' as
 * the menus' own figures (field) */
static int poolType(int where) {
    return where == PORT_LUIGI_BATTLE || where == PORT_LUIGI_VERSUS ? 1 : where == PORT_LUIGI_FIELD ? 2 : 0;
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
    /* (versus: Luigi's side's unit is always Luigi) */
    int on = portLuigiOn() || where == PORT_LUIGI_VERSUS;

    /* the menus' figures: Mario's own model only (not 8-bit Mario's d_mario,
     * whose animations have names of Luigi's) */
    if (where >= PORT_LUIGI_MENU && poseId >= 0 &&
        strncmp(animPoseGetAnimBaseDataPtr(poseId)->fileName, "a_mario", 7) != 0) {
        allowed = 0;
    }
    if (!poseValid(where, sStandIn[where].pose)) {
        sStandIn[where].pose = -1;
    }
    src = NULL;
    if (on && allowed && poseId >= 0) {
        src = animPoseGetAnimPosePtr(poseId);
        /* (a paper form, plane, boat, spin, ..., is drawn from a capture of
         * the pose: Luigi's while he stands in, animPaperPoseDispSub) */
        if (sStandIn[where].pose < 0) {
            sStandIn[where].pose = animPoseEntry(LUIGI_MODEL, poolType(where));
            sStandIn[where].anim[0] = '\0';
            if (sStandIn[where].pose < 0) {
                sStandIn[where].pose = -1;
            }
        }
        anim = luigiAnim(where, sStandIn[where].pose, animPoseGetCurrentAnim(poseId)->name);
        if (getenv("TTYD_LUIGI_TRACE") != NULL) {
            static const char* const kWhere[SLOTS] = {"field", "battle", "menu", "journal", "versus"};
            static char last[SLOTS][64];
            char now[64];
            snprintf(now, sizeof(now), "%s %d %s", animPoseGetCurrentAnim(poseId)->name, sStandIn[where].pose,
                     anim != NULL ? anim : (src->paperId != -1 ? "(paper)" : "-"));
            if (strcmp(now, last[where]) != 0) {
                printf("[luigi] %s: Mario %s\n", kWhere[where], now);
                fflush(stdout);
                strcpy(last[where], now);
            }
        }
    }
    if (anim == NULL || sStandIn[where].pose < 0) {
        sStandIn[where].source = -1;
        if (!on && sStandIn[where].pose >= 0) {
            animPoseRelease(sStandIn[where].pose);
            sStandIn[where].pose = -1;
        }
        return;
    }
    dst = animPoseGetAnimPosePtr(sStandIn[where].pose);
    if (sStandIn[where].source != poseId || strcmp(sStandIn[where].anim, anim) != 0) {
        animPoseSetAnim(sStandIn[where].pose, anim, TRUE);
        strncpy(sStandIn[where].anim, anim, sizeof(sStandIn[where].anim) - 1);
    }
    sStandIn[where].source = poseId;
    /* Mario's clock: the game sets his time to hold or scrub a pose (the spin
     * hammer's charge, events), and Luigi's versions keep Mario's timing */
    if (dst->type == src->type) {
        dst->localTime = src->localTime;
    }
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

void portLuigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale) {
    AnimationPose* src;
    AnimationPose* dst;
    int where;
    int npc;

    if ((npc = npcStandIn(poseId)) >= 0) {
        copyMaterial(animPoseGetAnimPosePtr(npc), animPoseGetAnimPosePtr(poseId));
        animPoseDrawMtx(npc, mtx, xluMode, rotY, scale);
        return;
    }
    for (where = 0; where < SLOTS; where++) {
        /* (a battle's stand-in stays recorded after the battle freed it) */
        if (poseId >= 0 && sStandIn[where].source == poseId && poseValid(where, sStandIn[where].pose) &&
            animPoseGetAnimPosePtr(poseId)->paperId == -1) {
            src = animPoseGetAnimPosePtr(poseId);
            dst = animPoseGetAnimPosePtr(sStandIn[where].pose);
            copyMaterial(dst, src); /* Mario's, as the game set it for this draw */
            animPoseDrawMtx(sStandIn[where].pose, mtx, xluMode, rotY, scale);
            return;
        }
    }
    animPoseDrawMtx(poseId, mtx, xluMode, rotY, scale);
}

struct AnimationPose* portLuigiPaperPose(struct AnimationPose* pose) {
    int where;
    for (where = 0; where < SLOTS; where++) {
        if (sStandIn[where].source >= 0 && animPoseGetAnimPosePtr(sStandIn[where].source) == pose &&
            poseValid(where, sStandIn[where].pose)) {
            AnimationPose* dst = animPoseGetAnimPosePtr(sStandIn[where].pose);
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
    for (where = PORT_LUIGI_MENU; where <= PORT_LUIGI_LOG; where++) {
        if (poseId >= 0 && sStandIn[where].source == poseId) {
            if (poseValid(where, sStandIn[where].pose)) {
                animPoseRelease(sStandIn[where].pose);
            }
            sStandIn[where].source = -1;
            sStandIn[where].pose = -1;
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
} kNames[] = {
    {"Mario", 'o'}, {"Luigi", 'i'}, {"MARIO", 'O'}, {"LUIGI", 'I'},
    {"\x83\x7D\x83\x8A\x83\x49", 0},             /* マリオ (Shift-JIS) */
    {"\x83\x8B\x83\x43\x81\x5B\x83\x57", 0},     /* ルイージ */
};
#define NAMES ((int)(sizeof(kNames) / sizeof(kNames[0])))
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
        } else if (startsWith(s + k, "\x83\x7D\x83\x93\x83\x56\x83\x87\x83\x93")) {
            return 0;
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
} LuigiText;
static LuigiText* sTexts[1024];

const char* portLuigiText(const char* msg) {
    unsigned int hash = 2166136261u;
    const unsigned char* p;
    LuigiText* t;
    size_t len;
    char* out;

    if (msg == NULL || !portLuigiOn()) {
        return msg;
    }
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
        if (!strcmp(t->in, msg)) {
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
    /* Mario's field sounds on a plain floor and Luigi's (his NPC's: the same
     * sounds, lower, as the game plays them for him) */
    static const char* const kSfx[][2] = {
        {"SFX_MARIO_MOVE1L", "SFX_FLD_NPC_LUIGI_MOVE1L"},
        {"SFX_MARIO_MOVE1R", "SFX_FLD_NPC_LUIGI_MOVE1R"},
        {"SFX_MARIO_JUMP1", "SFX_FLD_NPC_LUIGI_JUMP1"},
        {"SFX_MARIO_LANDING1", "SFX_FLD_NPC_LUIGI_LANDING1"},
    };
    static int ids[4][2];
    static int ready;
    int i;

    if (!portLuigiOn()) {
        return id;
    }
    if (!ready) {
        for (i = 0; i < 4; i++) {
            ids[i][0] = searchPSSFXList(kSfx[i][0]);
            ids[i][1] = searchPSSFXList(kSfx[i][1]);
        }
        ready = 1;
    }
    for (i = 0; i < 4; i++) {
        /* (0: a name the table doesn't have) */
        if (ids[i][0] != 0 && ids[i][1] != 0 && (id & 0x1FFF) == ids[i][0]) {
            if (getenv("TTYD_LUIGI_TRACE") != NULL) {
                printf("[luigi] sound %s for %s\n", kSfx[i][1], kSfx[i][0]);
                fflush(stdout);
            }
            return (id & ~0x1FFF) | ids[i][1];
        }
    }
    return id;
}

/* ------------------------------------------------------- NPCs, credits */

/* The brothers swap in the scenes too: an NPC drawn with c_luigi (Luigi in
 * Mario's house, Rogueport, Poshley Heights, the Pianta Parlor, the
 * digest...) is drawn as Mario, and one drawn with a_mario (the digest's and
 * Doopliss's Mario) as Luigi. As for the player, the NPC's own pose runs as
 * before and a stand-in of the other brother follows it and is drawn instead. */

/* the models a stand-in uses: Mario's front, back and event sets, Luigi's */
enum { NPC_A_MARIO, NPC_A_MARIO_R, NPC_E_MARIO, NPC_LUIGI, NPC_MODELS };

static const char* npcModel(int m) {
    static const char* const kModel[] = {"a_mario", "a_mario_r", "e_mario"};
    return m == NPC_LUIGI ? LUIGI_MODEL : kModel[m];
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

/* the brother an NPC's model swaps to: NPC_A_MARIO (it is Luigi),
 * NPC_LUIGI (it is Mario), -1 (neither) */
static int npcSwap(int poseId) {
    const char* file = animPoseGetAnimBaseDataPtr(poseId)->fileName;
    if (!strcmp(file, "c_luigi")) {
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
    if (poseId < 0 || !portLuigiOn() || animPoseGetAnimPosePtr(poseId)->paperId != -1) {
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

/* Mario's animation for Luigi's anim: *model and the name */
static const char* toMario(const char* anim, int* model) {
    char base[24];
    size_t len = strlen(anim), i;
    int back = len > 1 && len < sizeof(base) && anim[len - 1] == 'R';
    static char name[24];

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
    int s, swap, model, pose, type;

    if (poseId < 0) {
        return;
    }
    if (!portLuigiOn()) {
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
        want = toMario(anim, &model);
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
        want = luigiAnim(PORT_LUIGI_FIELD, pose, anim);
        if (want == NULL) {
            want = "S_1";
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
            want = model == NPC_LUIGI ? "S_1" : model == NPC_A_MARIO_R ? "M_S_1R" : "M_S_1";
        }
        if (getenv("TTYD_LUIGI_TRACE") != NULL) {
            printf("[luigi] npc %d: %s as %s %s\n", poseId, anim, npcModel(model), want);
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
    if (!portLuigiOn()) {
        return;
    }
    if (!strcmp(*model, "a_mario")) {
        *model = LUIGI_MODEL;
        *anim = "W_1";
    } else if (!strcmp(*model, "c_luigi")) {
        *model = "a_mario";
        *anim = "M_W_1";
    }
}
