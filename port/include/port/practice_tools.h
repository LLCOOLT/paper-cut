#ifndef PORT_PRACTICE_TOOLS_H
#define PORT_PRACTICE_TOOLS_H

/* The Practice tab's reach into the game (port/src/common/practice_tools_pc.c):
 * the saved words and flags, a memory watch, stats and items. Each change
 * marks the run (no achievements from it, port/include/port/practice.h). */

#ifdef __cplusplus
extern "C" {
#endif

/* A game is running (the field, a battle, a map change, Game Over). */
int portPracticeInGame(void);

/* GSW (saved words; 0 is the story, a word) and GSWF (saved flags). */
int portPracticeGsw(int index);
void portPracticeSetGsw(int index, int value);
int portPracticeGswf(int index);
void portPracticeSetGswf(int index, int on);

/* The watch's fields ("mario.position", "pouch.coins", ...), and one's value as text. */
int portWatchCount(void);
const char* portWatchName(int i);
int portWatchRead(int i, char* out, int size);

/* Mario's stats in the pouch. */
enum {
    PORT_STAT_COINS,
    PORT_STAT_HP,
    PORT_STAT_MAX_HP, /* without HP Plus */
    PORT_STAT_FP,
    PORT_STAT_MAX_FP, /* without FP Plus */
    PORT_STAT_BP,
    PORT_STAT_SP,
    PORT_STAT_MAX_SP,
    PORT_STAT_LEVEL,
    PORT_STAT_STAR_POINTS,
    PORT_STAT_STAR_PIECES,
    PORT_STAT_SHINE_SPRITES,
    PORT_STAT_COUNT
};
int portPracticeStat(int which);
void portPracticeSetStat(int which, int value);

/* Items: kind 0 the normal items, 1 badges, 2 key items: ids [first, end). */
int portPracticeItemFirst(int kind);
int portPracticeItemEnd(int kind);
const char* portPracticeItemName(int id, char* out, int size);
int portPracticeGiveItem(int id); /* FALSE: no room */
int portPracticeHeldItem(int i);  /* the held items, 0-19 (0: empty) */
void portPracticeRemoveItem(int id);

#ifdef __cplusplus
}
#endif

#endif
