/* Duo moves (port/include/port/duo.h, docs/DUO_MOVES.md): Mario and the
 * partner spend both their moves of the turn on one team attack, with both
 * players' action commands at once.
 *
 * The moves are data (kMoves below): which partner, the hero's half (a
 * stomp or a hammer swing), how the partner's animations are put together
 * (a leap, a dart or Bobbery's blast) and with which of its poses, the
 * command (a timed press as each one hits, or both mashing), the FP and the
 * power. One script runs them all (duo_attack_event): the two halves run side
 * by side as sibling events, each walking its unit to its spot; once both are
 * there the command starts, each half times its leap so it lands on its own
 * mark, and hits with its own weapon when its player's press is judged.
 *
 * Each half is a BattleWeapon of its own, copied from the hero's or the
 * partner's basic move (its counters, elements and specials as the game
 * has them) with the duo's power (duoPower): base by boots, hammer or rank,
 * plus the command's bonuses. The header weapon the menu lists (targets, FP,
 * the help text) only starts the script.
 *
 * Everything here is driven by the game's input (gp->buttonNew, the same in
 * both games online) and the battle's own state, so lockstep carries it. The
 * command's display draws with the game's font: the random numbers are kept
 * as they were around it (second_player_pc.c's tagDisp says why). */
#include "port/duo.h"

#include "battle/battle.h"
#include "battle/battle_ac.h"
#include "battle/battle_camera.h"
#include "battle/battle_damage.h"
#include "battle/battle_disp.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_seq_command.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/icondrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "evt/evt_audience.h"
#include "evt/evt_cmd.h"
#include "evt/evt_eff.h"
#include "evt/evt_snd.h"
#include "manager/evtmgr_cmd.h"
#include "manager/fontmgr.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "port/ac_cue.h"
#include "port/assist.h"
#include "port/battle_speed.h"
#include "port/gameplus.h"
#include "port/online.h"
#include "port/platform.h"
#include "port/settings.h"
#include "port/versus.h"
#ifdef TTYD_BACKEND_AURORA
#include "port/tr.h"
#endif
#include "system.h"
#include "version.h"
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* the basic moves the halves are copied from */
extern BattleWeapon marioDefaultWeapon_Jump;
extern BattleWeapon marioDefaultWeapon_Hammer;
extern BattleWeapon partyWeapon_ChristineNormalAttack;
extern BattleWeapon partyWeapon_NokotarouKouraAttack;
extern BattleWeapon partyWeapon_YoshiNormalAttack;
extern BattleWeapon partyWeapon_ClaudaNormalAttack;
extern BattleWeapon partyWeapon_VivianNormalAttack;
extern BattleWeapon partyWeapon_SandersNormalAttack;
extern BattleWeapon partyWeapon_ChuchurinaNormalAttackLeftLast;

/* second_player_pc.c: co-op with a second controller (or online) */
int portCoopTwoPlayers(void);

/* --- The moves --- */

/* The timed command's window, frames either side of the mark, and the
 * mashing's counts (over MASH_FRAMES) for good and just right. */
#define WINDOW 5
#define PERFECT 1
#define EARLY 12 /* a press this many frames before the window is a miss; earlier ones aren't counted */
#define LEAD 48  /* frames from the command's start to the hero's mark */
#define MASH_FRAMES 90
#define MASH_GOOD 9
#define MASH_PERFECT 15
#define LINGER 24 /* frames the results stay up */

static const PortDuoMove kMoves[] = {
    /* Goombella: both stomp the same enemy, Goombella a beat after Mario */
    {"twin_bonk", PORT_DUO_SECOND_PARTNER, UNIT_GOOMBELLA, 0, 3, PORT_DUO_AC_TIMED, 10, PORT_DUO_HERO_STOMP, PORT_DUO_PARTNER_LEAP,
     {1, 2, 3}, {1, 2, 3}, 1, 1, 1, "PKR_A_2B", "PKR_J_1A", "PKR_J_1A", "SFX_BTL_KURI_ATTACK1", ELEMENT_NORMAL, 0, 30,
     0, 0},
    /* Koops: his shell slams in as Mario lands on top, both at once */
    {"shell_stomp", PORT_DUO_SECOND_PARTNER, UNIT_KOOPS, 0, 3, PORT_DUO_AC_TIMED, 0, PORT_DUO_HERO_STOMP, PORT_DUO_PARTNER_DASH, {1, 2, 3},
     {2, 3, 4}, 1, 1, 1, "PNK_K_1", "PNK_K_1", "PNK_K_1", "SFX_BTL_NOKO_ATTACK2", ELEMENT_NORMAL, 0, 14, 0, 1},
    /* Yoshi: his ground pound first, Mario's stomp right after */
    {"flutter_drop", PORT_DUO_SECOND_PARTNER, UNIT_YOSHI, 0, 3, PORT_DUO_AC_TIMED, -10, PORT_DUO_HERO_STOMP, PORT_DUO_PARTNER_LEAP, {1, 2, 3},
     {1, 2, 3}, 1, 1, 1, "PYS_R_5", "PYS_J_1A", "PYS_A_1", "SFX_PARTY_BATTLE_LANDING1", ELEMENT_NORMAL, 0, 28, 0, 0},
    /* Flurrie: both mash, then she and Mario drop onto it */
    {"cloud_crush", PORT_DUO_SECOND_PARTNER, UNIT_FLURRIE, 0, 4, PORT_DUO_AC_MASH, 0, PORT_DUO_HERO_STOMP, PORT_DUO_PARTNER_LEAP, {1, 2, 3},
     {2, 3, 4}, 1, 1, 1, NULL, "PWD_A1_1", "PWD_A1_2", "SFX_BTL_CLAUD_ATTACK1", ELEMENT_NORMAL, 0, 40, 1, 0},
    /* Vivian: her Shade Fist and Mario's hammer; a good punch can burn */
    {"shade_smash", PORT_DUO_SECOND_PARTNER, UNIT_VIVIAN, 0, 3, PORT_DUO_AC_TIMED, 8, PORT_DUO_HERO_HAMMER, PORT_DUO_PARTNER_DASH, {1, 2, 3},
     {1, 2, 3}, 1, 1, 1, "PTR_A1_1", "PTR_A1_1", "PTR_A1_2", "SFX_BTL_VIVIAN_ATTACK4", ELEMENT_FIRE, 1, 12, 0, 0},
    /* Bobbery: mash to light the fuse; he blows up as Mario lands */
    {"blast_bounce", PORT_DUO_SECOND_PARTNER, UNIT_BOBBERY, 0, 4, PORT_DUO_AC_MASH, 0, PORT_DUO_HERO_STOMP, PORT_DUO_PARTNER_BLAST, {1, 2, 3},
     {2, 3, 4}, 1, 1, 1, "A_3", "R_2", "A_3", "SFX_BTL_THUNDERS_BOMB1", ELEMENT_EXPLOSION, 0, 0, 0, 0},
    /* Ms. Mowz: her slap first, Mario's hammer follows through */
    {"slap_smash", PORT_DUO_SECOND_PARTNER, UNIT_MS_MOWZ, 0, 3, PORT_DUO_AC_TIMED, -8, PORT_DUO_HERO_HAMMER, PORT_DUO_PARTNER_DASH, {1, 2, 3},
     {1, 2, 3}, 1, 1, 1, "PCH_A1_1", "PCH_A1_1", "PCH_A1_2", "SFX_BTL_CHUCHURINA_ATTACK1", ELEMENT_NORMAL, 0, 10, 0, 0},
    /* Ultra rank: a stronger second move for each partner */
    {"bonk_barrage", PORT_DUO_SECOND_PARTNER, UNIT_GOOMBELLA, 2, 6, PORT_DUO_AC_MASH, 0, PORT_DUO_HERO_STOMP,
     PORT_DUO_PARTNER_LEAP, {2, 3, 4}, {3, 4, 5}, 1, 1, 2, "PKR_A_2B", "PKR_J_1A", "PKR_J_1A", "SFX_BTL_KURI_ATTACK1",
     ELEMENT_NORMAL, 2, 30, 0, 0},
    {"shell_quake", PORT_DUO_SECOND_PARTNER, UNIT_KOOPS, 2, 6, PORT_DUO_AC_TIMED, 0, PORT_DUO_HERO_HAMMER,
     PORT_DUO_PARTNER_DASH, {2, 3, 4}, {3, 4, 5}, 1, 1, 2, "PNK_K_1", "PNK_K_1", "PNK_K_1", "SFX_BTL_NOKO_ATTACK2",
     ELEMENT_NORMAL, 2, 14, 0, 0},
    {"yoshi_rush", PORT_DUO_SECOND_PARTNER, UNIT_YOSHI, 2, 6, PORT_DUO_AC_MASH, 0, PORT_DUO_HERO_STOMP,
     PORT_DUO_PARTNER_LEAP, {2, 3, 4}, {3, 4, 5}, 1, 1, 2, "PYS_R_5", "PYS_J_1A", "PYS_A_1", "SFX_PARTY_BATTLE_LANDING1",
     ELEMENT_NORMAL, 0, 28, 0, 0},
    {"gale_crush", PORT_DUO_SECOND_PARTNER, UNIT_FLURRIE, 2, 6, PORT_DUO_AC_TIMED, 6, PORT_DUO_HERO_STOMP,
     PORT_DUO_PARTNER_LEAP, {2, 3, 4}, {3, 4, 5}, 1, 1, 2, NULL, "PWD_A1_1", "PWD_A1_2", "SFX_BTL_CLAUD_ATTACK1",
     ELEMENT_NORMAL, 3, 40, 1, 0},
    {"inferno_smash", PORT_DUO_SECOND_PARTNER, UNIT_VIVIAN, 2, 6, PORT_DUO_AC_MASH, 0, PORT_DUO_HERO_HAMMER,
     PORT_DUO_PARTNER_DASH, {2, 3, 4}, {3, 4, 5}, 1, 1, 2, "PTR_A1_1", "PTR_A1_1", "PTR_A1_2", "SFX_BTL_VIVIAN_ATTACK4",
     ELEMENT_FIRE, 1, 12, 0, 0},
    {"mega_blast", PORT_DUO_SECOND_PARTNER, UNIT_BOBBERY, 2, 7, PORT_DUO_AC_TIMED, 0, PORT_DUO_HERO_STOMP,
     PORT_DUO_PARTNER_BLAST, {2, 3, 4}, {4, 5, 6}, 1, 1, 2, "A_3", "R_2", "A_3", "SFX_BTL_THUNDERS_BOMB1",
     ELEMENT_EXPLOSION, 0, 0, 0, 0},
    {"dizzy_smash", PORT_DUO_SECOND_PARTNER, UNIT_MS_MOWZ, 2, 6, PORT_DUO_AC_TIMED, -8, PORT_DUO_HERO_HAMMER,
     PORT_DUO_PARTNER_DASH, {2, 3, 4}, {3, 4, 5}, 1, 1, 2, "PCH_A1_1", "PCH_A1_1", "PCH_A1_2",
     "SFX_BTL_CHUCHURINA_ATTACK1", ELEMENT_NORMAL, 2, 10, 0, 0},
    /* Brothers (a second hero, portDuoSetSecondHero): both stomp, the second a beat later */
    {"bros_bounce", PORT_DUO_SECOND_HERO, 0, 0, 3, PORT_DUO_AC_TIMED, 10, PORT_DUO_HERO_STOMP, PORT_DUO_HERO_STOMP,
     {1, 2, 3}, {1, 2, 3}, 1, 1, 1, NULL, NULL, NULL, NULL, ELEMENT_NORMAL, 0, 0, 0, 0},
    /* both hammers, side by side, one swing after the other */
    {"bros_hammer", PORT_DUO_SECOND_HERO, 0, 0, 3, PORT_DUO_AC_TIMED, 8, PORT_DUO_HERO_HAMMER, PORT_DUO_HERO_HAMMER,
     {1, 2, 3}, {1, 2, 3}, 1, 1, 1, NULL, NULL, NULL, NULL, ELEMENT_NORMAL, 0, 0, 0, 0},
    /* both mash, then one stomps as the other's hammer comes down */
    {"bros_drop", PORT_DUO_SECOND_HERO, 0, 0, 5, PORT_DUO_AC_MASH, 0, PORT_DUO_HERO_STOMP, PORT_DUO_HERO_HAMMER,
     {2, 3, 4}, {2, 3, 4}, 1, 1, 1, NULL, NULL, NULL, NULL, ELEMENT_NORMAL, 0, 0, 0, 0},
};
#define BUILTIN ((int)(sizeof(kMoves) / sizeof(kMoves[0])))

/* the English of the built-in moves' names and help (translated at drawing, portTrGame) */
static const struct {
    const char* key;
    const char* name;
    const char* desc;
} kText[] = {
    {"twin_bonk", "Twin Bonk", "Mario and Goombella stomp the same\nenemy, one right after the other."},
    {"shell_stomp", "Shell Stomp", "Koops's shell slams in as Mario\nlands on top. Press together!"},
    {"flutter_drop", "Flutter Drop", "Yoshi pounds the enemy first,\nthen Mario stomps it."},
    {"cloud_crush", "Cloud Crush", "Mash together, then Flurrie and\nMario drop onto one enemy."},
    {"shade_smash", "Shade Smash", "Vivian's Shade Fist and Mario's\nhammer. It can burn the enemy."},
    {"blast_bounce", "Blast Bounce", "Mash to light the fuse: Bobbery\nblows up as Mario lands."},
    {"slap_smash", "Slap Smash", "Ms. Mowz slaps the enemy, then\nMario's hammer follows through."},
    {"bonk_barrage", "Bonk Barrage", "Mash together, then both stomp.\nIt can leave the enemy dizzy."},
    {"shell_quake", "Shell Quake", "Koops's shell and Mario's hammer\nat once. It can daze the enemy."},
    {"yoshi_rush", "Yoshi Rush", "Mash together, then Yoshi and\nMario pound one enemy hard."},
    {"gale_crush", "Gale Crush", "Mario stomps, Flurrie slams down.\nIt can put the enemy to sleep."},
    {"inferno_smash", "Inferno Smash", "Mash together: Vivian's fire and\nMario's hammer. It burns."},
    {"mega_blast", "Mega Blast", "Bobbery's big blast as Mario\nlands. Press together!"},
    {"dizzy_smash", "Dizzy Smash", "Ms. Mowz's slap and Mario's hammer.\nIt can leave the enemy dizzy."},
    {"bros_bounce", "Bros. Bounce", "Mario and Luigi stomp the same\nenemy, one right after the other."},
    {"bros_hammer", "Bros. Hammer", "Two hammers side by side,\none swing after the other."},
    {"bros_drop", "Bros. Drop", "Mash together, then one stomps as\nthe other's hammer comes down."},
};

static PortDuoMove sAdded[PORT_DUO_MAX_MOVES - BUILTIN] PORT_HOST_DATA; /* (registrations: kept over save states) */
static int sAddedCount PORT_HOST_DATA;
static const char* (*sResolver)(const char* tag) PORT_HOST_DATA;
static int sPlayersOverride;
static int (*sSecondHero)(void) PORT_HOST_DATA;
static int (*sControllerOf)(const void* unit) PORT_HOST_DATA;
static int* sSetting PORT_HOST_DATA;

/* each move's three weapons: the one the menu lists, the hero's half, the partner's */
static BattleWeapon sHeader[PORT_DUO_MAX_MOVES];
static BattleWeapon sHero[PORT_DUO_MAX_MOVES];
static BattleWeapon sPartner[PORT_DUO_MAX_MOVES];
static int sBuilt[PORT_DUO_MAX_MOVES];
#define TAG_SIZE 48 /* "pc_duo_name_" and a key of up to 35 */
static char sTags[PORT_DUO_MAX_MOVES][3][TAG_SIZE];

int portDuoMoveCount(void) {
    return BUILTIN + sAddedCount;
}

const PortDuoMove* portDuoMoveAt(int i) {
    if (i < 0) {
        return NULL;
    }
    if (i < BUILTIN) {
        return &kMoves[i];
    }
    i -= BUILTIN;
    return i < sAddedCount ? &sAdded[i] : NULL;
}

int portDuoRegister(const PortDuoMove* move) {
    if (move == NULL || move->key == NULL || strlen(move->key) > TAG_SIZE - sizeof("pc_duo_name_") ||
        sAddedCount >= (int)(sizeof(sAdded) / sizeof(sAdded[0]))) {
        return -1;
    }
    sAdded[sAddedCount] = *move;
    return BUILTIN + sAddedCount++;
}

void portDuoSetMsgResolver(const char* (*resolve)(const char* tag)) {
    sResolver = resolve;
}

void portDuoSetPlayers(int players) {
    sPlayersOverride = players;
}

void portDuoSetSecondHero(int (*otherHeroUnitId)(void)) {
    sSecondHero = otherHeroUnitId;
}

void portDuoSetControllerFor(int (*controllerOf)(const void* unit)) {
    sControllerOf = controllerOf;
}

/* the second hero's unit this battle, or NULL */
static BattleUnit* secondHero(BattleWork* wp) {
    const int id = sSecondHero != NULL && wp != NULL ? sSecondHero() : -1;
    BattleUnit* u = id >= 0 ? BattleGetUnitPtr(wp, id) : NULL;
    return u != NULL && u->currentKind == UNIT_MARIO ? u : NULL;
}

/* the second half's unit: the partner, or the second hero */
static BattleUnit* secondUnit(BattleWork* wp, const PortDuoMove* m) {
    return m->second == PORT_DUO_SECOND_HERO ? secondHero(wp) : BattleGetPartyPtr(wp);
}

static int setting(void) {
    if (sSetting == NULL) {
        sSetting = portSettingsDeclare("duo_moves", 1, 0, 2); /* 0 off, 1 co-op and versus, 2 always */
    }
    return *sSetting;
}

/* one player plays both halves (A and B), or two (each on A) */
static int onePlayer(void) {
    if (sPlayersOverride != PORT_DUO_PLAYERS_AUTO) {
        return sPlayersOverride == PORT_DUO_PLAYERS_ONE;
    }
    if (portVersusOn()) {
        return 1; /* each side of a versus fight is one player's */
    }
    return !portCoopTwoPlayers();
}

int portDuoEnabled(void) {
    const int s = setting();
    if (s == 0) {
        return 0;
    }
    if (s == 2 || portVersusOn() || sPlayersOverride != PORT_DUO_PLAYERS_AUTO) {
        return 1;
    }
    return portCoopTwoPlayers();
}

/* the partner's basic move a half is copied from */
static BattleWeapon* partnerTemplate(int kind) {
    switch (kind) {
        case UNIT_GOOMBELLA:
            return &partyWeapon_ChristineNormalAttack;
        case UNIT_KOOPS:
            return &partyWeapon_NokotarouKouraAttack;
        case UNIT_YOSHI:
            return &partyWeapon_YoshiNormalAttack;
        case UNIT_FLURRIE:
            return &partyWeapon_ClaudaNormalAttack;
        case UNIT_VIVIAN:
            return &partyWeapon_VivianNormalAttack;
        case UNIT_BOBBERY:
            return &partyWeapon_SandersNormalAttack;
        default:
            return &partyWeapon_ChuchurinaNormalAttackLeftLast;
    }
}

static s32 duoPower(BattleUnit* attacker, BattleWeapon* weapon, BattleUnit* target, BattleUnitPart* part);
extern EvtWord duo_attack_event[];

/* the statuses a half's template had, cleared (the duo's own come after) */
static void clearStatuses(BattleWeapon* w) {
    memset(&w->sleepChance, 0, (size_t)((u8*)&w->pad_AE - (u8*)&w->sleepChance));
}

/* move i's weapons, made once */
static void build(int i) {
    const PortDuoMove* m = portDuoMoveAt(i);
    BattleWeapon* partnerSrc;
    BattleWeapon* heroSrc;
    if (m == NULL || sBuilt[i]) {
        return;
    }
    sBuilt[i] = 1;
    snprintf(sTags[i][0], sizeof(sTags[i][0]), "pc_duo_name_%s", m->key);
    snprintf(sTags[i][1], sizeof(sTags[i][1]), "pc_duo_desc_%s", m->key);
    snprintf(sTags[i][2], sizeof(sTags[i][2]), "pc_duo_ac_%s", m->ac == PORT_DUO_AC_MASH ? "mash" : "timed");
    heroSrc = m->hero == PORT_DUO_HERO_HAMMER ? &marioDefaultWeapon_Hammer : &marioDefaultWeapon_Jump;
    partnerSrc = m->second == PORT_DUO_SECOND_HERO
                     ? (m->partner == PORT_DUO_HERO_HAMMER ? &marioDefaultWeapon_Hammer : &marioDefaultWeapon_Jump)
                     : partnerTemplate(m->partnerKind);

    sHero[i] = *heroSrc;
    sHero[i].hpDamageFn = duoPower;
    sHero[i].attackEvtScript = NULL;
    sHero[i].baseFpCost = 0;

    sPartner[i] = *partnerSrc;
    sPartner[i].hpDamageFn = duoPower;
    sPartner[i].attackEvtScript = NULL;
    sPartner[i].baseFpCost = 0;
    sPartner[i].element = (u8)m->element;
    clearStatuses(&sPartner[i]);
    switch (m->status) {
        case 1:
            sPartner[i].burnChance = 100;
            sPartner[i].burnTime = 2;
            break;
        case 2:
            sPartner[i].dizzyChance = 80;
            sPartner[i].dizzyTime = 2;
            break;
        case 3:
            sPartner[i].sleepChance = 80;
            sPartner[i].sleepTime = 2;
            break;
    }
    if (m->element == ELEMENT_EXPLOSION) {
        sPartner[i].special |= SPECIAL_FLIPS_SHELLED;
    }

    /* the listed one: the hero's targets, narrowed by the partner's half */
    sHeader[i] = *heroSrc;
    sHeader[i].name = sTags[i][0];
    sHeader[i].description = sTags[i][1];
    sHeader[i].acHelpMsg = sTags[i][2];
    sHeader[i].icon = m->second == PORT_DUO_SECOND_HERO ? heroSrc->icon : partnerSrc->icon;
    sHeader[i].baseFpCost = (s8)m->fp;
    sHeader[i].hpDamageFn = duoPower;
    sHeader[i].attackEvtScript = duo_attack_event;
    sHeader[i].targetPropertyFlags |=
        partnerSrc->targetPropertyFlags & (TARGET_PROP_NO_FLOATING | TARGET_PROP_NO_CEILING | TARGET_PROP_NO_FLOATING_2 |
                                           TARGET_PROP_SHELL_TOSS_LIKE | TARGET_PROP_FRONTMOST |
                                           TARGET_PROP_NO_SHELL_SHIELDED);
    sHeader[i].special &= ~SPECIAL_USABLE_IF_CONFUSED;
    clearStatuses(&sHeader[i]);
}

/* which move's half a weapon is: 0 the hero's, 1 the partner's, 2 the header; -1 none */
static int halfOf(const BattleWeapon* w, int* move) {
    int i;
    for (i = 0; i < portDuoMoveCount(); i++) {
        if (w == &sHero[i] || w == &sPartner[i] || w == &sHeader[i]) {
            *move = i;
            return w == &sHero[i] ? 0 : w == &sPartner[i] ? 1 : 2;
        }
    }
    return -1;
}

/* --- Who can join in --- */

static int lectureOn(const BattleWork* wp) {
    return (wp->flags & (BATTLE_FLAG_LECTURE_LOCK_ALL | BATTLE_FLAG_LECTURE_JUMP | BATTLE_FLAG_LECTURE_HAMMER |
                         BATTLE_FLAG_LECTURE_SPECIAL | BATTLE_FLAG_LECTURE_MENU | BATTLE_FLAG_LECTURE_STEP2)) != 0;
}

static int canAct(BattleUnit* u) {
    return u != NULL && u->currentHp > 0 && !BtlUnit_CheckStatus(u, STATUS_INSTANT_KILL) && BtlUnit_CanActStatus(u);
}

/* the hero can play his half (from either's turn) */
static int heroCan(BattleWork* wp, BattleUnit* hero, int half) {
    const PouchData* pouch = pouchGetPtr();
    const NpcBattleInfo* info = wp->fbatInfo != NULL ? wp->fbatInfo->setup : NULL;
    if (hero->attributeFlags & UNIT_ATTR_VEILED) {
        return 0;
    }
    if (half == PORT_DUO_HERO_HAMMER) {
        return pouch->hammerLevel >= 1 && !hero->badgesEquipped.Jumpman && wp->commandDisabled[1] <= 0 &&
               (info == NULL || !(info->battleFlags & NPC_BATTLE_NO_HAMMER));
    }
    return pouch->jumpLevel >= 1 && !hero->badgesEquipped.Hammerman && wp->commandDisabled[0] <= 0 &&
           (info == NULL || !(info->battleFlags & NPC_BATTLE_NO_JUMP));
}

static int partnerCan(BattleWork* wp, BattleUnit* partner) {
    (void)wp;
    if (partner->flippedTurns > 0) {
        return 0;
    }
    if (VERSION_PICK(wp->commandDisabled[3] > 0, partner->usCommandDisabled[0] != 0)) {
        return 0;
    }
    return 1;
}

/* the actor's move for the list: 1 listed; *usable 0 when the other can't join now */
static int listedFor(BattleWork* wp, BattleUnit* actor, int i, int* usable) {
    const PortDuoMove* m = portDuoMoveAt(i);
    BattleUnit* hero;
    BattleUnit* second;
    BattleUnit* other;
    int secondCan;
    *usable = 0;
    if (m == NULL || wp == NULL || actor == NULL || !portDuoEnabled() || lectureOn(wp)) {
        return 0;
    }
    hero = BattleGetMarioPtr(wp);
    second = secondUnit(wp, m);
    if (hero == NULL || second == NULL) {
        return 0;
    }
    /* the hero's turn, or the partner's; a second hero picks the Bros. moves on his own turn, as the game's Mario */
    if (actor != hero && (actor != second || m->second == PORT_DUO_SECOND_HERO)) {
        return 0;
    }
    if (m->second == PORT_DUO_SECOND_HERO) {
        secondCan = heroCan(wp, second, m->partner);
    } else {
        if (second->currentKind != m->partnerKind ||
            pouchGetPartyAttackLv((PartyId)BattleTransPartyId(second->currentKind)) < m->minRank) {
            return 0;
        }
        secondCan = partnerCan(wp, second);
    }
    other = actor == hero ? second : hero;
    *usable = actor->moveState == 0 && canAct(other) && other->movesRemaining > 0 && other->moveState != 10 &&
              heroCan(wp, hero, m->hero) && secondCan;
    return 1;
}

int portDuoListed(int i, int* usable) {
    BattleWork* wp = _battleWorkPointer;
    int dummy;
    if (usable == NULL) {
        usable = &dummy;
    }
    *usable = 0;
    if (gp == NULL || !gp->inBattle || wp == NULL || wp->activeUnitId < 0) {
        return 0;
    }
    return listedFor(wp, BattleGetUnitPtr(wp, wp->activeUnitId), i, usable);
}

/* --- The command menu (battle_seq_command.c) --- */

/* the command menu's weapon list (BattleCommand.weapon) */
#define WEAPON_SLOTS ((int)(sizeof(((BattleCommand*)0)->weapon) / sizeof(((BattleCommand*)0)->weapon[0])))

void portDuoAddWeapons(BattleCommand* cmd, int type, int* num) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* actor;
    int i, usable;
    if (cmd == NULL || num == NULL || wp == NULL || (type != 0 && type != 1 && type != 6)) {
        return;
    }
    actor = BattleGetUnitPtr(wp, wp->activeUnitId);
    for (i = 0; i < portDuoMoveCount() && *num < WEAPON_SLOTS; i++) {
        const PortDuoMove* m = portDuoMoveAt(i);
        BattleCommandWeapon* e;
        if (!listedFor(wp, actor, i, &usable)) {
            continue;
        }
        if ((type == 0 && (actor->currentKind != UNIT_MARIO || m->hero != PORT_DUO_HERO_STOMP)) ||
            (type == 1 && (actor->currentKind != UNIT_MARIO || m->hero != PORT_DUO_HERO_HAMMER)) ||
            (type == 6 && actor->currentKind == UNIT_MARIO)) {
            continue;
        }
        build(i);
        e = &cmd->weapon[*num];
        e->weapon = &sHeader[i];
        e->enabled = 0;
        e->disableReason = 0;
        e->index = -1;
        e->itemId = 0;
        e->icon = sHeader[i].icon;
        e->name = msgSearch(sHeader[i].name);
        (*num)++;
    }
}

void portDuoCheckWeapons(BattleCommand* cmd, int count) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* actor;
    int i, move, usable;
    if (cmd == NULL || wp == NULL) {
        return;
    }
    actor = BattleGetUnitPtr(wp, wp->activeUnitId);
    for (i = 0; i < count && i < WEAPON_SLOTS; i++) {
        if (cmd->weapon[i].weapon == NULL || halfOf(cmd->weapon[i].weapon, &move) != 2) {
            continue;
        }
        if (!listedFor(wp, actor, move, &usable) || !usable) {
            cmd->weapon[i].enabled = 0;
            cmd->weapon[i].disableReason = PORT_DUO_REASON;
        }
    }
}

int portDuoListsMario(BattleWork* wp) {
    BattleUnit* hero;
    int i, usable, lists = 0;
    if (wp == NULL || !portDuoEnabled()) {
        return 0;
    }
    hero = BattleGetMarioPtr(wp);
    for (i = 0; i < portDuoMoveCount(); i++) {
        if (listedFor(wp, hero, i, &usable)) {
            lists |= portDuoMoveAt(i)->hero == PORT_DUO_HERO_HAMMER ? 2 : 1;
        }
    }
    return lists;
}

/* --- The messages --- */

static const char* tr(const char* english) {
#ifdef TTYD_BACKEND_AURORA
    return portTrGame(english);
#else
    return english; /* the headless build has no translations */
#endif
}

/* An action command's help (the box above the stage, which draws <icon>s as
 * the game's own help does): each A and B standing alone in the translated
 * text as the button's picture, which the controller's button prompts redraw
 * (button_prompts.cpp). Plain letters said "B" beside an Xbox controller's
 * blue X. Shift-JIS's second bytes can be 'A' or 'B': skipped on the JP release. */
static const char* withIcons(const char* text) {
    static char out[512];
    const unsigned char* s = (const unsigned char*)text;
    size_t n = 0;
    int prev = ' ';
    while (*s != 0 && n + 32 < sizeof(out)) {
        const int next = s[1];
#if VERSION == VERSION_JP
        if ((*s >= 0x81 && *s <= 0x9F) || (*s >= 0xE0 && *s <= 0xFC)) {
            out[n++] = (char)*s++;
            if (*s != 0) {
                out[n++] = (char)*s++;
            }
            prev = 0x80;
            continue;
        }
#endif
        if ((*s == 'A' || *s == 'B') && !(prev < 0x80 && (isalnum(prev) || prev == '<')) &&
            !(next < 0x80 && isalnum(next))) {
            n += (size_t)snprintf(out + n, sizeof(out) - n, "<icon %s 0.6 1 2 6>", *s == 'A' ? "PAD_A" : "PAD_B");
        } else {
            out[n++] = (char)*s;
        }
        prev = *s++;
    }
    out[n] = 0;
    return out;
}

/* "pc_duo_name_" and "pc_duo_desc_": what comes before a move's key */
#define TAG_HEAD ((int)sizeof("pc_duo_name_") - 1)

const char* portDuoMsg(const char* tag) {
    int i;
    if (tag == NULL || strncmp(tag, "pc_duo_", 7) != 0) {
        return NULL;
    }
    if (strcmp(tag, "pc_duo_busy") == 0) {
        return tr("A duo move needs Mario and the\npartner both still to have their move.");
    }
    if (strcmp(tag, "pc_duo_ac_timed") == 0) {
        return withIcons(tr(onePlayer() ? "Press {A} as Mario hits and {B} as your partner hits!"
                                        : "Each player presses {A} as their hero hits!"));
    }
    if (strcmp(tag, "pc_duo_ac_mash") == 0) {
        return withIcons(tr(onePlayer() ? "Mash {A} for Mario and {B} for your partner!" : "Both players mash {A}!"));
    }
    for (i = 0; i < (int)(sizeof(kText) / sizeof(kText[0])); i++) {
        if ((int)strlen(tag) <= TAG_HEAD || strcmp(tag + TAG_HEAD, kText[i].key) != 0) {
            continue;
        }
        if (strncmp(tag + 7, "name_", 5) == 0) {
            return tr(kText[i].name);
        }
        if (strncmp(tag + 7, "desc_", 5) == 0) {
            return tr(kText[i].desc);
        }
    }
    return sResolver != NULL ? sResolver(tag) : NULL;
}

/* --- The running move --- */

enum { FLAG_HERO_READY, FLAG_PARTNER_READY, FLAG_HERO_DONE, FLAG_PARTNER_DONE, FLAG_COUNT };

static struct {
    int active;       /* the script runs */
    int actor;        /* the unit whose turn it is */
    int move;         /* the move's index */
    int flags[FLAG_COUNT];
    int started;      /* the command started (running or over) */
    int running;      /* it runs (input, display) */
    int frame;        /* frames since it started */
    int target[2];    /* each one's mark (timed), or the mashing's end */
    int window[2];    /* frames either side of the mark */
    int perfect[2];
    int judged[2];
    int result[2];    /* 0 missed, 1 good, 2 just right */
    int pressedAt[2]; /* the frame of the press (-1 none) */
    int count[2];     /* the presses mashed */
    int mashGood[2], mashPerfect[2];
    int automatic[2];
    int pad[2];       /* two players: the controller each half is read from */
    int one;          /* one player plays both */
} sDuo;

static const PortDuoMove* current(void) {
    return portDuoMoveAt(sDuo.move);
}

int portDuoActive(void) {
    BattleWork* wp = _battleWorkPointer;
    if (!sDuo.active) {
        return 0;
    }
    if (gp == NULL || !gp->inBattle || wp == NULL || wp->activeUnitId != sDuo.actor ||
        (wp->flags & BATTLE_FLAG_COMMAND_INPUT)) {
        sDuo.active = 0; /* the battle went on without the script's end (it was stopped) */
        sDuo.running = 0;
        return 0;
    }
    return 1;
}

int portDuoCommandRunning(void) {
    return portDuoActive() && sDuo.running;
}

static BattleUnit* halfUnit(int player) {
    BattleWork* wp = _battleWorkPointer;
    return player == 0 ? BattleGetMarioPtr(wp) : current() != NULL ? secondUnit(wp, current()) : NULL;
}

/* the power of a half's hit (BattleWeapon.hpDamageFn) */
static s32 duoPower(BattleUnit* attacker, BattleWeapon* weapon, BattleUnit* target, BattleUnitPart* part) {
    const PortDuoMove* m;
    int move, half, power, lv;
    (void)target;
    (void)part;
    half = halfOf(weapon, &move);
    m = portDuoMoveAt(move);
    if (half < 0 || half > 1 || m == NULL) {
        return 0;
    }
    if (half == 0) {
        lv = m->hero == PORT_DUO_HERO_HAMMER ? pouchGetPtr()->hammerLevel : pouchGetPtr()->jumpLevel;
        lv = lv < 1 ? 1 : lv > 3 ? 3 : lv;
        power = m->heroPower[lv - 1];
        if (m->hero == PORT_DUO_HERO_HAMMER && attacker->badgesEquipped.Hammerman) {
            power += attacker->badgesEquipped.Hammerman;
        }
        if (m->hero == PORT_DUO_HERO_STOMP && attacker->badgesEquipped.Jumpman) {
            power += attacker->badgesEquipped.Jumpman;
        }
    } else if (m->second == PORT_DUO_SECOND_HERO) {
        lv = m->partner == PORT_DUO_HERO_HAMMER ? pouchGetPtr()->hammerLevel : pouchGetPtr()->jumpLevel;
        lv = lv < 1 ? 1 : lv > 3 ? 3 : lv;
        power = m->partnerPower[lv - 1];
        if (m->partner == PORT_DUO_HERO_HAMMER && attacker->badgesEquipped.Hammerman) {
            power += attacker->badgesEquipped.Hammerman;
        }
        if (m->partner == PORT_DUO_HERO_STOMP && attacker->badgesEquipped.Jumpman) {
            power += attacker->badgesEquipped.Jumpman;
        }
    } else {
        lv = pouchGetPartyAttackLv((PartyId)BattleTransPartyId(attacker->currentKind));
        lv = lv < 0 ? 0 : lv > 2 ? 2 : lv;
        power = m->partnerPower[lv];
    }
    if (sDuo.result[half] >= 1) {
        power += m->goodBonus;
    }
    if (sDuo.result[half] >= 2) {
        power += m->perfectBonus;
    }
    if (half == 1 && sDuo.result[0] >= 1 && sDuo.result[1] >= 1) {
        power += m->duoBonus;
    }
    return power;
}

/* duoBegin(header weapon, out hero half, out partner half): the move is on;
 * the other's move this turn goes with it */
static USER_FUNC(duoBegin) {
    BattleWork* wp = _battleWorkPointer;
    EvtWord* args = event->args;
    BattleWeapon* header = (BattleWeapon*)evtGetValue(event, args[0]);
    BattleUnit* actor = BattleGetUnitPtr(wp, BattleTransID(event, BTL_UNIT_SELF));
    BattleUnit* other;
    int move = -1;
    (void)isFirstCall;
    memset(&sDuo, 0, sizeof(sDuo));
    if (header == NULL || halfOf(header, &move) != 2 || actor == NULL) {
        evtSetValue(event, args[1], 0);
        evtSetValue(event, args[2], 0);
        return EVT_RETURN_DONE;
    }
    sDuo.active = 1;
    sDuo.actor = actor->unitId;
    sDuo.move = move;
    sDuo.one = onePlayer();
    sDuo.pressedAt[0] = sDuo.pressedAt[1] = -1;
    other = actor == BattleGetMarioPtr(wp) ? secondUnit(wp, current()) : BattleGetMarioPtr(wp);
    if (other != NULL) {
        if (BtlUnit_CheckStatusFlag(other, STATUS_FLAG_EXTRA_MOVE)) {
            BtlUnit_OffStatusFlag(other, STATUS_FLAG_EXTRA_MOVE);
        } else if (other->movesRemaining > 0) {
            other->movesRemaining--;
        }
        if (other->movesRemaining <= 0) {
            other->moveState = 10;
            other->moveColorLv = 0x60;
        }
    }
    evtSetValue(event, args[1], (EvtWord)&sHero[move]);
    evtSetValue(event, args[2], (EvtWord)&sPartner[move]);
    printf("[duo] %s (%s's turn, %s)\n", current()->key, actor->currentKind == UNIT_MARIO ? "Mario" : "the partner",
           sDuo.one ? "one player" : "two players");
    fflush(stdout);
    return EVT_RETURN_DONE;
}

static USER_FUNC(duoEnd) {
    (void)event;
    (void)isFirstCall;
    sDuo.active = 0;
    sDuo.running = 0;
    return EVT_RETURN_DONE;
}

/* duoSecondUnit(out): the second half's unit (BTL_UNIT_PARTNER, or the second hero's id) */
static USER_FUNC(duoSecondUnit) {
    const PortDuoMove* m = current();
    BattleUnit* u = m != NULL && m->second == PORT_DUO_SECOND_HERO ? secondHero(_battleWorkPointer) : NULL;
    (void)isFirstCall;
    evtSetValue(event, event->args[0], u != NULL ? u->unitId : BTL_UNIT_PARTNER);
    return EVT_RETURN_DONE;
}

/* duoKind(0 hero, 1 the second, out): the half's PORT_DUO_HERO_* / PORT_DUO_PARTNER_*
 * (a second hero's: 10 + PORT_DUO_HERO_*) */
static USER_FUNC(duoKind) {
    const PortDuoMove* m = current();
    const int which = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    EvtWord v = 0;
    if (m != NULL) {
        v = which == 0 ? m->hero : m->second == PORT_DUO_SECOND_HERO ? 10 + m->partner : m->partner;
    }
    evtSetValue(event, event->args[1], v);
    return EVT_RETURN_DONE;
}

/* duoPose(0 wait, 1 air, 2 hit, out): the partner's pose name, 0 for none */
static USER_FUNC(duoPose) {
    const PortDuoMove* m = current();
    const int which = (int)evtGetValue(event, event->args[0]);
    const char* pose = NULL;
    (void)isFirstCall;
    if (m != NULL) {
        pose = which == 0 ? m->poseWait : which == 1 ? m->poseAir : m->poseHit;
    }
    evtSetValue(event, event->args[1], (EvtWord)pose);
    return EVT_RETURN_DONE;
}

/* duoHammerPose(0 raise, 1 hold, 2 swing, out): Mario's, by his hammer */
static USER_FUNC(duoHammerPose) {
    static const char* const kPoses[3][3] = {
        {"M_H_1A", "M_H_1B", "M_H_1C"}, {"M_H_5A", "M_H_5B", "M_H_5C"}, {"M_H_9A", "M_H_9B", "M_H_9C"}};
    int lv = pouchGetPtr()->hammerLevel;
    const int which = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    lv = lv < 1 ? 1 : lv > 3 ? 3 : lv;
    evtSetValue(event, event->args[1], (EvtWord)kPoses[lv - 1][which < 0 ? 0 : which > 2 ? 2 : which]);
    return EVT_RETURN_DONE;
}

/* duoParam(0 frames of the partner's leap or dart, 1 it floats, 2 it spins, 3 its hit's sound, out) */
static USER_FUNC(duoParam) {
    const PortDuoMove* m = current();
    const int which = (int)evtGetValue(event, event->args[0]);
    EvtWord v = 0;
    (void)isFirstCall;
    if (m != NULL) {
        switch (which) {
            case 0:
                v = m->airFrames > 0 ? m->airFrames : 1;
                break;
            case 1:
                v = m->floats;
                break;
            case 2:
                v = m->partner == PORT_DUO_PARTNER_DASH && m->partnerKind == UNIT_KOOPS;
                break;
            case 3:
                v = (EvtWord)m->sfxHit;
                break;
        }
    }
    evtSetValue(event, event->args[1], v);
    return EVT_RETURN_DONE;
}

static USER_FUNC(duoSetFlag) {
    const int i = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (i >= 0 && i < FLAG_COUNT) {
        sDuo.flags[i] = 1;
    }
    return EVT_RETURN_DONE;
}

static USER_FUNC(duoWaitFlag) {
    const int i = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (i < 0 || i >= FLAG_COUNT || sDuo.flags[i] || !sDuo.active) {
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

/* duoWaitMark(player, frames): until that many frames from the player's mark (- before it) */
static USER_FUNC(duoWaitMark) {
    const int p = (int)evtGetValue(event, event->args[0]) & 1;
    const int off = (int)evtGetValue(event, event->args[1]);
    (void)isFirstCall;
    if (!sDuo.active || (sDuo.started && sDuo.frame >= sDuo.target[p] + off)) {
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

/* duoWaitHit(player): until the player's press is judged and the mark reached */
static USER_FUNC(duoWaitHit) {
    const int p = (int)evtGetValue(event, event->args[0]) & 1;
    (void)isFirstCall;
    if (!sDuo.active || (sDuo.started && sDuo.judged[p] && sDuo.frame >= sDuo.target[p])) {
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

static USER_FUNC(duoResult) {
    const int p = (int)evtGetValue(event, event->args[0]) & 1;
    (void)isFirstCall;
    evtSetValue(event, event->args[1], sDuo.result[p]);
    return EVT_RETURN_DONE;
}

/* the rating shown on a good hit: Nice, Good; the partner's a step up when both were good */
static USER_FUNC(duoPrize) {
    const int p = (int)evtGetValue(event, event->args[0]) & 1;
    int lv = sDuo.result[p] >= 2 ? 3 : 2;
    (void)isFirstCall;
    if (p == 1 && sDuo.result[0] >= 1) {
        lv++;
        if (sDuo.result[0] >= 2 && sDuo.result[1] >= 2) {
            lv++;
        }
    }
    evtSetValue(event, event->args[1], lv);
    return EVT_RETURN_DONE;
}

/* duoAlive(unit, out): the target is still up to be hit */
static USER_FUNC(duoAlive) {
    BattleUnit* u = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, event->args[0])));
    (void)isFirstCall;
    evtSetValue(event, event->args[1], u != NULL && u->currentHp > 0 && !BtlUnit_CheckStatus(u, STATUS_INSTANT_KILL));
    return EVT_RETURN_DONE;
}

/* --- The command --- */

static int assist(void) {
    return portGameplus("ac_attack_assist"); /* 0 off, 1 wider windows, 2 automatic */
}

static void commandStart(void) {
    const PortDuoMove* m = current();
    int p;
    sDuo.started = 1;
    sDuo.running = 1;
    sDuo.frame = 0;
    for (p = 0; p < 2; p++) {
        BattleUnit* u = halfUnit(p);
        sDuo.window[p] = WINDOW;
        sDuo.perfect[p] = PERFECT;
        sDuo.mashGood[p] = MASH_GOOD;
        sDuo.mashPerfect[p] = MASH_PERFECT;
        if (assist() == 1) {
            sDuo.window[p] *= 2;
            sDuo.perfect[p] = 2;
            sDuo.mashGood[p] = MASH_GOOD * 2 / 3;
            sDuo.mashPerfect[p] = MASH_PERFECT * 2 / 3;
        }
        if (u != NULL && u->badgesEquipped.Simplifier) {
            sDuo.window[p] += 2 * u->badgesEquipped.Simplifier;
            sDuo.mashGood[p] -= 2;
            sDuo.mashPerfect[p] -= 2;
        }
        sDuo.automatic[p] = assist() == 2 || (u != NULL && u->badgesEquipped.AutoCommand);
        sDuo.pad[p] = p; /* the hero's on controller 1, the second half's on controller 2 */
        if (sControllerOf != NULL && u != NULL) {
            const int c = sControllerOf(u);
            if (c == 0 || c == 1) {
                sDuo.pad[p] = c; /* the mode's own (Brothers: Luigi's player 2 whoever acts) */
            }
        }
    }
    if (m != NULL && m->ac == PORT_DUO_AC_MASH) {
        sDuo.target[0] = sDuo.target[1] = MASH_FRAMES;
    } else {
        const int off = m != NULL ? m->partnerOffset : 0;
        sDuo.target[0] = LEAD + (off < 0 ? -off : 0);
        sDuo.target[1] = sDuo.target[0] + off;
    }
}

/* this frame's press for the player's half: two players each on A (player 1
 * the game's first controller, player 2 its second, the guest's online);
 * one player A for the hero and B for the partner */
static int pressed(int p, int held) {
    const u32* buttons = held ? gp->button : gp->buttonNew;
    if (sDuo.one) {
        return (buttons[0] & (p == 0 ? PAD_BUTTON_A : PAD_BUTTON_B)) != 0;
    }
    return (buttons[sDuo.pad[p]] & PAD_BUTTON_A) != 0;
}

static void judgeTimed(int p) {
    const int t = sDuo.target[p], f = sDuo.frame;
    int d;
    if (sDuo.judged[p]) {
        return;
    }
    if (sDuo.automatic[p]) {
        if (f >= t) {
            sDuo.judged[p] = 1;
            sDuo.result[p] = 2;
            sDuo.pressedAt[p] = f;
        }
        return;
    }
    if (f == t - sDuo.window[p] && gPortHapticsCue != NULL && !portOnlineActive()) {
        gPortHapticsCue(sDuo.one ? 0 : sDuo.pad[p], PORT_HAPTICS_ATTACK); /* the window opens (feel only) */
    }
    if (pressed(p, 0) && f >= t - sDuo.window[p] - EARLY) {
        sDuo.judged[p] = 1;
        sDuo.pressedAt[p] = f;
        d = f - t < 0 ? t - f : f - t;
        sDuo.result[p] = f < t - sDuo.window[p] ? 0 : d <= sDuo.perfect[p] ? 2 : d <= sDuo.window[p] ? 1 : 0;
        return;
    }
    if (f > t + sDuo.window[p]) {
        sDuo.judged[p] = 1; /* not pressed in time */
        sDuo.result[p] = 0;
    }
}

static void judgeMash(int p) {
    if (sDuo.judged[p]) {
        return;
    }
    if (sDuo.automatic[p]) {
        sDuo.count[p] = sDuo.mashPerfect[p];
    } else if (pressed(p, 0)) {
        sDuo.count[p]++;
    }
    if (sDuo.frame >= sDuo.target[p]) {
        sDuo.judged[p] = 1;
        sDuo.result[p] = sDuo.count[p] >= sDuo.mashPerfect[p] ? 2 : sDuo.count[p] >= sDuo.mashGood[p] ? 1 : 0;
    }
}

/* --- What the players see --- */

#define TRACK_X0 (-80.0f)
#define TRACK_W 200.0f
#define LINE_AT 160.0f /* the mark's place along the track */
#define ROW_Y0 (-104.0f)
#define ROW_DY 34.0f

static void quad(f32 x0, f32 y0, f32 x1, f32 y1, GXColor c) {
    btlDispGXInit2DRasta();
    GXLoadPosMtxImm(camGetPtr(CAMERA_2D)->view, 0);
    btlDispGXQuads2DRasta(x0, y0, x1, y1, c.r, c.g, c.b, c.a);
}

static GXColor resultColor(int p) {
    if (!sDuo.judged[p]) {
        return (GXColor){0xFF, 0xE8, 0x40, 0xFF};
    }
    return sDuo.result[p] >= 2 ? (GXColor){0xFF, 0xC8, 0x20, 0xFF}
           : sDuo.result[p] == 1 ? (GXColor){0x50, 0xE0, 0x60, 0xFF}
                                 : (GXColor){0xE0, 0x40, 0x40, 0xFF};
}

static void rowDisp(int p, int mash, u8 alpha) {
    const f32 y = ROW_Y0 - ROW_DY * p;
    const f32 t = (f32)(sDuo.target[p] > 0 ? sDuo.target[p] : 1);
    const GXColor back = {0x18, 0x18, 0x28, (u8)(0xC0 * alpha / 0xFF)};
    GXColor c = resultColor(p);
    Vec pos = {TRACK_X0 - 22.0f, y + 2.0f, 0.0f};
    const s32 button = sDuo.one && p == 1 ? PAD_BUTTON_B : PAD_BUTTON_A;
    c.a = alpha;
    quad(TRACK_X0 - 2.0f, y + 9.0f, TRACK_X0 + TRACK_W + 2.0f, y - 9.0f, back);
    if (mash) {
        const f32 full = (f32)(sDuo.mashPerfect[p] > 0 ? sDuo.mashPerfect[p] : 1);
        f32 r = (f32)sDuo.count[p] / full;
        const f32 good = TRACK_X0 + TRACK_W * (f32)sDuo.mashGood[p] / full;
        r = r > 1.0f ? 1.0f : r;
        if (r > 0.0f) {
            quad(TRACK_X0, y + 7.0f, TRACK_X0 + TRACK_W * r, y - 7.0f, c);
        }
        quad(good - 1.0f, y + 9.0f, good + 1.0f, y - 9.0f, (GXColor){0xFF, 0xFF, 0xFF, alpha});
    } else {
        const f32 line = TRACK_X0 + LINE_AT;
        const f32 zone = LINE_AT * (f32)sDuo.window[p] / t;
        const int at = sDuo.judged[p] && sDuo.pressedAt[p] >= 0 ? sDuo.pressedAt[p] : sDuo.frame;
        f32 x = TRACK_X0 + LINE_AT * (f32)at / t;
        x = x > TRACK_X0 + TRACK_W ? TRACK_X0 + TRACK_W : x;
        quad(line - zone, y + 7.0f, line + zone, y - 7.0f, (GXColor){0x30, 0x90, 0x50, (u8)(0xE0 * alpha / 0xFF)});
        quad(line - 1.0f, y + 9.0f, line + 1.0f, y - 9.0f, (GXColor){0xFF, 0xFF, 0xFF, alpha});
        quad(x - 4.0f, y + 8.0f, x + 4.0f, y - 8.0f, c);
    }
    iconDispGx(&pos, ICON_DISP_2D, BattleACGetButtonIcon(button, pressed(p, 1)), 0.8f);
    if (!sDuo.one) {
        const char* tag = sDuo.pad[p] == 0 ? "1P" : "2P";
        FontDrawStart_alpha(alpha);
        FontDrawEdge();
        FontDrawColor(sDuo.pad[p] == 0 ? (GXColor){0xFF, 0x70, 0x60, alpha} : (GXColor){0x6C, 0xC8, 0xFF, alpha});
        FontDrawScale(0.7f);
        FontDrawString(TRACK_X0 - 66.0f, y + 9.0f, tag);
    }
}

static void commandDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    const PortDuoMove* m = current();
    int p, last = sDuo.target[0] > sDuo.target[1] ? sDuo.target[0] : sDuo.target[1];
    u8 alpha = 0xFF;
    (void)camId;
    (void)param;
    if (m == NULL || !sDuo.running) {
        return;
    }
    last += sDuo.window[0] > sDuo.window[1] ? sDuo.window[0] : sDuo.window[1];
    if (sDuo.judged[0] && sDuo.judged[1] && sDuo.frame > last + LINGER - 10) {
        const int left = last + LINGER - sDuo.frame;
        alpha = (u8)(left <= 0 ? 0 : 0xFF * left / 10);
    }
    for (p = 0; p < 2; p++) {
        rowDisp(p, m->ac == PORT_DUO_AC_MASH, alpha);
    }
    portRandSetState(seed);
}

/* duoRunCommand: the command, a frame a call, until both are judged and
 * their results shown for a moment */
static USER_FUNC(duoRunCommand) {
    const PortDuoMove* m = current();
    int p, last;
    if (!sDuo.active || m == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        commandStart();
    }
    portBattleSpeedMark(); /* faster battles stay at 1x while the players act */
    if (m->ac == PORT_DUO_AC_MASH) {
        portMashWanted(sDuo.one ? (PAD_BUTTON_A | PAD_BUTTON_B) : PAD_BUTTON_A);
    }
    for (p = 0; p < 2; p++) {
        if (m->ac == PORT_DUO_AC_MASH) {
            judgeMash(p);
        } else {
            judgeTimed(p);
        }
    }
    dispEntry(CAMERA_2D, 900.0f, 1, commandDisp, NULL);
    last = sDuo.target[0] > sDuo.target[1] ? sDuo.target[0] : sDuo.target[1];
    last += sDuo.window[0] > sDuo.window[1] ? sDuo.window[0] : sDuo.window[1];
    if (sDuo.judged[0] && sDuo.judged[1] && sDuo.frame >= last + LINGER) {
        sDuo.running = 0;
        printf("[duo] results: Mario %d, the partner %d\n", sDuo.result[0], sDuo.result[1]);
        fflush(stdout);
        return EVT_RETURN_DONE;
    }
    sDuo.frame++;
    return EVT_RETURN_BLOCK;
}

/* --- The scripts --- */

/* A half's hit, from its own unit (LW11), with its own weapon (LW13), on the
 * target (LW3, LW4), judged from its player's press (LW10). */
static EVT_BEGIN(duo_strike)
    CALL(duoAlive, LW(3), LW(0))
    IF_INT_EQUAL(LW(0), 0)
        GOTO(90)
    END_IF
    CALL(btlevtcmd_PreCheckDamage, LW(11), LW(3), LW(4), LW(13), DAMAGE_FLAG_LAST_HIT, LW(5))
    IF_INT_NOT_EQUAL(LW(5), 1)
        IF_INT_EQUAL(LW(5), 3)
            CALL(btlevtcmd_StartAvoid, LW(3), 38)
        END_IF
        IF_INT_EQUAL(LW(5), 6)
            CALL(btlevtcmd_StartAvoid, LW(3), 39)
        END_IF
        IF_INT_EQUAL(LW(5), 2)
            CALL(btlevtcmd_StartAvoid, LW(3), 40)
        END_IF
        GOTO(90)
    END_IF
    CALL(duoResult, LW(10), LW(6))
    IF_INT_EQUAL(LW(6), 0)
        CALL(btlevtcmd_CheckDamage, LW(11), LW(3), LW(4), LW(13), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_NO_STATUS, LW(5))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(13), -1)
        GOTO(90)
    END_IF
    CALL(btlevtcmd_CheckDamage, LW(11), LW(3), LW(4), LW(13), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
    CALL(duoPrize, LW(10), LW(6))
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_AudienceDeclareACResult, LW(13), LW(6))
    LABEL(90)
    RETURN
EVT_END()

/* A unit (LW11) hops back off the enemy and walks home. */
static EVT_BEGIN(duo_go_home)
    CALL(btlevtcmd_SetFallAccel, LW(11), FLOAT(0.69921875f))
    CALL(btlevtcmd_GetPos, LW(11), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetHomePos, LW(11), EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_FaceDirectionSub, LW(11), LW(0), 40)
    CALL(btlevtcmd_JumpPosition, LW(11), LW(0), LW(1), LW(2), 22, -1)
    CALL(btlevtcmd_SetRotate, LW(11), 0, 0, 0)
    CALL(btlevtcmd_SetMoveSpeed, LW(11), FLOAT(6.0f))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_WALK)
    CALL(btlevtcmd_GetHomePos, LW(11), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, LW(11), LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_ResetFaceDirection, LW(11))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_STAY)
    RETURN
EVT_END()

/* Mario's stomp: to his take-off spot, then a jump that lands on his mark. */
static EVT_BEGIN(duo_hero_stomp)
    CALL(btlevtcmd_JumpSetting, LW(11), 20, FLOAT(0.0f), FLOAT(0.69921875f))
    CALL(btlevtcmd_SetMoveSpeed, LW(11), FLOAT(4.0f))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetTakeoffPosition, LW(11), LW(3), LW(4), LW(0), LW(1), LW(2))
    SET_VALUE(LW(6), LW(10))
    MULTIPLY(LW(6), 30)
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), LW(6))
    SET_VALUE(LW(6), LW(10))
    MULTIPLY(LW(6), 15)
    ADD(LW(2), LW(6))
    CALL(btlevtcmd_CalculateFaceDirection, LW(11), -1, LW(0), LW(2), 1, LW(6))
    CALL(btlevtcmd_ChangeFaceDirection, LW(11), LW(6))
    CALL(btlevtcmd_MovePosition, LW(11), LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_CalculateFaceDirection, LW(11), -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, LW(11), LW(15))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_STAY)
    CALL(duoSetFlag, LW(10))
    CALL(duoWaitMark, LW(10), -30)
    CALL(btlevtcmd_AnimeChangePose, LW(11), 1, STRING("M_J_1B"))
    CALL(btlevtcmd_snd_se, LW(11), STRING("SFX_VOICE_MARIO_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    ADD(LW(2), 5)
    SET_VALUE(LW(6), LW(10))
    MULTIPLY(LW(6), 10)
    ADD(LW(2), LW(6))
    SET_VALUE(LW(6), 30)
    CALL(btlevtcmd_MarioJumpParam, LW(11), LW(0), LW(1), LW(2), LW(6))
    CALL(btlevtcmd_MarioJumpPosition, LW(11), LW(0), LW(1), LW(2), LW(6), 0, -1)
    CALL(btlevtcmd_AnimeChangePose, LW(11), 1, STRING("M_J_1D"))
    CALL(duoWaitHit, LW(10))
    CALL_CHILD(duo_strike)
    CALL(btlevtcmd_AnimeChangePose, LW(11), 1, STRING("M_J_1B"))
    CALL_CHILD(duo_go_home)
    RETURN
EVT_END()

/* Mario's hammer: in front of the enemy, raised, swung as his mark comes. */
static EVT_BEGIN(duo_hero_hammer)
    CALL(btlevtcmd_SetMoveSpeed, LW(11), FLOAT(4.0f))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetPos, LW(11), EVT_NULLPTR, LW(1), EVT_NULLPTR)
    SET_VALUE(LW(6), LW(10))
    MULTIPLY(LW(6), 30)
    ADD(LW(6), 35)
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), LW(6))
    SET_VALUE(LW(6), LW(10))
    MULTIPLY(LW(6), 20)
    ADD(LW(6), 5)
    ADD(LW(2), LW(6))
    CALL(btlevtcmd_CalculateFaceDirection, LW(11), -1, LW(0), LW(2), 1, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, LW(11), LW(15))
    CALL(btlevtcmd_MovePosition, LW(11), LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_CalculateFaceDirection, LW(11), -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, LW(11), LW(15))
    CALL(duoHammerPose, 0, LW(7))
    CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    CALL(duoSetFlag, LW(10))
    CALL(duoWaitMark, LW(10), -20)
    CALL(btlevtcmd_snd_se, LW(11), STRING("SFX_VOICE_MARIO_POWER2"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(duoHammerPose, 1, LW(7))
    CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    CALL(duoWaitHit, LW(10))
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_HAMMER_SWING1"), 0)
    CALL(duoHammerPose, 2, LW(7))
    CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    WAIT_FRAMES(7)
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_HAMMER_HIT_GROUND1"), 0)
    CALL_CHILD(duo_strike)
    WAIT_FRAMES(10)
    CALL(btlevtcmd_SetMoveSpeed, LW(11), FLOAT(6.0f))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_WALK)
    CALL(btlevtcmd_GetHomePos, LW(11), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, LW(11), LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_ResetFaceDirection, LW(11))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_STAY)
    RETURN
EVT_END()

/* The partner's leap (Goombella, Yoshi, Flurrie): to a take-off spot a step
 * behind Mario's, then a jump that lands on its mark. */
static EVT_BEGIN(duo_partner_leap)
    CALL(btlevtcmd_SetMoveSpeed, LW(11), FLOAT(4.0f))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetTakeoffPosition, LW(11), LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetPos, LW(11), EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 30)
    ADD(LW(2), 15)
    CALL(btlevtcmd_CalculateFaceDirection, LW(11), -1, LW(0), LW(2), 1, LW(6))
    CALL(btlevtcmd_ChangeFaceDirection, LW(11), LW(6))
    CALL(btlevtcmd_MovePosition, LW(11), LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_CalculateFaceDirection, LW(11), -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, LW(11), LW(15))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_STAY)
    CALL(duoPose, 0, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    END_IF
    CALL(duoSetFlag, LW(10))
    CALL(duoParam, 0, LW(9))
    SET_VALUE(LW(8), 0)
    SUBTRACT(LW(8), LW(9))
    CALL(duoWaitMark, LW(10), LW(8))
    CALL(duoParam, 1, LW(8))
    IF_INT_NOT_EQUAL(LW(8), 0)
        CALL(btlevtcmd_OffUnitFlag, LW(11), UNIT_FLAG_FLOAT_ACTIVE)
    END_IF
    CALL(duoPose, 1, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    END_IF
    CALL(btlevtcmd_snd_se, LW(11), STRING("SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_SetFallAccel, LW(11), FLOAT(0.5f))
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    ADD(LW(2), 10)
    CALL(btlevtcmd_JumpPosition, LW(11), LW(0), LW(1), LW(2), LW(9), -1)
    CALL(duoPose, 2, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    END_IF
    CALL(duoWaitHit, LW(10))
    CALL(duoParam, 3, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_snd_se, LW(11), LW(7), EVT_NULLPTR, 0, EVT_NULLPTR)
    END_IF
    CALL_CHILD(duo_strike)
    CALL(duoPose, 1, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    END_IF
    CALL_CHILD(duo_go_home)
    CALL(duoParam, 1, LW(8))
    IF_INT_NOT_EQUAL(LW(8), 0)
        CALL(btlevtcmd_OnUnitFlag, LW(11), UNIT_FLAG_FLOAT_ACTIVE)
    END_IF
    RETURN
EVT_END()

/* The partner's dart (Koops's shell, Vivian's Shade Fist, Ms. Mowz's slap):
 * winds up a little way in front of the enemy, then darts into it to hit on
 * its mark. */
static EVT_BEGIN(duo_partner_dash)
    CALL(btlevtcmd_SetMoveSpeed, LW(11), FLOAT(4.0f))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetPos, LW(11), EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 75)
    ADD(LW(2), 20)
    CALL(btlevtcmd_CalculateFaceDirection, LW(11), -1, LW(0), LW(2), 1, LW(6))
    CALL(btlevtcmd_ChangeFaceDirection, LW(11), LW(6))
    CALL(btlevtcmd_MovePosition, LW(11), LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_CalculateFaceDirection, LW(11), -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, LW(11), LW(15))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_STAY)
    CALL(duoPose, 0, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    END_IF
    CALL(duoSetFlag, LW(10))
    CALL(duoParam, 0, LW(9))
    SET_VALUE(LW(8), 0)
    SUBTRACT(LW(8), LW(9))
    CALL(duoWaitMark, LW(10), LW(8))
    CALL(duoPose, 1, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    END_IF
    CALL(duoParam, 2, LW(8))
    IF_INT_NOT_EQUAL(LW(8), 0)
        BEGIN_SIBLING_EVENT
            LOOP(LW(9))
                CALL(btlevtcmd_AddRotate, LW(11), 0, 30, 0)
                WAIT_FRAMES(1)
            END_LOOP
        END_SIBLING_EVENT
    END_IF
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetPos, LW(11), EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 10)
    ADD(LW(2), 20)
    CALL(btlevtcmd_MovePosition, LW(11), LW(0), LW(1), LW(2), LW(9), -1, 0)
    CALL(duoPose, 2, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    END_IF
    CALL(duoWaitHit, LW(10))
    CALL(duoParam, 3, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_snd_se, LW(11), LW(7), EVT_NULLPTR, 0, EVT_NULLPTR)
    END_IF
    CALL_CHILD(duo_strike)
    WAIT_FRAMES(8)
    CALL(btlevtcmd_SetRotate, LW(11), 0, 0, 0)
    CALL_CHILD(duo_go_home)
    RETURN
EVT_END()

/* Bobbery's blast: he walks up to the enemy, glows, and blows up on his mark;
 * the blast throws him home. */
static EVT_BEGIN(duo_partner_blast)
    CALL(btlevtcmd_SetMoveSpeed, LW(11), FLOAT(3.0f))
    CALL(duoPose, 1, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    ELSE
        CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_WALK)
    END_IF
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetPos, LW(11), EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 25)
    ADD(LW(2), 20)
    CALL(btlevtcmd_CalculateFaceDirection, LW(11), -1, LW(0), LW(2), 1, LW(6))
    CALL(btlevtcmd_ChangeFaceDirection, LW(11), LW(6))
    CALL(btlevtcmd_MovePosition, LW(11), LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_CalculateFaceDirection, LW(11), -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, LW(11), LW(15))
    CALL(duoPose, 0, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(11), 1, LW(7))
    END_IF
    CALL(duoSetFlag, LW(10))
    CALL(duoWaitHit, LW(10))
    CALL(btlevtcmd_GetPos, LW(11), LW(0), LW(1), LW(2))
    CALL(evt_eff64, STRING(""), STRING("bomb_n64"), 0, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    CALL(duoParam, 3, LW(7))
    IF_INT_NOT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_snd_se, LW(11), LW(7), EVT_NULLPTR, 0, EVT_NULLPTR)
    END_IF
    CALL(evt_btl_camera_shake_h, 0, 4, 0, 10, 13)
    CALL_CHILD(duo_strike)
    CALL(btlevtcmd_AnimeChangePose, LW(11), 1, STRING("J_1A"))
    CALL(btlevtcmd_SetFallAccel, LW(11), FLOAT(0.5f))
    CALL(btlevtcmd_GetHomePos, LW(11), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_JumpPosition, LW(11), LW(0), LW(1), LW(2), 50, -1)
    CALL(btlevtcmd_ResetFaceDirection, LW(11))
    CALL(btlevtcmd_AnimeChangePoseType, LW(11), 1, BTL_POSE_STAY)
    RETURN
EVT_END()

/* Every duo move: the target, the FP, the other's move; the two halves side
 * by side; the command once both are in place. */
EVT_BEGIN(duo_attack_event)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(duoBegin, LW(12), LW(13), LW(14))
    IF_INT_EQUAL(LW(13), 0)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF) /* (once the move is sure: no FP lost) */
    CALL(duoSecondUnit, LW(15))
    CALL(btlevtcmd_StopWaitEvent, BTL_UNIT_MARIO)
    CALL(btlevtcmd_StopWaitEvent, LW(15))
    CALL(evt_btl_camera_set_mode, 0, 11)
    CALL(evt_btl_camera_set_homing_unitparts, 0, BTL_UNIT_MARIO, 1, LW(3), LW(4))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    BEGIN_SIBLING_EVENT
        SET_VALUE(LW(10), 0)
        SET_VALUE(LW(11), BTL_UNIT_MARIO)
        CALL(duoKind, 0, LW(0))
        IF_INT_EQUAL(LW(0), PORT_DUO_HERO_HAMMER)
            CALL_CHILD(duo_hero_hammer)
        ELSE
            CALL_CHILD(duo_hero_stomp)
        END_IF
        CALL(duoSetFlag, FLAG_HERO_DONE)
    END_SIBLING_EVENT
    SET_VALUE(LW(10), 1)
    SET_VALUE(LW(11), LW(15))
    SET_VALUE(LW(13), LW(14))
    BEGIN_SIBLING_EVENT
        CALL(duoKind, 1, LW(0))
        SWITCH(LW(0))
            CASE_EQUAL(PORT_DUO_PARTNER_DASH)
                CALL_CHILD(duo_partner_dash)
            CASE_EQUAL(PORT_DUO_PARTNER_BLAST)
                CALL_CHILD(duo_partner_blast)
            CASE_EQUAL(10 + PORT_DUO_HERO_STOMP)
                CALL_CHILD(duo_hero_stomp)
            CASE_EQUAL(10 + PORT_DUO_HERO_HAMMER)
                CALL_CHILD(duo_hero_hammer)
            CASE_DEFAULT
                CALL_CHILD(duo_partner_leap)
        END_SWITCH
        CALL(duoSetFlag, FLAG_PARTNER_DONE)
    END_SIBLING_EVENT
    CALL(duoWaitFlag, FLAG_HERO_READY)
    CALL(duoWaitFlag, FLAG_PARTNER_READY)
    CALL(duoRunCommand)
    CALL(duoWaitFlag, FLAG_HERO_DONE)
    CALL(duoWaitFlag, FLAG_PARTNER_DONE)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    LABEL(99)
    CALL(duoSecondUnit, LW(15))
    CALL(duoEnd)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_MARIO)
    CALL(btlevtcmd_ResetFaceDirection, LW(15))
    CALL_CHILD(btldefaultevt_SuitoruBadgeEffect)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_MARIO)
    CALL(btlevtcmd_StartWaitEvent, LW(15))
    RETURN
EVT_END()
