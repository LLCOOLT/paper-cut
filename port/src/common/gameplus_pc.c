/* Enhancements and accessibility options (port/include/port/gameplus.h): every
 * one off by default, so the game plays as the original until a setting says
 * otherwise; any build takes $TTYD_SET_<key> for a run. */
#include <stdio.h>
#include <stdlib.h>
#include "port/achievements.h"
#include "port/gameplus.h"
#include "port/area_maps.h"
#include "port/fast_travel.h"
#include "port/rules.h"
#include "port/statesync.h"
#include "port/qol.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/companion.h"
#include "port/pm64.h"
#include "port/settings.h"
#include "port/toggles.h"
#include "port/open_world.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mario.h"
#include "battle/battle_mario.h"
#include "data/item_data.h"
#include "port/savestate.h"
#include "system.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "battle/battle_monosiri.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "driver/seqdrv.h"
#include "mario/mariost.h"
#include "manager/cardmgr.h"
#include "win/win_main.h"
#include <string.h>
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "evt/evt_cmd.h"
#include <dolphin/pad.h>

void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture (keeps the pointer) */

/* every Enhancements setting: key, default, range */
static const struct {
    const char* key;
    int def, min, max;
} kSettings[] = {
    {"quick_swap", 0, 0, 1},
    {"bigger_bag", 0, 0, 2}, /* Bag size: 0 original (10), 1 20, 2 the remake's 15 (20 with the Strange Sack) */
    {"partner_revive", 0, 0, 1},
    {"levelup_dpad", 0, 0, 1},
    {"heal_all_partners", 0, 0, 1},
    {"save_no_prompt", 0, 0, 1},
    {"difficulty", 0, 0, 3},
    {"difficulty_enemy_hp", 100, 25, 300},
    {"difficulty_enemy_attack", 100, 25, 300},
    {"ac_attack_assist", 0, 0, 3}, /* 3: by command type (port/include/port/dojo.h) */
    {"ac_guard_assist", 0, 0, 3},
    {"remake_balance", 0, 0, 1},
    {"fast_battle_menus", 0, 0, 1},
    {"gameover_retry", 0, 0, 1},
    {"skip_scenes", 0, 0, 2}, /* Skip scenes: 0 off, 1 those seen (the remake's), 2 always (the first time too) */
    {"fast_travel", 0, 0, 1},
    {"new_game_plus", 0, 0, 3},
    {"pit_plus", 0, 0, 1},
    {"glitz_pit_kind", 0, 0, 1},
    {"battle_popups", 0, 0, 1},
    {"free_tattle", 0, 0, 1},
    {"playable_luigi", 0, 0, 3}, /* Play as: 0 Mario, 1 Luigi, 2 Peach, 3 Bowser */
    {"luigi_high_jump", 1, 0, 1}, /* (on: it only acts while Luigi is the pick; luigi_jump_pc.c) */
    {"new_game_plus_from", 1, 1, 4},
    {"start_in_rogueport", 0, 0, 1}, /* (0.5.0's earlier switch: 1 now means rogueport_start Always) */
    {"rogueport_start", 0, 0, 2},    /* Start in Rogueport: 0 off (the original file menu), 1 offered, 2 always */
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
    portRulesDeclare(); /* rules_pc.c */
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
int gPortPartnerWheelOwns, gPortPartnerWheelOpen, gPortPartnerWheelPort;
static int sSwapPressed, sSwapHeld, sSwapCoop, sSwapPick;

void portPartnerWheelPad(PADStatus* pads) {
    /* the wheel's choosing stick isn't its player's walk (this game's own players,
     * before online's exchange: what goes out is what this game plays); only that
     * player's: player 2 holding it no longer stops Mario */
    if (gPortPartnerWheelOpen && gPortPartnerWheelPort >= 0 && gPortPartnerWheelPort < 4) {
        pads[gPortPartnerWheelPort].stickX = 0;
        pads[gPortPartnerWheelPort].stickY = 0;
    }
}

void portQuickSwapPress(void) {
    sSwapPressed = 1;
}

void portQuickSwapPressCoop(void) {
    sSwapPressed = 1;
    sSwapCoop = 1;
}

void portQuickSwapPickCoop(int member) {
    sSwapPick = member;
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
#ifndef TTYD_BACKEND_AURORA
    {
        /* tests: $TTYD_WHEEL_TEST=<id>[,...] with no Partner wheel to hold open (headless: the wheel is Aurora's
           ImGui, port/settings/features/partner_wheel.cpp): the same pick, once, as soon as the field is free */
        static int sTestId = -1, sTestPolls;
        if (sTestId < 0) {
            const char* t = getenv("TTYD_WHEEL_TEST");
            sTestId = t != NULL ? atoi(t) : 0;
        }
        if (sTestId > 0 && ++sTestPolls == 10) {
            at = sTestId;
            sTestId = 0;
            printf("[gameplus] TTYD_WHEEL_TEST: partner %d picked (no wheel)\n", at);
            fflush(stdout);
        }
    }
#endif
    if (at >= PM64_WHEEL_BASE) { /* a Paper Mario 64 partner from the wheel: on their host (pm64.h) */
        return portPm64WheelChoice(at, current);
    }
    if (at > 0 && at <= 7) {
        portPm64PartnerWear(at, 0); /* (the TTYD partner themselves) */
        const u16 flags = pouch->partyData[at].flags;
        if (at != current && (flags & POUCH_PARTY_FLAG_JOINED) && !(flags & POUCH_PARTY_FLAG_AWAY)) {
            printf("[companion] partner %d to %d\n", current, at);
            fflush(stdout);
            return at;
        }
        if (at != current) {
            portCoopNotice("Not now."); /* (not joined, or away: the second screen's tap said, not dropped) */
        }
    }
    at = sSwapPick; /* the player menu's Character strip (player 2's pick, second_player_pc.c) */
    sSwapPick = 0;
    if (at > 0 && at <= 7 && at != current && (pouch->partyData[at].flags & POUCH_PARTY_FLAG_JOINED) &&
        !(pouch->partyData[at].flags & POUCH_PARTY_FLAG_AWAY)) {
        printf("[gameplus] player 2's pick: partner %d to %d\n", current, at);
        fflush(stdout);
        return at;
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
    /* (with the Strange Sack every size is 20, MAX_HELD_ITEMS: the remake's 15 + 5) */
    switch (portGameplus("bigger_bag")) {
        case 1: return MAX_HELD_ITEMS;
        case 2: return 15;
    }
    return 10;
}

int portEvtHeldMax(struct EventEntry* event, int isFirstCall) {
    (void)isFirstCall;
    /* (as mario_pouch.c's pouchGetItem: the Strange Sack makes it MAX_HELD_ITEMS whatever the size) */
    evtSetValue(event, event->args[0], pouchCheckItem(ITEM_STRANGE_SACK) ? MAX_HELD_ITEMS : portHeldItemBase());
    return EVT_RETURN_DONE;
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
            printf("[gameplus] partner revive: partner %d to 1 HP\n", i);
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

/* a mode's own (portDifficultyOverride): in memory only, so no settings save, quit or crash keeps it */
static int sOverride[2];

void portDifficultyOverride(int hpPercent, int attackPercent) {
    sOverride[0] = hpPercent > 0 ? hpPercent : 0;
    sOverride[1] = attackPercent > 0 ? attackPercent : 0;
}

/* the difficulty's percentages: enemy HP (which = 0) or attack (1) */
static int difficultyPercent(int which) {
    static const int kPresets[3][2] = {{100, 100}, {75, 50}, {150, 150}};
    const int d = portGameplus("difficulty");
    if (sOverride[0] > 0 || sOverride[1] > 0) {
        return sOverride[which] > 0 ? sOverride[which] : 100;
    }
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
    static int sTold;
    const int r = scaled(damage, difficultyPercent(1));
    if (r != damage && !sTold) {
        sTold = 1;
        printf("[gameplus] difficulty: enemy attack %d -> %d\n", damage, r);
    }
    return r;
}

/* --- Remake balance --- */

/* The remake's Star Power by source: the crowd counts for more, the
 * action-command and Stylish bonuses, Danger/Peril and the BINGO boost for
 * less (its exact numbers aren't published: these are judged to its feel) */
int portStarPowerRemake(float* root, float* a, float* b, float* mm, float* pm, float* bingo) {
    if (!portGameplus("remake_balance")) {
        return 0;
    }
    *root *= 1.6f;
    *a *= 0.75f;
    *b *= 0.75f;
    *mm = 1.0f + (*mm - 1.0f) * 0.5f; /* Peril x3 -> x2, Danger x2 -> x1.5 */
    *pm = 1.0f + (*pm - 1.0f) * 0.5f;
    *bingo = 1.0f + (*bingo - 1.0f) * 0.5f;
    return 1;
}

int portPowerBounceCount(int count) {
    /* the remake's tighter Power Bounce / Multibonk: the window narrows twice as fast */
    return portGameplus("remake_balance") && count < 50000 ? count * 2 : count;
}

/* the data the remake balance changes, kept as the game had it */
static void remakeData(int on) {
    static int sApplied, sFirstAttackBp;
    static WeaponPowerFn sThrowFn;
    static u32 sThrowWork0;
    if (on == sApplied) {
        return;
    }
    if (on) {
        sFirstAttackBp = itemDataTable[ITEM_FIRST_ATTACK].bp_cost;
        itemDataTable[ITEM_FIRST_ATTACK].bp_cost = 0;
        badgeWeapon_HammerNageru.special |= SPECIAL_PIERCES_DEFENSE;
        /* Hammer Throw: a fixed 2, through defense */
        sThrowFn = badgeWeapon_HammerNageru.hpDamageFn;
        sThrowWork0 = badgeWeapon_HammerNageru.hpDamageWork[0];
        badgeWeapon_HammerNageru.hpDamageFn = weaponGetPowerDefault;
        badgeWeapon_HammerNageru.hpDamageWork[0] = 2;
    } else {
        itemDataTable[ITEM_FIRST_ATTACK].bp_cost = (s8)sFirstAttackBp;
        badgeWeapon_HammerNageru.special &= ~SPECIAL_PIERCES_DEFENSE;
        if (sThrowFn != NULL) {
            badgeWeapon_HammerNageru.hpDamageFn = sThrowFn;
            badgeWeapon_HammerNageru.hpDamageWork[0] = sThrowWork0;
        }
    }
    sApplied = on;
}

static void fastTravelFrame(void);

static void sceneSpeedFrame(void);

void portGameplusFrame(void) {
    remakeData(portGameplus("remake_balance"));
    portRulesFrame(); /* rules_pc.c: the remake's data and rules, each its own setting */
    fastTravelFrame();
    sceneSpeedFrame(); /* (Skip repeated scenes: a boss's intro after a Game Over) */
    portQolFrame(); /* the partner hint (port/include/port/qol.h) */
    portLoadoutFrame(); /* a loadout worn from the Start menu, once it is closed */
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
    if ((portGameplus("gameover_retry") || portBowserRetryRoom()) && !portHardcoreActive()) {
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
    if (scene >= PORT_SCENE_EVER) {
        return portSceneSeenEver(scene - PORT_SCENE_RECORDED, 0);
    }
    if (scene >= PORT_SCENE_RECORDED) {
        return portSceneSeen(scene - PORT_SCENE_RECORDED, 0);
    }
    return 0;
}

/* the scene is skipped (or cut short): the setting on 1 and the scene seen, or on 2 ("Always"). Every hook
 * leaves what the scene gives (items, flags, the story value) as the played scene does, and keeps the lines
 * that say where to go next (the pedestal's and the Crystal Star's are only waits cut short), so "Always"
 * is safe on a scene never seen. */
static int sceneSkipped(int scene) {
    const int v = portGameplus("skip_scenes");
    return v >= 2 || (v == 1 && sceneSeen(scene));
}

/* a seen scene's time, ms the full one */
static int sceneShortMs(int scene, int ms) {
    switch (scene) {
        case PORT_SCENE_CRYSTAL_STAR: return 2500;
        case PORT_SCENE_PEDESTAL: return 1500;
        case PORT_SCENE_INN_SPOT: return 1000;
        case PORT_SCENE_INN_DARK: return 300;
        case PORT_SCENE_UPGRADE: return 1500;
        case PORT_SCENE_PALACE_STANDS: return ms / 8;
        case PORT_SCENE_PALACE_GLOBE: return ms / 5;
    }
    (void)ms;
    return 0;
}

int portEvtSceneSkip(struct EventEntry* event, int isFirstCall) {
    EvtWord* args = event->args;
    const int scene = evtGetValue(event, args[0]);
    (void)isFirstCall;
    evtSetValue(event, args[1], sceneSkipped(scene) ||
                                    (scene == PORT_SCENE_EXPRESS && portPaceTrims())); /* (pace trims: every trip) */
    return EVT_RETURN_DONE;
}

/* A boss's intro run again after a Game Over: from the Game Over until a
 * battle on that map, the map's scenes (Mario not in control) play at 4x. */
static char sGameOverMap[16];
static int sGameOverStory = -1;

static void sceneSpeedFrame(void) {
    const s32 seq = (s32)seqGetSeq();
    if (seq == SEQ_GAME_OVER && pouchGetPtr() != NULL) {
        snprintf(sGameOverMap, sizeof(sGameOverMap), "%s", gp->currentMapName);
        sGameOverStory = swStoryPeek();
    } else if (seq == SEQ_BATTLE && sGameOverStory >= 0 && !strcmp(gp->currentMapName, sGameOverMap)) {
        sGameOverStory = -1; /* the fight began: the intro is over */
    }
}

int portSceneSpeedPct(void) {
    if (sGameOverStory < 0 || !portGameplus("skip_scenes") || portStateSyncLinked() || seqGetSeq() != SEQ_GAME ||
        pouchGetPtr() == NULL || swStoryPeek() != sGameOverStory || strcmp(gp->currentMapName, sGameOverMap) ||
        marioChkKey()) {
        return 100;
    }
    return 400;
}

int portEvtSceneSeen(struct EventEntry* event, int isFirstCall) {
    const int scene = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (scene >= PORT_SCENE_RECORDED) {
        portSceneSeen(scene - PORT_SCENE_RECORDED, 1);
    }
    return EVT_RETURN_DONE;
}

int portEvtSceneMs(struct EventEntry* event, int isFirstCall) {
    EvtWord* args = event->args;
    const int scene = evtGetValue(event, args[0]);
    int ms = evtGetValue(event, args[1]);
    (void)isFirstCall;
    if (sceneSkipped(scene)) {
        ms = sceneShortMs(scene, ms);
    }
    evtSetValue(event, args[2], ms);
    return EVT_RETURN_DONE;
}

/* --- Start in Rogueport: the whole opening skipped (aaa_00.c evt_prologue) --- */

static int sQuickStart = -1; /* the new file's pick: 1 Rogueport, 0 the beginning, -1 none */

/* the setting (0 off, 1 offered, 2 always); the earlier on/off switch's on is Always, once */
static int quickStartMode(void) {
    int* old;
    int* mode;
    portGameplusDeclare();
    old = portSettingsFind("start_in_rogueport");
    mode = portSettingsFind("rogueport_start");
    if (old != NULL && *old != 0 && mode != NULL) {
        *mode = 2;
        *old = 0;
        portSettingsSave();
    }
    return mode != NULL ? *mode : 0;
}

/* online, the host's game is the world (port/statesync.h): its new file starts in Rogueport as alone, the
   opening's state going to every guest with the rest of the host's; a guest's game never makes its own */
static int quickStartOnlineOk(void) {
    return !portStateSyncLinked() || ssNetIsHost();
}

/* (Offer and Always both show the new file's two ways in: Always only swaps which is {A}'s) */
int portQuickStartOffered(void) {
    return quickStartMode() != 0 && !portConsoleAccurate() && quickStartOnlineOk();
}

int portQuickStartDefault(void) {
    return quickStartMode() == 2 && !portConsoleAccurate() && quickStartOnlineOk();
}

void portQuickStartPick(int rogueport) {
    sQuickStart = rogueport;
}

/* What the played opening leaves at the moment the plaza hands Mario control (gor_01.c christine_nakama's end;
 * compared with a played-through save: tools/port/save_diff.py, tests/port/testpass/quickstart_*.json). Its
 * story values, from the letter (aaa_00) to Goombella joining (gor_01), set nothing else than: */
static void quickStartApply(void) {
    PouchData* pouch = pouchGetPtr();
    const int level = pouch->level;
    int sp;
    swSet(GSWF_TELOP_SHOWN_BASE + 0);              /* sys_01: the "Prologue" title card (evt_telop.c) */
    swSet(GSWF_GOR00_GOOMBELLA_ASSAULTED_SEEN);    /* gor_00: Goombella and Lord Crump's X-Nauts */
    swByteSet(GSW_STORY_PROGRESS, STORY_STG0_07_GOOMBELLA_JOINS);
    /* gor_00, the fight with Lord Crump (unit_boss_kanbu1: level 20, no bonus; one enemy): its star points, as
     * BtlUnit_GetExp and the Enhancements' multiplier give them (New Game+'s level: fewer, or none). The
     * battle's level-up is at its end screen, so a total of 100 or more is held at 99 */
    sp = level > 20 ? 0 : (int)(1.0f * (f32)(20 - level) / 2.0f);
    sp = portBattleExp(sp);
    pouch->starPoints = (s16)(pouch->starPoints + sp > 99 ? 99 : pouch->starPoints + sp);
    /* gor_01: she joins (evt_pouch_party_join's way) and is out (as a loaded save's partner: continueGame) */
    if (!portOpenPartyJoin(PARTNER_GOOMBELLA)) {
        partyJoin(PARTNER_GOOMBELLA);
    }
    marioPartyEntry(PARTNER_GOOMBELLA);
    printf("[gameplus] start in Rogueport: story %d, %d star points\n", swByteGet(GSW_STORY_PROGRESS),
           pouch->starPoints);
    fflush(stdout);
}

int portEvtQuickStart(struct EventEntry* event, int isFirstCall) {
    EvtWord* args = event->args;
    const int apply = evtGetValue(event, args[0]);
    /* (the pick alone, whatever the setting: the file menu's {A} or {Y} under Offer or Always, or the launcher's New
       adventure, which always offers it) */
    const int on = sQuickStart == 1 && !portConsoleAccurate() && quickStartOnlineOk() &&
                   swByteGet(GSW_STORY_PROGRESS) == 0;
    (void)isFirstCall;
    if (on && apply) {
        sQuickStart = -1;
        quickStartApply();
        portAchievementCounterMax("quick_start", 1); /* (Fast Forward, once chapter 1 is over) */
    }
    evtSetValue(event, args[1], on);
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
    {"Poshley Heights", "pik_00"}, {"Fahr Outpost", "bom_02"},
};

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

/* the hub's first way in Mario knows, or -1 (port/include/port/fast_travel.h's rules) */
static int hubWay(int i) {
    const int room = i >= 0 && i < portFastTravelCount() ? portAreaRoomFind(kHubs[i].map) : -1;
    int k;
    for (k = 0; room >= 0 && k < portAreaExitCount; k++) {
        if (portAreaExits[k].room == room && portTravelWayKnown(k)) {
            return k;
        }
    }
    return -1;
}

/* fast travel's rules (fast_travel_pc.c): Mario may leave, has been there and may arrive */
int portFastTravelUnlocked(int i) {
    return hubWay(i) >= 0 && portTravelDepartRefusal() == NULL &&
           portTravelArriveRefusal(portAreaExits[hubWay(i)].room) == NULL;
}

void portFastTravelRequest(int i) {
    /* (refused or not, through the trip's own checks: a refusal is logged with its reason, never dropped
       silently) */
    portTravelRequest(hubWay(i));
}

static void fastTravelFrame(void) {
    portTravelFrame();
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
    {
        /* tests: no Mover while $TTYD_MODERUN_PITCOOP runs the Pit (port/include/port/moderun.h) */
        const char* t = getenv("TTYD_MODERUN_PITCOOP");
        if (t != NULL && atoi(t) > 0) {
            evtSetValue(event, event->args[0], 1000);
        }
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
