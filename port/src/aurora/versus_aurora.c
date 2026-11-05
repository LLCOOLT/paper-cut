/* Versus battles (port/include/port/versus.h, docs/VERSUS.md): the set-up
 * kept between fights, the fight's own map and battle setup, and the hooks
 * that make the enemy side Luigi's: his units entered from his pouch, the
 * game's "Mario", "partner" and pouch his while his side acts, the second
 * controller read as the first then, and the defender's guards on every hit.
 *
 * The fight is on a map of its own in the stage's area (tou_m2 with tou_03's
 * files for the Glitz Pit, kStages, as the modes' maps: port/src/aurora/modes_aurora.c), whose
 * script starts the battle with an invisible stand-in NPC and, after it,
 * goes back to the room the fight was started in. Both sides' pouches are
 * made from the set-up and the file's own is put back afterwards.
 *
 * Every hook returns at once unless a versus fight is on (sState), so the
 * game's own battles, replays and online play are as they were. */
#include "port/versus.h"
#include "port/platform.h"
#include "port/mod_host.h"
#include "port/mods.h"
#include "port/hook_list.h"
#include "port/settings.h"
#include "port/pause_forms.h"
#include "battle/battle.h"
#include "battle/battle_ac.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_sub.h"
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
#include "driver/msgdrv.h"
#include "manager/fontmgr.h"
#include "win/win_main.h"
#include "port/luigi.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/tr.h"

#include <SDL3/SDL_timer.h>
#include <dolphin/pad.h>
#include <stdarg.h>
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
static PortVersusSetup sFight; /* the fight's (the set-up as it was at its start; on the ladder, Luigi's side the rung's) */
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

/* one line of versus.txt ("key=value") into sSetup */
static void parseLine(char* line) {
    char* eq = strchr(line, '=');
    PortVersusSide* d;
    int side;
    if (eq == NULL) {
        return;
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
        return;
    }
    side = line[1] - '0';
    d = &sSetup.side[side];
    if (strcmp(line + 3, "cpu") == 0) {
        /* 0 a player; 1, 2, 3 the computer, easy, normal, hard */
        const int v = atoi(eq);
        d->controller = v > 0 ? PORT_VERSUS_COMPUTER : PORT_VERSUS_LOCAL;
        d->skill = v > 0 ? v - 1 : PORT_VERSUS_NORMAL;
    } else if (strcmp(line + 3, "level") == 0) {
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

/* whatever the text says, a set-up the page can show */
static void clampSetup(void) {
    int side;
    for (side = 0; side < 2; side++) {
        PortVersusSide* d = &sSetup.side[side];
        int k;
        d->level = d->level < 1 ? 1 : d->level > 30 ? 30 : d->level;
        d->gear = d->gear < 1 ? 1 : d->gear > 3 ? 3 : d->gear;
        d->partnerRank = d->partnerRank < 0 ? 0 : d->partnerRank > 2 ? 2 : d->partnerRank;
        /* (each alone first: three large ones from a crafted text would overflow the sum) */
        if (d->hpUps < 0 || d->fpUps < 0 || d->bpUps < 0 || d->hpUps > 29 || d->fpUps > 29 || d->bpUps > 29 ||
            d->hpUps + d->fpUps + d->bpUps > d->level - 1) {
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
        if (side == 0 || d->controller != PORT_VERSUS_COMPUTER) {
            d->controller = PORT_VERSUS_LOCAL; /* (the computer plays Luigi's side only) */
        }
        d->skill = d->skill < PORT_VERSUS_EASY ? PORT_VERSUS_EASY : d->skill > PORT_VERSUS_HARD ? PORT_VERSUS_HARD : d->skill;
    }
    sSetup.rules.turnTimer = sSetup.rules.turnTimer < 0 ? 0 : sSetup.rules.turnTimer > 120 ? 120 : sSetup.rules.turnTimer;
    sSetup.rules.stage = sSetup.rules.stage < 0 || sSetup.rules.stage >= PORT_VERSUS_STAGES ? 0 : sSetup.rules.stage;
}

static void readSetup(void) {
    char path[512], line[512];
    FILE* f;
    sSetupRead = 1;
    portVersusSetupDefaults(&sSetup);
    setupPath(path, sizeof(path));
    if ((f = fopen(path, "r")) != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            parseLine(line);
        }
        fclose(f);
    }
    clampSetup();
}

/* an online session's set-up kept for the run, not this player's versus.txt */
static int sHeld;

PortVersusSetup* portVersusSetup(void) {
    if (!sSetupRead) {
        readSetup();
    }
    return &sSetup;
}

void portVersusSetupForSession(const char* text, unsigned len, int hold) {
    char line[512];
    unsigned at = 0;
    portVersusSetupDefaults(&sSetup);
    while (text != NULL && at < len) {
        unsigned n = 0;
        while (at < len && text[at] != '\n') {
            if (n + 1 < sizeof(line)) {
                line[n++] = text[at];
            }
            at++;
        }
        at++;
        line[n] = '\0';
        parseLine(line);
    }
    clampSetup();
    sSetupRead = 1;
    sHeld = hold;
}

/* text appended at out + *at (kept within size) */
static void put(char* out, unsigned size, unsigned* at, const char* fmt, ...) {
    va_list ap;
    int n;
    if (*at >= size) {
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(out + *at, size - *at, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *at = *at + (unsigned)n < size ? *at + (unsigned)n : size - 1;
    }
}

static void putList(char* out, unsigned size, unsigned* at, const char* key, int side, const short* v, int n) {
    int k, first = 1;
    put(out, size, at, "s%d.%s=", side, key);
    for (k = 0; k < n; k++) {
        if (v[k] != 0) {
            put(out, size, at, first ? "%d" : ",%d", v[k]);
            first = 0;
        }
    }
    put(out, size, at, "\n");
}

int portVersusSetupText(char* out, unsigned size) {
    const PortVersusSetup* s = portVersusSetup();
    unsigned at = 0;
    int i;
    if (size == 0) {
        return 0;
    }
    out[0] = '\0';
    put(out, size, &at, "stage=%d\ntimer=%d\nluck=%d\n", s->rules.stage, s->rules.turnTimer, s->rules.luckBadges);
    for (i = 0; i < 2; i++) {
        const PortVersusSide* d = &s->side[i];
        short p[PORT_VERSUS_PARTNERS];
        int k;
        put(out, size, &at, "s%d.level=%d\ns%d.ups=%d,%d,%d\ns%d.gear=%d\ns%d.rank=%d\ns%d.cpu=%d\n", i, d->level, i,
            d->hpUps, d->fpUps, d->bpUps, i, d->gear, i, d->partnerRank, i,
            d->controller == PORT_VERSUS_COMPUTER ? d->skill + 1 : 0);
        for (k = 0; k < PORT_VERSUS_PARTNERS; k++) {
            p[k] = (short)d->partners[k];
        }
        putList(out, size, &at, "partners", i, p, PORT_VERSUS_PARTNERS);
        putList(out, size, &at, "badges", i, d->badges, PORT_VERSUS_BADGES);
        putList(out, size, &at, "items", i, d->items, PORT_VERSUS_ITEMS);
    }
    return (int)at;
}

void portVersusSave(void) {
    char path[512], text[2048];
    FILE* f;
    const int n = portVersusSetupText(text, sizeof(text));
    if (sHeld) {
        return; /* an online session's set-up (the host's): this player's own file stays as it was */
    }
    setupPath(path, sizeof(path));
    if ((f = fopen(path, "w")) == NULL) {
        return;
    }
    fwrite(text, 1, (size_t)n, f);
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
static int sCmdFrames;      /* frames the command being chosen has taken (the turn timer: frames, the same in every game of an online session) */
static int sController[2];  /* who plays each side this fight (PORT_VERSUS_LOCAL, PORT_VERSUS_COMPUTER) */
static int sSkill;          /* the computer's skill this fight */

int portVersusOn(void) {
    return sState == STATE_GOING || sState == STATE_FIGHT;
}

int portVersusBusy(void) {
    return sState != STATE_NONE;
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

/* both players have a controller of their own (or the tests' second); online
 * always: the guest's is the second (the controllers connected here are this
 * game's alone, and both games must read the same) */
static int twoPads(void) {
    static int sTest = -1;
    if (portOnlineActive()) {
        return 1;
    }
    if (sTest < 0) {
        sTest = getenv("TTYD_P2_PRESS") != NULL;
    }
    return sTest || PADGetIndexForPort(1) >= 0;
}

/* the computer plays the side */
static int computer(int side) {
    return sController[side] == PORT_VERSUS_COMPUTER;
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
    buildPouch(&sFight.side[1]);
    sOther = *pouchGetPtr();
    buildPouch(&sFight.side[0]);
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
        if (sFight.side[side].partners[k] > 0) {
            return sFight.side[side].partners[k];
        }
    }
    return 0;
}

/* --- the ladder --- */

/* The endless ladder: Mario's side as set up against a computer Luigi, fight
 * after fight in the same ring until Mario falls; each rung's Luigi a level
 * higher, stronger in gear, partners, badges and items, and harder to play
 * against. Each rung's side is made from its number alone (random numbers
 * from it), so it is the same every time and in every game of an online
 * session. The most rungs won is kept in <user>/versus_ladder.txt. */
static int sLadder;          /* the rung being fought (0: no ladder) */
static int sLadderLast = -1; /* the last ladder's rungs won (-1: none yet) */
static int sLadderBest = -1; /* the most rungs won (-1: not read yet) */

static unsigned sLadderRng;

static unsigned ladderNext(void) {
    sLadderRng ^= sLadderRng << 13;
    sLadderRng ^= sLadderRng >> 17;
    sLadderRng ^= sLadderRng << 5;
    return sLadderRng;
}

static int ladderPick(int n) {
    return n > 0 ? (int)(ladderNext() % (unsigned)n) : 0;
}

static void ladderPath(char* out, size_t size) {
    snprintf(out, size, "%s/versus_ladder.txt", portSettingsUserPath());
}

static void ladderReadBest(void) {
    char path[512];
    FILE* f;
    if (sLadderBest >= 0) {
        return;
    }
    sLadderBest = 0;
    ladderPath(path, sizeof(path));
    if ((f = fopen(path, "r")) != NULL) {
        if (fscanf(f, "best=%d", &sLadderBest) != 1 || sLadderBest < 0) {
            sLadderBest = 0;
        }
        fclose(f);
    }
}

/* a rung won: the best kept */
static void ladderRecord(int won) {
    char path[512];
    FILE* f;
    ladderReadBest();
    if (won <= sLadderBest) {
        return;
    }
    sLadderBest = won;
    ladderPath(path, sizeof(path));
    if ((f = fopen(path, "w")) != NULL) {
        fprintf(f, "best=%d\n", sLadderBest);
        fclose(f);
    }
}

/* Luigi's side for a rung, against Mario's side as set up now */
static void ladderSide(int rung, PortVersusSide* d) {
    static const short kBadges[] = {
        ITEM_POWER_JUMP,   ITEM_POWER_SMASH,  ITEM_MULTIBOUNCE, ITEM_QUAKE_HAMMER, ITEM_DEFEND_PLUS,
        ITEM_POWER_PLUS,   ITEM_DAMAGE_DODGE, ITEM_HP_PLUS,     ITEM_FP_PLUS,      ITEM_POWER_BOUNCE,
        ITEM_FIRE_DRIVE,   ITEM_ICE_SMASH,    ITEM_HEAD_RATTLE, ITEM_PIERCING_BLOW, ITEM_TORNADO_JUMP,
        ITEM_SLEEPY_STOMP, ITEM_SOFT_STOMP,   ITEM_HAMMER_THROW, ITEM_MEGA_JUMP,   ITEM_MEGA_SMASH,
        ITEM_POWER_PLUS_P, ITEM_DEFEND_PLUS_P, ITEM_DAMAGE_DODGE_P, ITEM_HP_PLUS_P, ITEM_FP_PLUS,
    };
    static const short kItems[] = {
        ITEM_MUSHROOM,     ITEM_HONEY_SYRUP,  ITEM_FIRE_FLOWER, ITEM_SUPER_SHROOM, ITEM_THUNDER_RAGE,
        ITEM_ICE_STORM,    ITEM_MAPLE_SYRUP,  ITEM_POW_BLOCK,   ITEM_ULTRA_SHROOM, ITEM_EARTH_QUAKE,
    };
    const PortVersusSide* mario = &portVersusSetup()->side[0];
    int k, n, tries, free[PARTNER_MSMOWZ], frees = 0;
    sLadderRng = 0x9E3779B9u * (unsigned)rung ^ 0x7F4A7C15u;
    if (sLadderRng == 0) {
        sLadderRng = 1;
    }
    ladderNext();
    memset(d, 0, sizeof(*d));
    d->level = mario->level - 3 + rung;
    d->level = d->level < 1 ? 1 : d->level > 30 ? 30 : d->level;
    for (k = 0; k < d->level - 1; k++) {
        const int r = ladderPick(4); /* half to HP, a quarter each to FP and BP */
        if (r < 2) {
            d->hpUps++;
        } else if (r == 2) {
            d->fpUps++;
        } else {
            d->bpUps++;
        }
    }
    d->gear = d->level < 8 ? 1 : d->level < 18 ? 2 : 3;
    d->partnerRank = d->level < 10 && rung < 4 ? 0 : d->level < 20 && rung < 8 ? 1 : 2;
    /* three partners Mario's side hasn't */
    for (k = 1; k <= PARTNER_MSMOWZ; k++) {
        int j, taken = 0;
        for (j = 0; j < PORT_VERSUS_PARTNERS; j++) {
            taken |= mario->partners[j] == k;
        }
        if (!taken) {
            free[frees++] = k;
        }
    }
    for (k = 0; k < PORT_VERSUS_PARTNERS && frees > 0; k++) {
        const int at = ladderPick(frees);
        d->partners[k] = free[at];
        free[at] = free[--frees];
    }
    /* badges as the BP allow (a few tries at the pool) */
    n = 0;
    for (tries = 0; tries < 40 && n < PORT_VERSUS_BADGES; tries++) {
        const short b = kBadges[ladderPick((int)(sizeof(kBadges) / sizeof(kBadges[0])))];
        if (!portVersusBadgeAllowed(b, NULL) || itemDataTable[b].bp_cost <= 0 ||
            portVersusBpUsed(d) + itemDataTable[b].bp_cost > portVersusBp(d)) {
            continue;
        }
        d->badges[n++] = b;
    }
    /* two items, one more each two rungs, up to six; the strongest from the fifth rung */
    n = 2 + rung / 2;
    n = n > PORT_VERSUS_ITEMS ? PORT_VERSUS_ITEMS : n;
    for (k = 0; k < n; k++) {
        const int pool = rung < 5 ? 8 : (int)(sizeof(kItems) / sizeof(kItems[0]));
        d->items[k] = kItems[ladderPick(pool)];
    }
    d->controller = PORT_VERSUS_COMPUTER;
    d->skill = rung <= 2 ? PORT_VERSUS_EASY : rung <= 5 ? PORT_VERSUS_NORMAL : PORT_VERSUS_HARD;
}

/* --- the stages --- */

/* Where the fight is: a map of the fight's own in a chapter's area (the
 * area's REL and one of its rooms' files, as the modes' maps), and one of
 * the area's battle setups, for its stage, audience and music. The name is
 * the game's own for the place (the journal's map), the Glitz Pit's the
 * port's. */
typedef struct VersusStage {
    const char* map;   /* the fight's map (portModsAddHostMap) */
    const char* files; /* the room whose files it reads */
    int setup;         /* the area's btlsetup table entry */
    const char* name;  /* the game's message for the place, or NULL: the Glitz Pit */
} VersusStage;

static const VersusStage kStages[PORT_VERSUS_STAGES] = {
    {"tou_m2", "tou_03", 1, NULL},                /* the Glitz Pit: its first match */
    {"hei_v0", "hei_01", 4, "menu_monosiri_hei"}, /* Petal Meadows */
    {"gon_v0", "gon_02", 3, "menu_monosiri_gon"}, /* Hooktail Castle */
    {"win_v0", "win_00", 1, "menu_monosiri_win"}, /* Boggly Woods */
    {"jin_v0", "jin_00", 8, "menu_monosiri_jin"}, /* Creepy Steeple */
    {"muj_v0", "muj_02", 9, "menu_monosiri_muj"}, /* Keelhaul Key */
    {"las_v0", "las_17", 9, "menu_monosiri_las"}, /* the Palace of Shadow */
    {"tik_v0", "tik_01", 5, "menu_monosiri_tik"}, /* Rogueport Sewers */
};

static int clampStage(int stage) {
    return stage < 0 || stage >= PORT_VERSUS_STAGES ? 0 : stage;
}

const char* portVersusStageName(int stage) {
    const VersusStage* s = &kStages[clampStage(stage)];
    return s->name != NULL ? msgSearch(s->name) : portTrGame("Glitz Pit");
}

/* the fight's stage: the set-up's; on the ladder, the next one each rung */
static int sStage;

static int fightStage(void) {
    const int first = clampStage(portVersusSetup()->rules.stage);
    return sLadder > 0 ? (first + sLadder - 1) % PORT_VERSUS_STAGES : first;
}

static int versusMap(const char* map) {
    int i;
    for (i = 0; i < PORT_VERSUS_STAGES; i++) {
        if (strcmp(map, kStages[i].map) == 0) {
            return 1;
        }
    }
    return 0;
}

/* --- the map and its script --- */

/* the fight's battle: the stage's setup (its stage, audience and music: the
 * area's REL's table), no enemies of the game's own: Luigi's side is entered
 * by unitSetupPost */
static NpcSetupBattleInfo sInfo;
static NpcBattleSelect sSelect[2];
static BattleGroupSetup sGroup;
static BattleUnitSetup sNoEnemy;
static PointDropData sNoDrops[1]; /* the drop tables' end only (the battle's end reads them, whatever the flags say) */

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
    const VersusStage* stage = &kStages[sStage];
    char area[4];
    AreaData* ad;
    const NpcSetupBattleInfo* tbl;
    NpcEntry* npc = npcNameToPtr_NoAssert(name);
    (void)isFirstCall;
    snprintf(area, sizeof(area), "%.3s", stage->map);
    ad = areaDataPtr(area);
    tbl = ad != NULL ? (const NpcSetupBattleInfo*)ad->btlSetupTbl : NULL;
    if (sState != STATE_GOING || npc == NULL || tbl == NULL || tbl[stage->setup].select == NULL) {
        evtSetValue(event, args[1], 0);
        return EVT_RETURN_DONE;
    }
    sFight = sSetup; /* (the ladder's: Luigi's side the rung's) */
    if (sLadder > 0) {
        ladderSide(sLadder, &sFight.side[1]);
    }
    sInfo = tbl[stage->setup]; /* its stage, audience and music */
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
    sGroup.hpDropTable = sNoDrops;
    sGroup.fpDropTable = sNoDrops;
    memset(sSelect, 0, sizeof(sSelect));
    sSelect[0].weight = 1;
    sSelect[0].groupSetup = &sGroup;
    sSelect[0].stageData = tbl[stage->setup].select[0].stageData;
    sInfo.select = sSelect;
    npcSetupBattleInfo(npc, &sInfo);
    buildPouches();
    sController[0] = PORT_VERSUS_LOCAL;
    sController[1] = sFight.side[1].controller;
    sSkill = sFight.side[1].skill;
    portVersusCpuReset(sSkill);
    sState = STATE_FIGHT;
    sWinner = -1;
    printf("[versus] the fight starts: Mario (level %d) vs Luigi (level %d)\n", sFight.side[0].level,
           sFight.side[1].level);
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
    if (sLadder > 0 && sWinner == 0) {
        /* the ladder: the next rung in the same ring (the map again: its script starts the next fight) */
        ladderRecord(sLadder);
        sLadder++;
        printf("[versus] the ladder: rung %d next\n", sLadder);
        fflush(stdout);
        sState = STATE_GOING;
        sStage = fightStage();
        seqSetSeq(SEQ_MAP_CHANGE, kStages[sStage].map, NULL);
        return EVT_RETURN_DONE;
    }
    if (sLadder > 0) {
        sLadderLast = sLadder - 1; /* (the rungs won) */
        sLadder = 0;
    }
    sState = STATE_OVER;
    seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "gor_01", NULL);
    return EVT_RETURN_DONE;
}

/* An online versus room (the host's option, sent with the session's start):
 * every game of the session opens the Start menu on the set-up page at the
 * first frame the menu could open in the field with a file in play, the
 * same frame in each (its state is the same in each). Once a session. */
static int sRoom;

void portVersusRoomSet(int on) {
    sRoom = on != 0;
}

int portVersusRoomPoll(void) {
    if (!sRoom || !portOnlineActive() || gp == NULL || gp->inBattle || seqGetSeq() != SEQ_GAME || portVersusOn()) {
        return 0;
    }
    sRoom = 0;
    printf("[versus] the versus room: the set-up page\n");
    fflush(stdout);
    return 1;
}

int portVersusWinner(void) {
    return sWinner;
}

int portVersusLadder(int* best, int* last) {
    ladderReadBest();
    *best = sLadderBest;
    *last = sLadderLast;
    return sLadder;
}

/* Not in replays: the set-up is this player's versus.txt, which a replay
 * doesn't keep (online, the host's is sent: online.cpp) */
static int replayRefuses(const char** why) {
    if (portReplayActive()) {
        *why = "Not during a replay.";
        return 1;
    }
    return 0;
}

int portVersusLadderStart(const char** why) {
    PortVersusSetup s = *portVersusSetup();
    const char* bad;
    if (replayRefuses(why)) {
        return 0;
    }
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (sState != STATE_NONE && sState != STATE_OVER) {
        *why = "A versus fight is on already.";
        return 0;
    }
    ladderSide(1, &s.side[1]);
    if ((bad = portVersusCheck(&s, 0)) != NULL) {
        *why = bad;
        return 0;
    }
    portVersusSave();
    sLadder = 1;
    sLadderLast = -1;
    __atomic_store_n(&sAsked, 1, __ATOMIC_SEQ_CST);
    return 1;
}

int portVersusStart(const char** why) {
    int i;
    const PortVersusSetup* s = portVersusSetup();
    if (replayRefuses(why)) {
        return 0;
    }
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
    sLadder = 0;
    sLadderLast = -1;
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

/* The sides' entrances, at the fight's start (the game's opening, SEQ_FIRST_ACT,
 * waits for them): each unit from behind its side's end of the stage, Mario's
 * side first, running in to its place and appealing to the audience there,
 * then Luigi's from the other end. Mario's appeal has his cry. In a
 * versus fight every player unit's entry (the game's run in) is this one. */
#define ENTRY_WAIT 30       /* frames before Mario's side comes in (the curtain up) */
#define ENTRY_LUIGI_WAIT 60 /* and Luigi's after it */

USER_FUNC(evt_versus_entry_wait);

static EVT_BEGIN(versus_entry_evt)
    CALL(evt_versus_entry_wait, LW(5), LW(6))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 250)
    CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    WAIT_FRAMES(LW(5))
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 40, -1, 0)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_APPEAL)
    IF_INT_EQUAL(LW(6), 1)
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_MARIO_APPEAL1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    END_IF
    WAIT_FRAMES(36)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
    RETURN
EVT_END()

/* out: the frames the unit waits first (by its side), whether it leads its side */
USER_FUNC(evt_versus_entry_wait) {
    BattleUnit* u = BattleGetUnitPtr(_battleWorkPointer, event->unitId);
    (void)isFirstCall;
    evtSetValue(event, event->args[0], ENTRY_WAIT + (u != NULL && u->alliance == 1 ? ENTRY_LUIGI_WAIT : 0));
    /* (Mario's cry: Luigi, the other Mario unit, has none of his own) */
    evtSetValue(event, event->args[1], u != NULL && u->currentKind == UNIT_MARIO && u->alliance == 0);
    return EVT_RETURN_DONE;
}

static void setEntrances(BattleWork* wp) {
    int i;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u != NULL && playerKind(u) && (u->alliance == 0 || u->alliance == 1) &&
            !(u->flags & UNIT_FLAG_NOT_ACTIVE_PARTNER)) {
            u->entryEventCode = versus_entry_evt;
        }
    }
}

/* BattleSetSeq / BattleIncSeq: the side follows the phase (Luigi's from the
 * phase after Mario's to the enemies'), and Mario's at the end */
static void seqChanged(BattleWork* wp, BattleSequence seq) {
    if (!fight()) {
        return;
    }
    if (seq == SEQ_FIRST_ACT && BattleGetSeq(wp, SEQ_FIRST_ACT) == 0x2000000) {
        setEntrances(wp); /* (the opening runs the entries next) */
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
    if (!twoPads() && !computer(1)) { /* (against the computer, Mario's player guards on the first controller, read as the second then) */
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

/* Luigi's side's HUD: the game's drawn again at the bottom of the screen with his values and
 * his face; the turn timer in the middle (the ladder's rung over it) */
static void versusHud(CameraId camId, void* param) {
    const int left = portVersusTimeLeft();
    /* the game's font takes random numbers per letter, and "Rung" is in each
     * player's menu language: the random state as it was (win_root.c) */
    const u32 seed = portRandState();
    portLuigiFaceForce(1);
    portStatusWinDispOther(camId, param);
    portLuigiFaceForce(-1);
    if (sLadder > 0) {
        /* the ladder's rung, over the turn timer, in the middle */
        char t[64];
        const f32 k = 0.7f;
        snprintf(t, sizeof(t), "%s %d", portTrGame("Rung"), sLadder);
        winFontInit();
        winFontSet((Vec){-(f32)FontGetMessageWidth(t) * k / 2.0f, 80.0f, 0.0f}, (Vec){k, k, k},
                   (GXColor){0xFF, 0xE0, 0x60, 0xFF}, "%s", t);
    }
    if (left >= 0) {
        char t[16];
        const f32 k = 0.9f;
        snprintf(t, sizeof(t), "%d", left);
        winFontInit();
        winFontSet((Vec){-(f32)FontGetMessageWidth(t) * k / 2.0f, 40.0f, 0.0f}, (Vec){k, k, k},
                   left <= 5 ? (GXColor){0xFF, 0x50, 0x40, 0xFF} : (GXColor){0xFF, 0xFF, 0xFF, 0xFF}, "%s", t);
    }
    portRandSetState(seed);
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
        portStatusWinSetOther(&v, -158.0f); /* (at the bottom: the command menus are over the middle; the audience count up a little, battle_audience.c) */
        dispEntry(CAMERA_2D, 500.0f, 0, versusHud, NULL);
    }
}

/* BattleStoreExp: no Star Points from a versus fight */
static int storeExpPre(BattleWork** wp, s32* exp) {
    (void)wp;
    (void)exp;
    return fight();
}

/* the turn timer: BattleCommandInit starts a command's count of frames (60
 * a second; frames, not the clock, so every game of an online session runs
 * out on the same one); when it runs out, BattleCommandInput ends with the
 * turn passed (a script that does nothing) and the menu put away. The
 * computer's commands have no timer. */
static EVT_BEGIN(versus_time_up_evt)
    RETURN
EVT_END()

static int sTimeUp;

static int commandInitPre(BattleWork** wp) {
    (void)wp;
    sCmdFrames = 0;
    portVersusCpuCommandBegins();
    return 0;
}

static int commandInputPre(s32* r, void** work) {
    BattleWork* wp = (BattleWork*)*work;
    BattleUnit* unit;
    if (!fight()) {
        return 0;
    }
    unit = BattleGetUnitPtr(wp, wp->activeUnitId);
    if (unit == NULL) {
        return 0;
    }
    if (computer(sSide)) {
        portVersusCpuCommand(wp, unit); /* (once it has chosen, the menu's own last step makes the attack) */
        return 0;
    }
    if (sFight.rules.turnTimer <= 0 || ++sCmdFrames < sFight.rules.turnTimer * 60) {
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

/* the seconds left for the command being chosen, or -1 (no timer, not choosing, the computer's) */
int portVersusTimeLeft(void) {
    int left;
    if (!fight() || sFight.rules.turnTimer <= 0 || computer(sSide) ||
        !(_battleWorkPointer->flags & BATTLE_FLAG_COMMAND_INPUT)) {
        return -1;
    }
    left = sFight.rules.turnTimer * 60 - sCmdFrames;
    return left <= 0 ? 0 : (left + 59) / 60;
}

/* BattleActionCommandCheckDefence: the computer's guards come up by chance */
static int defencePre(s32* r, BattleUnit** unit, BattleWeapon** weapon) {
    BattleUnit* u = *unit;
    if (!fight() || u == NULL || u->alliance != 1 || !computer(1) || !playerKind(u)) {
        return 0;
    }
    *r = portVersusCpuGuard(u, *weapon);
    _battleWorkPointer->actionCommands.defenseResult = *r;
    return 1;
}

/* BattleActionCommandStart: the computer's action command's roll */
static int acStartPre(BattleWork** wp) {
    (void)wp;
    if (fight() && computer(1)) {
        portVersusCpuActionCommand();
    }
    return 0;
}

int portVersusAutoCommand(const void* unit) {
    const BattleUnit* u = (const BattleUnit*)unit;
    if (!fight() || !computer(1) || u == NULL || u->alliance != 1 || !playerKind(u)) {
        return -1;
    }
    return portVersusCpuActionGood();
}

int portVersusRoute(void) {
    if (!fight() || gp == NULL || !gp->inBattle) {
        return 0;
    }
    if (sSide == 1 && (twoPads() || computer(1))) {
        /* Luigi's player is the game's first controller; Mario's the second (his guards).
         * The computer's side: the first reads nothing (its moves are its own) */
#define SWAP(f)                 \
    do {                        \
        __typeof__(gp->f[0]) t = gp->f[0]; \
        gp->f[0] = computer(1) ? 0 : gp->f[1]; \
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
    /* $TTYD_VERSUS_TEST: a fight with the set-up as it is, 30 s in (past a $TTYD_MAP warp; tests); "ladder": the ladder */
    if (!sTested && getenv("TTYD_VERSUS_TEST") != NULL && SDL_GetTicks() > 30000 && seq == SEQ_GAME && gp != NULL &&
        !gp->inBattle) {
        const char* why = NULL;
        const int ladder = strcmp(getenv("TTYD_VERSUS_TEST"), "ladder") == 0;
        sTested = 1;
        if (!(ladder ? portVersusLadderStart(&why) : portVersusStart(&why))) {
            printf("[versus] test not started: %s\n", why);
        }
    }
    if (__atomic_exchange_n(&sAsked, 0, __ATOMIC_SEQ_CST)) {
        sState = STATE_ASKED;
    }
    /* (once the Start menu has closed: a map change under it left the game held, the fight never starting) */
    if (sState == STATE_ASKED && seq == SEQ_GAME && gp != NULL && !gp->inBattle && marioStGetSystemLevel() == 0) {
        snprintf(sReturnMap, sizeof(sReturnMap), "%s", gp->currentMapName);
        sState = STATE_GOING;
        sStage = fightStage();
        printf("[versus] to %s (back to %s after)\n", kStages[sStage].map, sReturnMap);
        fflush(stdout);
        seqSetSeq(SEQ_MAP_CHANGE, kStages[sStage].map, NULL);
    }
    if ((sState == STATE_FIGHT || sState == STATE_GOING) && (seq == SEQ_GAME_OVER || seq == SEQ_TITLE)) {
        restorePouch();
        sState = STATE_NONE;
        if (sLadder > 0) {
            sLadderLast = sLadder - 1;
            sLadder = 0;
        }
    }
    if (sState == STATE_OVER && seq == SEQ_GAME && gp != NULL && !versusMap(gp->currentMapName)) {
        sState = STATE_NONE;
    }
}

void portVersusRegisterMaps(void) {
    int i;
    for (i = 0; i < PORT_VERSUS_STAGES; i++) {
        portModsAddHostMap(kStages[i].map, versus_map_init, kStages[i].files);
    }
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
    portModHostAddHook(0, kSlot_BattleActionCommandCheckDefence, (void*)defencePre);
    portModHostAddHook(0, kSlot_BattleActionCommandStart, (void*)acStartPre);
}
