/* Versus battles (port/include/port/versus.h, docs/VERSUS.md): the set-up
 * kept between fights, the fight's own map and battle setup, and the hooks
 * that make the enemy side Luigi's: his units entered from his pouch, the
 * game's "Mario", "partner" and pouch his while his side acts, the second
 * controller read as the first then, and the defender's guards on every hit.
 *
 * The fight is on a map of its own in the Glitz Pit's area (tou_m2, with
 * tou_03's files, as the modes' maps: port/src/aurora/modes_aurora.c), whose
 * script starts the battle with an invisible stand-in NPC and, after it,
 * goes back to the room the fight was started in. Both sides' pouches are
 * made from the set-up and the file's own is put back afterwards.
 *
 * Every hook returns at once unless a versus fight is on (sState), so the
 * game's own battles, replays and online play are as they were. */
#include "port/versus.h"
#include "port/mod_host.h"
#include "port/mods.h"
#include "port/hook_list.h"
#include "port/settings.h"
#include "port/pause_forms.h"
#include "battle/battle.h"
#include "battle/battle_ac.h"
#include "battle/battle_pad.h"
#include "battle/battle_party.h"
#include "battle/battle_seq_command.h"
#include "battle/battle_stage.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "data/mapdata.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"
#include "unit/unit_mario.h"
#include "statuswindow.h"
#include "driver/dispdrv.h"
#include "manager/fontmgr.h"
#include "win/win_main.h"
#include "port/luigi.h"

#include <SDL3/SDL_timer.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern BattleUnitSetup entryunit_mario; /* battle.c */
void npcSetupBattleInfo(NpcEntry* npc, NpcSetupBattleInfo* setup); /* npcdrv.c */

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

/* --- the set-up --- */

static PortVersusSetup sSetup;
static int sSetupRead;

void portVersusSetupDefaults(PortVersusSetup* s) {
    static const short kBadges[2][3] = {{ITEM_POWER_JUMP, ITEM_POWER_SMASH, ITEM_DEFEND_PLUS},
                                        {ITEM_MULTIBOUNCE, ITEM_QUAKE_HAMMER, ITEM_DAMAGE_DODGE}};
    static const int kPartners[2][3] = {{PARTNER_GOOMBELLA, PARTNER_KOOPS, PARTNER_FLURRIE},
                                        {PARTNER_YOSHI, PARTNER_VIVIAN, PARTNER_BOBBERY}};
    int i, k;
    memset(s, 0, sizeof(*s));
    for (i = 0; i < 2; i++) {
        PortVersusSide* d = &s->side[i];
        d->level = 10;
        d->hpUps = 3;
        d->fpUps = 3;
        d->bpUps = 3;
        d->gear = 2;
        d->partnerRank = 1;
        for (k = 0; k < PORT_VERSUS_PARTNERS; k++) {
            d->partners[k] = kPartners[i][k];
        }
        for (k = 0; k < 3; k++) {
            d->badges[k] = kBadges[i][k];
        }
        d->items[0] = ITEM_MUSHROOM;
        d->items[1] = ITEM_MUSHROOM;
        d->items[2] = ITEM_HONEY_SYRUP;
        d->items[3] = ITEM_FIRE_FLOWER;
        d->controller = PORT_VERSUS_LOCAL;
    }
    s->rules.stage = 0;
    s->rules.turnTimer = 0;
    s->rules.luckBadges = 0;
}

static void setupPath(char* out, size_t size) {
    snprintf(out, size, "%s/versus.txt", portSettingsUserPath());
}

static void readList(const char* v, short* out, int max) {
    int n = 0;
    while (*v != '\0' && n < max) {
        out[n++] = (short)atoi(v);
        while (*v != '\0' && *v != ',') {
            v++;
        }
        if (*v == ',') {
            v++;
        }
    }
}

static void readSetup(void) {
    char path[512], line[512];
    FILE* f;
    sSetupRead = 1;
    int side;
    portVersusSetupDefaults(&sSetup);
    setupPath(path, sizeof(path));
    if ((f = fopen(path, "r")) == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char* eq = strchr(line, '=');
        PortVersusSide* d;
        if (eq == NULL) {
            continue;
        }
        *eq++ = '\0';
        line[strcspn(line, "\r\n")] = '\0';
        eq[strcspn(eq, "\r\n")] = '\0';
        if (strcmp(line, "stage") == 0) {
            sSetup.rules.stage = atoi(eq);
        } else if (strcmp(line, "timer") == 0) {
            sSetup.rules.turnTimer = atoi(eq);
        } else if (strcmp(line, "luck") == 0) {
            sSetup.rules.luckBadges = atoi(eq) != 0;
        }
        if (line[0] != 's' || (line[1] != '0' && line[1] != '1') || line[2] != '.') {
            continue;
        }
        side = line[1] - '0';
        d = &sSetup.side[side];
        if (strcmp(line + 3, "level") == 0) {
            d->level = atoi(eq);
        } else if (strcmp(line + 3, "ups") == 0) {
            sscanf(eq, "%d,%d,%d", &d->hpUps, &d->fpUps, &d->bpUps);
        } else if (strcmp(line + 3, "gear") == 0) {
            d->gear = atoi(eq);
        } else if (strcmp(line + 3, "rank") == 0) {
            d->partnerRank = atoi(eq);
        } else if (strcmp(line + 3, "partners") == 0) {
            short p[PORT_VERSUS_PARTNERS] = {0};
            int k;
            readList(eq, p, PORT_VERSUS_PARTNERS);
            for (k = 0; k < PORT_VERSUS_PARTNERS; k++) {
                d->partners[k] = p[k];
            }
        } else if (strcmp(line + 3, "badges") == 0) {
            memset(d->badges, 0, sizeof(d->badges));
            readList(eq, d->badges, PORT_VERSUS_BADGES);
        } else if (strcmp(line + 3, "items") == 0) {
            memset(d->items, 0, sizeof(d->items));
            readList(eq, d->items, PORT_VERSUS_ITEMS);
        }
    }
    fclose(f);
    /* whatever the file says, a set-up the page can show */
    for (side = 0; side < 2; side++) {
        PortVersusSide* d = &sSetup.side[side];
        int k;
        d->level = d->level < 1 ? 1 : d->level > 30 ? 30 : d->level;
        d->gear = d->gear < 1 ? 1 : d->gear > 3 ? 3 : d->gear;
        d->partnerRank = d->partnerRank < 0 ? 0 : d->partnerRank > 2 ? 2 : d->partnerRank;
        if (d->hpUps < 0 || d->fpUps < 0 || d->bpUps < 0 || d->hpUps + d->fpUps + d->bpUps > d->level - 1) {
            d->hpUps = d->level - 1;
            d->fpUps = d->bpUps = 0;
        }
        for (k = 0; k < PORT_VERSUS_PARTNERS; k++) {
            if (d->partners[k] < 0 || d->partners[k] > PARTNER_MSMOWZ) {
                d->partners[k] = 0;
            }
        }
        for (k = 0; k < PORT_VERSUS_BADGES; k++) {
            if (d->badges[k] != 0 && (d->badges[k] < ITEM_POWER_JUMP || d->badges[k] >= BADGE_MAX)) {
                d->badges[k] = 0;
            }
        }
        for (k = 0; k < PORT_VERSUS_ITEMS; k++) {
            if (d->items[k] != 0 && !portVersusItemAllowed(d->items[k])) {
                d->items[k] = 0;
            }
        }
    }
    sSetup.rules.turnTimer = sSetup.rules.turnTimer < 0 ? 0 : sSetup.rules.turnTimer > 120 ? 120 : sSetup.rules.turnTimer;
}

PortVersusSetup* portVersusSetup(void) {
    if (!sSetupRead) {
        readSetup();
    }
    return &sSetup;
}

static void writeList(FILE* f, const char* key, int side, const short* v, int n) {
    int k, first = 1;
    fprintf(f, "s%d.%s=", side, key);
    for (k = 0; k < n; k++) {
        if (v[k] != 0) {
            fprintf(f, first ? "%d" : ",%d", v[k]);
            first = 0;
        }
    }
    fputc('\n', f);
}

void portVersusSave(void) {
    char path[512];
    FILE* f;
    int i;
    setupPath(path, sizeof(path));
    if ((f = fopen(path, "w")) == NULL) {
        return;
    }
    fprintf(f, "stage=%d\ntimer=%d\nluck=%d\n", sSetup.rules.stage, sSetup.rules.turnTimer, sSetup.rules.luckBadges);
    for (i = 0; i < 2; i++) {
        const PortVersusSide* d = &sSetup.side[i];
        short p[PORT_VERSUS_PARTNERS];
        int k;
        fprintf(f, "s%d.level=%d\ns%d.ups=%d,%d,%d\ns%d.gear=%d\ns%d.rank=%d\n", i, d->level, i, d->hpUps, d->fpUps,
                d->bpUps, i, d->gear, i, d->partnerRank);
        for (k = 0; k < PORT_VERSUS_PARTNERS; k++) {
            p[k] = (short)d->partners[k];
        }
        writeList(f, "partners", i, p, PORT_VERSUS_PARTNERS);
        writeList(f, "badges", i, d->badges, PORT_VERSUS_BADGES);
        writeList(f, "items", i, d->items, PORT_VERSUS_ITEMS);
    }
    fclose(f);
}

/* the luck badges, which leave a hit to chance: only with the rule on */
int portVersusBadgeFlagged(int item) {
    switch (item) {
        case ITEM_CLOSE_CALL:
        case ITEM_CLOSE_CALL_P:
        case ITEM_PRETTY_LUCKY:
        case ITEM_PRETTY_LUCKY_P:
        case ITEM_LUCKY_DAY:
        case ITEM_LUCKY_DAY_P:
        case ITEM_LUCKY_START:
        case ITEM_LUCKY_START_P:
            return 1;
    }
    return 0;
}

/* badges with nothing to do in a versus fight (the field's, the coins'),
 * and the unused one, aren't offered */
int portVersusBadgeAllowed(int item, const PortVersusRules* rules) {
    if (item < ITEM_POWER_JUMP || item >= BADGE_MAX) {
        return 0;
    }
    switch (item) {
        case ITEM_HEART_FINDER:
        case ITEM_FLOWER_FINDER:
        case ITEM_MONEY_MONEY:
        case ITEM_ITEM_HOG:
        case ITEM_PEEKABOO:
        case ITEM_CHILL_OUT:
        case ITEM_FIRST_ATTACK:
        case ITEM_BUMP_ATTACK:
        case ITEM_SLOW_GO:
        case ITEM_UNUSED_DEFEND_BADGE:
        case ITEM_UNUSED_DEFEND_BADGE_P:
            return 0;
    }
    if (portVersusBadgeFlagged(item) && (rules == NULL || !rules->luckBadges)) {
        return 0;
    }
    return itemDataTable[item].bp_cost >= 0;
}

/* The items a side can take: the battle items, not the ones that win or
 * end a fight by themselves (Fright Mask scares the other side off; Mystery
 * can be anything), nor a Life Shroom (no revival in a versus fight). */
int portVersusItemAllowed(int item) {
    switch (item) {
        case ITEM_THUNDER_BOLT:
        case ITEM_THUNDER_RAGE:
        case ITEM_SHOOTING_STAR:
        case ITEM_ICE_STORM:
        case ITEM_FIRE_FLOWER:
        case ITEM_EARTH_QUAKE:
        case ITEM_BOOS_SHEET:
        case ITEM_VOLT_SHROOM:
        case ITEM_REPEL_CAPE:
        case ITEM_RUIN_POWDER:
        case ITEM_SLEEPY_SHEEP:
        case ITEM_POW_BLOCK:
        case ITEM_STOPWATCH:
        case ITEM_DIZZY_DIAL:
        case ITEM_POWER_PUNCH:
        case ITEM_COURAGE_SHELL:
        case ITEM_HP_DRAIN_ITEM:
        case ITEM_MINI_MR_MINI:
        case ITEM_MR_SOFTENER:
        case ITEM_MUSHROOM:
        case ITEM_SUPER_SHROOM:
        case ITEM_ULTRA_SHROOM:
        case ITEM_DRIED_SHROOM:
        case ITEM_TASTY_TONIC:
        case ITEM_HONEY_SYRUP:
        case ITEM_MAPLE_SYRUP:
        case ITEM_JAMMIN_JELLY:
        case ITEM_SLOW_SHROOM:
        case ITEM_GRADUAL_SYRUP:
        case ITEM_POINT_SWAP:
        case ITEM_SPITE_POUCH:
        case ITEM_KOOPA_CURSE:
            return 1;
    }
    return 0;
}

int portVersusBp(const PortVersusSide* side) {
    return 3 + 3 * side->bpUps;
}

int portVersusMaxHp(const PortVersusSide* side) {
    return 10 + 5 * side->hpUps;
}

int portVersusMaxFp(const PortVersusSide* side) {
    return 5 + 5 * side->fpUps;
}

int portVersusBpUsed(const PortVersusSide* side) {
    int k, used = 0;
    for (k = 0; k < PORT_VERSUS_BADGES; k++) {
        if (side->badges[k] != 0) {
            used += itemDataTable[side->badges[k]].bp_cost;
        }
    }
    return used;
}

const char* portVersusCheck(const PortVersusSetup* s, int side) {
    const PortVersusSide* d = &s->side[side];
    int k, j;
    if (d->hpUps + d->fpUps + d->bpUps != d->level - 1) {
        return "Share out every level-up between HP, FP and BP.";
    }
    if (portVersusBpUsed(d) > portVersusBp(d)) {
        return "The badges need more BP than this side has.";
    }
    for (k = 0; k < PORT_VERSUS_BADGES; k++) {
        if (d->badges[k] != 0 && !portVersusBadgeAllowed(d->badges[k], &s->rules)) {
            return "A luck badge is on, and the rules don't allow them.";
        }
    }
    for (k = 0; k < PORT_VERSUS_PARTNERS; k++) {
        for (j = 0; j < PORT_VERSUS_PARTNERS && d->partners[k] != 0; j++) {
            if (s->side[!side].partners[j] == d->partners[k]) {
                return "A partner can fight on one side only.";
            }
        }
    }
    return NULL;
}

/* --- the fight's state --- */

enum { STATE_NONE, STATE_ASKED, STATE_GOING, STATE_FIGHT, STATE_OVER };

static int sState;
static int sAsked;          /* __atomic: the page asked for a start */
static int sSide;           /* the side acting (its units are "Mario" and "the partner"; its pouch is live) */
static int sLive;           /* whose pouch is the game's now */
static int sNoRedirect;     /* the HUD reads Mario's side whatever the side */
static PouchData sOther;    /* the pouch of the side that isn't live */
static PouchData sFilePouch; /* the file's own, put back afterwards */
static char sReturnMap[16];
static int sWinner = -1;    /* the last fight's: 0 Mario's side, 1 Luigi's */
static Uint64 sCmdStart;    /* a command's start (the turn timer) */
static int sController[2];

int portVersusOn(void) {
    return sState == STATE_GOING || sState == STATE_FIGHT;
}

/* the battle's hooks act: a versus battle is running */
static int fight(void) {
    return sState == STATE_FIGHT && _battleWorkPointer != NULL;
}

int portVersusSide(void) {
    return fight() ? sSide : 0;
}

void portVersusSetController(int side, int controller) {
    if (side == 0 || side == 1) {
        sController[side] = controller;
    }
}

static int playerKind(const BattleUnit* u) {
    return u->currentKind == UNIT_MARIO || (u->currentKind >= TYPE_PARTNER_MIN && u->currentKind < TYPE_PARTNER_MAX);
}

int portVersusPlayerUnit(const void* unit) {
    const BattleUnit* u = (const BattleUnit*)unit;
    if (u == NULL) {
        return 0;
    }
    if (!fight()) {
        return u->alliance == 0;
    }
    return u->alliance == 0 || (u->alliance == 1 && playerKind(u));
}

static void livePouch(int side) {
    if (side != sLive) {
        PouchData* p = pouchGetPtr();
        PouchData t = *p;
        *p = sOther;
        sOther = t;
        sLive = side;
    }
}

static void setSide(int side) {
    livePouch(side);
    if (side != sSide) {
        printf("[versus] %s's side acts\n", side ? "Luigi" : "Mario");
        fflush(stdout);
    }
    sSide = side;
}

/* both players have a controller of their own (or the tests' second) */
static int twoPads(void) {
    static int sTest = -1;
    if (sTest < 0) {
        sTest = getenv("TTYD_P2_PRESS") != NULL;
    }
    return sTest || PADGetIndexForPort(1) >= 0;
}

/* --- the pouches --- */

/* the side's pouch, made in the game's from the file's (its colours, Yoshi's name) */
static void buildPouch(const PortVersusSide* d) {
    PouchData* p = pouchGetPtr();
    int i, k;
    *p = sFilePouch;
    memset(p->heldItems, 0, sizeof(p->heldItems));
    memset(p->storedItems, 0, sizeof(p->storedItems));
    for (i = 0; i < MAX_BADGES; i++) {
        p->badges[i] = ITEM_NULL;
        p->equippedBadges[i] = ITEM_NULL;
    }
    for (i = 1; i <= PARTNER_MSMOWZ; i++) {
        p->partyData[i].flags &= ~(POUCH_PARTY_FLAG_JOINED | POUCH_PARTY_FLAG_AWAY);
        p->partyData[i].HPLevel = (s16)d->partnerRank;
        p->partyData[i].attackLevel = (s16)d->partnerRank;
        p->partyData[i].techLevel = (s16)d->partnerRank;
    }
    for (k = 0; k < PORT_VERSUS_PARTNERS; k++) {
        if (d->partners[k] > 0) {
            p->partyData[d->partners[k]].flags |= POUCH_PARTY_FLAG_JOINED;
        }
    }
    p->level = (s16)d->level;
    p->baseMaxHP = p->maximumHP = (s16)portVersusMaxHp(d);
    p->baseMaxFP = p->maximumFP = (s16)portVersusMaxFp(d);
    p->maximumBP = p->availableBP = (s16)portVersusBp(d);
    p->jumpLevel = (s8)d->gear;
    p->hammerLevel = (s8)d->gear;
    p->starPowersObtained = 0xFF;
    p->maximumSP = 800;
    p->currentSP = 0;
    p->starPoints = 0;
    p->merleeCurseUsesRemaining = 0;
    p->turnsUntilMerleeActivation = 0;
    p->merleeNextCurseType = 0;
    for (k = 0; k < PORT_VERSUS_BADGES; k++) {
        if (d->badges[k] != 0) {
            pouchGetItem((ItemType)d->badges[k]);
        }
    }
    for (k = 0; k < PORT_VERSUS_BADGES; k++) {
        if (d->badges[k] != 0) {
            pouchEquipBadgeID(d->badges[k]);
        }
    }
    for (k = 0; k < PORT_VERSUS_ITEMS; k++) {
        if (d->items[k] != 0) {
            pouchGetItem((ItemType)d->items[k]);
        }
    }
    pouchReviseMarioParam();
    pouchRevisePartyParam();
    p->currentHP = p->maximumHP;
    p->currentFP = p->maximumFP;
    for (i = 1; i <= PARTNER_MSMOWZ; i++) {
        p->partyData[i].currentHP = p->partyData[i].maximumHP;
    }
}

/* Both sides' pouches: Luigi's kept aside, Mario's the game's. */
static void buildPouches(void) {
    sFilePouch = *pouchGetPtr();
    buildPouch(&sSetup.side[1]);
    sOther = *pouchGetPtr();
    buildPouch(&sSetup.side[0]);
    sLive = 0;
    sSide = 0;
}

static void restorePouch(void) {
    if (sState == STATE_FIGHT || sState == STATE_OVER) {
        livePouch(0);
        *pouchGetPtr() = sFilePouch;
    }
}

static int firstPartner(int side) {
    int k;
    for (k = 0; k < PORT_VERSUS_PARTNERS; k++) {
        if (sSetup.side[side].partners[k] > 0) {
            return sSetup.side[side].partners[k];
        }
    }
    return 0;
}

/* --- the map and its script --- */

/* the fight's battle: the Glitz Pit's stage and audience (btlsetup_tou_tbl's
 * first match, in the area's REL), no enemies of the game's own: Luigi's
 * side is entered by btlSetupPost */
static NpcSetupBattleInfo sInfo;
static NpcBattleSelect sSelect[2];
static BattleGroupSetup sGroup;
static BattleUnitSetup sNoEnemy;

USER_FUNC(evt_versus_battle);
USER_FUNC(evt_versus_battle_end);

static EVT_BEGIN(versus_fight_evt)
    WAIT_FRAMES(20)
    CALL(evt_npc_entry, STRING("vs_stand_in"), STRING("c_kuribo"))
    CALL(evt_npc_flag_onoff, 1, STRING("vs_stand_in"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW)
    CALL(evt_npc_set_position, STRING("vs_stand_in"), 0, -1000, 0)
    CALL(evt_versus_battle, STRING("vs_stand_in"), LW(0))
    IF_INT_EQUAL(LW(0), 0)
        RETURN
    END_IF
    CALL(evt_npc_battle_start, STRING("vs_stand_in"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(0))
    CALL(evt_versus_battle_end, LW(0))
    RETURN
EVT_END()

static EVT_BEGIN(versus_map_init)
    SPAWN_EVENT(versus_fight_evt)
    RETURN
EVT_END()

/* in: the stand-in's name; out: 1 when the fight starts (its battle set up) */
USER_FUNC(evt_versus_battle) {
    EvtWord* args = event->args;
    const char* name = (const char*)evtGetValue(event, args[0]);
    const NpcSetupBattleInfo* tou = (const NpcSetupBattleInfo*)areaDataPtr("tou")->btlSetupTbl;
    NpcEntry* npc = npcNameToPtr_NoAssert(name);
    (void)isFirstCall;
    if (sState != STATE_GOING || npc == NULL || tou == NULL) {
        evtSetValue(event, args[1], 0);
        return EVT_RETURN_DONE;
    }
    sInfo = tou[1]; /* the first match: its stage, audience and music */
    sInfo.name = "versus";
    sInfo.fixedItem = 0;
    sInfo.swFlag = -1;
    sInfo.altSelect = NULL;
    sInfo.battleFlags = NPC_BATTLE_LOSS_OK | NPC_BATTLE_NO_ESCAPE | NPC_BATTLE_NO_COINS | NPC_BATTLE_NO_HEARTS |
                        NPC_BATTLE_NO_FLOWERS | NPC_BATTLE_NO_ITEM;
    memset(&sNoEnemy, 0, sizeof(sNoEnemy));
    sNoEnemy.unitKindParams = &unitdata_Mario; /* (read for its coins, with none entered) */
    memset(&sGroup, 0, sizeof(sGroup));
    sGroup.numEnemies = 0;
    sGroup.enemyData = &sNoEnemy;
    sGroup.noItemWeight = 1;
    memset(sSelect, 0, sizeof(sSelect));
    sSelect[0].weight = 1;
    sSelect[0].groupSetup = &sGroup;
    sSelect[0].stageData = tou[1].select[0].stageData;
    sInfo.select = sSelect;
    npcSetupBattleInfo(npc, &sInfo);
    buildPouches();
    sState = STATE_FIGHT;
    sWinner = -1;
    printf("[versus] the fight starts: Mario (level %d) vs Luigi (level %d)\n", sSetup.side[0].level,
           sSetup.side[1].level);
    fflush(stdout);
    evtSetValue(event, args[1], 1);
    return EVT_RETURN_DONE;
}

/* in: the battle's result (1 Mario's side won, 3 lost) */
USER_FUNC(evt_versus_battle_end) {
    const s32 result = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (sState != STATE_FIGHT) {
        return EVT_RETURN_DONE;
    }
    sWinner = result == 1 ? 0 : 1;
    printf("[versus] %s's side wins (result %d)\n", sWinner ? "Luigi" : "Mario", (int)result);
    fflush(stdout);
    restorePouch();
    sState = STATE_OVER;
    seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "gor_01", NULL);
    return EVT_RETURN_DONE;
}

int portVersusWinner(void) {
    return sWinner;
}

int portVersusStart(const char** why) {
    int i;
    const PortVersusSetup* s = portVersusSetup();
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (sState != STATE_NONE && sState != STATE_OVER) {
        *why = "A versus fight is on already.";
        return 0;
    }
    for (i = 0; i < 2; i++) {
        const char* bad = portVersusCheck(s, i);
        if (bad != NULL) {
            *why = bad;
            return 0;
        }
    }
    portVersusSave();
    __atomic_store_n(&sAsked, 1, __ATOMIC_SEQ_CST);
    return 1;
}

/* --- the hooks --- */

/* BattleGetMarioPtr / BattleGetPartyPtr: Luigi's side's while it acts */
static BattleUnit* sideUnit(BattleWork* wp, int partner) {
    int i;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u == NULL || u->alliance != 1) {
            continue;
        }
        if (!partner && u->currentKind == UNIT_MARIO) {
            return u;
        }
        if (partner && !(u->flags & UNIT_FLAG_NOT_ACTIVE_PARTNER) && u->currentKind >= TYPE_PARTNER_MIN &&
            u->currentKind < TYPE_PARTNER_MAX) {
            return u;
        }
    }
    return NULL;
}

static int marioPre(BattleUnit** r, BattleWork** wp) {
    BattleUnit* u;
    if (!fight() || sSide == 0 || sNoRedirect || (u = sideUnit(*wp, 0)) == NULL) {
        return 0;
    }
    *r = u;
    return 1;
}

static int partyPre(BattleUnit** r, BattleWork** wp) {
    if (!fight() || sSide == 0 || sNoRedirect) {
        return 0;
    }
    *r = sideUnit(*wp, 1); /* (NULL: Luigi fights alone) */
    return 1;
}

/* BtlUnit_Entry: a unit for the player's side while Luigi's side acts (a
 * partner swapped in, Bobbery's bombs, Koops's shell) is Luigi's: the enemy
 * side, acting in its phases, on the right */
static BattleUnitSetup sFlipped[16];
static int sFlippedNext;

static int entryPre(BattleUnit** r, BattleUnitSetup** setup) {
    BattleUnitSetup* s;
    (void)r;
    if (!fight() || sSide == 0 || *setup == NULL || (*setup)->alliance != 0) {
        return 0;
    }
    s = &sFlipped[sFlippedNext++ % 16];
    *s = **setup;
    s->alliance = 1;
    s->attackPhase = s->attackPhase == BATTLE_PHASE_BEFORE_PLAYER ? BATTLE_PHASE_AFTER_PLAYER
                     : s->attackPhase == BATTLE_PHASE_PLAYER      ? BATTLE_PHASE_ENEMY
                                                                  : s->attackPhase;
    s->position.x = -s->position.x;
    *setup = s;
    return 0;
}

/* Btl_UnitSetup: after Mario's side and the (no) enemies, Luigi and his first partner */
static void unitSetupPost(BOOL* r, BattleWork* wp) {
    BattleUnit* luigi;
    int partner;
    (void)r;
    if (!fight()) {
        return;
    }
    wp->alliances[1].allianceId = 2; /* (as Mario's side: lost when Luigi is down) */
    setSide(1);
    luigi = BtlUnit_Entry(&entryunit_mario);
    if (luigi != NULL) {
        BtlUnit_EquipItem(luigi, EQUIP_MARIO | EQUIP_RESET, ITEM_NULL);
        BtlUnit_SetParamFromPouch(luigi);
    }
    partner = firstPartner(1);
    if (partner != 0) {
        const BattleUnitType kind = BattleTransPartyIdToUnitKind(partner);
        BattleUnit* party = BtlUnit_Entry(&entryunit_party[kind - UNIT_GOOMBELLA]);
        if (party != NULL) {
            f32 x, y, z;
            BtlUnit_GetHomePos(party, &x, &y, &z);
            BtlUnit_SetHomePos(party, 140.0f, y, 5.0f);
            BtlUnit_SetPos(party, 140.0f, y, 5.0f);
            BtlUnit_EquipItem(party, EQUIP_PARTNER | EQUIP_RESET, ITEM_NULL);
            BtlUnit_SetParamFromPouch(party);
        }
    }
    setSide(0);
}

/* BattleInformationSetParty: Mario's first partner comes out, whoever is out in the field */
static int setPartyPre(FieldBattleInfo** info, BattleUnitType* party) {
    (void)info;
    if (sState == STATE_FIGHT) {
        const int first = firstPartner(0);
        *party = first != 0 ? BattleTransPartyIdToUnitKind(first) : (BattleUnitType)0;
    }
    return 0;
}

/* BattleSetSeq / BattleIncSeq: the side follows the phase (Luigi's from the
 * phase after Mario's to the enemies'), and Mario's at the end */
static void seqChanged(BattleWork* wp, BattleSequence seq) {
    if (!fight()) {
        return;
    }
    if (seq == SEQ_PHASE) {
        const u32 phase = BattleGetSeq(wp, SEQ_PHASE);
        setSide(phase == BATTLE_PHASE_AFTER_PLAYER || phase == BATTLE_PHASE_ENEMY);
    } else if (seq == SEQ_END || seq == SEQ_FIRST_ACT || seq == SEQ_TURN) {
        setSide(0);
    }
}

static void setSeqPost(BattleWork* wp, BattleSequence seq, u32 value) {
    (void)value;
    seqChanged(wp, seq);
}

static void incSeqPost(BattleWork* wp, BattleSequence seq) {
    seqChanged(wp, seq);
}

/* BtlUnit_GetACPossibility: Luigi's side guards too */
static int acPossiblePre(BOOL* r, BattleUnit** unit) {
    BattleUnit* u = *unit;
    if (!fight() || u == NULL || u->alliance != 1 || !playerKind(u)) {
        return 0;
    }
    *r = !(BtlUnit_CheckStatus(u, STATUS_SLEEP) || BtlUnit_CheckStatus(u, STATUS_STOP) ||
           BtlUnit_CheckStatus(u, STATUS_FREEZE) || BtlUnit_CheckStatus(u, STATUS_INSTANT_KILL) ||
           BtlUnit_CheckStatusFlag(u, 1));
    return 1;
}

/* BattleCheckDamage: a hit from the other side is guarded (or Superguarded)
 * with the defender's own timing, as an enemy's attack script does with
 * btlevtcmd_ResultACDefence just before its hit; the attacker's action
 * command record is kept through it */
static int checkDamagePre(s32* r, BattleUnit** attacker, BattleUnit** target, BattleUnitPart** part,
                          BattleWeapon** weapon, s32* flags) {
    BattleWork* wp = _battleWorkPointer;
    BattleWorkPad kept;
    (void)r;
    (void)part;
    (void)flags;
    if (!fight() || *attacker == NULL || *target == NULL || *weapon == NULL) {
        return 0;
    }
    if ((*attacker)->alliance == (*target)->alliance || !playerKind(*attacker) || !playerKind(*target)) {
        return 0;
    }
    kept = wp->actionCommands.pad;
    BattleActionCommandCheckDefence(*target, *weapon);
    wp->actionCommands.pad = kept;
    return 0;
}

static BattleWorkPad sNoPad; /* one controller: the defender can't guard (it is the attacker's) */

void* portVersusGuardPad(const void* unit, int* handled) {
    (void)unit;
    if (!fight()) {
        *handled = 0;
        return NULL;
    }
    *handled = 1;
    if (!twoPads()) {
        memset(&sNoPad, 0, sizeof(sNoPad));
        return &sNoPad;
    }
    /* the side that isn't acting: its controller is the second one in both phases (portVersusRoute) */
    return &_battleWorkPointer->padWork[1];
}

/* statusWinMain: the game's HUD is Mario's side's, whoever acts */
static int sHudHeld;

static int statusWinPre(void) {
    if (fight() && sSide == 1) {
        sHudHeld = 1;
        livePouch(0);
        sNoRedirect = 1;
    }
    return 0;
}

/* Luigi's side's HUD: the game's drawn again below it with his values and
 * his face; the turn timer under it */
static void versusHud(CameraId camId, void* param) {
    const int left = portVersusTimeLeft();
    portLuigiFaceForce(1);
    portStatusWinDispOther(camId, param);
    portLuigiFaceForce(-1);
    if (left >= 0) {
        char t[16];
        const f32 k = 0.9f;
        snprintf(t, sizeof(t), "%d", left);
        winFontInit();
        winFontSet((Vec){-(f32)FontGetMessageWidth(t) * k / 2.0f, 40.0f, 0.0f}, (Vec){k, k, k},
                   left <= 5 ? (GXColor){0xFF, 0x50, 0x40, 0xFF} : (GXColor){0xFF, 0xFF, 0xFF, 0xFF}, "%s", t);
    }
}

static void statusWinPost(void) {
    if (sHudHeld) {
        sHudHeld = 0;
        sNoRedirect = 0;
        livePouch(1);
    }
    if (fight() && gp->inBattle) {
        const PouchData* p2 = sLive == 1 ? pouchGetPtr() : &sOther;
        BattleUnit* luigi = sideUnit(_battleWorkPointer, 0);
        BattleUnit* party = sideUnit(_battleWorkPointer, 1);
        StatusValue v;
        memset(&v, 0, sizeof(v));
        if (luigi == NULL) {
            return;
        }
        v.hp = (s16)luigi->currentHp;
        v.maxHp = (s16)luigi->maxHp;
        v.fp = (s16)luigi->currentFp;
        v.maxFp = (s16)luigi->maxFp;
        v.ap = p2->currentSP;
        v.maxAp = p2->maximumSP;
        if (party != NULL) {
            v.party = (s16)BattleTransPartyId(party->currentKind);
            v.partyHp = (s16)party->currentHp;
            v.partyMaxHp = (s16)party->maxHp;
        }
        portStatusWinSetOther(&v, 100.0f);
        dispEntry(CAMERA_2D, 500.0f, 0, versusHud, NULL);
    }
}

/* BattleStoreExp: no Star Points from a versus fight */
static int storeExpPre(BattleWork** wp, s32* exp) {
    (void)wp;
    (void)exp;
    return fight();
}

/* the turn timer: BattleCommandInit starts a command's clock; when it runs
 * out, BattleCommandInput ends with the turn passed (a script that does
 * nothing) and the menu put away */
static EVT_BEGIN(versus_time_up_evt)
    RETURN
EVT_END()

static int sTimeUp;

static int commandInitPre(BattleWork** wp) {
    (void)wp;
    sCmdStart = SDL_GetTicks();
    return 0;
}

static int commandInputPre(s32* r, void** work) {
    BattleWork* wp = (BattleWork*)*work;
    BattleUnit* unit;
    if (!fight() || sSetup.rules.turnTimer <= 0 ||
        SDL_GetTicks() - sCmdStart < (Uint64)sSetup.rules.turnTimer * 1000u) {
        return 0;
    }
    unit = BattleGetUnitPtr(wp, wp->activeUnitId);
    if (unit == NULL) {
        return 0;
    }
    unit->attackEventCode = versus_time_up_evt;
    unit->attackEventFlags = 0;
    wp->commandMenu.flags &= ~CMD_MENU_FLAG_SHOW_ALL_ENEMY_HP;
    wp->dispFlags &= ~BATTLE_DISP_FLAG_COMMAND_MENU;
    BattleStageOffLightInCommand(0, 0);
    sTimeUp = 1;
    printf("[versus] time's up for %s's side\n", sSide ? "Luigi" : "Mario");
    fflush(stdout);
    *r = 1;
    return 1;
}

/* the seconds left for the command being chosen, or -1 (no timer, not choosing) */
int portVersusTimeLeft(void) {
    Uint64 used;
    if (!fight() || sSetup.rules.turnTimer <= 0 || !(_battleWorkPointer->flags & BATTLE_FLAG_COMMAND_INPUT)) {
        return -1;
    }
    used = (SDL_GetTicks() - sCmdStart) / 1000u;
    return used >= (Uint64)sSetup.rules.turnTimer ? 0 : sSetup.rules.turnTimer - (int)used;
}

int portVersusRoute(void) {
    if (!fight() || gp == NULL || !gp->inBattle) {
        return 0;
    }
    if (sSide == 1 && twoPads()) {
        /* Luigi's player is the game's first controller; Mario's the second (his guards) */
#define SWAP(f)                 \
    do {                        \
        __typeof__(gp->f[0]) t = gp->f[0]; \
        gp->f[0] = gp->f[1];    \
        gp->f[1] = t;           \
    } while (0)
        SWAP(button);
        SWAP(buttonNew);
        SWAP(buttonRepeat);
        SWAP(buttonRepeatDelay);
        SWAP(buttonUp);
        SWAP(dirs);
        SWAP(dirsNew);
        SWAP(dirsRepeat);
        SWAP(dirsRepeatDelay);
        SWAP(stickX);
        SWAP(stickY);
        SWAP(substickX);
        SWAP(substickY);
        SWAP(triggerLeft);
        SWAP(triggerRight);
#undef SWAP
    }
    return 1;
}

/* Each frame (after marioStMain): a start asked from the page, once in the
 * field; the end of a fight once back in the field; a Game Over or the title
 * ends one. */
static void frameHook(void) {
    const int seq = seqGetSeq();
    static int sTested;
    /* $TTYD_VERSUS_TEST: a fight with the set-up as it is, 30 s in (past a $TTYD_MAP warp; tests) */
    if (!sTested && getenv("TTYD_VERSUS_TEST") != NULL && SDL_GetTicks() > 30000 && seq == SEQ_GAME && gp != NULL &&
        !gp->inBattle) {
        const char* why = NULL;
        sTested = 1;
        if (!portVersusStart(&why)) {
            printf("[versus] test not started: %s\n", why);
        }
    }
    if (__atomic_exchange_n(&sAsked, 0, __ATOMIC_SEQ_CST)) {
        sState = STATE_ASKED;
    }
    if (sState == STATE_ASKED && seq == SEQ_GAME && gp != NULL && !gp->inBattle) {
        snprintf(sReturnMap, sizeof(sReturnMap), "%s", gp->currentMapName);
        sState = STATE_GOING;
        printf("[versus] to the Glitz Pit (back to %s after)\n", sReturnMap);
        fflush(stdout);
        seqSetSeq(SEQ_MAP_CHANGE, "tou_m2", NULL);
    }
    if ((sState == STATE_FIGHT || sState == STATE_GOING) && (seq == SEQ_GAME_OVER || seq == SEQ_TITLE)) {
        restorePouch();
        sState = STATE_NONE;
    }
    if (sState == STATE_OVER && seq == SEQ_GAME && gp != NULL && strcmp(gp->currentMapName, "tou_m2") != 0) {
        sState = STATE_NONE;
    }
}

void portVersusRegisterMaps(void) {
    portModsAddHostMap("tou_m2", versus_map_init, "tou_03");
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portModHostAddHook(0, kSlot_BattleGetMarioPtr, (void*)marioPre);
    portModHostAddHook(0, kSlot_BattleGetPartyPtr, (void*)partyPre);
    portModHostAddHook(0, kSlot_BtlUnit_Entry, (void*)entryPre);
    portModHostAddHook(1, kSlot_Btl_UnitSetup, (void*)unitSetupPost);
    portModHostAddHook(0, kSlot_BattleInformationSetParty, (void*)setPartyPre);
    portModHostAddHook(1, kSlot_BattleSetSeq, (void*)setSeqPost);
    portModHostAddHook(1, kSlot_BattleIncSeq, (void*)incSeqPost);
    portModHostAddHook(0, kSlot_BtlUnit_GetACPossibility, (void*)acPossiblePre);
    portModHostAddHook(0, kSlot_BattleCheckDamage, (void*)checkDamagePre);
    portModHostAddHook(0, kSlot_statusWinMain, (void*)statusWinPre);
    portModHostAddHook(1, kSlot_statusWinMain, (void*)statusWinPost);
    portModHostAddHook(0, kSlot_BattleStoreExp, (void*)storeExpPre);
    portModHostAddHook(0, kSlot_BattleCommandInit, (void*)commandInitPre);
    portModHostAddHook(0, kSlot_BattleCommandInput, (void*)commandInputPre);
}
