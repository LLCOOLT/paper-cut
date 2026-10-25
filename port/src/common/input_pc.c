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
 *   give ID             the item, key item or badge ID (pouchGetItem)
 *   join N / party N    partner N joins / joins and comes out (1 Goombella..7)
 *   story N / flag N    the story (GSW 0) set to N / switch GSWF N set
 *   audience N, coins N, sp N, fp N   those set (sp: star power, in 1/100 units)
 *   maxsp N, maxhp N    the maximums set (maxhp also fills HP)
 *   stars MASK          the special moves obtained (bit 0: Sweet Treat ...)
 *   mobj_event NAME     run map object NAME's event, as hitting it would (a
 *                       switch's, a block's)
 *   talk_all [MAX]      talk to every NPC of the map in turn (not those parked
 *                       below the map), as A in front of one does (its talk script), A pressed every 20 frames
 *                       and B now and then to get through; each one at most
 *                       MAX frames (default 900); stops if the map changes
 *   mobj_all [MAX]      the same with every map object's event (switches,
 *                       blocks, chests)
 *   select N|list       open selection window type N (0-18; key items, items,
 *                       badges, partners, ...), or a window of a list of items
 *                       as a locked door's script opens; press keys to close it
 *   random N [SEED]     N frames of made-up play (random walking, A, B, X, ...;
 *                       the same for the same SEED, default 1), to shake out
 *                       crashes and stalls
 *   state save|load N   save state slot N (1-4) saved or loaded at the end of
 *                       this frame (Aurora, port/src/aurora/savestate_aurora.c)
 *   end                 exit (0) once any capture is written
 * Each command is logged as "[input] frame F: <line>". */
#include <ctype.h>
#include <math.h>
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
#include "data/mapdata.h"
#include "driver/hitdrv.h"
#include "driver/swdrv.h"
#include "manager/cardmgr.h"
#include "driver/npcdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "mario/mario_party.h"
#include "driver/mobjdrv.h"
#include "party.h"
EventEntry* mobjRunEvent(MapObjectEntry* entry, EvtWord* eventCode); /* mobjdrv.c */
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
    u32 seed;    /* random: its generator's seed (0: none, plain input) */
} Step;

/* random N [SEED]: N frames of made-up play, the same for the same seed */
static u32 sRand;
static s32 sRandHold;
static PADStatus sRandPad;

static u32 nextRand(void) {
    sRand = sRand * 1664525u + 1013904223u;
    return sRand >> 8;
}

/* A new input every 6-30 frames: mostly walking or running somewhere, often A
 * (talk, check, jump), sometimes B, X, Y, Z, R, L, the C-stick or the d-pad,
 * rarely START; never the GameCube's reset combination (START + B + X). */
static void randomInput(PADStatus* out) {
    if (sRandHold <= 0) {
        static const u16 kButtons[] = {PAD_BUTTON_A, PAD_BUTTON_A, PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X,
                                       PAD_BUTTON_Y, PAD_TRIGGER_Z, PAD_TRIGGER_R, PAD_TRIGGER_L, PAD_BUTTON_LEFT,
                                       PAD_BUTTON_RIGHT, PAD_BUTTON_UP, PAD_BUTTON_DOWN};
        const u32 r = nextRand() % 100;
        memset(&sRandPad, 0, sizeof(sRandPad));
        if (r < 70) {
            const u32 a = nextRand() % 360;
            const f32 tilt = nextRand() % 3 == 0 ? 60.0f : 120.0f;
            sRandPad.stickX = (s8)(tilt * cosf(a * 0.017453292f));
            sRandPad.stickY = (s8)(tilt * sinf(a * 0.017453292f));
        }
        if (nextRand() % 100 < 45) {
            sRandPad.button |= kButtons[nextRand() % (sizeof(kButtons) / sizeof(kButtons[0]))];
        }
        if (nextRand() % 400 == 0) { /* about once a minute: mostly field play */
            sRandPad.button |= PAD_BUTTON_START;
            sRandPad.button &= ~(PAD_BUTTON_B | PAD_BUTTON_X);
        }
        if (nextRand() % 100 < 5) {
            sRandPad.substickX = (s8)((s32)(nextRand() % 255) - 127);
        }
        sRandHold = 6 + (s32)(nextRand() % 25);
    }
    sRandHold--;
    *out = sRandPad;
    out->err = PAD_ERR_NONE;
    out->triggerLeft = (sRandPad.button & PAD_TRIGGER_L) ? 255 : 0;
    out->triggerRight = (sRandPad.button & PAD_TRIGGER_R) ? 255 : 0;
    out->analogA = (sRandPad.button & PAD_BUTTON_A) ? 255 : 0;
    out->analogB = (sRandPad.button & PAD_BUTTON_B) ? 255 : 0;
}

static s32 sWaitFrames = -1; /* frames spent in the current wait_seq / wait_map, -1: none */

/* talk_all / mobj_all: the item being run, -1 when none */
MapObjectWork* portMobjGetWork(void); /* mobjdrv.c */
EventEntry* mobjRunEvent(MapObjectEntry* entry, EvtWord* eventCode);
static s32 sSweepKind; /* 0 none, 1 talk, 2 mobj */
static s32 sSweepIndex, sSweepEvt, sSweepFrames, sSweepMax, sSweepDone;
static NpcEntry* sSweepNpc;
static char sSweepMap[32];

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

/* port/src/aurora/savestate_aurora.c defines it; the headless build has no save states */
__attribute__((weak)) void portSaveStateRequest(int slot, int load) {
    (void)slot;
    (void)load;
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
        char cmd[32] = "", a1[64] = "", a2[16] = "", a3[16] = "";
        char* hash = strchr(line, '#');
        Step* s = &sSteps[sCount];
        n++;
        if (hash) *hash = '\0';
        if (sscanf(line, "%31s %63s %15s %15s", cmd, a1, a2, a3) < 1) {
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
            snprintf(s->name, sizeof(s->name), "%s", a1); /* optional: an entry of the area's table */
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
        } else if (strcmp(cmd, "save") == 0) {
            /* save: the open file, here and now, as a Save Block does
             * (cardWrite); waits until the card is done. A checkpoint: a later
             * run resumes with load_file.txt */
            s->frames = -10;
            s->waitMax = a1[0] ? atoi(a1) : 1200;
        } else if (strcmp(cmd, "press_until_story") == 0) {
            /* press KEYS every 30 frames until the story (GSW 0) reaches N */
            s->frames = -11;
            snprintf(s->name, sizeof(s->name), "%s", a2);
            s->waitMax = a3[0] ? atoi(a3) : 3600;
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "press_until_control") == 0) {
            /* press KEYS every 30 frames until Mario can be moved (cutscenes
             * and messages over) and has been for a second */
            s->frames = -9;
            s->waitMax = a2[0] ? atoi(a2) : 3600;
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "hp") == 0) {
            s->frames = -4;
            s->stickX = 0;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "goto") == 0) {
            /* goto HIT: Mario onto a hit object (an exit's zone, a door), as
             * evt_mario_set_pos does; the script then walks or presses into it */
            s->frames = -8;
            if (a3[0]) { /* goto X Y Z: a position */
                snprintf(s->name, sizeof(s->name), "%s %s %s", a1, a2, a3);
            } else {
                snprintf(s->name, sizeof(s->name), "%s", a1);
            }
        } else if (strcmp(cmd, "pause") == 0) {
            s->frames = -7;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "give") == 0 || strcmp(cmd, "join") == 0 || strcmp(cmd, "party") == 0 ||
                   strcmp(cmd, "story") == 0 || strcmp(cmd, "flag") == 0 || strcmp(cmd, "audience") == 0 ||
                   strcmp(cmd, "coins") == 0 || strcmp(cmd, "sp") == 0 || strcmp(cmd, "fp") == 0 ||
                   strcmp(cmd, "maxsp") == 0 || strcmp(cmd, "maxhp") == 0 || strcmp(cmd, "stars") == 0) {
            /* state set-up for a test, at once */
            s->frames = -13;
            snprintf(s->name, sizeof(s->name), "%s", cmd);
            s->waitMax = (s32)strtol(a1, NULL, 0);
        } else if (strcmp(cmd, "random") == 0) {
            s->frames = atoi(a1);
            s->seed = a2[0] ? (u32)strtoul(a2, NULL, 0) : 1u;
            if (s->seed == 0) {
                s->seed = 1;
            }
        } else if (strcmp(cmd, "talk_all") == 0 || strcmp(cmd, "mobj_all") == 0) {
            s->frames = cmd[0] == 't' ? -16 : -17;
            s->waitMax = a1[0] ? atoi(a1) : 900;
        } else if (strcmp(cmd, "mobj_event") == 0) {
            s->frames = -15;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "select") == 0) {
            s->frames = -14;
            s->waitMax = strcmp(a1, "list") == 0 ? -1 : atoi(a1);
        } else if (strcmp(cmd, "state") == 0) {
            s->frames = -12;
            s->waitMax = atoi(a2) - 1;
            s->stickX = strcmp(a1, "load") == 0;
            if (strcmp(a1, "load") != 0 && strcmp(a1, "save") != 0) goto bad;
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

/* select N|list: a selection window (winmgr.c's select_desc_tbl type N,
 * or a list of items, as a door or chest asking for a key passes) */
USER_FUNC(evt_win_item_select);
EVT_BEGIN(input_select_evt)
    CALL(evt_win_item_select, 1, LW(0), LW(1), LW(2))
    RETURN
EVT_END()
static s32 sSelectList[] = {0x001F, 0x0021, 0x0001, -1}; /* key items' ids; any list will do */

EVT_BEGIN(input_battle_evt)
    CALL(evt_npc_battle_start, PTR(sBattleNpc), 0)
    RETURN
EVT_END()

/* battle N: entry N of the current area's battle table (relSetBtlAddr),
 * given to the nearest NPC, as a script's evt_npc_set_battle_info does for a
 * scripted fight: every enemy formation can be fought from one map of its
 * area. FALSE (logged) if there is no such entry. */
static BOOL setBattleEntry(NpcEntry* npc, s32 n) {
    char area[4] = {gp->currentMapName[0], gp->currentMapName[1], gp->currentMapName[2], '\0'};
    AreaData* data = areaDataPtr(area);
    NpcSetupBattleInfo* tbl = data != NULL ? (NpcSetupBattleInfo*)data->btlSetupTbl : NULL;
    s32 count = 0;
    while (tbl != NULL && tbl[count].name != NULL) {
        count++;
    }
    if (n < 0 || n >= count) {
        printf("[input] battle: area %s has no battle %d (%d entries)\n", area, (int)n, (int)count);
        fflush(stdout);
        return FALSE;
    }
    npcSetBattleInfo(npc, n);
    printf("[input] battle %d of %s (%d entries)\n", (int)n, area, (int)count);
    return TRUE;
}

/* The nearest live NPC with a battle group (an enemy), as a map script names
 * it; with entry >= 0 the nearest live NPC, which gets that battle. */
static void startBattle(s32 entry) {
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
        if (!(npc->flags & 1) || (entry < 0 && (npc->battleInfo.groupSetup == NULL || npc->deadEvent == NULL))) {
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
        printf("[input] battle: no %s on this map\n", entry < 0 ? "enemy NPC" : "NPC");
        fflush(stdout);
        return;
    }
    if (entry >= 0 && !setBattleEntry(best, entry)) {
        _Exit(6);
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
    if (sSweepKind != 0) {
        /* talk_all / mobj_all */
        memset(&pad[0], 0, sizeof(pad[0]));
        pad[0].err = PAD_ERR_NONE;
        if (gp == NULL || strncmp(gp->currentMapName, sSweepMap, sizeof(sSweepMap)) != 0) {
            printf("[input] %s: the map changed, stopping (%d run)\n", sSweepKind == 1 ? "talk_all" : "mobj_all",
                   (int)sSweepDone);
            fflush(stdout);
            sSweepKind = 0;
            return;
        }
        if (sSweepEvt >= 0) {
            const BOOL running = evtCheckID(sSweepEvt);
            if (running && ++sSweepFrames <= sSweepMax) {
                if (sSweepFrames % 20 < 2) {
                    pad[0].button = PAD_BUTTON_A;
                } else if (sSweepFrames % 90 >= 45 && sSweepFrames % 90 < 47) {
                    pad[0].button = PAD_BUTTON_B;
                }
                return;
            }
            if (running) {
                printf("[input] %s %d: still running after %d frames\n", sSweepKind == 1 ? "talk" : "mobj",
                       (int)sSweepIndex, (int)sSweepMax);
                evtDeleteID(sSweepEvt);
            }
            if (sSweepKind == 1 && sSweepNpc != NULL && (sSweepNpc->flags & 1)) {
                evtStartID(sSweepNpc->regularEvtId);
            }
            marioKeyOn();
            partyKeyOn();
            sSweepEvt = -1;
            sSweepDone++;
            sSweepIndex++;
            return; /* a frame between items */
        }
        for (;;) {
            EventEntry* e = NULL;
            if (sSweepKind == 1) {
                NpcWork* wp = npcGetWorkPtr();
                NpcEntry* npc;
                if (sSweepIndex >= wp->count) {
                    break;
                }
                npc = &wp->entries[sSweepIndex];
                /* parked out of reach (scripts put unused NPCs at y -1000):
                 * a player cannot talk to them, and their scripts may need
                 * set-up that only comes with placing them */
                if (!(npc->flags & 1) || npc->talkEvt == NULL || npc->position.y < -500.0f) {
                    sSweepIndex++;
                    continue;
                }
                printf("[input] talk %d: '%s'\n", (int)sSweepIndex, npc->description);
                evtStopID(npc->regularEvtId);
                marioKeyOff();
                partyKeyOff();
                e = evtEntry(npc->talkEvt, 0, 0);
                e->wNpcEventType = 6;
                e->ownerNPC = npc;
                sSweepNpc = npc;
            } else {
                MapObjectWork* wp = portMobjGetWork();
                MapObjectEntry* mobj;
                if (sSweepIndex >= wp->count) {
                    break;
                }
                mobj = &wp->entries[sSweepIndex];
                if (!(mobj->flags & 1) || mobj->event == 0) {
                    sSweepIndex++;
                    continue;
                }
                printf("[input] mobj %d: '%s'\n", (int)sSweepIndex, mobj->name);
                e = mobjRunEvent(mobj, (EvtWord*)mobj->event);
            }
            fflush(stdout);
            if (e == NULL) {
                sSweepIndex++;
                continue;
            }
            sSweepEvt = e->eventId;
            sSweepFrames = 0;
            return;
        }
        printf("[input] %s: %d run\n", sSweepKind == 1 ? "talk_all" : "mobj_all", (int)sSweepDone);
        fflush(stdout);
        sSweepKind = 0;
    }
    if (sWaitFrames >= 0) {
        /* wait_seq / wait_map: neutral input until the condition holds */
        const Step* w = &sSteps[sCur];
        static s32 sControlFrames;
        BOOL met;
        if (w->frames == -9) {
            sControlFrames = seqGetSeq() == SEQ_GAME && marioChkKey() && marioChkCtrl() ? sControlFrames + 1 : 0;
            met = sControlFrames >= 60;
        } else if (w->frames == -11) {
            met = swByteGet(0) >= atoi(w->name);
        } else if (w->frames == -10) {
            met = sWaitFrames > 0 && !cardIsExec(); /* save: the card is done */
        } else {
            met = w->frames == -5 ? (s32)seqGetSeq() == atoi(w->name)
                                  : gp != NULL && strcmp(gp->currentMapName, w->name) == 0;
        }
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
            /* battle [N]; N may also come from $TTYD_BATTLE_SETUP (map_sweep --battles) */
            const char* n = s->name[0] ? s->name : getenv("TTYD_BATTLE_SETUP");
            startBattle(n != NULL && *n ? atoi(n) : -1);
            continue;
        }
        if (s->frames == -10) {
            printf("[input] save: slot %d, map '%s', story %d\n", (int)gp->saveSlot, gp->currentMapName,
                   (int)swByteGet(0));
            fflush(stdout);
            cardWrite(gp->saveSlot);
        }
        if (s->frames == -5 || s->frames == -6 || s->frames == -9 || s->frames == -10 || s->frames == -11) {
            sWaitFrames = 0;
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            return;
        }
        if (s->frames == -8) {
            Vec pos;
            if (sscanf(s->name, "%f %f %f", &pos.x, &pos.y, &pos.z) != 3) {
                hitObjGetPos(s->name, &pos);
            }
            marioGetPtr()->position = pos;
            continue;
        }
        if (s->frames == -7) {
            pauseFor(atoi(s->name));
            continue;
        }
        if (s->frames == -4) {
            pouchSetHP((s16)atoi(s->name)); /* e.g. 1, so the next hit ends in a Game Over */
            continue;
        }
        if (s->frames == -13) {
            const s32 v = s->waitMax;
            if (strcmp(s->name, "give") == 0) {
                pouchGetItem((ItemType)v);
            } else if (strcmp(s->name, "join") == 0) {
                partyJoin((PartyId)v);
            } else if (strcmp(s->name, "party") == 0) {
                partyJoin((PartyId)v);
                marioPartyKill();
                marioPartyEntry((PartyId)v);
            } else if (strcmp(s->name, "story") == 0) {
                swByteSet(0, v);
            } else if (strcmp(s->name, "flag") == 0) {
                swSet(v);
            } else if (strcmp(s->name, "audience") == 0) {
                pouchSetAudienceNum((f32)v);
            } else if (strcmp(s->name, "coins") == 0) {
                pouchSetCoin(v);
            } else if (strcmp(s->name, "sp") == 0) {
                pouchSetAP((s16)v);
            } else if (strcmp(s->name, "fp") == 0) {
                pouchSetFP((s16)v);
            } else if (strcmp(s->name, "maxsp") == 0) {
                pouchGetPtr()->maximumSP = (s16)v;
            } else if (strcmp(s->name, "maxhp") == 0) {
                pouchSetMaxHP((s16)v);
                pouchSetHP((s16)v);
            } else if (strcmp(s->name, "stars") == 0) {
                pouchGetPtr()->mStarPowersObtained = (u16)v;
            }
            continue;
        }
        if (s->frames == -16 || s->frames == -17) {
            sSweepKind = s->frames == -16 ? 1 : 2;
            sSweepIndex = 0;
            sSweepEvt = -1;
            sSweepDone = 0;
            sSweepMax = s->waitMax;
            sSweepNpc = NULL;
            snprintf(sSweepMap, sizeof(sSweepMap), "%s", gp != NULL ? gp->currentMapName : "");
            return; /* runs from the next frame */
        }
        if (s->frames == -15) {
            MapObjectEntry* mobj = mobjNameToPtrNoAssert(s->name);
            if (mobj == NULL || mobj->event == 0) {
                printf("[input] mobj_event: no map object '%s' with an event\n", s->name);
            } else {
                mobjRunEvent(mobj, (EvtWord*)mobj->event);
            }
            fflush(stdout);
            continue;
        }
        if (s->frames == -14) {
            EventEntry* e = evtEntry(input_select_evt, 0, 0);
            e->lwData[0] = s->waitMax < 0 ? (EvtWord)sSelectList : (EvtWord)s->waitMax;
            continue;
        }
        if (s->frames == -12) {
            portSaveStateRequest(s->waitMax, s->stickX);
            continue;
        }
        if (s->frames == -2) {
            return;
        }
        sLeft = s->frames;
        if (s->seed != 0) {
            sRand = s->seed;
            sRandHold = 0;
        }
    }
    s = &sSteps[sCur];
    if (s->seed != 0) {
        randomInput(&pad[0]);
        sLeft--;
        return;
    }
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
