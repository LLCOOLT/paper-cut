/* Scripted controller input ($TTYD_INPUT=<file>), for tests and for replaying
 * a bug: drives controller 1 frame by frame, applied over the backend's own
 * read (src/sdk/DEMOPad.c calls portInputApply after PADRead).
 *
 * The script starts at the first frame, or, with the $TTYD_MAP warp, once the
 * map is reached (portInputStart). One command per line; '#' comments:
 *   wait N              no input for N frames
 *   hold KEYS N         hold KEYS for N frames; KEYS joined with '+':
 *                       a b x y z l r start, left right up down (main stick,
 *                       full tilt), dleft dright dup ddown (d-pad),
 *                       cleft cright cup cdown (C-stick)
 *   press KEYS          hold KEYS 2 frames, then release for 2
 *   stick X Y N         main stick at X, Y (-128..127) for N frames
 *   shot NAME           capture this frame as NAME.png (Aurora, $TTYD_SHOT_DIR)
 *   battle              start a battle with the nearest enemy NPC, as a map
 *                       script would (evt_npc_battle_start)
 *   hp N                set Mario's HP (pouchSetHP), e.g. 1 to reach a Game Over
 *   wait_seq N [MAX]    no input until the game sequence is N (seqdrv.h:
 *                       1 title, 2 field, 4 battle, 6 file select), at most
 *                       MAX frames (default 3600), else exit 5
 *   wait_map NAME [MAX] no input until NAME is the current map, as wait_seq
 *   press_until_seq KEYS N [MAX]  press KEYS every 30 frames until sequence N
 *   press_until_map KEYS NAME [MAX]  the same, until NAME is the current map
 *                       (screens that ignore input during their intro)
 *   end                 exit (0) once any capture is written
 * Each command is logged as "[input] frame F: <line>". */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif
#include <dolphin/os.h>
#include "port/platform.h"
#include "driver/npcdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "driver/seqdrv.h"

#define MAX_LINES 1024

typedef struct Step {
    char text[96];
    u16 buttons;
    s8 stickX, stickY, substickX, substickY;
    u8 trigL, trigR;
    s32 frames; /* -1: shot, -2: end, -3: battle, -4: hp, -5: wait_seq, -6: wait_map */
    char name[64];
    s32 waitMax; /* wait_seq / wait_map */
} Step;

static s32 sWaitFrames = -1; /* frames spent in the current wait_seq / wait_map, -1: none */

static Step sSteps[MAX_LINES];
static s32 sCount, sCur = -1, sLeft;
static int sState = -1; /* -1 unread, 0 off, 1 loaded, 2 running, 3 ended */
static u32 sFrame;

static BOOL parseKeys(Step* s, char* keys) {
    char* k;
    for (k = strtok(keys, "+"); k != NULL; k = strtok(NULL, "+")) {
        static const struct { const char* name; u16 button; s8 sx, sy, cx, cy; u8 l, r; } kKeys[] = {
            {"a", PAD_BUTTON_A}, {"b", PAD_BUTTON_B}, {"x", PAD_BUTTON_X}, {"y", PAD_BUTTON_Y},
            {"z", PAD_TRIGGER_Z}, {"l", PAD_TRIGGER_L, 0, 0, 0, 0, 255, 0}, {"r", PAD_TRIGGER_R, 0, 0, 0, 0, 0, 255},
            {"start", PAD_BUTTON_START},
            {"left", 0, -127, 0}, {"right", 0, 127, 0}, {"up", 0, 0, 127}, {"down", 0, 0, -127},
            {"dleft", PAD_BUTTON_LEFT}, {"dright", PAD_BUTTON_RIGHT}, {"dup", PAD_BUTTON_UP},
            {"ddown", PAD_BUTTON_DOWN},
            {"cleft", 0, 0, 0, -127, 0}, {"cright", 0, 0, 0, 127, 0}, {"cup", 0, 0, 0, 0, 127},
            {"cdown", 0, 0, 0, 0, -127},
        };
        size_t i;
        for (i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++) {
            if (strcmp(k, kKeys[i].name) == 0) {
                s->buttons |= kKeys[i].button;
                if (kKeys[i].sx) s->stickX = kKeys[i].sx;
                if (kKeys[i].sy) s->stickY = kKeys[i].sy;
                if (kKeys[i].cx) s->substickX = kKeys[i].cx;
                if (kKeys[i].cy) s->substickY = kKeys[i].cy;
                if (kKeys[i].l) s->trigL = kKeys[i].l;
                if (kKeys[i].r) s->trigR = kKeys[i].r;
                break;
            }
        }
        if (i == sizeof(kKeys) / sizeof(kKeys[0])) {
            return FALSE;
        }
    }
    return TRUE;
}

static void load(const char* path) {
    FILE* f = fopen(path, "r");
    char line[128];
    int n = 0;
    if (f == NULL) {
        fprintf(stderr, "TTYD_INPUT: cannot open %s\n", path);
        exit(2);
    }
    while (fgets(line, sizeof(line), f) && sCount < MAX_LINES - 1) {
        char cmd[16] = "", a1[64] = "", a2[16] = "", a3[16] = "";
        char* hash = strchr(line, '#');
        Step* s = &sSteps[sCount];
        n++;
        if (hash) *hash = '\0';
        if (sscanf(line, "%15s %63s %15s %15s", cmd, a1, a2, a3) < 1) {
            continue;
        }
        memset(s, 0, sizeof(*s));
        snprintf(s->text, sizeof(s->text), "%s %s %s %s", cmd, a1, a2, a3);
        if (strcmp(cmd, "wait") == 0) {
            s->frames = atoi(a1);
        } else if (strcmp(cmd, "hold") == 0) {
            s->frames = atoi(a2);
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "press") == 0) {
            Step* rel = &sSteps[sCount + 1];
            s->frames = 2;
            if (!parseKeys(s, a1)) goto bad;
            memset(rel, 0, sizeof(*rel));
            snprintf(rel->text, sizeof(rel->text), "(release)");
            rel->frames = 2;
            sCount++;
        } else if (strcmp(cmd, "stick") == 0) {
            s->stickX = (s8)atoi(a1);
            s->stickY = (s8)atoi(a2);
            s->frames = atoi(a3);
        } else if (strcmp(cmd, "shot") == 0) {
            s->frames = -1;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "battle") == 0) {
            s->frames = -3;
        } else if (strcmp(cmd, "wait_seq") == 0 || strcmp(cmd, "wait_map") == 0) {
            s->frames = cmd[5] == 's' ? -5 : -6;
            snprintf(s->name, sizeof(s->name), "%s", a1);
            s->waitMax = a2[0] ? atoi(a2) : 3600;
        } else if (strcmp(cmd, "press_until_seq") == 0 || strcmp(cmd, "press_until_map") == 0) {
            /* a wait_seq / wait_map that also presses KEYS every 30 frames */
            s->frames = strcmp(cmd, "press_until_seq") == 0 ? -5 : -6;
            snprintf(s->name, sizeof(s->name), "%s", a2);
            s->waitMax = a3[0] ? atoi(a3) : 3600;
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "hp") == 0) {
            s->frames = -4;
            s->stickX = 0;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "pause") == 0) {
            s->frames = -7;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "end") == 0) {
            s->frames = -2;
        } else {
            goto bad;
        }
        sCount++;
        continue;
    bad:
        fprintf(stderr, "TTYD_INPUT: %s:%d: cannot parse '%s'\n", path, n, line);
        exit(2);
    }
    fclose(f);
}

/* pause MS: as the window being minimized for MS of real time (portPause);
 * reports how far the game clock moved meanwhile (should be ~0). */
static void pauseFor(int ms) {
    OSTime before;
    portPause();
    before = OSGetTime();
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    {
        struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000};
        nanosleep(&ts, NULL);
    }
#endif
    printf("[input] paused %d ms; game clock moved %lld us\n", ms,
           (long long)OSTicksToMicroseconds(OSGetTime() - before));
    fflush(stdout);
    portResume();
}

static char sBattleNpc[32];

EVT_BEGIN(input_battle_evt)
    CALL(evt_npc_battle_start, PTR(sBattleNpc), 0)
    RETURN
EVT_END()

/* The nearest live NPC with a battle group (an enemy), as a map script names it. */
static void startBattle(void) {
    NpcWork* wp = npcGetWorkPtr();
    MarioWork* mario = marioGetPtr();
    NpcEntry* best = NULL;
    f32 bestDist = 1e30f;
    s32 i;
    for (i = 0; i < wp->count; i++) {
        NpcEntry* npc = &wp->entries[i];
        f32 dx, dz;
        /* as the field battle check (npcdrv.c): a battle group and a defeat
         * script; story NPCs with only a group would fight out of context */
        if (!(npc->flags & 1) || npc->battleInfo.groupSetup == NULL || npc->deadEvent == NULL) {
            continue;
        }
        dx = npc->position.x - mario->position.x;
        dz = npc->position.z - mario->position.z;
        if (dx * dx + dz * dz < bestDist) {
            bestDist = dx * dx + dz * dz;
            best = npc;
        }
    }
    if (best == NULL) {
        printf("[input] battle: no enemy NPC on this map\n");
        fflush(stdout);
        return;
    }
    snprintf(sBattleNpc, sizeof(sBattleNpc), "%s", best->description);
    printf("[input] battle with '%s'\n", sBattleNpc);
    fflush(stdout);
    evtEntry(input_battle_evt, 0, 0);
}

BOOL portInputActive(void) {
    if (sState < 0) {
        const char* path = getenv("TTYD_INPUT");
        sState = 0;
        if (path != NULL && *path) {
            load(path);
            sState = 1;
        }
    }
    return sState > 0;
}

/* The map warp calls this once the map is reached; without the warp the
 * script starts on the first frame. */
void portInputStart(void) {
    if (portInputActive() && sState == 1) {
        sState = 2;
    }
}

void portInputApply(PADStatus* pad) {
    const Step* s;
    if (!portInputActive()) {
        return;
    }
    sFrame++;
    if (sState == 1 && getenv("TTYD_MAP") == NULL) {
        sState = 2;
    }
    if (sState != 2) {
        return;
    }
    if (sWaitFrames >= 0) {
        /* wait_seq / wait_map: neutral input until the condition holds */
        const Step* w = &sSteps[sCur];
        BOOL met = w->frames == -5 ? (s32)seqGetSeq() == atoi(w->name)
                                   : gp != NULL && strcmp(gp->currentMapName, w->name) == 0;
        if (!met) {
            if (++sWaitFrames > w->waitMax) {
                printf("[input] FAIL: %s not reached in %d frames (sequence %d, map '%s')\n", w->text,
                       (int)w->waitMax, (int)seqGetSeq(), gp != NULL ? gp->currentMapName : "");
                fflush(stdout);
                _Exit(5);
            }
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            if (sWaitFrames % 30 < 2) { /* press_until_seq: a 2-frame press every 30 */
                pad[0].button = w->buttons;
                pad[0].stickX = w->stickX;
                pad[0].stickY = w->stickY;
            }
            return;
        }
        printf("[input] frame %u: reached after %d frames\n", (unsigned)sFrame, (int)sWaitFrames);
        fflush(stdout);
        sWaitFrames = -1;
    }
    while (sLeft <= 0) {
        if (sCur >= 0 && sSteps[sCur].frames == -2) {
            /* end: wait for a pending capture, then leave */
            if (!portShotPending()) {
                printf("[input] end\n");
                fflush(stdout);
                _Exit(0);
            }
            return;
        }
        if (++sCur >= sCount) {
            sState = 3;
            return;
        }
        s = &sSteps[sCur];
        printf("[input] frame %u: %s\n", (unsigned)sFrame, s->text);
        fflush(stdout);
        if (s->frames == -1) {
            /* with the map warp (map_sweep --input), named <map>__<NAME> */
            const char* map = getenv("TTYD_MAP");
            char name[128];
            if (map != NULL && *map) {
                /* the map's name without the entrance ("eki_02,s_bero") */
                snprintf(name, sizeof(name), "%.*s__%s", (int)strcspn(map, ","), map, s->name);
            } else {
                snprintf(name, sizeof(name), "%s", s->name);
            }
            portShotRequest(name);
            continue;
        }
        if (s->frames == -3) {
            startBattle();
            continue;
        }
        if (s->frames == -5 || s->frames == -6) {
            sWaitFrames = 0;
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            return;
        }
        if (s->frames == -7) {
            pauseFor(atoi(s->name));
            continue;
        }
        if (s->frames == -4) {
            pouchSetHP((s16)atoi(s->name)); /* e.g. 1, so the next hit ends in a Game Over */
            continue;
        }
        if (s->frames == -2) {
            return;
        }
        sLeft = s->frames;
    }
    s = &sSteps[sCur];
    pad[0].err = PAD_ERR_NONE;
    pad[0].button = s->buttons;
    pad[0].stickX = s->stickX;
    pad[0].stickY = s->stickY;
    pad[0].substickX = s->substickX;
    pad[0].substickY = s->substickY;
    pad[0].triggerLeft = s->trigL;
    pad[0].triggerRight = s->trigR;
    pad[0].analogA = (s->buttons & PAD_BUTTON_A) ? 255 : 0;
    pad[0].analogB = (s->buttons & PAD_BUTTON_B) ? 255 : 0;
    sLeft--;
}
