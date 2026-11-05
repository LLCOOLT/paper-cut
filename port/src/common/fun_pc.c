/* Fun toggles (port/include/port/toggles.h): silly switches on the Modes tab.
 * Big heads, paper-thin and spinning only change how models are drawn (the
 * matrices the draw uses, after anything the game keeps: world positions,
 * a paper character's turn), so they never touch the game's state. Moon
 * gravity changes play: online it is the host's, as every setting that
 * changes play. Luigi's voice is the sound alone. */
#include "port/toggles.h"
#include "port/settings.h"
#include "port/platform.h"
#include "driver/animdrv.h"
#include <dolphin/os.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const kKeys[PORT_FUN_COUNT] = {"fun_big_heads", "fun_moon_gravity", "fun_paper_thin",
                                                  "fun_luigi_voice", "fun_spin"};
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
