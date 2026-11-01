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
#define SLOTS 4
static struct {
    int source;
    int pose;
    char anim[60];
} sStandIn[SLOTS] = {{-1, -1, ""}, {-1, -1, ""}, {-1, -1, ""}, {-1, -1, ""}};

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
        if (where == PORT_LUIGI_BATTLE) {
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
    return where == PORT_LUIGI_BATTLE ? 1 : where == PORT_LUIGI_FIELD ? 2 : 0;
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
            static const char* const kWhere[SLOTS] = {"field", "battle", "menu", "journal"};
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

void portLuigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale) {
    AnimationPose* src;
    AnimationPose* dst;
    int where;

    for (where = 0; where < SLOTS; where++) {
        /* (a battle's stand-in stays recorded after the battle freed it) */
        if (poseId >= 0 && sStandIn[where].source == poseId && poseValid(where, sStandIn[where].pose) &&
            animPoseGetAnimPosePtr(poseId)->paperId == -1) {
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
    for (where = PORT_LUIGI_MENU; where < SLOTS; where++) {
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

/* The player's name in each spelling, Mario's and Luigi's side by side: the
 * name, the letter a shout draws out ("MARIOOO!") and Luigi's for it. */
static const struct {
    const char* mario;
    const char* luigi;
    char stretch, luigiStretch;
} kNames[] = {
    {"Mario", "Luigi", 'o', 'i'},
    {"MARIO", "LUIGI", 'O', 'I'},
    {"\x83\x7D\x83\x8A\x83\x49", "\x83\x8B\x83\x43\x81\x5B\x83\x57", 0, 0}, /* マリオ, ルイージ (Shift-JIS) */
};
#define NAME_JP 2

/* "Mario and Luigi" (and Luigi and Mario) are swapped, not made "Luigi and
 * Luigi": the words between the two names. */
static const char* const kAnd[] = {
    " and ", " AND ", " & ", " et ", " e ", " y ", " und ",
    "\x82\xC6\x81\x40", "\x82\xC6", "\x82\xE2\x81\x40", "\x82\xE2", /* と　, と, や　, や */
};

/* a letter of a word in the Latin languages (ASCII, Latin-1's accented ones) */
static int isLetter(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
}

static int startsWith(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* The name kNames[n].mario starts at s + i as the player's name: not part of
 * a longer word, a game's title ("Paper Mario", "Super Mario Bros.", マリオ
 * ストーリー) or the series' ("MARIO SERIES THEME MUSIC"). *end: past it and a
 * shout's drawn-out letters, *stretch: how many of those. prevWide: the
 * character before is a double-byte one. */
static int nameAt(const char* s, size_t i, int n, int prevWide, size_t* end, int* stretch) {
    const char* name = kNames[n].mario;
    size_t len = strlen(name);
    size_t k;
    const char* after;

    if (strncmp(s + i, name, len) != 0) {
        return 0;
    }
    k = i + len;
    *stretch = 0;
    if (n == NAME_JP) {
        /* ペーパーマリオ; マリオブラザーズ, マリオストーリー, マリオシリーズ, マリオネット */
        static const char* const kNotAfter[] = {"\x83\x75\x83\x89", "\x83\x58\x83\x67", "\x83\x56\x83\x8A",
                                                "\x83\x6C\x83\x62"};
        size_t j;
        if (i >= 8 && memcmp(s + i - 8, "\x83\x79\x81\x5B\x83\x70\x81\x5B", 8) == 0) {
            return 0;
        }
        for (j = 0; j < sizeof(kNotAfter) / sizeof(kNotAfter[0]); j++) {
            if (startsWith(s + k, kNotAfter[j])) {
                return 0;
            }
        }
        *end = k;
        return 1;
    }
    if (i > 0 && !prevWide && isLetter((unsigned char)s[i - 1])) {
        return 0; /* the end of a longer word */
    }
    if (i >= 6 && (s[i - 1] == ' ' || s[i - 1] == '\n') &&
        (strncmp(s + i - 6, "Paper", 5) == 0 || strncmp(s + i - 6, "PAPER", 5) == 0)) {
        return 0; /* the game's own title */
    }
    while (s[k] == kNames[n].stretch) {
        k++;
        (*stretch)++;
    }
    if (isLetter((unsigned char)s[k])) {
        /* "Marios" (German's genitive) is the name; "Mariole", "Marioink" are other words */
        if (*stretch != 0 || (s[k] != 's' && s[k] != 'S') || isLetter((unsigned char)s[k + 1])) {
            return 0;
        }
    }
    if (*stretch == 0 && (s[k] == ' ' || s[k] == '\n')) {
        after = s + k + 1;
        if (startsWith(after, "Bro") || startsWith(after, "BRO") || startsWith(after, "SERIES")) {
            return 0; /* the Mario Bros., Super Mario Bros., the series */
        }
    }
    *end = k;
    return 1;
}

/* Luigi's name kNames[n].luigi at s + i, as a word */
static int luigiAt(const char* s, size_t i, int n, int prevWide) {
    size_t len = strlen(kNames[n].luigi);
    if (strncmp(s + i, kNames[n].luigi, len) != 0) {
        return 0;
    }
    if (n == NAME_JP) {
        return 1;
    }
    return !(i > 0 && !prevWide && isLetter((unsigned char)s[i - 1])) && !isLetter((unsigned char)s[i + len]);
}

/* one of kAnd at s + i: its length, else 0 */
static size_t andAt(const char* s, size_t i, int n) {
    size_t j;
    for (j = 0; j < sizeof(kAnd) / sizeof(kAnd[0]); j++) {
        /* the Japanese ones with the Japanese name only */
        if (((unsigned char)kAnd[j][0] >= 0x80) == (n == NAME_JP) && startsWith(s + i, kAnd[j])) {
            return strlen(kAnd[j]);
        }
    }
    return 0;
}

static void put(char** o, const char* s, size_t len) {
    memcpy(*o, s, len);
    *o += len;
}

/* msg with the player's name as Luigi; out has room for twice msg */
static void substitute(const char* s, char* out) {
    char* o = out;
    size_t i = 0;
    int prevWide = 0;
    int n, st;
    size_t end, a, end2;

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
        for (n = 0; n < (int)(sizeof(kNames) / sizeof(kNames[0])); n++) {
            if (nameAt(s, i, n, prevWide, &end, &st)) {
                /* "Mario and Luigi": "Luigi and Mario" */
                if ((a = andAt(s, end, n)) != 0 && st == 0 && luigiAt(s, end + a, n, 0)) {
                    put(&o, kNames[n].luigi, strlen(kNames[n].luigi));
                    put(&o, s + end, a);
                    put(&o, kNames[n].mario, strlen(kNames[n].mario));
                    i = end + a + strlen(kNames[n].luigi);
                } else {
                    put(&o, kNames[n].luigi, strlen(kNames[n].luigi));
                    for (; st > 0; st--) {
                        *o++ = kNames[n].luigiStretch;
                    }
                    i = end;
                }
                break;
            }
            if (luigiAt(s, i, n, prevWide)) {
                size_t l = strlen(kNames[n].luigi);
                /* "Luigi and Mario": "Mario and Luigi" */
                if ((a = andAt(s, i + l, n)) != 0 && nameAt(s, i + l + a, n, 0, &end2, &st) && st == 0) {
                    put(&o, kNames[n].mario, strlen(kNames[n].mario));
                    put(&o, s + i + l, a);
                    put(&o, kNames[n].luigi, l);
                    i = end2;
                    break;
                }
            }
        }
        if (n < (int)(sizeof(kNames) / sizeof(kNames[0]))) {
            prevWide = n == NAME_JP;
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
    if (strstr(msg, "Mario") == NULL && strstr(msg, "MARIO") == NULL && strstr(msg, kNames[NAME_JP].mario) == NULL) {
        return msg;
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

int portLuigiHidesNpc(unsigned int npcFlags) {
    return (npcFlags & NPC_FLAG_LUIGI) && portLuigiOn();
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
