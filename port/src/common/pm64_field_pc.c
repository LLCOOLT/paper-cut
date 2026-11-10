/* The Paper Mario 64 partners' own field abilities (port/include/port/pm64.h,
 * docs/PM64_LINK.md), on top of their hosts' (pm64_partner_pc.c). Each uses the
 * game's own systems:
 *
 * - Watt (on Ms. Mowz) lights the way: in a room whose fog is dark, the fog is
 *   pushed back while she's out; the hidden blocks near her spark
 *   (effSparkEntry, the game's own spark); she floats close behind Mario, and
 *   the partner button puts her in his hands, where the hidden blocks near
 *   her show, see-through, as in Paper Mario.
 * - Sushie (on Vivian) swims: deep water (HIT_ATTR_DEEP_WATER) is a surface
 *   Mario rides on her back instead of a fall that sends him back; she's drawn
 *   under him there.
 * - Lakilester (on Flurrie) carries Mario over spikes and lava: his field
 *   ability's button puts Mario on his cloud (instead of Flurrie's Gale Force)
 *   and takes him off it, never over a hazard; ridden, a spiked or burning
 *   floor (HIT_ATTR_SPIKES, HIT_ATTR_DAMAGE_LIQUID) is one Mario rides over
 *   unhurt, and a spiked wall he walks into stops him unhurt.
 * - Parakarry (on Yoshi) carries Mario over gaps as in PM64: X held, he's
 *   drawn over Mario holding him up, flying level (Yoshi's flutter, three
 *   times as long) up to a reach; let go or past it, Mario drops and stands
 *   (never a ride on the ground). Sushie's X is refused (the buzzer).
 *
 * The last three reach past what their hosts do (a gap Yoshi can't flutter,
 * a spiked floor, a sea): setting "pm64_field" 1, "Like their partner", leaves
 * them out, so each reaches what its host does (Lakilester: Gale Force, and
 * the spikes hurt); 0, "Full" (the default), keeps them, with the places
 * they'd leave Mario stuck taken out (kParakarryShort; Sushie's SWIM_DROP and
 * Lakilester's CARRY_DROP: never into water or onto hazards farther below
 * Mario's last floor than he can get back up; from
 * tools/port/pm64_field_sweep.py, which reads these and finds no place left
 * with no way back, nor any door, pipe or story trigger only the extras
 * reach). Watt's light and sparks open nothing and stay either way.
 *
 * All of it is game state only through settings and GSW every game of an
 * online session shares (the partners on: pm64_partner_pc.c's hello token;
 * "pm64_field" is the host's session rule, port/include/port/online_local.h). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/pm64.h"
#include "driver/animdrv.h"
#include "port/settings.h"
#include "port/versions.h"
#include "port/luigi.h"
#ifdef PORT_MODS
#include "port/mod_host.h"
#include "port/postgame_extra.h"
#endif
#include "port/luigi.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/effdrv.h"
#include "mario/mario_cam.h"
#include <dolphin/mtx.h>
#include "driver/seqdrv.h"
#include "driver/hitdrv.h"
#include "driver/mapdrv.h"
#include "driver/mobjdrv.h"
#include "eff/eff_spark.h"
#include "mario/mario.h"
#include "mario/mario_motion.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mario_sbr.h"
#include "system.h"
#include "mario/mariost.h"
#include "party.h"
#include "party/party_move.h"
#include <math.h>
#include <dolphin/pad.h>
#include "mario/mario_hit.h"
#include "party/party_yoshi.h"
#include "pmario_sound.h"
#include "mario/mario_motion.h"
#include "system.h"

MapObjectWork* portMobjGetWork(void);

/* the movement extras on: "pm64_field" 0, full (as in Paper Mario) */
static int extras(void) {
    static int* sField;
    if (sField == NULL) {
        sField = portSettingsDeclare("pm64_field", 0, 0, 1); /* (as pm64_link.cpp: headless has no Settings page) */
    }
    return sField == NULL || *sField == 0;
}

/* the PM64 partner out now in the field (on the partner out), 0 none */
static int outNow(void) {
    PartyEntry* party;
    if (gp == NULL || gp->inBattle || (party = partyGetPtr(marioGetPartyId())) == NULL) {
        return 0;
    }
    return portPm64PartnerOn(party->currentMemberId);
}

int portPm64PartnerOut(void) {
    return outNow();
}

/* --- Parakarry, ridden (party_yoshi.c) */

static int logOn(void);

/* The airspace Parakarry's long flutter is cut short in (map, x0 z0 x1 z1 y0 y1): entering it, the flutter ends as
 * Yoshi's would by then, and Mario falls. Each is the way to a floor his long one would land Mario on with no way
 * back. From tools/port/pm64_field_sweep.py parakarry, which reads this table and finds none left. */
static const struct {
    const char* map;
    float x0, z0, x1, z1, y0, y1;
} kParakarryShort[] = {
    /* Palace of Shadow's entrance hall: from the north pillar row's east pillar to the low pillar in the corner (the
       one beside it stays: his long flutter back) */
    {"las_00", 150.0f, -265.0f, 340.0f, -175.0f, 200.0f, 330.0f},
};

static int parakarryShort(void) {
    MarioWork* mario = marioGetPtr();
    size_t i;
    for (i = 0; mario != NULL && i < sizeof(kParakarryShort) / sizeof(kParakarryShort[0]); i++) {
        const Vec* p = &mario->position;
        if (!strcmp(gp->currentMapName, kParakarryShort[i].map) && p->x >= kParakarryShort[i].x0 &&
            p->x <= kParakarryShort[i].x1 && p->z >= kParakarryShort[i].z0 && p->z <= kParakarryShort[i].z1 &&
            p->y >= kParakarryShort[i].y0 && p->y <= kParakarryShort[i].y1) {
            static unsigned sLogged;
            if (logOn() && gp->retraceCount - sLogged > 60) {
                printf("[pm64] Parakarry: the long flutter cut short in %s (%.0f, %.0f, %.0f)\n", gp->currentMapName,
                       p->x, p->y, p->z);
                fflush(stdout);
                sLogged = gp->retraceCount;
            }
            return 1;
        }
    }
    return 0;
}

/* PM64's carry (as world_parakarry's, its feel only): X lifts Mario, and he's carried while X is held, at most
 * CARRY_REACH from where he was lifted (and the flutter's frames); let go, or past it, Parakarry lets go and Mario
 * drops to the floor below and stands. Never a ride on the ground: landed, he's set down at once (Yoshi's own
 * dismount). Refused (the game's buzzer) with a ceiling too low to lift him under. Both settings: "Like their
 * partner" only keeps the carry as long as Yoshi's flutter (portPm64FlutterFrames). */
#define CARRY_REACH 200.0f
#define CARRY_HEADROOM 70.0f
static int sSwimming; /* (Sushie's, below) */
static int sCarry;      /* 0 none, 1 lifted (not yet flying), 2 flying, 3 let go */
static float sCarryX, sCarryZ;

static float carryDist(const MarioWork* mario) {
    const float dx = mario->position.x - sCarryX, dz = mario->position.z - sCarryZ;
    return sqrtf(dx * dx + dz * dz);
}

/* party_yoshi.c, each frame of a flutter once Yoshi's own steering is done (angle and speed: the stick, camera
 * relative, L_getStick): in Parakarry's carry he flies where the stick points, every way (Yoshi's flutter steers
 * left and right only), at CARRY_SPEED; the stick let go, he slows to a hover. */
#define CARRY_SPEED 2.5f
void portPm64CarrySteer(PartyEntry* party, float angle, float speed) {
    static unsigned sLogged;
    if (sCarry == 0 || party == NULL || outNow() != PM64_PARTNER_PARAKARRY || (party->flags & PARTY_FLAG_IS_EVT_USE)) {
        return;
    }
    if (speed > 0.0f) {
        party->moveSpeed = CARRY_SPEED;
        party->targetDir = angle;
        party->moveDir = angle;
    } else {
        party->moveSpeed *= 0.85f;
        if (party->moveSpeed < 0.05f) {
            party->moveSpeed = 0.0f;
        }
    }
    if (logOn() && speed > 0.0f && gp->retraceCount - sLogged > 30) {
        sLogged = gp->retraceCount;
        printf("[pm64] Parakarry: carried toward %.0f (camera %.0f)\n", angle, party->camDir);
        fflush(stdout);
    }
}

static void parakarryFrame(void) {
    PartyEntry* party = partyGetPtr(marioGetPartyId());
    MarioWork* mario = marioGetPtr();
    const int status = outNow() == PM64_PARTNER_PARAKARRY ? yoshiGetStatus() : 0;
    if (status == 0 || party == NULL || mario == NULL || (party->flags & PARTY_FLAG_IS_EVT_USE)) {
        sCarry = 0; /* (a script's ride, as Yoshi's) */
        return;
    }
    if (status == 1 && party->useMotionId == 13 && !(mario->flags & MARIO_FLAG_FORCED_MOVEMENT) &&
        party->runMode != 8 && !marioKeyOffChk()) { /* (Mario's own, as ever, while the game holds him) */
        if (sCarry == 0 && (party->buttonHold & PAD_BUTTON_X)) {
            sCarry = 1; /* lifted: off at once */
            sCarryX = mario->position.x;
            sCarryZ = mario->position.z;
            party->useMotionId = 20;
            if (logOn()) {
                printf("[pm64] Parakarry: carrying Mario from %.0f %.0f %.0f\n", mario->position.x, mario->position.y,
                       mario->position.z);
                fflush(stdout);
            }
        } else {
            if (logOn()) {
                printf("[pm64] Parakarry: Mario set down at %.0f %.0f %.0f (%.0f carried)\n", mario->position.x,
                       mario->position.y, mario->position.z, sCarry ? carryDist(mario) : 0.0f);
                fflush(stdout);
            }
            sCarry = 0;
            party->useMotionId = 100; /* never a ride on the ground: Yoshi's dismount */
        }
    } else if (status == 2 && sCarry == 1) {
        sCarry = 2;
    } else if ((status == 1 || status == 4) && sCarry == 2) { /* (landed or falling; 5 is a step up too) */
        if (logOn()) {
            printf("[pm64] Parakarry: lets go after %.0f at %.0f %.0f %.0f (%s; dx %.0f dz %.0f)\n", carryDist(mario),
                   mario->position.x, mario->position.y, mario->position.z,
                   status == 1                          ? "over a floor"
                   : (party->buttonHold & PAD_BUTTON_X) ? "his time is up"
                                                        : "X let go",
                   mario->position.x - sCarryX, mario->position.z - sCarryZ);
            fflush(stdout);
        }
        sCarry = 3;
    }
}

/* the button the flutter is held on: X, Parakarry's carry */
int portPm64FlutterButton(void) {
    return sCarry != 0 && outNow() == PM64_PARTNER_PARAKARRY ? PAD_BUTTON_X : PAD_BUTTON_A;
}

/* Parakarry's ability refused (partyDoWork, the X button): the game's buzzer, and nothing else happens. Parakarry
 * under a ceiling too low to lift Mario; Sushie away from deep water (PM64: only into it; beside it X hops Mario
 * onto her back, sushieBoard), and on her back (Vivian's Veil under her would sink Mario out of sight). */
static int sushieBoard(MarioWork* mario);

int portPm64FieldRefuse(void) {
    MarioWork* mario = marioGetPtr();
    const int out = outNow();
    const char* why = NULL;
    if (mario == NULL) {
        return 0;
    }
    if (out == PM64_PARTNER_SUSHIE) {
        if (!sSwimming && sushieBoard(mario)) {
            return 1; /* (X used: he hops onto her back) */
        }
        why = sSwimming ? "on her back (no dive)" : "not on deep water";
    } else if (out == PM64_PARTNER_PARAKARRY) {
        Vec pos = mario->position;
        f32 h = CARRY_HEADROOM;
        if (marioSearchHead(&pos, &h, 10.0f) != NULL) {
            why = "a ceiling too low to lift Mario";
        }
    }
    if (why == NULL) {
        return 0;
    }
    if (!sSwimming) {
        psndSFXOn((const char*)0x20014); /* the game's buzzer */
    }
    if (logOn()) {
        printf("[pm64] %s: refused, %s, at %.0f %.0f %.0f in %s\n", out == PM64_PARTNER_SUSHIE ? "Sushie" : "Parakarry",
               why, mario->position.x, mario->position.y, mario->position.z, gp->currentMapName);
        fflush(stdout);
    }
    return 1;
}

int portPm64FlutterFrames(int frames) {
    static unsigned sLogged;
    MarioWork* mario = marioGetPtr();
    if (sCarry != 0 && outNow() == PM64_PARTNER_PARAKARRY && mario != NULL && carryDist(mario) > CARRY_REACH) {
        if (sCarry != 3 && logOn()) {
            printf("[pm64] Parakarry: lets go after %.0f at %.0f %.0f %.0f (his reach; dx %.0f dz %.0f)\n",
                   carryDist(mario), mario->position.x, mario->position.y, mario->position.z,
                   mario->position.x - sCarryX, mario->position.z - sCarryZ);
            fflush(stdout);
        }
        sCarry = 3;
        return 0; /* (past his reach: he lets go) */
    }
    if (outNow() != PM64_PARTNER_PARAKARRY || !extras() || parakarryShort()) {
        return frames;
    }
    if (logOn() && gp->retraceCount - sLogged > 120) { /* (asked each frame of a flutter) */
        sLogged = gp->retraceCount;
        printf("[pm64] Parakarry: a flutter of %d frames (Yoshi's %d)\n", frames * 3, frames);
        fflush(stdout);
    }
    return frames * 3;
}

/* --- Sushie (mario_motion.c, mario_hit.c, mot_hip.c, mot_jump.c) */

#define DOCKS (HIT_ATTR_DOCK_NEG_Z | HIT_ATTR_DOCK_POS_Z | HIT_ATTR_DOCK_NEG_X | HIT_ATTR_DOCK_POS_X)

extern HitEntry* hitActiveListTop;

/* The boat's water: any room of Pirate's Grotto, whose channels are the boat's (swimming them would skip its
 * ways and puzzles, and leave Mario where the game expects the boat), and Mario in Boat Mode anywhere. The other
 * rooms with Boat Mode's panels swim (the user's call): the harbour (gor_00, gor_02), the sewers' (tik_02, tik_18,
 * tik_20) and the Palace's (las_09). There, from tools/port/pm64_field_sweep.py sushie, no floor swimming reaches
 * leaves Mario with no way back; the boat's tunnel exits (tik_18's e_bero_1, tik_20's w_bero) are walls Mario
 * pushes only in Boat Mode (HIT_ATTR_ALWAYS_SOLID): swimming never takes him out through them; every panel is at
 * most 20 over the water, so he swims back up onto it. The room's name and Mario's motion (the same in every game
 * of a session). */
static int boatWater(void) {
    const MarioWork* mario = marioGetPtr();
    return !strncmp(gp->currentMapName, "dou_", 4) || (mario != NULL && mario->currMotionId == MARIO_MOTION_SHIP);
}

static int sSwimming;

/* The floor Mario last stood on out of the water, its height (sushieFrame). Sushie takes him on her back only from
 * water at most SWIM_DROP below it: her jump (portPm64SwimJump) gets him back up that far, so he's never left
 * swimming in a pool he can't climb out of (a drop from higher, Palace of Shadow's las_04, falls in as ever and the
 * game puts him back). tools/port/pm64_field_sweep.py sushie checks the rest of the water this way. 80, not 65:
 * Rogueport's east harbour (gor_02) has its quay 75 over the water; her jump (12/8 of his) catches floors up to
 * about 86 over it. */
#define SWIM_DROP 80.0f
static char sShoreMap[16];
static float sShoreY;

/* X beside deep water (as PM64's Sushie): Mario hops from the edge onto her back in the water he faces, a small
 * jump carried out over it (sushieBoardFrame), and rides on. Only where she'd swim (not the boat's water, "Full"),
 * from a floor at most SWIM_DROP over that water (so her jump gets him back out), and with no wall between. */
#define BOARD_REACH 70.0f
#define BOARD_STEP 3.0f
static int sBoard;      /* frames of the hop left */
static float sWalkDir;  /* the way Mario last walked (sushieBoardFrame) */
static float sBoardDir, sBoardLeft;

static HitEntry* floorBelow(float x, float y, float z, float* hy) {
    f32 hx, hz, dist = 200.0f, nx, ny, nz;
    f32 yy;
    HitEntry* h = hitCheckFilter(NULL, &hx, &yy, &hz, &dist, &nx, &ny, &nz, x, y, z, 0.0f, -1.0f, 0.0f);
    *hy = yy;
    return h;
}

static int sushieBoard(MarioWork* mario) {
    float d, wy = 0.0f;
    const float dir = sWalkDir; /* (the way he last walked: directionView settles to left or right) */
    if (!extras() || boatWater() || mario->hitobjStandOn == NULL ||
        (hitGetAttr(mario->hitobjStandOn) & HIT_ATTR_DEEP_WATER)) {
        return 0;
    }
    for (d = 20.0f; d <= BOARD_REACH; d += 10.0f) {
        float x = mario->position.x, z = mario->position.z, hy;
        HitEntry* h;
        movePos(&x, &z, d, dir);
        h = floorBelow(x, mario->position.y + 30.0f, z, &hy);
        if (h == NULL) {
            continue;
        }
        if (!(h->attributes & HIT_ATTR_DEEP_WATER)) {
            if (hy > mario->position.y + 15.0f) {
                return 0; /* (a ledge up, or a wall: not the water's edge) */
            }
            continue;
        }
        wy = hy;
        if (mario->position.y - wy > SWIM_DROP) {
            if (logOn()) {
                printf("[pm64] Sushie: the water at %.0f %.0f is %.0f below: too far to get back out\n", x, z,
                       mario->position.y - wy);
                fflush(stdout);
            }
            return 0;
        }
        {   /* (nothing in the way at his middle) */
            f32 hx, hyy, hz, dist = d, nx, ny, nz;
            const float rad = dir * 3.14159265f / 180.0f;
            if (hitCheckFilter(NULL, &hx, &hyy, &hz, &dist, &nx, &ny, &nz, mario->position.x, mario->position.y + 20.0f,
                               mario->position.z, sinf(rad), 0.0f, -cosf(rad)) != NULL && dist < d - 10.0f) {
                return 0;
            }
        }
        memcpy(sShoreMap, gp->currentMapName, sizeof(sShoreMap));
        sShoreY = mario->position.y;
        sBoardDir = dir;
        sBoardLeft = d + 15.0f; /* (over the edge and onto the water) */
        sBoard = 60;
        marioChgSmallJumpMotion();
        mario->flags &= ~MARIO_FLAG_STOP_AIR_MOVEMENT;
        mario->baseSpeed = BOARD_STEP;
        mario->directionView = dir;
        if (logOn()) {
            printf("[pm64] Sushie: Mario hops onto her back from %.0f %.0f %.0f to the water %.0f ahead (%.0f below)\n",
                   mario->position.x, mario->position.y, mario->position.z, d, mario->position.y - wy);
            fflush(stdout);
        }
        return 1;
    }
    return 0;
}

/* each frame of the hop: carried out over the water until he's there (or lands, or the game takes him) */
static void sushieBoardFrame(void) {
    MarioWork* mario = marioGetPtr();
    if (mario != NULL && sBoard <= 0 && mario->baseSpeed > 0.0f &&
        (mario->currMotionId == MARIO_MOTION_WALK || mario->currMotionId == MARIO_MOTION_DASH)) {
        sWalkDir = mario->directionView;
    }
    if (sBoard <= 0) {
        return;
    }
    if (mario == NULL || outNow() != PM64_PARTNER_SUSHIE || marioKeyOffChk() ||
        (mario->currMotionId != MARIO_MOTION_JUMP && mario->currMotionId != MARIO_MOTION_FALL) ||
        sBoardLeft <= 0.0f) {
        if (logOn() && mario != NULL) {
            printf("[pm64] Sushie: the hop ends at %.0f %.0f %.0f (motion %d, %.0f left)\n", mario->position.x,
                   mario->position.y, mario->position.z, (int)mario->currMotionId, sBoardLeft);
            fflush(stdout);
        }
        sBoard = 0;
        return;
    }
    sBoard--;
    /* (carried by the jump's own air move: its speed along his way, the stick's or not) */
    mario->flags &= ~MARIO_FLAG_STOP_AIR_MOVEMENT;
    mario->baseSpeed = BOARD_STEP;
    mario->directionView = sBoardDir;
    sBoardLeft -= BOARD_STEP;
}

/* --- Lakilester (on Flurrie): Mario carried over spikes and lava ("Full")
 *
 * A spiked or burning floor with Lakilester out is one Mario rides over on his cloud, unhurt (as PM64's ride), and
 * a spiked wall or spike cluster he walks into stops him unhurt; Flurrie's Gale Force stays his field ability. Only
 * onto floors at most CARRY_DROP below the last floor Mario stood on out of them: Mario's own jump (48) gets him
 * back up that far, and walking off one at that height is a step down he walks back up, so he's never left on one
 * he can't leave (a drop from higher lands as ever: hurt, and the game puts him back). The Palace of Shadow's
 * spike room (las_03): its two pits are 11 below the walkway, their spike clusters taller than Mario's step: he
 * stops at them unhurt and walks back out. Pirate's Grotto's spear traps (dou_10) aren't a floor: they hurt as
 * ever. "Like their partner": nothing of this (Gale Force only; the spikes hurt). The game's own state: settings,
 * the room and Mario's place (the same in every game of a session). */
#define CARRY_DROP 40.0f
#define HAZARD (HIT_ATTR_SPIKES | HIT_ATTR_DAMAGE_LIQUID)
static int sCarried;
static char sSafeMap[16];
static float sSafeY;

/* (in a room none of whose safe floors he has stood on yet, a drop in from a pipe or a hole onto a hazard: not
 * carried, hurt as with Flurrie and put back by the game, never left on a hazard he can't leave) */
static int sRide; /* Mario on the cloud: PM64's ride (world_lakilester UseAbility), his field ability's button */
static char sRideMap[16];

static int lakilesterCarries(const MarioWork* mario) {
    return outNow() == PM64_PARTNER_LAKILESTER && extras() && sRide && mario != NULL &&
           strcmp(gp->currentMapName, "dou_10") &&
           (sCarried || (strcmp(sSafeMap, gp->currentMapName) == 0 && sSafeY - mario->position.y <= CARRY_DROP));
}

int portPm64Carried(void) {
    return sRide;
}

/* The button with Lakilester out ("Full"): on the cloud, or off it, as PM64's ride (Flurrie's Gale Force is his host's,
 * not his). Getting off is refused over spikes or lava and in the air, as PM64 refuses it over hazards (a buzz, and he
 * stays under Mario). Either way the use ends at once and he goes back to following (party_cloud.c's end of
 * N__cloud_use), so the button works again the next frame. */
int portPm64LakilesterUse(void* p) {
    PartyEntry* party = (PartyEntry*)p;
    MarioWork* mario = marioGetPtr();
    int refuse = 0;
    if (party == NULL || mario == NULL || outNow() != PM64_PARTNER_LAKILESTER || !extras()) {
        return 0;
    }
    if (party->trigFlags & PARTY_TRIG_FLAG_CHG_RUN_MODE) {
        party->trigFlags &= ~PARTY_TRIG_FLAG_CHG_RUN_MODE;
        if (!sRide) {
            sRide = 1;
            memcpy(sRideMap, gp->currentMapName, sizeof(sRideMap));
        } else if (mario->hitobjStandOn == NULL || (hitGetAttr(mario->hitobjStandOn) & (HAZARD | HIT_ATTR_DEEP_WATER))) {
            refuse = 1;
            psndSFXOn("SFX_AC_COMMAND_NG1");
        } else {
            sRide = 0;
            mario->dispOfs2.y = 0.0f;
        }
        if (logOn()) {
            printf("[pm64] Lakilester: %s at %.0f %.0f %.0f\n",
                   refuse ? "can't get off here (a hazard or the air below)" : sRide ? "Mario rides the cloud" : "Mario gets off",
                   mario->position.x, mario->position.y, mario->position.z);
            fflush(stdout);
        }
    }
    party->flags &= ~(PARTY_FLAG_IS_BEING_USED | PARTY_FLAG_IS_EVT_USE);
    party->flags2 |= PARTY_DISP_FLAG_DIR_LOCK;
    partyChgRunMode(party, 2);
    party->moveArg = 0;
    partyChgMoveMode(party, 2);
    partyChgMot(party, 1);
    return 1;
}

/* --- Bombette (on Bobbery): back from her blast (party_bomhei.c's use from 80: waiting off screen, dropping in
 * beside Mario, landing, done) she faces where Mario faces, as PM64's Bombette comes back; Bobbery keeps the way he
 * walked off to blow up, which left her facing away from Mario once he had turned. */
void portPm64BombetteUsed(void* p) {
    PartyEntry* party = (PartyEntry*)p;
    MarioWork* mario = marioGetPtr();
    if (party == NULL || mario == NULL || outNow() != PM64_PARTNER_BOMBETTE || party->useMotionId < 80) {
        return;
    }
    party->displayDirection = mario->displayDirection;
    party->displayDirectionTarget = mario->displayDirectionTarget;
    if (party->useMotionId == 100 && logOn()) {
        printf("[pm64] Bombette: back from her blast facing as Mario (%.0f)\n", party->displayDirection);
        fflush(stdout);
    }
}

int portPm64MarioAttr(struct HitEntry* hit) {
    s32 attr = hitGetAttr(hit);
    MarioWork* mario;
    /* (as Lakilester's: in a room none of whose shores he has stood on yet, he falls in as with Vivian) */
    if ((attr & HIT_ATTR_DEEP_WATER) && outNow() == PM64_PARTNER_SUSHIE && extras() && !boatWater() &&
        (mario = marioGetPtr()) != NULL &&
        (sSwimming || (strcmp(sShoreMap, gp->currentMapName) == 0 && sShoreY - mario->position.y <= SWIM_DROP))) {
        attr &= ~HIT_ATTR_DEEP_WATER;
    }
    if ((attr & HAZARD) && outNow() == PM64_PARTNER_LAKILESTER) {
        static unsigned sLogged;
        const char* name = hitGetName(hit);
        mario = marioGetPtr();
        if (lakilesterCarries(mario)) {
            if (logOn() && gp->retraceCount - sLogged > 60) {
                printf("[pm64] Lakilester: over %s, unhurt (%s in %s)\n", (attr & HIT_ATTR_SPIKES) ? "spikes" : "lava",
                       name != NULL ? name : "?", gp->currentMapName);
                fflush(stdout);
                sLogged = gp->retraceCount;
            }
            /* (lava that's deep too: a floor on the cloud, not a fall) */
            return attr & ~(HAZARD | HIT_ATTR_DEEP_WATER);
        }
        if (logOn() && gp->retraceCount - sLogged > 60) {
            printf("[pm64] Lakilester: %s in %s hurts (%s)\n", name != NULL ? name : "?", gp->currentMapName,
                   extras() ? "too far below his last floor" : "like their partner: Flurrie's Gale Force only");
            fflush(stdout);
            sLogged = gp->retraceCount;
        }
    }
    return attr;
}

static void ridePoseFrame(int on, int p, const char* who);
#define RIDE_SEAT_MARIO 14.0f
#define RIDE_SEAT_LUIGI 14.0f
#define RIDE_SEAT_PEACH 18.0f
#define RIDE_SEAT_BOWSER 16.0f

/* How high Mario sits riding the cloud (drawn only): the ride pose's seat on the cloud's top, as PM64's ride
 * (Lakilester drawn at Mario's feet, pm64_partner_pc.c), for each play-as pick's own ride pose. Set by eye from
 * side views (pm64_lakilester_ride and its _luigi, _peach, _bowser). */
static float rideSeat(void) {
    switch (portPlayAs()) {
    case PORT_PLAY_AS_BOWSER:
        return RIDE_SEAT_BOWSER;
    case PORT_PLAY_AS_PEACH:
        return RIDE_SEAT_PEACH;
    case PORT_PLAY_AS_LUIGI:
        return RIDE_SEAT_LUIGI;
    default:
        return RIDE_SEAT_MARIO;
    }
}

/* each frame: Mario on the cloud while he stands on a hazard floor it carries him over (drawn on it), and the last
 * floor he stood on out of them */
static void lakilesterFrame(void) {
    MarioWork* mario = marioGetPtr();
    s32 attr;
    int carried;
    /* Ridden, he is under Mario (drawn there): the spikes his host's model follows over don't send him off hurt
     * (party.c's party_damage_return, run mode 13, a long flight off screen and back that holds the button off) */
    if (sRide && mario != NULL) {
        PartyEntry* party = partyGetPtr(marioGetPartyId());
        if (party != NULL && party->runMode == 13) {
            partyChgRunMode(party, 2);
            party->position = mario->position;
        }
    }
    /* (the ride ends with the room, a battle, another partner or "Like their partner": PM64's ends with them too) */
    if (sRide && (mario == NULL || gp->inBattle || outNow() != PM64_PARTNER_LAKILESTER || !extras() ||
                  strcmp(sRideMap, gp->currentMapName) != 0)) {
        sRide = 0;
        if (mario != NULL) {
            mario->dispOfs2.y = 0.0f;
        }
        if (logOn()) {
            printf("[pm64] Lakilester: the ride ends (%s)\n", gp->inBattle ? "a battle" : "the room or partner");
            fflush(stdout);
        }
    }
    if (mario == NULL) {
        sCarried = 0;
        return;
    }
    attr = mario->hitobjStandOn != NULL ? hitGetAttr(mario->hitobjStandOn) : 0;
    carried = mario->hitobjStandOn != NULL && (attr & HAZARD) && lakilesterCarries(mario);
    if (carried != sCarried && logOn()) {
        printf("[pm64] Lakilester: %s at %.0f %.0f %.0f\n", carried ? "Mario on the cloud" : "Mario off the cloud",
               mario->position.x, mario->position.y, mario->position.z);
        fflush(stdout);
    }
    if (sRide) {
        mario->dispOfs2.y = rideSeat(); /* on the cloud's top */
    } else if (sCarried) {
        mario->dispOfs2.y = 0.0f;
    }
    sCarried = carried;
    /* sitting still on it, as in PM64: never walking or running on the cloud */
    ridePoseFrame(sRide && outNow() == PM64_PARTNER_LAKILESTER, PM64_PARTNER_LAKILESTER, "Lakilester");
    if (mario->hitobjStandOn != NULL && !(attr & (HAZARD | HIT_ATTR_DEEP_WATER))) {
        memcpy(sSafeMap, gp->currentMapName, sizeof(sSafeMap));
        sSafeY = mario->position.y;
    }
}

int portPm64Swimming(void) {
    return sSwimming;
}

/* tests: TTYD_PM64_LOG=1 logs the field abilities as they act, and each room's water, spike, lava and boat
 * panel hit objects as it loads */
static int logOn(void) {
    static int sOn = -1;
    if (sOn < 0) {
        const char* v = getenv("TTYD_PM64_LOG");
        sOn = v != NULL && v[0] != '\0' && v[0] != '0';
    }
    return sOn;
}

static void logRoom(void) {
    static char sMap[16];
    HitEntry* h;
    if (!logOn() || !strcmp(sMap, gp->currentMapName) || hitActiveListTop == NULL) {
        return;
    }
    memcpy(sMap, gp->currentMapName, sizeof(sMap));
    for (h = hitActiveListTop; h != NULL; h = h->nextActive) {
        const s32 attr = h->attributes;
        if (attr & (HIT_ATTR_DEEP_WATER | HIT_ATTR_SPIKES | HIT_ATTR_DAMAGE_LIQUID | DOCKS)) {
            printf("[pm64] %s hit %s attr %08x at %.0f %.0f %.0f r %.0f%s\n", sMap, h->joint != NULL ? h->joint->name : "?",
                   (unsigned)attr, h->centerWorld.x, h->centerWorld.y, h->centerWorld.z, h->radius,
                   h->damage != NULL ? " (return)" : "");
        }
    }
    fflush(stdout);
}

/* Mario rides Sushie as in PM64: still on her back, never walking or running on the water. TTYD's ride on Yoshi's
 * back (M_Y_1) stands for his stands, walks and runs there; every play-as character has it (their own, or the
 * play-as fallback for a pose they lack). */
#define RIDE_POSE "M_Y_1"
static int ridePoseFor(const char* name) {
    return name != NULL && (!strncmp(name, "M_S_", 4) || !strncmp(name, "M_W_", 4) || !strncmp(name, "M_R_", 4));
}

/* Mario riding a PM64 partner now: on Sushie's back, or on Lakilester's cloud (the same still seat) */
static int riding(void) {
    const int p = outNow();
    return (sSwimming && p == PM64_PARTNER_SUSHIE) || (sRide && p == PM64_PARTNER_LAKILESTER);
}

/* pm64_aurora.c's marioChgPose hook: riding, a stand, walk or run asked for is the ride pose */
int portPm64RidePose(const char** name) {
    if (riding() && name != NULL && ridePoseFor(*name)) {
        *name = RIDE_POSE;
    }
    return 0;
}

/* each frame: riding (on, partner p's), the stand or walk he had as he got on is the ride pose; off again, the ride
 * pose is a stand (the walk sets its own as he moves). who: the log's name. */
static void ridePoseFrame(int on, int p, const char* who) {
    MarioWork* mario = marioGetPtr();
    static char sPose[16];
    if (mario == NULL || mario->animName == NULL) {
        return;
    }
    if (on && ridePoseFor(mario->animName)) {
        marioChgPose(RIDE_POSE);
    } else if (!on && strcmp(mario->animName, RIDE_POSE) == 0 && outNow() == p) {
        marioChgPose("M_S_1");
    }
    if (on && logOn() && strncmp(sPose, mario->animName, sizeof(sPose) - 1) != 0) {
        snprintf(sPose, sizeof(sPose), "%s", mario->animName);
        printf("[pm64] %s: Mario's pose %s: %s\n", who, p == PM64_PARTNER_SUSHIE ? "on her back" : "on the cloud", sPose);
        fflush(stdout);
    } else if (!on && outNow() == p) {
        sPose[0] = '\0';
    }
}

static void sushieFrame(void) {
    MarioWork* mario = marioGetPtr();
    /* (only when she carries him: with "Like their partner", or on the boat's water, he only falls in) */
    const int swim = mario != NULL && outNow() == PM64_PARTNER_SUSHIE && extras() && !boatWater() &&
                     mario->hitobjStandOn != NULL && (hitGetAttr(mario->hitobjStandOn) & HIT_ATTR_DEEP_WATER);
    if (swim != sSwimming && mario != NULL && logOn()) {
        printf("[pm64] Sushie: %s at %.0f %.0f %.0f\n", swim ? "swimming" : "ashore", mario->position.x,
               mario->position.y, mario->position.z);
        fflush(stdout);
    }
    if (swim) {
        mario->dispOfs2.y = 6.0f; /* on her back */
    } else if (sSwimming && mario != NULL) {
        mario->dispOfs2.y = 0.0f;
    }
    sSwimming = swim;
    /* still on her back, not walking */
    ridePoseFrame(swim, PM64_PARTNER_SUSHIE, "Sushie");
    if (mario != NULL && !swim && mario->hitobjStandOn != NULL &&
        !(hitGetAttr(mario->hitobjStandOn) & HIT_ATTR_DEEP_WATER)) {
        memcpy(sShoreMap, gp->currentMapName, sizeof(sShoreMap));
        sShoreY = mario->position.y;
    }
}

/* A jump off Sushie's back (mot_jump.c's marioPreJump, once its curve is set): she flips Mario up higher than
 * his own jump (floors up to about 86 over the water, to its 43), so from the water he reaches any shore he could
 * step off into it (SWIM_DROP: gor_02's quay is 75 over its water, Keelhaul Key's beach 65 over the sea) and is
 * never left swimming below a ledge. From the floor he jumps
 * off, as the game's own state (the same in every game of a session). */
void portPm64SwimJump(void) {
    MarioWork* mario = marioGetPtr();
    /* (the floor jumped off: marioPreJump has let go of the one stood on by now) */
    if (mario == NULL || mario->hitobjJumpFrom == NULL || outNow() != PM64_PARTNER_SUSHIE || !extras() ||
        !(hitGetAttr(mario->hitobjJumpFrom) & HIT_ATTR_DEEP_WATER) || boatWater() ||
        mario->jumpVelocityY <= 0.0f || (mario->flags & MARIO_FLAG_PRE_BATTLE_PHYSICS)) {
        return;
    }
    mario->jumpVelocityY *= 12.0f / 8.0f;
    if (logOn()) {
        printf("[pm64] Sushie: a jump off her back at %.0f %.0f %.0f\n", mario->position.x, mario->position.y,
               mario->position.z);
        fflush(stdout);
    }
}

/* --- Watt */

static struct {
    char map[16];
    int on;
    s32 type;
    f32 start, end;
    GXColor color;
} sFog;

static void wattFog(int watt) {
    if (strcmp(sFog.map, gp->currentMapName) != 0) {
        /* another room: its own fog came with it */
        memcpy(sFog.map, gp->currentMapName, sizeof(sFog.map));
        sFog.on = 0;
    }
    if (watt && !sFog.on) {
        s32 type;
        f32 start, end;
        GXColor color;
        mapGetFog(&type, &start, &end, &color);
        /* a dark room: its fog on, close in and near black (the type is fog_type[]'s index: 0, linear, is
           every room's) */
        if ((mapGetWork()->entries[0].flags & MAPENT_FLAG_FOG) && color.r + color.g + color.b < 3 * 72 &&
            end < 2000.0f) {
            sFog.type = type;
            sFog.start = start;
            sFog.end = end;
            sFog.color = color;
            sFog.on = 1;
            mapSetFog(type, start + 250.0f, end * 1.75f + 300.0f, color);
            if (logOn()) {
                printf("[pm64] Watt: %s's fog %.0f..%.0f pushed back\n", gp->currentMapName, start, end);
                fflush(stdout);
            }
        }
    } else if (!watt && sFog.on) {
        mapSetFog(sFog.type, sFog.start, sFog.end, sFog.color);
        sFog.on = 0;
    }
}

/* Watt carried (PM64's world_watt ability): the partner button (Ms. Mowz's use, pm64_aurora.c's hook) picks her
 * up into Mario's hands, in front of him on the side he faces, and he walks and jumps with her; pressed again,
 * she floats down behind him. No freeze and no sniffing (Ms. Mowz's). Put down by a battle, a scene (Mario's
 * keys off), another room or partner. Held, the hidden blocks near her show, see-through (wattReveal); out, she
 * floats close behind Mario, as in Paper Mario, not at a walker's distance. */
#define WATT_CLOSE 24.0f  /* her distance behind Mario, out */
#define WATT_REVEAL 150.0f /* how far from her the hidden blocks show, held */
static int sWattHeld;
static char sWattHeldMap[16];

int portPm64WattHeld(void) {
    return sWattHeld;
}

/* Mario holds her up in both hands with the game's own carry: Bobbery's (party_bomhei.c), the lift M_U_2 held at
 * its end, the carry walk M_W_7, no jump poses over it (mot_stay.c, mot_walk.c, mot_jump.c keep them while
 * MARIO_FLAG_IS_CARRYING_BOBBERY is set). Every Play as pick has them (the animation parity rule; its runtime
 * fallback otherwise). Only while she's out: Bobbery's own carry never meets hers. */
extern s32 m_w_7_sync[];
static int sWattSync;

static void wattHold(int on, const char* why) {
    MarioWork* mario = marioGetPtr();
    if (on != sWattHeld && logOn()) {
        printf("[pm64] Watt: %s (%s)\n", on ? "in Mario's hands" : "put down", why);
        fflush(stdout);
    }
    if (on != sWattHeld && mario != NULL) {
        if (on) {
            mario->flags |= MARIO_FLAG_IS_CARRYING_BOBBERY;
            marioChgPose("M_U_2");
        } else {
            mario->flags &= ~MARIO_FLAG_IS_CARRYING_BOBBERY;
            if (mario->currMotionId == MARIO_MOTION_STAY || mario->currMotionId == MARIO_MOTION_WALK) {
                marioChgMot(MARIO_MOTION_STAY);
            }
        }
    }
    sWattHeld = on;
    sWattSync = 0;
    if (on) {
        memcpy(sWattHeldMap, gp->currentMapName, sizeof(sWattHeldMap));
    }
}

void portPm64WattUse(void* p) {
    PartyEntry* party = (PartyEntry*)p;
    /* (the use is over at once: Mario keeps his control, nothing waits on her) */
    if (party->trigFlags & PARTY_TRIG_FLAG_CHG_RUN_MODE) {
        party->trigFlags &= ~PARTY_TRIG_FLAG_CHG_RUN_MODE;
        wattHold(!sWattHeld, "the partner button");
    }
    party->flags &= ~PARTY_FLAG_IS_BEING_USED;
    partyChgRunMode(party, 2);
    marioChgMot(MARIO_MOTION_STAY);
    if (sWattHeld) {
        marioChgPose("M_U_2"); /* (after the stand: lifting her) */
    }
}

/* where she is (her own place: the game's, which the sparks and reveal measure from) */
void portPm64WattFollow(void* p) {
    PartyEntry* party = (PartyEntry*)p;
    MarioWork* mario = marioGetPtr();
    float dx, dz, d;
    if (outNow() != PM64_PARTNER_WATT || mario == NULL || party == NULL) {
        return;
    }
    if (sWattHeld) {
        /* in his raised hands, where Bobbery sits in the same carry (bomheiSetHoldPos: 36 up, bobbing with the
           carry walk M_W_7, whose frame follows hers), facing his way */
        static unsigned sLogged;
        float height = 36.0f;
        if (mario->animName != NULL && strcmp(mario->animName, "M_W_7") == 0) {
            animPoseSetLocalTime(mario->paperAnimGroupId[marioAnimeId()], (f32)sWattSync);
            height += (float)m_w_7_sync[sWattSync] / 4.0f;
            sWattSync = sWattSync + 1 >= 40 ? 0 : sWattSync + 1;
        } else {
            sWattSync = 0;
        }
        if (mario->currMotionId == MARIO_MOTION_STAY && mario->animName != NULL &&
            strcmp(mario->animName, "M_U_2") != 0) {
            /* standing (mot_stay.c leaves the carry's pose alone): holding her up, the lift's end */
            marioChgPose("M_U_2");
            mario->poseTime = 10;
        }
        party->position.x = mario->position.x;
        party->position.y = mario->position.y + height;
        party->position.z = mario->position.z;
        movePos(&party->position.x, &party->position.z, -6.0f, toMovedirSimple(mario->displayDirectionTarget));
        party->position.z += 2.0f;
        party->displayDirectionTarget = mario->displayDirectionTarget;
        party->displayDirection = mario->displayDirection;
        party->moveSpeed = 0.0f;
        if (logOn() && gp->retraceCount - sLogged > 60) {
            sLogged = gp->retraceCount;
            printf("[pm64] Watt: held, Mario %s, Watt %.0f up\n", mario->animName != NULL ? mario->animName : "?",
                   party->position.y - mario->position.y);
            fflush(stdout);
        }
        return;
    }
    dx = party->position.x - mario->position.x;
    dz = party->position.z - mario->position.z;
    d = sqrtf(dx * dx + dz * dz);
    if (d > WATT_CLOSE) {
        party->position.x = mario->position.x + dx * WATT_CLOSE / d;
        party->position.z = mario->position.z + dz * WATT_CLOSE / d;
    }
}

/* a spark on each hidden block near Watt, while she's near (the room's effects
 * go with the room: the list is forgotten then) */
#define SPARKS 16
static struct {
    char map[16];
    MapObjectEntry* block[SPARKS];
    EffectEntry* spark[SPARKS];
} sSparks;

/* Watt's own place (not Mario's: she floats behind him, or is in his hands) */
static const Vec* wattAt(void) {
    PartyEntry* party = partyGetPtr(marioGetPartyId());
    MarioWork* mario = marioGetPtr();
    return party != NULL ? &party->position : mario != NULL ? &mario->position : NULL;
}

static int nearWattIn(const MapObjectEntry* m, float r) {
    const Vec* w = wattAt();
    float dx, dz;
    if (w == NULL) {
        return 0;
    }
    dx = m->position.x - w->x;
    dz = m->position.z - w->z;
    return dx * dx + dz * dz < r * r && m->position.y - w->y < r;
}

static int nearWatt(const MapObjectEntry* m, const MarioWork* mario) {
    (void)mario;
    return nearWattIn(m, 200.0f);
}

/* Held, the hidden blocks within WATT_REVEAL of her show, see-through: the block's own model posed as it is once
 * hit (its "A_1", held at its end) with half its alpha; put back as it was when she's put down or goes, or the
 * block is hit (the game's own A_1 then, and the block's own colour). Draw only: the block stays hidden for the
 * game (hit from below, as ever). */
#define REVEALS 16
static struct {
    char map[16];
    MapObjectEntry* block[REVEALS];
    char anim[REVEALS][16];
} sReveal;

static void wattReveal(int held) {
    MapObjectWork* wp = portMobjGetWork();
    static const GXColor kSeen = {0xFF, 0xFF, 0xFF, 0x80}, kOwn = {0xFF, 0xFF, 0xFF, 0xFF};
    int i, k;
    if (strcmp(sReveal.map, gp->currentMapName) != 0) {
        memset(&sReveal, 0, sizeof(sReveal));
        memcpy(sReveal.map, gp->currentMapName, sizeof(sReveal.map));
    }
    for (k = 0; k < REVEALS; k++) {
        MapObjectEntry* m = sReveal.block[k];
        if (m == NULL) {
            continue;
        }
        if (!(m->flags & MOBJ_FLAG_USED)) {
            sReveal.block[k] = NULL;
        } else if (!(m->flags & MOBJ_FLAG_HIDDEN)) {
            /* hit: the game's own pose, its own colour */
            animPoseSetMaterialEvtColor(m->poseId, kOwn);
            animPoseSetMaterialFlagOff(m->poseId, ANIM_MATERIAL_FLAG_EVT_COLOR);
            sReveal.block[k] = NULL;
        } else if (!held || !nearWattIn(m, WATT_REVEAL)) {
            animPoseSetAnim(m->poseId, sReveal.anim[k], 1);
            animPoseSetMaterialEvtColor(m->poseId, kOwn);
            animPoseSetMaterialFlagOff(m->poseId, ANIM_MATERIAL_FLAG_EVT_COLOR);
            sReveal.block[k] = NULL;
        }
    }
    if (!held || wp == NULL || wp->entries == NULL) {
        return;
    }
    for (i = 0; i < wp->count; i++) {
        MapObjectEntry* m = &wp->entries[i];
        AnimationPose* pose;
        AnimationPoseData* data;
        int free = -1;
        if (!(m->flags & MOBJ_FLAG_USED) || !(m->flags & MOBJ_FLAG_HIDDEN) || !nearWattIn(m, WATT_REVEAL)) {
            continue;
        }
        for (k = 0; k < REVEALS && sReveal.block[k] != m; k++) {
            if (sReveal.block[k] == NULL && free < 0) {
                free = k;
            }
        }
        if (k < REVEALS || free < 0 || (pose = animPoseGetAnimPosePtr(m->poseId)) == NULL ||
            (data = animPoseGetAnimBaseDataPtr(m->poseId)) == NULL || pose->animId < 0 ||
            pose->animId >= data->animCount) {
            continue;
        }
        snprintf(sReveal.anim[free], sizeof(sReveal.anim[free]), "%s", data->anims[pose->animId].name);
        sReveal.block[free] = m;
        animPoseSetAnim(m->poseId, "A_1", 0);
        animPoseSetMaterialFlagOn(m->poseId, ANIM_MATERIAL_FLAG_EVT_COLOR);
        animPoseSetMaterialEvtColor(m->poseId, kSeen);
        if (logOn()) {
            printf("[pm64] Watt: a hidden block shows at %.0f %.0f %.0f (%s)\n", m->position.x, m->position.y,
                   m->position.z, sReveal.anim[free]);
            fflush(stdout);
        }
    }
}

static void wattFrame(int watt) {
    MarioWork* mario = marioGetPtr();
    if (sWattHeld && (!watt || mario == NULL || strcmp(sWattHeldMap, gp->currentMapName) != 0)) {
        wattHold(0, !watt ? "she isn't out" : "another room");
    } else if (sWattHeld && marioKeyOffChk()) {
        wattHold(0, "a scene");
    }
    wattReveal(watt && sWattHeld);
}

static void wattBlocks(int watt) {
    MarioWork* mario = marioGetPtr();
    MapObjectWork* wp = portMobjGetWork();
    int i, k;
    if (strcmp(sSparks.map, gp->currentMapName) != 0) {
        memset(&sSparks, 0, sizeof(sSparks));
        memcpy(sSparks.map, gp->currentMapName, sizeof(sSparks.map));
    }
    /* the ones no longer wanted: Watt gone, the block hit (shown) or far */
    for (k = 0; k < SPARKS; k++) {
        MapObjectEntry* m = sSparks.block[k];
        if (m != NULL && (!watt || mario == NULL || !(m->flags & MOBJ_FLAG_USED) || !(m->flags & MOBJ_FLAG_HIDDEN) ||
                          !nearWatt(m, mario))) {
            EffectEntry* e = sSparks.spark[k];
            /* (still ours: a room loaded again under the same name frees them) */
            if (e != NULL && (e->flags & EFFECT_FLAG_IN_USE) && e->type != NULL && !strcmp(e->type, "Spark")) {
                effSoftDelete(e);
            }
            sSparks.block[k] = NULL;
            sSparks.spark[k] = NULL;
        }
    }
    if (!watt || mario == NULL || wp == NULL || wp->entries == NULL || gp->retraceCount % 10 != 0) {
        return;
    }
    for (i = 0; i < wp->count; i++) {
        MapObjectEntry* m = &wp->entries[i];
        int free = -1;
        if (!(m->flags & MOBJ_FLAG_USED) || !(m->flags & MOBJ_FLAG_HIDDEN) || !nearWatt(m, mario)) {
            continue;
        }
        for (k = 0; k < SPARKS && sSparks.block[k] != m; k++) {
            if (sSparks.block[k] == NULL && free < 0) {
                free = k;
            }
        }
        if (k == SPARKS && free >= 0) {
            sSparks.block[free] = m;
            sSparks.spark[free] = effSparkEntry(0, m->position.x, m->position.y + 12.0f, m->position.z);
            if (logOn()) {
                printf("[pm64] Watt: a hidden block sparks at %.0f %.0f %.0f\n", m->position.x, m->position.y,
                       m->position.z);
                fflush(stdout);
            }
        }
    }
}

/* tests: TTYD_PM64_UNLINK_AT=<frames> unlinks the ROM that many frames into play (the field and battles), as
 * the settings page's Unlink while playing */
static void testUnlink(void) {
    static int sAt = -2, sFrames;
    if (sAt == -2) {
        const char* v = getenv("TTYD_PM64_UNLINK_AT");
        sAt = v != NULL && v[0] != 0 ? atoi(v) : -1;
    }
    if (sAt > 0 && (seqGetSeq() == SEQ_GAME || seqGetSeq() == SEQ_BATTLE) && ++sFrames == sAt) {
        portPm64Unlink();
        printf("[pm64] TTYD_PM64_UNLINK_AT: unlinked in %s\n", gp->currentMapName);
        fflush(stdout);
    }
}

/* --- Bow (on Vivian): Outta Sight. Vivian's Veil hides Mario as ever (its rules, its timing, what passes him
 * by); PM64's look instead of the sinking into a shadow (pm64_aurora.c's hooks leave out the shadow's paper
 * and the sunk pose). PM64's world_bow UseAbility (OUTTA_SIGHT_GATHER, _VANISH, _REAPPEAR): she gathers onto
 * Mario in 5 frames, then the two fade by 8 a frame from 255 to 128 and stay so; coming out, both are opaque
 * at once. PM64 runs 30 frames a second, TTYD 60: here 10 frames, then 4 a frame over 32. */

enum { BOW_GATHER = 10, BOW_FADE = 32 };
static int sBowVeilOn;   /* hidden (the hooks) */
static unsigned sBowVeilAt; /* the frame they last said so */
static int sBowVeilF;    /* frames into hiding (0: out) */
static int sBowVeilTint; /* Mario's tint is ours */

void portPm64BowVeilSet(int on) {
    sBowVeilOn = on;
    sBowVeilAt = gp != NULL ? gp->retraceCount : 0;
}

/* how far she has gathered onto Mario (0 .. 1) */
float portPm64BowVeil(void) {
    const float t = (float)sBowVeilF / (float)BOW_GATHER;
    return outNow() != PM64_PARTNER_BOW ? 0.0f : t > 1.0f ? 1.0f : t;
}

/* the two's alpha while hidden (255 .. 128) */
int portPm64BowVeilAlpha(void) {
    const int f = sBowVeilF - BOW_GATHER;
    if (outNow() != PM64_PARTNER_BOW || f <= 0) {
        return 255;
    }
    return f >= BOW_FADE ? 128 : 255 - 4 * f;
}

/* Mario's draw this frame while see-through (portPm64BowVeilDefer): his matrix and the stages the game drew
 * (marioDisp: 2 then 1), drawn again by bowVeilDraw among the translucent things */
static struct {
    int pose;
    int stages; /* bit n: stage n asked for */
    int drawing;
    Mtx mtx;
    float rotY, scale;
} sBowDefer = {-1};

int portPm64BowVeilDefer(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale) {
    if (sBowDefer.drawing || poseId < 0 || poseId != sBowDefer.pose || !sBowVeilTint || gp == NULL ||
        gp->inBattle || xluMode < 0 || xluMode > 3) {
        return 0;
    }
    PSMTXCopy(mtx, sBowDefer.mtx);
    sBowDefer.rotY = rotY;
    sBowDefer.scale = scale;
    sBowDefer.stages |= 1 << xluMode;
    return 1;
}

static void bowVeilDraw(CameraId cam, void* param) {
    (void)cam;
    (void)param;
    if (sBowDefer.pose < 0 || sBowDefer.stages == 0) {
        return;
    }
    sBowDefer.drawing = 1;
    if (logOn() && portPm64BowVeilAlpha() == 128) {
        static unsigned sLogged;
        if (gp->retraceCount - sLogged > 60) {
            sLogged = gp->retraceCount;
            printf("[pm64] Bow: Mario drawn see-through (alpha 128) with the translucent things\n");
            fflush(stdout);
        }
    }
    if (sBowDefer.stages & 4) {
        portLuigiDrawMtx(sBowDefer.pose, sBowDefer.mtx, 2, sBowDefer.rotY, sBowDefer.scale);
    }
    if (sBowDefer.stages & 2) {
        portLuigiDrawMtx(sBowDefer.pose, sBowDefer.mtx, 1, sBowDefer.rotY, sBowDefer.scale);
    }
    sBowDefer.drawing = 0;
    sBowDefer.stages = 0;
}

static void bowFrame(void) {
    MarioWork* m = marioGetPtr();
    const int bow = outNow() == PM64_PARTNER_BOW;
    PartyEntry* party = partyGetPtr(marioGetPartyId());
    AnimationPose* ap;
    int pose;
    /* (hidden only while Veil is in use: a hit, a scene or a partner change ends it at once) */
    if (!bow || gp->inBattle || party == NULL || !(party->flags & PARTY_FLAG_IS_BEING_USED) ||
        gp->retraceCount - sBowVeilAt > 2) {
        sBowVeilOn = 0;
    }
    if (sBowVeilOn && bow && m != NULL) {
        if (sBowVeilF == 0 && logOn()) {
            printf("[pm64] Bow: Outta Sight on (field)\n");
            fflush(stdout);
        }
        sBowVeilF += sBowVeilF < BOW_GATHER + BOW_FADE ? 1 : 0;
    } else {
        if (sBowVeilF > 0 && logOn()) {
            printf("[pm64] Bow: Outta Sight off (field)\n");
            fflush(stdout);
        }
        sBowVeilF = 0; /* (out at once, as PM64's end_outta_sight_cleanup) */
    }
    if (m == NULL) {
        sBowVeilTint = 0;
        return;
    }
    pose = m->paperAnimGroupId[marioAnimeId()];
    /* Mario see-through: the scripts' tint (ANIM_MATERIAL_FLAG_EVT_COLOR, as the game's motion blur) with the
       alpha, and his draw held back to the translucent things (bowVeilDraw). Drawn among the opaque things, as
       the game draws him, a see-through pose writes no depth: what lies behind him and is drawn after him (near
       to far there) covered him, so he vanished in some rooms (tik_04) and wasn't see-through in others. */
    (void)ap;
    if (sBowVeilF > 0 && portPm64BowVeilAlpha() < 255) {
        const GXColor c = {0xFF, 0xFF, 0xFF, (u8)portPm64BowVeilAlpha()};
        Vec at = m->position;
        float z;
        animPoseSetMaterialFlagOn(pose, ANIM_MATERIAL_FLAG_EVT_COLOR);
        animPoseSetMaterialEvtColor(pose, c);
        sBowVeilTint = 1;
        sBowDefer.pose = pose;
        sBowDefer.stages = 0;
        PSMTX44MultVec(camGetPtr(CAMERA_3D)->projection, &at, &at);
        z = 5000.0f * at.z + 5000.0f;
        dispEntry((CameraId)marioGetCamId(), z < 0.0f ? 0.0f : z > 10000.0f ? 10000.0f : z, 2, bowVeilDraw, NULL);
    } else if (sBowVeilTint) {
        int k;
        const GXColor c = {0xFF, 0xFF, 0xFF, 0xFF};
        for (k = 0; k < 3; k++) { /* (front, back and the second set: whichever he turned to while hidden) */
            if (m->paperAnimGroupId[k] >= 0) {
                animPoseSetMaterialEvtColor(m->paperAnimGroupId[k], c);
                animPoseSetMaterialFlagOff(m->paperAnimGroupId[k], ANIM_MATERIAL_FLAG_EVT_COLOR);
            }
        }
        sBowVeilTint = 0;
        sBowDefer.pose = -1;
    }
}

/* tests: TTYD_PM64_JOIN_AT=<frames>,<party> has TTYD partner party (1..7) join that many frames into the field, as
 * the story's own joining does (partyJoin: its PM64 partners then join with it, portPm64JoinFrame; Ms. Mowz ends
 * Watt's stand-in) */
static void testJoin(void) {
    static int sAt = -2, sParty, sFrames;
    if (sAt == -2) {
        const char* v = getenv("TTYD_PM64_JOIN_AT");
        sAt = v != NULL && v[0] != 0 ? atoi(v) : -1;
        sParty = v != NULL && strchr(v, ',') != NULL ? atoi(strchr(v, ',') + 1) : 0;
    }
    if (sAt > 0 && sParty >= 1 && sParty <= 7 && seqGetSeq() == SEQ_GAME && ++sFrames == sAt &&
        pouchGetPtr() != NULL) {
        partyJoin((PartyId)sParty);
        printf("[pm64] TTYD_PM64_JOIN_AT: partner %d joined in %s\n", sParty, gp->currentMapName);
        fflush(stdout);
    }
}

/* --- Goombario's Tattle (on Goombella: party_kuribo.c's pre_kurio_use)
 *
 * He tells of the characters Paper Mario has too, and of a few rooms, in his own words: Paper Mario's field Tattles,
 * read from the linked ROM's message banks (section 0x1A, the characters; 0x19, the rooms) as he speaks. Only
 * where his line is true here: the same character (Luigi, Merlon, Merlee, Rowf), the same kind (a Whacka, a
 * Bub-ulb), or a room his line describes as it is (a Save Block, a palace passage, a tree's wide hollow). Anywhere
 * else, and in a language the ROM has no text in, Goombella's line as ever. This table holds the message ids
 * only; the text is the player's ROM's. */
static const struct {
    const char* map;    /* the room (NULL: any) */
    const char* npc;    /* the NPC's name, its Tattle's tag (the game's Shift JIS); NULL: the room's own Tattle */
    unsigned short msg; /* PM64_MSG(0x1A, msg) for an NPC, PM64_MSG(0x19, msg) for a room */
} kTattles[] = {
    /* the characters */
    {NULL, "\x83\x8b\x83\x43\x81\x5b\x83\x57", 0x69},                             /* ルイージ: Luigi (Rogueport) */
    {"gor_02", "\x83\x70\x83\x8f\x81\x5b\x83\x41\x83\x62\x83\x76\x89\xae", 0x21}, /* パワーアップ屋: Merlon */
    {"tik_12", "\x82\xdc\x82\xb6\x82\xc8\x82\xa2\x8e\x74", 0x9B},                 /* まじない師: Merlee */
    {NULL, "pc_pm64_rowf", 0x24},                                                 /* Rowf (pm64_badges_aurora.c) */
    {"muj_00", "\x83\x52\x83\x75\x83\x8d\x83\x93", 0x89},                         /* コブロン: the Whacka */
    {"nok_01", "\x83\x6d\x83\x52\x83\x6d\x83\x52\x47", 0x32},                     /* ノコノコG: a Bub-ulb */
    /* the rooms */
    {"gon_00", NULL, 0x3F},  /* Hooktail Castle's gate: "There's a Save Block" */
    {"las_27", NULL, 0x3F},  /* the Palace of Shadow's quiet room, and its Save Block */
    {"las_02", NULL, 0x140}, /* the Palace of Shadow's long corridor: "a palatial passage" */
    {"mri_11", NULL, 0xFF},  /* the Great Tree's widest hollow: "so spacious... inside a tree" */
};

/* the PM64 message language that is the game's text language, -1 none: Japanese (a JP ROM's text isn't
 * decoded) and Italian (no Paper Mario in it) have none; the European release's German, French and Spanish
 * only with a European ROM, which has them (port/include/port/pm64.h) */
int portPm64TextLanguage(void) {
    static const uint32_t kSeg[4] = {PM64_SEG_MESSAGES, PM64_SEG_MESSAGES_DE, PM64_SEG_MESSAGES_FR,
                                     PM64_SEG_MESSAGES_ES};
    const PortVersionInfo* v = portVersionSelf();
    if (v == NULL || v->id[3] == 'J') {
        return -1;
    }
    if (v->id[3] != 'P') {
        return 0; /* (the US release's text is English) */
    }
    /* gp->language: msgdrv.c's language_dir, 1 English .. 4 Spanish, 5 Italian */
    if (gp == NULL || gp->language < 1 || gp->language > 4) {
        return -1;
    }
    return portPm64Segment(kSeg[gp->language - 1], NULL) != NULL ? (int)gp->language - 1 : -1;
}

const char* portPm64FieldTattle(const char* npc, const char* tag) {
    static const char kTag[] = "pc_pm64_gtattle";
    const char* map;
    const char* said = tag;
    char raw[1024], text[1100];
    size_t o = 0;
    int i, n, lang, which = -1;
    if (tag == NULL || gp == NULL || outNow() != PM64_PARTNER_GOOMBARIO) {
        return tag;
    }
    map = gp->currentMapName;
    for (i = 0; i < (int)(sizeof(kTattles) / sizeof(kTattles[0])); i++) {
        if ((kTattles[i].map == NULL || strcmp(kTattles[i].map, map) == 0) &&
            (npc != NULL ? kTattles[i].npc != NULL && strcmp(kTattles[i].npc, npc) == 0
                         : kTattles[i].npc == NULL && strcmp(tag, "msg_kuri_map") == 0)) {
            which = i;
            break;
        }
    }
    lang = n = -1;
    if (which < 0 || (lang = portPm64TextLanguage()) < 0 ||
        (n = portPm64MessageText(PM64_MSG(kTattles[which].npc != NULL ? 0x1A : 0x19, kTattles[which].msg), lang,
                                 raw, sizeof(raw))) <= 0) {
        if (logOn()) {
            printf("[pm64] Goombario's Tattle in %s: Goombella's line (%s)%s\n", map, tag,
                   which < 0 ? "" : lang < 0 ? ": not in the ROM's languages" : ": no such message in the ROM");
            fflush(stdout);
        }
        return tag;
    }
    /* his pages (a line break, a wait and the next page: a blank line) as the game's, the lines rewrapped */
    for (i = 0; i < n && o + 8 < sizeof(text); i++) {
        if (raw[i] == '\n' && i + 1 < n && raw[i + 1] == '\n') {
            memcpy(text + o, "<p>", 3);
            o += 3;
            while (i + 1 < n && raw[i + 1] == '\n') {
                i++;
            }
        } else {
            text[o++] = raw[i] == '\n' ? ' ' : raw[i];
        }
    }
    memcpy(text + o, "<k>", 4);
#ifdef PORT_MODS
    portModsSetMessage(kTag, portGameLayout(text, 30, 3));
    said = kTag;
#endif
    if (logOn()) {
        printf("[pm64] Goombario's Tattle in %s: his own, PM64 %02X%04X as %s\n", map,
               kTattles[which].npc != NULL ? 0x1A : 0x19, kTattles[which].msg, said);
        fflush(stdout);
    }
    return said;
}

/* each frame (pm64_aurora.c's frame hook) */
void portPm64FieldFrame(void) {
    int watt;
    if (gp == NULL) {
        return;
    }
    testUnlink();
    bowFrame();
    testJoin();
    portPm64JoinFrame();
    watt = outNow() == PM64_PARTNER_WATT;
    if (!gp->inBattle) {
        logRoom();
    }
    sushieBoardFrame();
    sushieFrame();
    parakarryFrame();
    lakilesterFrame();
    if (gp->inBattle && sWattHeld) {
        wattHold(0, "a battle");
    }
    if (!gp->inBattle) {
        wattFog(watt);
        wattFrame(watt);
        wattBlocks(watt);
    }
}
