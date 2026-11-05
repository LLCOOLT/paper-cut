/* Fun toggles (port/include/port/toggles.h): silly switches on the Modes tab.
 * Big heads, paper-thin and spinning only change how models are drawn (the
 * matrices the draw uses, after anything the game keeps: world positions,
 * a paper character's turn), so they never touch the game's state. Moon
 * gravity changes play: online it is the host's, as every setting that
 * changes play. Luigi's voice is the sound alone. */
#include "port/toggles.h"
#include "port/settings.h"
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
        /* the wall clock's, not the game's: the same turn at any frame rate */
        const float seconds = (float)(OSTicksToMilliseconds(OSGetTime()) % 2000000) / 1000.0f;
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

/* A group named for a head (or a face), in English or the Japanese the
 * models are named in; only the topmost of a subtree, so a face inside a head
 * isn't grown twice. Found once per model and kept. */
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
} HeadEntry;
static HeadEntry sHeads[HEAD_CACHE];

static void findHeads(const AnimationPoseData* data, int group, int underHead, HeadEntry* e, int depth) {
    while (group >= 0 && group < data->groupCount && depth < 64) {
        const AnimPoseGroup* g = &data->groups[group];
        int head = 0;
        if (!underHead && isHeadName(g->name) && e->count < HEADS_MAX) {
            e->groups[e->count++] = group;
            head = 1;
        }
        findHeads(data, g->childGroupId, underHead || head, e, depth + 1);
        group = g->brotherGroupId;
        depth++;
    }
}

static const HeadEntry* heads(const AnimationPoseData* data) {
    HeadEntry* e = &sHeads[((size_t)data >> 4) % HEAD_CACHE];
    if (e->data != data || strncmp(e->file, data->fileName, sizeof(e->file)) != 0) {
        memset(e, 0, sizeof(*e));
        e->data = data;
        snprintf(e->file, sizeof(e->file), "%s", data->fileName);
        if (isCharacter(data->fileName)) {
            findHeads(data, data->groupCount - 1, 0, e, 0); /* dispProc's root */
        }
        if (getenv("TTYD_FUN_TRACE") != NULL) {
            int i;
            printf("[fun] %s: %d head group(s)", data->fileName, e->count);
            for (i = 0; i < e->count; i++) {
                printf(" %s", data->groups[e->groups[i]].name);
            }
            printf("\n");
            fflush(stdout);
        }
    }
    return e;
}

int portFunHeadScale(const void* poseData, int groupId, float* scale) {
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
            return 1;
        }
    }
    return 0;
}
