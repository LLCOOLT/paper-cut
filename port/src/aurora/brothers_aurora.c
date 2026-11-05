/* Brothers mode in battle (port/include/port/brothers.h, docs/BROTHERS.md):
 * Luigi as a second Mario unit on Mario's side, with versus_aurora.c's
 * technique for its second Mario: the unit entered after Mario's
 * (Btl_UnitSetup), the game's "Mario" his while he acts (BattleGetMarioPtr),
 * his HUD drawn under the game's, his HP and FP kept for the next battle
 * (BattleSetMarioParamToFieldBattle). His turn's input and his guards are
 * the second controller's (brothers_pc.c through second_player_pc.c).
 *
 * The battle is lost when both brothers are down (Mario's unit kind is what
 * the player's side needs standing: either one keeps the battle going); a
 * brother who is down skips his turns, as a fallen partner does.
 *
 * Nothing here acts unless the mode is on (and no versus fight is). */
#include "port/brothers.h"
#include "port/luigi.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/versus.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/dispdrv.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "statuswindow.h"
#include "system.h"

#include <stdio.h>
#include <string.h>

extern BattleUnitSetup entryunit_mario; /* battle.c */

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static BattleUnitSetup sLuigiSetup;

/* (which unit is Luigi's is brothers_pc.c's, in the save states) */
static BattleUnit* luigiUnit(BattleWork* wp) {
    int i;
    for (i = 0; wp != NULL && i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u != NULL && portBrothersLuigiUnit(u)) {
            return u;
        }
    }
    return NULL;
}

/* Btl_UnitSetup: Luigi beside Mario, the partner a step further back */
static void unitSetupPost(BOOL* r, BattleWork* wp) {
    BattleUnit* mario;
    BattleUnit* party;
    BattleUnit* luigi;
    f32 x, y, z;
    int hp, fp;
    (void)r;
    portBrothersSetLuigiUnit(-1);
    if (!portBrothersOn() || portVersusOn() || (mario = BattleGetMarioPtr(wp)) == NULL) {
        return;
    }
    BtlUnit_GetHomePos(mario, &x, &y, &z);
    sLuigiSetup = entryunit_mario;
    sLuigiSetup.position.x = x - 42.0f;
    sLuigiSetup.position.y = y;
    sLuigiSetup.position.z = z - 14.0f;
    luigi = BtlUnit_Entry(&sLuigiSetup);
    if (luigi == NULL) {
        return;
    }
    BtlUnit_SetHomePos(luigi, x - 42.0f, y, z - 14.0f);
    BtlUnit_SetPos(luigi, x - 42.0f, y, z - 14.0f);
    BtlUnit_EquipItem(luigi, EQUIP_MARIO | EQUIP_RESET, ITEM_NULL);
    BtlUnit_SetParamFromPouch(luigi);
    portBrothersGetStats(&hp, &fp);
    luigi->currentHp = (s16)(hp > 0 ? (hp < luigi->maxHp ? hp : luigi->maxHp) : 1);
    luigi->currentFp = (s16)(fp < luigi->maxFp ? fp : luigi->maxFp);
    portBrothersSetLuigiUnit(luigi->unitId);
    party = BattleGetPartyPtr(wp);
    if (party != NULL) {
        f32 px, py, pz;
        BtlUnit_GetHomePos(party, &px, &py, &pz);
        BtlUnit_SetHomePos(party, px - 45.0f, py, pz);
        BtlUnit_SetPos(party, px - 45.0f, py, pz);
    }
    printf("[brothers] Luigi in the battle (unit %d, HP %d/%d, FP %d/%d)\n", (int)luigi->unitId, luigi->currentHp,
           luigi->maxHp, luigi->currentFp, luigi->maxFp);
    fflush(stdout);
}

/* BattleGetMarioPtr: Luigi's unit while he acts (his menu, his move, its
 * FP, his action commands) */
static int marioPre(BattleUnit** r, BattleWork** wp) {
    BattleUnit* u;
    if (!portBrothersLuigiActs() || (u = luigiUnit(*wp)) == NULL) {
        return 0;
    }
    *r = u;
    return 1;
}

/* BattleSetMarioParamToFieldBattle: Luigi's HP and FP for the next battle
 * (a brother who fell gets up with 1 HP, as one who won it alone keeps
 * Mario on his feet) */
static void paramPost(BattleWork* wp) {
    BattleUnit* luigi = luigiUnit(wp);
    PouchData* pouch = pouchGetPtr();
    if (luigi == NULL) {
        return;
    }
    if (pouch->currentHP <= 0 && luigi->currentHp > 0) {
        pouch->currentHP = 1;
    }
    portBrothersSetStats(luigi->currentHp > 0 ? luigi->currentHp : 1, luigi->currentFp);
    printf("[brothers] after the battle: Luigi HP %d, FP %d\n", luigi->currentHp, luigi->currentFp);
    fflush(stdout);
}

/* statusWinMain: Luigi's HUD under the game's, with his face */
static void luigiHud(CameraId camId, void* param) {
    portLuigiFaceForce(1);
    portStatusWinDispOther(camId, param);
    portLuigiFaceForce(-1);
}

static void statusWinPost(void) {
    BattleUnit* luigi;
    StatusValue v;
    const PouchData* pouch;
    if (gp == NULL || !gp->inBattle || portVersusOn() ||
        (luigi = luigiUnit(_battleWorkPointer)) == NULL) {
        return;
    }
    pouch = pouchGetPtr();
    memset(&v, 0, sizeof(v));
    v.hp = luigi->currentHp;
    v.maxHp = luigi->maxHp;
    v.fp = luigi->currentFp;
    v.maxFp = luigi->maxFp;
    v.ap = pouch->currentSP;
    v.maxAp = pouch->maximumSP;
    portStatusWinSetOther(&v, 100.0f);
    dispEntry(CAMERA_2D, 500.0f, 0, luigiHud, NULL);
}

/* (Bros. attacks: duo moves' Bros. Bounce, Bros. Hammer and Bros. Drop,
 * wired in brothers_pc.c's portBrothersDuoFrame) */

void portBrothersRegister(void) {
    portModHostAddHook(1, kSlot_Btl_UnitSetup, (void*)unitSetupPost);
    portModHostAddHook(0, kSlot_BattleGetMarioPtr, (void*)marioPre);
    portModHostAddHook(1, kSlot_BattleSetMarioParamToFieldBattle, (void*)paramPost);
    portModHostAddHook(1, kSlot_statusWinMain, (void*)statusWinPost);
}
