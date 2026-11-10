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
        int jumped;
    } trail[TRAIL];
    int trailHead, trailCount;
    float camX, camZ; /* the camera's shift toward him */
    /* HP and FP between battles (-1: full) */
    int hp, fp;
    int lastHp, lastFp, lastMaxHp, lastMaxFp;
    int luigiUnit; /* this battle's (-1 none) */
} sB = {0, 0, 0, 0, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, "", {{{0.0f, 0.0f, 0.0f}, 0}}, 0, 0, 0.0f, 0.0f,
        -1, -1, -1, -1, -1, -1, -1};

/* an online co-op guest's Luigi's field actions (statesync_coop_pc.inc "the guest's field actions"; not in the save
   state: an online session's): the guest's game, its swing played here (its hits the host's) and its talk's pose
   (frames Luigi stands in it); the host's game, a guest's swing under way here (its result told once) */
static struct {
    int swingLocal; /* the guest's: this swing is its own, played here: what it hits is the host's game's */
    int act;        /* the guest's: frames Luigi stands in his talk's pose (the host's talk follows) */
    int guestSwing; /* the host's: this swing is a guest's, from its game's action */
    int told;       /* ... its result told */
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

void portBrothersSetStats(int hp, int fp) {
    const PouchData* pouch = pouchGetPtr();
    sB.hp = hp;
    sB.fp = fp;
    /* (what the battle left Mario with isn't healing in the field) */
    sB.lastHp = pouch->currentHP;
    sB.lastFp = pouch->currentFP;
}

/* The field's healing is the brothers' both: what Mario gets back (an item,
 * a Heart Block, the inn), Luigi gets too, and he is full when Mario is. */
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

/* Beside Mario (where a partner appears), standing, following him. */
static void placeAtMario(PartyEntry* p) {
    MarioWork* mario = marioGetPtr();
    Vec pos;
    if (!partyGetAppearPos(p, &pos)) {
        pos = mario->position;
    }
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

void portBrothersReset(void) {
    destroy();
    sB.hp = sB.fp = -1;
    sB.lastHp = sB.lastFp = sB.lastMaxHp = sB.lastMaxFp = -1;
    sB.map[0] = '\0';
    sB.trailCount = sB.trailHead = 0;
    sB.tucked = sB.driven = sB.swing = 0;
    sB.camX = sB.camZ = 0.0f;
    sB.luigiUnit = -1;
}

/* Mario's doing something Luigi can't follow him into (he waits out of
 * sight, and is back beside Mario after it) */
static int mustTuck(void) {
    MarioWork* mario = marioGetPtr();
    if ((mario->flags & (MARIO_FLAG_IS_8_BIT | MARIO_FLAG_BG_MODE)) || yoshiGetStatus() != 0) {
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
    if (sB.trailCount > 0) {
        moved = PSVECDistance(&mario->position, &sB.trail[last].pos);
        if (moved < 4.0f) {
            return 0;
        }
        if (moved > 120.0f) {
            sB.trailCount = 0;
            sB.trailHead = 0;
            return 1;
        }
    }
    sB.trail[sB.trailHead].pos = mario->position;
    sB.trail[sB.trailHead].jumped = (mario->flags & (MARIO_FLAG_IS_JUMPING | MARIO_FLAG_IS_FALLING)) != 0;
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
    } else if (kind == PORT_COOP_ACT_TALK && sG.act > 0) {
        sG.act = 0;
        if (p != NULL) {
            partyChgPose(p, "S_1");
        }
    }
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
    /* a new room (or the same one entered again): beside Mario */
    if (strncmp(sB.map, gp->currentMapName, sizeof(sB.map)) != 0) {
        snprintf(sB.map, sizeof(sB.map), "%s", gp->currentMapName);
        p->camId = marioGetCamId();
        p->playerPtr = mario;
        p->flags &= ~(PARTY_FLAG_CTRL_OFF | PARTY_FLAG_BG_MODE);
        p->flags2 = 0;
        placeAtMario(p);
        sB.camX = sB.camZ = 0.0f;
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
    if (fieldControl() && !sB.tucked) {
        const u32 pressed = gp->buttonNew[1];
        if (hypotf(sB.stickX, sB.stickY) > DEAD_ZONE || (pressed & (PAD_BUTTON_A | PAD_BUTTON_B))) {
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
}

/* --- his walk --- */

static void startJump(PartyEntry* p, float speed) {
    p->flags &= ~(PARTY_FLAG_IS_GROUNDED | PARTY_FLAG_IS_FALLING);
    p->flags |= PARTY_FLAG_IS_JUMPING;
    p->velY = speed * p->playerPtr->playerGravity;
    p->accelY = GRAVITY * p->playerPtr->playerGravity;
    sB.headHit = 0;
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

/* The point of Mario's steps FOLLOW_DIST behind him (along them), and
 * whether he was in the air there; 0 when he hasn't walked that far. */
static int followPoint(Vec* out, int* jumped) {
    MarioWork* mario = marioGetPtr();
    Vec prev = mario->position;
    float along = 0.0f;
    int i;
    for (i = 1; i <= sB.trailCount; i++) {
        const int k = (sB.trailHead - i + TRAIL * 2) % TRAIL;
        along += PSVECDistance(&prev, &sB.trail[k].pos);
        prev = sB.trail[k].pos;
        if (along >= FOLLOW_DIST) {
            /* where Mario took off: in the air here, on the ground just before */
            const int before = (k + TRAIL - 1) % TRAIL;
            *out = sB.trail[k].pos;
            *jumped = sB.trail[k].jumped && (i == sB.trailCount || !sB.trail[before].jumped);
            return 1;
        }
    }
    return 0;
}

/* his own pose names (c_luigi's) and footsteps (his NPC's), not a
 * follower's: stand, walk, run, jump up, come down */
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
    int roams, tug;
    int grounded;
    if (p == NULL || p != luigi()) {
        return 0;
    }
    mario = marioGetPtr();
    p->playerPtr = mario;
    if (sB.tucked) {
        p->position = mario->position;
        p->moveSpeed = 0.0f;
        partySetTargetSpd(p, 0.0f);
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
    if (portCoopFallen(0, p) || (!roams && p->position.y < mario->position.y - 200.0f) ||
        (!roams && !sB.driven && dist > LEASH * 2.0f) || sB.stuck > 120) {
        placeAtMario(p);
        return 1;
    }
    /* walked by his player but left outside the picture (Mario walked on): he follows Mario back */
    edgeWas = sB.driven && !roams ? portCoopEdgeExcess(p->position.x, p->position.y, p->position.z) : 0.0f;
    tug = sB.driven && !roams && edgeWas > 12.0f;
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
        int jumped = 0, have = followPoint(&target, &jumped);
        if (!have && tug) {
            target = mario->position; /* (Mario hasn't walked far: straight to him) */
            have = 1;
        }
        if (have) {
            const float d = distABf(p->position.x, p->position.z, target.x, target.z);
            if (d > 6.0f) {
                dir = angleABf(p->position.x, p->position.z, target.x, target.z);
                spd = d > 120.0f ? 3.5f : (d - 4.0f) * 0.14f;
                if (spd > 2.6f) {
                    spd = 2.6f;
                }
            }
            /* a hop where Mario jumped, or up to where he went */
            if (grounded && d < 40.0f && (jumped || target.y > p->position.y + 14.0f ||
                                         (sB.blocked && target.y > p->position.y + 4.0f))) {
                float rise = target.y - p->position.y + 16.0f;
                float v = sqrtf(2.0f * -GRAVITY * (rise > 20.0f ? rise : 20.0f));
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
    if (sB.swing == 0) {
        walkPose(p, grounded);
    }
    p->motionId = grounded && p->moveSpeed > 0.3f ? 1 : 0;
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
        const u32 ab = PAD_BUTTON_A | PAD_BUTTON_B;
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
