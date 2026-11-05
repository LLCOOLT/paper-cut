/* Enhancements and accessibility options (port/include/port/gameplus.h): every
 * one off by default, so the game plays as the original until a setting says
 * otherwise; any build takes $TTYD_SET_<key> for a run. */
#include <stdio.h>
#include "port/gameplus.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/companion.h"
#include "port/settings.h"
#include "mario/mario_pouch.h"
#include "battle/battle_mario.h"
#include "data/item_data.h"
#include "port/savestate.h"
#include "system.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "battle/battle_monosiri.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/seqdrv.h"
#include "mario/mariost.h"
#include "manager/cardmgr.h"
#include "win/win_main.h"
#include <string.h>
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "evt/evt_cmd.h"
#include <dolphin/pad.h>

/* every Enhancements setting: key, default, range */
static const struct {
    const char* key;
    int def, min, max;
} kSettings[] = {
    {"quick_swap", 0, 0, 1},
    {"bigger_bag", 0, 0, 1},
    {"partner_revive", 0, 0, 1},
    {"levelup_dpad", 0, 0, 1},
    {"heal_all_partners", 0, 0, 1},
    {"save_no_prompt", 0, 0, 1},
    {"difficulty", 0, 0, 3},
    {"difficulty_enemy_hp", 100, 25, 300},
    {"difficulty_enemy_attack", 100, 25, 300},
    {"ac_attack_assist", 0, 0, 2},
    {"ac_guard_assist", 0, 0, 2},
    {"remake_balance", 0, 0, 1},
    {"fast_battle_menus", 0, 0, 1},
    {"gameover_retry", 0, 0, 1},
    {"skip_scenes", 0, 0, 1},
    {"fast_travel", 0, 0, 1},
    {"new_game_plus", 0, 0, 3},
    {"pit_plus", 0, 0, 1},
    {"glitz_pit_kind", 0, 0, 1},
    {"battle_popups", 0, 0, 1},
    {"free_tattle", 0, 0, 1},
    {"playable_luigi", 0, 0, 3}, /* Play as: 0 Mario, 1 Luigi, 2 Peach, 3 Bowser */
    {"new_game_plus_from", 1, 1, 4},
};

static int sDeclared;

void portGameplusDeclare(void) {
    size_t i;
    if (sDeclared) {
        return;
    }
    sDeclared = 1;
    for (i = 0; i < sizeof(kSettings) / sizeof(kSettings[0]); i++) {
        portSettingsDeclare(kSettings[i].key, kSettings[i].def, kSettings[i].min, kSettings[i].max);
    }
    portGameplus2Declare();
}

int portGameplus(const char* key) {
    int* v;
    portGameplusDeclare();
    v = portSettingsFind(key);
    return v != NULL ? *v : 0;
}

/* --- Partner quick-swap --- */

int (*gPortQuickSwapHeld)(void);
int gPortQuickSwapTouch;
int gPortPartnerWheelOwns, gPortPartnerWheelOpen;
static int sSwapPressed, sSwapHeld, sSwapCoop;

void portPartnerWheelPad(PADStatus* pads) {
    /* the wheel's choosing stick isn't Mario's walk (this game's own player,
     * before online's exchange: what goes out is what this game plays) */
    if (gPortPartnerWheelOpen) {
        pads[0].stickX = 0;
        pads[0].stickY = 0;
    }
}

void portQuickSwapPress(void) {
    sSwapPressed = 1;
}

void portQuickSwapPressCoop(void) {
    sSwapPressed = 1;
    sSwapCoop = 1;
}

/* the partner menu's order (win_party.c winPartyDt): Goombella, Koops,
 * Flurrie, Yoshi, Vivian, Bobbery, Ms. Mowz */
const int gPortPartnerMenuOrder[PORT_PARTNER_MENU_COUNT] = {1, 2, 5, 4, 6, 3, 7};

int portQuickSwapPoll(int current) {
    /* (with the partner wheel on, the control is the wheel's: a tap there is a press here) */
    const int held = !gPortPartnerWheelOwns && ((gPortQuickSwapHeld != NULL && gPortQuickSwapHeld()) || gPortQuickSwapTouch);
    PouchData* pouch;
    int i, at = -1, coop;
    if (held && !sSwapHeld) {
        sSwapPressed = 1;
    }
    sSwapHeld = held;
    pouch = pouchGetPtr();
    /* a partner tapped on the second screen (companion_pc.c): that one */
    at = portCompanionPartner();
    if (at > 0 && at <= 7) {
        const u16 flags = pouch->partyData[at].flags;
        if (at != current && (flags & POUCH_PARTY_FLAG_JOINED) && !(flags & POUCH_PARTY_FLAG_AWAY)) {
            printf("[companion] partner %d to %d\n", current, at);
            fflush(stdout);
            return at;
        }
    }
    at = -1;
    if (!sSwapPressed) {
        return -1;
    }
    sSwapPressed = 0;
    coop = sSwapCoop; /* the second player's: whether the Enhancements tab's quick-swap is on or not */
    sSwapCoop = 0;
    if (!portGameplus("quick_swap") && !coop) {
        return -1;
    }
    for (i = 0; i < PORT_PARTNER_MENU_COUNT; i++) {
        if (gPortPartnerMenuOrder[i] == current) {
            at = i;
        }
    }
    for (i = 1; i <= PORT_PARTNER_MENU_COUNT; i++) {
        const int id = gPortPartnerMenuOrder[(at + i) % PORT_PARTNER_MENU_COUNT];
        const u16 flags = pouch->partyData[id].flags;
        if (id != current && (flags & POUCH_PARTY_FLAG_JOINED) && !(flags & POUCH_PARTY_FLAG_AWAY)) {
            printf("[gameplus] quick-swap: partner %d to %d\n", current, id);
            fflush(stdout);
            return id;
        }
    }
    return -1;
}

/* --- Pouch --- */

int portHeldItemBase(void) {
    return portGameplus("bigger_bag") ? MAX_HELD_ITEMS : 10;
}

static int joined(const PouchPartyData* p) {
    return (p->flags & POUCH_PARTY_FLAG_JOINED) != 0;
}

void portPartyReviveAfterBattle(void) {
    PouchData* pouch = pouchGetPtr();
    int i;
    if (!portGameplus("partner_revive")) {
        return;
    }
    for (i = 1; i < 8; i++) {
        if (joined(&pouch->partyData[i]) && pouch->partyData[i].currentHP <= 0) {
            pouch->partyData[i].currentHP = 1;
        }
    }
}

void portHealOtherPartners(int target, int hp) {
    PouchData* pouch = pouchGetPtr();
    int i;
    if (!portGameplus("heal_all_partners") || hp <= 0) {
        return;
    }
    for (i = 1; i < 8; i++) {
        if (i != target && joined(&pouch->partyData[i])) {
            pouchSetPartyHP(i, pouchGetPartyHP(i) + hp);
        }
    }
}

/* --- Difficulty --- */

/* the difficulty's percentages: enemy HP (which = 0) or attack (1) */
static int difficultyPercent(int which) {
    static const int kPresets[3][2] = {{100, 100}, {75, 50}, {150, 150}};
    const int d = portGameplus("difficulty");
    if (d == 3) {
        return portGameplus(which == 0 ? "difficulty_enemy_hp" : "difficulty_enemy_attack");
    }
    return kPresets[d][which];
}

static int scaled(int v, int percent) {
    int r;
    if (percent == 100 || v <= 0) {
        return v;
    }
    r = (v * percent + 50) / 100;
    return r < 1 ? 1 : r;
}

int portEnemyHp(int hp) {
    const int r = scaled(hp, difficultyPercent(0));
    if (r != hp) {
        printf("[gameplus] difficulty: enemy HP %d -> %d\n", hp, r);
        fflush(stdout);
    }
    return r;
}

int portEnemyDamage(int damage) {
    return scaled(damage, difficultyPercent(1));
}

/* --- Remake balance --- */

float portStarPowerScale(void) {
    return portGameplus("remake_balance") ? 1.5f : 1.0f;
}

int portPowerBounceCount(int count) {
    return portGameplus("remake_balance") ? count / 2 : count;
}

/* the data the remake balance changes, kept as the game had it */
static void remakeData(int on) {
    static int sApplied, sFirstAttackBp;
    if (on == sApplied) {
        return;
    }
    if (on) {
        sFirstAttackBp = itemDataTable[ITEM_FIRST_ATTACK].bp_cost;
        itemDataTable[ITEM_FIRST_ATTACK].bp_cost = 0;
        badgeWeapon_HammerNageru.special |= SPECIAL_PIERCES_DEFENSE;
    } else {
        itemDataTable[ITEM_FIRST_ATTACK].bp_cost = (s8)sFirstAttackBp;
        badgeWeapon_HammerNageru.special &= ~SPECIAL_PIERCES_DEFENSE;
    }
    sApplied = on;
}

static void fastTravelFrame(void);

void portGameplusFrame(void) {
    remakeData(portGameplus("remake_balance"));
    fastTravelFrame();
}

/* --- Faster battle menus --- */

int portBattleMenuStep(void) {
    return portGameplus("fast_battle_menus") ? 2 : 1;
}

/* --- Retry on Game Over --- */

/* port/src/aurora/savestate_aurora.c; the headless build has no save states */
__attribute__((weak)) int portSaveStateHas(int slot) {
    (void)slot;
    return 0;
}
__attribute__((weak)) void portSaveStateNotice(const char* text) {
    (void)text;
}

/* port/src/aurora/remix_aurora.c: a hardcore file (one life) has no retry */
__attribute__((weak)) int portHardcoreActive(void) {
    return 0;
}

static int sRetryLoading; /* frames left for the requested load to happen */

void portRetryPoint(void) {
    sRetryLoading = 0;
    if (portGameplus("gameover_retry") && !portHardcoreActive()) {
        portSaveStateRequest(PORT_SAVE_STATE_RETRY, 0);
    }
}

int portRetryOffer(void) {
    if (!portGameplus("gameover_retry") || !portSaveStateHas(PORT_SAVE_STATE_RETRY) || portHardcoreActive()) {
        return 0;
    }
    if (sRetryLoading > 0) {
        sRetryLoading--; /* loads at the end of the frame; if it is refused, the screen goes on */
        return 1;
    }
    portSaveStateNotice("Y: try again from the last room or battle");
    if (keyGetButtonTrg(0) & PAD_BUTTON_Y) {
        sRetryLoading = 30;
        portSaveStateRequest(PORT_SAVE_STATE_RETRY, 1);
        return 1;
    }
    return 0;
}

/* --- Skip repeated scenes --- */

static int crystalStars(void);

static int sceneSeen(int scene) {
    int i, stars = 0;
    switch (scene) {
        case PORT_SCENE_CRYSTAL_STAR:
            for (i = ITEM_DIAMOND_STAR; i <= ITEM_CRYSTAL_STAR; i++) {
                stars += pouchCheckItem(i) != 0;
            }
            return stars >= 2; /* this one is already in the pouch */
        case PORT_SCENE_COOKING:
            return swGet(GSWF_RECIPE_LOG_UNLOCKED); /* set after her first dish */
        case PORT_SCENE_PEDESTAL:
            return crystalStars() >= 2; /* the first star's scene was seen */
        case PORT_SCENE_GLITZ_OPENING:
        case PORT_SCENE_GLITZ_WIN:
            return swGet(GSWF_TOU_LAST_OPPONENT_VALID); /* a match was fought (set when one ends) */
        case PORT_SCENE_TRAIN_TICKET:
            return swStoryPeek() >= STORY_STG6_01_THE_CONDUCTOR;
        case PORT_SCENE_BLIMP_TICKET:
            return swStoryPeek() >= STORY_STG3_00_WELCOME_TO_GLITZVILLE;
    }
    return 0;
}

int portEvtSceneMs(struct EventEntry* event, int isFirstCall) {
    EvtWord* args = event->args;
    const int scene = evtGetValue(event, args[0]);
    int ms = evtGetValue(event, args[1]);
    (void)isFirstCall;
    if (portGameplus("skip_scenes") && sceneSeen(scene)) {
        ms = scene == PORT_SCENE_CRYSTAL_STAR ? 2500 : scene == PORT_SCENE_PEDESTAL ? 1500 : 0;
    }
    evtSetValue(event, args[2], ms);
    return EVT_RETURN_DONE;
}

/* --- Journal --- */

int portJournal(PortJournal* out) {
    const s32 seq = (s32)seqGetSeq();
    PouchData* pouch = pouchGetPtr();
    int i, n;
    memset(out, 0, sizeof(*out));
    if ((seq != SEQ_GAME && seq != SEQ_BATTLE) || pouch == NULL) {
        return 0;
    }
    out->story = swStoryPeek();
    out->troublesTotal = 30;
    for (i = 1; i <= 30; i++) {
        if (swGet(GSWF_TROUBLE_SOLVED_BASE + i)) {
            out->troubles++;
        } else {
            n = (int)strlen(out->troublesOpen);
            snprintf(out->troublesOpen + n, sizeof(out->troublesOpen) - n, "%s%d", n ? ", " : "", i);
        }
    }
    /* as the Journal counts them (win_log.c) */
    for (i = BADGE_MIN; i < 0x153; i++) {
        if (itemDataTable[i].type_sort_order != -1) {
            out->badgesTotal++;
            out->badges += swGet(i - BADGE_MIN + GSWF_BADGE_FOUND_BASE) != 0;
        }
    }
    for (i = WIN_RECIPE_MIN; i < ITEM_MAX; i++) {
        if (itemDataTable[i].type_sort_order != -1) {
            out->recipesTotal++;
            out->recipes += swGet(WIN_RECIPE_COOKED_SW(i - WIN_RECIPE_MIN)) != 0;
        }
    }
    for (i = 1; i < 0xD8; i++) {
        if (!battleGetUnitMonosiriPtr(i)->tattleLogTag || i == UNIT_BELDAM_CHAPTER2 || i == UNIT_MARILYN_CHAPTER2 ||
            i == UNIT_DOOPLISS_CHAPTER4_FIGHT1 || i == UNIT_BOWSER_CHAPTER3) {
            continue;
        }
        out->tattlesTotal++;
        out->tattles += swGet(i + GSWF_TATTLE_BASE) != 0;
    }
    out->starPieces = pouch->starPieceCount;
    out->shineSprites = pouch->shineSpriteCount;
    for (i = ITEM_DIAMOND_STAR; i <= ITEM_CRYSTAL_STAR; i++) {
        out->crystalStars += pouchCheckItem(i) != 0;
    }
    return 1;
}

/* --- Fast travel --- */

static const struct {
    const char* name;
    const char* map;
} kHubs[] = {
    {"Rogueport", "gor_01"},       {"Petalburg", "nok_00"},     {"Boggly Woods", "win_00"},
    {"Glitzville", "tou_01"},      {"Twilight Town", "usu_00"}, {"Keelhaul Key", "muj_01"},
    {"Poshley Heights", "pik_00"}, {"Fahr Outpost", "bom_01"},
};
static int sTravelTo = -1;

int portFastTravelCount(void) {
    return (int)(sizeof(kHubs) / sizeof(kHubs[0]));
}

const char* portFastTravelName(int i) {
    return i >= 0 && i < portFastTravelCount() ? kHubs[i].name : "";
}

static int crystalStars(void) {
    int i, n = 0;
    for (i = ITEM_DIAMOND_STAR; i <= ITEM_CRYSTAL_STAR; i++) {
        n += pouchCheckItem(i) != 0;
    }
    return n;
}

int portFastTravelUnlocked(int i) {
    if (!portGameplus("fast_travel") || i < 0 || i >= portFastTravelCount() || seqGetSeq() != SEQ_GAME ||
        pouchGetPtr() == NULL) {
        return 0;
    }
    /* Rogueport once the prologue is over (Goombella has joined) */
    return i == 0 ? swByteGet(GSW_STORY_PROGRESS) >= STORY_STG0_07_GOOMBELLA_JOINS : crystalStars() >= i;
}

void portFastTravelRequest(int i) {
    if (portFastTravelUnlocked(i) && !portOnlineActive() && !portReplayActive()) { /* online, replays: one game alone can't travel */
        sTravelTo = i;
    }
}

static void fastTravelFrame(void) {
    if (sTravelTo < 0) {
        return;
    }
    if (seqGetSeq() != SEQ_GAME) {
        sTravelTo = -1; /* a battle or the title: dropped */
        return;
    }
    if (seqCheckSeq() || marioStGetSystemLevel() != 0) {
        return; /* a scene or a menu: wait for it */
    }
    printf("[gameplus] fast travel: %s\n", kHubs[sTravelTo].map);
    fflush(stdout);
    seqSetSeq(SEQ_MAP_CHANGE, kHubs[sTravelTo].map, NULL);
    sTravelTo = -1;
}

/* --- New Game+ --- */

void portNewGamePlus(void) {
    const int mode = portGameplus("new_game_plus");
    CardData* card = cardGetFilePtr();
    const CardSaveSlot* from;
    const PouchData* old;
    PouchData* pouch = pouchGetPtr();
    int i;
    portGameplus2NewFile();
    if (mode == 0 || card == NULL || pouch == NULL) {
        return;
    }
    from = &card->saves[portGameplus("new_game_plus_from") - 1];
    if (from->data.flags & (CARD_SAVE_FLAG_EMPTY | CARD_SAVE_FLAG_CORRUPT)) {
        printf("[gameplus] new game+: file %d is empty\n", portGameplus("new_game_plus_from"));
        return;
    }
    old = &from->data.pouch;
    pouch->level = old->level;
    pouch->baseMaxHP = old->baseMaxHP;
    pouch->baseMaxFP = old->baseMaxFP;
    pouch->maximumBP = old->maximumBP;
    pouch->starPoints = old->starPoints;
    if (mode >= 2) {
        memcpy(pouch->badges, old->badges, sizeof(pouch->badges));
    }
    if (mode >= 3) {
        for (i = 1; i < 0xD8; i++) {
            const int f = GSWF_TATTLE_BASE + i;
            if (from->data.global.gswf[f / 32] & (1u << (f % 32))) {
                swSet(f);
            }
        }
    }
    pouchReviseMarioParam();
    pouch->currentHP = pouch->maximumHP;
    pouch->currentFP = pouch->maximumFP;
    printf("[gameplus] new game+: level %d from file %d\n", pouch->level, portGameplus("new_game_plus_from"));
    fflush(stdout);
}

/* --- Pit of 100 Trials --- */

int portEvtPitFloorCoins(struct EventEntry* event, int isFirstCall) {
    const int floor = evtGetValue(event, GSW(GSW_JON_FLOOR)); /* 0-based: 9, 19 ... are the rest floors */
    (void)isFirstCall;
    if (portGameplus("pit_plus") && floor % 10 != 9) {
        pouchAddCoin(floor / 10 + 1);
    }
    return EVT_RETURN_DONE;
}

int portEvtPitMover(struct EventEntry* event, int isFirstCall) {
    (void)isFirstCall;
    if (portGameplus("pit_plus") && evtGetValue(event, GSW(GSW_JON_DEEPEST_FLOOR)) >= 50) {
        evtSetValue(event, event->args[0], 0);
    }
    return EVT_RETURN_DONE;
}

/* --- Tattle without a turn --- */

static s32 sTattleShown[BATTLE_UNIT_MAX]; /* the kinds read so far this time */
static s32 sTattleShownCount;

/* the next living enemy already tattled whose kind wasn't read yet, or NULL */
static BattleUnit* tattleNext(void) {
    BattleWork* wp = _battleWorkPointer;
    s32 i, j;
    if (wp == NULL) {
        return NULL;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* unit = wp->units[i];
        if (unit == NULL || unit->alliance != 1 || unit->currentHp <= 0 ||
            !battleGetUnitMonosiriPtr(unit->currentKind)->tattleBtlTag || !battleCheckUnitMonosiriFlag(unit)) {
            continue;
        }
        for (j = 0; j < sTattleShownCount; j++) {
            if (sTattleShown[j] == unit->currentKind) {
                break;
            }
        }
        if (j == sTattleShownCount) {
            return unit;
        }
    }
    return NULL;
}

int portTattleFreeAvailable(void) {
    if (!portGameplus("free_tattle") || !(pouchGetPtr()->partyData[1].flags & POUCH_PARTY_FLAG_JOINED)) {
        return 0;
    }
    sTattleShownCount = 0;
    return tattleNext() != NULL;
}

void portTattleFreeBegin(void) {
    sTattleShownCount = 0;
}

int portEvtTattleFree(struct EventEntry* event, int isFirstCall) {
    BattleWork* wp = _battleWorkPointer;
    (void)isFirstCall;
    evtSetValue(event, event->args[0], wp != NULL && wp->commandMenu.disableReason == PORT_OPERATION_TATTLE);
    return EVT_RETURN_DONE;
}

int portEvtTattleNext(struct EventEntry* event, int isFirstCall) {
    BattleUnit* unit = tattleNext();
    (void)isFirstCall;
    if (unit != NULL && sTattleShownCount < BATTLE_UNIT_MAX) {
        sTattleShown[sTattleShownCount++] = unit->currentKind;
    }
    evtSetValue(event, event->args[0],
                unit != NULL ? (EvtWord)battleGetUnitMonosiriPtr(unit->currentKind)->tattleBtlTag : 0);
    return EVT_RETURN_DONE;
}
