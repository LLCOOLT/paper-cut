/* Battle puzzles (port/include/port/battlex.h): hand-made battles with a
 * fixed set-up (Mario's level, HP, FP, partner, badges and items) and a goal
 * (win within so many turns, maybe without items, damage, jumps or the
 * hammer), like chess puzzles, rising in difficulty. Each is one of the
 * game's own battles, on a map this file adds to the mods' new maps
 * (port/mods/mods.cpp) the way the boss rush does (modes_aurora.c): named
 * for the battle's area, with one of its maps' files, so the map change
 * loads the area's REL and battle table. Mario's pouch is put back and he
 * returns where he started when the puzzle ends, won or lost (a lost puzzle
 * isn't a Game Over). Not online: the settings page doesn't offer them then.
 *
 * The daily puzzle is picked from the UTC day's number; results go to
 * <user>/puzzles.txt and, later, a results board (gPortPuzzleResultHook). */
#include "port/battlex.h"
#include "port/date_seed.h"
#include "port/enemy_variant.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/mods.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "battle/battle.h"
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

#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The areas: the puzzle map, the map whose files it reads */
typedef struct PuzzleArea {
    const char* map;
    const char* files;
} PuzzleArea;

enum { A_TIK, A_HEI, A_GON, A_GOR, A_JIN, A_MRI, A_DOU, A_MUJ, A_EKI, A_RSH, A_BOM, A_MOO, A_AJI, A_GRA, A_PIK, A_LAS, A_COUNT };
static const PuzzleArea kAreas[A_COUNT] = {
    {"tik_q0", "tik_02"}, {"hei_q0", "hei_10"}, {"gon_q0", "gon_11"}, {"gor_q0", "gor_00"},
    {"jin_q0", "jin_04"}, {"mri_q0", "mri_20"}, {"dou_q0", "dou_01"}, {"muj_q0", "muj_12"},
    {"eki_q0", "eki_02"}, {"rsh_q0", "rsh_06_a"}, {"bom_q0", "bom_03"}, {"moo_q0", "moo_01"},
    {"aji_q0", "aji_00"}, {"gra_q0", "gra_01"}, {"pik_q0", "pik_03"}, {"las_q0", "las_26"},
};

/* Mario's tiers: level, HP, FP, boots, hammer, the partner's rank, Star Powers, SP */
typedef struct Tier {
    s16 level, hp, fp;
    s8 jump, hammer, rank;
    u16 starPowers;
    s16 sp;
} Tier;
static const Tier kTiers[] = {
    {1, 10, 5, 1, 1, 0, 0x00, 0},       /* 0: the prologue */
    {5, 20, 10, 1, 1, 0, 0x03, 200},    /* 1: chapter 1 */
    {10, 25, 15, 2, 1, 1, 0x07, 300},   /* 2: chapters 2-3 */
    {15, 30, 25, 2, 2, 1, 0x1F, 400},   /* 3: chapters 4-5 */
    {22, 40, 35, 2, 2, 2, 0x3F, 500},   /* 4: chapter 6-7 */
    {30, 60, 50, 3, 3, 2, 0xFF, 700},   /* 5: chapter 8 */
};

#define PZ_BADGES 6
#define PZ_ITEMS 6
typedef struct Puzzle {
    PortPuzzleInfo info;
    s8 area;
    s8 battle; /* the area battle table's entry */
    s8 tier;
    s8 partner; /* PartyId; 0 Mario alone */
    s16 badges[PZ_BADGES];
    s16 items[PZ_ITEMS];
} Puzzle;

#define NI PORT_PUZZLE_NO_ITEMS
#define ND PORT_PUZZLE_NO_DAMAGE
#define NJ PORT_PUZZLE_NO_JUMP
#define NH PORT_PUZZLE_NO_HAMMER
#define P(name, hint, foes, diff, turns, flags) {name, hint, foes, diff, turns, flags}

static const Puzzle kPuzzles[] = {
    /* 1: the basics */
    {P("First Steps", "Jump on it: a timed press lands the second hit.", "a Goomba", 1, 1, ND), A_TIK, 14, 0, 1},
    {P("Spike Sense", "Spikes on top: the hammer, then.", "a Spiky Goomba", 1, 1, NJ), A_TIK, 16, 0, 1},
    {P("Two at Once", "Mario takes one, Goombella the other. Mind the spikes.", "two Spiky Goombas", 1, 1, 0),
     A_TIK, 5, 0, 1},
    {P("Wings First", "A jump clips its wings; a timed press lands the second hit.", "a Paragoomba", 1, 1, NH), A_TIK,
     15, 0, 1},
    {P("Paper Thin", "Both of you, on the same one.", "a Spinia", 1, 1, 0), A_TIK, 6, 0, 1},
    {P("Shell Shock", "Turn the Koopa over first; then it's soft.", "a Koopa Troopa and a Goomba", 1, 2, 0), A_HEI, 6,
     1, 2, {ITEM_POWER_BOUNCE}},
    {P("Bristle Wall", "Piercing Blow goes through defence.", "two Bristles", 1, 2, NJ), A_HEI, 11, 1, 1,
     {ITEM_PIERCING_BLOW}},
    {P("Fuzzy Swarm", "Hit the whole row at once.", "four Fuzzies", 1, 2, 0), A_HEI, 22, 1, 1,
     {ITEM_QUAKE_HAMMER, ITEM_MULTIBOUNCE}},
    {P("Gold Rush", "Charge first; it can't run from a charged hammer.", "the Gold Fuzzy", 1, 3, 0), A_HEI, 19, 1, 2,
     {ITEM_POWER_SMASH, ITEM_CHARGE}, {ITEM_MUSHROOM}},
    {P("Bone Dry", "One item does it.", "four Dull Bones", 1, 1, 0), A_GON, 7, 1, 2, {0}, {ITEM_FIRE_FLOWER}},
    /* 2: chapters 1-2 */
    {P("Red Alert", "The red one leads: down it first, then sweep the rest.", "Red Bones and four Dull Bones", 2, 2,
       0), A_GON, 6, 1, 2, {ITEM_QUAKE_HAMMER, ITEM_POWER_JUMP}, {ITEM_FIRE_FLOWER}},
    {P("Flying Circus", "Bounce from one to the next.", "three Paratroopas", 2, 1, NH), A_GON, 18, 1, 1,
     {ITEM_MULTIBOUNCE, ITEM_POWER_JUMP}},
    {P("Dragon's Bark", "Charge, and save the Star Power for Sweet Treat.", "Hooktail", 2, 5, 0), A_GON, 20, 1, 2,
     {ITEM_POWER_SMASH, ITEM_POWER_JUMP, ITEM_CHARGE, ITEM_DEFEND_PLUS},
     {ITEM_FIRE_FLOWER, ITEM_THUNDER_RAGE, ITEM_SUPER_SHROOM, ITEM_SUPER_SHROOM}},
    {P("Crumpled", "He hits hard but can't take much: all in.", "Lord Crump", 2, 2, 0), A_GOR, 2, 1, 1,
     {ITEM_POWER_JUMP, ITEM_POWER_SMASH}, {ITEM_POWER_PUNCH}},
    {P("Kingdom of Ink", "Its arms first, or the head is out of reach.", "Blooper", 2, 3, 0), A_TIK, 3, 2, 2,
     {ITEM_POWER_SMASH, ITEM_MULTIBOUNCE, ITEM_POWER_PLUS}, {ITEM_THUNDER_RAGE}},
    {P("Spider Den", "They come down to bite: a bounce across all four.", "four Piders", 2, 2, 0), A_MRI, 8, 2, 5,
     {ITEM_MULTIBOUNCE, ITEM_POWER_PLUS}},
    {P("Pale Garden", "Biting plants don't like the cold.", "four Pale Piranhas", 2, 2, NJ), A_MRI, 10, 2, 5,
     {ITEM_ICE_SMASH, ITEM_QUAKE_HAMMER}, {ITEM_ICE_STORM}},
    {P("Shield Wall", "The little ones shield the big one: clear them first.", "three Yuxes", 2, 3, 0), A_MRI, 14,
     2, 1, {ITEM_MULTIBOUNCE, ITEM_POWER_PLUS}, {ITEM_THUNDER_RAGE}},
    {P("X-Factor", "Two X-Nauts: no time to waste.", "two X-Nauts", 2, 1, ND), A_MRI, 6, 2, 4,
     {ITEM_POWER_BOUNCE, ITEM_DAMAGE_DODGE}},
    {P("Grapple Tackle", "Its Mini-Yuxes heal it; strike when they're gone.", "Magnus von Grapple", 2, 5, 0), A_MRI,
     20, 2, 5, {ITEM_POWER_SMASH, ITEM_MULTIBOUNCE, ITEM_POWER_PLUS, ITEM_CHARGE},
     {ITEM_THUNDER_RAGE, ITEM_SUPER_SHROOM}},
    /* 3: chapters 3-4 */
    {P("Hard Hats", "They're hard on top; turn them over.", "four Buzzy Beetles", 3, 3, 0), A_JIN, 22, 2, 2,
     {ITEM_QUAKE_HAMMER, ITEM_POWER_BOUNCE}},
    {P("Spiked Hats", "No jumping on these.", "two Spike Tops", 3, 2, NJ), A_JIN, 23, 2, 2,
     {ITEM_PIERCING_BLOW, ITEM_QUAKE_HAMMER}},
    {P("Air Raid", "Clip both their wings, then they're grounded.", "a Paragoomba and a Paratroopa", 3, 1, NH), A_GON,
     13, 2, 1, {ITEM_MULTIBOUNCE, ITEM_POWER_PLUS}},
    {P("Atomic Boo", "One big Boo is still a Boo: patience, and Star Power.", "the Atomic Boo", 3, 6, 0), A_JIN, 7, 3,
     5, {ITEM_POWER_SMASH, ITEM_POWER_JUMP, ITEM_POWER_PLUS, ITEM_CHARGE}, {ITEM_THUNDER_RAGE, ITEM_ULTRA_SHROOM}},
    {P("Mirror Match", "He copies you: hit before he's ready.", "Doopliss", 3, 6, 0), A_JIN, 5, 3, 6,
     {ITEM_POWER_SMASH, ITEM_POWER_BOUNCE, ITEM_POWER_PLUS}, {ITEM_FIRE_FLOWER, ITEM_ULTRA_SHROOM}},
    {P("Hot Foot", "Fight fire with ice.", "four Lava Bubbles", 3, 1, 0), A_DOU, 5, 3, 4, {ITEM_ICE_SMASH},
     {ITEM_ICE_STORM}},
    {P("Short Fuse", "Too slow and they go off; win before they do.", "four Bulky Bob-ombs", 3, 2, ND), A_DOU, 16, 3,
     4, {ITEM_QUAKE_HAMMER, ITEM_HAMMER_THROW}, {ITEM_THUNDER_RAGE}},
    {P("Blaster Duo", "Their shells are tough; their cannons tougher.", "two Bill Blasters", 3, 3, 0), A_DOU, 19, 3, 3,
     {ITEM_PIERCING_BLOW, ITEM_POWER_PLUS}},
    {P("Ember Glow", "Cold steel puts them out.", "three Embers", 3, 2, 0), A_DOU, 3, 3, 2,
     {ITEM_ICE_SMASH, ITEM_MULTIBOUNCE}},
    {P("Flying Hard Hats", "Knock them out of the air first.", "three Parabuzzies", 3, 2, 0), A_DOU, 12, 3, 2,
     {ITEM_MULTIBOUNCE, ITEM_POWER_PLUS}},
    /* 4: chapters 5-6 */
    {P("Bone Collector", "His weapons first; his bones last.", "Cortez", 4, 7, 0), A_MUJ, 4, 3, 4,
     {ITEM_POWER_SMASH, ITEM_POWER_BOUNCE, ITEM_POWER_PLUS, ITEM_CHARGE, ITEM_DEFEND_PLUS},
     {ITEM_THUNDER_RAGE, ITEM_ULTRA_SHROOM, ITEM_MAPLE_SYRUP}},
    {P("Crump's Crew", "Scatter the X-Nauts, then the boss.", "Lord Crump and his X-Nauts", 4, 4, 0), A_MUJ, 5, 3, 3,
     {ITEM_QUAKE_HAMMER, ITEM_MULTIBOUNCE, ITEM_POWER_PLUS}, {ITEM_EARTH_QUAKE}},
    {P("Static Cling", "Touch them and you get a shock: Zap Tap's the other way round.", "three Dark Puffs", 4, 2, 0),
     A_EKI, 6, 4, 4, {ITEM_HAMMER_THROW, ITEM_QUAKE_HAMMER, ITEM_POWER_PLUS}},
    {P("Mummy's Curse", "Poisonous on top: keep your feet off them.", "three Poison Pokeys", 4, 2, NJ), A_EKI, 9, 4, 3,
     {ITEM_FIRE_DRIVE, ITEM_POWER_PLUS}},
    {P("Spiky Fliers", "Spikes and wings: the partner gets them down.", "three Spiky Parabuzzies", 4, 3, 0), A_EKI,
     13, 4, 2, {ITEM_QUAKE_HAMMER, ITEM_HAMMER_THROW}},
    {P("Train Cloud", "The hands guard it; strike through them.", "Smorg", 4, 6, 0), A_RSH, 1, 4, 3,
     {ITEM_POWER_SMASH, ITEM_FIRE_DRIVE, ITEM_POWER_PLUS, ITEM_CHARGE},
     {ITEM_THUNDER_RAGE, ITEM_ULTRA_SHROOM, ITEM_JAMMIN_JELLY}},
    {P("Frozen Garden", "Biting plants like heat even less than cold.", "four Frost Piranhas", 4, 2, 0), A_BOM, 6, 4, 3,
     {ITEM_FIRE_DRIVE, ITEM_QUAKE_HAMMER}, {ITEM_FIRE_FLOWER}},
    {P("Big Guns", "The cannons fire Bombshell Bills; take the cannons out.", "two Bombshell Bill Blasters", 4, 3, 0),
     A_LAS, 25, 4, 3, {ITEM_PIERCING_BLOW, ITEM_POWER_PLUS, ITEM_POWER_PLUS}, {ITEM_THUNDER_RAGE}},
    /* 5: chapters 7-8 */
    {P("Moon Rocks", "Defence 5: only piercing hits get through.", "three Moon Clefts", 5, 3, 0), A_MOO, 14, 4, 3,
     {ITEM_PIERCING_BLOW, ITEM_HEAD_RATTLE}, {ITEM_THUNDER_RAGE}},
    {P("Z-Barrier", "Break the shield-makers first.", "two Z-Yuxes", 5, 3, 0), A_MOO, 6, 4, 5,
     {ITEM_MULTIBOUNCE, ITEM_POWER_PLUS, ITEM_POWER_PLUS}},
    {P("Elite Squad", "Four of the best: a Star Power evens it.", "four Elite X-Nauts", 5, 2, 0), A_AJI, 12, 4, 3,
     {ITEM_QUAKE_HAMMER, ITEM_MULTIBOUNCE, ITEM_POWER_PLUS}, {ITEM_EARTH_QUAKE}},
    {P("Mad Scientists", "They buff each other; don't give them the time.", "four X-Naut PhDs", 5, 3, 0), A_AJI, 20, 4,
     6, {ITEM_MULTIBOUNCE, ITEM_POWER_PLUS, ITEM_POWER_PLUS}, {ITEM_THUNDER_RAGE}},
    {P("Grapple 2.0", "Its fists come back; its head doesn't.", "Magnus von Grapple 2.0", 5, 6, 0), A_AJI, 5, 4, 3,
     {ITEM_POWER_SMASH, ITEM_POWER_BOUNCE, ITEM_POWER_PLUS, ITEM_CHARGE, ITEM_DEFEND_PLUS},
     {ITEM_THUNDER_RAGE, ITEM_ULTRA_SHROOM, ITEM_JAMMIN_JELLY}},
    {P("Hyper Hype", "They charge up; hit them before they let go.", "three Hyper Goombas", 5, 1, ND), A_GRA, 4, 4, 4,
     {ITEM_POWER_BOUNCE, ITEM_POWER_PLUS, ITEM_POWER_PLUS}},
    {P("Hyper Spikes", "Charged and spiked.", "three Hyper Spiky Goombas", 5, 2, NJ), A_GRA, 6, 4, 2,
     {ITEM_QUAKE_HAMMER, ITEM_POWER_PLUS, ITEM_POWER_PLUS}},
    {P("Haunted Hall", "Dark Boos vanish: hit them while you see them.", "four Dark Boos", 5, 2, 0), A_PIK, 9, 5, 1,
     {ITEM_MULTIBOUNCE, ITEM_POWER_PLUS}, {ITEM_EARTH_QUAKE}},
    {P("Phantom Fire", "Fire can't hurt them: ice can.", "three Phantom Embers", 5, 2, 0), A_LAS, 55, 5, 4,
     {ITEM_ICE_SMASH, ITEM_MULTIBOUNCE}, {ITEM_ICE_STORM}},
    {P("Chain Gang", "Defence 5 and big teeth.", "three Chain Chomps", 5, 3, 0), A_LAS, 43, 5, 3,
     {ITEM_PIERCING_BLOW, ITEM_POWER_PLUS, ITEM_POWER_PLUS}, {ITEM_THUNDER_RAGE}},
    {P("Night Flight", "They drain what they bite: don't let them.", "four Swoopulas", 5, 2, ND), A_LAS, 20, 5, 5,
     {ITEM_MULTIBOUNCE, ITEM_POWER_PLUS, ITEM_DAMAGE_DODGE}, {ITEM_EARTH_QUAKE}},
    {P("Gloom and Doom", "Its breath is the danger: guard it, then strike.", "Gloomtail", 5, 8, 0), A_LAS, 14, 5, 4,
     {ITEM_POWER_SMASH, ITEM_POWER_BOUNCE, ITEM_POWER_PLUS, ITEM_POWER_PLUS, ITEM_DEFEND_PLUS, ITEM_FEELING_FINE},
     {ITEM_ULTRA_SHROOM, ITEM_ULTRA_SHROOM, ITEM_JAMMIN_JELLY, ITEM_THUNDER_RAGE}},
    {P("Queen's Gambit", "Hold on through her first form; the real fight starts after.", "the Shadow Queen", 5, 12,
       0), A_LAS, 16, 5, 2,
     {ITEM_POWER_SMASH, ITEM_POWER_BOUNCE, ITEM_POWER_PLUS, ITEM_POWER_PLUS, ITEM_DEFEND_PLUS, ITEM_FEELING_FINE},
     {ITEM_ULTRA_SHROOM, ITEM_ULTRA_SHROOM, ITEM_JAMMIN_JELLY, ITEM_JAMMIN_JELLY, ITEM_LIFE_SHROOM}},
};
#define PUZZLE_COUNT ((int)(sizeof(kPuzzles) / sizeof(kPuzzles[0])))

void (*gPortPuzzleResultHook)(int puzzle, int daily, unsigned day, int solved, int turns);

static int sActive = -1, sDaily, sStarted, sTurn;
static char sReturnMap[16];
static PouchData sSavedPouch;
static int sPouchSaved;
static unsigned sResultCount;
static int sResultPuzzle, sResultSolved, sResultTurns;
static int sBest[64]; /* fewest turns solved in (0: never) */
static int sBestRead;
static int sStartAsk = -1, sStartDaily, sStopAsk; /* __atomic: the page's thread */

int portPuzzleCount(void) {
    return PUZZLE_COUNT;
}

const PortPuzzleInfo* portPuzzleInfo(int i) {
    return i >= 0 && i < PUZZLE_COUNT ? &kPuzzles[i].info : NULL;
}

/* the lockstep clock's day (date_seed.h): the page's, the battle's and a replay's agree */
unsigned portPuzzleDay(void) {
    return portDateDay();
}

int portPuzzleDaily(void) {
    /* from the second half (difficulty 3 on), by a mix of the day's number */
    unsigned x = portPuzzleDay() * 2654435761u;
    const int first = PUZZLE_COUNT / 2;
    x ^= x >> 15;
    return first + (int)(x % (unsigned)(PUZZLE_COUNT - first));
}

/* --- results: <user>/puzzles.txt, "<number> <fewest turns>" a line --- */

static void resultsPath(char* out, size_t size) {
    snprintf(out, size, "%s/puzzles.txt", portSettingsUserPath());
}

static void readBest(void) {
    char path[1024];
    FILE* f;
    if (sBestRead) {
        return;
    }
    sBestRead = 1;
    resultsPath(path, sizeof(path));
    if ((f = fopen(path, "r")) != NULL) {
        int n, t;
        char line[64];
        while (fgets(line, sizeof(line), f) != NULL) {
            if (sscanf(line, "%d %d", &n, &t) == 2 && n >= 1 && n <= PUZZLE_COUNT && t > 0) {
                sBest[n - 1] = t;
            }
        }
        fclose(f);
    }
}

static void writeBest(void) {
    char path[1024];
    FILE* f;
    int i;
    resultsPath(path, sizeof(path));
    if ((f = fopen(path, "w")) != NULL) {
        fprintf(f, "# Paper Cut battle puzzles solved: number, fewest turns\n");
        for (i = 0; i < PUZZLE_COUNT; i++) {
            if (sBest[i] > 0) {
                fprintf(f, "%d %d\n", i + 1, sBest[i]);
            }
        }
        fclose(f);
    }
}

int portPuzzleSolved(int i, int* bestTurns) {
    readBest();
    if (i < 0 || i >= PUZZLE_COUNT) {
        return 0;
    }
    if (bestTurns != NULL) {
        *bestTurns = sBest[i];
    }
    return sBest[i] > 0;
}

int portPuzzleActive(int* daily, int* turn, int* maxTurns) {
    if (daily != NULL) {
        *daily = sDaily;
    }
    if (turn != NULL) {
        *turn = sTurn;
    }
    if (maxTurns != NULL) {
        *maxTurns = sActive >= 0 ? kPuzzles[sActive].info.maxTurns : 0;
    }
    return sActive;
}

unsigned portPuzzleLastResult(int* puzzle, int* solved, int* turns) {
    *puzzle = sResultPuzzle;
    *solved = sResultSolved;
    *turns = sResultTurns;
    return sResultCount;
}

/* --- the set-up --- */

static void restorePouch(void) {
    if (sPouchSaved) {
        *pouchGetPtr() = sSavedPouch;
        sPouchSaved = 0;
    }
}

static void giveSetup(const Puzzle* z) {
    PouchData* p = pouchGetPtr();
    const Tier* t = &kTiers[z->tier];
    int i;
    sSavedPouch = *p;
    sPouchSaved = 1;
    for (i = 1; i <= 7; i++) {
        p->partyData[i].attackLevel = p->partyData[i].HPLevel = p->partyData[i].techLevel = t->rank;
    }
    if (z->partner > 0) {
        partyJoin((PartyId)z->partner);
    }
    p->level = t->level;
    p->baseMaxHP = p->maximumHP = p->currentHP = t->hp;
    p->baseMaxFP = p->maximumFP = p->currentFP = t->fp;
    p->maximumBP = 99;
    p->jumpLevel = t->jump;
    p->hammerLevel = t->hammer;
    p->starPowersObtained = t->starPowers;
    p->maximumSP = p->currentSP = t->sp;
    memset(p->heldItems, 0, sizeof(p->heldItems));
    memset(p->badges, 0, sizeof(p->badges));
    memset(p->equippedBadges, 0, sizeof(p->equippedBadges));
    for (i = 0; i < PZ_BADGES && z->badges[i] != 0; i++) {
        p->badges[i] = p->equippedBadges[i] = z->badges[i];
    }
    for (i = 0; i < PZ_ITEMS && z->items[i] != 0; i++) {
        p->heldItems[i] = z->items[i];
    }
    pouchReviseMarioParam();
    pouchRevisePartyParam();
    for (i = 1; i <= 7; i++) {
        p->partyData[i].currentHP = p->partyData[i].maximumHP;
    }
}

/* --- the fight's script --- */

USER_FUNC(evt_puzzle_battle);
USER_FUNC(evt_puzzle_battle_end);

static EVT_BEGIN(puzzle_fight_evt)
    WAIT_FRAMES(20)
    CALL(evt_puzzle_battle, LW(0))
    IF_INT_EQUAL(LW(0), -1)
        RETURN
    END_IF
    CALL(evt_npc_entry, STRING("puzzle_foe"), STRING("c_kuribo"))
    CALL(evt_npc_flag_onoff, 1, STRING("puzzle_foe"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW)
    CALL(evt_npc_set_position, STRING("puzzle_foe"), 0, -1000, 0)
    CALL(evt_npc_set_battle_info, STRING("puzzle_foe"), LW(0))
    CALL(evt_puzzle_battle, LW(0)) /* again, now the NPC has its battle info: its flags */
    CALL(evt_npc_battle_start, STRING("puzzle_foe"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(0))
    CALL(evt_puzzle_battle_end, LW(0))
    RETURN
EVT_END()

static EVT_BEGIN(puzzle_map_init)
    SPAWN_EVENT(puzzle_fight_evt)
    RETURN
EVT_END()

/* out: the battle entry (-1: no puzzle for this map); the second call sets
 * the stand-in's battle flags */
USER_FUNC(evt_puzzle_battle) {
    const Puzzle* z = sActive >= 0 ? &kPuzzles[sActive] : NULL;
    NpcEntry* npc;
    (void)isFirstCall;
    if (z == NULL || strcmp(gp->currentMapName, kAreas[z->area].map) != 0) {
        evtSetValue(event, event->args[0], -1);
        return EVT_RETURN_DONE;
    }
    if ((npc = npcNameToPtr_NoAssert("puzzle_foe")) != NULL) {
        s32 f = NPC_BATTLE_LOSS_OK | NPC_BATTLE_NO_COINS | NPC_BATTLE_NO_ITEM | NPC_BATTLE_NO_HEARTS |
                NPC_BATTLE_NO_FLOWERS | NPC_BATTLE_NO_BUMP_ATTACK;
        if (z->info.flags & PORT_PUZZLE_NO_JUMP) {
            f |= NPC_BATTLE_NO_JUMP;
        }
        if (z->info.flags & PORT_PUZZLE_NO_HAMMER) {
            f |= NPC_BATTLE_NO_HAMMER;
        }
        npc->battleInfo.battleFlags |= f;
    }
    evtSetValue(event, event->args[0], z->battle);
    return EVT_RETURN_DONE;
}

static void finish(int solved, int turns) {
    const int was = sActive;
    sResultPuzzle = was;
    sResultSolved = solved;
    sResultTurns = turns;
    sResultCount++;
    if (solved) {
        readBest();
        if (sBest[was] == 0 || turns < sBest[was]) {
            sBest[was] = turns;
            writeBest();
        }
    }
    if (gPortPuzzleResultHook != NULL) {
        gPortPuzzleResultHook(was, sDaily, portPuzzleDay(), solved, turns);
    }
    printf("[puzzles] %d %s in %d turns\n", was + 1, solved ? "solved" : "failed", turns);
    fflush(stdout);
    restorePouch();
    sActive = -1;
    sStarted = 0;
    seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "gor_01", NULL);
}

/* in: the battle's result; the judging was done at the battle's end (battleOver) */
static int sJudged, sJudgedSolved;

USER_FUNC(evt_puzzle_battle_end) {
    (void)isFirstCall;
    (void)event;
    if (sActive >= 0) {
        finish(sJudged && sJudgedSolved, sTurn); /* not judged: fled or interrupted */
    }
    sJudged = 0;
    return EVT_RETURN_DONE;
}

/* battle_seq_end.c, the result decided: judged from the battle's records */
static void battleOver(BattleWork* wp, int result) {
    const Puzzle* z;
    int ok;
    if (sActive < 0 || !sStarted) {
        return;
    }
    z = &kPuzzles[sActive];
    sTurn = wp->actRecord.turnCount;
    ok = result == 1 && sTurn <= z->info.maxTurns;
    if (z->info.flags & PORT_PUZZLE_NO_ITEMS) {
        const BattleActRecord* r = &wp->actRecord;
        ok = ok && r->marioAttackItems + r->marioSupportItems + r->partnerAttackItems + r->partnerSupportItems == 0;
    }
    if (z->info.flags & PORT_PUZZLE_NO_DAMAGE) {
        ok = ok && wp->actRecord.marioDamageTaken + wp->actRecord.partnerDamageTaken == 0;
    }
    sJudged = 1;
    sJudgedSolved = ok;
}

/* Btl_UnitSetup, before: the puzzle's partner (or none) in the battle */
static int unitSetupPre(BOOL* result, BattleWork** wp) {
    (void)result;
    if (sActive >= 0 && (*wp)->fbatInfo != NULL) {
        (*wp)->fbatInfo->party = (s8)kPuzzles[sActive].partner;
        sStarted = 1;
        sJudged = 0;
    }
    return 0;
}

static void startNow(int i, int daily) {
    portPuzzleStop();
    snprintf(sReturnMap, sizeof(sReturnMap), "%s", gp->currentMapName);
    sActive = i;
    sDaily = daily;
    sTurn = 0;
    sStarted = 0;
    sJudged = 0;
    giveSetup(&kPuzzles[i]);
    seqSetSeq(SEQ_MAP_CHANGE, kAreas[kPuzzles[i].area].map, NULL);
    printf("[puzzles] %d (%s) started\n", i + 1, kPuzzles[i].info.name);
    fflush(stdout);
}

void portPuzzleStop(void) {
    if (sActive >= 0) {
        restorePouch();
    }
    sActive = -1;
    sStarted = 0;
}

static void frameHook(void) {
    const int ask = __atomic_exchange_n(&sStartAsk, -1, __ATOMIC_SEQ_CST);
    if (__atomic_exchange_n(&sStopAsk, 0, __ATOMIC_SEQ_CST)) {
        portPuzzleStop();
    }
    if (ask >= 0 && seqGetSeq() == SEQ_GAME && !gp->inBattle) {
        startNow(ask, __atomic_load_n(&sStartDaily, __ATOMIC_SEQ_CST));
    }
    if (sActive >= 0) {
        if (gp->inBattle) {
            BattleWork* wp = _battleWorkPointer;
            if (wp != NULL) {
                sTurn = wp->actRecord.turnCount;
            }
        } else if (seqGetSeq() == SEQ_GAME_OVER || seqGetSeq() == SEQ_TITLE) {
            restorePouch();
            sActive = -1;
        }
    }
}

int portPuzzleStart(int i, int daily, const char** why) {
    if (i < 0 || i >= PUZZLE_COUNT) {
        *why = "No such puzzle.";
        return 0;
    }
    if (portOnlineActive() || portReplayActive()) {
        /* a start from the page is no input: one game alone would go (and a playback never) */
        *why = "Not during a replay or online co-op.";
        return 0;
    }
    if (seqGetSeq() != SEQ_GAME || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    __atomic_store_n(&sStartDaily, daily, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sStartAsk, i, __ATOMIC_SEQ_CST);
    return 1;
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

void portPuzzlesRegister(void) {
    int i;
    for (i = 0; i < A_COUNT; i++) {
        portModsAddHostMap(kAreas[i].map, puzzle_map_init, kAreas[i].files);
    }
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)unitSetupPre);
    gPortBattlexOverHook = battleOver;
}
