/* Brothers mode (port/include/port/brothers.h, docs/BROTHERS.md): Luigi in
 * the field, his HP and FP between battles, and the second controller's
 * routing for his turn in battle. The battle's own hooks are in
 * port/src/aurora/brothers_aurora.c.
 *
 * Luigi in the field is a PartyEntry of the game's party code, in a slot the
 * game never hands out (party.c's partyPtrTbl[PORT_PARTY_SLOT_LUIGI],
 * port/include/port/players.h; player 4's companion has the slot before): the
 * game drives and draws him with the partners (partyMain, driveParty,
 * partyPreDisp), with his model (Play as Luigi's c_luigi_pc, else the disc's
 * c_luigi) and the walking followers' pose names (Punio's row of
 * partyPoseTbl: Z_1, S_1, W_1, R_1, J_1A, J_1B, T_1, which Luigi's model
 * has). partyWalkMain hands his frame to portBrothersWalk, which moves him
 * with partyMoveNoHosei (walls, floors, falls) by the second player's stick
 * or along a trail of Mario's steps of its own (the game's two footmark
 * trails belong to the partner and the follower), so no script, save, menu
 * or footmark ever sees him.
 *
 * Every decision here follows from the game's state, the settings (online
 * sends the host's) and the second controller's input (online, the guest's),
 * so both games of an online session do the same. The state is in the
 * save-state section (CMakeLists.txt). */
#include "port/brothers.h"
#include "port/coop_camera.h"
#include "port/duo.h"
#include "port/gameplus.h"
#include "port/luigi.h"
#include "port/players.h"
#include "port/pm64_spin.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/versus.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/animdrv.h"
#include "driver/hitdrv.h"
#include "driver/mobjdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mario_cam.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mario_sbr.h"
#include "mario/mariost.h"
#include "memory.h"
#include "party.h"
#include "party/party_motion.h"
#include "party/party_move.h"
#include "party/party_yoshi.h"
#include "pmario_sound.h"
#include "sfx_name_word.h"
#include "system.h"
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern PartyEntry* partyPtrTbl[];                                   /* party.c (PORT_PARTY_SLOTS) */
EventEntry* mobjRunEvent(MapObjectEntry* entry, EvtWord* eventCode); /* mobjdrv.c */
MapObjectWork* portMobjGetWork(void);                                /* mobjdrv.c */
BOOL marioChkTalkable(void);                                         /* mario_motion.c */

#define LUIGI_SLOT PORT_PARTY_SLOT_LUIGI
#define LUIGI_MEMBER PARTY_MEMBER_PUNIO /* the pose-name row (partyPoseTbl[10]) */

#define DEAD_ZONE 14.0f   /* of the stick's 70 */
#define LEASH 300.0f      /* following Mario, twice this far behind: back at his side (walked: the picture's edge) */
#define FOLLOW_DIST 64.0f /* how far along Mario's steps he walks behind (past the partner) */
#define TRAIL 64          /* Mario's steps kept */
#define IDLE_FRAMES 150   /* the second player's hands off this long: he follows Mario again */
#define JUMP_SPEED 9.5f   /* a little higher than Mario's */
#define GRAVITY (-0.8f)
#define HAMMER_FRAMES 26
#define HAMMER_HIT 9      /* the frame the hammer comes down */
#define HAMMER_REACH 34.0f

static int* sOn;

static struct {
    /* the field */
    int driven;       /* the second player walks him (else he follows Mario) */
    int idle;         /* frames the second player has left him alone */
    int jump, hammer; /* A, B pressed (taken by the next walk frame) */
    float stickX, stickY;
    int swing;        /* frames into a hammer swing (0: none) */
    int swingHit;     /* this swing hit something */
    int tucked;       /* out of sight (a pipe, a paper form, Yoshi, ...) */
    int airborne;     /* last frame */
    int headHit;      /* this jump worked a block from below */
    int step;         /* his left or right foot next */
    int blocked;      /* a wall in front of him last frame */
    int stuck;        /* frames following without getting nearer */
    float lastDist;
    char map[16];
    struct {
        Vec pos;
        int jumped; /* in the air there: the jump of Mario's it is a point of (counted from the start), -1 a fall */
        int serial; /* which step (counted from the start) */
    } trail[TRAIL];
    int trailHead, trailCount, trailSerial;
    int marioJump, marioJumps; /* Mario's jump under way (as jumped), and his jumps counted */
    int marioGround;           /* frames Mario has been on the ground */
    int hopSerial;   /* the step of Mario's he last hopped at (one hop each) */
    float climbY;    /* the floor he tries to hop up to, and how often he tried (once each floor) */
    int climbTries;
    int still;       /* frames Mario hasn't stepped on (a jump in place is no step: only across the ground counts) */
    Vec stillAt;     /* where Mario stands still (his floor's height there: a landing elsewhere is a step) */
    /* his copy of a jump of Mario's, steered in the air: 1 with Mario, his place beside him kept (aimOff from Mario;
       Mario took off at aimFrom); 2 down at aim, on the floor Mario came down on */
    int aimOn;
    Vec aim, aimFrom, aimOff;
    /* Mario's first step he follows on (come down beside Mario on a ledge: not back down the steps before it) */
    int trailFrom;
    int walled;      /* frames a wall has been between him and Mario */
    int arrive;      /* frames into a room's arrival (0: none): out of sight until Mario's entrance is done */
    int marioHidden; /* frames Mario hasn't been drawn (a scene hides him) */
    int marioAway;   /* frames Mario has had no floor under him (a scene holds him out of the room) */
    int carried, carryRest; /* frames a scene has carried Mario up or down (a lift), and since it stopped */
    float marioLastX, marioLastY, marioLastZ;
    int noPlace, noPlaceAge; /* nowhere beside Mario to stand: out of sight (checked again every few frames) */
    /* his own place beside Mario while Mario stands or a scene goes on (not in the partner, an NPC or Mario) */
    int spotOn, spotAge, spotStuck;
    Vec spot, spotAnchor;
    float spotLastD;
    float camX, camZ; /* the camera's shift toward him */
    /* HP and FP between battles (-1: full) */
    int hp, fp;
    int lastHp, lastFp, lastMaxHp, lastMaxFp;
    int luigiUnit; /* this battle's (-1 none) */
    /* Paper Mario 64's Spin Dash (port/include/port/pm64_spin.h), his as Mario's */
    int spinReq;   /* its control pressed (taken by the next walk frame) */
    int spin;      /* frames into a spin (0: none) */
    int spinCool;  /* frames before the next */
    int spinHeld;  /* frames held up by a wall */
    float spinDir, spinRun;
    float spinSign;  /* the way his paper turns */
    int spinEnding;  /* a wall cut it short: turning to its end */
    int spinLocked;  /* this set PARTY_DISP_FLAG_DIR_LOCK (his paper the spin's to turn) */
    void* spinRibbon; /* the badges' ribbons round him (pm64_spin.h), and their work */
    void* spinRibbonWork;
    PortSpinShape spinShape;
} sB = {.hp = -1, .fp = -1, .lastHp = -1, .lastFp = -1, .lastMaxHp = -1, .lastMaxFp = -1, .luigiUnit = -1};

/* an online co-op guest's Luigi's field actions (statesync_coop_pc.inc "the guest's field actions"; not in the save
   state: an online session's): the guest's game, its swing played here (its hits the host's) and its talk's pose
   (frames Luigi stands in it); the host's game, a guest's swing under way here (its result told once) */
static struct {
    int swingLocal; /* the guest's: this swing is its own, played here: what it hits is the host's game's */
    int act;        /* the guest's: frames Luigi stands in his talk's pose (the host's talk follows) */
    int guestSwing; /* the host's: this swing is a guest's, from its game's action */
    int told;       /* ... its result told */
    int spinLocal;  /* the guest's: this spin is its own, played here: what it touches is the host's game's */
    int guestSpin;  /* the host's: frames left of a guest's Luigi's spin, from its game's action */
} sG;

static int settingOn(void) {
    if (sOn == NULL) {
        sOn = portSettingsDeclare("brothers_mode", 0, 0, 1);
    }
    return sOn != NULL && *sOn;
}

int portBrothersSetting(void) {
    return settingOn();
}

int portBrothersOn(void) {
    MarioWork* mario;
    if (!settingOn() || gp == NULL || portVersusOn() || (mario = marioGetPtr()) == NULL) {
        return 0; /* (no Mario yet: before the game's start, a settings page's question) */
    }
    /* Peach's and Bowser's interludes: the brothers aren't there */
    return mario->characterId == 0;
}

static PartyEntry* luigi(void) {
    return partyPtrTbl[LUIGI_SLOT];
}

int portBrothersIsLuigi(const PartyEntry* party) {
    return party != NULL && party == luigi();
}

PartyEntry* portBrothersFieldLuigi(void) {
    PartyEntry* p = luigi();
    return p != NULL && !(p->flags2 & PARTY_DISP_FLAG_HIDDEN) ? p : NULL;
}

int portBrothersDriven(void) {
    return sB.driven;
}

PartyEntry* portBrothersLuigiAny(void) {
    return luigi();
}

int portBrothersInField(void) {
    return luigi() != NULL && gp != NULL && !gp->inBattle;
}

/* --- HP and FP --- */

void portBrothersGetStats(int* hp, int* fp) {
    const PouchData* pouch = pouchGetPtr();
    *hp = sB.hp < 0 || sB.hp > pouch->maximumHP ? pouch->maximumHP : sB.hp;
    *fp = sB.fp < 0 || sB.fp > pouch->maximumFP ? pouch->maximumFP : sB.fp;
}

/* Mario's numbers as they are now, taken in: the next healFrame doesn't give Luigi a heal twice */
static void heardMario(void) {
    const PouchData* pouch = pouchGetPtr();
    sB.lastHp = pouch->currentHP;
    sB.lastFp = pouch->currentFP;
    sB.lastMaxHp = pouch->maximumHP;
    sB.lastMaxFp = pouch->maximumFP;
}

/* each change to Luigi's numbers between battles, in the log (the test pass's brothers_hp_* scenarios) */
static void saidStats(const char* why) {
    static int sSaidHp = -2, sSaidFp = -2, sSaidMaxHp, sSaidMaxFp;
    const PouchData* pouch = pouchGetPtr();
    int hp, fp;
    portBrothersGetStats(&hp, &fp);
    if (hp == sSaidHp && fp == sSaidFp && pouch->maximumHP == sSaidMaxHp && pouch->maximumFP == sSaidMaxFp) {
        return;
    }
    sSaidHp = hp;
    sSaidFp = fp;
    sSaidMaxHp = pouch->maximumHP;
    sSaidMaxFp = pouch->maximumFP;
    printf("[brothers] Luigi HP %d/%d FP %d/%d (%s; Mario HP %d/%d FP %d/%d)\n", hp, pouch->maximumHP, fp,
           pouch->maximumFP, why, pouch->currentHP, pouch->maximumHP, pouch->currentFP, pouch->maximumFP);
    fflush(stdout);
}

void portBrothersSetStats(int hp, int fp) {
    sB.hp = hp;
    sB.fp = fp;
    /* (what the battle left Mario with isn't healing in the field; a level-up's new maximums were the battle's,
       Luigi's unit full with them: brothers_aurora.c) */
    heardMario();
    saidStats("after the battle");
}

void portBrothersHealFull(const char* why) {
    if (gp == NULL || gp->inBattle || !portBrothersOn()) {
        return; /* (a level-up is the battle's: brothers_aurora.c) */
    }
    sB.hp = sB.fp = -1;
    heardMario();
    saidStats(why);
}

void portBrothersHealMario(int hp, int fp) {
    const PouchData* pouch = pouchGetPtr();
    int lHp, lFp;
    if (gp == NULL || gp->inBattle || !portBrothersOn()) {
        return;
    }
    if (hp > 0 || fp > 0) {
        portBrothersGetStats(&lHp, &lFp);
        lHp += hp > 0 ? hp : 0;
        lFp += fp > 0 ? fp : 0;
        sB.hp = lHp > pouch->maximumHP ? pouch->maximumHP : lHp;
        sB.fp = lFp > pouch->maximumFP ? pouch->maximumFP : lFp;
    }
    heardMario(); /* (damage, or a number set lower, is Mario's own) */
    saidStats("Mario healed");
}

/* What else raises Mario's numbers between battles (a mod's, the practice tools', online's) is Luigi's too:
 * what Mario gets back he gets, and he is full when Mario is. */
static void healFrame(void) {
    const PouchData* pouch = pouchGetPtr();
    int hp, fp;
    portBrothersGetStats(&hp, &fp);
    if (sB.lastHp >= 0 && pouch->currentHP > sB.lastHp) {
        hp = pouch->currentHP >= pouch->maximumHP ? pouch->maximumHP : hp + (pouch->currentHP - sB.lastHp);
    }
    if (sB.lastFp >= 0 && pouch->currentFP > sB.lastFp) {
        fp = pouch->currentFP >= pouch->maximumFP ? pouch->maximumFP : fp + (pouch->currentFP - sB.lastFp);
    }
    /* a level-up's new maximum: as full as Mario's gets */
    if (sB.lastMaxHp >= 0 && pouch->maximumHP > sB.lastMaxHp && pouch->currentHP >= pouch->maximumHP) {
        hp = pouch->maximumHP;
    }
    if (sB.lastMaxFp >= 0 && pouch->maximumFP > sB.lastMaxFp && pouch->currentFP >= pouch->maximumFP) {
        fp = pouch->maximumFP;
    }
    sB.hp = hp > pouch->maximumHP ? pouch->maximumHP : hp;
    sB.fp = fp > pouch->maximumFP ? pouch->maximumFP : fp;
    sB.lastHp = pouch->currentHP;
    sB.lastFp = pouch->currentFP;
    sB.lastMaxHp = pouch->maximumHP;
    sB.lastMaxFp = pouch->maximumFP;
    saidStats("as Mario's");
}

/* --- Luigi's entry --- */

static const char* model(void) {
    return portLuigiModelReady() ? "c_luigi_pc" : "c_luigi";
}

static int hasAnim(PartyEntry* p, const char* name) {
    AnimationPoseData* d;
    int i;
    if (p->paperAnimGroup < 0) {
        return 0;
    }
    d = animPoseGetAnimBaseDataPtr(p->paperAnimGroup);
    for (i = 0; i < d->animCount; i++) {
        if (!strcmp(d->anims[i].name, name)) {
            return 1;
        }
    }
    return 0;
}

static void clearMotion(PartyEntry* p) {
    p->flags &= ~(PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING | PARTY_FLAG_IS_STEPPING | PARTY_FLAG_FORCE_FALL);
    p->flags |= PARTY_FLAG_IS_GROUNDED;
    p->velY = 0.0f;
    p->accelY = 0.0f;
    p->gravity = 0.0f;
    p->moveSpeed = 0.0f;
    partySetTargetSpd(p, 0.0f);
    p->hitobjAction = NULL;
    p->hitobjFront = NULL;
    p->hitobjStandOn = NULL;
    p->hitobjJumpFrom = NULL;
    p->hitobjPush = NULL;
    p->hitobjHead = NULL;
    p->hitobjUnder = NULL;
    p->damageHit = NULL;
    sB.swing = 0;
    sB.airborne = 0;
    sB.stuck = 0;
    if (sG.guestSwing && !sG.told) {
        portStateSyncCoopActResult(2, ""); /* (a guest's swing cut short: nothing hit) */
    }
    sG.guestSwing = sG.told = sG.swingLocal = 0;
    sG.act = 0;
}

/* --- his place: walls, floors, the others --- */

static float traceFloorGap(const Vec* at) {
    HitCheckQuery q;
    memset(&q, 0, sizeof(q));
    q.targetPos = (Vec){at->x, at->y + 10.0f, at->z};
    q.targetDir = (Vec){0.0f, -1.0f, 0.0f};
    q.targetDistance = 400.0f;
    return hitCheckVecFilter(&q, NULL) != NULL ? at->y - q.hitPos.y : 9999.0f;
}

/* the walls a partner's rays see (party_hit.c's chkfilter), without the map objects: a block between them is no
   wall between him and Mario */
static BOOL wallFilter(HitCheckQuery* query, HitEntry* entry) {
    (void)query;
    if (entry->attributes & (HIT_ATTR_ALWAYS_SOLID | HIT_ATTR_TUBE_PASS)) {
        return TRUE;
    }
    return !(entry->attributes & (HIT_ATTR_PASS_THROUGH | HIT_ATTR_PARTY_PASS | HIT_ATTR_MOBJ));
}

/* a wall between two places (at body height), pad past the second one counted too (his width) */
static int wallBetweenPad(const Vec* a, const Vec* b, float pad) {
    HitCheckQuery q;
    const float dx = b->x - a->x, dz = b->z - a->z, d = hypotf(dx, dz);
    if (d < 1.0f) {
        return 0;
    }
    memset(&q, 0, sizeof(q));
    q.targetPos = (Vec){a->x, a->y + 20.0f, a->z};
    q.targetDir = (Vec){dx / d, 0.0f, dz / d};
    q.targetDistance = d + pad;
    return hitCheckVecFilter(&q, wallFilter) != NULL;
}

static int wallBetween(const Vec* a, const Vec* b) {
    return wallBetweenPad(a, b, 0.0f);
}

/* one stands in another, or right in front of or behind him, as the camera (yaw) sees them */
static int overlapsAt(float x, float z, const Vec* o, float yaw) {
    float fx = 0.0f, fz = 0.0f, dx = o->x - x, dz = o->z - z, lat, depth;
    movePos(&fx, &fz, 1.0f, yaw);
    lat = fabsf(dx * fz - dz * fx);
    depth = fabsf(dx * fx + dz * fz);
    return hypotf(dx, dz) < 22.0f || (lat < 16.0f && depth < 70.0f);
}

/* the others he must not stand in: the party's other entries (the partner, the follower, players 3 and 4's), the
   NPCs shown; the one's name in *who */
static int overlapsOthers(PartyEntry* p, float x, float z, float y, const char** who) {
    MarioWork* mario = marioGetPtr();
    NpcWork* nw = npcGetWorkPtr();
    int i;
    for (i = 0; i < PORT_PARTY_SLOTS; i++) {
        PartyEntry* o = partyPtrTbl[i];
        if (o == NULL || o == p || !(o->flags & PARTY_FLAG_IS_ACTIVE) || (o->flags2 & PARTY_DISP_FLAG_HIDDEN) ||
            fabsf(o->position.y - y) > 60.0f) {
            continue;
        }
        if (overlapsAt(x, z, &o->position, mario->targetYaw)) {
            if (who != NULL) {
                *who = "partner";
            }
            return 1;
        }
    }
    for (i = 0; nw != NULL && i < nw->total; i++) {
        NpcEntry* n = &nw->entries[i];
        if ((n->flags & (NPC_FLAG_USED | NPC_FLAG_ACTIVE)) != (NPC_FLAG_USED | NPC_FLAG_ACTIVE) ||
            (n->flags & (NPC_FLAG_HIDDEN | NPC_FLAG_NO_DISPLAY)) || fabsf(n->position.y - y) > 60.0f) {
            continue;
        }
        if (overlapsAt(x, z, &n->position, mario->targetYaw)) {
            if (who != NULL) {
                *who = n->description;
            }
            return 1;
        }
    }
    return 0;
}

/* how clear of the others (the party, the NPCs shown, Mario) a place is, as the camera sees it: the least of
   their sideways distances from it (0: in someone; 100: nobody near) */
static float sepFrom(float x, float z, const Vec* o, float yaw) {
    float fx = 0.0f, fz = 0.0f, dx = o->x - x, dz = o->z - z, lat, depth;
    movePos(&fx, &fz, 1.0f, yaw);
    lat = fabsf(dx * fz - dz * fx);
    depth = fabsf(dx * fx + dz * fz);
    if (hypotf(dx, dz) < 22.0f) {
        return 0.0f;
    }
    return depth < 70.0f && lat < 100.0f ? lat : 100.0f;
}

static float clearance(PartyEntry* p, float x, float z, float y) {
    MarioWork* mario = marioGetPtr();
    NpcWork* nw = npcGetWorkPtr();
    float best = sepFrom(x, z, &mario->position, mario->targetYaw), s;
    int i;
    for (i = 0; i < PORT_PARTY_SLOTS; i++) {
        PartyEntry* o = partyPtrTbl[i];
        if (o != NULL && o != p && (o->flags & PARTY_FLAG_IS_ACTIVE) && !(o->flags2 & PARTY_DISP_FLAG_HIDDEN) &&
            fabsf(o->position.y - y) <= 60.0f && (s = sepFrom(x, z, &o->position, mario->targetYaw)) < best) {
            best = s;
        }
    }
    for (i = 0; nw != NULL && i < nw->total; i++) {
        NpcEntry* n = &nw->entries[i];
        if ((n->flags & (NPC_FLAG_USED | NPC_FLAG_ACTIVE)) == (NPC_FLAG_USED | NPC_FLAG_ACTIVE) &&
            !(n->flags & (NPC_FLAG_HIDDEN | NPC_FLAG_NO_DISPLAY)) && fabsf(n->position.y - y) <= 60.0f &&
            (s = sepFrom(x, z, &n->position, mario->targetYaw)) < best) {
            best = s;
        }
    }
    return best;
}

/* (x, z) beside Mario is a place he can stand: no wall between it and Mario (his width past it too), a floor
   there from a step above Mario's feet to a drop below them, not deep water, spikes or a block a partner may not
   stand on (partyGetAppearPos's floor). Its floor's height in *y. */
static int spotFree(float x, float z, float* y) {
    MarioWork* mario = marioGetPtr();
    const Vec at = {x, mario->position.y, z};
    HitCheckQuery q;
    HitEntry* hit;
    if (wallBetweenPad(&mario->position, &at, 14.0f)) {
        return 0;
    }
    memset(&q, 0, sizeof(q));
    q.targetPos = (Vec){x, mario->position.y + 37.0f, z};
    q.targetDir = (Vec){0.0f, -1.0f, 0.0f};
    q.targetDistance = 37.0f + 100.0f;
    hit = hitCheckVecFilter(&q, NULL);
    if (hit == NULL || (hitGetAttr(hit) & (HIT_ATTR_DEEP_WATER | HIT_ATTR_SPIKES)) || partyChkNoStandMobj(hit) ||
        q.hitPos.y > mario->position.y + 14.0f) {
        return 0; /* (higher than a step: a table, a counter, a crate's top) */
    }
    *y = q.hitPos.y;
    return 1;
}

/* The way straight from (ax, az) to (bx, bz) goes through Mario (in him, or right over his feet): 1, the way round him
 * in (*rx, *rz) (beside him across the way, the side away from the camera first, on Mario's floor); 2 none round him
 * (a narrow ledge); 0 the way is clear of him. */
#define PAST_MARIO 28.0f
static int pastMario(float ax, float az, float bx, float bz, float* rx, float* rz) {
    MarioWork* mario = marioGetPtr();
    const float wx = bx - ax, wz = bz - az, len2 = wx * wx + wz * wz;
    float t, nx, nz, fx = 0.0f, fz = 0.0f, y, sgn;
    int k;
    if (len2 < 1.0f) {
        return 0;
    }
    t = ((mario->position.x - ax) * wx + (mario->position.z - az) * wz) / len2;
    if (t <= 0.0f || t >= 1.0f ||
        distABf(ax + wx * t, az + wz * t, mario->position.x, mario->position.z) >= PAST_MARIO) {
        return 0;
    }
    nx = -wz / sqrtf(len2);
    nz = wx / sqrtf(len2);
    movePos(&fx, &fz, 1.0f, mario->targetYaw);
    sgn = nx * fx + nz * fz >= 0.0f ? 1.0f : -1.0f;
    for (k = 0; k < 2; k++, sgn = -sgn) {
        const float x = mario->position.x + nx * sgn * (PAST_MARIO + 8.0f);
        const float z = mario->position.z + nz * sgn * (PAST_MARIO + 8.0f);
        if (spotFree(x, z, &y) && fabsf(y - mario->position.y) <= 14.0f) {
            *rx = x;
            *rz = z;
            return 1;
        }
    }
    return 2;
}

/* His own place beside Mario, as a scene would put a second partner: across from the partner (else the side he is
 * on already, else behind Mario's back), a little further from the camera than Mario; then further out, the other
 * side, behind. The first with a floor, no wall between it and Mario and nobody there (the partner, an NPC, Mario):
 * 2; else the first with a floor, else the partner's own appear place: 1; else Mario's: 0 (nowhere to stand). */
static int pickSpot(PartyEntry* p, Vec* out) {
    static const float kSpots[][2] = {
        /* (along the picture's width, toward his side; away from the camera) */
        {44.0f, 12.0f},  {44.0f, -8.0f},  {64.0f, 26.0f}, {-44.0f, 12.0f}, {-80.0f, 12.0f},  {30.0f, 42.0f},
        {-30.0f, 42.0f}, {90.0f, 0.0f},   {0.0f, 50.0f},  {-90.0f, 0.0f},  {64.0f, -24.0f}, {-64.0f, -24.0f},
        /* (a crowded room: further out) */
        {60.0f, 64.0f},  {-60.0f, 64.0f}, {115.0f, 16.0f}, {-115.0f, 16.0f}, {100.0f, -40.0f}, {-100.0f, -40.0f},
        {0.0f, 85.0f},   {30.0f, -50.0f}, {-30.0f, -50.0f},
    };
    MarioWork* mario = marioGetPtr();
    PartyEntry* partner = partyGetPtr(mario->currPartySlotId[0]);
    const float yaw = mario->targetYaw;
    float fx = 0.0f, fz = 0.0f, lx = 0.0f, lz = 0.0f, side = 1.0f, y = 0.0f, partnerLat = 0.0f;
    int i, firstFree = -1, lowerFree = 0;
    float bestClear = 0.0f;
    Vec firstAt = mario->position, lowerAt = mario->position;
    movePos(&fx, &fz, 1.0f, yaw);
    movePos(&lx, &lz, 1.0f, yaw + 90.0f);
    if (partner != NULL && partner != p && !(partner->flags2 & PARTY_DISP_FLAG_HIDDEN) &&
        PSVECDistance(&partner->position, &mario->position) < 150.0f) {
        partnerLat = (partner->position.x - mario->position.x) * lx + (partner->position.z - mario->position.z) * lz;
    }
    if (fabsf(partnerLat) > 6.0f) {
        side = partnerLat > 0.0f ? -1.0f : 1.0f;
    } else {
        const float mine = (p->position.x - mario->position.x) * lx + (p->position.z - mario->position.z) * lz;
        if (fabsf(mine) > 8.0f && PSVECDistance(&p->position, &mario->position) < 200.0f) {
            side = mine > 0.0f ? 1.0f : -1.0f;
        } else {
            float ax = 0.0f, az = 0.0f;
            movePos(&ax, &az, 1.0f, mario->displayDirection);
            side = ax * lx + az * lz > 0.0f ? -1.0f : 1.0f; /* (behind his back) */
        }
    }
    for (i = 0; i < (int)(sizeof(kSpots) / sizeof(kSpots[0])); i++) {
        const float x = mario->position.x + lx * kSpots[i][0] * side + fx * kSpots[i][1];
        const float z = mario->position.z + lz * kSpots[i][0] * side + fz * kSpots[i][1];
        float rx, rz;
        int through;
        if (!spotFree(x, z, &y)) {
            continue;
        }
        /* (a place he could get to only through Mario, no way round him: across a narrow ledge from him) */
        through = fabsf(p->position.y - mario->position.y) <= 14.0f &&
                  distABf(p->position.x, p->position.z, mario->position.x, mario->position.z) < 150.0f &&
                  pastMario(p->position.x, p->position.z, x, z, &rx, &rz) == 2;
        if (!through && !overlapsOthers(p, x, z, y, NULL) && !overlapsAt(x, z, &mario->position, yaw)) {
            if (y < mario->position.y - 14.0f) {
                /* (below Mario's floor, off the ledge he stands on: a place on Mario's own floor first, further
                   out) */
                if (!lowerFree) {
                    lowerFree = 1;
                    lowerAt = (Vec){x, y, z};
                }
                continue;
            }
            *out = (Vec){x, y, z};
            return 2;
        }
        {
            /* (all taken, a crowded room: the clearest of them) */
            const float c = through ? -1.0f : clearance(p, x, z, y);
            if (firstFree < 0 || c > bestClear) {
                firstFree = i;
                bestClear = c;
                firstAt = (Vec){x, y, z};
            }
        }
    }
    {
        /* (none free on Mario's floor he can get to: where he stands, when that is beside Mario on his floor, a
           ledge Mario jumped up to; not off it) */
        const float apart = distABf(p->position.x, p->position.z, mario->position.x, mario->position.z);
        if (fabsf(p->position.y - mario->position.y) <= 14.0f && apart >= 30.0f && apart <= 90.0f &&
            !overlapsOthers(p, p->position.x, p->position.z, p->position.y, NULL) &&
            !overlapsAt(p->position.x, p->position.z, &mario->position, yaw)) {
            *out = p->position;
            return 2;
        }
    }
    if (lowerFree) {
        *out = lowerAt;
        return 2;
    }
    if (firstFree >= 0) {
        *out = firstAt;
        return 1;
    }
    if (partyGetAppearPos(p, out)) {
        return 1;
    }
    *out = mario->position;
    return 0;
}

/* Mario has a floor under him (not held in the air or out of the room by a scene) */
static int marioOnFloor(void) {
    MarioWork* mario = marioGetPtr();
    return traceFloorGap(&mario->position) < 200.0f;
}

/* Beside Mario (his own place there), standing, following him. */
static void placeAtMario(PartyEntry* p) {
    MarioWork* mario = marioGetPtr();
    Vec pos;
    /* (nowhere beside Mario to stand: out of sight until there is, mustTuck) */
    sB.noPlace = !pickSpot(p, &pos);
    p->position = pos;
    p->trailPos = pos;
    portCoopFallReset(0);
    p->moveDir = p->targetDir = mario->moveDirection;
    p->displayDirection = p->displayDirectionTarget = mario->displayDirection;
    clearMotion(p);
    partyChgPose(p, "S_1");
    sB.driven = 0;
    sB.trailCount = 0;
    sB.trailHead = 0;
    sB.walled = 0;
    sB.climbTries = 0;
    sB.spot = pos;
    sB.spotAnchor = mario->position;
    sB.spotOn = 1;
    sB.spotAge = 0;
    sB.spotStuck = 0;
    sB.spotLastD = 0.0f;
}

static PartyEntry* create(void) {
    MarioWork* mario = marioGetPtr();
    PartyEntry* p = (PartyEntry*)__memAlloc(HEAP_DEFAULT, sizeof(PartyEntry));
    if (p == NULL) {
        return NULL;
    }
    memset(p, 0, sizeof(PartyEntry));
    p->paperAnimGroup = animPoseEntry(model(), 2);
    if (p->paperAnimGroup < 0) {
        __memFree(HEAP_DEFAULT, p);
        return NULL;
    }
    p->camId = marioGetCamId();
    p->reserved_10 = -1;
    p->paperPoseId = -1;
    p->flags = PARTY_FLAG_IS_ACTIVE | PARTY_FLAG_IS_GROUNDED;
    p->currentSlotId = LUIGI_SLOT;
    p->playerPtr = mario;
    p->currentMemberId = LUIGI_MEMBER;
    p->moveKind = 0;
    p->height = 30.0f; /* (Luigi is a little taller than the partners' 24) */
    p->width = 24.0f;
    p->homingDist = 50.0f;
    p->scale = (Vec){1.0f, 1.0f, 1.0f};
    p->yoshiPsndSFXId = -1;
    animPosePeraOff(p->paperAnimGroup);
    animPoseSetMaterialFlagOn(p->paperAnimGroup, animPoseGetMaterialFlag(mario->paperAnimGroupId[marioAnimeId()]));
    animPoseSetMaterialLightFlagOn(p->paperAnimGroup,
                                   animPoseGetMaterialLightFlag(mario->paperAnimGroupId[marioAnimeId()]));
    /* run mode 2 (moving: party_dp->move_func, the walkers' partyMoveWalk, then
     * partyWalkMain, which hands the frame to portBrothersWalk), set without
     * the run-mode change's partyWalkInit, which would clear the partners'
     * footmarks */
    p->runMode = 2;
    p->moveMode = 1;
    p->motionId = 0;
    partyPtrTbl[LUIGI_SLOT] = p;
    placeAtMario(p);
    printf("[brothers] Luigi joins (%s)\n", model());
    fflush(stdout);
    return p;
}

static void destroy(void) {
    PartyEntry* p = luigi();
    if (p == NULL) {
        return;
    }
    partyPtrTbl[LUIGI_SLOT] = NULL;
    if (p->paperAnimGroup >= 0) {
        animPoseRelease(p->paperAnimGroup);
    }
    __memFree(HEAP_DEFAULT, p);
    sB.driven = 0;
    printf("[brothers] Luigi leaves\n");
    fflush(stdout);
}

static int sResumeSlot = -1, sResumeHp, sResumeFp;

void portBrothersResumeStats(int slot, int hp, int fp) {
    sResumeSlot = slot;
    sResumeHp = hp;
    sResumeFp = fp;
}

void portBrothersReset(void) {
    destroy();
    sB.hp = sB.fp = -1;
    if (sResumeSlot >= 0 && gp != NULL && gp->saveSlot == sResumeSlot) {
        /* (a quick resume's Continue: Luigi as he was, no heal) */
        sB.hp = sResumeHp;
        sB.fp = sResumeFp;
        printf("[brothers] quick resume: Luigi HP %d FP %d as they were\n", sResumeHp, sResumeFp);
        fflush(stdout);
    }
    sResumeSlot = -1;
    sB.lastHp = sB.lastFp = sB.lastMaxHp = sB.lastMaxFp = -1;
    sB.map[0] = '\0';
    sB.trailCount = sB.trailHead = 0;
    sB.tucked = sB.driven = sB.swing = 0;
    sB.arrive = sB.marioHidden = sB.still = sB.walled = 0;
    sB.marioAway = sB.carried = sB.carryRest = sB.noPlace = 0;
    sB.spotOn = 0;
    sB.camX = sB.camZ = 0.0f;
    sB.luigiUnit = -1;
}

/* a scene has Mario: his keys or control off (an event, a talk) */
static int inScene(void) {
    return !(marioChkKey() && marioChkCtrl());
}

/* Mario's doing something Luigi can't follow him into (he waits out of
 * sight, and is back beside Mario after it) */
static int marioTucks(void) {
    MarioWork* mario = marioGetPtr();
    if ((mario->flags & (MARIO_FLAG_IS_8_BIT | MARIO_FLAG_BG_MODE)) || yoshiGetStatus() != 0) {
        return 1;
    }
    /* a scene hides Mario a while (not a hurt Mario's blinking), holds him where there is no floor (out of the
       room) or carries him up or down (a lift, which Luigi isn't on) */
    if (sB.marioHidden >= 20 || sB.marioAway >= 10 || sB.carried >= 6) {
        return 1;
    }
    switch (mario->currMotionId) {
        case MARIO_MOTION_SLIT:
        case MARIO_MOTION_ROLL:
        case MARIO_MOTION_PLANE:
        case MARIO_MOTION_SHIP:
        case MARIO_MOTION_YOSHI:
        case MARIO_MOTION_VIVIAN:
        case MARIO_MOTION_DOKAN:
        case MARIO_MOTION_BOTTOMLESS:
        case MARIO_MOTION_SHADOW:
            return 1;
    }
    return 0;
}

static int guestOwn(void);
static int mustTuck(void) {
    MarioWork* mario = marioGetPtr();
    /* an online guest's own Luigi (this game's, or the guest's in the host's game) roams the room by its own
       controller: only what Mario's form shuts him out of (a pipe, Yoshi, 8-bit, ...), never Mario's scenes */
    if (guestOwn() || portCoopRoams(1)) {
        return (mario->flags & (MARIO_FLAG_IS_8_BIT | MARIO_FLAG_BG_MODE)) || yoshiGetStatus() != 0 ||
               (marioTucks() && sB.marioHidden < 20 && sB.marioAway < 10 && sB.carried < 6);
    }
    /* a room's entrance under way (he comes in beside Mario once it's done), or nowhere beside Mario to stand */
    return marioTucks() || sB.arrive > 0 || sB.noPlace;
}

static int fieldControl(void) {
    /* (a co-op guest walking its own Luigi: its Mario's keys are off, the host's field is free) */
    return gp != NULL && !gp->inBattle && marioStGetSystemLevel() == 0 &&
           ((marioChkKey() && marioChkCtrl()) || portStateSyncCoopLocalSeat() == 2) && yoshiGetStatus() == 0;
}

/* this co-op guest's own Luigi, walked here: his talk, hammer and block hits are the host's game's */
static int guestOwn(void) {
    return portStateSyncCoopLocalSeat() == 2;
}

int portBrothersDrivenNow(void) {
    return sB.driven;
}

void portBrothersDrivenSet(int driven) {
    sB.driven = driven != 0;
    sB.idle = 0;
    sB.jump = sB.hammer = 0;
}

/* the host: a co-op guest's own Luigi hit this block from below in its game: worked here */
static void work(MapObjectEntry* m);
void portBrothersHitByName(const char* name) {
    MapObjectEntry* m = name != NULL && name[0] != '\0' ? mobjNameToPtrNoAssert(name) : NULL;
    if (m == NULL) {
        printf("[brothers] a co-op guest's Luigi hit '%s': no such block here\n", name != NULL ? name : "");
    } else if (!((m->flags & 1) && m->event != 0)) {
        printf("[brothers] a co-op guest's Luigi hit '%s': found here, nothing a hit runs\n", m->name);
    }
    fflush(stdout);
    work(m); /* (as his own hit: "Luigi hits ...") */
}

int portBrothersBusy(void) {
    /* (a guest's own swing, from its game's action, isn't: the host's game swings him where the guest's game has
       him, and the guest's walks him on after it) */
    return (sB.swing > 0 && !sG.guestSwing) || sB.tucked;
}

/* Mario's steps, for Luigi to follow: one each few units he moves. 1 when
 * Mario was put somewhere else at once (a scene, a room's entrance): the
 * steps start again there, and Luigi is put beside him. */
static int trailFrame(void) {
    MarioWork* mario = marioGetPtr();
    const int last = (sB.trailHead + TRAIL - 1) % TRAIL;
    float moved;
    /* Mario's jumps, each its own (a jump from the moment he takes off to the frame he stands again; a jump
       right after a landing a new one), and his falls */
    if (mario->flags & (MARIO_FLAG_IS_JUMPING | MARIO_FLAG_IS_FALLING)) {
        sB.marioGround = 0;
        if (sB.marioJump <= 0 && (mario->flags & MARIO_FLAG_IS_JUMPING)) {
            sB.marioJump = ++sB.marioJumps;
        } else if (sB.marioJump == 0) {
            sB.marioJump = -1;
        }
    } else if (++sB.marioGround >= 2) {
        sB.marioJump = 0; /* (a frame between his rise and his fall is still the jump) */
    }
    {
        /* Mario standing still: not a step across the ground or up or down one (a jump in place is standing: its
           rise and fall no steps, so Luigi keeps his place beside him through it, not walking to Mario's own) */
        if (mario->flags & (MARIO_FLAG_IS_JUMPING | MARIO_FLAG_IS_FALLING)) {
            sB.stillAt.y = mario->position.y;
        }
        if (distABf(mario->position.x, mario->position.z, sB.stillAt.x, sB.stillAt.z) >= 4.0f ||
            fabsf(mario->position.y - sB.stillAt.y) >= 4.0f) {
            sB.stillAt = mario->position;
            sB.still = 0;
        } else {
            sB.still++;
        }
    }
    if (sB.trailCount > 0) {
        moved = PSVECDistance(&mario->position, &sB.trail[last].pos);
        /* (a point at each takeoff and landing, however near the last: one jump's points never run on into the
           next's) */
        if (moved < 4.0f && sB.trail[last].jumped == sB.marioJump) {
            return 0;
        }
        if (moved > 120.0f) {
            sB.trailCount = 0;
            sB.trailHead = 0;
            sB.still = 0;
            return 1;
        }
    }
    sB.trail[sB.trailHead].pos = mario->position;
    sB.trail[sB.trailHead].jumped = sB.marioJump;
    sB.trail[sB.trailHead].serial = ++sB.trailSerial;
    sB.trailHead = (sB.trailHead + 1) % TRAIL;
    if (sB.trailCount < TRAIL) {
        sB.trailCount++;
    }
    return 0;
}

/* --- talking --- */

/* The NPC Luigi would talk to with A now: as the game picks Mario's (a
 * friendly NPC with a talk event, within reach, in front of him, no wall
 * between), while Mario could talk too and is near enough that the scene
 * that follows (which is Mario's: the NPC turns to him, the camera is his)
 * still reads. NULL none. */
#define TALK_MARIO_NEAR 150.0f

static NpcEntry* talkTarget(PartyEntry* p) {
    MarioWork* mario = marioGetPtr();
    NpcWork* nw = npcGetWorkPtr();
    NpcEntry* best = NULL;
    float bestD = 1000.0f;
    int i;
    /* (an online co-op guest's own Luigi: its Mario's keys are always off; whether Mario can talk now is the host's
       game's, which answers its talk) */
    const int guest = guestOwn();
    if (nw == NULL || (p->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING)) ||
        (!guest && (gp->fbatData == NULL || gp->fbatData->mode != 1 || gp->mapEventId || !marioChkTalkable() ||
                    mario->keyOffCount))) {
        return NULL;
    }
    for (i = 0; i < nw->total; i++) {
        NpcEntry* npc = &nw->entries[i];
        float d, a, s, c, hx, hy, hz, nx, ny, nz, len;
        if (!(npc->flags & NPC_FLAG_ACTIVE) || (npc->flags & NPC_FLAG_ENEMY) || npc->talkEvt == NULL) {
            continue;
        }
        if (npc->position.y > p->position.y + p->height || p->position.y > npc->position.y + npc->height) {
            continue;
        }
        d = distABf(p->position.x, p->position.z, npc->position.x, npc->position.z);
        if (d > 0.5f * (p->width + npc->width) + 10.0f || d >= bestD ||
            distABf(mario->position.x, mario->position.z, npc->position.x, npc->position.z) > TALK_MARIO_NEAR) {
            continue;
        }
        a = reviseAngle(angleABf(p->position.x, p->position.z, npc->position.x, npc->position.z) - p->moveDir);
        if (a > 90.0f && a < 270.0f) {
            continue; /* behind him */
        }
        sincosf(&s, &c, angleABf(0.0f, 0.0f, npc->position.x - p->position.x, npc->position.z - p->position.z));
        len = d;
        if (hitCheckFilter(NULL, &hx, &hy, &hz, &len, &nx, &ny, &nz, p->position.x, p->position.y + 0.75f * p->height,
                           p->position.z, s, 0.0f, c) != NULL) {
            continue; /* a wall between */
        }
        best = npc;
        bestD = d;
    }
    return best;
}

/* a talk Luigi started runs (frames since, 0 none): its text and choices are the game's, read on port 0 (msgdrv's
 * keyGetButtonTrg(0)), so the second player's A, B and directions go there too (luigiTalkFrame), as co-op's
 * talkFrame does for a partner's talk. Without it only player 1 could read on what player 2 started (online:
 * the host read the guest's talk for them). */
static int sLuigiTalk;

/* the game's own talk start (npcdrv.c's A on Mario's talk target) */
static void startTalk(NpcEntry* npc) {
    FieldBattleData* data = gp->fbatData;
    printf("[brothers] Luigi talks to '%s'\n", npc->description);
    fflush(stdout);
    sLuigiTalk = 1;
    data->mode = 2;
    data->subState = 0;
    data->npc = npc;
    npc->fbatHitKind = 0;
}

/* --- an online co-op guest's own Luigi's actions (statesync_coop_pc.inc "the guest's field actions") --- */

/* the guest's game: A in front of someone: Luigi turns to them in his talk pose at once, and the talk goes to the
   host's game, whose talk follows (its scene on every screen) */
static void guestTalk(PartyEntry* p, NpcEntry* npc) {
    const float dir = angleABf(p->position.x, p->position.z, npc->position.x, npc->position.z);
    if (!portStateSyncCoopAct(PORT_COOP_ACT_TALK, npc->description, 0, 0, p->position.x, p->position.y, p->position.z,
                              dir)) {
        return;
    }
    p->targetDir = p->moveDir = dir;
    if (hasAnim(p, "T_1")) {
        partyChgPose(p, "T_1");
    }
    sG.act = 40;
}

/* the host's game: a guest's Luigi swung where its game had him (1: swinging here; what it hits is this game's,
   told back as it lands) */
int portBrothersGuestSwing(float x, float y, float z, float dir) {
    PartyEntry* p = luigi();
    if (p == NULL || !portBrothersOn() || sB.tucked || sB.swing != 0 || pouchGetPtr()->hammerLevel <= 0 ||
        !fieldControl() || (p->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING))) {
        return 0;
    }
    p->position.x = x;
    p->position.y = y;
    p->position.z = z;
    p->targetDir = p->moveDir = dir;
    p->moveSpeed = 0.0f;
    partySetTargetSpd(p, 0.0f);
    sB.driven = 1;
    sB.swing = 1; /* (his walk swings it from the next frame: hammerFrame) */
    sB.swingHit = 0;
    sG.guestSwing = 1;
    sG.told = 0;
    printf("[brothers] a co-op guest's Luigi swings his hammer at (%.0f, %.0f, %.0f), facing %.0f\n", x, y, z, dir);
    fflush(stdout);
    return 1;
}

/* the host's game: a guest's Luigi's talk with this NPC (his place there): as his own A in front of them */
int portBrothersGuestTalk(const char* name, float x, float y, float z) {
    PartyEntry* p = luigi();
    MarioWork* mario = marioGetPtr();
    NpcEntry* npc = name != NULL && name[0] != '\0' ? npcNameToPtr_NoAssert(name) : NULL;
    (void)y;
    if (p == NULL || npc == NULL || mario == NULL || !portBrothersOn() || sB.tucked || sB.swing != 0 || !fieldControl() ||
        gp->fbatData == NULL || gp->fbatData->mode != 1 || gp->mapEventId || !marioChkTalkable() || mario->keyOffCount) {
        return 0;
    }
    if (!(npc->flags & NPC_FLAG_ACTIVE) || (npc->flags & NPC_FLAG_ENEMY) || npc->talkEvt == NULL ||
        distABf(x, z, npc->position.x, npc->position.z) > 0.5f * (p->width + npc->width) + 30.0f ||
        distABf(mario->position.x, mario->position.z, npc->position.x, npc->position.z) > TALK_MARIO_NEAR + 30.0f) {
        return 0;
    }
    p->targetDir = p->moveDir = angleABf(p->position.x, p->position.z, npc->position.x, npc->position.z);
    startTalk(npc);
    return 1;
}

/* the guest's game: Luigi's action the host refused, taken back */
void portBrothersGuestUndo(int kind) {
    PartyEntry* p = luigi();
    if (kind == PORT_COOP_ACT_HAMMER && sB.swing > 0 && sG.swingLocal) {
        sB.swing = 0;
        sG.swingLocal = 0;
        if (p != NULL) {
            partyChgPose(p, "S_1");
        }
    } else if (kind == PORT_COOP_ACT_SPIN && sB.spin > 0 && sG.spinLocal) {
        sB.spin = 0; /* (the host's Luigi couldn't spin: his own stops here too) */
        sG.spinLocal = 0;
    } else if (kind == PORT_COOP_ACT_TALK && sG.act > 0) {
        sG.act = 0;
        if (p != NULL) {
            partyChgPose(p, "S_1");
        }
    }
}

/* the host's game: a guest's Luigi spun where its game had him (1: his spin counts here, for what he touches; its
   game walks him meanwhile) */
int portBrothersGuestSpin(float x, float y, float z, float dir) {
    PartyEntry* p = luigi();
    PortSpinShape shape;
    (void)dir;
    if (p == NULL || !portBrothersOn() || sB.tucked || !fieldControl() || !portSpinDashOn() ||
        hypotf(p->position.x - x, p->position.z - z) > 80.0f || fabsf(p->position.y - y) > 80.0f) {
        return 0;
    }
    portSpinDashShape(&shape);
    sG.guestSpin = shape.frames + 24; /* (its turn to its end, and its positions come a little late) */
    printf("[brothers] a co-op guest's Luigi spins at (%.0f, %.0f, %.0f)\n", x, y, z);
    fflush(stdout);
    return 1;
}

const PartyEntry* portBrothersSpinning(void) {
    PartyEntry* p = luigi();
    if (p == NULL || !portBrothersOn() || sB.tucked) {
        return NULL;
    }
    /* (a co-op guest's own spin touches nothing in its game: the host's game decides) */
    return (sB.spin > 0 && !sG.spinLocal) || sG.guestSpin > 0 ? p : NULL;
}

/* --- the hammer's first strike --- */

#define HAMMER_LIVE_FROM (HAMMER_HIT - 3)
#define HAMMER_LIVE_TO (HAMMER_HIT + 4)
#define HAMMER_HEAD_RADIUS 12.0f

/* fbatHitCheck (npcdrv.c), after the game's own checks found nothing: Luigi's
 * hammer coming down on an NPC counts as Mario's (an enemy: a first strike
 * and the battle; a friendly NPC that reacts to the hammer: its reaction),
 * with the game's own filters. */
void portBrothersFbatHitPost(NpcEntry** r, u32 flags, void* result) {
    PartyEntry* p = luigi();
    NpcWork* nw;
    MarioWork* mario;
    NpcEntry* hit = NULL;
    float x, y, z, best = 1000.0f;
    int i;
    /* (a co-op guest's own swing hits nothing in its game: the host's game's swing hits) */
    if (*r != NULL || p == NULL || sB.tucked || sG.swingLocal || sB.swing < HAMMER_LIVE_FROM || sB.swing > HAMMER_LIVE_TO ||
        !(flags & FBAT_CHECK_HAMMER) || !(flags & FBAT_CHECK_LIVE_ATTACK) || !portBrothersOn() || !fieldControl() ||
        gp->mapEventId || (nw = npcGetWorkPtr()) == NULL) {
        return;
    }
    mario = marioGetPtr();
    if (((flags & FBAT_CHECK_CONTACT) && mario->mutekiTimer > 0) || (mario->flags & MARIO_FLAG_KEY_OFF) ||
        mario->currMotionId == MARIO_MOTION_DAMAGE || mario->currMotionId == MARIO_MOTION_DAMAGE_TOGE) {
        return;
    }
    /* the hammer's head, in front of him where it comes down */
    x = p->position.x;
    z = p->position.z;
    movePos(&x, &z, 30.0f, p->moveDir);
    y = p->position.y + 8.0f;
    for (i = 0; i < nw->total; i++) {
        NpcEntry* npc = &nw->entries[i];
        float dx, dy, dz, d;
        if (!(npc->flags & NPC_FLAG_ACTIVE) || ((flags & FBAT_CHECK_ENEMIES) != 0) != ((npc->flags & NPC_FLAG_ENEMY) != 0) ||
            ((flags & FBAT_CHECK_QUERY) && !(npc->reactionFlags & 2)) ||
            (npc->flags & (NPC_FLAG_NO_FIELD_ATTACK | NPC_FLAG_INTANGIBLE | NPC_FLAG_KOOPA_MODE)) ||
            (npc->jumpFlags & NPC_STATUS_DEAD) || npc->fbatHitCooldown) {
            continue;
        }
        dx = x - npc->position.x;
        dz = z - npc->position.z;
        dy = y < npc->position.y ? y - npc->position.y
             : y > npc->position.y + npc->height ? y - (npc->position.y + npc->height)
                                                 : 0.0f;
        d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < 0.5f * npc->width + HAMMER_HEAD_RADIUS && d < best) {
            best = d;
            hit = npc;
        }
    }
    if (hit == NULL) {
        return;
    }
    if (result != NULL) {
        *(u32*)result = FBAT_HIT_HAMMER; /* (FbatHitResult's kind; the rest as the game's checks left it) */
    }
    if (!(flags & FBAT_CHECK_QUERY)) {
        fbatSetAttackAnnounce(FBAT_HIT_HAMMER);
        printf("[brothers] Luigi's hammer hits '%s'\n", hit->description);
        fflush(stdout);
        if (sG.guestSwing && !sG.told) {
            sG.told = 1;
            portStateSyncCoopActResult(2, hit->description); /* (a guest's swing: the enemy it hit, told back) */
        }
    }
    *r = hit;
}

/* --- where he is, traced (TTYD_BROS_TRACE=1: the test pass's brothers_place_* scenarios) ---
 * Each arrival in a room (once Mario's keys are back, or 90 frames in), each scene's end (Mario's keys or control
 * off a while: an event, a talk) and every 120 frames of a scene: Luigi's place against Mario, the others in the
 * party and the NPCs, the floor under him and the wall between him and Mario, with a verdict: OK, or BAD: and the
 * classes (far: left behind; wall: a wall between him and Mario; float: standing over no floor; nofloor; overlap:
 * in or right behind the partner or an NPC; jumps: hopping over and over in a scene; hops: hopping over and over
 * in place while Mario stands). */
static struct {
    int on;
    int frame;
    int scene, sceneJumps, sceneOverlap, sceneMid;
    int arrive; /* frames since a new room (0: none pending) */
    char map[16];
    int hopFrames, hops; /* the field: his own hops while Mario stands */
    Vec marioAt;
    /* the field, since Mario last stood a while: Mario's jumps, his copies of them, his hops up to a floor */
    int marioUp, marioJumps, luigiJumps, luigiClimbs, calm;
    float closest; /* since Mario's first jump of them: the nearest Luigi came to Mario by his own moves (across the
                      ground) */
} sT = {-1, .closest = 1e9f};

static void traceReport(PartyEntry* p, const char* kind) {
    MarioWork* mario = marioGetPtr();
    const char* who = "";
    const float dM = PSVECDistance(&p->position, &mario->position);
    const float gap = traceFloorGap(&p->position);
    const int grounded = !(p->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING));
    const int wall = wallBetween(&mario->position, &p->position);
    /* (in a scene's middle only how long he has stood in someone counts: a camera turning past puts anyone right
       behind someone for a moment) */
    const int in = overlapsOthers(p, p->position.x, p->position.z, p->position.y, &who) ||
                   (overlapsAt(p->position.x, p->position.z, &mario->position, mario->targetYaw) && (who = "Mario"));
    const int ovl = in && p->moveSpeed < 0.3f && strcmp(kind, "scene-mid") != 0;
    /* lit as Mario is: a house walked into darkens the town around it (the map's blend), all but what it exempts
       (Mario, the partners, its own NPCs: their no-blend flags); without them he goes dark with the town */
    const int marioPose = mario->paperAnimGroupId[marioAnimeId()];
    const u32 noBlend = ANIM_MATERIAL_FLAG_NO_BLEND | ANIM_MATERIAL_FLAG_NO_BLEND2;
    const u32 litM = marioPose >= 0 ? animPoseGetMaterialFlag(marioPose) & noBlend : 0;
    const u32 litL = p->paperAnimGroup >= 0 ? animPoseGetMaterialFlag(p->paperAnimGroup) & noBlend : 0;
    char bad[96] = "";
    if (dM > 200.0f) {
        strcat(bad, ",far");
    }
    if (wall) {
        strcat(bad, ",wall");
    }
    if (litM & ~litL) {
        strcat(bad, ",dark");
    }
    if (gap > 9000.0f) {
        strcat(bad, ",nofloor");
    } else if (grounded && gap > 10.0f) {
        strcat(bad, ",float");
    }
    if (ovl || (sT.scene >= 60 && sT.sceneOverlap * 3 > sT.scene)) {
        strcat(bad, ",overlap");
    }
    if (sT.sceneJumps >= 3 && sT.sceneJumps * 300 > sT.scene) { /* (more than a hop each 5 seconds) */
        strcat(bad, ",jumps");
    }
    printf("[bros-trace] %s f=%d map=%s L=(%.0f,%.0f,%.0f) M=(%.0f,%.0f,%.0f) dM=%.0f floor=%.0f wall=%d lit=%x/%x "
           "with=%s scene=%d jumps=%d overlapped=%d verdict=%s%s\n",
           kind, sT.frame, gp->currentMapName, p->position.x, p->position.y, p->position.z, mario->position.x,
           mario->position.y, mario->position.z, dM, gap > 9000.0f ? -1.0f : gap, wall, (unsigned)litL,
           (unsigned)litM, who[0] ? who : "-", sT.scene,
           sT.sceneJumps, sT.sceneOverlap, bad[0] ? "BAD:" : "OK", bad[0] ? bad + 1 : "");
    fflush(stdout);
}

static void traceFrame(PartyEntry* p) {
    MarioWork* mario = marioGetPtr();
    int scene;
    if (sT.on < 0) {
        const char* e = getenv("TTYD_BROS_TRACE");
        sT.on = e != NULL && e[0] == '1';
    }
    if (!sT.on || p == NULL) {
        return;
    }
    sT.frame++;
    if (strncmp(sT.map, gp->currentMapName, sizeof(sT.map)) != 0) {
        snprintf(sT.map, sizeof(sT.map), "%s", gp->currentMapName);
        sT.arrive = 1;
        sT.scene = sT.sceneJumps = sT.sceneOverlap = 0;
    }
    scene = !(marioChkKey() && marioChkCtrl());
    if (sB.tucked) {
        /* out of sight: right while Mario is (a pipe, a paper form, a scene hiding him); not once Mario's own
           again in a room (hidden: the old "not in the building" class) */
        if (scene) {
            sT.scene++;
        } else if (sT.scene >= 20 || sT.arrive > 0) {
            if (!marioTucks() && (sB.noPlace || sB.arrive >= 3)) { /* (not the frame before he comes in) */
                printf("[bros-trace] %s f=%d map=%s out of sight (arriving %d, nowhere to stand %d) "
                       "verdict=BAD:hidden\n",
                       sT.arrive > 0 ? "arrive" : "scene-end", sT.frame, gp->currentMapName, sB.arrive, sB.noPlace);
                fflush(stdout);
            }
            sT.arrive = 0;
            sT.scene = sT.sceneJumps = sT.sceneOverlap = 0;
        }
        return;
    }
    if (sT.arrive > 0 && ((!scene && sT.arrive > 20) || ++sT.arrive > 90)) {
        sT.arrive = 0;
        traceReport(p, "arrive");
    } else if (sT.arrive > 0 && !scene) {
        sT.arrive++;
    }
    if (scene) {
        sT.scene++;
        if (p->moveSpeed < 0.3f && overlapsOthers(p, p->position.x, p->position.z, p->position.y, NULL)) {
            sT.sceneOverlap++; /* (standing in someone: walking past them is no overlap) */
        }
        if (sT.scene % 120 == 0) {
            traceReport(p, "scene-mid");
        }
    } else if (sT.scene > 0) {
        if (sT.scene >= 20) {
            traceReport(p, "scene-end");
        }
        sT.scene = sT.sceneJumps = sT.sceneOverlap = 0;
    }
    /* the field: his hops for Mario's jumps (one each: chain, more), told once Mario has stood a while after them */
    {
        const int up = (mario->flags & MARIO_FLAG_IS_JUMPING) != 0;
        const int bothDown = !(mario->flags & (MARIO_FLAG_IS_JUMPING | MARIO_FLAG_IS_FALLING)) &&
                             !(p->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING));
        if (up && !sT.marioUp && !scene && !sB.driven) {
            sT.marioJumps++;
        }
        sT.marioUp = up;
        /* (his own moves: in the air, or walking toward Mario; Mario walking past him where he stands, or on
           past him as he walks on, is Mario's) */
        if (sT.marioJumps > 0 && !scene && !sB.driven) {
            float sx = 0.0f, sz = 0.0f;
            movePos(&sx, &sz, 1.0f, p->targetDir);
            if ((p->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING)) ||
                (p->moveSpeed > 0.3f &&
                 (mario->position.x - p->position.x) * sx + (mario->position.z - p->position.z) * sz > 0.0f)) {
                const float apart = distABf(p->position.x, p->position.z, mario->position.x, mario->position.z);
                if (apart < sT.closest) {
                    sT.closest = apart;
                }
            }
        }
        sT.calm = bothDown && !scene && PSVECDistance(&mario->position, &sT.marioAt) < 20.0f ? sT.calm + 1 : 0;
        if (sT.calm >= 90 && (sT.marioJumps > 0 || sT.luigiJumps > 0 || sT.luigiClimbs > 0)) {
            /* (chain: more copies than Mario's jumps, or more hops up a step than one for each) */
            const int chain =
                sT.luigiJumps > sT.marioJumps || sT.luigiClimbs > (sT.marioJumps > 0 ? sT.marioJumps : 1);
            /* (close: nearer Mario than beside him, in him or on top of him; lower: on another floor than Mario's
               once both stand, left below the ledge Mario jumped up to) */
            const int tooNear = sT.marioJumps > 0 && sT.closest < 24.0f;
            const int lower = fabsf(p->position.y - mario->position.y) > 10.0f;
            char bad[32] = "";
            if (chain) {
                strcat(bad, ",chain");
            }
            if (tooNear) {
                strcat(bad, ",close");
            }
            if (lower) {
                strcat(bad, ",lower");
            }
            printf("[bros-trace] jumps-copied f=%d map=%s mario=%d luigi=%d climbs=%d closest=%.0f floors=%.0f/%.0f "
                   "verdict=%s%s\n",
                   sT.frame, gp->currentMapName, sT.marioJumps, sT.luigiJumps, sT.luigiClimbs,
                   sT.marioJumps > 0 ? sT.closest : -1.0f, p->position.y, mario->position.y, bad[0] ? "BAD:" : "OK",
                   bad[0] ? bad + 1 : "");
            fflush(stdout);
            sT.marioJumps = sT.luigiJumps = sT.luigiClimbs = 0;
            sT.closest = 1e9f;
        }
    }
    /* the field: hopping in place while Mario stands */
    if (!scene && PSVECDistance(&mario->position, &sT.marioAt) < 20.0f) {
        if (++sT.hopFrames > 180) {
            if (sT.hops >= 4) {
                printf("[bros-trace] hops f=%d map=%s %d hops in 180 frames, Mario standing verdict=BAD:hops\n",
                       sT.frame, gp->currentMapName, sT.hops);
                fflush(stdout);
            }
            sT.hopFrames = sT.hops = 0;
        }
    } else {
        sT.marioAt = mario->position;
        sT.hopFrames = sT.hops = 0;
    }
}

/* a hop of his own (traced; sTraceCopy: a copy of a jump of Mario's, no hop in place) */
static int sTraceCopy;
static void traceJump(void) {
    if (sT.on > 0) {
        if (!(marioChkKey() && marioChkCtrl())) {
            sT.sceneJumps++;
        } else if (!sB.driven) {
            sT.hops += !sTraceCopy;
            if (sTraceCopy) {
                sT.luigiJumps++;
            } else {
                sT.luigiClimbs++;
            }
        }
    }
    sTraceCopy = 0;
}

void portBrothersFieldFrame(void) {
    PartyEntry* p = luigi();
    MarioWork* mario;
    NpcEntry* npc;
    if (gp == NULL) {
        return;
    }
    if (!portBrothersOn() || gp->inBattle || seqGetSeq() != SEQ_GAME) {
        if (p != NULL && (!portBrothersOn() || seqGetSeq() != SEQ_GAME)) {
            destroy();
        }
        if (!portBrothersOn()) {
            /* (what Mario gains meanwhile, a Peach interlude, an inn, a level-up, isn't Luigi's: the mode
               coming back takes Mario's numbers then as where it starts from) */
            sB.lastHp = sB.lastFp = sB.lastMaxHp = sB.lastMaxFp = -1;
        }
        return;
    }
    mario = marioGetPtr();
    healFrame();
    if (p == NULL && (p = create()) == NULL) {
        return;
    }
    if (!(p->flags2 & PARTY_DISP_FLAG_HIDDEN) && !sB.tucked && p->animName != NULL) {
        portLookCheck(30, "luigi", p->position.x, p->position.y, p->position.z, p->displayDirection, p->animName);
    }
    /* a new room (or the same one entered again): beside Mario */
    if (strncmp(sB.map, gp->currentMapName, sizeof(sB.map)) != 0) {
        snprintf(sB.map, sizeof(sB.map), "%s", gp->currentMapName);
        p->camId = marioGetCamId();
        p->playerPtr = mario;
        p->flags &= ~(PARTY_FLAG_CTRL_OFF | PARTY_FLAG_BG_MODE);
        p->flags2 = 0;
        placeAtMario(p);
        sB.camX = sB.camZ = 0.0f;
        sB.tucked = 0;
        sB.arrive = 1; /* (out of sight until Mario's entrance is done: not left in the doorway, the pipe, the
                          edge he walked in from) */
    }
    if (sB.arrive > 0) {
        /* Mario's own again (or standing a while in the scene the room starts with, or a long time in): his */
        if ((sB.arrive >= 2 && marioChkKey() && marioChkCtrl()) || (sB.arrive >= 90 && sB.still >= 8) ||
            sB.arrive >= 240) {
            sB.arrive = 0;
        } else {
            sB.arrive++;
        }
    }
    sB.marioHidden = (mario->dispFlags & MARIO_DISP_FLAG_HIDDEN) ? sB.marioHidden + 1 : 0;
    sB.marioAway =
        !(mario->flags & (MARIO_FLAG_IS_JUMPING | MARIO_FLAG_IS_FALLING)) && !marioOnFloor() ? sB.marioAway + 1 : 0;
    {
        /* carried: a scene moves Mario up or down, standing (not a jump or a fall), on the spot */
        const float dy = mario->position.y - sB.marioLastY;
        const float dxz = distABf(mario->position.x, mario->position.z, sB.marioLastX, sB.marioLastZ);
        const int carry = inScene() && fabsf(dy) > 0.8f && fabsf(dy) < 60.0f && dxz < 1.0f &&
                          !(mario->flags & (MARIO_FLAG_IS_JUMPING | MARIO_FLAG_IS_FALLING));
        if (carry) {
            sB.carried++;
            sB.carryRest = 0;
        } else if (sB.carried > 0 && ++sB.carryRest > 15) {
            sB.carried = 0; /* (the lift stopped a while: beside Mario again) */
        }
        sB.marioLastX = mario->position.x;
        sB.marioLastY = mario->position.y;
        sB.marioLastZ = mario->position.z;
    }
    if (sB.noPlace && ++sB.noPlaceAge % 15 == 0) {
        Vec at;
        sB.noPlace = !pickSpot(p, &at); /* (somewhere to stand now: placed there as he comes back) */
    }
    if (mustTuck()) {
        if (!sB.tucked) {
            sB.tucked = 1;
            p->flags2 |= PARTY_DISP_FLAG_HIDDEN | PARTY_DISP_FLAG_NO_SHADOW;
        }
        p->position = mario->position;
    } else if (sB.tucked) {
        sB.tucked = 0;
        p->flags2 &= ~(PARTY_DISP_FLAG_HIDDEN | PARTY_DISP_FLAG_NO_SHADOW);
        placeAtMario(p);
    }
    if (trailFrame() && !sB.tucked) {
        placeAtMario(p);
        trailFrame();
    }
    /* the second player (online: the guest) */
    sB.stickX = gp->stickX[1];
    sB.stickY = gp->stickY[1];
    {
        /* (read every frame: a binding held through a spin is no press after it) */
        const int spin = portSpinDashPressed(1, (gp->button[1] & PAD_TRIGGER_L) != 0) && portSpinDashOn();
        sB.spinReq = spin && fieldControl() && !sB.tucked;
    }
    if (sG.guestSpin > 0) {
        sG.guestSpin--;
    }
    if (fieldControl() && !sB.tucked) {
        const u32 pressed = gp->buttonNew[1];
        if (hypotf(sB.stickX, sB.stickY) > DEAD_ZONE || (pressed & (PAD_BUTTON_A | PAD_BUTTON_B)) || sB.spinReq) {
            if (!sB.driven) {
                printf("[brothers] the second player takes Luigi\n");
                fflush(stdout);
            }
            sB.driven = 1;
            sB.idle = 0;
        } else if (sB.driven && !portCoopRoams(1) && ++sB.idle >= IDLE_FRAMES) {
            sB.driven = 0; /* hands off a while: he follows Mario again (an online guest's stays put) */
        }
        if ((pressed & PAD_BUTTON_A) && sB.swing == 0 && sG.act == 0 && (npc = talkTarget(p)) != NULL) {
            if (guestOwn()) {
                guestTalk(p, npc); /* (a co-op guest's own Luigi: his talk's start here, the talk the host's) */
            } else {
                startTalk(npc); /* A in front of someone: a talk, as Mario's A */
            }
        } else {
            sB.jump |= (pressed & PAD_BUTTON_A) != 0;
        }
        sB.hammer |= (pressed & PAD_BUTTON_B) != 0; /* (a co-op guest's: swung here, its hits the host's game's) */
    } else if (sB.driven && !sB.tucked && portCoopRoams(1)) {
        /* an online guest's Luigi, away in the room: a talk or a scene of Mario's leaves him where he
           is (no script ever moves him) */
        sB.stickX = sB.stickY = 0.0f;
        sB.jump = sB.hammer = 0;
    } else {
        sB.driven = 0;
        sB.jump = sB.hammer = 0;
    }
    traceFrame(p);
}

/* --- his walk --- */

static void startJump(PartyEntry* p, float speed) {
    p->flags &= ~(PARTY_FLAG_IS_GROUNDED | PARTY_FLAG_IS_FALLING);
    p->flags |= PARTY_FLAG_IS_JUMPING;
    p->velY = speed * p->playerPtr->playerGravity;
    p->accelY = GRAVITY * p->playerPtr->playerGravity;
    sB.headHit = 0;
    traceJump();
    psndSFXOn_3D("SFX_FLD_NPC_LUIGI_JUMP1", &p->position);
}

/* a map object (a block, a switch) worked as Mario's hammer or a hit from
 * below works it */
static void work(MapObjectEntry* m) {
    if (m != NULL && (m->flags & 1) && m->event != 0) {
        printf("[brothers] Luigi hits '%s'\n", m->name);
        fflush(stdout);
        mobjRunEvent(m, (EvtWord*)m->event);
    }
}

static void hammerFrame(PartyEntry* p) {
    static const char* const kSwing[] = {"M_H_1", "M_H_4", "M_H_7"};
    const int lv = pouchGetPtr()->hammerLevel;
    const char* pose = kSwing[lv >= 3 ? 2 : lv == 2 ? 1 : 0];
    if (sB.swing == 1) {
        if (hasAnim(p, pose)) {
            partyChgPose(p, pose);
        }
        psndSFXOn_3D(SFX_NAME_WORD(lv >= 3 ? 0x15D : lv == 2 ? 0x15C : 0x15B), &p->position);
    }
    if (sB.swing == HAMMER_HIT && !sB.swingHit && sG.swingLocal) {
        sB.swingHit = 1; /* (a co-op guest's own swing: what it hits is the host's game's, told back) */
    }
    if (sB.swing == HAMMER_HIT && !sB.swingHit) {
        MapObjectWork* mw = portMobjGetWork();
        MapObjectEntry* best = NULL;
        float bestD = HAMMER_REACH, x = p->position.x, z = p->position.z;
        int i;
        /* where the hammer comes down: in front of him, the way he faces */
        movePos(&x, &z, 26.0f, p->moveDir);
        for (i = 0; mw != NULL && i < mw->count; i++) {
            MapObjectEntry* m = &mw->entries[i];
            const float d = hypotf(m->position.x - x, m->position.z - z);
            const float dy = m->position.y - p->position.y;
            if ((m->flags & 1) && m->event != 0 && dy > -12.0f && dy < 50.0f && d < bestD) {
                best = m;
                bestD = d;
            }
        }
        if (best != NULL) {
            sB.swingHit = 1;
            psndSFXOn_3D(SFX_NAME_WORD(lv >= 3 ? 0x160 : lv == 2 ? 0x15F : 0x15E), &best->position);
            if (sG.guestSwing && !sG.told) {
                sG.told = 1;
                portStateSyncCoopActResult(2, best->name); /* (a guest's swing: what it hit, told back) */
            }
            work(best);
        }
    }
    if (++sB.swing > HAMMER_FRAMES) {
        sB.swing = 0;
        partyChgPose(p, "S_1");
        if (sG.guestSwing && !sG.told) {
            portStateSyncCoopActResult(2, ""); /* (a guest's swing: nothing hit) */
        }
        sG.guestSwing = sG.told = sG.swingLocal = 0;
    }
}

/* The point of Mario's steps FOLLOW_DIST behind him (along them), and whether he was in the air there (*air: a
 * point of a jump, no floor); for a jump's point, the jump's (*serial: the step he took off from, one each jump;
 * *from: where he took off, on its floor; *top: the highest he got; *land: where he came down, on its floor, or his
 * last point in the air so far). 0 when he hasn't walked that far. */
static int followPoint(Vec* out, int* air, int* serial, Vec* from, float* top, Vec* land) {
    MarioWork* mario = marioGetPtr();
    Vec prev = mario->position;
    float along = 0.0f;
    int i;
    for (i = 1; i <= sB.trailCount; i++) {
        const int k = (sB.trailHead - i + TRAIL * 2) % TRAIL;
        if (sB.trail[k].serial < sB.trailFrom) {
            return 0; /* (he is past these: Mario hasn't walked that far on from where Luigi joined him) */
        }
        /* (across the ground: a jump's rise and fall are no way along, else a jump in place put the point on Mario's
           own spot and he walked into Mario) */
        along += distABf(prev.x, prev.z, sB.trail[k].pos.x, sB.trail[k].pos.z);
        prev = sB.trail[k].pos;
        if (along >= FOLLOW_DIST) {
            const int id = sB.trail[k].jumped;
            int j, n;
            *out = sB.trail[k].pos;
            *air = id != 0;
            *serial = id > 0 ? id : 0;
            *from = *land = sB.trail[k].pos;
            *top = sB.trail[k].pos.y;
            if (id <= 0) {
                return 1;
            }
            /* back to where he took off (the step before the jump's first point, its floor) */
            for (j = i + 1; j <= sB.trailCount; j++) {
                n = (sB.trailHead - j + TRAIL * 2) % TRAIL;
                *from = sB.trail[n].pos;
                if (sB.trail[n].jumped != id) {
                    break;
                }
                if (sB.trail[n].pos.y > *top) {
                    *top = sB.trail[n].pos.y;
                }
            }
            /* on to where he came down */
            for (j = i - 1; j >= 1; j--) {
                n = (sB.trailHead - j + TRAIL * 2) % TRAIL;
                *land = sB.trail[n].pos;
                if (sB.trail[n].jumped != id) {
                    break;
                }
                if (sB.trail[n].pos.y > *top) {
                    *top = sB.trail[n].pos.y;
                }
            }
            return 1;
        }
    }
    return 0;
}

/* Where he comes down from his copy of a jump of Mario's that went up or down to another floor (Mario took off at
 * from, came down at land): on that floor, clear of Mario (where Mario came down when Mario has gone on from it,
 * else beside it across the jump's way, on his own side of it first, or short of it), no wall between it and where
 * Mario came down, and not over Mario on the way from where he is (at). 0: none. */
static int landAim(const Vec* at, const Vec* from, const Vec* land, Vec* out) {
    static const float kAims[][2] = {
        /* (across the jump's way, along it) */
        {0.0f, 0.0f},   {36.0f, 0.0f},  {-36.0f, 0.0f}, {36.0f, -20.0f}, {-36.0f, -20.0f}, {0.0f, -36.0f},
        {36.0f, 20.0f}, {-36.0f, 20.0f}, {56.0f, 0.0f}, {-56.0f, 0.0f},  {0.0f, 36.0f},
    };
    MarioWork* mario = marioGetPtr();
    float ax = land->x - from->x, az = land->z - from->z;
    const float len = hypotf(ax, az);
    float mine, rx, rz;
    int i;
    if (len < 1.0f) {
        ax = 0.0f;
        az = 1.0f;
    } else {
        ax /= len;
        az /= len;
    }
    /* (his side of the jump's way: across it toward him) */
    mine = (at->x - land->x) * -az + (at->z - land->z) * ax < 0.0f ? -1.0f : 1.0f;
    for (i = 0; i < (int)(sizeof(kAims) / sizeof(kAims[0])); i++) {
        const float x = land->x - az * kAims[i][0] * mine + ax * kAims[i][1];
        const float z = land->z + ax * kAims[i][0] * mine + az * kAims[i][1];
        const Vec there = {x, land->y, z};
        HitCheckQuery q;
        HitEntry* hit;
        if (distABf(x, z, mario->position.x, mario->position.z) < 34.0f || wallBetweenPad(land, &there, 10.0f) ||
            pastMario(at->x, at->z, x, z, &rx, &rz) != 0) {
            continue;
        }
        memset(&q, 0, sizeof(q));
        q.targetPos = (Vec){x, land->y + 30.0f, z};
        q.targetDir = (Vec){0.0f, -1.0f, 0.0f};
        q.targetDistance = 30.0f + 40.0f;
        hit = hitCheckVecFilter(&q, NULL);
        if (hit == NULL || (hitGetAttr(hit) & (HIT_ATTR_DEEP_WATER | HIT_ATTR_SPIKES)) || partyChkNoStandMobj(hit) ||
            fabsf(q.hitPos.y - land->y) > 8.0f) {
            continue; /* (off that floor: the ledge's edge, a drop) */
        }
        *out = (Vec){x, q.hitPos.y, z};
        return 1;
    }
    return 0;
}

/* his copy of a jump of Mario's in the air: on to his place beside Mario (Mario in the air, or come down on the floor
   he took off from), or down at his own place on the floor Mario came down on */
static void aimFrame(PartyEntry* p, float* dir, float* spd) {
    MarioWork* mario = marioGetPtr();
    static float marioWasY;
    const int marioFalls = mario->position.y < marioWasY - 0.1f ||
                           !(mario->flags & (MARIO_FLAG_IS_JUMPING | MARIO_FLAG_IS_FALLING));
    Vec goal;
    float d;
    marioWasY = mario->position.y;
    if (sB.aimOn == 1 && marioFalls) {
        /* Mario coming down (or down) over another floor than the one he took off from (a ledge he jumped up to,
           one he jumped down from): his own place on it, beside where Mario comes down (aimed while both are still
           in the air: Luigi's jump is the higher, he comes down after Mario) */
        const float gap = traceFloorGap(&mario->position);
        const Vec under = {mario->position.x, mario->position.y - gap, mario->position.z};
        if (gap < 9000.0f && fabsf(under.y - sB.aimFrom.y) >= 6.0f &&
            landAim(&p->position, &sB.aimFrom, &under, &sB.aim)) {
            sB.aimOn = 2;
        }
    }
    if (sB.aimOn == 2) {
        goal = sB.aim;
    } else {
        goal = (Vec){mario->position.x + sB.aimOff.x, mario->position.y, mario->position.z + sB.aimOff.z};
    }
    d = distABf(p->position.x, p->position.z, goal.x, goal.z);
    if (d > 2.0f) {
        *dir = angleABf(p->position.x, p->position.z, goal.x, goal.z);
        *spd = d > 5.0f ? 5.0f : d;
    } else {
        *spd = 0.0f;
    }
}

/* Mario stands (this long) or a scene has him (his keys or control off: an event, a talk): Luigi goes to his own
 * place beside him and stands there, as a scene places a partner, instead of on Mario's steps (which end in the
 * partner's place, or under a step he would hop at over and over). */
#define STILL_FRAMES 24
#define SCENE_STILL_FRAMES 6

static void spotWalk(PartyEntry* p, int scene, float* dir, float* spd) {
    MarioWork* mario = marioGetPtr();
    float d;
    sB.spotAge++;
    /* a new place when Mario moved on from where it was picked, or (each half second) someone stands in it */
    if (!sB.spotOn || distABf(mario->position.x, mario->position.z, sB.spotAnchor.x, sB.spotAnchor.z) > 30.0f ||
        fabsf(mario->position.y - sB.spotAnchor.y) > 30.0f ||
        (sB.spotAge % 30 == 0 && overlapsOthers(p, sB.spot.x, sB.spot.z, sB.spot.y, NULL))) {
        Vec at;
        if (pickSpot(p, &at)) {
            sB.spot = at;
        } else if (!sB.spotOn) {
            sB.spot = p->position; /* (nowhere better: where he is) */
        }
        sB.spotAnchor = mario->position;
        sB.spotOn = 1;
        sB.spotAge = 0;
        sB.spotStuck = 0;
        sB.spotLastD = 1e9f;
    }
    d = distABf(p->position.x, p->position.z, sB.spot.x, sB.spot.z);
    /* not getting there (a wall, a step up he can't take: no hopping at it), or far (the scene cut away): there */
    if (d > 4.0f && d >= sB.spotLastD - 0.3f) {
        sB.spotStuck++;
    } else {
        sB.spotStuck = 0;
    }
    sB.spotLastD = d;
    if (d > 240.0f || sB.spotStuck > (scene ? 30 : 60) || fabsf(p->position.y - sB.spot.y) > 60.0f) {
        const Vec at = sB.spot;
        p->position = at;
        p->trailPos = at;
        clearMotion(p);
        sB.spotStuck = 0;
        sB.spotLastD = 0.0f;
        d = 0.0f;
    }
    if (d > 4.0f) {
        const float top = scene ? 1.8f : 2.4f; /* (a scene's walk is calm) */
        float rx, rz;
        *dir = angleABf(p->position.x, p->position.z, sB.spot.x, sB.spot.z);
        if (pastMario(p->position.x, p->position.z, sB.spot.x, sB.spot.z, &rx, &rz) == 1) {
            *dir = angleABf(p->position.x, p->position.z, rx, rz); /* (round Mario, not through him) */
        }
        *spd = d > 60.0f ? top : (d - 2.0f) * 0.1f + 0.4f;
        if (*spd > top) {
            *spd = top;
        }
    } else {
        /* there: he turns to Mario (as the partner does beside him) */
        if (distABf(p->position.x, p->position.z, mario->position.x, mario->position.z) > 8.0f) {
            *dir = angleABf(p->position.x, p->position.z, mario->position.x, mario->position.z);
        }
        *spd = 0.0f;
    }
}

/* his own pose names (c_luigi's) and footsteps (his NPC's), not a
 * follower's: stand, walk, run, jump up, come down */
/* the badges' ribbons round his spin fade out */
static void spinRibbonEnd(void) {
    portSpinDashRibbonEnd(sB.spinRibbon, sB.spinRibbonWork);
    sB.spinRibbon = sB.spinRibbonWork = NULL;
}

static void walkPose(PartyEntry* p, int grounded) {
    if (!grounded) {
        partyChgPose(p, p->velY > 0.0f ? "J_1A" : "J_1B");
        p->footstepTimer = 0;
    } else if (p->moveSpeed > 0.3f) {
        const int run = p->moveSpeed >= 1.65f;
        partyChgPose(p, run ? "R_1" : "W_1");
        if (++p->footstepTimer >= (run ? 10 : 20)) {
            p->footstepTimer = 0;
            sB.step ^= 1;
            psndSFXOn_3D(sB.step ? "SFX_FLD_NPC_LUIGI_MOVE1L" : "SFX_FLD_NPC_LUIGI_MOVE1R", &p->position);
        }
    } else {
        partyChgPose(p, "S_1");
        p->footstepTimer = 0;
    }
}

int portBrothersWalk(PartyEntry* p) {
    MarioWork* mario;
    float dir, spd = 0.0f, dist, edgeWas;
    Vec was;
    int roams, tug, fell;
    int grounded;
    if (p == NULL || p != luigi()) {
        return 0;
    }
    mario = marioGetPtr();
    p->playerPtr = mario;
    if (sB.spin == 0 && sB.spinRibbon != NULL) {
        spinRibbonEnd(); /* (his spin ended last frame, however it ended) */
    }
    {
        static int was; /* (his spin's end, however it ended, for the log: as Mario's) */
        if (sB.spin == 0 && sB.spinLocked) {
            /* his paper his own turn's again, from facing left or right, the nearer (as Mario's spin ends) */
            const float d = revise360(p->displayDirection);
            sB.spinLocked = 0;
            p->flags2 &= ~PARTY_DISP_FLAG_DIR_LOCK;
            p->displayDirection = p->displayDirectionTarget = d > 90.0f && d <= 270.0f ? 180.0f : 0.0f;
        }
        if (was > 0 && sB.spin == 0) {
            printf("[brothers] Luigi's spin ends after %d frames at (%.0f, %.0f, %.0f) in %s\n", was, p->position.x,
                   p->position.y, p->position.z, gp->currentMapName);
            fflush(stdout);
        }
        was = sB.spin;
    }
    if (sB.tucked) {
        p->position = mario->position;
        p->moveSpeed = 0.0f;
        partySetTargetSpd(p, 0.0f);
        sB.spin = 0;
        return 1;
    }
    if (sG.act > 0) {
        /* a co-op guest's own Luigi's talk: he stands turned to the NPC in his talk pose (the host's talk follows) */
        sG.act = guestOwn() ? sG.act - 1 : 0;
        if (guestOwn()) {
            p->moveSpeed = 0.0f;
            partySetTargetSpd(p, 0.0f);
            sB.jump = sB.hammer = 0;
            return 1;
        }
    }
    if (!(sB.hammer && pouchGetPtr()->hammerLevel > 0) && sB.swing == 0 && portStateSyncCoopPlace(2, p)) {
        /* the host: an online guest's own game walks Luigi; he is where that game has him (his hammer, a
           talk: still from the guest's controller, here) */
        sB.driven = 1;
        sB.stuck = 0;
        sB.jump = 0;
        return 1;
    }
    grounded = !(p->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING));
    dir = p->targetDir;
    dist = PSVECDistance(&p->position, &mario->position);
    roams = portCoopRoams(1); /* an online guest's Luigi: the whole room */
    /* fallen, in deep water, stuck or left far behind: back at Mario's side (walked by his player, he
       is held at the picture's edge instead: coop_camera_pc.c; an online guest's has the room) */
    fell = portCoopFallen(0, p);
    if (fell || (!roams && p->position.y < mario->position.y - 200.0f) ||
        (!roams && !sB.driven && dist > LEASH * 2.0f) || sB.stuck > 120) {
        if (fell && roams) {
            printf("[brothers] Luigi fell at (%.0f, %.0f, %.0f) in %s: put back beside Mario\n", p->position.x,
                   p->position.y, p->position.z, gp->currentMapName);
            fflush(stdout);
        }
        placeAtMario(p);
        return 1;
    }
    /* walked by his player but left outside the picture (Mario walked on): he follows Mario back */
    edgeWas = sB.driven && !roams ? portCoopEdgeExcess(p->position.x, p->position.y, p->position.z) : 0.0f;
    tug = sB.driven && !roams && edgeWas > 12.0f;
    if (sB.spin > 0 && (sB.swing > 0 || !sB.driven || tug)) {
        sB.spin = 0; /* (his player let him go, or the picture's edge pulls him: no spin then) */
        sG.spinLocal = 0;
    }
    if (sB.swing > 0) {
        hammerFrame(p);
    } else if (sB.driven && !tug) {
        const float mag = hypotf(sB.stickX, sB.stickY);
        if (mag > DEAD_ZONE) {
            /* as marioGetStick turns Mario's stick: the camera's yaw added (an online guest's: its own camera's) */
            dir = revise360(angleABf(0.0f, 0.0f, sB.stickX, -sB.stickY) + portCoopStickYaw(1, mario->targetYaw));
            /* (as player 2's partner: full speed at the stick's full tilt, a short stick's learned; the walk
               boost by player 2's own control) */
            spd = portCoopStickTilt(1, mag) * (mario->baseDashSpeed > 0.0f ? mario->baseDashSpeed : 2.25f) *
                  portWalkBoostFor(1);
        }
        if (sB.spinCool > 0) {
            sB.spinCool--;
        }
        /* Paper Mario 64's Spin Dash: from the ground, his stick's way or his own (pm64_spin.h) */
        if (sB.spinReq && grounded && sB.spin == 0 && sB.spinCool == 0 && !sB.jump && !sB.hammer &&
            portSpinDashOn() &&
            (!guestOwn() || portStateSyncCoopAct(PORT_COOP_ACT_SPIN, NULL, 0, 0, p->position.x, p->position.y,
                                                 p->position.z, mag > DEAD_ZONE ? dir : p->moveDir))) {
            /* (a co-op guest's own Luigi: spun here at once, the spin sent; what he touches the host's game's) */
            sG.spinLocal = guestOwn();
            portSpinDashShape(&sB.spinShape);
            /* (as Mario's, spin.c: the turn's way by the way he was going, then the stick's way, fixed; no
               stick, where he stands) */
            sB.spinSign = portSpinDashTurnSign(p->moveDir, mario->targetYaw);
            sB.spinRun = mag > DEAD_ZONE
                             ? (mario->baseDashSpeed > 0.0f ? mario->baseDashSpeed : 2.25f) * portWalkBoostFor(1)
                             : 0.0f;
            sB.spinDir = mag > DEAD_ZONE ? dir : p->moveDir;
            sB.spin = 1;
            sB.spinHeld = 0;
            sB.spinEnding = 0;
            portSpinDashStartFx(p->position.x, p->position.y, p->position.z);
            sB.spinRibbon = portSpinDashRibbonStart(NULL, sB.spinSign);
            sB.spinRibbonWork = portSpinDashRibbonWork(sB.spinRibbon);
            printf("[brothers] Luigi spins at (%.0f, %.0f, %.0f) toward %.0f: %d frames%s%s\n", p->position.x,
                   p->position.y, p->position.z, sB.spinDir, sB.spinShape.frames, sB.spinRun > 0.0f ? "" : ", in place",
                   sG.spinLocal ? " (sent)" : "");
            fflush(stdout);
        }
        sB.spinHeld = sB.spin > 0 && sB.blocked ? sB.spinHeld + 1 : 0;
        if (sB.spinHeld >= 20) {
            sB.spinEnding = 1; /* (a wall: it turns to its end now, as Mario's) */
        }
        if (sB.spin > 0 && (sB.jump || sB.hammer || !grounded || sB.spin > sB.spinShape.frames + 40)) {
            sB.spin = 0; /* (a jump, the hammer or a fall: a run's speed at most after it) */
            sB.spinCool = 12;
            sG.spinLocal = 0;
        }
        if (sB.spin > 0) {
            dir = sB.spinDir; /* (fixed, as Mario's) */
            spd = portSpinDashSpeed(&sB.spinShape, sB.spin - 1, sB.spinRun);
            if (sB.spin % 4 == 0) {
                portSpinDashDust(p->position.x, p->position.y, p->position.z, -dir);
            }
            sB.spin++;
        }
        if (sB.jump && grounded) {
            startJump(p, JUMP_SPEED);
            grounded = 0;
        }
        if (sB.hammer && grounded && pouchGetPtr()->hammerLevel > 0 &&
            (!guestOwn() || portStateSyncCoopAct(PORT_COOP_ACT_HAMMER, NULL, pouchGetPtr()->hammerLevel, 0,
                                                 p->position.x, p->position.y, p->position.z, dir))) {
            /* (a co-op guest's own Luigi: swung here at once, the swing sent; its hits the host's game's) */
            sG.swingLocal = guestOwn();
            sB.swing = 1;
            sB.swingHit = 0;
            spd = 0.0f;
            hammerFrame(p);
        }
        sB.stuck = 0;
    } else {
        Vec target;
        const int scene = inScene();
        int air = 0, serial = 0, have;
        float top = 0.0f;
        Vec from = {0.0f, 0.0f, 0.0f}, land = {0.0f, 0.0f, 0.0f};
        if (grounded && sB.aimOn) {
            if (sB.aimOn == 2) {
                sB.trailFrom = sB.trailSerial; /* (down beside Mario on his floor: on from here) */
            }
            sB.aimOn = 0; /* (down: his copy of Mario's jump is over) */
        }
        /* a wall between him and Mario a while (left in a doorway, the far side of a door or a wall, the room's
           edge he came in from): beside Mario */
        sB.walled = wallBetween(&mario->position, &p->position) ? sB.walled + 1 : 0;
        if (sB.walled > 45 && grounded) {
            placeAtMario(p);
            return 1;
        }
        if (!tug && sB.still >= (scene ? SCENE_STILL_FRAMES : STILL_FRAMES)) {
            /* Mario stands, or a scene holds him: his own place beside Mario */
            if (grounded) {
                spotWalk(p, scene, &dir, &spd);
                /* his place is up on the floor Mario stands on (Mario jumped up to it): a hop up there, aimed, once
                   (then the walk there, or put there when he can't get up) */
                if (!scene && sB.spot.y > p->position.y + 14.0f &&
                    distABf(p->position.x, p->position.z, sB.spot.x, sB.spot.z) < 90.0f) {
                    if (fabsf(sB.spot.y - sB.climbY) > 10.0f) {
                        sB.climbY = sB.spot.y;
                        sB.climbTries = 0;
                    }
                    if (sB.climbTries < 1) {
                        const float rise = sB.spot.y - p->position.y + 16.0f;
                        const float v = sqrtf(2.0f * -GRAVITY * rise);
                        sB.climbTries++;
                        if (sT.on > 0) {
                            printf("[bros-trace] hop f=%d climb at (%.0f,%.0f,%.0f) to his place (%.0f,%.0f,%.0f)\n",
                                   sT.frame, p->position.x, p->position.y, p->position.z, sB.spot.x, sB.spot.y,
                                   sB.spot.z);
                            fflush(stdout);
                        }
                        sB.aimOn = 2;
                        sB.aim = sB.spot;
                        startJump(p, v > 11.0f ? 11.0f : v);
                        grounded = 0;
                    }
                }
            }
            sB.stuck = 0;
            have = 0;
        } else {
            sB.spotOn = 0;
            have = followPoint(&target, &air, &serial, &from, &top, &land);
        }
        if (!have && tug) {
            target = mario->position; /* (Mario hasn't walked far: straight to him) */
            have = 1;
        }
        /* Mario jumps with him beside him (standing, or on his heels): he jumps with Mario, at once, as high as
           Mario's own, keeping his place beside Mario in the air (aimFrame: on to his own place on the floor Mario
           comes down on, a ledge Mario jumped up to); one behind on Mario's steps copies it where Mario took off,
           below */
        if (grounded && !scene && sB.marioJump > sB.hopSerial && sB.marioJump > 0 &&
            distABf(p->position.x, p->position.z, mario->position.x, mario->position.z) < 70.0f) {
            sB.hopSerial = sB.marioJump;
            if (sT.on > 0) {
                printf("[bros-trace] hop f=%d jump with Mario at (%.0f,%.0f,%.0f) step %d\n", sT.frame, p->position.x,
                       p->position.y, p->position.z, sB.marioJump);
                fflush(stdout);
            }
            sTraceCopy = 1;
            sB.aimOn = 1;
            sB.aimFrom = mario->position;
            sB.aimOff = (Vec){p->position.x - mario->position.x, 0.0f, p->position.z - mario->position.z};
            startJump(p, JUMP_SPEED);
            grounded = 0;
        }
        if (have) {
            const float d = distABf(p->position.x, p->position.z, target.x, target.z);
            /* a step up to a floor (not a point of a jump of Mario's in the air: that is no floor, and a hop at
               each of them was a hop after hop) */
            const int climb =
                !air && (target.y > p->position.y + 14.0f || (sB.blocked && target.y > p->position.y + 4.0f));
            const int hop = serial > sB.hopSerial && !scene; /* (a jump of Mario's not copied yet, nor a later one) */
            if (d > 6.0f) {
                dir = angleABf(p->position.x, p->position.z, target.x, target.z);
                spd = d > 120.0f ? 3.5f : (d - 4.0f) * 0.14f;
                if (spd > (scene ? 2.0f : 2.6f)) {
                    spd = scene ? 2.0f : 2.6f;
                }
                {
                    /* (Mario turned back on his steps, toward him: he waits there, not walking on into Mario) */
                    float sx = 0.0f, sz = 0.0f;
                    movePos(&sx, &sz, 1.0f, dir);
                    if (distABf(p->position.x, p->position.z, mario->position.x, mario->position.z) < 34.0f &&
                        (mario->position.x - p->position.x) * sx + (mario->position.z - p->position.z) * sz > 0.0f) {
                        spd = 0.0f;
                    }
                }
            }
            if (climb && fabsf(target.y - sB.climbY) > 10.0f) {
                sB.climbY = target.y; /* (a new floor to get up to) */
                sB.climbTries = 0;
            }
            /* Mario's jump copied once, as a partner following: where he took off, as high as he went (up to where
               he came down); or a hop up to a floor he walked up to, once (then he walks on, or is put beside Mario
               when he can't get there) */
            if (grounded && ((hop && d < 60.0f) || (climb && d < 40.0f && sB.climbTries < 1))) {
                float rise = hop ? (top > land.y ? top : land.y) - p->position.y + (top > land.y ? 4.0f : 16.0f)
                                 : target.y - p->position.y + 16.0f;
                float v = sqrtf(2.0f * -GRAVITY * (rise > 20.0f ? rise : 20.0f));
                if (hop) {
                    sB.hopSerial = serial;
                    if (land.y > from.y + 14.0f) {
                        sB.climbY = land.y; /* (the floor Mario jumped up to: this hop is his try at it) */
                        sB.climbTries = 1;
                    }
                    /* (Mario came down on another floor: down on it too, at his own place there) */
                    sB.aimOn = sB.marioJump != serial && fabsf(land.y - from.y) >= 6.0f &&
                                       landAim(&p->position, &from, &land, &sB.aim)
                                   ? 2
                                   : 0;
                } else {
                    sB.climbTries++;
                }
                if (sT.on > 0) {
                    printf("[bros-trace] hop f=%d %s at (%.0f,%.0f,%.0f) to %.0f (from %.0f, top %.0f, down %.0f) "
                           "step %d\n",
                           sT.frame, hop ? "jump" : "climb", p->position.x, p->position.y, p->position.z, target.y,
                           from.y, top, land.y, serial);
                    fflush(stdout);
                }
                sTraceCopy = hop;
                startJump(p, v > 11.0f ? 11.0f : v);
                grounded = 0;
            }
            /* following without getting nearer to Mario */
            if (dist > 90.0f && dist >= sB.lastDist - 0.2f) {
                sB.stuck++;
            } else {
                sB.stuck = 0;
            }
        } else {
            sB.stuck = 0;
        }
    }
    if (!grounded && sB.aimOn && !sB.driven) {
        aimFrame(p, &dir, &spd); /* (his copy of Mario's jump, steered in the air) */
        p->moveSpeed = spd;
    }
    sB.jump = sB.hammer = 0;
    sB.lastDist = dist;
    p->targetDir = dir;
    if (grounded) {
        /* at once, as Mario starts and stops (partyMoveWalk's easing keeps it) */
        partySetTargetSpd(p, spd);
        p->moveSpeed = spd * mario->playerGravity;
    }
    was = p->position;
    partyMoveNoHosei(p); /* (driveParty cleared last frame's hit objects) */
    if (sB.driven && !tug && !roams) {
        const float now = portCoopEdgeExcess(p->position.x, p->position.y, p->position.z);
        if (now > 0.0f && now > edgeWas) {
            /* the picture's edge: held there, pushing against it */
            p->position.x = was.x;
            p->position.z = was.z;
        }
    }
    if (roams) {
        /* an online guest's: never off the map's edge (an exit's road: the host's Mario takes the exits) */
        portCoopHoldOnMap(p, was.x, was.y, was.z);
    }
    sB.blocked = p->hitobjFront != NULL;
    /* the end of the rise (or a ceiling: a block there is hit from below) */
    if ((p->flags & PARTY_FLAG_IS_JUMPING) && (p->velY <= 0.0f || p->hitobjHead != NULL)) {
        if (p->hitobjHead != NULL && !sB.headHit && (hitGetAttr(p->hitobjHead) & HIT_ATTR_MOBJ)) {
            MapObjectEntry* m = mobjHitObjPtrToPtr(p->hitobjHead);
            sB.headHit = 1;
            if (guestOwn()) {
                /* (a co-op guest's own Luigi: the block is the host's, told by name, worked there) */
                if (m != NULL) {
                    portStateSyncCoopLuigiHit(m->name);
                }
            } else {
                work(m);
            }
        }
        p->flags &= ~PARTY_FLAG_IS_JUMPING;
        p->flags |= PARTY_FLAG_IS_FALLING;
        if (p->velY > 0.0f) {
            p->velY = 0.0f;
        }
    }
    grounded = !(p->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING));
    if (grounded && sB.airborne) {
        psndSFXOn_3D("SFX_FLD_NPC_LUIGI_LANDING1", &p->position);
    }
    sB.airborne = !grounded;
    if (sB.spin > 0 && grounded && portLuigiModelReady()) {
        /* Mario's spin pose (Play as Luigi's model has every Mario animation; the disc's c_luigi its run) */
        partyChgPose(p, portSpinDashPoseName());
    } else if (sB.swing == 0) {
        walkPose(p, grounded);
    }
    if (sB.spin > 0) {
        /* his paper turning round and round, as Mario's, then on to face the way he went: there it ends. His
           paper locked to the spin's turn meanwhile, as Mario's (MARIO_DISP_FLAG_DIR_LOCK): the partner's own turn
           (partyMakeDispDir) eased it back toward left or right each frame, so it hardly turned, one way never
           reached its end, and the spin ran on to its longest, slowing to a crawl edge on */
        float facing;
        if (!(p->flags2 & PARTY_DISP_FLAG_DIR_LOCK)) {
            p->flags2 |= PARTY_DISP_FLAG_DIR_LOCK;
            sB.spinLocked = 1;
        }
        facing = p->displayDirection;
        if (portSpinDashTurn(&sB.spinShape, sB.spin - 2, sB.spinEnding, sB.spinSign, &facing)) {
            sB.spin = 0;
            sB.spinCool = 12;
            sG.spinLocal = 0;
        }
        p->displayDirection = p->displayDirectionTarget = facing;
        portSpinDashRibbonFollow(sB.spinRibbon, sB.spinRibbonWork, p->position.x, p->position.y, p->position.z);
        if (sB.spin == 0) {
            spinRibbonEnd();
        }
    }
    p->motionId = grounded && p->moveSpeed > 0.3f ? 1 : 0;
    if (grounded && p->moveSpeed == 0.0f && sB.spin == 0 && revise360(p->targetDir - p->moveDir) > 1.0f &&
        revise360(p->targetDir - p->moveDir) < 359.0f) {
        /* turned where he stands (to Mario beside him, to whom he talks): his paper turns that way too, as a
           partner's does on the move (partyMakeDispDir turns it only while he moves: he stood turned away) */
        const float diff = revise360(p->targetDir - p->camDir);
        if (diff >= 190.0f && diff <= 350.0f) {
            p->displayDirectionTarget = 0.0f;
        } else if (diff >= 10.0f && diff <= 170.0f) {
            p->displayDirectionTarget = 180.0f;
        }
    }
    p->moveDir = p->targetDir;
    return 1;
}

/* --- the camera --- */

void portBrothersCamera(float* x, float* z) {
    PartyEntry* p = luigi();
    float wantX = 0.0f, wantZ = 0.0f;
    if (p != NULL && !sB.tucked && portBrothersOn() && gp != NULL && !gp->inBattle) {
        MarioWork* mario = marioGetPtr();
        float len;
        wantX = (p->position.x - mario->position.x) * 0.5f;
        wantZ = (p->position.z - mario->position.z) * 0.5f;
        len = hypotf(wantX, wantZ);
        if (len > 120.0f) {
            wantX *= 120.0f / len;
            wantZ *= 120.0f / len;
        }
    }
    sB.camX += (wantX - sB.camX) * 0.08f;
    sB.camZ += (wantZ - sB.camZ) * 0.08f;
    *x += sB.camX;
    *z += sB.camZ;
}

/* --- battle --- */

void portBrothersSetLuigiUnit(int unitId) {
    sB.luigiUnit = unitId;
}

int portBrothersLuigiUnit(const void* unit) {
    const BattleUnit* u = (const BattleUnit*)unit;
    return u != NULL && sB.luigiUnit >= 0 && gp != NULL && gp->inBattle && _battleWorkPointer != NULL &&
           u->unitId == sB.luigiUnit && u->currentKind == UNIT_MARIO && u->alliance == 0;
}

int portBrothersLuigiActs(void) {
    BattleWork* wp = _battleWorkPointer;
    if (sB.luigiUnit < 0 || gp == NULL || !gp->inBattle || wp == NULL || wp->activeUnitId != sB.luigiUnit) {
        return 0;
    }
    /* his act inside the player phase (not the battle's end, which still
     * names him the active unit when his move won it) */
    return BattleGetSeq(wp, SEQ_PHASE) == BATTLE_PHASE_PLAYER && BattleGetSeq(wp, SEQ_MOVE) == 0x5000009 &&
           portBrothersLuigiUnit(BattleGetUnitPtr(wp, wp->activeUnitId));
}

/* Mario's own unit, while Luigi acts too (NULL: none found) */
static BattleUnit* marioUnit(BattleWork* wp) {
    int i;
    for (i = 0; wp != NULL && i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u != NULL && u->currentKind == UNIT_MARIO && u->alliance == 0 && !portBrothersLuigiUnit(u)) {
            return u;
        }
    }
    return NULL;
}

void* portBrothersHudMario(void* wp, void* mario) {
    BattleUnit* own;
    if (!portBrothersLuigiUnit(mario) || (own = marioUnit((BattleWork*)wp)) == NULL) {
        return mario;
    }
    return own;
}

void portBrothersLevelUp(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* u;
    if (!portBrothersOn() || wp == NULL || sB.luigiUnit < 0 ||
        !portBrothersLuigiUnit(u = BattleGetUnitPtr(wp, sB.luigiUnit))) {
        return;
    }
    BtlUnit_SetParamFromPouch(u); /* (the new maximums, his HP and FP Mario's: full) */
    u->currentHp = u->maxHp;
    u->currentFp = u->maxFp;
    printf("[brothers] level-up: Luigi HP %d/%d FP %d/%d\n", u->currentHp, u->maxHp, u->currentFp, u->maxFp);
    fflush(stdout);
}

/* --- duo moves' Bros. moves (port/include/port/duo.h) --- */

/* the hero other than the one the game calls Mario now: Luigi's unit, or
 * Mario's while Luigi acts (brothers_aurora.c's BattleGetMarioPtr); -1 none */
static int duoOtherHero(void) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    if (!portBrothersOn() || wp == NULL || sB.luigiUnit < 0 ||
        !portBrothersLuigiUnit(BattleGetUnitPtr(wp, sB.luigiUnit))) {
        return -1;
    }
    if (!portBrothersLuigiActs()) {
        return sB.luigiUnit;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u != NULL && u->currentKind == UNIT_MARIO && u->alliance == 0 && !portBrothersLuigiUnit(u)) {
            return u->unitId;
        }
    }
    return -1;
}

/* with two players, a half's controller: Luigi's player 2's, Mario's player
 * 1's, whichever of them picked the move (the partner's: duo's default, 2) */
static int duoControllerOf(const void* unit) {
    const BattleUnit* u = (const BattleUnit*)unit;
    if (!portBrothersOn() || u == NULL || u->currentKind != UNIT_MARIO || u->alliance != 0) {
        return -1;
    }
    return portBrothersLuigiUnit(u) ? 1 : 0;
}

void portBrothersDuoFrame(int secondPad) {
    static int sRegistered;
    const int players = !portBrothersOn() ? PORT_DUO_PLAYERS_AUTO
                        : secondPad      ? PORT_DUO_PLAYERS_TWO
                                         : PORT_DUO_PLAYERS_ONE;
    if (!sRegistered) {
        sRegistered = 1;
        portDuoSetSecondHero(duoOtherHero);
        portDuoSetControllerFor(duoControllerOf);
    }
    portDuoSetPlayers(players); /* (every frame: nothing of it to go stale over a save state) */
}

/* each frame of a talk Luigi started: the second player's A, B and directions on port 0 too (the text, the
 * choices), until Mario's controls are back */
static void luigiTalkFrame(int secondPad) {
    if (!sLuigiTalk) {
        return;
    }
    if (!portBrothersOn() || gp->inBattle ||
        (++sLuigiTalk > 10 && fieldControl() && (gp->fbatData == NULL || gp->fbatData->mode != 2))) {
        printf("[brothers] Luigi's talk ends after %d frames\n", sLuigiTalk);
        fflush(stdout);
        sLuigiTalk = 0;
        return;
    }
    if (secondPad) {
        const u32 ab = PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_UP | PAD_BUTTON_DOWN; /* (a choice's d-pad too) */
        gp->button[0] |= gp->button[1] & ab;
        gp->buttonNew[0] |= gp->buttonNew[1] & ab;
        gp->buttonRepeat[0] |= gp->buttonRepeat[1] & ab;
        gp->dirs[0] |= gp->dirs[1];
        gp->dirsNew[0] |= gp->dirsNew[1];
        gp->dirsRepeat[0] |= gp->dirsRepeat[1];
    }
}

int portBrothersRoute(int secondPad) {
    if (gp != NULL) {
        luigiTalkFrame(secondPad);
    }
    if (!portBrothersOn() || !portBrothersLuigiActs()) {
        return 0; /* (co-op's routing then: players 3 and 4's partners' turns, second_player_pc.c) */
    }
    if (portDuoActive()) {
        return 1; /* a duo move: each player's controller plays their own half (duo_pc.c) */
    }
    if (secondPad) {
        gp->button[0] = gp->button[1];
        gp->buttonNew[0] = gp->buttonNew[1];
        gp->buttonRepeat[0] = gp->buttonRepeat[1];
        gp->buttonRepeatDelay[0] = gp->buttonRepeatDelay[1];
        gp->buttonUp[0] = gp->buttonUp[1];
        gp->dirs[0] = gp->dirs[1];
        gp->dirsNew[0] = gp->dirsNew[1];
        gp->dirsRepeat[0] = gp->dirsRepeat[1];
        gp->dirsRepeatDelay[0] = gp->dirsRepeatDelay[1];
        gp->stickX[0] = gp->stickX[1];
        gp->stickY[0] = gp->stickY[1];
        gp->substickX[0] = gp->substickX[1];
        gp->substickY[0] = gp->substickY[1];
        gp->triggerLeft[0] = gp->triggerLeft[1];
        gp->triggerRight[0] = gp->triggerRight[1];
    }
    return 1;
}

void* portBrothersGuardPad(const void* unit, int secondPad, int* handled) {
    if (!portBrothersOn() || _battleWorkPointer == NULL || !portBrothersLuigiUnit(unit)) {
        *handled = 0; /* (the partner's: co-op's, player 1's unless it's player 3's or 4's) */
        return NULL;
    }
    *handled = 1;
    return secondPad ? &_battleWorkPointer->padWork[1] : NULL;
}
