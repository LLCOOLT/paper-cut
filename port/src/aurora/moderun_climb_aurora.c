/* The Paper Tower climbed by itself (tests): port/include/port/moderun.h's TTYD_TOWER_AUTO.
 *
 * Each walking floor, Mario is walked by the controller from the way in to the way out, the gates on the way
 * met as a player meets them (paper_tower_aurora.c's gatesFrame judges them, as for anyone):
 *   a paper gate, a wind wall   R held from a little before it until past it (Paper mode: Mario side-on)
 *   a tube gate                 stopped just before it, R held (Paper mode) and the stick spun until he rolls
 *                               up (mot_slit's marioRollChgChk: R and three turns of the stick), rolled under,
 *                               then B to stand up again (mot_roll)
 * The pad is written over the frame's key data (gp's, makeKey's) before the game's frame reads it (a pre hook
 * on seqMain), and only while Mario has control on the floor's own map: the battles, the floors' map changes
 * and the climb's end are the game's and the auto-battler's. Nothing happens unless TTYD_TOWER_AUTO is set. */
#include "port/moderun.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/platform.h"
#include "port/postgame.h"
#include "driver/camdrv.h"
#include "driver/hitdrv.h"
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include <dolphin/pad.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

#define PAPER_LEAD 170.0f /* R held from this far before a paper gate */
#define WIND_LEAD 230.0f  /* ... and a wind wall (its gust reaches 70 before its line) */
#define WIND_GUST 70.0f   /* a wind wall's gust each side of its line (paper_tower_aurora.c's WIND_DEPTH) */
#define PAPER_CLOSE 60.0f /* from this far before a paper gate, Paper mode kept even when stuck */
#define WIND_CLOSE 80.0f  /* ... and a wind wall (just outside its gust: stuck before that, Mario stands to
                             jump or go round; floor 23 stuck 94 short of one, in Paper mode) */
#define TUBE_STOP 70.0f   /* stopped this far before a tube gate, to roll up */
#define RUB_STEP 0.52f    /* the stick's turn a frame while rolling up (30 degrees: 12 frames a turn) */
#define RUB_MAX 300       /* frames of spinning before R is let go and it starts again */

static int sOn = -1;      /* 0 off, 1 on, 2 gates off */
static int sMaxFrames = 5400, sStartAt = 700;
static int sStartAsked, sCalm;
static int sFloorNow, sFloorBattle, sFloorFrames, sControl, sSaid;
static int sPassed;       /* this floor's gates passed, in order */
static int sStuck, sTries, sDetour, sWalkFrames;
static Vec sPrev, sHeadPos;
static int sHeadFrames, sHeadLogs;
static int sRubFrames, sRubTries, sLetGo, sBack, sBFrames;
static float sRubAngle;
static u32 sHeld;         /* last frame's buttons (for the new presses) */

static void readEnv(void) {
    const char* v = getenv("TTYD_TOWER_AUTO");
    const char* m = getenv("TTYD_TOWER_AUTO_MAX");
    const char* a = getenv("TTYD_TOWER_AUTO_START");
    sOn = v == NULL || *v == '\0' || strcmp(v, "0") == 0 ? 0 : strcmp(v, "nogates") == 0 ? 2 : 1;
    if (m != NULL && atoi(m) > 0) {
        sMaxFrames = atoi(m);
    }
    if (a != NULL && atoi(a) > 0) {
        sStartAt = atoi(a);
    }
}

int portTowerAutoGatesOff(void) {
    if (sOn < 0) {
        readEnv();
    }
    return sOn == 2;
}

static const char* kindName(int kind) {
    return kind == PORT_TOWER_PAPER ? "paper" : kind == PORT_TOWER_TUBE ? "tube" : "wind";
}

/* how far past its line a gate counts as passed */
static float passMargin(int kind) {
    /* (a paper or tube gate is through once its line is crossed: just past it, as the next gate's gust may
       begin there, hei_05's three gates a quarter of the way apart: rolling on would never get further) */
    return kind == PORT_TOWER_WIND ? WIND_GUST + 10.0f : 12.0f;
}

/* the stick for walking along world direction (dx, dz): by the field camera's facing, as input_pc.c's walk_to */
static void stickFor(float dx, float dz, float tilt, s8* sx, s8* sy) {
    const CameraEntry* cam = camGetPtr(CAMERA_3D);
    float fx = 0.0f, fz = -1.0f, fl;
    if (cam != NULL) {
        fx = cam->cameraAt.x - cam->cameraPos.x;
        fz = cam->cameraAt.z - cam->cameraPos.z;
    }
    fl = sqrtf(fx * fx + fz * fz);
    if (fl < 1e-3f || (fabsf(fx) < 0.02f * fl && fz < 0.0f)) {
        *sx = (s8)(dx * tilt);
        *sy = (s8)(-dz * tilt);
        return;
    }
    fx /= fl;
    fz /= fl;
    *sx = (s8)((dx * -fz + dz * fx) * tilt);
    *sy = (s8)((dx * fx + dz * fz) * tilt);
}

/* the frame's key data, as makeKey would have made it from this controller */
static void setPad(s8 sx, s8 sy, u32 held) {
    gp->stickX[0] = sx;
    gp->stickY[0] = sy;
    gp->button[0] = held;
    gp->buttonNew[0] = held & ~sHeld;
    gp->buttonRepeat[0] = gp->buttonNew[0];
    gp->buttonUp[0] = sHeld & ~held;
    gp->triggerRight[0] = (held & PAD_TRIGGER_R) ? 200 : 0;
    sHeld = held;
}

static void newFloor(const PortTowerAutoFloor* f) {
    if (sFloorNow > 0 && !sFloorBattle) {
        printf("[tower] auto: floor %d crossed in %d frames\n", sFloorNow, sFloorFrames);
        fflush(stdout);
    }
    sFloorNow = f->floor;
    sFloorBattle = f->battle;
    sFloorFrames = sControl = sSaid = sPassed = 0;
    sStuck = sTries = sDetour = sWalkFrames = 0;
    sRubFrames = sRubTries = sLetGo = sBack = sBFrames = 0;
    sHeadFrames = sHeadLogs = 0;
    sHeld = 0;
}

static void drive(const PortTowerAutoFloor* f, MarioWork* m) {
    const char* in = f->reverse ? "e_bero" : "w_bero";
    const char* out = f->reverse ? "w_bero" : "e_bero";
    const int roll = m->currMotionId == MARIO_MOTION_ROLL;
    const int gates = f->gatesLive ? f->gateCount : 0;
    Vec from, to;
    float ux, uz, len, way, dx, dz, s = 0.0f, tubeStop = TUBE_STOP;
    int kind = 0, wantR = 0, stop = 0, rub = 0, pressB = 0, atGate = 0;
    u32 held = 0;
    s8 sx = 0, sy = 0;
    if (hitNameToPtr(in) == NULL || hitNameToPtr(out) == NULL) {
        return;
    }
    hitObjGetPos(in, &from);
    hitObjGetPos(out, &to);
    ux = to.x - from.x;
    uz = to.z - from.z;
    len = sqrtf(ux * ux + uz * uz);
    way = len;
    ux = len > 0.0f ? ux / len : 1.0f;
    uz = len > 0.0f ? uz / len : 0.0f;
    if (!sSaid) {
        char kinds[64] = "";
        int g;
        for (g = 0; g < f->gateCount; g++) {
            snprintf(kinds + strlen(kinds), sizeof(kinds) - strlen(kinds), "%s%s", g ? " " : "",
                     kindName(f->gateKind[g]));
        }
        printf("[tower] auto: floor %d: %s, out by %s, gates %s%s\n", f->floor, f->map, out,
               f->gateCount ? kinds : "none", f->gateCount && !f->gatesLive ? " (not in force)" : "");
        fflush(stdout);
        sSaid = 1;
    }
    /* the gates passed (along the way across: the signed distance past each one's line) */
    while (sPassed < gates) {
        const float cx = from.x + (to.x - from.x) * f->gateAt[sPassed];
        const float cz = from.z + (to.z - from.z) * f->gateAt[sPassed];
        float margin;
        kind = f->gateKind[sPassed];
        s = (m->position.x - cx) * ux + (m->position.z - cz) * uz;
        margin = passMargin(kind);
        if (sPassed + 1 < gates) {
            /* (never past the next gate's line: three gates on a short way are close) */
            const float gap = (f->gateAt[sPassed + 1] - f->gateAt[sPassed]) * way;
            margin = margin < gap * 0.5f ? margin : gap * 0.5f;
        }
        if (s <= margin) {
            break;
        }
        printf("[tower] auto: floor %d gate %d (%s) through (%s)\n", f->floor, sPassed + 1, kindName(kind),
               marioChkSts(4) ? "paper mode" : roll ? "tube mode" : "walking");
        fflush(stdout);
        sPassed++;
        sRubFrames = sRubTries = sLetGo = sBack = 0;
    }
    if (sPassed < gates) {
        if (kind == PORT_TOWER_PAPER || kind == PORT_TOWER_WIND) {
            /* close to it (a wind wall: its gust and a little before) Paper mode is kept whatever; before
               that R is held from the lead on, but let go while stuck (a rock, a step on the way: in Paper
               mode Mario can't jump, so the jump and the way along the wall need him standing) */
            atGate = s > -(kind == PORT_TOWER_WIND ? WIND_CLOSE : PAPER_CLOSE);
            if (roll) {
                pressB = 1; /* stand up first */
            } else if (s > -(kind == PORT_TOWER_WIND ? WIND_LEAD : PAPER_LEAD) &&
                       (atGate || (sStuck == 0 && sDetour <= 0))) {
                wantR = 1;
            }
        } else if (!roll) {
            if (sPassed > 0 && f->gateKind[sPassed - 1] == PORT_TOWER_WIND) {
                /* a wind wall just before: rolled up out of its gust (rolling, Mario isn't side-on) */
                const float free = (f->gateAt[sPassed] - f->gateAt[sPassed - 1]) * way - WIND_GUST - 4.0f;
                tubeStop = free < tubeStop ? (free > 8.0f ? free : 8.0f) : tubeStop;
            }
            atGate = s > -tubeStop - 60.0f;
            if (sBack > 0) {
                /* rolling up didn't take here (a wall close by): a step back along the way, then again */
                sBack--;
                stickFor(-ux, -uz, 100.0f, &sx, &sy);
                setPad(sx, sy, 0);
                return;
            }
            if (s > -tubeStop) {
                stop = 1;
                if (sLetGo > 0) {
                    sLetGo--; /* R let go a moment: Mario stands again */
                } else {
                    wantR = 1;
                    rub = m->currMotionId == MARIO_MOTION_SLIT;
                    if (++sRubFrames > RUB_MAX) {
                        sRubFrames = 0;
                        sLetGo = 40;
                        if (++sRubTries % 2 == 0) {
                            sBack = 30;
                        }
                        printf("[tower] auto: floor %d gate %d (tube): no roll after %d frames, again (%d)\n",
                               f->floor, sPassed + 1, RUB_MAX, sRubTries);
                        fflush(stdout);
                    }
                }
            }
        } else if (s < -(tubeStop + 60.0f)) {
            /* rolled up far before this gate (the last one's roll carried on): stand up and walk, as a player
               would, since a tube can't step over what a standing Mario clears (floor 22's way, 222 short of
               its second gate); then roll up again at the gate */
            pressB = 1;
        } else {
            atGate = 1; /* rolling under it */
        }
    } else if (roll) {
        pressB = 1; /* past the last tube gate: stand up */
    }
    /* (the log: no headway for a second, at most a few times a floor) */
    if (++sHeadFrames >= 60) {
        const float moved = fabsf(m->position.x - sHeadPos.x) + fabsf(m->position.z - sHeadPos.z);
        if (moved < 8.0f && sHeadLogs < 8) {
            sHeadLogs++;
            printf("[tower] auto: floor %d: no headway at %.0f %.0f %.0f (motion %d, gate %d of %d %s, %.0f from "
                   "its line, way %.0f %.0f to %.0f %.0f)\n",
                   f->floor, m->position.x, m->position.y, m->position.z, (int)m->currMotionId, sPassed + 1, gates,
                   sPassed < gates ? kindName(kind) : "-", s, from.x, from.z, to.x, to.z);
            fflush(stdout);
        }
        sHeadFrames = 0;
        sHeadPos = m->position;
    }
    if (pressB) {
        held |= ++sBFrames % 20 < 2 ? PAD_BUTTON_B : 0; /* (a press, then let go: mot_roll reads the new press) */
        atGate = 1;
    } else {
        sBFrames = 0;
    }
    if (wantR) {
        held |= PAD_TRIGGER_R;
    }
    if (rub) {
        sRubAngle += RUB_STEP;
        sx = (s8)(cosf(sRubAngle) * 100.0f);
        sy = (s8)(sinf(sRubAngle) * 100.0f);
        setPad(sx, sy, held);
        return;
    }
    if (stop) {
        setPad(0, 0, held);
        return;
    }
    /* towards the way out (its hit object: the road's zone), as walk_to does */
    dx = to.x - m->position.x;
    dz = to.z - m->position.z;
    len = sqrtf(dx * dx + dz * dz);
    if (len > 1.0f) {
        stickFor(dx / len, dz / len, 100.0f, &sx, &sy);
    }
    if (!atGate) {
        /* stuck for a moment: a jump (a step up); longer: along the wall, one side then the other */
        if (++sWalkFrames % 20 == 0) {
            const float moved = fabsf(m->position.x - sPrev.x) + fabsf(m->position.z - sPrev.z);
            sStuck = moved < 6.0f ? sStuck + 1 : 0;
            sPrev = m->position;
            if (sStuck >= 2 && sDetour <= 0) {
                sTries++;
                sDetour = 60 * (1 + sTries / 2);
                sStuck = 0;
            }
        }
        if (sStuck > 0 && sWalkFrames % 20 < 3) {
            held |= PAD_BUTTON_A;
        }
        if (sDetour > 0 && len > 1.0f) {
            const float side = sTries % 2 ? 1.0f : -1.0f;
            sDetour--;
            stickFor(-dz / len * side, dx / len * side, 100.0f, &sx, &sy);
        }
    } else {
        sStuck = 0;
        sDetour = 0;
        sPrev = m->position;
    }
    setPad(sx, sy, held);
}

/* seqMain, before: after makeKey read the controllers, before the game's frame uses them */
static int climbPre(void) {
    PortTowerAutoFloor f;
    MarioWork* m;
    if (sOn <= 0 || gp == NULL) {
        return 0;
    }
    if (!sStartAsked && getenv("TTYD_TOWER_TEST") == NULL) {
        /* the climb asked for once Mario has been free in the field TTYD_TOWER_AUTO_START frames (as
           $TTYD_MODE_TEST's modes are: the file's state settled first, which TTYD_MODERUN's check is against) */
        const int calm = seqGetSeq() == SEQ_GAME && !gp->inBattle && !seqCheckSeq() &&
                         marioStGetSystemLevel() == 0 && marioChkKey();
        sCalm = calm ? sCalm + 1 : 0;
        if (sCalm >= sStartAt) {
            const char* why = "";
            sStartAsked = 1;
            printf("[tower] auto: climb asked on %s\n", gp->currentMapName);
            if (!portTowerStart(&why)) {
                printf("[tower] auto: start refused: %s\n", why);
            }
            fflush(stdout);
        }
        return 0;
    }
    if (!portTowerAutoFloor(&f)) {
        if (sFloorNow > 0) {
            printf("[tower] auto: the climb over (last on floor %d)\n", sFloorNow);
            fflush(stdout);
            sFloorNow = 0;
        }
        return 0;
    }
    if (f.floor != sFloorNow) {
        newFloor(&f);
    }
    if (f.battle) {
        return 0;
    }
    if (++sFloorFrames > sMaxFrames) {
        MarioWork* mw = marioGetPtr();
        printf("[tower] auto: floor %d not crossed in %d frames (map %s, Mario at %.0f %.0f %.0f, motion %d, "
               "gates passed %d of %d)\n",
               f.floor, sMaxFrames, gp->currentMapName, mw != NULL ? mw->position.x : 0.0f,
               mw != NULL ? mw->position.y : 0.0f, mw != NULL ? mw->position.z : 0.0f,
               mw != NULL ? (int)mw->currMotionId : -1, sPassed, f.gatesLive ? f.gateCount : 0);
        fflush(stdout);
        portTestExit(6);
    }
    if (seqGetSeq() != SEQ_GAME || gp->inBattle || seqCheckSeq() || strcmp(gp->currentMapName, f.map) != 0) {
        sControl = 0;
        return 0;
    }
    m = marioGetPtr();
    if (m == NULL || !marioChkKey() || marioStGetSystemLevel() != 0) {
        sControl = 0;
        sHeld = 0;
        return 0;
    }
    if (++sControl < 10) {
        return 0; /* (the way in walked, the floor's script done) */
    }
    drive(&f, m);
    return 0;
}

void portTowerAutoInit(void) {
    static int sDone;
    if (sOn < 0) {
        readEnv();
    }
    if (sDone || sOn <= 0) {
        return;
    }
    sDone = 1;
    portModHostAddHook(0, kSlot_seqMain, (void*)climbPre);
    printf("[tower] auto: the climber on%s\n", sOn == 2 ? ", every floor's gates off" : "");
    fflush(stdout);
}
