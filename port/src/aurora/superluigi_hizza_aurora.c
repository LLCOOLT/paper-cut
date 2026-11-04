/* Chapter 2's battles (docs/super_luigi/ch2.md, "Battles"): its field
 * formations, Sergeant Bulkhead and his escort (the mid-boss), and Hizza,
 * the two-headed snake (the boss).
 *
 * Formations: every field enemy of the chapter is named for its formation
 * set (c2f_<set>_<n>); as its battle is set up (a host hook on
 * Btl_UnitSetup) one of the set's formations is rolled, as the game's
 * NpcBattleSelect does, and built from the room's own area's units (found in
 * its battle table by kind), with their own places, drops and stage.
 *
 * Hizza (unit_sl_hizza): one unit, two heads as parts sharing 40 HP. Hiss
 * (c_sl_hizza, the coil and the front head) on the enemy side; Zah
 * (c_sl_hizza_tail, the tail rising out of the floor) BEHIND Luigi, at the
 * stage's far left, reachable only by jumps (and by every-foe moves). They
 * Snap and Sneak Bite, Venom Spit from turn 3, Coil Squeeze once below half;
 * every third turn (second, below half) they telegraph the Pincer: both
 * heads draw back and fix on Luigi, and next turn lunge at him at once from
 * the front and the back. A Superguard (or any miss) and Luigi lunges aside:
 * the heads crash, Hizza takes 8 and is dizzy. The blow that would end it
 * doesn't: Hizza clings on at 1 HP, Luigi braces (a forced command), and its
 * last Pincer comes with "Lunge left!" on screen: the stick left in the
 * window and the heads crash for good; missed, it comes again. Luigi starts
 * the battle fainted, asleep for his first turn. Its models' placement:
 * docs/LUIGI_ANIMS.md, "Hizza" (the snouts meet 38 left of Hiss's origin and
 * 45 right of Zah's, 17 up, at frame 5 of the strike). */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/mod_host.h"
#include "port/mods.h"
#include "port/hook_list.h"
#include "battle/battle.h"
#include "battle/battle_camera.h"
#include "battle/battle_database_common.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_event_subset.h"
#include "battle/battle_pad.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "data/mapdata.h"
#include "driver/npcdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_eff.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
 * The field formations
 * ====================================================================== */

#define K_GOOMBA UNIT_GOOMBA
#define K_PUFF UNIT_ICE_PUFF
#define K_PIRANHA UNIT_FROST_PIRANHA
#define K_EMBER UNIT_EMBER
#define K_BUZZY UNIT_PARABUZZY
#define K_BULKY UNIT_BULKY_BOB_OMB
#define K_BLASTER UNIT_BILL_BLASTER

typedef struct {
    const char* set;  /* the enemies' name: c2f_<set>_<n> */
    int weight;
    short kinds[5];   /* 0-ended */
} Formation;

/* ch2.md, "Formations": each field enemy rolls one of its set's */
static const Formation kForms[] = {
    {"goomba", 10, {K_GOOMBA, K_GOOMBA, 0}},
    {"goomba", 10, {K_GOOMBA, K_PUFF, 0}},
    {"puff", 10, {K_PUFF, K_PUFF, 0}},
    {"piranha", 10, {K_PIRANHA, K_PUFF, 0}},
    {"puff22", 10, {K_PUFF, K_PUFF, K_PUFF, 0}},
    {"puff22", 10, {K_PUFF, K_PUFF, K_GOOMBA, 0}},
    {"ember21", 10, {K_EMBER, K_EMBER, 0}},
    {"ember22", 10, {K_EMBER, K_BUZZY, 0}},
    {"buzzy22", 10, {K_BUZZY, K_BUZZY, 0}},
    {"buzzy26", 10, {K_BUZZY, K_BUZZY, K_EMBER, 0}},
    {"ember26", 10, {K_EMBER, K_EMBER, K_EMBER, 0}},
    {"ember27", 10, {K_EMBER, K_EMBER, K_BUZZY, 0}},
    {"buzzy30", 10, {K_BLASTER, K_BUZZY, K_BUZZY, 0}},
    {"guard", 10, {K_BULKY, K_BULKY, 0}},
    {NULL, 0, {0}},
};

/* A kind's unit as the room's area has it: its kind, its place's height, its drops */
typedef struct {
    BattleUnitKind* kind;
    f32 y;
    ItemDropData* drops;
    struct BattleStageData* stage;
} AreaUnit;

static int findUnit(int type, AreaUnit* out) {
    AreaData* area = gp != NULL ? areaDataPtr(gp->currentAreaName) : NULL;
    NpcSetupBattleInfo* s;
    if (area == NULL || area->btlSetupTbl == NULL) {
        return 0;
    }
    for (s = (NpcSetupBattleInfo*)area->btlSetupTbl; s->name != NULL || s->select != NULL; s++) {
        NpcBattleSelect* sel;
        for (sel = s->select; sel != NULL && sel->weight != 0; sel++) {
            BattleGroupSetup* g = sel->groupSetup;
            int i;
            for (i = 0; g != NULL && i < g->numEnemies; i++) {
                BattleUnitSetup* u = &g->enemyData[i];
                if (u->unitKindParams != NULL && (int)u->unitKindParams->unitType == type) {
                    out->kind = u->unitKindParams;
                    out->y = u->position.y;
                    out->drops = u->itemDropTable;
                    out->stage = sel->stageData;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static BattleUnitSetup sFormUnits[5];
static BattleGroupSetup sFormGroup;

/* the enemy's set: "c2f_goomba_1" -> "goomba" */
static int setOf(const char* npc, char* out, size_t size) {
    const char* end;
    if (npc != NULL && strncmp(npc, "c2_hg", 5) == 0) {
        snprintf(out, size, "guard"); /* a Hissguard whose bride ran */
        return 1;
    }
    if (npc == NULL || strncmp(npc, "c2f_", 4) != 0) {
        return 0;
    }
    end = strchr(npc + 4, '_'); /* the set's name has no '_': "c2f_piranha_1_s0" is a Piranha's bite */
    if (end == NULL) {
        end = npc + strlen(npc);
    }
    snprintf(out, size, "%.*s", (int)(end - (npc + 4)), npc + 4);
    return 1;
}

static int buildFormation(BattleWork* wp, const char* npc) {
    char set[24];
    const Formation* f;
    const Formation* pick = NULL;
    int total = 0, roll, i, n = 0;
    if (!setOf(npc, set, sizeof(set))) {
        return 0;
    }
    for (f = kForms; f->set != NULL; f++) {
        if (strcmp(f->set, set) == 0) {
            total += f->weight;
        }
    }
    if (total == 0) {
        return 0;
    }
    roll = rand() % total;
    for (f = kForms; f->set != NULL; f++) {
        if (strcmp(f->set, set) == 0) {
            if (roll < f->weight) {
                pick = f;
                break;
            }
            roll -= f->weight;
        }
    }
    if (pick == NULL) {
        return 0;
    }
    memset(sFormUnits, 0, sizeof(sFormUnits));
    for (i = 0; i < 5 && pick->kinds[i] != 0; i++) {
        AreaUnit a;
        if (!findUnit(pick->kinds[i], &a)) {
            printf("[superluigi] chapter 2: no unit of kind %d in %s's battles\n", pick->kinds[i], gp->currentAreaName);
            continue;
        }
        sFormUnits[n].unitKindParams = a.kind;
        sFormUnits[n].alliance = 1;
        sFormUnits[n].attackPhase = BATTLE_PHASE_ENEMY;
        sFormUnits[n].position.x = 50.0f + 40.0f * (f32)n;
        sFormUnits[n].position.y = a.y;
        sFormUnits[n].position.z = (n & 1) ? 10.0f : -10.0f;
        sFormUnits[n].itemDropTable = a.drops;
        n++;
    }
    if (n == 0) {
        return 0;
    }
    sFormGroup = *wp->fbatInfo->setup->groupSetup;
    sFormGroup.numEnemies = n;
    sFormGroup.enemyData = sFormUnits;
    wp->fbatInfo->setup->groupSetup = &sFormGroup;
    printf("[superluigi] chapter 2: %s's battle, %d foe(s) of set %s\n", npc, n, set);
    fflush(stdout);
    return 1;
}

/* ======================================================================
 * Hizza
 * ====================================================================== */

#define HISS 1
#define ZAH 2

enum {
    UW_TURN,     /* its turns so far */
    UW_PHASE,    /* 1, then 2 below half */
    UW_PINCER,   /* 1: the Pincer is telegraphed, it strikes next turn */
    UW_FINAL,    /* 1: clinging on at 1 HP, the last Pincer to come; 2 done */
    UW_ANGRY,    /* 1 after a crash in phase 2: the heads may bite each other */
    UW_BICKER,   /* the bicker line's turn */
    UW_MUSHROOM, /* the brides threw their Mushroom */
    UW_SQUEEZED, /* Coil Squeeze done */
    UW_DIZZY,    /* 1 while dazed after a crash */
};

static PoseTableEntry kHissPoses[] = {
    {1, "S_1"}, {2, "S_1"}, {9, "S_1"}, {3, "X_2"}, {5, "X_2"}, {4, "D_1"}, {27, "D_4"},
    {28, "S_1"}, {29, "S_1"}, {30, "S_1"}, {31, "S_1"}, {39, "D_1"}, {69, "S_1"},
    {BTL_POSE_NORMAL, "S_1"}, {BTL_POSE_DAMAGE, "D_1"}, {BTL_POSE_WALK, "W_1"}, {BTL_POSE_RUN, "R_1"},
    {BTL_POSE_TALK, "T_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"},
};
static PoseTableEntry kZahPoses[] = {
    {1, "S_1"}, {2, "S_1"}, {9, "S_1"}, {3, "X_2"}, {5, "X_2"}, {4, "D_1"}, {27, "D_4"},
    {28, "S_1"}, {29, "S_1"}, {30, "S_1"}, {31, "S_1"}, {39, "D_1"}, {69, "S_1"},
    {BTL_POSE_NORMAL, "S_1"}, {BTL_POSE_DAMAGE, "D_1"}, {BTL_POSE_WALK, "S_1"}, {BTL_POSE_RUN, "S_1"},
    {BTL_POSE_TALK, "T_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"},
};

static BattleUnitDefense kHeadDef = {{0, -1, 0, 0, 0}}; /* fire: +1 */
static BattleUnitDefenseAttr kNoAttr = {{0, 0, 0, 0, 0}};
static StatusVulnerability kHizzaRegist = {0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

extern EvtWord hizza_init[];
static BattleUnitKindPart kHizzaParts[2];
static BattleDataEntry kHizzaData[3];
static BattleUnitKind kHizza;
static BattleUnitSetup kHizzaSetup[1];
static BattleGroupSetup kHizzaGroup;

static BattleWeapon kSnap, kSneak, kVenom, kSqueeze, kPincer, kCrash, kBite, kFaint;

static void weapon(BattleWeapon* w, int dmg, int jumpLike, int weights) {
    memset(w, 0, sizeof(*w));
    w->baseAccuracy = 100;
    w->reserved_14 = 1.0f;
    w->stylishCommandMultiplier = 1;
    w->reserved_19 = 1;
    w->bingoSlotChance = 1;
    w->reserved_1B = 1;
    w->hpDamageFn = dmg > 0 ? weaponGetPowerDefault : NULL;
    w->hpDamageWork[0] = dmg;
    w->targetClassFlags = TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT | TARGET_CLASS_NO_OWN_ALLIANCE |
                          TARGET_CLASS_NO_SELF | TARGET_CLASS_MAIN_PARTS | TARGET_CLASS_SINGLE;
    w->targetPropertyFlags = (jumpLike ? TARGET_PROP_JUMPLIKE : 0) | TARGET_PROP_START_OPPOSING_SIDE;
    w->acLevel = 3;
    w->reserved_6F = 2;
    w->special = SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE;
    w->targetWeightFlags = weights | TARGET_WEIGHT_RANDOM;
    w->superguardsAllowed = 1;
    w->stageEffectRates[3] = 100;
}

static void hizzaMake(void) {
    static int made;
    BattleUnitKindPart* p;
    if (made) {
        return;
    }
    made = 1;
    weapon(&kSnap, 3, 0, TARGET_WEIGHT_PREFER_FRONT);
    weapon(&kSneak, 3, 0, TARGET_WEIGHT_PREFER_BACK);
    weapon(&kVenom, 2, 0, 0);
    kVenom.poisonChance = 100, kVenom.poisonTime = 3, kVenom.poisonStrength = 1;
    kVenom.superguardsAllowed = 0;
    weapon(&kSqueeze, 2, 0, 0);
    kSqueeze.superguardsAllowed = 0;
    weapon(&kPincer, 3, 0, TARGET_WEIGHT_PREFER_MARIO);
    weapon(&kCrash, 8, 0, 0);
    kCrash.targetClassFlags = TARGET_CLASS_ONLY_SELF | TARGET_CLASS_MAIN_PARTS | TARGET_CLASS_SINGLE;
    kCrash.special |= SPECIAL_UNGUARDABLE;
    kCrash.dizzyChance = 100, kCrash.dizzyTime = 1;
    kCrash.superguardsAllowed = 0;
    kBite = kCrash;
    kBite.hpDamageWork[0] = 3;
    kBite.dizzyChance = 0;
    weapon(&kFaint, 0, 0, TARGET_WEIGHT_PREFER_MARIO);
    kFaint.special |= SPECIAL_UNGUARDABLE;
    kFaint.superguardsAllowed = 0;
    kFaint.sleepChance = 100, kFaint.sleepTime = 1;

    p = &kHizzaParts[0];
    memset(kHizzaParts, 0, sizeof(kHizzaParts));
    p->partNum = HISS;
    p->partName = "sl_un_hiss";
    p->modelName = "c_sl_hizza";
    p->hitBaseOffset = (Vec){-26.0f, 38.0f, 0.0f};
    p->hitCursorBaseOffset = (Vec){-26.0f, 58.0f, 0.0f};
    p->shadowSize = 60;
    p->hitHeight = 30;
    p->baseAlpha = 255;
    p->defense = &kHeadDef;
    p->defenseAttr = &kNoAttr;
    p->attributes = PART_ATTR_MAIN | PART_ATTR_GUARD_COVER;
    p->poseTable = kHissPoses;
    p = &kHizzaParts[1];
    p->partNum = ZAH;
    p->partName = "sl_un_zah";
    p->modelName = "c_sl_hizza_tail";
    p->hitBaseOffset = (Vec){20.0f, 32.0f, 0.0f};
    p->hitCursorBaseOffset = (Vec){20.0f, 52.0f, 0.0f};
    p->shadowSize = 30;
    p->hitHeight = 26;
    p->baseAlpha = 255;
    p->defense = &kHeadDef;
    p->defenseAttr = &kNoAttr;
    p->attributes = PART_ATTR_WORLD_POSITION | PART_ATTR_HAMMERLIKE_CANNOT_TARGET | PART_ATTR_SHELL_TOSSLIKE_CANNOT_TARGET |
                    PART_ATTR_IMPORTANT;
    p->poseTable = kZahPoses;

    kHizzaData[0].tag = 48, kHizzaData[0].data = btldefaultevt_Dummy;
    kHizzaData[1].tag = 49;
    kHizzaData[2].tag = 0, kHizzaData[2].data = NULL;

    memset(&kHizza, 0, sizeof(kHizza));
    kHizza.unitType = UNIT_HOOKTAIL;
    kHizza.name = "sl_un_hizza";
    kHizza.maxHp = 40;
    kHizza.dangerHP = 10;
    kHizza.perilHP = 5;
    kHizza.level = 30;
    kHizza.bonusExp = 30;
    kHizza.bonusCoin = 30;
    kHizza.bonusCoinRate = 100;
    kHizza.runRate = 0;
    kHizza.pbSoftCap = 9999;
    kHizza.width = 80;
    kHizza.height = 70;
    kHizza.centerOffset = (Vec){0.0f, 35.0f, 0.0f};
    kHizza.hpGaugeOffset[0] = 0;
    kHizza.hpGaugeOffset[1] = 10;
    kHizza.talkTogeBaseOffset = (Vec){-30.0f, 70.0f, 0.0f};
    kHizza.heldItemBaseOffset = (Vec){0.0f, 70.0f, 0.0f};
    kHizza.burnFlameOffset = (Vec){0.0f, 40.0f, 0.0f};
    kHizza.cutWidth = 80.0f;
    kHizza.cutHeight = 70.0f;
    kHizza.turnOrder = 10;
    kHizza.starPointDispOffset = (Vec){0.0f, 80.0f, 0.0f};
    kHizza.damageSfxName = "SFX_BOSS_GNB_DAMAGED1";
    kHizza.fireDamageSfxName = "SFX_BTL_DAMAGE_FIRE1";
    kHizza.iceDamageSfxName = "SFX_BTL_DAMAGE_ICE1";
    kHizza.explosionDamageSfxName = "SFX_BTL_DAMAGE_BIRIBIRI1";
    kHizza.attributeFlags = UNIT_ATTR_NO_KNOCKBACK_POSE | UNIT_ATTR_NO_HAMMER_KNOCKBACK;
    kHizza.statusVulnerability = &kHizzaRegist;
    kHizza.numParts = 2;
    kHizza.parts = kHizzaParts;
    kHizza.initEventCode = hizza_init;
    kHizza.dataTable = kHizzaData;

    memset(kHizzaSetup, 0, sizeof(kHizzaSetup));
    kHizzaSetup[0].unitKindParams = &kHizza;
    kHizzaSetup[0].alliance = 1;
    kHizzaSetup[0].attackPhase = BATTLE_PHASE_ENEMY;
    kHizzaSetup[0].position = (Vec){100.0f, 0.0f, 0.0f};
    kHizzaGroup.numEnemies = 1;
    kHizzaGroup.enemyData = kHizzaSetup;
    kHizzaGroup.noItemWeight = 100;
}

/* in: frames: out: 1 if the stick went left within them (the last Pincer's "Lunge left!") */
static USER_FUNC(hizza_lunge_window) {
    static int frames, got;
    if (isFirstCall) {
        frames = evtGetValue(event, event->args[0]);
        got = 0;
    }
    if (BattlePadCheckNow(BTL_PAD_STICK_LEFT) || (frames == 20 && getenv("TTYD_SL_C2_LUNGE") != NULL)) {
        got = 1; /* ($TTYD_SL_C2_LUNGE: the lunge made for a test) */
    }
    if (--frames > 0 && !got) {
        return EVT_RETURN_BLOCK;
    }
    evtSetValue(event, event->args[1], got);
    return EVT_RETURN_DONE;
}

/* out: 1 if Jerry is the partner in this battle (his opening line), else 0 */
static USER_FUNC(hizza_jerry_out) {
    BattleUnit* p = BattleGetPartnerPtr(_battleWorkPointer, BattleGetMarioPtr(_battleWorkPointer));
    (void)isFirstCall;
    evtSetValue(event, event->args[0], p != NULL && p->currentKind == UNIT_BOBBERY);
    return EVT_RETURN_DONE;
}

/* $TTYD_SL_C2_HIZZA_HP: Hizza's HP at the start (tests: the last Pincer straight away) */
static USER_FUNC(hizza_test_hp) {
    const char* v = getenv("TTYD_SL_C2_HIZZA_HP");
    (void)isFirstCall;
    evtSetValue(event, event->args[0], v != NULL ? atoi(v) : 0);
    return EVT_RETURN_DONE;
}

static USER_FUNC(hizza_force) {
    const int on = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    portSlForceCommand(0, on ? PORT_SL_ACT_TACTICS : -1, NULL, on ? "sl_c2_bt_brace" : NULL);
    return EVT_RETURN_DONE;
}

EVT_BEGIN(hizza_wait)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 2)
    RETURN
EVT_END()

/* a head's hurt: the part that was hit plays its hurt */
EVT_BEGIN(hizza_damage)
    CALL(btlevtcmd_GetDamagePartsId, BTL_UNIT_SELF, LW(11))
    IF_INT_EQUAL(LW(11), -1)
        SET_VALUE(LW(11), 1)
    END_IF
    SET_VALUE(LW(10), -2)
    CALL_CHILD(btldefaultevt_Damage)
    RETURN
EVT_END()

/* Zah's home: the far left of the stage, behind Luigi and his partner */
EVT_BEGIN(hizza_zah_home)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_MARIO, LW(0), LW(1), LW(2))
    SUBTRACT(LW(0), 110)
    SET_VALUE(LW(2), -20)
    CALL(btlevtcmd_SetPartsPos, BTL_UNIT_SELF, ZAH, LW(0), 0, LW(2))
    RETURN
EVT_END()

/* Zah under the floor and out again at (LW(0), LW(2)) */
EVT_BEGIN(hizza_zah_move)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("Q_1"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_GNB_NECK_UP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(20)
    CALL(btlevtcmd_SetPartsPos, BTL_UNIT_SELF, ZAH, LW(0), 0, LW(2))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("U_2"))
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), 0, LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_OFF_WALL_BREAK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(24)
    RETURN
EVT_END()

/* Hiss's coil slides to x = LW(0) */
EVT_BEGIN(hizza_hiss_to)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("W_1"))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(5.0f))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(5), LW(6), LW(7))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(6), LW(7), 0, -1, 0)
    RETURN
EVT_END()

EVT_BEGIN(hizza_go_home)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL_CHILD(hizza_hiss_to)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_GetPartsPos, BTL_UNIT_SELF, ZAH, LW(5), LW(6), LW(7))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_MARIO, LW(0), LW(1), LW(2))
    SUBTRACT(LW(0), 110)
    IF_INT_NOT_EQUAL(LW(5), LW(0))
        SET_VALUE(LW(2), -20)
        CALL_CHILD(hizza_zah_move)
    END_IF
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, HISS)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, ZAH)
    RETURN
EVT_END()

/* one guarded bite at LW(3) with weapon LW(12) (the game's guard window) */
EVT_BEGIN(hizza_bite_hit)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
    SWITCH(LW(5))
        CASE_EQUAL(3)
        CALL(btlevtcmd_StartAvoid, LW(3), 38)
        CASE_EQUAL(6)
        CALL(btlevtcmd_StartAvoid, LW(3), 39)
        CASE_EQUAL(2)
        CALL(btlevtcmd_StartAvoid, LW(3), 40)
        CASE_DEFAULT
        CALL(btlevtcmd_ResultACDefence, LW(3), LW(12))
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
    END_SWITCH
    RETURN
EVT_END()

/* Hiss's Snap: one bite at the front character */
EVT_BEGIN(hizza_snap)
    SET_VALUE(LW(12), PTR(&kSnap))
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(12), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    ADD(LW(0), 38)
    CALL_CHILD(hizza_hiss_to)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("A_2A"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_GNB_ROAR1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(24)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("A_2B"))
    WAIT_FRAMES(3)
    CALL_CHILD(hizza_bite_hit)
    WAIT_FRAMES(21)
    CALL_CHILD(hizza_go_home)
    RETURN
EVT_END()

/* Zah's Sneak Bite: from behind, at the back character */
EVT_BEGIN(hizza_sneak)
    SET_VALUE(LW(12), PTR(&kSneak))
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(12), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, LW(3), LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    SUBTRACT(LW(0), 45)
    CALL_CHILD(hizza_zah_move)
    /* the sway that gives it away, then the bite */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("A_3A"))
    WAIT_FRAMES(40)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("A_3B"))
    WAIT_FRAMES(3)
    CALL_CHILD(hizza_bite_hit)
    WAIT_FRAMES(21)
    CALL_CHILD(hizza_go_home)
    RETURN
EVT_END()

/* Venom Spit: either head, 2 and Poison */
EVT_BEGIN(hizza_venom)
    SET_VALUE(LW(12), PTR(&kVenom))
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(12), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(evt_sub_random, 1, LW(9))
    IF_INT_EQUAL(LW(9), 0)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("V_1A"))
        WAIT_FRAMES(24)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("V_1B"))
    ELSE
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("V_2A"))
        WAIT_FRAMES(24)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("V_2B"))
    END_IF
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_PAKKUN_POISON1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(14)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(0.8f), 0, 0, 0, 0, 0, 0, 0)
    CALL_CHILD(hizza_bite_hit)
    WAIT_FRAMES(20)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, HISS)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, ZAH)
    RETURN
EVT_END()

/* Coil Squeeze (once below half): the body tightens on both of them */
EVT_BEGIN(hizza_squeeze)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_SQUEEZED, 1)
    CALL(evt_msg_print, 0, STRING("sl_c2_bt_squeeze"), 0, 0)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("C_1A"))
    WAIT_FRAMES(30)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("C_1B"))
    CALL(evt_btl_camera_shake_h, 1, 4, 0, 30, 13)
    SET_VALUE(LW(12), PTR(&kSqueeze))
    SET_VALUE(LW(3), BTL_UNIT_MARIO)
    SET_VALUE(LW(4), 1)
    CALL_CHILD(hizza_bite_hit)
    CALL(btlevtcmd_GetUnitId, BTL_UNIT_PARTNER, LW(3))
    IF_INT_NOT_EQUAL(LW(3), -1)
        SET_VALUE(LW(3), BTL_UNIT_PARTNER)
        CALL_CHILD(hizza_bite_hit)
    END_IF
    WAIT_FRAMES(30)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, HISS)
    RETURN
EVT_END()

/* The crash: the heads meet where Luigi was. Hizza takes 8 and is dazed (the brides cheer). */
EVT_BEGIN(hizza_crash)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("X_1"))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("X_1"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_GNB_DAMAGED1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_OFF_WALL_BREAK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(evt_btl_camera_shake_h, 1, 10, 0, 30, 13)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_MARIO, LW(0), LW(1), LW(2))
    ADD(LW(1), 17)
    CALL(evt_eff, 0, STRING("hit"), 1, LW(0), LW(1), LW(2), FLOAT(3.0f), 4, 0, 0, 0, 0, 0, 0)
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(1.5f), 0, 0, 0, 0, 0, 0, 0)
    WAIT_FRAMES(20)
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, BTL_UNIT_SELF, HISS, PTR(&kCrash), 256, LW(5))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_DIZZY, 1)
    /* dazed on the floor: Zah lies right behind Luigi, in the hammer's reach */
    CALL(btlevtcmd_OffPartsAttribute, BTL_UNIT_SELF, ZAH, PART_ATTR_HAMMERLIKE_CANNOT_TARGET)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("X_2"))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("X_2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_bt_crash"), 0, 0)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_PHASE, LW(0))
    IF_INT_EQUAL(LW(0), 2)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_ANGRY, 1)
    END_IF
    RETURN
EVT_END()

/* Luigi lunges aside, toward the front of the stage, and back */
EVT_BEGIN(hizza_luigi_dodge)
    CALL(btlevtcmd_GetPos, BTL_UNIT_MARIO, LW(0), LW(1), LW(2))
    ADD(LW(2), 45)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_MARIO, FLOAT(0.7f))
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_MARIO, 1, 49)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_MARIO, LW(0), LW(1), LW(2), 14, -1)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_MARIO, 1, BTL_POSE_STAY_DEFAULT)
    RETURN
EVT_END()

EVT_BEGIN(hizza_luigi_back)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_MARIO, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_MARIO, FLOAT(0.7f))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_MARIO, LW(0), LW(1), LW(2), 18, -1)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_MARIO, 1, BTL_POSE_STAY_DEFAULT)
    RETURN
EVT_END()

/* The two heads take their places on either side of Luigi and fix on him (the telegraph) */
EVT_BEGIN(hizza_pincer_set)
    CALL(evt_btl_camera_set_mode, 0, 3)
    CALL(evt_btl_camera_set_zoom, 0, -120)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_MARIO, LW(0), LW(1), LW(2))
    ADD(LW(0), 38)
    CALL_CHILD(hizza_hiss_to)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_MARIO, LW(0), LW(1), LW(2))
    SUBTRACT(LW(0), 45)
    CALL_CHILD(hizza_zah_move)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("A_1A"))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("A_1A"))
    WAIT_FRAMES(36)
    RETURN
EVT_END()

/* The Pincer's strike: both at once, one moment to Superguard */
EVT_BEGIN(hizza_pincer)
    SET_VALUE(LW(12), PTR(&kPincer))
    SET_VALUE(LW(3), BTL_UNIT_MARIO)
    SET_VALUE(LW(4), 1)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL_CHILD(hizza_pincer_set)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("A_1B"))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("A_1B"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_GNB_ROAR1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(5)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
    IF_INT_NOT_EQUAL(LW(5), 1)
        /* a miss of any kind (Dodgy, ...) is the dodge too */
        SET_VALUE(LW(6), 5)
    ELSE
        CALL(btlevtcmd_ResultACDefence, LW(3), LW(12))
        CALL(btlevtcmd_GetResultACDefence, LW(6))
    END_IF
    IF_INT_EQUAL(LW(6), 5)
        CALL_CHILD(hizza_luigi_dodge)
        CALL_CHILD(hizza_crash)
        CALL_CHILD(hizza_luigi_back)
        WAIT_FRAMES(30)
    ELSE
        /* both bites land (a Guard takes 1 off each) */
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 0, LW(5))
        WAIT_FRAMES(10)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("A_1C"))
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("A_1C"))
        WAIT_FRAMES(30)
    END_IF
    CALL_CHILD(hizza_go_home)
    RETURN
EVT_END()

/* The last Pincer: no telegraph, "Lunge left!" on screen and the stick left in the window */
EVT_BEGIN(hizza_final_pincer)
    SET_VALUE(LW(12), PTR(&kPincer))
    SET_VALUE(LW(3), BTL_UNIT_MARIO)
    SET_VALUE(LW(4), 1)
    CALL(evt_msg_print, 2, STRING("sl_c2_bt_final"), 0, BTL_UNIT_SELF)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL_CHILD(hizza_pincer_set)
    CALL(evt_sl_prompt, STRING("sl_c2_bt_lunge"), 1400)
    WAIT_FRAMES(20)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("A_1B"))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("A_1B"))
    CALL(hizza_lunge_window, 40, LW(6))
    CALL(evt_sl_prompt, 0, 0)
    IF_INT_EQUAL(LW(6), 1)
        /* he jumps aside at the last second: the heads crash, the beast stiffens and bursts */
        CALL(btlevtcmd_GetPos, BTL_UNIT_MARIO, LW(0), LW(1), LW(2))
        SUBTRACT(LW(0), 20)
        ADD(LW(2), 40)
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_MARIO, LW(0), LW(1), LW(2), 14, -1)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_FINAL, 2)
        CALL(hizza_force, 0)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("X_1"))
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("X_1"))
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_GNB_DAMAGED1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(evt_btl_camera_shake_h, 1, 14, 0, 40, 13)
        WAIT_FRAMES(30)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("D_4"))
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("D_4"))
        WAIT_FRAMES(40)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        SET_VALUE(LW(7), 0)
        LOOP(6)
            CALL(evt_snd_sfxon, STRING("SFX_OFF_WALL_BREAK1"), 0)
            CALL(evt_eff, 0, STRING("bomb"), 0, LW(0), LW(1), LW(2), FLOAT(2.0f), 0, 0, 0, 0, 0, 0, 0)
            SUBTRACT(LW(0), 40)
            ADD(LW(1), 8)
            WAIT_FRAMES(10)
        END_LOOP
        CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(4.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(btlevtcmd_SetHp, BTL_UNIT_SELF, 0)
        CALL(btlevtcmd_OnAttribute, BTL_UNIT_SELF, UNIT_ATTR_HIDDEN)
        CALL(btlevtcmd_OnPartsAttribute, BTL_UNIT_SELF, ZAH, PART_ATTR_HIDDEN)
        CALL(btlevtcmd_OnPartsAttribute, BTL_UNIT_SELF, HISS, PART_ATTR_HIDDEN)
        WAIT_FRAMES(30)
        CALL_CHILD(hizza_luigi_back)
        /* its coins and Star Points, as a foe's defeat banks them (it goes without subsetevt_dead_core) */
        CALL(btlevtcmd_GetCoin, BTL_UNIT_SELF, LW(0))
        CALL(btlevtcmd_StoreCoin, LW(0))
        CALL(btlevtcmd_GetExp, BTL_UNIT_SELF, LW(0))
        CALL(btlevtcmd_StoreExp, LW(0))
        CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 1)
        RETURN
    END_IF
    /* missed: he takes 3, and it comes again */
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("A_1C"))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("A_1C"))
    WAIT_FRAMES(30)
    CALL_CHILD(hizza_go_home)
    RETURN
EVT_END()

/* The blow that would end it: Hizza clings on at 1 HP (the last Pincer comes next) */
EVT_BEGIN(hizza_dead)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_FINAL, LW(0))
    IF_INT_LESS(LW(0), 2)
        CALL(btlevtcmd_SetHp, BTL_UNIT_SELF, 1)
        IF_INT_EQUAL(LW(0), 0)
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_FINAL, 1)
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PINCER, 0)
            CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("P_1"))
            CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("P_1"))
            CALL(evt_btl_camera_shake_h, 1, 6, 0, 40, 13)
            CALL(evt_msg_print, 0, STRING("sl_c2_bt_cling"), 0, 0)
            CALL(hizza_force, 1)
        END_IF
        CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, HISS)
        CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, ZAH)
        RETURN
    END_IF
    CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

/* after each of its turns in phase 1: the heads bicker */
EVT_BEGIN(hizza_bicker)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_BICKER, LW(0))
    CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, UW_BICKER, 1)
    MODULO(LW(0), 4)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("B_1"))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("B_1"))
    SWITCH(LW(0))
        CASE_EQUAL(0)
        CALL(evt_msg_print, 2, STRING("sl_c2_hizza_01"), 0, BTL_UNIT_SELF)
        CASE_EQUAL(1)
        CALL(evt_msg_print, 2, STRING("sl_c2_hizza_02"), 0, BTL_UNIT_SELF)
        CASE_EQUAL(2)
        CALL(evt_msg_print, 2, STRING("sl_c2_hizza_03"), 0, BTL_UNIT_SELF)
        CASE_DEFAULT
        CALL(evt_msg_print, 2, STRING("sl_c2_hizza_04"), 0, BTL_UNIT_SELF)
    END_SWITCH
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, HISS)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, ZAH)
    RETURN
EVT_END()

EVT_BEGIN(hizza_attack)
    /* the brides' Mushroom, the first time Luigi's in Danger */
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_MUSHROOM, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_GetHp, BTL_UNIT_MARIO, LW(1))
        IF_INT_LESS_OR_EQUAL(LW(1), 5)
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_MUSHROOM, 1)
            CALL(evt_msg_print, 0, STRING("sl_c2_bt_brides_mush"), 0, 0)
            CALL(btlevtcmd_RecoverHp, BTL_UNIT_MARIO, 1, 5)
            CALL(btlevtcmd_GetPos, BTL_UNIT_MARIO, LW(0), LW(1), LW(2))
            ADD(LW(1), 30)
            CALL(evt_eff, 0, STRING("recovery"), 0, LW(0), LW(1), LW(2), 5, 0, 0, 0, 0, 0, 0, 0)
            CALL(evt_snd_sfxon, STRING("SFX_HP_RECOVER_MID1"), 0)
            WAIT_FRAMES(40)
        END_IF
    END_IF
    /* back on its feet after a daze: Zah out of the hammer's reach again */
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_DIZZY, 0)
    CALL(btlevtcmd_OnPartsAttribute, BTL_UNIT_SELF, ZAH, PART_ATTR_HAMMERLIKE_CANNOT_TARGET)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_FINAL, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL_CHILD(hizza_final_pincer)
        GOTO(99)
    END_IF
    /* phase 2 from half its HP: it rears up in a rage, and squeezes */
    CALL(btlevtcmd_GetHp, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_PHASE, LW(1))
    IF_INT_LESS_OR_EQUAL(LW(0), 20)
        IF_INT_EQUAL(LW(1), 1)
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PHASE, 2)
            CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("P_1"))
            CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("P_1"))
            CALL(evt_btl_camera_shake_h, 1, 6, 0, 60, 13)
            CALL(evt_msg_print, 0, STRING("sl_c2_bt_rage"), 0, 0)
            CALL(btlevtcmd_AnimeSetPoseTable, BTL_UNIT_SELF, HISS, PTR(kHissPoses))
            CALL_CHILD(hizza_squeeze)
            GOTO(99)
        END_IF
    END_IF
    CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, UW_TURN, 1)
    /* the telegraphed Pincer strikes */
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_PINCER, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PINCER, 0)
        CALL_CHILD(hizza_pincer)
        GOTO(99)
    END_IF
    /* every third turn (second below half): the telegraph */
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TURN, LW(0))
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_PHASE, LW(1))
    SET_VALUE(LW(2), 3)
    IF_INT_EQUAL(LW(1), 2)
        SET_VALUE(LW(2), 2)
    END_IF
    MODULO(LW(0), LW(2))
    IF_INT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PINCER, 1)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("A_1A"))
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("A_1A"))
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_GNB_ROAR1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        WAIT_FRAMES(36)
        CALL(evt_msg_print, 2, STRING("sl_c2_bt_tele_hiss"), 0, BTL_UNIT_SELF)
        CALL(evt_msg_print, 2, STRING("sl_c2_bt_tele_zah"), 0, BTL_UNIT_SELF)
        CALL(evt_msg_print, 0, STRING("sl_c2_bt_tele"), 0, 0)
        GOTO(99)
    END_IF
    /* angry heads may bite each other */
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_ANGRY, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_sub_random, 1, LW(1))
        IF_INT_EQUAL(LW(1), 0)
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_ANGRY, 0)
            CALL(evt_sub_random, 1, LW(1))
            IF_INT_EQUAL(LW(1), 0)
                CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, HISS, STRING("A_2B"))
                CALL(evt_msg_print, 0, STRING("sl_c2_bt_hissbit"), 0, 0)
            ELSE
                CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("A_3B"))
                CALL(evt_msg_print, 0, STRING("sl_c2_bt_zahbit"), 0, 0)
            END_IF
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, BTL_UNIT_SELF, HISS, PTR(&kBite), 256, LW(5))
            GOTO(99)
        END_IF
    END_IF
    /* a bite, or (from turn 3) a spit */
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TURN, LW(0))
    CALL(evt_sub_random, 2, LW(1))
    IF_INT_GREATER_OR_EQUAL(LW(0), 3)
        IF_INT_EQUAL(LW(1), 0)
            CALL_CHILD(hizza_venom)
            GOTO(90)
        END_IF
    END_IF
    MODULO(LW(0), 2)
    IF_INT_EQUAL(LW(0), 1)
        CALL_CHILD(hizza_snap)
    ELSE
        CALL_CHILD(hizza_sneak)
    END_IF
    LABEL(90)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_PHASE, LW(1))
    IF_INT_EQUAL(LW(1), 1)
        CALL_CHILD(hizza_bicker)
    END_IF
    LABEL(99)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* The battle opens with Luigi fainted (asleep for his first turn) and the partner's line */
EVT_BEGIN(hizza_entry)
    CALL_CHILD(hizza_zah_home)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, ZAH, STRING("U_1"))
    WAIT_FRAMES(60)
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, BTL_UNIT_MARIO, 1, PTR(&kFaint), 256, LW(5))
    CALL(evt_msg_print, 0, STRING("sl_c2_bt_fainted"), 0, 0)
    CALL(hizza_jerry_out, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_msg_print, 2, STRING("sl_c2_bt_wake_je"), 0, BTL_UNIT_PARTNER)
    ELSE
        CALL(evt_msg_print, 2, STRING("sl_c2_bt_wake_bl"), 0, BTL_UNIT_PARTNER)
    END_IF
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, HISS)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, ZAH)
    RETURN
EVT_END()

EVT_BEGIN(hizza_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(hizza_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(hizza_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(hizza_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(hizza_attack))
    CALL(btlevtcmd_SetEventEntry, BTL_UNIT_SELF, PTR(hizza_entry))
    CALL(btlevtcmd_SetWalkSound, BTL_UNIT_SELF, STRING("SFX_BOSS_GNB_MOVE1"), STRING("SFX_BOSS_GNB_MOVE1"), 0, 20, 20)
    CALL(btlevtcmd_SetRunSound, BTL_UNIT_SELF, STRING("SFX_BOSS_GNB_MOVE1"), STRING("SFX_BOSS_GNB_MOVE1"), 0, 12, 12)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PHASE, 1)
    CALL(hizza_test_hp, LW(0))
    IF_INT_GREATER(LW(0), 0)
        CALL(btlevtcmd_SetHp, BTL_UNIT_SELF, LW(0))
    END_IF
    CALL_CHILD(hizza_zah_home)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* ======================================================================
 * Sergeant Bulkhead and his escort (the mid-boss)
 * ====================================================================== */

enum { UW_LIT = 0, UW_BTURN = 1 };

static BattleUnitKind kBulkhead, kGuard;
static BattleUnitKindPart kBulkParts[2], kGuardParts[2];
static BattleDataEntry kBulkData[2];
static BattleUnitSetup kBulkSetup[3];
static BattleGroupSetup kBulkGroup;
static BattleWeapon kBump, kGuardBump, kGuardBoom;

extern EvtWord bulk_init[], guard_init[];

/* in: a unit; out: its id on the stage (0 for none), for Light 'Em Up's pick */
static USER_FUNC(bulk_pick_guard) {
    BattleWork* wp = _battleWorkPointer;
    int i, best = -1;
    (void)isFirstCall;
    for (i = 0; i < 64; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u != NULL && u->kindParams == &kGuard && u->currentHp > 0 && u->work[UW_LIT] == 0) {
            best = i;
            break;
        }
    }
    evtSetValue(event, event->args[0], best);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(bomb_hop_home)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_WALK)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    RETURN
EVT_END()

/* Belly Bump: he barrels into the front one (LW(12): the weapon) */
static EVT_BEGIN(bomb_bump)
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(12), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 50)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(5.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 20)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 10, -1)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_HEAVYB_ATTACK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL_CHILD(hizza_bite_hit)
    WAIT_FRAMES(20)
    CALL_CHILD(bomb_hop_home)
    RETURN
EVT_END()

EVT_BEGIN(bulk_attack)
    CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, UW_BTURN, 1)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_BTURN, LW(0))
    MODULO(LW(0), 2)
    IF_INT_EQUAL(LW(0), 1)
        /* Light 'Em Up: he lights one of his lads */
        CALL(bulk_pick_guard, LW(13))
        IF_INT_NOT_EQUAL(LW(13), -1)
            CALL(evt_msg_print, 2, STRING("sl_c2_bt_light"), 0, BTL_UNIT_SELF)
            CALL(btlevtcmd_SetUnitWork, LW(13), UW_LIT, 1)
            CALL(btlevtcmd_GetPos, LW(13), LW(0), LW(1), LW(2))
            ADD(LW(1), 70)
            CALL(evt_eff, 0, STRING("sandars"), 5, LW(0), LW(1), LW(2), 60, 0, 0, 0, 0, 0, 0, 0)
            CALL(evt_snd_sfxon, STRING("SFX_ENM_BOMB_SPARK1"), 0)
            CALL(evt_msg_print, 0, STRING("sl_c2_bt_lit"), 0, 0)
            GOTO(99)
        END_IF
    END_IF
    SET_VALUE(LW(12), PTR(&kBump))
    CALL_CHILD(bomb_bump)
    LABEL(99)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* a lit Hissguard goes off: 4 to Luigi and his partner (guard both), and he's gone */
EVT_BEGIN(guard_boom)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 30)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK4"), 0)
    CALL(evt_eff, STRING(""), STRING("sandars"), 3, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_btl_camera_shake_h, 0, 8, 0, 20, 13)
    SET_VALUE(LW(12), PTR(&kGuardBoom))
    SET_VALUE(LW(3), BTL_UNIT_MARIO)
    SET_VALUE(LW(4), 1)
    CALL_CHILD(hizza_bite_hit)
    CALL(btlevtcmd_GetUnitId, BTL_UNIT_PARTNER, LW(3))
    IF_INT_NOT_EQUAL(LW(3), -1)
        SET_VALUE(LW(3), BTL_UNIT_PARTNER)
        CALL_CHILD(hizza_bite_hit)
    END_IF
    CALL(btlevtcmd_SetHp, BTL_UNIT_SELF, 0)
    /* gone as the game's own Bulky Bob-omb goes (kill flag 1 only stops a unit: he stood there at 0 HP) */
    SET_VALUE(LW(10), BTL_UNIT_SELF)
    CALL_CHILD(subsetevt_dead_core)
    CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 0)
    RETURN
EVT_END()

EVT_BEGIN(guard_attack)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_LIT, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_msg_print, 0, STRING("sl_c2_bt_boom"), 0, 0)
        CALL_CHILD(guard_boom)
        RETURN
    END_IF
    SET_VALUE(LW(12), PTR(&kGuardBump))
    CALL_CHILD(bomb_bump)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

EVT_BEGIN(bomb_wait)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

EVT_BEGIN(bomb_damage)
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    RETURN
EVT_END()

EVT_BEGIN(bulk_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(bomb_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(bulk_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(bomb_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(bulk_attack))
    CALL(btlevtcmd_SetPartsBaseScale, BTL_UNIT_SELF, 1, FLOAT(1.4f), FLOAT(1.4f), FLOAT(1.4f))
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

EVT_BEGIN(guard_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(bomb_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(guard_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(bomb_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(guard_attack))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LIT, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Jerry's Lit Fuse on a lit Hissguard: it goes off at once, on the enemy side */
static EVT_BEGIN(guard_boom_now)
    WAIT_FRAMES(10)
    CALL(evt_msg_print, 2, STRING("sl_c2_bt_fusefuse"), 0, BTL_UNIT_PARTNER)
    CALL(btlevtcmd_GetPos, LW(0), LW(5), LW(6), LW(7))
    ADD(LW(6), 30)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK4"), 0)
    CALL(evt_eff, STRING(""), STRING("sandars"), 3, LW(5), LW(6), LW(7), 0, 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_btl_camera_shake_h, 0, 8, 0, 20, 13)
    RETURN
EVT_END()

static int sGuardBoomTarget;
static USER_FUNC(guard_hit_neighbours) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* lit = BattleGetUnitPtr(wp, sGuardBoomTarget);
    int i;
    (void)event;
    (void)isFirstCall;
    if (lit == NULL) {
        return EVT_RETURN_DONE;
    }
    for (i = 0; i < 64; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u != NULL && u != lit && u->alliance == lit->alliance && u->currentHp > 0 &&
            (u->kindParams == &kGuard || u->kindParams == &kBulkhead) &&
            u->homePosition.x > lit->homePosition.x - 60.0f && u->homePosition.x < lit->homePosition.x + 60.0f) {
            u->currentHp = u->currentHp > 4 ? u->currentHp - 4 : 1;
        }
    }
    lit->currentHp = 0;
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(guard_boom_after)
    CALL_CHILD(guard_boom_now)
    CALL(guard_hit_neighbours)
    SET_VALUE(LW(10), LW(0))
    CALL_CHILD(subsetevt_dead_core)
    CALL(btlevtcmd_KillUnit, LW(0), 0)
    RETURN
EVT_END()

int portSlC2LitGuard(BattleUnit* target) {
    EventEntry* e;
    if (target == NULL || target->kindParams != &kGuard || target->work[UW_LIT] != 1) {
        return 0;
    }
    target->work[UW_LIT] = 2;
    sGuardBoomTarget = target->unitId;
    e = evtEntry(guard_boom_after, 0, 0);
    if (e != NULL) {
        e->lwData[0] = target->unitId;
    }
    return 1;
}

static int bulkMake(void) {
    AreaUnit a;
    static int made;
    if (!findUnit(K_BULKY, &a)) {
        return 0;
    }
    if (!made) {
        made = 1;
        weapon(&kBump, 3, 0, TARGET_WEIGHT_PREFER_FRONT);
        kBump.superguardsAllowed = 1;
        weapon(&kGuardBump, 2, 0, TARGET_WEIGHT_PREFER_FRONT);
        weapon(&kGuardBoom, 4, 0, 0);
        kGuardBoom.element = ELEMENT_EXPLOSION;
        kGuardBoom.superguardsAllowed = 0;
    }
    kGuard = *a.kind;
    memcpy(kGuardParts, a.kind->parts, sizeof(kGuardParts[0]) * (a.kind->numParts > 2 ? 2 : a.kind->numParts));
    kGuard.parts = kGuardParts;
    /* the body alone: the game's part 2 is the fuse flame, hidden by its own init (ours drew a
     * second whole bomb 80 above each) */
    kGuard.numParts = 1;
    kGuard.name = "sl_un_hissguard";
    kGuardParts[0].partName = "sl_un_hissguard";
    kGuard.initEventCode = guard_init;
    kBulkhead = kGuard;
    memcpy(kBulkParts, kGuardParts, sizeof(kBulkParts));
    kBulkhead.parts = kBulkParts;
    kBulkhead.unitType = UNIT_BOB_ULK;
    kBulkhead.name = "sl_un_bulkhead";
    kBulkParts[0].partName = "sl_un_bulkhead";
    {
        size_t size = 0;
        if (portModsGetHostFile("a/c_sl_bulkhead", &size) != NULL) {
            kBulkParts[0].modelName = "c_sl_bulkhead"; /* the sergeant's stripe (a variant of c_heavy) */
            kBulkParts[1].modelName = "c_sl_bulkhead";
        }
    }
    kBulkhead.maxHp = 16;
    kBulkhead.level = 24;
    kBulkhead.bonusExp = 4;
    kBulkhead.initEventCode = bulk_init;
    kBulkData[0].tag = 48, kBulkData[0].data = btldefaultevt_Dummy;
    kBulkData[1].tag = 0, kBulkData[1].data = NULL;
    kBulkhead.dataTable = kBulkData;
    memset(kBulkSetup, 0, sizeof(kBulkSetup));
    kBulkSetup[0].unitKindParams = &kGuard;
    kBulkSetup[0].position = (Vec){40.0f, 0.0f, -10.0f};
    kBulkSetup[1].unitKindParams = &kBulkhead;
    kBulkSetup[1].position = (Vec){95.0f, 0.0f, 0.0f};
    kBulkSetup[2].unitKindParams = &kGuard;
    kBulkSetup[2].position = (Vec){150.0f, 0.0f, 10.0f};
    for (int i = 0; i < 3; i++) {
        kBulkSetup[i].alliance = 1;
        kBulkSetup[i].attackPhase = BATTLE_PHASE_ENEMY;
        kBulkSetup[i].itemDropTable = a.drops;
    }
    kBulkGroup = *(BattleGroupSetup*)&kHizzaGroup;
    kBulkGroup.numEnemies = 3;
    kBulkGroup.enemyData = kBulkSetup;
    kBulkGroup.noItemWeight = 100;
    return 1;
}

/* ======================================================================
 * Arming and the hook
 * ====================================================================== */

static int sArmed; /* 1 Bulkhead's fight, 2 Hizza's */

/* in: 0 off, 1 the next field battle is Bulkhead's, 2 Hizza's */
USER_FUNC(evt_sl_c2_arm) {
    (void)isFirstCall;
    sArmed = evtGetValue(event, event->args[0]);
    return EVT_RETURN_DONE;
}

/* the boss's battle is running (its camera, the partner lines) */
int portSlC2Boss(void) {
    return sArmed == 2 && gp != NULL && gp->inBattle && portSuperLuigiChapter() == 2 &&
           strcmp(gp->currentMapName, "gon_20") == 0;
}

static int unitSetupPre(BOOL* result, BattleWork** wpp) {
    BattleWork* wp = *wpp;
    FieldBattleData* fbat;
    (void)result;
    if (portSuperLuigiChapter() != 2 || wp->fbatInfo == NULL || wp->fbatInfo->setup == NULL) {
        return 0;
    }
    /* armed only in its own room: a Game Over in the fight never got to disarm it, and
     * the chapter played again would have met the boss in its first field battle */
    if ((sArmed == 1 && strcmp(gp->currentMapName, "dou_28") != 0) ||
        (sArmed == 2 && strcmp(gp->currentMapName, "gon_20") != 0)) {
        sArmed = 0;
    }
    if (sArmed == 2) {
        hizzaMake();
        wp->fbatInfo->setup->groupSetup = &kHizzaGroup;
        wp->fbatInfo->setup->bgmName = "BGM_BOSS_STG1_GONBABA1";
        printf("[superluigi] chapter 2: Hizza's fight\n");
        fflush(stdout);
        return 0;
    }
    if (sArmed == 1) {
        hizzaMake();
        if (bulkMake()) {
            wp->fbatInfo->setup->groupSetup = &kBulkGroup;
            wp->fbatInfo->setup->bgmName = "BGM_CHUBOSS_BATTLE1";
            printf("[superluigi] chapter 2: Sergeant Bulkhead's fight\n");
            fflush(stdout);
        }
        return 0;
    }
    fbat = fbatGetPointer();
    /* the enemy fought: a touch sets npc and target, a battle a script starts
     * (evt_npc_battle_start: a Frost Piranha's bite, a Hissguard's charge) only target */
    if (fbat != NULL && (fbat->target != NULL || fbat->npc != NULL)) {
        buildFormation(wp, (fbat->target != NULL ? fbat->target : fbat->npc)->description);
    }
    return 0;
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

/* chapter 2's setup (superluigi_ch02_aurora.c) */
void portSlC2BattleRegister(void) {
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)unitSetupPre);
    hizzaMake();
    kHizzaData[1].data = hizza_dead;
}
