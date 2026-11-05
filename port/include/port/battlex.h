#ifndef PORT_BATTLEX_H
#define PORT_BATTLEX_H

/* Battle extras (docs/BATTLE_EXTRAS.md):
 *
 * - Battle grades ("battle_grades"): each battle won graded D to S from the
 *   Stylish moves, Superguards, damage taken and turns; an S pays a few more
 *   coins, an A one more. port/src/common/battlex_pc.c.
 * - The Paper Cut badge set ("pc_badges") and the badge maker: badges made
 *   of the game's own badges' effects, sold by a merchant in Rogueport's
 *   plaza; Zess T.'s new dishes ("pc_recipes"). port/src/common/battlex_pc.c
 *   (the items), port/src/aurora/battlex_aurora.c (their texts, the merchant).
 * - Battle puzzles: fixed battles to win under a condition, a daily one by
 *   the date. port/src/aurora/puzzles_aurora.c.
 * - Enemy variants: port/include/port/enemy_variant.h.
 *
 * Each is off until turned on (or started), so the original game is
 * untouched; the ones that change play are settings the online host sends.
 *
 * The items: on PC the item table (src/data/item_data.c itemDataTable) is
 * longer than the game's 0x154 entries, and the port's own items take ids
 * from PORT_ITEM_MIN, past the game's last (ITEM_TYPE_MAX 0x153), so no id
 * the game or a save uses changes meaning. The game's two unused badge ids
 * (0x14F/0x150, the unused Defend badges) are left alone: two weren't enough,
 * and Dolphin's game reads them as real badges. A save holding the port's
 * items still loads in the port with the items in place; the GameCube game
 * would read them past its table, so a file meant for Dolphin should be
 * kept free of them (sold or thrown away first). */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_ITEM_MIN 0x160
#define PORT_BADGE_SET_MIN 0x160 /* the Paper Cut badge set */
#define PORT_BADGE_SET_COUNT 12
#define PORT_BADGE_CUSTOM_MIN 0x170 /* the badge maker's badges */
#define PORT_BADGE_CUSTOM_COUNT 8
#define PORT_DISH_MIN 0x180 /* Zess T.'s new dishes */
#define PORT_DISH_COUNT 16
#define PORT_ITEM_END 0x1A0
#define PORT_ITEM_TABLE_SIZE PORT_ITEM_END /* itemDataTable's length on PC */

#define PORT_BADGE_GRANTS 4 /* a made badge's effects: up to four of the game's badges */

/* --- the items --- */

/* Fills the port's item table entries (once; safe to call again). */
void portItemsInit(void);
/* id is one of the port's badges / held items (dishes). */
int portItemIsBadge(int id);
int portItemIsHeld(int id);
/* A port badge's effects: the game's badges it counts as (n, up to
 * PORT_BADGE_GRANTS; 0 for one with none, as a badge maker slot left empty). */
int portBadgeGrants(int id, const short** grants);
/* Badges worn now: how many of the game's badge baseId the port's badges
 * add (mario_pouch.c pouchEquipCheckBadge and pouchReviseMarioParam). */
int portBadgeEquippedGrants(int baseId);
/* gor_01.c cook_chk2, a pair with no recipe of the game's: a new dish, or 0. */
int portRecipeCook(int a, int b);
/* A dish's or badge's English name and description (the merchant's list, the page). */
const char* portItemName(int id);
const char* portItemDescription(int id);

/* The badge maker's slots (settings "badge_maker_<k>_fx", "badge_maker_<k>_icon"):
 * effects as the game's badge ids (0 none), the icon's badge id. */
int portBadgeMakerGet(int slot, short grants[PORT_BADGE_GRANTS], int* iconBadge);
void portBadgeMakerSet(int slot, const short grants[PORT_BADGE_GRANTS], int iconBadge);
/* Reads the badge maker's slots from the settings again (each frame: the
 * online host's may have arrived); a count that changes when they did. */
unsigned portBattlexRefresh(void);
/* A made badge's name (offline; the page reads <user>/badges.txt); "" none. */
void portBadgeMakerSetName(int slot, const char* name);
const char* portBadgeMakerName(int slot);
/* The fair BP cost of a badge of these effects: the sum of theirs, one less
 * for each effect past the first, at least 1. */
int portBadgeMakerCost(const short grants[PORT_BADGE_GRANTS]);
/* The merchant's price: 20 coins and 15 a BP. */
int portBadgeMakerPrice(int bp);
/* The merchant's stock now (badge ids, at most max; the count): the set when "pc_badges"
 * is on, the badge maker's filled slots. */
int portMerchantStock(int* out, int max);
/* evt_win_other_select's window type for the merchant's list (src/manager/winmgr.c) */
#define PORT_SELECT_MERCHANT 0x7F01

/* --- battle grades --- */

enum { PORT_GRADE_D, PORT_GRADE_C, PORT_GRADE_B, PORT_GRADE_A, PORT_GRADE_S, PORT_GRADE_COUNT };
typedef struct PortBattleGrade {
    int grade; /* PORT_GRADE_* */
    int score;
    int stylish, superguards, damageTaken, turns, par;
    int bonusCoins;
    unsigned serial; /* a different number each battle graded (0: none yet) */
} PortBattleGrade;
/* The last battle's grade; its serial (0: none since the start). */
unsigned portBattleGradeLast(PortBattleGrade* out);
/* Battles graded so far with this grade (kept in <user>/battle_grades.txt;
 * for the achievements). */
int portBattleGradeCount(int grade);
const char* portBattleGradeLetter(int grade);

/* The game's hooks (TARGET_PC blocks): */
/* battle.c Btl_UnitSetup: a battle begins with numEnemies. */
void portBattlexBattleStart(int numEnemies);
/* battle_acrobat.c: a Stylish move landed. */
void portBattlexStylish(void);
/* battle_damage.c: a Superguard. */
void portBattlexSuperguard(void);
/* battle_damage.c, for the recap (port/include/port/stats.h): a guard; a
 * hit's damage (BattleDamageDirect: the alliances, -1 no attacker); an enemy
 * of unit kind defeated. */
void portBattlexGuard(void);
void portBattlexHit(int attackerAlliance, int targetAlliance, int damage, int heal);
void portBattlexDefeated(int kind);
/* battle_seq_end.c, the battle won: the coins to add (grades, variants). */
struct BattleWork;
int portBattlexBattleWon(struct BattleWork* wp);
/* battle_seq_end.c: the result decided (1 won, 3 lost, 4 fled, 5 interrupted). */
void portBattlexBattleOver(struct BattleWork* wp, int result);
/* The grades' counts to their file (the game's frame, outside the battle). */
void portBattleGradesSave(void);
/* The puzzles' share of the battle's end (set by puzzles_aurora.c; NULL none). */
extern void (*gPortBattlexOverHook)(struct BattleWork* wp, int result);

/* --- battle puzzles (port/src/aurora/puzzles_aurora.c) --- */

enum {
    PORT_PUZZLE_NO_ITEMS = 1,   /* no items used */
    PORT_PUZZLE_NO_DAMAGE = 2,  /* neither Mario nor the partner hurt */
    PORT_PUZZLE_NO_JUMP = 4,    /* Jump greyed out */
    PORT_PUZZLE_NO_HAMMER = 8,  /* Hammer greyed out */
};
typedef struct PortPuzzleInfo {
    const char* name;  /* English */
    const char* hint;  /* English */
    const char* foes;  /* English: who's fought */
    int difficulty;    /* 1-5 */
    int maxTurns;      /* win in this many turns or fewer */
    unsigned flags;    /* PORT_PUZZLE_*: the goal's other conditions */
} PortPuzzleInfo;
int portPuzzleCount(void);
const PortPuzzleInfo* portPuzzleInfo(int i);
/* Starts puzzle i (from the field, a file loaded); 0 and why when it can't. */
int portPuzzleStart(int i, int daily, const char** why);
void portPuzzleStop(void);
/* The same from the Start menu's form: on the game's next frame (online, every game's alike). */
void portPuzzleGiveUp(void);
/* The puzzle being played (-1 none), whether it's the daily one, the
 * battle's turn and the limit. */
int portPuzzleActive(int* daily, int* turn, int* maxTurns);
/* The last one's end: puzzle, 1 solved 0 not, turns; a count that changes. */
unsigned portPuzzleLastResult(int* puzzle, int* solved, int* turns);
/* Solved before (kept in <user>/puzzles.txt), and the fewest turns. */
int portPuzzleSolved(int i, int* bestTurns);
/* Today's puzzle (the UTC day's number, picked from the hard half). */
int portPuzzleDaily(void);
unsigned portPuzzleDay(void);
/* A results board later (through the relay): called as each puzzle ends;
 * NULL: none. */
extern void (*gPortPuzzleResultHook)(int puzzle, int daily, unsigned day, int solved, int turns);

/* The frame hooks and the maps (Aurora; port/src/aurora/battlex_aurora.c). */
void portBattlexInit(void);
/* A variant's name and Tattle in place of its kind's (NULL, NULL: the game's again). */
void portBattlexVariantTexts(int kind, const char* name, const char* tattle);
/* An enemy kind's name in UTF-8 (the game's text; "" when it has none or no game runs). */
const char* portBattlexEnemyName(int kind, char* out, int size);
/* The Enemy maker's variants, armed for every battle while "enemy_variants" is
 * on (not when a mode armed its own); safe from the page's thread. */
struct PortEnemyVariant;
void portBattlexSetUserVariants(const struct PortEnemyVariant* list, int count);
/* Online: the host's list for the session (port/include/port/online_session.h),
 * in every game of it in place of each player's own; on 0: each player's own
 * again (none online). */
void portBattlexSetSessionVariants(const struct PortEnemyVariant* list, int count, int on);

#ifdef __cplusplus
}
#endif

#endif
