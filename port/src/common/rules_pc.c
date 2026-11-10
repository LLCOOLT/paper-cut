/* The Switch remake's rule changes and the players' rule wishes
 * (port/include/port/rules.h): every one its own setting, off by default.
 * Where the remake's exact numbers aren't published, the values here are
 * judged to its feel and noted. */
#include <stdio.h>
#include <stdlib.h>
#include "port/rules.h"
#include "port/gameplus.h"
#include "port/settings.h"
#include "port/practice_more.h"
#include "port/practice_jump.h"
#include "port/mode_lending.h"
#include "mario/mario_pouch.h"
#include "data/item_data.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "evt/evt_badgeshop.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "battle/battle_item_data.h"
#include "battle/battle_database_common.h"
#include "driver/npcdrv.h"
#include <string.h>
#include "battle/battle_seq_command.h"
#include "battle/battle_mario.h"
#include "statuswindow.h"
#include "pmario_sound.h"
#include <dolphin/vi.h>
#include "battle/battle_sub.h"
#include "battle/battle_audience.h"
#include "battle/battle_disp.h"
#include "battle/battle_status_effect.h"
#include "driver/dispdrv.h"
#include "manager/fontmgr.h"
#include "manager/evtmgr_cmd.h"
#include "port/platform.h"
#include "system.h"
#include "driver/animdrv.h"
#include <dolphin/mtx.h>
#include "battle/battle_information.h"
#include "driver/camdrv.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "evt/evt_cmd.h"
#include "eff/eff_small_star.h"
#include "port/statesync.h"
#include "port/master_quest.h"
#include "port/tr.h"

static const struct {
    const char* key;
    int def, min, max;
} kRules[] = {
    /* the remake's item and badge data */
    {"rules_sleepy_sheep", 0, 0, 1},  /* Sleepy Sheep: 3 turns of sleep (was 5) */
    {"rules_power_lift", 0, 0, 1},    /* Power Lift: 4 arrows a point (was 5) */
    {"rules_sweet_treat", 0, 0, 1},   /* Sweet Treat: 2 Poison Mushrooms (was 3) */
    {"rules_mystery_box", 0, 0, 1},   /* Mystery: the remake's pool, better after chapter 4 */
    {"rules_lottery", 0, 0, 1},       /* the lottery's first prizes swapped (Lucky Day first) */
    {"rules_pit_shop", 0, 0, 1},      /* Charlieton in the Pit: Ultra Shroom and Jammin' Jelly too */
    {"rules_trouble_coins", 0, 0, 1}, /* a Trouble's 20-coin reward is 30 */
    {"rules_boss_coins", 0, 0, 1},    /* bosses pay 50/100/200 coins (Bonetail 999) */
    {"rules_flee_coins", 0, 0, 1},    /* running away drops no coins */
    {"rules_hazard_pierce", 0, 0, 1}, /* stage hazards go through defense */
    {"rules_deep_sleep", 0, 0, 1},    /* a hit wakes a sleeper 20 % of the time (was 50 %) */
    /* the remake's enemy rules */
    {"rules_bomb_no_sp", 0, 0, 1},       /* a Bob-omb that blows up (not knocked out) gives no Star Points */
    {"rules_buzzy_blast", 0, 0, 1},      /* Buzzies and Spike Tops are immune to explosions */
    {"rules_flower_fuzzy", 0, 0, 1},     /* Flower Fuzzies go for Mario (his FP) only */
    {"rules_bandit_returns", 0, 0, 1},   /* what a Bandit stole comes back when the battle ends */
    {"rules_spider_strike", 0, 0, 1},    /* Piders and Arantulas easier to First Strike */
    {"rules_swoopula_swoop", 0, 0, 1},   /* Swoopulas also Swoop: a plain hit, no drain */
    {"rules_chomp_still", 0, 0, 1},      /* Chain Chomps stop hopping while a jump is aimed at them */
    /* the battle screen's information (output only) */
    {"battle_fp_preview", 0, 0, 1},  /* the FP gauge blinks over a move that costs FP */
    {"battle_sp_grey", 0, 0, 1},     /* Special greyed when no Special is affordable */
    {"spring_jump_cue", 0, 0, 1},    /* a sound as Spring Jump's timing window opens */
    {"battle_immune_cross", 0, 0, 1}, /* statuses the target can't take crossed out over it */
    {"rules_audience_pause", 0, 0, 1}, /* the battle menu waits while the audience throws */
    {"rules_sp_fill_wait", 0, 0, 1},   /* the enemy turn waits for the Star Power fill */
    {"rules_doopliss", 0, 0, 1},       /* Doopliss: easier Run Away, a Life Shroom in the chapter 4 fight */
    {"rules_doopliss_shed", 0, 0, 1},  /* Doopliss's surprise attack in the Twilight Trail shed takes 1 HP */
    {"rules_glitz_conditions", 0, 0, 1}, /* Glitz Pit: Iron Adonises want a partner move, Rawk Hawk nothing, no SP from the cake */
    {"rules_tremor_stagehand", 0, 0, 1}, /* a stagehand carries a helpless partner away from a quake */
    {"less_flashing", 0, 0, 1},        /* flash effects at a quarter of their strength */
    /* bosses */
    {"rules_retattle", 0, 0, 1}, /* a boss's second fight hides its HP until tattled again */
    /* players' wishes */
    {"balanced_badges", 0, 0, 1},    /* one Power Rush / Mega Rush counts, Charge to +5, Power Lift to +3 */
    {"no_stage_hazards", 0, 0, 1},   /* attacks never bring the scenery down or set off the stage's jets */
    {"no_audience_throws", 0, 0, 1}, /* the audience never throws anything */
    {"less_shake", 0, 0, 1},         /* screen shakes at a third of their size */
    {"auto_win_weak", 0, 0, 1},      /* touching a field enemy 10 levels below Mario wins at once */
};

static int sDeclared;

void portRulesDeclare(void) {
    size_t i;
    if (sDeclared) {
        return;
    }
    sDeclared = 1;
    for (i = 0; i < sizeof(kRules) / sizeof(kRules[0]); i++) {
        portSettingsDeclare(kRules[i].key, kRules[i].def, kRules[i].min, kRules[i].max);
    }
}

static int on(const char* key) {
    return portGameplus(key) != 0;
}

static int crystalStars(void) {
    int i, n = 0;
    if (pouchGetPtr() == NULL) {
        return 0;
    }
    for (i = ITEM_DIAMOND_STAR; i <= ITEM_CRYSTAL_STAR; i++) {
        n += pouchCheckItem(i) != 0;
    }
    return n;
}

/* --- Remake item and badge data --- */

float portRulesPowerLiftStep(float step) {
    return on("rules_power_lift") ? 0.25f : step;
}

int portRulesSweetTreatPoisons(int count) {
    return on("rules_sweet_treat") && count > 2 ? 2 : count;
}

/* Mystery's roulette: no Dried Shroom; Thunder Bolt, Fire Flower and the
 * stronger attack items in; after the fourth Crystal Star the healing ones
 * grow (Ultra Shroom, Jammin' Jelly) and Thunder Bolt is Thunder Rage. Only
 * items Mystery's own script plays (battle_item_data.c ItemEvent_Naniga_Okorukana). */
static const int kMysteryEarly[] = {
    ITEM_MUSHROOM, ITEM_HONEY_SYRUP, ITEM_THUNDER_BOLT, ITEM_FIRE_FLOWER, ITEM_THUNDER_RAGE, ITEM_SHOOTING_STAR,
    ITEM_VOLT_SHROOM, ITEM_REPEL_CAPE, ITEM_STOPWATCH, ITEM_SLOW_SHROOM, ITEM_GRADUAL_SYRUP, ITEM_MUSHROOM, 0,
};
static const int kMysteryLate[] = {
    ITEM_ULTRA_SHROOM, ITEM_JAMMIN_JELLY, ITEM_THUNDER_RAGE, ITEM_FIRE_FLOWER, ITEM_THUNDER_RAGE, ITEM_SHOOTING_STAR,
    ITEM_VOLT_SHROOM, ITEM_REPEL_CAPE, ITEM_STOPWATCH, ITEM_SLOW_SHROOM, ITEM_GRADUAL_SYRUP, ITEM_SHOOTING_STAR, 0,
};

const int* portRulesMysteryTable(const int* original) {
    if (!on("rules_mystery_box")) {
        return original;
    }
    return crystalStars() >= 4 ? kMysteryLate : kMysteryEarly;
}

int portRulesLotteryPrize(int rank, int item) {
    if (!on("rules_lottery")) {
        return item;
    }
    if (item == ITEM_POWER_PLUS && rank == 0) {
        return ITEM_LUCKY_DAY;
    }
    if (item == ITEM_LUCKY_DAY && rank == 1) {
        return ITEM_POWER_PLUS;
    }
    return item;
}

int portRulesHazardPierces(struct BattleUnit* attacker) {
    return attacker != NULL && attacker->currentKind == UNIT_SYSTEM && on("rules_hazard_pierce");
}

int portRulesWakeChance(int percent) {
    return on("rules_deep_sleep") ? 20 : percent;
}

/* the data tables the rules change, kept as the game had them */
static void sleepySheep(int want) {
    static int sApplied;
    static s8 sTime;
    if (want == sApplied) {
        return;
    }
    if (want) {
        sTime = ItemWeaponData_Nemure_Yoikoyo.sleepTime;
        ItemWeaponData_Nemure_Yoikoyo.sleepTime = 3;
    } else {
        ItemWeaponData_Nemure_Yoikoyo.sleepTime = sTime;
    }
    sApplied = want;
}

/* Charlieton's Pit stock: the remake adds Ultra Shroom and Jammin' Jelly
 * (evt_badgeshop.c leaves two slots for them past the zero end) */
static void pitShop(int want) {
    static int sApplied;
    int n = 0;
    if (want == sApplied) {
        return;
    }
    while (badge_bottakuru100_table[n] != 0) {
        n++;
    }
    if (want) {
        badge_bottakuru100_table[n] = ITEM_ULTRA_SHROOM;
        badge_bottakuru100_table[n + 1] = ITEM_JAMMIN_JELLY;
        badge_bottakuru100_table[n + 2] = 0;
    } else if (n >= 2 && badge_bottakuru100_table[n - 1] == ITEM_JAMMIN_JELLY &&
               badge_bottakuru100_table[n - 2] == ITEM_ULTRA_SHROOM) {
        badge_bottakuru100_table[n - 2] = 0;
        badge_bottakuru100_table[n - 1] = 0;
    }
    sApplied = want;
}

/* A Trouble's reward of 20 coins becomes 30. The rewards are paid by evt_sub_get_coin (evt_sub.c, its
 * "iraiCoin" window: the Trouble Center's own, used by nothing else), which asks here as it starts, so the
 * window shows the 30 too. Before, a 20-coin gain within 10 s of any Trouble solved paid 10 more (an item
 * reward, then a sale for 20), and a real one was missed with the coins near the cap. */
int portRulesTroubleCoins(int coins) {
    return coins == 20 && on("rules_trouble_coins") ? 30 : coins; /* (asked on each of the window's frames) */
}

/* the coins a boss pays (chapter 1-2 bosses 50, 3-5 100, 6-8 200) */
static int bossCoins(int kind) {
    switch (kind) {
        case UNIT_BLOOPER:
        case UNIT_GOLD_FUZZY:
        case UNIT_HOOKTAIL:
        case UNIT_MAGNUS:
        case UNIT_BELDAM_CHAPTER2:
            return 50;
        case UNIT_MACHO_GRUBBA:
        case UNIT_RAWK_HAWK:
        case UNIT_DOOPLISS_CHAPTER4_FIGHT2:
        case UNIT_CORTEZ:
        case UNIT_LORD_CRUMP_CHAPTER5:
            return 100;
        case UNIT_SMORG:
        case UNIT_MAGNUS2_0:
        case UNIT_GLOOMTAIL:
        case UNIT_BELDAM_CHAPTER8:
        case UNIT_DOOPLISS_CHAPTER8:
        case UNIT_BOWSER_CHAPTER8:
        case UNIT_GRODUS:
            return 200;
        case UNIT_BONETAIL:
            return 999;
    }
    return 0;
}

static int sBossCoins; /* the most a boss in this battle pays */

static void bossSeen(void) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    if (seqGetSeq() != SEQ_BATTLE || wp == NULL) {
        sBossCoins = 0;
        return;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = wp->units[i];
        if (u != NULL && u->alliance == 1 && bossCoins(u->currentKind) > sBossCoins) {
            sBossCoins = bossCoins(u->currentKind);
        }
    }
}

void portRulesBattleWon(struct BattleWork* wp) {
    (void)wp;
    bossSeen();
#ifdef TTYD_BACKEND_AURORA
    if (portModeLending() != 0) {
        /* a boss refight, a rush step (or any mode's battle): the bounty is the story's, once a boss; paid on every
           refight, Macho Grubba, Smorg and Grodus at will were a coin farm the remake's one-time bounty isn't */
        sBossCoins = 0;
        return;
    }
#endif
    if (sBossCoins > 0 && on("rules_boss_coins")) {
        printf("[rules] boss coins: %d\n", sBossCoins);
        fflush(stdout);
        pouchAddCoin(sBossCoins);
    }
    sBossCoins = 0;
}

/* --- The remake's enemy rules --- */

int portRulesExp(struct BattleUnit* unit, int exp) {
    if (unit == NULL || !on("rules_bomb_no_sp")) {
        return exp;
    }
    switch (unit->currentKind) {
        case UNIT_BOB_OMB:
        case UNIT_BULKY_BOB_OMB:
        case UNIT_BOB_ULK:
            return unit->currentHp > 0 ? 0 : exp; /* blew up (its own blast or a chain), not knocked out */
    }
    return exp;
}

int portRulesDefenseAttr(struct BattleUnit* target, int element, int attr) {
    if (target == NULL || element != ELEMENT_EXPLOSION || !on("rules_buzzy_blast")) {
        return attr;
    }
    switch (target->currentKind) {
        case UNIT_BUZZY_BEETLE:
        case UNIT_SPIKE_TOP:
        case UNIT_PARABUZZY:
        case UNIT_SPIKY_PARABUZZY:
        case UNIT_RED_SPIKY_BUZZY:
            return 2; /* immune */
    }
    return attr;
}

static void stagehandTargets(BattleWorkWeaponTargets* d); /* the stagehand, below */

void portRulesTargets(BattleWorkWeaponTargets* d) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* attacker;
    int i, n = 0, mario = 0;
    stagehandTargets(d);
    if (wp == NULL || d->numTargets <= 0 || !on("rules_flower_fuzzy")) {
        return;
    }
    attacker = BattleGetUnitPtr(wp, d->unitId);
    if (attacker == NULL || attacker->currentKind != UNIT_FLOWER_FUZZY || attacker->alliance != 1) {
        return;
    }
    for (i = 0; i < d->numTargets; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, d->targets[d->targetIndices[i]].unitIdx);
        mario += u != NULL && u->currentKind == UNIT_MARIO;
    }
    if (!mario) {
        return; /* Mario out of reach: as the game had it */
    }
    for (i = 0; i < d->numTargets; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, d->targets[d->targetIndices[i]].unitIdx);
        if (u != NULL && u->currentKind == UNIT_MARIO) {
            d->targetIndices[n++] = d->targetIndices[i];
        }
    }
    d->numTargets = (s8)n;
}

int portRulesStolenBack(struct BattleUnit* unit) {
    BattleWork* wp = _battleWorkPointer;
    NpcBattleInfo* info;
    int i;
    if (!on("rules_bandit_returns") || unit == NULL || unit->heldItem == 0 || wp == NULL || wp->fbatInfo == NULL) {
        return 0;
    }
    if (unit->heldItem == ITEM_COIN) {
        pouchAddCoin(unit->stolenCoins > 0 ? unit->stolenCoins : 1);
        printf("[rules] Bandit: %d coins back\n", unit->stolenCoins);
    } else {
        info = wp->fbatInfo->setup;
        for (i = 0; i < 8 && info->recoveredItems[i] != 0; i++) {
        }
        if (i == 8) {
            return 0; /* no room: kept as the game keeps it */
        }
        info->recoveredItems[i] = unit->heldItem;
        printf("[rules] Bandit: item %d back\n", unit->heldItem);
    }
    fflush(stdout);
    unit->heldItem = 0;
    unit->stolenCoins = 0;
    return 1;
}

float portRulesFirstStrikeReach(struct NpcEntry* npc, float r) {
    if (npc == NULL || npc->tribe == NULL || npc->tribe->modelName == NULL || !on("rules_spider_strike")) {
        return r;
    }
    /* Pider (c_paid) and Arantula (c_paid_b): they hang high, so a wider reach */
    return strncmp(npc->tribe->modelName, "c_paid", 6) == 0 ? r + 20.0f : r;
}

/* the remake's Swoop: the bite's own data, without piercing (its power as
 * the bite's; a plain hit, so nothing is drained) */
int portEvtRuleSwoop(EventEntry* event, int isFirstCall) {
    static BattleWeapon sSwoop;
    const BattleWeapon* bite = (const BattleWeapon*)evtGetValue(event, event->args[0]);
    BattleWeapon* out = NULL;
    (void)isFirstCall;
    if (bite != NULL && on("rules_swoopula_swoop") && irand(100) < 50) {
        sSwoop = *bite;
        sSwoop.special &= ~SPECIAL_PIERCES_DEFENSE;
        out = &sSwoop;
        printf("[rules] Swoopula: Swoop\n");
        fflush(stdout);
    }
    evtSetValue(event, event->args[1], (EvtWord)out);
    return EVT_RETURN_DONE;
}

int portEvtRuleChompHold(EventEntry* event, int isFirstCall) {
    BattleWork* wp = _battleWorkPointer;
    int hold = 0;
    (void)isFirstCall;
    if (wp != NULL && on("rules_chomp_still")) {
        BattleWorkWeaponTargets* d = &wp->weaponTargets;
        BattleUnit* actor = BattleGetUnitPtr(wp, wp->activeUnitId);
        int self = BattleTransID(event, evtGetValue(event, event->args[0]));
        if (actor != NULL && actor->alliance == 0 && d->unitId == wp->activeUnitId && d->weapon != NULL &&
            (d->weapon->targetPropertyFlags & TARGET_PROP_JUMPLIKE) && d->numTargets > 0 && d->currentTarget >= 0 &&
            d->currentTarget < d->numTargets) {
            hold = d->targets[d->targetIndices[d->currentTarget]].unitIdx == self;
        }
    }
    evtSetValue(event, event->args[1], hold);
    return EVT_RETURN_DONE;
}

/* --- Bosses --- */

int portRulesRetattle(int kind) {
    switch (kind) {
        case UNIT_DOOPLISS_CHAPTER4_FIGHT2:
        case UNIT_DOOPLISS_CHAPTER8:
        case UNIT_BELDAM_CHAPTER8:
        case UNIT_MARILYN_CHAPTER8:
        case UNIT_BOWSER_CHAPTER8:
        case UNIT_SHADOW_QUEEN_DEMON:
            return on("rules_retattle");
    }
    return 0;
}

/* --- Players' wishes --- */

int portRulesBadgeStack(int count) {
    return on("balanced_badges") && count > 1 ? 1 : count;
}

int portRulesChargeCap(int strength) {
    return on("balanced_badges") && strength > 5 ? 5 : strength;
}

float portRulesPowerLiftMax(float max) {
    return on("balanced_badges") ? 3.0f : max;
}

int portRulesNoStageHazards(void) {
    static int told;
    if (!on("no_stage_hazards")) {
        return 0;
    }
    if (told < 3) { /* (the first few: the test pass's signal) */
        told++;
        printf("[rules] no stage hazards: an attack's aftereffect skipped\n");
        fflush(stdout);
    }
    return 1;
}

int portRulesNoAudienceThrows(void) {
    BattleWork* wp = _battleWorkPointer;
    /* a shared online battle: the crowd's throws are off on every screen. Each game's crowd rolls its own
     * throws, every frame of a command menu (battle_audience.c), and each game spends a different time in
     * the menu, so a mushroom or a rock landed on one screen only: player 1's snapshot then corrected
     * the HP on the others (a versus fight's 23 -> 25). */
    if (!on("no_audience_throws") && !portStateSyncSharedBattle()) {
        return 0;
    }
    /* Frankly's lessons still throw what they teach with */
    if (wp != NULL && (wp->flags & (BATTLE_FLAG_LECTURE_LOCK_ALL | BATTLE_FLAG_LECTURE_JUMP |
                                    BATTLE_FLAG_LECTURE_HAMMER | BATTLE_FLAG_LECTURE_SPECIAL |
                                    BATTLE_FLAG_LECTURE_MENU | BATTLE_FLAG_LECTURE_STEP2))) {
        return 0;
    }
    {
        static int told;
        if (told < 3) { /* (the first few: the test pass's signal) */
            told++;
            printf("[rules] no audience throws: a throw held back%s\n",
                   on("no_audience_throws") ? "" : " (shared battle)");
            fflush(stdout);
        }
    }
    return 1;
}

float portRulesShakeScale(void) {
    return on("less_shake") ? 0.33f : 1.0f;
}

/* --- Battle screen information --- */

int portRulesSpecialsShort(void) {
    PouchData* pouch = pouchGetPtr();
    int j;
    if (!on("battle_sp_grey") || pouch == NULL) {
        return 0;
    }
    for (j = 0; j < 16; j++) {
        if ((pouch->starPowersObtained & (1 << j)) && superActionTable[j] != NULL &&
            pouch->currentSP >= superActionTable[j]->baseSpCost * 100) {
            return 0;
        }
    }
    return 1;
}

void portRulesAcWindow(void* unit, int kind, int toJudge, int window) {
    static unsigned sCuedAt;
    BattleWork* wp = _battleWorkPointer;
    if (kind != 1 || toJudge < 0 || toJudge > window || wp == NULL || !on("spring_jump_cue")) {
        return;
    }
    if (unit != (void*)BattleGetMarioPtr(wp) || wp->weaponTargets.weapon != &marioWeapon_JyabaraJump) {
        return;
    }
    if (VIGetRetraceCount() - sCuedAt < 30) {
        return; /* once a window */
    }
    sCuedAt = VIGetRetraceCount();
    psndSFXOn("SFX_BTL_CURSOR_OK1");
}

/* the FP gauge blinks while the cursor is on a move that costs FP */
static void fpPreview(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleSubMenu* menu;
    if (seqGetSeq() != SEQ_BATTLE || wp == NULL || !on("battle_fp_preview")) {
        return;
    }
    menu = wp->commandMenu.subMenu;
    if (!(wp->commandMenu.flags & CMD_MENU_FLAG_WEAPON_SELECT) || menu == NULL || menu->weapons == NULL ||
        menu->cursor == NULL || menu->cursor->absPos < 0 || menu->cursor->absPos >= 21) {
        return;
    }
    {
        const BattleWeapon* w = menu->weapons[menu->cursor->absPos].weapon;
        if (w != NULL && w->baseFpCost > 0) {
            statusFPBlink(2);
        }
    }
}

/* --- Immunity crossed out ("battle_immune_cross") --- */

s32 portStatusIconId(s32 statusType); /* battle_status_icon.c */

/* the statuses a move can give and the target can't take (its vulnerability
 * 0), as status types; their count */
static int immuneStatuses(const BattleWeapon* w, const BattleUnit* t, int* out) {
    const StatusVulnerability* v;
    int n = 0;
    if (w == NULL || t == NULL || t->kindParams == NULL || t->kindParams->statusVulnerability == NULL) {
        return 0;
    }
    v = t->kindParams->statusVulnerability;
/* (only the statuses with a status icon: Fright, Gale Force and instant KO have none, and
 * portStatusIconId's -1 read past the battle menu's TPL: Bow's Spook on a Fright-immune enemy) */
#define IMMUNE(chance, field, type)          \
    if ((chance) > 0 && v->field == 0 && n < 6 && portStatusIconId(type) >= 0) { \
        out[n++] = (type);                   \
    }
    IMMUNE(w->sleepChance, sleep, STATUS_SLEEP)
    IMMUNE(w->stopChance, stop, STATUS_STOP)
    IMMUNE(w->dizzyChance, dizzy, STATUS_DIZZY)
    IMMUNE(w->poisonChance, poison, STATUS_POISON)
    IMMUNE(w->confuseChance, confuse, STATUS_CONFUSE)
    IMMUNE(w->burnChance, burn, STATUS_BURN)
    IMMUNE(w->freezeChance, freeze, STATUS_FREEZE)
    IMMUNE(w->sizeChangeStrength < 0 ? w->sizeChangeChance : 0, tiny, STATUS_TINY)
    IMMUNE(w->atkChangeStrength < 0 ? w->atkChangeChance : 0, attackDown, STATUS_ATTACK_DOWN)
    IMMUNE(w->defChangeStrength < 0 ? w->defChangeChance : 0, defenseDown, STATUS_DEFENSE_DOWN)
    IMMUNE(w->ohkoChance, ohko, STATUS_INSTANT_KILL)
    IMMUNE(w->frightChance, fright, STATUS_FRIGHT)
    IMMUNE(w->galeForceChance, galeForce, STATUS_GALE_FORCE)
#undef IMMUNE
    return n;
}

/* the targets the cursor is on while Mario's side picks one: the current one, or
 * every one for a move that hits them all at once (as the game's own target
 * display, battle_seq_command.c BattleCommandDisplay_TargetSelectMenuDisp) */
static int cursorTargets(BattleWork* wp, BattleWorkTarget** out, int max) {
    BattleWorkWeaponTargets* d;
    BattleUnit* actor;
    int i, n = 0;
    if (wp == NULL || !(wp->commandMenu.flags & CMD_MENU_FLAG_TARGET_SELECT)) {
        return 0;
    }
    d = &wp->weaponTargets;
    actor = BattleGetUnitPtr(wp, wp->activeUnitId);
    if (actor == NULL || actor->alliance != 0 || d->weapon == NULL || d->numTargets <= 0 || d->currentTarget < 0 ||
        d->currentTarget >= d->numTargets) {
        return 0;
    }
    if (d->weapon->targetClassFlags & TARGET_CLASS_SINGLE) {
        out[n++] = &d->targets[d->targetIndices[d->currentTarget]];
        return n;
    }
    for (i = 0; i < d->numTargets && n < max; i++) {
        out[n++] = &d->targets[d->targetIndices[i]];
    }
    return n;
}

static void immuneDisp(CameraId camId, void* param) {
    BattleWork* wp = _battleWorkPointer;
    BattleWorkTarget* targets[BATTLE_UNIT_MAX];
    const int count = cursorTargets(wp, targets, BATTLE_UNIT_MAX);
    const u32 seed = portRandState(); /* the font's random numbers kept (popups do the same) */
    int k;
    (void)camId;
    (void)param;
    for (k = 0; k < count; k++) {
        BattleUnit* t = BattleGetUnitPtr(wp, targets[k]->unitIdx);
        int st[6], n, i;
        Vec at, screen;
        f32 y;
        if (t == NULL || (n = immuneStatuses(wp->weaponTargets.weapon, t, st)) == 0) {
            continue;
        }
        /* over the hand cursor (drawn at the target's point, pointing down at it; under
         * it for a unit on the ceiling, whose hand is flipped) */
        at.x = targets[k]->posX;
        at.y = targets[k]->posY;
        at.z = targets[k]->posZ;
        btlGetScreenPoint(&at, &screen);
        y = (t->attributeFlags & UNIT_ATTR_CEILING) ? screen.y - 58.0f : screen.y + 58.0f;
        for (i = 0; i < n; i++) {
            Mtx m;
            const f32 x = screen.x + (i - (n - 1) * 0.5f) * 26.0f;
            PSMTXScale(m, 0.7f, 0.7f, 1.0f);
            m[0][3] = x;
            m[1][3] = y;
            m[2][3] = 0.0f;
            btlDispTexPlane2(m, (u32)portStatusIconId(st[i]), (GXColor){0xFF, 0xFF, 0xFF, 0xC0}, 0);
            FontDrawStart_alpha(0xFF);
            FontDrawEdge();
            FontDrawColor((GXColor){0xFF, 0x30, 0x30, 0xFF});
            FontDrawScale(1.0f);
            FontDrawString(x - FontGetMessageWidth("X") * 0.5f, y + 10.0f, "X");
        }
    }
    portRandSetState(seed);
}

static void immuneCross(void) {
    BattleWorkTarget* t[1];
    if (seqGetSeq() == SEQ_BATTLE && on("battle_immune_cross") && cursorTargets(_battleWorkPointer, t, 1) > 0) {
        dispEntry(CAMERA_2D, 903.0f, 1, immuneDisp, NULL);
    }
}

/* --- Inputs paused while the audience throws ("rules_audience_pause") --- */

void portRulesCommandInputPre(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleAudience* aud;
    int i;
    if (wp == NULL || !on("rules_audience_pause") || (aud = BattleAudienceBaseGetPtr()) == NULL) {
        return;
    }
    for (i = 0; i < AUDIENCE_ITEM_MAX; i++) {
        if (aud->items[i].flags & AUDIENCE_ITEM_FLAG_ACTIVE) {
            /* this frame's presses dropped: the menu waits for the throw */
            wp->padWork[0].buttonsPressedHistory[0] = 0;
            wp->padWork[0].buttonsRepeatHistory[0] = 0;
            return;
        }
    }
}

/* --- The enemy turn waits for the Star Power fill ("rules_sp_fill_wait") --- */

int portRulesWaitStarPower(void) {
    BattleAudience* aud;
    int i;
    if (!on("rules_sp_fill_wait") || (aud = BattleAudienceBaseGetPtr()) == NULL) {
        return 0;
    }
    for (i = 0; i < AUDIENCE_ITEM_MAX; i++) {
        if (aud->starpower[i].flags & AUDIENCE_STAR_FLAG_ACTIVE) {
            return 1;
        }
    }
    return 0;
}

/* --- Doopliss ("rules_doopliss") --- */

static int isDoopliss(int kind) {
    return kind == UNIT_DOOPLISS_CHAPTER4_FIGHT1 || kind == UNIT_DOOPLISS_CHAPTER4_INVINCIBLE ||
           kind == UNIT_DOOPLISS_CHAPTER4_FIGHT2;
}

int portRulesEscapeRate(int rate) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    if (wp == NULL || !on("rules_doopliss")) {
        return rate;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = wp->units[i];
        if (u != NULL && u->alliance == 1 && isDoopliss(u->currentKind)) {
            return rate < 90 ? 90 : rate; /* the gauge starts nearly full */
        }
    }
    return rate;
}

/* the chapter 4 Doopliss fight holds a Life Shroom (the game's own enemy
 * revive), given once a battle */
static void dooplissShroom(void) {
    static int sGiven;
    BattleWork* wp = _battleWorkPointer;
    int i;
    if (seqGetSeq() != SEQ_BATTLE || wp == NULL) {
        sGiven = 0;
        return;
    }
    if (sGiven || !on("rules_doopliss")) {
        return;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = wp->units[i];
        if (u != NULL && u->alliance == 1 && u->currentKind == UNIT_DOOPLISS_CHAPTER4_FIGHT2 && u->heldItem == 0) {
            u->heldItem = ITEM_LIFE_SHROOM;
            sGiven = 1;
            printf("[rules] Doopliss holds a Life Shroom\n");
            fflush(stdout);
        }
    }
}

/* Doopliss's surprise attack in the Twilight Trail shed (gra_00.c) hurts:
 * 1 HP, never Mario's last, with the field's hit sound and stars */
int portEvtRuleShedHit(EventEntry* event, int isFirstCall) {
    MarioWork* mario = marioGetPtr();
    int hit = 0;
    (void)isFirstCall;
    if (on("rules_doopliss_shed") && mario != NULL && pouchGetHP() > 1) {
        pouchAddHP(-1);
        psndSFXOn_3D("SFX_BTL_FLD_MARIO_DAMAGED1", &mario->position);
        effSmallStarEntry(3, 3, mario->position.x, mario->position.y + mario->wPlayerCollisionBox.y, mario->position.z,
                          0.0f, -1.0f, 0.0f);
        hit = 1;
        printf("[rules] Doopliss's surprise attack: 1 HP\n");
        fflush(stdout);
    }
    evtSetValue(event, event->args[0], hit);
    return EVT_RETURN_DONE;
}

/* --- The Glitz Pit ("rules_glitz_conditions") --- */

/* the fighters' ids (tou.c fighterDt) */
#define TOU_RAWK_HAWK 0
#define TOU_ARMORED_HARRIERS 10 /* the Iron Adonises, Red and Blue */
/* ACT_RECORD_COND_PARTNERS_USE_MOVES_MORE (battle_actrecord.h): the
 * partner's FP moves at least param times; no Glitz Pit table has it */
#define TOU_RULE_PARTNER_MOVE 0x35

int portRulesGlitzRule(int mode, int after, const char** speech, int* id, int* param, int* param2) {
    /* tests: $TTYD_GLITZ_RULE=<id> books every match with that condition (an ACT_RECORD_COND_ id, 0 none:
     * always kept), so the climb doesn't hang on a random condition the auto battler ignores
     * (tests/port/full_run, chapter 3); read once */
    static int sForced = -2;
    if (sForced == -2) {
        const char* v = getenv("TTYD_GLITZ_RULE");
        sForced = v != NULL && *v ? atoi(v) : -1;
        if (sForced >= 0) {
            printf("[rules] TTYD_GLITZ_RULE: every Glitz Pit match booked with condition %d\n", sForced);
            fflush(stdout);
        }
    }
    if (sForced >= 0) {
        *speech = after ? "pc_rules_tou_test_re" : "pc_rules_tou_test";
        *id = sForced;
        *param = 1;
        *param2 = 255;
        return 1;
    }
    if (!on("rules_glitz_conditions")) {
        return 0;
    }
    if (mode == TOU_RAWK_HAWK) {
        *speech = after ? "pc_rules_tou_free_re" : "pc_rules_tou_free";
        *id = 0; /* ACT_RECORD_COND_NONE: always kept, no banner */
        *param = 0;
        *param2 = 0;
        return 1;
    }
    if (mode == TOU_ARMORED_HARRIERS) {
        *speech = after ? "pc_rules_tou_partner_re" : "pc_rules_tou_partner";
        *id = TOU_RULE_PARTNER_MOVE;
        *param = 1;
        *param2 = 255; /* no turn limit */
        return 1;
    }
    return 0;
}

const char* portRulesGlitzDesc(int id) {
    if (id == TOU_RULE_PARTNER_MOVE) {
        return "pc_rules_tou_cp_partner";
    }
    if (id == 0) {
        return "pc_rules_tou_cp_free";
    }
    return NULL;
}

int portEvtRuleCakeSp(EventEntry* event, int isFirstCall) {
    static int sAp = -1;
    (void)isFirstCall;
    if (evtGetValue(event, event->args[0]) == 0) {
        sAp = on("rules_glitz_conditions") ? pouchGetAP() : -1;
    } else if (sAp >= 0) {
        pouchSetAP((s16)sAp);
        sAp = -1;
    }
    return EVT_RETURN_DONE;
}

static const char* trGame(const char* english) {
#ifdef TTYD_BACKEND_AURORA
    return portTrGame(english);
#else
    return english; /* the headless build has no translations */
#endif
}

/* Grubba's and (after chapter 3) Jolene's lines for the remake's conditions,
 * in Paper Cut's own words, following their booking speech (stg3_tou_107_00 /
 * stg3_tou_re_361, which end with <o>); the short texts as the condition banner and the booking
 * notice show them ("The promoter asks that you [...]") */
const char* portRulesMsg(const char* tag) {
    if (tag == NULL || strncmp(tag, "pc_rules_", 9) != 0) {
        return NULL;
    }
    if (strcmp(tag, "pc_rules_tou_free") == 0) {
        return trGame("<p>\nIt's the title match, son!\nNo special conditions this\ntime. Just beat the champ!\n<k>\n"
                      "<p>\nNow go on out there an'\nbring the house down!\n<k>");
    }
    if (strcmp(tag, "pc_rules_tou_free_re") == 0) {
        return trGame("<p>\nThis is the title match, so\nthere are no special\nconditions. Just win!\n<k>\n"
                      "<p>\nNow go out there and bring\nthe house down, all right?\n<k>");
    }
    if (strcmp(tag, "pc_rules_tou_partner") == 0) {
        return trGame("<p>\nHere's the deal, son: this time\nI want your partner to use\none of their FP moves, OK?\n<k>\n"
                      "<p>\nThose iron boys don't feel a\nthing from your boots, so\nlet your buddy shine!\n<k>");
    }
    if (strcmp(tag, "pc_rules_tou_partner_re") == 0) {
        return trGame("<p>\nOne thing: for this battle,\nI'd like your partner to use\none of their FP moves, OK?\n<k>\n"
                      "<p>\nNow go out there and bring\nthe house down, all right?\n<k>");
    }
    if (strcmp(tag, "pc_rules_tou_cp_partner") == 0) {
        return trGame("Have your partner use an\nFP move at least once!");
    }
    if (strcmp(tag, "pc_rules_tou_cp_free") == 0) {
        return trGame("Just win the match!");
    }
    /* $TTYD_GLITZ_RULE (tests only): the booking speech, whatever the condition */
    if (strcmp(tag, "pc_rules_tou_test") == 0 || strcmp(tag, "pc_rules_tou_test_re") == 0) {
        return "<p>\nThe test's condition this time.\n<k>";
    }
    return NULL;
}

/* --- The stagehand ("rules_tremor_stagehand") ---
 * An enemy's quake (a move on every grounded unit of Mario's side: Gloomtail's
 * and Magnus 2.0's, any move like them) aimed at a partner who can't act
 * (asleep, stopped, frozen, flipped): a Toad stagehand runs on, carries the
 * partner off-stage before the shake lands (no target there for it or for
 * the rest of the enemies' phase) and carries them back as Mario's side's next
 * phase begins. The Toad is the game's own (c_kinopio, its run). */

enum { HAND_IDLE, HAND_OFF_IN, HAND_OFF_OUT, HAND_AWAY, HAND_BACK_IN, HAND_BACK_OUT };

#define HAND_EDGE (-460.0f)   /* where the stagehand comes on and goes off */
#define HAND_AWAY_X (-900.0f) /* the partner's place off-stage */
#define HAND_SPEED 10.0f      /* a run's step a frame */
#define HAND_LIFT 34.0f       /* the partner held over the Toad's head */

static struct {
    int state;
    BattleUnit* partner;
    int pose;
    char poseFile[64];
    float x, y, z;
    int facingRight;
} sHand = {HAND_IDLE, NULL, -1, "", 0.0f, 0.0f, 0.0f, 0};

static int handPoseOurs(void) {
    const AnimationPose* p;
    if (sHand.pose < 0) {
        return 0;
    }
    p = animPoseGetAnimPosePtr(sHand.pose);
    return (p->flags & ANIM_POSE_FLAG_USED) && strcmp(animPoseGetAnimBaseDataPtr(sHand.pose)->fileName, sHand.poseFile) == 0;
}

static void handReset(void) {
    if (handPoseOurs()) {
        animPoseRelease(sHand.pose);
    }
    sHand.state = HAND_IDLE;
    sHand.partner = NULL;
    sHand.pose = -1;
}

static int isQuake(const BattleWeapon* w) {
    return w != NULL && (w->targetClassFlags & TARGET_CLASS_MULTIPLE) &&
           (w->targetPropertyFlags & (TARGET_PROP_NO_FLOATING | TARGET_PROP_NO_FLOATING_2));
}

static int helpless(BattleUnit* u) {
    return u != NULL && u->currentHp > 0 && !BtlUnit_CanActStatus(u);
}

static void handRun(int right) {
    sHand.facingRight = right;
    if (handPoseOurs()) {
        animPoseSetAnim(sHand.pose, "KNP_R_1", TRUE);
    }
    psndSFXOn_3D("SFX_AUDIENCE_RUN1", &sHand.partner->position);
}

static void stagehandTargets(BattleWorkWeaponTargets* d) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* attacker;
    BattleUnit* partner;
    int i, n = 0, aimed = 0;
    if (wp == NULL || d->numTargets <= 0 || !on("rules_tremor_stagehand")) {
        return;
    }
    attacker = BattleGetUnitPtr(wp, d->unitId);
    partner = BattleGetPartyPtr(wp);
    if (attacker == NULL || attacker->alliance != 1 || partner == NULL) {
        return;
    }
    for (i = 0; i < d->numTargets; i++) {
        aimed |= BattleGetUnitPtr(wp, d->targets[d->targetIndices[i]].unitIdx) == partner;
    }
    if (!aimed) {
        return;
    }
    if (sHand.state == HAND_IDLE) {
        if (!isQuake(d->weapon) || !helpless(partner)) {
            return;
        }
        sHand.partner = partner;
        sHand.x = HAND_EDGE;
        sHand.y = partner->homePosition.y;
        sHand.z = partner->homePosition.z + 8.0f; /* in front of the partner */
        sHand.pose = animPoseEntry("c_kinopio", 1);
        if (sHand.pose >= 0) {
            snprintf(sHand.poseFile, sizeof(sHand.poseFile), "%s", animPoseGetAnimBaseDataPtr(sHand.pose)->fileName);
        }
        sHand.state = HAND_OFF_IN;
        handRun(1);
        printf("[rules] stagehand: the partner carried off before the quake\n");
        fflush(stdout);
    } else if (sHand.partner != partner || sHand.state == HAND_BACK_OUT) {
        return;
    }
    /* off-stage (or on the way): nothing of the enemies' reaches them */
    for (i = 0; i < d->numTargets; i++) {
        if (BattleGetUnitPtr(wp, d->targets[d->targetIndices[i]].unitIdx) != partner) {
            d->targetIndices[n++] = d->targetIndices[i];
        }
    }
    d->numTargets = (s8)n;
}

int portRulesStagehandWait(int phase) {
    if (sHand.state == HAND_IDLE || phase == BATTLE_PHASE_ENEMY || phase == BATTLE_PHASE_AFTER_ENEMY) {
        return 0;
    }
    if (sHand.state == HAND_AWAY) {
        sHand.state = HAND_BACK_IN;
        sHand.x = HAND_EDGE;
        handRun(1);
    }
    return sHand.state != HAND_BACK_OUT;
}

static void handDisp(CameraId cam, void* param) {
    Mtx m, r;
    (void)cam;
    (void)param;
    if (!handPoseOurs()) {
        return;
    }
    MTXScale(m, 1.0f, 1.0f, 1.0f);
    MTXRotRad(r, 'y', sHand.facingRight ? 3.14159265f : 0.0f);
    MTXConcat(r, m, m);
    MTXTrans(r, sHand.x, sHand.y, sHand.z);
    MTXConcat(r, m, m);
    animPoseDrawMtx(sHand.pose, m, 1, 0.0f, 2.0f);
    animPoseDrawMtx(sHand.pose, m, 2, 0.0f, 2.0f);
    animPoseDrawMtx(sHand.pose, m, 3, 0.0f, 2.0f);
}

static void stagehandFrame(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* partner;
    if (sHand.state == HAND_IDLE) {
        return;
    }
    if (seqGetSeq() != SEQ_BATTLE || wp == NULL || (partner = BattleGetPartyPtr(wp)) != sHand.partner) {
        /* the battle over (its poses already let go), or the partner gone or
         * changed: no one to carry */
        if (seqGetSeq() != SEQ_BATTLE) {
            sHand.pose = -1;
        }
        handReset();
        return;
    }
    switch (sHand.state) {
        case HAND_OFF_IN: /* runs on to the partner */
            sHand.x += HAND_SPEED;
            if (sHand.x >= partner->position.x) {
                sHand.x = partner->position.x;
                sHand.state = HAND_OFF_OUT;
                handRun(0);
            }
            break;
        case HAND_OFF_OUT: /* and off with them overhead */
            sHand.x -= HAND_SPEED;
            partner->position.x = sHand.x;
            partner->position.y = sHand.y + HAND_LIFT;
            if (sHand.x <= HAND_EDGE) {
                sHand.state = HAND_AWAY;
            }
            break;
        case HAND_AWAY:
            partner->position.x = HAND_AWAY_X;
            partner->position.y = sHand.y;
            break;
        case HAND_BACK_IN: /* back on, carrying them home */
            sHand.x += HAND_SPEED;
            partner->position.x = sHand.x;
            partner->position.y = sHand.y + HAND_LIFT;
            if (sHand.x >= partner->homePosition.x) {
                partner->position = partner->homePosition;
                sHand.state = HAND_BACK_OUT;
                handRun(0);
            }
            break;
        case HAND_BACK_OUT:
            sHand.x -= HAND_SPEED;
            if (sHand.x <= HAND_EDGE) {
                handReset();
                return;
            }
            break;
    }
    if (sHand.state != HAND_AWAY && handPoseOurs()) {
        Vec pos = {sHand.x, sHand.y, sHand.z};
        animPoseMain(sHand.pose);
        dispEntry(CAMERA_3D, dispCalcZ(&pos), 1, handDisp, NULL);
    }
}

/* --- Auto-win much weaker fights ("auto_win_weak") --- */

/* Mario's level at least this far over the group's strongest: below it every
 * Star Point is 0 anyway (BtlUnit_GetExp), so the fight only cost time */
#define AUTO_WIN_GAP 10

static NpcEntry* sTouched;

void portRulesFieldTouch(NpcEntry* npc) {
    sTouched = npc;
}

/* the hearts, flowers and item a won fight leaves (battle_information.c
 * BattleInformationSetDropMaterial, from the pouch's HP and FP: there is no
 * battle Mario; Item Hog's own extra drop left out) */
static void autoWinDrops(NpcBattleInfo* setup) {
    BattleGroupSetup* group = setup->groupSetup;
    PointDropData* t;
    const int stat[2] = {pouchGetHP() * 100 / (pouchGetMaxHP() > 0 ? pouchGetMaxHP() : 1),
                         pouchGetFP() * 100 / (pouchGetMaxFP() > 0 ? pouchGetMaxFP() : 1)};
    int k, i, n, r, held, random, none, hog;
    for (k = 0; k < 2; k++) {
        int pct = stat[k] > 100 ? 100 : stat[k];
        int drops = 0;
        for (t = k == 0 ? group->hpDropTable : group->fpDropTable; t != NULL && t->maxStatPercent; t++) {
            if (pct <= t->maxStatPercent) {
                if (irand(100) < t->overallDropChance) {
                    for (n = t->numDrops; n != 0; n--) {
                        drops += irand(100) < t->dropChance;
                    }
                }
                break;
            }
        }
        if (k == 0) {
            setup->heartsDropped = drops;
        } else {
            setup->flowersDropped = drops;
        }
    }
    held = group->heldItemWeight;
    random = group->randomItemWeight;
    none = group->noItemWeight;
    hog = pouchEquipCheckBadge(ITEM_ITEM_HOG);
    if (hog >= 1) {
        held *= hog + 1;
        random *= hog + 1;
    }
    setup->itemDropped = 0;
    if (random + held + none <= 0) {
        return;
    }
    r = irand(random + held + none);
    if (r < random) {
        ItemDropData* d = group->enemyData->itemDropTable;
        ItemDropData* p;
        int sum = 0;
        for (p = d; p != NULL && (p->dropWeight > 0 || p->type != 0); p++) {
            sum += p->dropWeight;
        }
        if (sum > 0) {
            r = irand(sum);
            for (; d->dropWeight > 0 || d->type != 0; d++) {
                if ((r -= d->dropWeight) < 0) {
                    setup->itemDropped = d->type;
                    break;
                }
            }
        }
    } else if (r < random + held) {
        for (n = i = 0; i < 8; i++) {
            n += setup->heldItems[i] != 0;
        }
        if (n > 0) {
            r = irand(n);
            for (i = 0; i < 8; i++) {
                if (setup->heldItems[i] && --r < 0) {
                    setup->itemDropped = setup->heldItems[i];
                    break;
                }
            }
        }
    }
}

int portRulesAutoWin(NpcEntry* npc, FieldBattleInfo* info) {
    NpcEntry* touched = sTouched;
    PouchData* pouch = pouchGetPtr();
    MarioWork* mario = marioGetPtr();
    BattleGroupSetup* group;
    int i, top = 0, coins = 0;
    sTouched = NULL;
    /* only a field enemy Mario ran into (no scripted fight), only Mario, and
     * never in the Pit, Master Quest, a challenge or online */
    if (!on("auto_win_weak") || npc == NULL || npc != touched || pouch == NULL || mario == NULL ||
        mario->characterId != 0 || (group = npc->battleInfo.groupSetup) == NULL || group->enemyData == NULL) {
        return 0;
    }
    if (strncmp(gp->currentMapName, "jon", 3) == 0 || portMasterQuestOn() || portStateSyncOn() ||
        portChallenge("challenge_ohko") || portChallenge("challenge_guard") || portChallenge("challenge_commands") ||
        portChallenge("challenge_no_battle_items")) {
        return 0;
    }
    /* the fights the game itself won't let Bump Attack end, or never lets Mario run from */
    if (npc->battleInfo.battleFlags & (NPC_BATTLE_NO_BUMP_ATTACK | NPC_BATTLE_NO_ESCAPE)) {
        return 0;
    }
    for (i = 0; i < group->numEnemies; i++) {
        const BattleUnitSetup* u = &group->enemyData[i];
        if (u->unitKindParams == NULL || u->alliance != 1) {
            continue;
        }
        if (bossCoins(u->unitKindParams->unitType) != 0) {
            return 0; /* a boss */
        }
        top = u->unitKindParams->level > top ? u->unitKindParams->level : top;
    }
    if (top + AUTO_WIN_GAP > pouch->level) {
        return 0;
    }
    /* a won battle's coins (BtlUnit_GetCoin for each enemy; the field's
     * fbatBattleMode multiplies them as after a battle) and drops */
    for (i = 0; i < group->numEnemies; i++) {
        const BattleUnitKind* k = group->enemyData[i].unitKindParams;
        int b;
        if (k == NULL || group->enemyData[i].alliance != 1) {
            continue;
        }
        coins += k->baseCoin;
        for (b = 0; b < k->bonusCoin; b++) {
            coins += irand(100) < k->bonusCoinRate;
        }
    }
    if (npc->battleInfo.battleFlags & NPC_BATTLE_NO_COINS) {
        coins = 0;
    }
    BattleInfomationSetBattleSetupInfo(info, &npc->battleInfo);
    info->coins = (u32)coins;
    autoWinDrops(&npc->battleInfo);
    printf("[rules] auto-win: group level %d, Mario %d, %d coins\n", top, pouch->level, coins);
    fflush(stdout);
    return 1;
}

/* --- Less flashing ("less_flashing") --- */

int portRulesFlashAlpha(int alpha) {
    return on("less_flashing") ? alpha / 4 : alpha;
}

void portRulesFrame(void) {
    portRoomWarpFrame(); /* practice_more_pc.c: a warp asked for */
    portPracticeJumpFrame(); /* practice_jump_pc.c: a jump to a chapter asked for (and its question) */
    dooplissShroom();
    stagehandFrame();
    fpPreview();
    immuneCross();
    sleepySheep(on("rules_sleepy_sheep"));
    pitShop(on("rules_pit_shop"));
    bossSeen();
}
