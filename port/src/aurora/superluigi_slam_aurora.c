/* Chapter 4's two big fights (docs/super_luigi/ch4.md, "Battles"): the
 * Hum-Along Choir in Dayzee Dell, and the Drama Slam, a three-act stage
 * battle where the audience is the score.
 *
 * The units are ours (the conventions' "copies in the mod"): Dayzees on the
 * game's c_pansy, the Choirmaster and Dazzlina on the Amazy Dayzee's
 * c_kpansy, Bruno on the Hyper Goomba's c_kuribo_h, and Petunia, an actor
 * who never fights: her turns run the play (acts, scenery, lines), and she
 * keeps the battle going between acts. Each fight replaces the field
 * encounter's enemies when its scene arms it (Btl_UnitSetup, as chapter 1's
 * statue does).
 *
 * The House: our number (100 to 250), drawn top centre and driving the
 * game's own audience (its target count follows it, so seats fill and
 * empty). Stylish +5, Appeal +15, a partner's move +3, a jump or hammer
 * without a Stylish -2, a heckler bonked +5, the saboteurs' Prop Smash -20 and
 * Lullaby to the House -10, Dazzlina's Spotlight Steal -15, an act ending with
 * its set standing +10. At 150 Dazzlina is upstaged and loses every other turn; at 0
 * the show is cancelled (the battle lost). The audience needs a Star Power to
 * exist, so the Slam lends Luigi one for the night (with no Star Power to
 * spend).
 *
 * The three painted sets, the cardboard sun, the chandelier and the eruption
 * flame are the models agent's (super-luigi-models): until they land, each
 * act's scenery is the stage's light (a look per act) and the props'
 * moments are the game's effects and sounds. */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/mods.h"
#include "battle/battle.h"
#include "battle/battle_audience.h"
#include "battle/battle_damage.h"
#include "battle/battle_camera.h"
#include "battle/battle_database_common.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_event_subset.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "driver/dispdrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "driver/windowdrv.h"
#include "evt/evt_cmd.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "manager/fontmgr.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

enum { ARM_NONE, ARM_CHOIR, ARM_SLAM };
static int sArmed;
static int sSlam;      /* the Drama Slam is on (this battle) */
static int sHouse = 100, sHouseFinal;
static int sEmptied;    /* the House reached 0: the show's cancelled at Petunia's next turn, whatever comes back after */
static int sAct, sTurn, sActStart;
static int sSetDown;   /* this act's set piece knocked over */
static int sLieLow, sLieLowUsed;
static int sStylish, sMarioHit, sPartnerHit;
static int sRapids;    /* Blooey's next move is The Rapids */
static int sStarWas, sMaxSpWas, sSpWas;

int portSlSlamBattle(void) {
    return sSlam && gp != NULL && gp->inBattle && portSuperLuigiChapter() == 4;
}
int portSlSlamHouse(void) {
    return portSlSlamBattle() ? sHouse : -1;
}
void portSlSlamLieLow(void) {
    sLieLow = 1;
    sLieLowUsed = 1;
}
int portSlSlamLieLowUsed(void) {
    return sLieLowUsed;
}

/* the House changes, with a word in the announcement window */
static void houseAdd(int n, const char* tag) {
    BattleAudience* a;
    if (!portSlSlamBattle()) {
        return;
    }
    sHouse += n;
    if (sHouse > 250) {
        sHouse = 250;
    }
    if (sHouse <= 0) {
        sHouse = 0;
        sEmptied = 1;
    }
    if (tag != NULL) {
        portSlPrompt(tag, 2200);
    }
    a = BattleAudienceBaseGetPtr();
    if (a != NULL) {
        if (n > 0) {
            BattleAudienceSoundCheer(40, 10);
        } else if (n < 0) {
            BattleAudienceSoundBooing();
        }
    }
    printf("[superluigi] House %+d -> %d\n", n, sHouse);
    fflush(stdout);
}

/* ======================================================================
 * Weapons
 * ====================================================================== */

static BattleWeapon mkWeapon(int damage, int multiple, int sleep, int sleepTime) {
    BattleWeapon w;
    memset(&w, 0, sizeof(w));
    w.baseAccuracy = 100;
    w.superguardsAllowed = 2;
    w.stylishCommandMultiplier = 1;
    w.hpDamageFn = damage > 0 ? weaponGetPowerDefault : NULL;
    w.hpDamageWork[0] = damage;
    w.targetClassFlags = TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT | TARGET_CLASS_NO_OWN_ALLIANCE |
                         TARGET_CLASS_NO_SELF | TARGET_CLASS_MAIN_PARTS |
                         (multiple ? TARGET_CLASS_MULTIPLE : TARGET_CLASS_SINGLE);
    w.targetPropertyFlags = TARGET_PROP_START_OPPOSING_SIDE;
    w.element = ELEMENT_NORMAL;
    w.acLevel = 3;
    w.special = SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE;
    w.counterResistFlags = COUNTER_RESIST_ALL;
    w.targetWeightFlags = TARGET_WEIGHT_RANDOM;
    w.sleepChance = (u8)sleep;
    w.sleepTime = (s8)sleepTime;
    w.stageEffectRates[3] = 100;
    return w;
}

USER_FUNC(sl4_rapids);
static USER_FUNC(sl4_set_setup);
static USER_FUNC(sl4_set_id);
USER_FUNC(sl4_once);
USER_FUNC(sl4_final);
void portMapLookSet(const char* name, unsigned mul, unsigned add);
static BattleWeapon wLullaby, wChoirAll, wChoirOne, wCrescendo, wChandelier, wAria, wPetal, wCharge, wProp;

/* ======================================================================
 * The units
 * ====================================================================== */

#define UW_A 0 /* each unit's own counters */
#define UW_B 1

static PoseTableEntry kDzPoses[] = {
    {1, "PAN_N_1"}, {2, "PAN_Y_1"}, {9, "PAN_Y_1"}, {5, "PAN_K_1"}, {4, "PAN_X_1"}, {3, "PAN_X_1"},
    {BTL_POSE_NORMAL, "PAN_S_1"}, {29, "PAN_Q_1"}, {30, "PAN_Q_1"}, {31, "PAN_S_1"}, {BTL_POSE_DAMAGE, "PAN_D_1"},
    {50, "PAN_A_1"}, {BTL_POSE_RUN, "PAN_R_1"}, {BTL_POSE_WALK, "PAN_W_1"}, {BTL_POSE_DANGER, "PAN_X_1"},
    {BTL_POSE_PERIL, "PAN_X_1"}, {BTL_POSE_TALK, "PAN_T_1"}, {BTL_POSE_STAY_DEFAULT, "PAN_S_1"},
};
static PoseTableEntry kGbPoses[] = {
    {1, "KUR_N_1"}, {2, "KUR_Y_1"}, {9, "KUR_Y_1"}, {5, "KUR_K_1"}, {4, "KUR_I_1"}, {3, "KUR_I_1"},
    {BTL_POSE_NORMAL, "KUR_S_1"}, {29, "KUR_Q_1"}, {30, "KUR_Q_1"}, {31, "KUR_S_1"}, {BTL_POSE_DAMAGE, "KUR_D_1"},
    {50, "KUR_A_1"}, {51, "KUR_A_2"}, {BTL_POSE_RUN, "KUR_R_1"}, {BTL_POSE_WALK, "KUR_W_1"},
    {BTL_POSE_DANGER, "KUR_I_1"}, {BTL_POSE_PERIL, "KUR_I_1"}, {BTL_POSE_TALK, "KUR_T_1"},
    {BTL_POSE_STAY_DEFAULT, "KUR_S_1"},
};

static BattleUnitDefense kDef0 = {{0, 0, 0, 0, 0}};
static BattleUnitDefenseAttr kAttr0 = {{0, 0, 0, 0, 0}};
static StatusVulnerability kRegistDz = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
static StatusVulnerability kRegistBoss = {60, 50, 60, 60, 60, 60, 60, 60, 60, 50, 30, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60};
static StatusVulnerability kRegistNone = {0};

static BattleDataEntry kNoFirst[] = {{48, btldefaultevt_Dummy}, {0, NULL}};

static BattleUnitKindPart pUnder[1], pChoir[1], pMaster[1], pBruno[1], pDazz[2], pPetunia[1];
static BattleUnitKindPart pRoad[1], pBall[1], pVolc[1], pChand[1];
static BattleUnitKind kUnder, kChoirDz, kMaster, kBruno, kDazz, kPetunia;
static BattleUnitKind kRoad, kBall, kVolc, kChand;
static PoseTableEntry kSetPoses[] = {{BTL_POSE_NORMAL, "S_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"}};
static PoseTableEntry kSunPoses[] = {{BTL_POSE_NORMAL, "S_2"}, {BTL_POSE_STAY_DEFAULT, "S_2"}};
static PoseTableEntry kTiaraPoses[] = {{BTL_POSE_NORMAL, "S_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"}};
static int sSetId = -1, sChandId = -1;

extern EvtWord sl4_set_init[], sl4_set_pose[], sl4_set_on[], sl4_set_wait[];
extern EvtWord sl4_under_init[], sl4_choir_init[], sl4_master_init[], sl4_bruno_init[], sl4_dazz_init[], sl4_pet_init[];

static void mkPart(BattleUnitKindPart* p, const char* name, const char* model, PoseTableEntry* poses, float hitY) {
    memset(p, 0, sizeof(*p));
    p->partNum = 1;
    p->partName = name;
    p->modelName = model;
    p->positionOffset.y = 0.0f;
    p->hitBaseOffset.y = hitY;
    p->hitCursorBaseOffset.y = hitY + 5.0f;
    p->shadowSize = 20;
    p->hitHeight = 30;
    p->baseAlpha = 255;
    p->defense = &kDef0;
    p->defenseAttr = &kAttr0;
    p->attributes = PART_ATTR_MAIN | PART_ATTR_GUARD_COVER;
    p->poseTable = poses;
}

static void mkKind(BattleUnitKind* k, int type, const char* name, int hp, int level, int bonus, BattleUnitKindPart* part,
                   EvtWord* init, StatusVulnerability* regist) {
    memset(k, 0, sizeof(*k));
    k->unitType = type;
    k->name = name;
    k->maxHp = (s16)hp;
    k->dangerHP = (u8)(hp / 3);
    k->perilHP = 1;
    k->level = (u8)level;
    k->bonusExp = (u8)bonus;
    k->bonusCoin = 5;
    k->bonusCoinRate = 50;
    k->baseCoin = 1;
    k->pbSoftCap = 9999;
    k->width = 26;
    k->height = 34;
    k->hitOffset[1] = 34;
    k->centerOffset.y = 17.0f;
    k->hpGaugeOffset[1] = 0;
    k->talkTogeBaseOffset.y = 34.0f;
    k->heldItemBaseOffset.y = 34.0f;
    k->burnFlameOffset.y = 17.0f;
    k->turnOrder = 10;
    k->starPointDispOffset.y = 40.0f;
    k->damageSfxName = "SFX_ENM_PANSY_DAMAGED1";
    k->fireDamageSfxName = "SFX_BTL_DAMAGE_FIRE1";
    k->iceDamageSfxName = "SFX_BTL_DAMAGE_ICE1";
    k->explosionDamageSfxName = "SFX_BTL_DAMAGE_BIRIBIRI1";
    k->statusVulnerability = regist;
    k->numParts = 1;
    k->parts = part;
    k->initEventCode = init;
    k->dataTable = kNoFirst;
}

static BattleUnitSetup kChoirSetup[] = {
    {&kMaster, 1, {0}, BATTLE_PHASE_ENEMY, {90.0f, 0.0f, 0.0f}, 0, {0}, NULL},
    {&kChoirDz, 1, {0}, BATTLE_PHASE_ENEMY, {40.0f, 0.0f, -30.0f}, 0, {0}, NULL},
    {&kChoirDz, 1, {0}, BATTLE_PHASE_ENEMY, {140.0f, 0.0f, -30.0f}, 0, {0}, NULL},
    {&kChoirDz, 1, {0}, BATTLE_PHASE_ENEMY, {45.0f, 0.0f, 35.0f}, 0, {0}, NULL},
    {&kChoirDz, 1, {0}, BATTLE_PHASE_ENEMY, {145.0f, 0.0f, 35.0f}, 0, {0}, NULL},
};
static BattleGroupSetup kChoirGroup = {5, kChoirSetup, 0, 0, 100, NULL, NULL, 0};

static BattleUnitSetup kSlamSetup[] = {
    {&kPetunia, 1, {0}, BATTLE_PHASE_ENEMY, {20.0f, 0.0f, -45.0f}, 0, {0}, NULL},
    {&kUnder, 1, {0}, BATTLE_PHASE_ENEMY, {80.0f, 0.0f, 5.0f}, 0, {0}, NULL},
    {&kUnder, 1, {0}, BATTLE_PHASE_ENEMY, {135.0f, 0.0f, 10.0f}, 0, {0}, NULL},
};
static BattleGroupSetup kSlamGroup = {3, kSlamSetup, 0, 0, 100, NULL, NULL, 0};

/* spawned mid-battle: a revived Choir Dayzee, the Act 2 pair, Dazzlina, an encore's understudy */
static BattleUnitSetup sSpawnChoir = {&kChoirDz, 1, {0}, BATTLE_PHASE_ENEMY, {40.0f, 0.0f, -30.0f}, 0, {0}, NULL};
static BattleUnitSetup sSpawnBruno = {&kBruno, 1, {0}, BATTLE_PHASE_ENEMY, {85.0f, 0.0f, 5.0f}, 0, {0}, NULL};
static BattleUnitSetup sSpawnUnder = {&kUnder, 1, {0}, BATTLE_PHASE_ENEMY, {140.0f, 0.0f, 10.0f}, 0, {0}, NULL};
/* the painted sets at the back of the stage (super-luigi-models), and the chandelier over the front */
static BattleUnitSetup sSpawnRoad = {&kRoad, 1, {0}, BATTLE_PHASE_ENEMY, {0.0f, 0.0f, -75.0f}, 0, {0}, NULL};
static BattleUnitSetup sSpawnBall = {&kBall, 1, {0}, BATTLE_PHASE_ENEMY, {0.0f, 0.0f, -75.0f}, 0, {0}, NULL};
static BattleUnitSetup sSpawnVolc = {&kVolc, 1, {0}, BATTLE_PHASE_ENEMY, {0.0f, 0.0f, -75.0f}, 0, {0}, NULL};
static BattleUnitSetup sSpawnChand = {&kChand, 1, {0}, BATTLE_PHASE_ENEMY, {-60.0f, 0.0f, -10.0f}, 0, {0}, NULL};
static BattleUnitSetup sSpawnDazz = {&kDazz, 1, {0}, BATTLE_PHASE_ENEMY, {95.0f, 0.0f, 0.0f}, 0, {0}, NULL};

/* ======================================================================
 * The units' C helpers
 * ====================================================================== */

/* units of kind alive (and, awake=1, not asleep); out: the count, and the first one's id */
static int countKind(const BattleUnitKind* k, int awake, int* first) {
    BattleWork* wp = _battleWorkPointer;
    int i, n = 0;
    if (first != NULL) {
        *first = -1;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u == NULL || u->kindParams != k || u->currentHp <= 0 || (u->attributeFlags & UNIT_ATTR_INACTIVE)) {
            continue;
        }
        if (awake && (BtlUnit_CheckStatus(u, STATUS_SLEEP) || BtlUnit_CheckStatus(u, STATUS_STOP))) {
            continue;
        }
        if (first != NULL && *first < 0) {
            *first = u->unitId;
        }
        n++;
    }
    return n;
}

/* out: the awake Choir Dayzees, and whether this unit leads the song (the first of them) */
static USER_FUNC(sl4_choir_count) {
    int first, n = countKind(&kChoirDz, 1, &first);
    int self = BattleTransID(event, BTL_UNIT_SELF);
    (void)isFirstCall;
    evtSetValue(event, event->args[0], n);
    evtSetValue(event, event->args[1], first == self);
    evtSetValue(event, event->args[2], countKind(&kChoirDz, 0, NULL));
    if (event->args[3] != 0) {
        /* out: 1 while Luigi or his friend still sleeps (the Choir hums
         * then: singing them back under each turn kept both asleep for good,
         * a fight with no hits and no end) */
        BattleUnit* m = BattleGetMarioPtr(_battleWorkPointer);
        BattleUnit* p = BattleGetPartyPtr(_battleWorkPointer);
        evtSetValue(event, event->args[3], (m != NULL && m->sleepTurns > 0) || (p != NULL && p->sleepTurns > 0));
    }
    return EVT_RETURN_DONE;
}

/* in: a weapon pointer variable to fill: the Choir's song as strong as its singers */
static USER_FUNC(sl4_choir_weapon) {
    int n = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], (EvtWord)(AddrWord)(n >= 4 ? &wChoirAll : &wChoirOne));
    return EVT_RETURN_DONE;
}

/* in: n, a tag (or 0): the House changes */
static USER_FUNC(sl4_house_add) {
    (void)isFirstCall;
    houseAdd(evtGetValue(event, event->args[0]), (const char*)evtGetValue(event, event->args[1]));
    return EVT_RETURN_DONE;
}

/* out: the House now */
static USER_FUNC(sl4_house_get) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], sHouse);
    return EVT_RETURN_DONE;
}

/* out: this act's set piece is down (1), and in: set it (args 1: -1 read only) */
static USER_FUNC(sl4_set_down) {
    int v = evtGetValue(event, event->args[1]);
    (void)isFirstCall;
    if (v >= 0) {
        sSetDown = v;
    }
    evtSetValue(event, event->args[0], sSetDown);
    return EVT_RETURN_DONE;
}

/* in: a friend: out 1 when he's the partner on stage */
static USER_FUNC(sl4_partner_is) {
    const PortSlFriend* f = portSlGetFriend(evtGetValue(event, event->args[0]));
    BattleUnit* p = BattleGetPartyPtr(_battleWorkPointer);
    (void)isFirstCall;
    evtSetValue(event, event->args[1],
                f != NULL && p != NULL && p->currentKind == BattleTransPartyIdToUnitKind(f->partyId));
    return EVT_RETURN_DONE;
}

/* out: 1 when a partner is out and still standing */
static USER_FUNC(sl4_partner_up) {
    BattleUnit* p = BattleGetPartyPtr(_battleWorkPointer);
    (void)isFirstCall;
    evtSetValue(event, event->args[0], p != NULL && p->currentHp > 0);
    return EVT_RETURN_DONE;
}

/* tests, $TTYD_SL_SLAM_DOWN: out 1 on the foes' first turn, to knock the partner down; the show is
 * cancelled on their next ($TTYD_SL_SLAM_CANCEL's check waits for it) */
static int sTestDown;
static USER_FUNC(sl4_test_down) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], getenv("TTYD_SL_SLAM_DOWN") != NULL && sTestDown == 0);
    sTestDown = 1;
    return EVT_RETURN_DONE;
}

/* the director's state: out act, turn; in (args 2) a new act (-1 keeps) */
static USER_FUNC(sl4_act) {
    int newAct = evtGetValue(event, event->args[2]);
    (void)isFirstCall;
    if (newAct >= 0) {
        sAct = newAct;
        sActStart = sTurn;
        sSetDown = 0;
        sLieLowUsed = 0;
        printf("[superluigi] Drama Slam: Act %d (turn %d, House %d)\n", sAct, sTurn, sHouse);
        fflush(stdout);
    }
    evtSetValue(event, event->args[0], sAct);
    evtSetValue(event, event->args[1], sTurn);
    return EVT_RETURN_DONE;
}

static USER_FUNC(sl4_turn_inc) {
    (void)event, (void)isFirstCall;
    sTurn += getenv("TTYD_SL_SLAM_FAST") != NULL ? 4 : 1; /* tests: an act a turn */
    sLieLow = 0;
    return EVT_RETURN_DONE;
}

/* out: the foes alive now (not Petunia): understudies, Bruno, Dazzlina */
static USER_FUNC(sl4_foes) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], countKind(&kUnder, 0, NULL) + countKind(&kBruno, 0, NULL));
    evtSetValue(event, event->args[1], countKind(&kDazz, 0, NULL));
    return EVT_RETURN_DONE;
}

/* the act's leftover saboteurs flee into the wings */
static USER_FUNC(sl4_clear_foes) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    (void)event, (void)isFirstCall;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u != NULL && (u->kindParams == &kUnder || u->kindParams == &kBruno) && u->currentHp > 0) {
            BtlUnit_Delete(u);
        }
    }
    return EVT_RETURN_DONE;
}

/* the act's look: 1 the road (warm day), 2 the ball (violet and gold), 3 the volcano (red) */
static USER_FUNC(sl4_look) {
    int act = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    switch (act) {
    case 1: portMapLookSet(NULL, 0xFFF0D8, 0x100800); break;
    case 2: portMapLookSet(NULL, 0xC8A0FF, 0x200818); break;
    case 3: portMapLookSet(NULL, 0xFF9060, 0x401000); break;
    default: portMapLookSet(NULL, 0xFFFFFF, 0x000000); break;
    }
    return EVT_RETURN_DONE;
}

/* in: a unit: out its unit work A (another unit's) */
static USER_FUNC(sl4_dancing_bruno) {
    int first;
    BattleUnit* u;
    (void)isFirstCall;
    countKind(&kBruno, 0, &first);
    u = first >= 0 ? BattleGetUnitPtr(_battleWorkPointer, first) : NULL;
    evtSetValue(event, event->args[0], u != NULL ? u->work[UW_B] : 0);
    if (u != NULL) {
        u->work[UW_B] = 0;
    }
    return EVT_RETURN_DONE;
}

static int haveModel(const char* model) {
    char path[64];
    size_t n = 0;
    snprintf(path, sizeof(path), "a/%s", model);
    return portModsGetHostFile(path, &n) != NULL;
}

/* in: an act (1-3): its set's setup, or 0 when its model isn't in the build */
static USER_FUNC(sl4_set_setup) {
    int act = evtGetValue(event, event->args[0]);
    BattleUnitSetup* s = act == 1 ? &sSpawnRoad : act == 2 ? &sSpawnBall : act == 3 ? &sSpawnVolc : &sSpawnChand;
    (void)isFirstCall;
    evtSetValue(event, event->args[1], haveModel(s->unitKindParams->parts[0].modelName) ? (EvtWord)(AddrWord)s : 0);
    return EVT_RETURN_DONE;
}

/* in: which (0 the set, 1 the chandelier), an id (-1 reads): out the id kept */
static USER_FUNC(sl4_set_id) {
    int which = evtGetValue(event, event->args[0]);
    int id = evtGetValue(event, event->args[1]);
    int* v = which ? &sChandId : &sSetId;
    (void)isFirstCall;
    if (id != -1) {
        *v = id;
    }
    evtSetValue(event, event->args[2], *v);
    return EVT_RETURN_DONE;
}

/* the show's lost: the curtain drops */
static USER_FUNC(sl4_lost) {
    (void)isFirstCall;
    /* the House emptied at any point since her last turn (a foe's hit after hers, then a cheer before
     * it came round, kept a show at 0 going for turns) */
    evtSetValue(event, event->args[0], sHouse <= 0 || sEmptied || getenv("TTYD_SL_SLAM_CANCEL") != NULL); /* tests: the show cancelled */
    return EVT_RETURN_DONE;
}

/* ======================================================================
 * The units' scripts
 * ====================================================================== */

static EVT_BEGIN(sl4_wait)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

EVT_BEGIN(sl4_set_wait)
    RETURN
EVT_END()

static EVT_BEGIN(sl4_damage)
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    RETURN
EVT_END()

/* A song (LW(9) the weapon): the stage dims, notes rise, each target sleeps or guards */
static EVT_BEGIN(sl4_song)
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(9))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(9), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_WeaponAftereffect, LW(9))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    SET_VALUE(LW(0), 255)
    LOOP(0)
        IF_INT_LESS(LW(0), 192)
            BREAK_LOOP
        END_IF
        CALL(evt_map_set_blend, 0, LW(0), LW(0), 255, 255)
        SUBTRACT(LW(0), 2)
        WAIT_FRAMES(1)
    END_LOOP
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_PANSY_ATTACK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 50)
    BEGIN_SIBLING_EVENT
        LOOP(10)
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            ADD(LW(1), 30)
            CALL(evt_eff64, STRING(""), STRING("onpu_n64"), 1, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
            WAIT_FRAMES(12)
        END_LOOP
    END_SIBLING_EVENT
    WAIT_FRAMES(100)
    SET_VALUE(LW(6), 0)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    LABEL(10)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(9), DAMAGE_FLAG_LAST_HIT, LW(5))
    SWITCH(LW(5))
        CASE_EQUAL(3)
            CALL(btlevtcmd_StartAvoid, LW(3), 38)
        CASE_EQUAL(6)
            CALL(btlevtcmd_StartAvoid, LW(3), 39)
        CASE_EQUAL(2)
            CALL(btlevtcmd_StartAvoid, LW(3), 40)
        CASE_EQUAL(1)
            IF_INT_EQUAL(LW(6), 0)
                CALL(btlevtcmd_ResultACDefence, LW(3), LW(9))
                SET_VALUE(LW(6), 1)
            END_IF
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(9), DAMAGE_FLAG_LAST_HIT, LW(5))
    END_SWITCH
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    IF_INT_NOT_EQUAL(LW(3), -1)
        GOTO(10)
    END_IF
    SET_VALUE(LW(0), 192)
    LOOP(0)
        IF_INT_GREATER_OR_EQUAL(LW(0), 256)
            BREAK_LOOP
        END_IF
        CALL(evt_map_set_blend, 0, LW(0), LW(0), 255, 255)
        ADD(LW(0), 2)
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_map_blend_off, 0)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    WAIT_MS(300)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* A hit (LW(9) the weapon, LW(8) how many times): a lunge at one target, guardable */
static EVT_BEGIN(sl4_hit)
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(9))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(9), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_WeaponAftereffect, LW(9))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 35)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    LABEL(5)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 50)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_KURI_ATTACK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    ADD(LW(1), 20)
    CALL(evt_eff, 0, STRING("stardust"), 6, LW(0), LW(1), LW(2), 20, 20, 8, 30, 0, 0, 0, 0)
    WAIT_FRAMES(10)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(9), DAMAGE_FLAG_LAST_HIT, LW(5))
    IF_INT_EQUAL(LW(5), 1)
        CALL(btlevtcmd_ResultACDefence, LW(3), LW(9))
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(9), DAMAGE_FLAG_LAST_HIT, LW(5))
    ELSE
        CALL(btlevtcmd_StartAvoid, LW(3), 38)
    END_IF
    WAIT_FRAMES(20)
    SUBTRACT(LW(8), 1)
    IF_INT_GREATER(LW(8), 0)
        CALL(btlevtcmd_CheckDamageCode, LW(3), 0, LW(0))
        GOTO(5)
    END_IF
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- the Understudy: Prop Smash, Lullaby to the House, its lullaby --- */
static EVT_BEGIN(sl4_under_attack)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_A, LW(0))
    CALL(sl4_set_down, LW(1), -1)
    CALL(evt_sub_random, 99, LW(2))
    IF_INT_EQUAL(LW(0), 0)
        IF_INT_EQUAL(LW(1), 0)
            IF_INT_LESS(LW(2), 40)
                /* Prop Smash: the act's set piece comes down */
                CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, 1)
                CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
                CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(3), LW(4), LW(5))
                CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
                CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(3), LW(4), -80, 0, -1, 0)
                CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 50)
                CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_SYS_BREAK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
                CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(3), 30, -90, FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
                CALL(evt_btl_camera_shake_h, 0, 4, 0, 20, 13)
                CALL(sl4_set_down, LW(1), 1)
                CALL(sl4_act, LW(6), LW(7), -1)
                IF_INT_EQUAL(LW(6), 1)
                    SET_VALUE(LW(0), PTR("K_1"))
                    SET_VALUE(LW(1), 0)
                ELSE
                    SET_VALUE(LW(0), PTR("F_1"))
                    SET_VALUE(LW(1), 1)
                END_IF
                CALL_CHILD(sl4_set_pose)
                IF_INT_EQUAL(LW(6), 3)
                    CALL(sl4_house_add, -20, PTR("sl4_h_smash3"))
                END_IF
                IF_INT_EQUAL(LW(6), 2)
                    CALL(sl4_house_add, -20, PTR("sl4_h_smash2"))
                END_IF
                IF_INT_EQUAL(LW(6), 1)
                    CALL(sl4_house_add, -20, PTR("sl4_h_smash1"))
                END_IF
                WAIT_MS(600)
                CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
                CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(3), LW(4), LW(5))
                CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(3), LW(4), LW(5), 0, -1, 0)
                CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
                GOTO(99)
            END_IF
        END_IF
    END_IF
    IF_INT_LESS(LW(2), 20)
        /* Lullaby to the House: the seats nod off */
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 50)
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_PANSY_ATTACK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        LOOP(6)
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(3), LW(4), LW(5))
            ADD(LW(4), 30)
            CALL(evt_eff64, STRING(""), STRING("onpu_n64"), 2, LW(3), LW(4), LW(5), 0, 0, 0, 0, 0, 0, 0, 0)
            WAIT_FRAMES(12)
        END_LOOP
        CALL(sl4_house_add, -10, PTR("sl4_h_hush"))
        WAIT_MS(500)
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
        GOTO(99)
    END_IF
    IF_INT_LESS(LW(2), 65)
        /* a prop to the head: the saboteurs are here to hurt the show, and the cast */
        SET_VALUE(LW(9), PTR(&wProp))
        SET_VALUE(LW(8), 1)
        CALL_CHILD(sl4_hit)
        GOTO(99)
    END_IF
    SET_VALUE(LW(9), PTR(&wLullaby))
    CALL_CHILD(sl4_song)
    LABEL(99)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

EVT_BEGIN(sl4_under_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(sl4_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(sl4_under_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(sl4_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(sl4_under_attack))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- a Choir Dayzee: the first awake one leads the Choir's song --- */
static EVT_BEGIN(sl4_choir_attack)
    CALL(sl4_choir_count, LW(0), LW(1), LW(2), LW(3))
    IF_INT_EQUAL(LW(3), 1)
        SET_VALUE(LW(1), 0)
    END_IF
    IF_INT_EQUAL(LW(1), 0)
        /* humming along */
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("PAN_T_1"))
        WAIT_FRAMES(20)
        GOTO(99)
    END_IF
    IF_INT_LESS_OR_EQUAL(LW(0), 1)
        CALL(evt_msg_print, 2, STRING("sl4_choir_thin"), 0, BTL_UNIT_SELF)
        GOTO(99)
    END_IF
    IF_INT_GREATER_OR_EQUAL(LW(0), 4)
        CALL(evt_msg_print, 2, STRING("sl4_choir_full"), 0, BTL_UNIT_SELF)
    ELSE
        CALL(evt_msg_print, 2, STRING("sl4_choir_some"), 0, BTL_UNIT_SELF)
    END_IF
    CALL(sl4_choir_weapon, LW(0), LW(9))
    CALL_CHILD(sl4_song)
    LABEL(99)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

EVT_BEGIN(sl4_choir_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(sl4_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(sl4_choir_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(sl4_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(sl4_choir_attack))
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- the Choirmaster: Encore! (a fallen singer back), Crescendo when the Choir thins --- */
static EVT_BEGIN(sl4_master_attack)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_A, LW(10))
    ADD(LW(10), 1)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, LW(10))
    CALL(sl4_choir_count, LW(0), LW(1), LW(2), 0)
    IF_INT_LESS(LW(2), 4)
        IF_INT_GREATER_OR_EQUAL(LW(10), 4)
            /* Encore! a fallen singer back, at 3 HP */
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, 0)
            CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 50)
            CALL(evt_msg_print, 2, STRING("sl4_master_encore"), 0, BTL_UNIT_SELF)
            CALL(btlevtcmd_SpawnUnit, LW(3), PTR(&sSpawnChoir), 0)
            IF_INT_NOT_EQUAL(LW(3), -1)
                CALL(btlevtcmd_SetHp, LW(3), 3)
                CALL(btlevtcmd_GetPos, LW(3), LW(4), LW(5), LW(6))
                CALL(evt_eff, 0, STRING("stardust"), 6, LW(4), 30, LW(6), 20, 20, 8, 40, 0, 0, 0, 0)
            END_IF
            WAIT_MS(500)
            GOTO(99)
        END_IF
    END_IF
    IF_INT_LESS_OR_EQUAL(LW(2), 2)
        CALL(evt_msg_print, 2, STRING("sl4_master_crescendo"), 0, BTL_UNIT_SELF)
        SET_VALUE(LW(9), PTR(&wCrescendo))
        SET_VALUE(LW(8), 1)
        CALL_CHILD(sl4_hit)
        GOTO(99)
    END_IF
    /* conducting: the Choir swells */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("PAN_T_1"))
    CALL(evt_msg_print, 2, STRING("sl4_master_conduct"), 0, BTL_UNIT_SELF)
    WAIT_FRAMES(30)
    LABEL(99)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

EVT_BEGIN(sl4_master_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(sl4_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(sl4_master_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(sl4_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(sl4_master_attack))
    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(0.9f), FLOAT(0.9f), FLOAT(0.9f))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, 2)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- Bruno: two turns of winding up, then the Chandelier Drop; a Stylish sets him dancing --- */
static EVT_BEGIN(sl4_bruno_attack)
    CALL(sl4_dancing_bruno, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        /* he can't help himself */
        CALL(evt_msg_print, 2, STRING("sl4_bruno_dance"), 0, BTL_UNIT_SELF)
        LOOP(3)
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(1), LW(2), LW(3))
            CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(1), LW(2), LW(3), FLOAT(12.0f), -1)
            CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, 1)
            CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, -1)
        END_LOOP
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, 0)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_A, LW(0))
    IF_INT_LESS(LW(0), 2)
        ADD(LW(0), 1)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, LW(0))
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 51)
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_CONDITION_CHARGE1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(1), LW(2), LW(3))
        ADD(LW(2), 20)
        CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(1), LW(2), LW(3), FLOAT(0.5f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_msg_print, 2, STRING("sl4_bruno_charge"), 0, BTL_UNIT_SELF)
        GOTO(99)
    END_IF
    /* the Chandelier Drop, on the front of the stage */
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, 0)
    CALL(evt_msg_print, 2, STRING("sl4_bruno_drop"), 0, BTL_UNIT_SELF)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(1), LW(2), LW(3))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(1), 80, LW(3), FLOAT(20.0f), -1)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_SYS_BREAK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(1), LW(2), LW(3), FLOAT(20.0f), -1)
    CALL(sl4_set_down, LW(4), 1)
    SET_VALUE(LW(9), PTR(&wChandelier))
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(9))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(9), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(sl4_set_id, 1, -1, LW(10))
    IF_INT_GREATER_OR_EQUAL(LW(10), 0)
        CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_SetPos, LW(10), LW(0), 0, LW(2))
        CALL(btlevtcmd_AnimeChangePose, LW(10), 1, STRING("W_1"))
        WAIT_FRAMES(20)
        CALL(btlevtcmd_AnimeChangePose, LW(10), 1, STRING("F_1"))
        WAIT_FRAMES(8)
    END_IF
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    ADD(LW(1), 90)
    CALL(evt_eff, 0, STRING("stardust"), 6, LW(0), LW(1), LW(2), 30, 30, 10, 60, 0, 0, 0, 0)
    WAIT_FRAMES(24)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_SYS_BREAK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(9), DAMAGE_FLAG_LAST_HIT, LW(5))
    IF_INT_EQUAL(LW(5), 1)
        CALL(btlevtcmd_ResultACDefence, LW(3), LW(9))
        CALL(btlevtcmd_GetResultACDefence, LW(6))
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(9), DAMAGE_FLAG_LAST_HIT, LW(5))
        IF_INT_EQUAL(LW(6), 5)
            CALL(sl4_house_add, 20, PTR("sl4_h_caught"))
        END_IF
    ELSE
        CALL(btlevtcmd_StartAvoid, LW(3), 38)
    END_IF
    CALL(evt_btl_camera_shake_h, 0, 6, 0, 30, 13)
    LABEL(99)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

EVT_BEGIN(sl4_bruno_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(sl4_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(sl4_bruno_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(sl4_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(sl4_bruno_attack))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_B, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- Dazzlina: Upstaged at 150 every other turn (she comes back with the Petal Storm), else
 * Spotlight Steal, Petal Storm, Diva Aria or Encore! Encore! A House kept full used to lose
 * her every turn, and the boss of the chapter never landed a hit --- */
static EVT_BEGIN(sl4_dazz_attack)
    CALL(sl4_house_get, LW(0))
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_B, LW(7))
    IF_INT_EQUAL(LW(7), 1)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_B, 0)
        CALL(evt_msg_print, 2, STRING("sl4_dazz_petal"), 0, BTL_UNIT_SELF)
        SET_VALUE(LW(9), PTR(&wPetal))
        SET_VALUE(LW(8), 3)
        CALL_CHILD(sl4_hit)
        GOTO(99)
    END_IF
    IF_INT_GREATER_OR_EQUAL(LW(0), 150)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_B, 1)
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_DAMAGE)
        CALL(evt_msg_print, 2, STRING("sl4_dazz_upstaged"), 0, BTL_UNIT_SELF)
        CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(1), LW(2), LW(3))
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, 220, LW(2), LW(3), 0, -1, 0)
        WAIT_MS(500)
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(1), LW(2), LW(3), 0, -1, 0)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_A, LW(10))
    ADD(LW(10), 1)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, LW(10))
    CALL(sl4_foes, LW(11), LW(12))
    IF_INT_GREATER_OR_EQUAL(LW(10), 3)
        IF_INT_EQUAL(LW(11), 0)
            /* Encore! Encore!: an understudy back on */
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, 0)
            CALL(evt_msg_print, 2, STRING("sl4_dazz_encore"), 0, BTL_UNIT_SELF)
            CALL(btlevtcmd_SpawnUnit, LW(3), PTR(&sSpawnUnder), 0)
            WAIT_MS(500)
            GOTO(99)
        END_IF
    END_IF
    CALL(evt_sub_random, 99, LW(0))
    IF_INT_LESS(LW(0), 25)
        /* Spotlight Steal */
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 50)
        CALL(evt_msg_print, 2, STRING("sl4_dazz_spot"), 0, BTL_UNIT_SELF)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(1), LW(2), LW(3))
        ADD(LW(2), 30)
        CALL(evt_eff, 0, STRING("stardust"), 6, LW(1), LW(2), LW(3), 30, 30, 12, 60, 0, 0, 0, 0)
        CALL(sl4_house_add, -15, PTR("sl4_h_spot"))
        CALL(btlevtcmd_RecoverHp, BTL_UNIT_SELF, 1, 5)
        WAIT_MS(700)
        GOTO(99)
    END_IF
    IF_INT_LESS(LW(0), 60)
        CALL(evt_msg_print, 2, STRING("sl4_dazz_petal"), 0, BTL_UNIT_SELF)
        SET_VALUE(LW(9), PTR(&wPetal))
        SET_VALUE(LW(8), 3)
        CALL_CHILD(sl4_hit)
        GOTO(99)
    END_IF
    CALL(evt_msg_print, 2, STRING("sl4_dazz_aria"), 0, BTL_UNIT_SELF)
    SET_VALUE(LW(9), PTR(&wAria))
    CALL_CHILD(sl4_song)
    LABEL(99)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

EVT_BEGIN(sl4_dazz_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(sl4_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(sl4_dazz_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(sl4_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(sl4_dazz_attack))
    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.2f), FLOAT(1.2f), FLOAT(1.2f))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_A, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_B, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- Petunia: the play itself --- */

/* the act's set comes on (LW(0) the act): the last one goes */
EVT_BEGIN(sl4_set_on)
    CALL(sl4_set_id, 0, -1, LW(5))
    IF_INT_GREATER_OR_EQUAL(LW(5), 0)
        CALL(btlevtcmd_KillUnit, LW(5), 0)
        CALL(sl4_set_id, 0, -2, LW(5))
    END_IF
    CALL(sl4_set_id, 1, -1, LW(5))
    IF_INT_GREATER_OR_EQUAL(LW(5), 0)
        CALL(btlevtcmd_KillUnit, LW(5), 0)
        CALL(sl4_set_id, 1, -2, LW(5))
    END_IF
    CALL(sl4_set_setup, LW(0), LW(6))
    IF_INT_NOT_EQUAL(LW(6), 0)
        CALL(btlevtcmd_SpawnUnit, LW(5), LW(6), 0)
        IF_INT_NOT_EQUAL(LW(5), -1)
            CALL(btlevtcmd_SetScale, LW(5), FLOAT(5.0f), FLOAT(5.0f), FLOAT(1.0f))
            CALL(sl4_set_id, 0, LW(5), LW(7))
            IF_INT_EQUAL(LW(0), 1)
                /* the cardboard sun, raised by a stagehand */
                CALL(btlevtcmd_AnimeChangePose, LW(5), 1, STRING("U_1"))
            END_IF
        END_IF
    END_IF
    IF_INT_EQUAL(LW(0), 2)
        CALL(sl4_set_setup, 4, LW(6))
        IF_INT_NOT_EQUAL(LW(6), 0)
            CALL(btlevtcmd_SpawnUnit, LW(5), LW(6), 0)
            IF_INT_NOT_EQUAL(LW(5), -1)
                CALL(btlevtcmd_SetScale, LW(5), FLOAT(1.5f), FLOAT(1.5f), FLOAT(1.0f))
                CALL(sl4_set_id, 1, LW(5), LW(7))
            END_IF
        END_IF
    END_IF
    RETURN
EVT_END()

/* in: a pose for the set now (Prop Smash, the eruption), or for the chandelier (LW(1) 1) */
EVT_BEGIN(sl4_set_pose)
    CALL(sl4_set_id, LW(1), -1, LW(5))
    IF_INT_GREATER_OR_EQUAL(LW(5), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(5), 1, LW(0))
    END_IF
    RETURN
EVT_END()

/* the curtain between acts: the stage goes dark, the next act's look comes up (LW(0) the act) */
static EVT_BEGIN(sl4_curtain)
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_CLAPPING2"), 0)
    CALL(evt_map_set_blend, 0, 40, 40, 40, 255)
    WAIT_MS(600)
    CALL_CHILD(sl4_set_on)
    CALL(sl4_look, LW(0))
    CALL(evt_map_blend_off, 0)
    RETURN
EVT_END()

static EVT_BEGIN(sl4_pet_attack)
    CALL(sl4_turn_inc)
    CALL(sl4_test_down, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(sl4_partner_up, LW(0))
        IF_INT_EQUAL(LW(0), 1)
            CALL(btlevtcmd_DamageDirect, BTL_UNIT_PARTNER, 1, 99, DAMAGE_FLAG_LAST_HIT | DAMAGE_HIT_NORMAL, 0, 0)
        END_IF
        GOTO(99)
    END_IF
    CALL(sl4_lost, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        /* the house is empty: the curtain drops, the show is cancelled */
        CALL(evt_msg_print, 2, STRING("sl4_cancelled"), 0, BTL_UNIT_SELF)
        /* (the last-hit flag is what sets KO and defeat; a partner may not be out at all, or be down) */
        CALL(sl4_partner_up, LW(0))
        IF_INT_EQUAL(LW(0), 1)
            CALL(btlevtcmd_DamageDirect, BTL_UNIT_PARTNER, 1, 99, DAMAGE_FLAG_LAST_HIT | DAMAGE_HIT_NORMAL, 0, 0)
        END_IF
        CALL(btlevtcmd_DamageDirect, BTL_UNIT_MARIO, 1, 99, DAMAGE_FLAG_LAST_HIT | DAMAGE_HIT_NORMAL, 0, 0)
        GOTO(99)
    END_IF
    CALL(sl4_act, LW(10), LW(11), -1)
    CALL(sl4_foes, LW(12), LW(13))
    SWITCH(LW(10))
        CASE_EQUAL(1)
            IF_INT_EQUAL(LW(11), 1)
                /* the play opens */
                CALL(evt_msg_print, 2, STRING("sl4_act1_00"), 0, BTL_UNIT_SELF)
                CALL(sl4_partner_is, PORT_SL_FRIEND_JERRY, LW(0))
                IF_INT_EQUAL(LW(0), 1)
                    CALL(evt_msg_print, 2, STRING("sl4_act1_jerry"), 0, BTL_UNIT_PARTNER)
                END_IF
                CALL(evt_msg_print, 2, STRING("sl4_act1_01"), 0, BTL_UNIT_SELF)
            END_IF
            IF_INT_EQUAL(LW(12), 0)
                GOTO(20)
            END_IF
            IF_INT_GREATER(LW(11), 4)
                GOTO(20)
            END_IF
            GOTO(99)
            /* Act 2: The Ball */
            LABEL(20)
            CALL(sl4_set_down, LW(0), -1)
            IF_INT_EQUAL(LW(0), 0)
                CALL(sl4_house_add, 10, PTR("sl4_h_set_ok"))
            END_IF
            CALL(sl4_clear_foes)
            CALL(evt_msg_print, 2, STRING("sl4_act_end_1"), 0, BTL_UNIT_SELF)
            SET_VALUE(LW(0), 2)
            CALL_CHILD(sl4_curtain)
            CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_PEACH_WALTZ1"))
            CALL(sl4_act, LW(1), LW(2), 2)
            CALL(btlevtcmd_SpawnUnit, LW(3), PTR(&sSpawnBruno), 0)
            CALL(btlevtcmd_SpawnUnit, LW(3), PTR(&sSpawnUnder), 0)
            CALL(evt_msg_print, 2, STRING("sl4_act2_00"), 0, BTL_UNIT_SELF)
            CALL(sl4_partner_is, PORT_SL_FRIEND_HAYZEE, LW(0))
            IF_INT_EQUAL(LW(0), 1)
                CALL(evt_msg_print, 2, STRING("sl4_act2_baron"), 0, BTL_UNIT_PARTNER)
            END_IF
            CALL(sl4_partner_is, PORT_SL_FRIEND_BLOOEY, LW(0))
            IF_INT_EQUAL(LW(0), 1)
                CALL(evt_msg_print, 2, STRING("sl4_act2_river"), 0, BTL_UNIT_SELF)
                CALL(sl4_rapids)
            END_IF
            CALL(evt_msg_print, 2, STRING("sl4_act2_bruno"), 0, BTL_UNIT_SELF)
        CASE_EQUAL(2)
            IF_INT_EQUAL(LW(12), 0)
                GOTO(30)
            END_IF
            IF_INT_GREATER(LW(11), 8)
                GOTO(30)
            END_IF
            GOTO(99)
            /* Act 3: The Volcano, and Dazzlina */
            LABEL(30)
            CALL(sl4_set_down, LW(0), -1)
            IF_INT_EQUAL(LW(0), 0)
                CALL(sl4_house_add, 10, PTR("sl4_h_set_ok"))
            END_IF
            CALL(sl4_clear_foes)
            CALL(evt_msg_print, 2, STRING("sl4_act_end_2"), 0, BTL_UNIT_SELF)
            SET_VALUE(LW(0), 3)
            CALL_CHILD(sl4_curtain)
            CALL(sl4_act, LW(1), LW(2), 3)
            CALL(sl4_partner_is, PORT_SL_FRIEND_TORQUE, LW(0))
            IF_INT_EQUAL(LW(0), 1)
                CALL(evt_msg_print, 2, STRING("sl4_act3_torque"), 0, BTL_UNIT_PARTNER)
            END_IF
            CALL(evt_msg_print, 2, STRING("sl4_act3_00"), 0, BTL_UNIT_SELF)
            /* the house lights cut; sparkles; she sweeps on */
            CALL(evt_map_set_blend, 0, 30, 30, 50, 255)
            CALL(evt_snd_bgmoff, PSND_FADE_OUT)
            WAIT_MS(700)
            CALL(btlevtcmd_SpawnUnit, LW(3), PTR(&sSpawnDazz), 0)
            CALL(btlevtcmd_GetPos, LW(3), LW(4), LW(5), LW(6))
            ADD(LW(5), 30)
            CALL(evt_eff, 0, STRING("stardust"), 6, LW(4), LW(5), LW(6), 40, 40, 16, 80, 0, 0, 0, 0)
            CALL(evt_map_blend_off, 0)
            CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_BOSS_STG3_3WOMEN1"))
            CALL(evt_msg_print, 2, STRING("sl4_act3_dazz"), 0, LW(3))
            CALL(evt_msg_print, 2, STRING("sl4_act3_01"), 0, BTL_UNIT_SELF)
            CALL(sl4_partner_is, PORT_SL_FRIEND_HAYZEE, LW(0))
            IF_INT_EQUAL(LW(0), 1)
                CALL(evt_msg_print, 2, STRING("sl4_act3_hayzee"), 0, BTL_UNIT_PARTNER)
            ELSE
                CALL(evt_msg_print, 2, STRING("sl4_act3_hayzee_wing"), 0, BTL_UNIT_SELF)
            END_IF
        CASE_EQUAL(3)
            IF_INT_EQUAL(LW(13), 0)
                /* she's down: the volcano erupts on cue, the hat held high, curtain */
                /* the understudy of her Encore! flees too: left on stage, it played on after the curtain and
                 * could knock Luigi out, a Game Over for a show already won */
                CALL(sl4_clear_foes)
                CALL(sl4_partner_is, PORT_SL_FRIEND_TORQUE, LW(0))
                IF_INT_EQUAL(LW(0), 1)
                    CALL(evt_msg_print, 2, STRING("sl4_finale_torque"), 0, BTL_UNIT_PARTNER)
                ELSE
                    CALL(evt_msg_print, 2, STRING("sl4_finale_torque_wing"), 0, BTL_UNIT_SELF)
                END_IF
                SET_VALUE(LW(0), PTR("E_1"))
                SET_VALUE(LW(1), 0)
                CALL_CHILD(sl4_set_pose)
                CALL(evt_map_set_blend, 0, 255, 120, 60, 255)
                CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_BOMB_BURST1"), 0)
                CALL(evt_eff, 0, STRING("bomb"), 0, 0, 60, -120, FLOAT(2.5f), 0, 0, 0, 0, 0, 0, 0)
                CALL(evt_btl_camera_shake_h, 0, 8, 0, 40, 13)
                WAIT_MS(500)
                CALL(evt_eff, 0, STRING("confetti"), 3, 0, 150, 0, 160, 0, 0, 0, 0, 0, 0, 0)
                CALL(evt_msg_print, 2, STRING("sl4_finale_00"), 0, BTL_UNIT_SELF)
                CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_CHEER_ALLY4"), 0)
                CALL(sl4_house_add, 25, PTR("sl4_h_finale"))
                CALL(sl4_final)
                WAIT_MS(1200)
                CALL(evt_map_blend_off, 0)
                /* off before the field draws again: its display lists are rebuilt on that first frame and
                 * kept the volcano's look, leaving the hall orange for the verdict and the party */
                CALL(sl4_look, 0)
                CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 0)
                RETURN
            END_IF
            CALL(sl4_once, 0, LW(0))
            IF_INT_EQUAL(LW(0), 1)
                CALL(evt_msg_print, 2, STRING("sl4_act3_grass"), 0, BTL_UNIT_SELF)
                WAIT_MS(1000)
                CALL(sl4_house_add, 5, PTR("sl4_h_silence"))
            END_IF
    END_SWITCH
    LABEL(99)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

EVT_BEGIN(sl4_set_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(sl4_set_wait))
    RETURN
EVT_END()

EVT_BEGIN(sl4_pet_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(sl4_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(sl4_pet_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(sl4_wait))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(sl4_pet_attack))
    CALL(sl4_look, 1)
    SET_VALUE(LW(0), 1)
    CALL_CHILD(sl4_set_on)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* ======================================================================
 * Arming, the House, the hooks
 * ====================================================================== */

/* Blooey as the River: his next move hits twice as hard */
USER_FUNC(sl4_rapids) {
    (void)event, (void)isFirstCall;
    sRapids = 1;
    return EVT_RETURN_DONE;
}

/* in: a once-flag (0-7): out 1 the first time this battle */
static int sOnce;
USER_FUNC(sl4_once) {
    int bit = 1 << evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], !(sOnce & bit));
    sOnce |= bit;
    return EVT_RETURN_DONE;
}

/* the show's over: its final House, for the review */
USER_FUNC(sl4_final) {
    (void)event, (void)isFirstCall;
    sHouseFinal = sHouse;
    return EVT_RETURN_DONE;
}

/* in: ARM_* (0 off): the next field battle is that fight */
USER_FUNC(evt_sl4_arm) {
    int arm = evtGetValue(event, event->args[0]);
    PouchData* p = pouchGetPtr();
    (void)isFirstCall;
    if (arm == ARM_SLAM && sArmed != ARM_SLAM) {
        /* the audience needs a Star Power: one is lent for the night, with none to spend */
        sStarWas = p->starPowersObtained;
        sMaxSpWas = p->maximumSP;
        sSpWas = p->currentSP;
        if (p->starPowersObtained == 0) {
            p->starPowersObtained = 1;
        }
    } else if (arm != ARM_SLAM && sArmed == ARM_SLAM) {
        p->starPowersObtained = (u16)sStarWas;
        p->maximumSP = (s16)sMaxSpWas;
        p->currentSP = (s16)sSpWas;
    }
    sArmed = arm;
    return EVT_RETURN_DONE;
}

/* out: the House at the curtain */
USER_FUNC(evt_sl4_house) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], sHouseFinal);
    return EVT_RETURN_DONE;
}

/* Btl_UnitSetup: the armed fight's group */
static int unitSetupPre(BOOL* result, BattleWork** wp) {
    (void)result;
    if (sArmed != ARM_NONE && portSuperLuigiChapter() != 4) {
        /* armed when the chapter ended (a Game Over in the fight, the chapter
         * left from the Modes tab): never another game's battle */
        sArmed = ARM_NONE;
        sSlam = 0;
    }
    if (sArmed == ARM_NONE || (*wp)->fbatInfo == NULL || (*wp)->fbatInfo->setup == NULL) {
        return 0;
    }
    if (sArmed == ARM_CHOIR) {
        (*wp)->fbatInfo->setup->groupSetup = &kChoirGroup;
        (*wp)->fbatInfo->setup->bgmName = "BGM_CHUBOSS_BATTLE1";
        sSlam = 0;
        printf("[superluigi] the Hum-Along Choir\n");
    } else {
        (*wp)->fbatInfo->setup->groupSetup = &kSlamGroup;
        (*wp)->fbatInfo->setup->bgmName = "BGM_EVT_STG3_EGG1";
        (*wp)->fbatInfo->setup->battleFlags |= NPC_BATTLE_NO_ESCAPE;
        sSlam = 1;
        /* $TTYD_SL_SLAM_HOUSE=N: the curtain goes up on that House (tests: the show cancelled) */
        sHouse = getenv("TTYD_SL_SLAM_HOUSE") != NULL ? atoi(getenv("TTYD_SL_SLAM_HOUSE")) : 100;
        sHouseFinal = sHouse;
        sEmptied = 0;
        sAct = 1;
        sTurn = 0;
        sActStart = 0;
        sSetDown = 0;
        sLieLow = sLieLowUsed = 0;
        sRapids = 0;
        sOnce = 0;
        sSetId = sChandId = -1;
        kDazz.numParts = haveModel("c_sl_dazzlina_tiara") ? 2 : 1;
        pouchGetPtr()->maximumSP = 0;
        pouchGetPtr()->currentSP = 0;
        printf("[superluigi] the Drama Slam: curtain up\n");
    }
    fflush(stdout);
    return 0;
}

/* the House on screen, top centre; the audience follows it */
static void houseDisp(CameraId cameraId, void* param) {
    char text[32];
    u16 lines = 0;
    u16 width;
    f32 x, y;
    (void)cameraId, (void)param;
    snprintf(text, sizeof(text), "%s %d", msgSearch("sl4_house"), sHouse);
    width = FontGetMessageWidthLine(text, &lines);
    x = -(f32)((s32)width / 2);
    y = 222.0f;
    windowDispGX_Waku_col(0, sHouse >= 150 ? (GXColor){0xFF, 0xE8, 0x80, 0xFF} : (GXColor){0xFF, 0xFF, 0xFF, 0xFF},
                          x - 10.0f, -29.0f + (3.0f + y), (f32)((s32)width + 20), 32.0f, 10.0f);
    FontDrawStart();
    FontDrawMessage((s32)x, (s32)y - 29, text);
}

static void frame(void) {
    BattleAudience* a;
    if (!portSlSlamBattle()) {
        if (sSlam && gp != NULL && !gp->inBattle) {
            sSlam = 0;
            portMapLookSet(NULL, 0xFFFFFF, 0);
        }
        return;
    }
    dispEntry(CAMERA_2D, 880.0f, 1, houseDisp, NULL);
    a = BattleAudienceBaseGetPtr();
    if (a != NULL && a->maxAudience > 0) {
        f32 want = (f32)(sHouse * a->maxAudience / 250);
        a->targetAudienceCount = want;
    }
}

/* the House's sources: Stylish, Appeal, a heckler bonked, the turn's jumps and hammers */
static void acrobatGoodPost(void) {
    int first;
    BattleUnit* b;
    if (!portSlSlamBattle()) {
        return;
    }
    sStylish = 1;
    houseAdd(5, NULL);
    /* Bruno, winding up, can't help dancing */
    countKind(&kBruno, 0, &first);
    b = first >= 0 ? BattleGetUnitPtr(_battleWorkPointer, first) : NULL;
    if (b != NULL && b->work[UW_A] > 0 && !b->work[UW_B]) {
        b->work[UW_B] = 1;
        houseAdd(5, "sl4_h_bruno");
    }
}

static void appealPost(BattleUnit* unit) {
    BattleUnit* m = BattleGetMarioPtr(_battleWorkPointer);
    houseAdd(15, unit == m ? "sl4_h_sway" : "sl4_h_appeal");
}

static void audienceAttackPost(s32 index) {
    (void)index;
    houseAdd(5, "sl4_h_heckler");
}

static void checkDamagePost(s32* result, BattleUnit* attacker, BattleUnit* target, BattleUnitPart* part,
                            BattleWeapon* weapon, s32 flags) {
    (void)result, (void)target, (void)part, (void)flags;
    if (!portSlSlamBattle() || attacker == NULL || weapon == NULL || attacker->alliance != 0) {
        return;
    }
    if (attacker == BattleGetMarioPtr(_battleWorkPointer)) {
        if (weapon->targetPropertyFlags & (TARGET_PROP_JUMPLIKE | TARGET_PROP_HAMMERLIKE)) {
            sMarioHit = 1;
        }
    } else if (attacker == BattleGetPartyPtr(_battleWorkPointer)) {
        sPartnerHit = 1;
    }
}

static int perActPre(void) {
    if (!portSlSlamBattle()) {
        sMarioHit = sPartnerHit = sStylish = 0;
        return 0;
    }
    if (sMarioHit && !sStylish) {
        houseAdd(-2, "sl4_h_grass_no");
    }
    if (sPartnerHit) {
        houseAdd(3, NULL);
    }
    sMarioHit = sPartnerHit = sStylish = 0;
    return 0;
}

/* Lie Low: the next attack at Luigi this turn misses */
static void preCheckPost(s32* result, BattleUnit* attacker, BattleUnit* target, BattleUnitPart* part,
                         BattleWeapon* weapon, u32 flags) {
    (void)part, (void)weapon, (void)flags;
    if (sLieLow && portSlSlamBattle() && attacker != NULL && attacker->alliance == 1 &&
        target == BattleGetMarioPtr(_battleWorkPointer) && *result == 1) {
        *result = 3;
        sLieLow = 0;
        portSlPrompt("sl4_h_lielow", 2000);
    }
}

/* The Rapids: Blooey's next move, double */
static int calculateDamagePre(s32* result, BattleUnit** attacker, BattleUnit** target, BattleUnitPart** part,
                              BattleWeapon** weapon, u32** out, u32* flags) {
    static BattleWeapon sCopy;
    int i;
    (void)result, (void)target, (void)part, (void)out, (void)flags;
    if (!sRapids || !portSlSlamBattle() || *attacker != BattleGetPartyPtr(_battleWorkPointer) || *weapon == NULL) {
        return 0;
    }
    sCopy = **weapon;
    for (i = 0; i < 8; i++) {
        sCopy.hpDamageWork[i] *= 2;
    }
    *weapon = &sCopy;
    sRapids = 0;
    portSlPrompt("sl4_h_rapids", 2000);
    return 0;
}

void portSlSlamRegister(void) {
    wLullaby = mkWeapon(0, 0, 60, 1);
    wChoirAll = mkWeapon(0, 1, 60, 1);
    wChoirOne = mkWeapon(0, 0, 70, 1);
    wCrescendo = mkWeapon(4, 0, 0, 0);
    wChandelier = mkWeapon(6, 0, 0, 0);
    wChandelier.targetWeightFlags = TARGET_WEIGHT_PREFER_FRONT;
    wAria = mkWeapon(0, 1, 60, 1);
    wPetal = mkWeapon(3, 0, 0, 0);
    wProp = mkWeapon(3, 0, 0, 0);
    wCharge = mkWeapon(0, 0, 0, 0);
    mkPart(pUnder, "sl4_un_under", "c_pansy", kDzPoses, 29.0f);
    mkPart(pChoir, "sl4_un_choir", "c_pansy", kDzPoses, 29.0f);
    mkPart(pMaster, "sl4_un_master", "c_kpansy", kDzPoses, 29.0f);
    mkPart(pBruno, "sl4_un_bruno", "c_kuribo_h", kGbPoses, 22.0f);
    mkPart(pDazz, "sl4_un_dazz", "c_kpansy", kDzPoses, 29.0f);
    /* her sequin tiara, on her head (the models agent's) */
    mkPart(&pDazz[1], "sl4_un_dazz", "c_sl_dazzlina_tiara", kTiaraPoses, 29.0f);
    pDazz[1].partNum = 2;
    pDazz[1].positionOffset.y = 30.0f;
    pDazz[1].positionOffset.z = 1.0f;
    pDazz[1].attributes = PART_ATTR_NOT_TARGETABLE | PART_ATTR_UNTATTLEABLE | PART_ATTR_NO_SHADOW |
                          PART_ATTR_IMMUNE_TO_DAMAGE_OR_STATUS;
    mkPart(pRoad, "sl4_un_set", "c_sl_set_road", kSunPoses, 0.0f);
    mkPart(pBall, "sl4_un_set", "c_sl_set_ballroom", kSetPoses, 0.0f);
    mkPart(pVolc, "sl4_un_set", "c_sl_set_volcano", kSetPoses, 0.0f);
    mkPart(pChand, "sl4_un_set", "c_sl_chandelier", kSetPoses, 0.0f);
    mkPart(pPetunia, "sl4_un_petunia", "c_sl_petunia", kDzPoses, 29.0f);
    pPetunia->attributes = PART_ATTR_MAIN | PART_ATTR_NOT_TARGETABLE | PART_ATTR_UNTATTLEABLE |
                               PART_ATTR_IMMUNE_TO_DAMAGE_OR_STATUS | PART_ATTR_JUMPLIKE_CANNOT_TARGET |
                               PART_ATTR_HAMMERLIKE_CANNOT_TARGET | PART_ATTR_SHELL_TOSSLIKE_CANNOT_TARGET;
    mkKind(&kUnder, UNIT_CRAZEE_DAYZEE, "sl4_un_under", 7, 24, 0, pUnder, sl4_under_init, &kRegistDz);
    mkKind(&kChoirDz, UNIT_CRAZEE_DAYZEE, "sl4_un_choir", 4, 24, 0, pChoir, sl4_choir_init, &kRegistDz);
    mkKind(&kMaster, 1, "sl4_un_master", 15, 40, 10, pMaster, sl4_master_init, &kRegistBoss);
    mkKind(&kBruno, 1, "sl4_un_bruno", 10, 26, 0, pBruno, sl4_bruno_init, &kRegistDz);
    kBruno.damageSfxName = "SFX_ENM_KURI_DAMAGED1";
    mkKind(&kDazz, 1, "sl4_un_dazz", 30, 45, 35, pDazz, sl4_dazz_init, &kRegistBoss);
    mkKind(&kPetunia, 1, "sl4_un_petunia", 99, 0, 0, pPetunia, sl4_pet_init, &kRegistNone);
    kPetunia.turnOrder = 100;
    kPetunia.attributeFlags = UNIT_ATTR_NOT_SAMPLED_AS_TARGET | UNIT_ATTR_NO_HP_GAUGE | UNIT_ATTR_NO_DAMAGE_NUMBER;
    kPetunia.bonusCoin = kPetunia.baseCoin = 0;
    {
        BattleUnitKind* sets[4] = {&kRoad, &kBall, &kVolc, &kChand};
        BattleUnitKindPart* parts[4] = {pRoad, pBall, pVolc, pChand};
        int i;
        for (i = 0; i < 4; i++) {
            mkKind(sets[i], 1, "sl4_un_set", 99, 0, 0, parts[i], NULL, &kRegistNone);
            parts[i]->attributes = PART_ATTR_NOT_TARGETABLE | PART_ATTR_UNTATTLEABLE | PART_ATTR_NO_SHADOW |
                                   PART_ATTR_IMMUNE_TO_DAMAGE_OR_STATUS;
            sets[i]->attributeFlags = UNIT_ATTR_NOT_SAMPLED_AS_TARGET | UNIT_ATTR_NO_HP_GAUGE | UNIT_ATTR_NO_TURN |
                                      UNIT_ATTR_IGNORE_FOR_CLEAR | UNIT_ATTR_NO_SHADOW | UNIT_ATTR_NO_DAMAGE_NUMBER;
            sets[i]->initEventCode = sl4_set_init;
            sets[i]->bonusCoin = sets[i]->baseCoin = 0;
        }
    }
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)unitSetupPre);
    portModHostAddHook(1, kSlot_marioStMain, (void*)frame);
    portModHostAddHook(1, kSlot_BattleAudience_Case_AcrobatGood, (void*)acrobatGoodPost);
    portModHostAddHook(1, kSlot_BattleAudience_Case_Appeal, (void*)appealPost);
    portModHostAddHook(1, kSlot_BattleAudience_Attack, (void*)audienceAttackPost);
    portModHostAddHook(1, kSlot_BattleCheckDamage, (void*)checkDamagePost);
    portModHostAddHook(0, kSlot_BattleAudience_PerAct, (void*)perActPre);
    portModHostAddHook(1, kSlot_BattlePreCheckDamage, (void*)preCheckPost);
    portModHostAddHook(0, kSlot_BattleCalculateDamage, (void*)calculateDamagePre);
}

/* the text of the fights (the chapter's own tags) */
static const char* const kSlamText[] = {
    "sl4_house", "House:",
    "sl4_un_under", "Understudy",
    "sl4_un_choir", "Choir Dayzee",
    "sl4_un_master", "Choirmaster",
    "sl4_un_bruno", "Bruno",
    "sl4_un_dazz", "Dazzlina",
    "sl4_un_petunia", "Petunia",
    "sl4_un_set", "Scenery",
    "sl4_choir_full", "Hmmmm... all together now...\nSleep forever and EVER...\n<k>",
    "sl4_choir_some", "Hmm... hmm... the Choir sings\non, thinner but sweeter...\n<k>",
    "sl4_choir_thin", "...hmm? Where did everybody go?\nI can't sing a choir by\nmyself...\n<k>",
    "sl4_master_encore", "Encore! ENCORE! Back to your\nstump, you!\n<k>",
    "sl4_master_crescendo", "My Choir! My beautiful Choir!\nCRESCENDO!\n<k>",
    "sl4_master_conduct", "And... a little softer...\nsofter... sleepier...\n<k>",
    "sl4_bruno_charge", "Bruno's winding up. Something\nup in the flies is creaking.\n<k>",
    "sl4_bruno_drop", "Lights out, Grass!\nCHANDELIER!\n<k>",
    "sl4_bruno_dance", "No... no... my feet...\nthey're DANCING!\n<k>",
    "sl4_dazz_upstaged", "The crowd's not even looking\nat me! Nobody upstages ME!\nHmph!\n<k>",
    "sl4_dazz_encore", "Encore! Encore! Where's my\nunderstudy? ON STAGE!\n<k>",
    "sl4_dazz_spot", "Spotlight! On ME, darlings!\nAs it should be!\n<k>",
    "sl4_dazz_petal", "Petal Storm!\n<k>",
    "sl4_dazz_aria", "La la LAAAA! Sleep, little\npeople, sleep through my\naria!\n<k>",
    "sl4_act1_00", "Oh! A hat! A hat by the side\nof the road! And it's...\non fire!\n<k>",
    "sl4_act1_jerry", "I am the Hat. I burn because\nI care.\n<k>",
    "sl4_act1_01", "...Who are these two creeping\nout of the wings? That's not\nin the script!\n<k>",
    "sl4_act_end_1", "The curtain falls on Act One!\n<k>",
    "sl4_act2_00", "The Baron's ball! Everyone is\nhere! Everyone except...\nunderstanding.\n<k>",
    "sl4_act2_baron", "Feelings are for PEASANTS!\n<k>",
    "sl4_act2_river", "River, carry me!\n<k>",
    "sl4_act2_bruno", "And who is this enormous\nfellow by the chandelier?\n<k>",
    "sl4_act_end_2", "The curtain falls on Act Two!\n<k>",
    "sl4_act3_torque", "Smoke. Lights. Volcano.\nOn my cue.\n<k>",
    "sl4_act3_00", "The hat! The Baron has the hat!\nOnly a true heart can...\n<k>",
    "sl4_act3_dazz", "Hello, Jazzafrazz! Did you miss\nme? I couldn't bear to watch.\nThis show needs a STAR.\n<k>",
    "sl4_act3_01", "Baron! Who is this... this\nsparkling intruder?!\n<k>",
    "sl4_act3_hayzee", "Keep going, people! The show\nmust go on!\n<k>",
    "sl4_act3_hayzee_wing", "(From the wings, Hayzee:)\nKeep going, people! The show\nmust go on!\n<k>",
    "sl4_act3_grass", "Will no one stop the sparkling\none? Grass! Grass, you saw it\nall! What do you see?\n<k>\n<p>\n...\n<k>",
    "sl4_finale_torque", "Now? Now. NOW.\n<k>",
    "sl4_finale_torque_wing", "(From the wings, Torque:)\nNow? Now. NOW.\n<k>",
    "sl4_finale_00", "Now everyone...\nis UNDERSTOOD!\n<k>",
    "sl4_cancelled", "The house is empty! The curtain\ndrops! The show is cancelled!\n<k>",
    "sl4_h_smash1", "The cardboard sun crashes down!\nHouse -20",
    "sl4_h_smash2", "The ballroom flat topples!\nHouse -20",
    "sl4_h_smash3", "The volcano flat wobbles!\nHouse -20",
    "sl4_h_hush", "Seats are emptying! House -10",
    "sl4_h_spot", "Dazzlina steals the spotlight!\nHouse -15",
    "sl4_h_caught", "He CAUGHT it! House +20",
    "sl4_h_set_ok", "The set's still standing!\nHouse +10",
    "sl4_h_bruno", "Bruno can't help it: he's\ndancing! House +5",
    "sl4_h_sway", "Sway in the Breeze! House +15",
    "sl4_h_appeal", "A bow to the house! House +15",
    "sl4_h_heckler", "Got him! House +5",
    "sl4_h_grass_no", "Grass doesn't do that!\nHouse -2",
    "sl4_h_silence", "A beat of silence.\nThe house holds its breath. +5",
    "sl4_h_finale", "The house is on its feet!",
    "sl4_h_lielow", "Nobody notices the grass.",
    "sl4_h_rapids", "The Rapids! Double!",
    NULL,
};

/* the chapter's setup adds them (superluigi_ch04_aurora.c) */
const char* const* portSlSlamText(void) {
    return kSlamText;
}
