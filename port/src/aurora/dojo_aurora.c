/* The action-command dojo's battle (port/include/port/dojo.h, docs/DOJO.md):
 * a practice fight on the Glitz Pit's ring against one of the Glitz Pit's
 * own fighters, run by the game's own battle code. The map is the mode's own
 * (tou_d0: the ring's files and the Glitz Pit's REL, as the modes' maps in
 * modes_aurora.c), whose script starts the battle with an invisible stand-in
 * NPC as the area's scripts do; the battle's stage, audience and music are
 * the Pit's first match's, its one enemy a copy of the chosen fighter's unit
 * kind that never falls (dojo_pc.c fills its HP back, and lets it attack only
 * as asked) and gives no Star Points or coins.
 *
 * Mario is lent: his pouch as it was is put back when the dojo ends (Run
 * Away, the Modes tab's Leave, a Game Over or the title), so nothing done in
 * the dojo (items used, the "every move" loadout) stays. No autosave meanwhile.
 * Offline only: not during online play or a replay. */
#include "port/dojo.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/modes.h"
#include "port/mods.h"
#include "port/online.h"
#include "port/raid.h"
#include "port/replay.h"
#include "port/companion.h"
#include "battle/battle.h"
#include "battle/battle_monosiri.h"
#include "battle/battle_unit.h"
#include "data/mapdata.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void npcSetupBattleInfo(NpcEntry* npc, NpcSetupBattleInfo* setup); /* npcdrv.c */

#define DOJO_MAP "tou_d0"
#define DOJO_FILES "tou_03" /* the Glitz Pit's ring */
#define DOJO_STAGE_SETUP 1  /* btlsetup_tou_tbl: the first match's stage, audience and music */
#define DUMMY_HP 99

/* The Glitz Pit's fighters (their unit kinds' names, btl_un_*), roughly in
 * the order the Pit's ranking meets them; the English when no game runs. */
static const struct {
    const char* tag;
    const char* english;
} kDummies[] = {
    {"btl_un_kuriboo", "Goomba"},
    {"btl_un_hinnya", "Spinia"},
    {"btl_un_hannya", "Spania"},
    {"btl_un_togezo", "Spiny"},
    {"btl_un_nokonoko_fighter", "KP Koopa"},
    {"btl_un_patapata_fighter", "KP Paratroopa"},
    {"btl_un_sambo", "Pokey"},
    {"btl_un_honenoko", "Dull Bones"},
    {"btl_un_basabasa", "Swooper"},
    {"btl_un_bomhei", "Bob-omb"},
    {"btl_un_borodo", "Bandit"},
    {"btl_un_borodo_king", "Big Bandit"},
    {"btl_un_togedaruma", "Bristle"},
    {"btl_un_chorobon", "Fuzzy"},
    {"btl_un_green_chorobon", "Green Fuzzy"},
    {"btl_un_flower_chorobon", "Flower Fuzzy"},
    {"btl_un_crimson_togemet", "Red Spike Top"},
    {"btl_un_hyper_sinnosuke", "Hyper Bald Cleft"},
    {"btl_un_iron_sinemon", "Iron Cleft"},
    {"btl_un_hammer_bros", "Hammer Bro"},
    {"btl_un_boomerang_bros", "Boomerang Bro"},
    {"btl_un_fire_bros", "Fire Bro"},
    {"btl_un_jyugem", "Lakitu"},
    {"btl_un_kamec_red", "Red Magikoopa"},
    {"btl_un_kamec_white", "White Magikoopa"},
    {"btl_un_kamec_green", "Green Magikoopa"},
    {"btl_un_piders", "Pider"},
    {"btl_un_burst_wanwan", "Red Chomp"},
    {"btl_un_monochrome_pakkun", "Pale Piranha"},
    {"btl_un_monochrome_kurokumorn", "Dark Puff"},
    {"btl_un_ura_noko", "Shady Koopa"},
    {"btl_un_ura_pata", "Shady Paratroopa"},
    {"btl_un_togenoko_ace", "Dark Koopatrol"},
    {"btl_un_dark_keeper", "Dark Craw"},
};
#define DUMMY_COUNT ((int)(sizeof(kDummies) / sizeof(kDummies[0])))

/* the names as the game says them (UTF-8), read on the game's frame */
static char sNames[DUMMY_COUNT][48];
static int sNamesRead;

int portDojoDummyCount(void) {
    return DUMMY_COUNT;
}

const char* portDojoDummyName(int i) {
    if (i < 0 || i >= DUMMY_COUNT) {
        return "";
    }
    return sNamesRead && sNames[i][0] != '\0' ? sNames[i] : kDummies[i].english;
}

/* the partners' (by the game's party id) and the command types' names likewise */
static char sPartnerNames[8][48];
static char sTypeNames[PORT_DOJO_TYPES][48];

const char* portDojoPartnerName(int party) {
    static const char* const kEnglish[8] = {"", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian", "Ms. Mowz"};
    if (party < 1 || party > 7) {
        return "";
    }
    return sPartnerNames[party][0] != '\0' ? sPartnerNames[party] : kEnglish[party];
}

const char* portDojoTypeLabel(int type) {
    return type >= 0 && type < PORT_DOJO_TYPES && sTypeNames[type][0] != '\0' ? sTypeNames[type] : NULL;
}

/* the game's text tag as UTF-8 into out (kept when it reads the same) */
static void gameName(const char* game, char* out, int size) {
    char text[48];
    text[0] = '\0';
    if (game != NULL && game[0] != '\0') {
        portCompanionUtf8(game, text, (int)sizeof(text), 0);
    }
    if (strcmp(text, out) != 0) {
        snprintf(out, (size_t)size, "%s", text);
    }
}

static const char* found(const char* tag) {
    const char* s = msgSearch(tag);
    return s != NULL && strcmp(s, tag) != 0 ? s : NULL;
}

static void readNames(void) {
    static const char* const kPartyMsg[8] = {NULL,          "name_party0", "name_party1", "name_party2",
                                             NULL,          "name_party4", "name_party5", "name_party6"};
    static const char* const kTypeMsg[PORT_DOJO_TYPES] = {"btl_cmd_act_jump", "btl_cmd_act_hammer",
                                                          "btl_cmd_act_super_action"};
    static const int kTypeParty[PORT_DOJO_TYPES] = {0, 0, 0, 1, 2, 5, 4, 6, 3, 7, 0, 0, 0};
    int i;
    if (gp == NULL || seqGetSeq() != SEQ_GAME) {
        return;
    }
    for (i = 0; i < DUMMY_COUNT; i++) {
        gameName(found(kDummies[i].tag), sNames[i], (int)sizeof(sNames[i]));
    }
    for (i = 1; i <= 7; i++) {
        gameName(i == 4 ? pouchGetYoshiName() : found(kPartyMsg[i]), sPartnerNames[i], (int)sizeof(sPartnerNames[i]));
    }
    for (i = 0; i < PORT_DOJO_TYPES; i++) {
        if (kTypeParty[i] != 0) {
            snprintf(sTypeNames[i], sizeof(sTypeNames[i]), "%s", sPartnerNames[kTypeParty[i]]);
        } else if (kTypeMsg[i] != NULL) {
            gameName(found(kTypeMsg[i]), sTypeNames[i], (int)sizeof(sTypeNames[i]));
        }
    }
    sNamesRead = 1;
}

/* --- the state --- */

enum { DOJO_OFF, DOJO_GOING, DOJO_FIGHT };
static int sState;
static PortDojoSetup sSetup;
static char sReturnMap[16];
static PouchData sFilePouch; /* Mario as he was */
static int sStartAsked, sStopAsked; /* from the settings page's thread (__atomic) */
static PortDojoSetup sAsked;

/* --- the battle --- */

static NpcSetupBattleInfo sInfo;
static NpcBattleSelect sSelect[2];
static BattleGroupSetup sGroup;
static BattleUnitSetup sDummy;
static BattleUnitKind sDummyKind;
static PointDropData sNoDrops[1];

USER_FUNC(evt_dojo_battle);
USER_FUNC(evt_dojo_battle_end);

static EVT_BEGIN(dojo_fight_evt)
    WAIT_FRAMES(20)
    CALL(evt_npc_entry, STRING("dojo_dummy"), STRING("c_kuribo"))
    CALL(evt_npc_flag_onoff, 1, STRING("dojo_dummy"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW)
    CALL(evt_npc_set_position, STRING("dojo_dummy"), 0, -1000, 0)
    CALL(evt_dojo_battle, STRING("dojo_dummy"), LW(0))
    IF_INT_EQUAL(LW(0), 0)
        RETURN
    END_IF
    CALL(evt_npc_battle_start, STRING("dojo_dummy"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(0))
    CALL(evt_dojo_battle_end, LW(0))
    RETURN
EVT_END()

static EVT_BEGIN(dojo_map_init)
    SPAWN_EVENT(dojo_fight_evt)
    RETURN
EVT_END()

/* the chosen fighter's place in the Pit's lineups (the area's battle table) */
static const BattleUnitSetup* findFighter(const NpcSetupBattleInfo* tbl, const char* tag) {
    int i, k;
    for (i = 1; i < 26 && tbl[i].name != NULL; i++) {
        const NpcBattleSelect* sel = tbl[i].select;
        const BattleGroupSetup* g;
        if (sel == NULL || (g = sel[0].groupSetup) == NULL) {
            continue;
        }
        for (k = 0; k < g->numEnemies; k++) {
            const BattleUnitSetup* u = &g->enemyData[k];
            if (u->unitKindParams != NULL && u->unitKindParams->name != NULL && strcmp(u->unitKindParams->name, tag) == 0) {
                return u;
            }
        }
    }
    return NULL;
}

/* in: the stand-in's name; out: 1 when the fight starts (its battle set up) */
USER_FUNC(evt_dojo_battle) {
    EvtWord* args = event->args;
    NpcEntry* npc = npcNameToPtr_NoAssert((const char*)evtGetValue(event, args[0]));
    AreaData* ad = areaDataPtr("tou");
    const NpcSetupBattleInfo* tbl = ad != NULL ? (const NpcSetupBattleInfo*)ad->btlSetupTbl : NULL;
    const BattleUnitSetup* fighter;
    (void)isFirstCall;
    if (sState != DOJO_GOING || npc == NULL || tbl == NULL || tbl[DOJO_STAGE_SETUP].select == NULL) {
        evtSetValue(event, args[1], 0);
        return EVT_RETURN_DONE;
    }
    fighter = findFighter(tbl, kDummies[sSetup.dummy].tag);
    if (fighter == NULL) {
        fighter = findFighter(tbl, kDummies[0].tag);
    }
    if (fighter == NULL) {
        int i;
        printf("[dojo] no fighter %s in the Glitz Pit's lineups\n", kDummies[sSetup.dummy].tag);
        for (i = 0; i < 26 && tbl[i].name != NULL; i++) {
            const NpcBattleSelect* sel = tbl[i].select;
            const BattleGroupSetup* g = sel != NULL ? sel[0].groupSetup : NULL;
            printf("[dojo]   %d: %d enemies, the first %s\n", i, g != NULL ? (int)g->numEnemies : -1,
                   g != NULL && g->numEnemies > 0 && g->enemyData[0].unitKindParams != NULL &&
                           g->enemyData[0].unitKindParams->name != NULL
                       ? g->enemyData[0].unitKindParams->name
                       : "-");
        }
        fflush(stdout);
        evtSetValue(event, args[1], 0);
        return EVT_RETURN_DONE;
    }
    /* the dummy: the fighter's kind, but it never falls, gives nothing and lets Mario run */
    sDummyKind = *fighter->unitKindParams;
    sDummyKind.maxHp = DUMMY_HP;
    sDummyKind.level = 0;
    sDummyKind.bonusExp = 0;
    sDummyKind.bonusCoin = 0;
    sDummyKind.bonusCoinRate = 0;
    sDummyKind.baseCoin = 0;
    sDummyKind.runRate = 100;
    sDummy = *fighter;
    sDummy.unitKindParams = &sDummyKind;
    sDummy.alliance = 1;
    sDummy.attackPhase = BATTLE_PHASE_ENEMY;
    sDummy.position.x = 90.0f; /* (the lineup's first place) */
    sDummy.position.z = 0.0f;
    sDummy.itemDropTable = NULL;
    portDojoSetDummyKind(&sDummyKind);

    sInfo = tbl[DOJO_STAGE_SETUP]; /* its stage, audience and music */
    sInfo.name = "dojo";
    sInfo.fixedItem = 0;
    sInfo.swFlag = -1;
    sInfo.altSelect = NULL;
    sInfo.battleFlags =
        NPC_BATTLE_LOSS_OK | NPC_BATTLE_NO_COINS | NPC_BATTLE_NO_HEARTS | NPC_BATTLE_NO_FLOWERS | NPC_BATTLE_NO_ITEM;
    memset(&sGroup, 0, sizeof(sGroup));
    sGroup.numEnemies = 1;
    sGroup.enemyData = &sDummy;
    sGroup.noItemWeight = 1;
    sGroup.hpDropTable = sNoDrops;
    sGroup.fpDropTable = sNoDrops;
    memset(sSelect, 0, sizeof(sSelect));
    sSelect[0].weight = 1;
    sSelect[0].groupSetup = &sGroup;
    sSelect[0].stageData = tbl[DOJO_STAGE_SETUP].select[0].stageData;
    sInfo.select = sSelect;
    npcSetupBattleInfo(npc, &sInfo);
    sState = DOJO_FIGHT;
    portDojoSetRunning(1);
    printf("[dojo] fight: %s, attacks %d, partner %d, every move %d\n", kDummies[sSetup.dummy].tag, sSetup.attacks,
           sSetup.partner, sSetup.everyMove);
    fflush(stdout);
    evtSetValue(event, args[1], 1);
    return EVT_RETURN_DONE;
}

static void endDojo(void) {
    if (sState != DOJO_OFF) {
        *pouchGetPtr() = sFilePouch;
        printf("[dojo] ended: Mario as he was\n");
        fflush(stdout);
    }
    sState = DOJO_OFF;
    portDojoSetRunning(0);
    portDojoSetDummyKind(NULL);
}

/* in: the battle's result (1 won: the dummy fell after all, 3 lost, 4 ran away) */
USER_FUNC(evt_dojo_battle_end) {
    const s32 result = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (sState != DOJO_FIGHT) {
        return EVT_RETURN_DONE;
    }
    if (result == 1 && !__atomic_load_n(&sStopAsked, __ATOMIC_SEQ_CST)) {
        sState = DOJO_GOING; /* (a one-hit KO, a Bob-omb's blast): the dummy is back */
        seqSetSeq(SEQ_MAP_CHANGE, DOJO_MAP, NULL);
        return EVT_RETURN_DONE;
    }
    __atomic_store_n(&sStopAsked, 0, __ATOMIC_SEQ_CST);
    endDojo();
    seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "gor_01", NULL);
    return EVT_RETURN_DONE;
}

/* --- the loadout --- */

static void lendEveryMove(void) {
    PouchData* p = pouchGetPtr();
    int i, b, n;
    for (i = 1; i <= 7; i++) {
        partyJoin((PartyId)i);
        p->partyData[i].attackLevel = 2;
        p->partyData[i].HPLevel = 2;
        p->partyData[i].techLevel = 2;
    }
    p->jumpLevel = 2;
    p->hammerLevel = 2;
    p->starPowersObtained = 0xFF;
    p->maximumSP = p->currentSP = 800;
    /* the jump and hammer badges (Power Jump to Ice Smash), worn as well as Mario's own */
    for (b = ITEM_POWER_JUMP; b <= ITEM_ICE_SMASH; b++) {
        int worn = 0;
        n = 0;
        for (i = 0; i < MAX_BADGES; i++) {
            worn |= p->equippedBadges[i] == b;
            n += p->equippedBadges[i] != 0;
        }
        if (!worn && n < MAX_BADGES) {
            p->equippedBadges[n] = (s16)b;
        }
    }
}

/* --- hooks --- */

/* BattleInformationSetParty: the partner chosen comes out first */
static int setPartyPre(FieldBattleInfo** info, BattleUnitType* party) {
    (void)info;
    if (sState == DOJO_FIGHT && sSetup.partner >= 1 && sSetup.partner <= 7 &&
        (pouchGetPtr()->partyData[sSetup.partner].flags & POUCH_PARTY_FLAG_JOINED)) {
        *party = BattleTransPartyIdToUnitKind(sSetup.partner);
    }
    return 0;
}

/* BattleStoreExp: no Star Points from the dummy */
static int storeExpPre(BattleWork** wp, s32* exp) {
    (void)wp;
    (void)exp;
    return sState == DOJO_FIGHT;
}

static void startNow(void) {
    sSetup = sAsked;
    if (sSetup.dummy < 0 || sSetup.dummy >= DUMMY_COUNT) {
        sSetup.dummy = 0;
    }
    snprintf(sReturnMap, sizeof(sReturnMap), "%s", gp->currentMapName);
    sFilePouch = *pouchGetPtr();
    if (sSetup.everyMove) {
        lendEveryMove();
    }
    portDojoSetAttacks(sSetup.attacks);
    portDojoSessionStart();
    sState = DOJO_GOING;
    printf("[dojo] started from %s\n", sReturnMap);
    fflush(stdout);
    seqSetSeq(SEQ_MAP_CHANGE, DOJO_MAP, NULL);
}

/* After marioStMain, each frame of the game */
static void frameHook(void) {
    static unsigned sTick;
    const SequenceType seq = seqGetSeq();
    static int sEnvDone;
    if ((sTick++ % 120) == 0 || !sNamesRead) {
        readNames(); /* (the game's text: its language may change) */
    }
    if (!sEnvDone && seq == SEQ_GAME && gp != NULL && !gp->inBattle && sTick > 60) {
        /* $TTYD_DOJO=dummy,attacks,partner,everyMove (tests): the dojo at the first frame in the field */
        const char* env = getenv("TTYD_DOJO");
        sEnvDone = 1;
        if (env != NULL) {
            PortDojoSetup s = {0, PORT_DOJO_DUMMY_EVERY_TURN, 1, 1};
            const char* why = NULL;
            sscanf(env, "%d,%d,%d,%d", &s.dummy, &s.attacks, &s.partner, &s.everyMove);
            if (!portDojoStart(&s, &why)) {
                printf("[dojo] TTYD_DOJO: %s\n", why);
                fflush(stdout);
            }
        }
    }
    if (sState != DOJO_OFF && (seq == SEQ_GAME_OVER || seq == SEQ_TITLE || portOnlineActive())) {
        endDojo();
    }
    if (sState == DOJO_GOING && seq == SEQ_GAME && gp != NULL && strcmp(gp->currentMapName, DOJO_MAP) != 0 &&
        __atomic_load_n(&sStopAsked, __ATOMIC_SEQ_CST)) {
        __atomic_store_n(&sStopAsked, 0, __ATOMIC_SEQ_CST);
        endDojo();
    }
    if (sState == DOJO_OFF) {
        __atomic_store_n(&sStopAsked, 0, __ATOMIC_SEQ_CST);
    }
    if (seq == SEQ_GAME && gp != NULL && !gp->inBattle && __atomic_exchange_n(&sStartAsked, 0, __ATOMIC_SEQ_CST)) {
        if (sState == DOJO_OFF) {
            startNow();
        }
    }
    if (sState != DOJO_OFF && seq == SEQ_GAME && gp != NULL && !gp->inBattle && strcmp(gp->currentMapName, DOJO_MAP) == 0 &&
        __atomic_load_n(&sStopAsked, __ATOMIC_SEQ_CST)) {
        /* Leave asked for between fights: back now */
        __atomic_store_n(&sStopAsked, 0, __ATOMIC_SEQ_CST);
        endDojo();
        seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "gor_01", NULL);
    }
}

int portDojoStart(const PortDojoSetup* setup, const char** why) {
    const PortModeStatus* mode = portModeStatus();
    if (portOnlineActive() || portReplayActive()) {
        *why = "The dojo is offline only: not during online play or a replay.";
        return 0;
    }
    if (seqGetSeq() != SEQ_GAME || gp == NULL || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if ((mode->mode != PORT_MODE_NONE && !mode->finished) || portRaidActive() || sState != DOJO_OFF) {
        *why = "Another mode is running: finish it first.";
        return 0;
    }
    sAsked = *setup;
    __atomic_store_n(&sStartAsked, 1, __ATOMIC_SEQ_CST);
    return 1;
}

void portDojoStop(void) {
    __atomic_store_n(&sStopAsked, 1, __ATOMIC_SEQ_CST);
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

void portDojoRegisterMaps(void) {
    portModsAddHostMap(DOJO_MAP, dojo_map_init, DOJO_FILES);
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portModHostAddHook(0, kSlot_BattleInformationSetParty, (void*)setPartyPre);
    portModHostAddHook(0, kSlot_BattleStoreExp, (void*)storeExpPre);
}
