/* Bring-up trace ($TTYD_TRACE): one line whenever the game sequence or map
 * changes, with the frame number, so a headless run shows how far it got. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dolphin/vi.h>
#include "driver/seqdrv.h"
#include "mario/mariost.h"
#include "manager/evtmgr.h"
#include "evt/evt_cmd.h"
#include "driver/swdrv.h"
#include "port/open_world.h"
#include "mario/mario_party.h"
#include "event.h"
#include "port/platform.h"
#include "port/gameplus.h"
#include "port/toggles.h"
#include "port/practice.h"

/* $TTYD_TRACE=2: every running script, where it is and what it calls. */
static void traceEvents(u32 frame) {
    EventWork* wp = evtGetWork();
    char a[160], b[160];
    s32 i;
    printf("[trace] frame %u: events\n", (unsigned)frame);
    for (i = 0; i < wp->count; i++) {
        EventEntry* e = &wp->entries[i];
        if (!(e->flags & 1)) {
            continue;
        }
        printf("  #%d id %d op %d at %s", (int)i, (int)e->eventId, (int)e->opcode, portSymbolName(e->nextCommand, a, sizeof(a)));
        if (e->opcode == OPCODE_USER_FUNC && e->args != NULL) {
            printf(" -> %s", portSymbolName((void*)e->args[0], b, sizeof(b)));
        }
        /* the flags (stopped 0x?), the clock WAIT_MS reads and its start, the speed: why a wait never ends */
        printf(" flags 0x%x lifetime %lld ms ud %lld/%lld/%lld blocked %d scale %.2f%s\n", (unsigned)e->flags,
               (long long)OSTicksToMilliseconds(e->lifetime), (long long)e->userdata[0], (long long)e->userdata[1],
               (long long)e->userdata[2], (int)e->blocked, (double)e->timescale,
               e->waitingOnEvent ? " (waiting on a child)" : "");
    }
    fflush(stdout);
}

/* $TTYD_TRACE=3: every command run by any script, with its script and the
 * user function it calls (first 4000 commands after a map's REL is linked). */
void portTraceCmd(EventEntry* e) {
    static int level = -1;
    static u32 budget = 4000;
    static const char* filter;
    char a[160], b[160];
    if (level < 0) {
        const char* v = getenv("TTYD_TRACE");
        level = v ? atoi(v) : 0;
        /* $TTYD_TRACE_CMD=<text>[,<text>...]: only scripts whose name contains one;
         * $TTYD_TRACE_BUDGET: how many commands (default 4000) */
        filter = getenv("TTYD_TRACE_CMD");
        if ((v = getenv("TTYD_TRACE_BUDGET")) != NULL) {
            budget = (u32)strtoul(v, NULL, 10);
        }
    }
    if (level < 3 || budget == 0 || gp == NULL || gp->currentMapName[0] == '\0') {
        return;
    }
    portSymbolName(e->currCommand, a, sizeof(a));
    if (filter != NULL) {
        const char* f = filter;
        for (;;) {
            size_t n = strcspn(f, ",");
            snprintf(b, sizeof(b), "%.*s", (int)n, f);
            if (strstr(a, b) != NULL) {
                break;
            }
            if (f[n] == '\0') {
                return;
            }
            f += n + 1;
        }
    }
    budget--;
    printf("[cmd] f %u ev %d op %3d %s", (unsigned)VIGetRetraceCount(), (int)e->eventId, (int)e->opcode, a);
    if (e->opcode == OPCODE_USER_FUNC && e->args != NULL) {
        printf(" -> %s", portSymbolName((void*)e->args[0], b, sizeof(b)));
    }
    printf("\n");
}

/* $TTYD_MAP=<map>[,<bero>]: at the first field map (the opening demo) or the
 * title, start a new game on that map (as seq_load's new game does, plus a
 * partner: $TTYD_PARTY), run $TTYD_EXIT_FRAMES frames there (default 600) and
 * exit 0; exit 3 if the map is not reached within $TTYD_WARP_LIMIT seconds of
 * wall time (default 60). tools/port/map_sweep.py runs every map this way. */
static void warpFrame(u32 frame) {
    static int state = -1; /* -1 unread, 0 off, 1 waiting for the title, 2 warped, 3 on the map */
    static char map[32], bero[32];
    static u32 exitFrames = 600, limit = 60, onMap;
    static time_t start;
    if (state < 0) {
        const char* v = getenv("TTYD_MAP");
        const char* comma;
        state = 0;
        if (v && *v) {
            comma = strchr(v, ',');
            snprintf(map, sizeof(map), "%.*s", comma ? (int)(comma - v) : (int)strlen(v), v);
            snprintf(bero, sizeof(bero), "%s", comma ? comma + 1 : "");
            if ((v = getenv("TTYD_EXIT_FRAMES")) && atoi(v) > 0) {
                exitFrames = (u32)atoi(v);
            }
            if ((v = getenv("TTYD_WARP_LIMIT")) && atoi(v) > 0) {
                limit = (u32)atoi(v);
            }
            state = 1;
            start = time(NULL);
        }
    }
    if (state == 0 || gp == NULL) {
        return;
    }
    /* the title, or the opening demo's map (its narration stalls in $TTYD_FAST runs) */
    if (state == 1 && (seqGetSeq() == SEQ_TITLE || (seqGetSeq() == SEQ_GAME && strcmp(gp->currentMapName, map) != 0))) {
        stg0_00_init();
        marioPartyKill();
        swInit();
        {
            /* the story so far (GSW 0, which map scripts branch on):
             * $TTYD_STORY, default 0 (a new game) */
            const char* v = getenv("TTYD_STORY");
            swByteSet(0, v ? atoi(v) : 0);
        }
        {
            /* a partner, as a save at that point of the game would have
             * ($TTYD_PARTY: party id, default 1 = Goombella; 0 = none;
             * more after commas join too, the first one out) */
            const char* v = getenv("TTYD_PARTY");
            s32 party = v ? atoi(v) : 1;
            if (party > 0 && party < 8) {
                const char* more = v != NULL ? strchr(v, ',') : NULL;
                partyJoin(party);
                for (; more != NULL; more = strchr(more + 1, ',')) {
                    const s32 other = atoi(more + 1);
                    if (other > 0 && other < 8) {
                        partyJoin(other);
                    }
                }
                marioPartyEntry(party);
            }
        }
        {
            /* $TTYD_OPEN_SEED: the file is an open-world randomizer file with this seed
             * (port/include/port/open_world.h), its story as $TTYD_STORY says; never
             * online or in a replay (portOpenTestSeed) */
            const char* v = portOpenTestSeed();
            if (v != NULL) {
                portOpenStamp(v, 0x0F, 0);
                if (getenv("TTYD_OPEN_SELFTEST") != NULL) {
                    portOpenSelfTest();
                }
            }
        }
        printf("[warp] frame %u: to %s%s%s\n", (unsigned)frame, map, bero[0] ? " " : "", bero);
        fflush(stdout);
        seqSetSeq(SEQ_MAP_CHANGE, map, bero[0] ? bero : NULL);
        state = 2;
    } else if (state == 2 && seqGetSeq() == SEQ_GAME && strcmp(gp->currentMapName, map) == 0) {
        state = 3;
        onMap = frame;
        portInputStart(); /* a $TTYD_INPUT script runs from here and decides when to exit */
    } else if (state == 3 && !portInputActive() && frame - onMap >= exitFrames) {
        /* $TTYD_SHOT_DIR: save the last frame as <map>.png first (Aurora) */
        static int shot = 0;
        if (shot == 0 && getenv("TTYD_SHOT_DIR") != NULL) {
            portShotRequest(map);
            shot = 1;
        }
        if (shot && portShotPending() && frame - onMap < exitFrames + 60) {
            return;
        }
        printf("[warp] ok: %s ran %u frames\n", map, (unsigned)exitFrames);
        fflush(stdout);
        _Exit(0); /* no teardown: Aurora's GPU threads are still running */
    }
    if (state < 3 && time(NULL) - start >= (time_t)limit) {
        printf("[warp] FAIL: %s not reached in %us, frame %u (sequence %d, map '%s')\n", map, (unsigned)limit,
               (unsigned)frame, (int)seqGetSeq(), gp->currentMapName);
        fflush(stdout);
        _Exit(3);
    }
}

void portTraceCard(const char* step, s32 result) {
    if (getenv("TTYD_TRACE") != NULL) {
        printf("[card] %s: %d\n", step, (int)result);
        fflush(stdout);
    }
}

void portTraceMsgMissing(const char* tag) {
    static char seen[64][48];
    static int count;
    int i;
    if (getenv("TTYD_TRACE") == NULL || tag == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        if (strncmp(seen[i], tag, sizeof(seen[i]) - 1) == 0) {
            return;
        }
    }
    if (count < 64) {
        snprintf(seen[count++], sizeof(seen[0]), "%s", tag);
    }
    printf("[msg] missing '%s' (map '%s')\n", tag, gp != NULL ? gp->currentMapName : "");
    fflush(stdout);
}

void portTraceFrame(u32 frame) {
    static int enabled = -1;
    portWatchdogFrame();
    warpFrame(frame);
    portGameplusFrame();
    portChaosFrame();    /* chaos mode's timer and effects (port/toggles.h) */
    portPracticeFrame(); /* the speedrun timer, achievements */
    static s32 lastSeq = -2;
    static char lastMap[sizeof(gp->currentMapName)];
    s32 seq;

    if (gp != NULL) {
        /* the crash report's "game:" line, kept current */
        static s32 sCtxSeq = -3;
        static char sCtxMap[sizeof(gp->currentMapName)];
        const s32 s = (s32)seqGetSeq();
        if (s != sCtxSeq || strncmp(sCtxMap, gp->currentMapName, sizeof(sCtxMap)) != 0) {
            char ctx[160];
            sCtxSeq = s;
            memcpy(sCtxMap, gp->currentMapName, sizeof(sCtxMap));
            snprintf(ctx, sizeof(ctx), "sequence %d, map '%.*s', frame %u", (int)s, (int)sizeof(sCtxMap),
                     sCtxMap, (unsigned)frame);
            portCrashSetContext(ctx);
        }
    }
    if (enabled < 0) {
        const char* v = getenv("TTYD_TRACE");
        enabled = v == NULL ? 0 : atoi(v) > 1 ? 2 : 1;
    }
    if (!enabled || gp == NULL) {
        return;
    }
    seq = (s32)seqGetSeq();
    static u32 every;
    if (every == 0) {
        const char* v = getenv("TTYD_TRACE_EVERY");
        every = v != NULL && atoi(v) > 0 ? (u32)atoi(v) : 3000;
    }
    if (enabled > 1 && frame % every == 0) {
        traceEvents(frame);
    }
    if (frame % 600 == 0) {
        printf("[trace] frame %u: dvdError %d renderTime %lld\n", (unsigned)frame, (int)gp->dvdError,
               (long long)gp->renderTime);
        fflush(stdout);
    }
    {
        static s32 lastStory = -1;
        if (swByteGet(0) != lastStory) { /* the story advanced (GSW 0) */
            lastStory = swByteGet(0);
            printf("[trace] frame %u: story %d (map '%.*s')\n", (unsigned)frame, (int)lastStory,
                   (int)sizeof(gp->currentMapName), gp->currentMapName);
            fflush(stdout);
        }
    }
    if (seq != lastSeq || strncmp(lastMap, gp->currentMapName, sizeof(lastMap)) != 0) {
        lastSeq = seq;
        memcpy(lastMap, gp->currentMapName, sizeof(lastMap));
        printf("[trace] frame %u: sequence %d, map '%.*s', area '%.*s', story %d\n", (unsigned)frame, (int)seq,
               (int)sizeof(gp->currentMapName), gp->currentMapName, (int)sizeof(gp->currentAreaName),
               gp->currentAreaName, (int)swByteGet(0));
        fflush(stdout);
    }
}
