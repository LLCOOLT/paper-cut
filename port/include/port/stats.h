#ifndef PORT_STATS_H
#define PORT_STATS_H

/* Lifetime stats and the battle recap (docs/BATTLE_EXTRAS.md).
 *
 * - Lifetime stats (port/src/common/stats_pc.c): what the player has done,
 *   counted from the game's own state as it plays (the field's jumps, hammer
 *   swings, steps and coins each frame; each battle's numbers as it ends),
 *   kept per save file in the user folder as the achievements and stickers
 *   are (Stats/<game id>/file<n>.txt, "name=<player>" first: another game
 *   begun in the slot starts its own), and added up for every file on this
 *   device (Stats/totals.txt). Nothing goes in the card file, so the save
 *   stays the GameCube's and Dolphin's. Counted only while a file is played
 *   (not in replays, battle puzzles, or by an online guest for its file; a
 *   guest's device totals do count the game played together). Not part of
 *   save states: one loaded doesn't take counts back.
 * - The Journal's Stats (port/src/aurora/stats_journal_aurora.c): a book of
 *   them after the game's own entries and Photos, this file's or every
 *   file's (A switches), the pages turned with L and R.
 * - The battle recap (port/src/aurora/recap_aurora.c): each battle won, one
 *   card with the battle grade (when grades are on) and, as "battle_recap"
 *   says (0 off, 1 short, 2 full), the battle's numbers. Draw-only: it never
 *   waits, takes the game's random numbers or reads a clock; a button press
 *   puts it away early.
 *
 * Plain C; the counting runs in every build, the pages and the card on
 * Aurora only. */

#ifdef __cplusplus
extern "C" {
#endif

/* --- one battle (port/src/common/battlex_pc.c counts it; the card shows it) --- */

typedef struct PortBattleRecap {
    int damageDealt, damageTaken, bestHit;
    int superguards, guards, stylish;
    int turns, fpUsed, itemsUsed;
    int enemies;    /* enemies defeated */
    int starPoints; /* the battle's Star Points */
    int grade;      /* PORT_GRADE_* (port/include/port/battlex.h), -1 grades off */
    int bonusCoins; /* the grade's */
    unsigned serial; /* a different number each battle won (0: none yet) */
} PortBattleRecap;
/* The last battle won's; its serial (0: none since the start). */
unsigned portBattleRecapLast(PortBattleRecap* out);

/* --- the lifetime counters --- */

enum {
    PORT_STAT_PLAY_FRAMES,    /* frames played (a file loaded, the field or a battle) */
    PORT_STAT_DISTANCE,       /* the field's distance walked, in the game's units (100 a metre) */
    PORT_STAT_COINS,          /* coins gained */
    PORT_STAT_FIELD_JUMPS,    /* jumps in the field */
    PORT_STAT_SPIN_JUMPS,     /* Spin Jumps in the field */
    PORT_STAT_FIELD_HAMMERS,  /* hammer swings in the field */
    PORT_STAT_BATTLES_WON,
    PORT_STAT_BATTLES_FLED,
    PORT_STAT_BATTLES_LOST,
    PORT_STAT_TURNS,
    PORT_STAT_ENEMIES,        /* enemies defeated */
    PORT_STAT_STAR_POINTS,    /* Star Points from battles */
    PORT_STAT_DAMAGE_DEALT,
    PORT_STAT_DAMAGE_TAKEN,
    PORT_STAT_BEST_HIT,       /* a best, not a sum */
    PORT_STAT_SUPERGUARDS,
    PORT_STAT_GUARDS,
    PORT_STAT_STYLISH,
    PORT_STAT_JUMP_ATTACKS,   /* Mario's jumps in battle */
    PORT_STAT_HAMMER_ATTACKS, /* and his hammer */
    PORT_STAT_SPECIALS,       /* Star Powers */
    PORT_STAT_PARTNER_MOVES,  /* the partner's moves */
    PORT_STAT_ITEMS_USED,     /* items used in battle */
    PORT_STAT_FP_SPENT,
    PORT_STAT_APPEALS,
    PORT_STAT_GRADE_D,        /* battles won with each grade (grades on), D to S */
    PORT_STAT_GRADE_C,
    PORT_STAT_GRADE_B,
    PORT_STAT_GRADE_A,
    PORT_STAT_GRADE_S,
    PORT_STAT_COUNT
};
#define PORT_STATS_CHAPTERS 10 /* the prologue, chapters 1 to 8, the epilogue and after */
#define PORT_STATS_KINDS 0xD8  /* the enemies' unit kinds (below UNIT_UNUSED_TREE) */

typedef struct PortStats {
    long long v[PORT_STAT_COUNT];
    long long chapterFrames[PORT_STATS_CHAPTERS];
    unsigned enemies[PORT_STATS_KINDS]; /* defeated, by unit kind */
} PortStats;

/* This file's (NULL: none being counted) and this device's. */
const PortStats* portStatsFile(void);
const PortStats* portStatsTotals(void);
/* The chapter story progress p is in (0 the prologue .. 9 the epilogue). */
int portStatsChapter(int storyProgress);

/* The game's side: a file loaded (portPracticeFileLoaded); each game frame
 * (marioStMain, Aurora's frame hook); the counts to their files when they
 * changed (outside battles). */
void portStatsFileLoaded(void);
void portStatsFrame(void);
void portStatsSave(void);
/* battlex_pc.c: a battle over (1 won, 3 lost, 4 fled) with its numbers (won
 * only: recap, else NULL), and Mario's side's moves (the act record's). */
void portStatsBattleOver(int result, const PortBattleRecap* recap, int jumps, int hammers, int specials,
                         int partnerMoves, int itemsUsed, int fpSpent, int appeals, int turns, int damageTaken);
/* an enemy of kind defeated */
void portStatsEnemyDefeated(int kind);
/* Not counted while this says so (a battle puzzle; set by the Aurora side, NULL none). */
extern int (*gPortStatsSkip)(void);

/* --- Aurora: the Journal's Stats (stats_journal_aurora.c) --- */

struct WindowWork;
/* win_log.c: the entry is there (not online, not in a replay) */
int portStatsJournalEntry(void);
/* its button, as the Photos' (pause_settings_aurora.c) */
void portStatsJournalButton(struct WindowWork* wp, float x, float y, int selected);
/* opened; once a frame (0 stay, -1 back to the entries, -2 the menu closed); drawn at the Journal's (x, y) */
void portStatsJournalOpen(struct WindowWork* wp);
int portStatsJournalMain(struct WindowWork* wp);
void portStatsJournalDisp(struct WindowWork* wp, float x, float y);
/* msgSearch: the entry's help ("pc_menu_stats..."), or NULL */
const char* portStatsJournalMsg(const char* tag);

/* --- Aurora: the recap card (recap_aurora.c) --- */

/* the frame hook and the setting (portBattlexInit) */
void portRecapInit(void);

#ifdef __cplusplus
}
#endif

#endif
