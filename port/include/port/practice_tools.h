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
/* The Practice tab's edits (stats, items, badges, flags, the RNG, the party, the warp) can be
 * made now: a game in play, and neither online (state sync: this game alone would change) nor
 * in a replay. Every setter checks it; the page shows portPracticeEditRefusal's line instead. */
int portPracticeCanEdit(void);
/* "Not online or in a replay." (English) when that is why not, else NULL */
const char* portPracticeEditRefusal(void);
/* A mode runs (any, the field's too: a Paper Tower climb, the Pit in co-op, a chapter replay), so the tools
 * that move Mario or set the story up (Jump to chapter, the warp) would break it: an English refusal, else
 * NULL. kPortPracticeModeNamed is a format naming the mode (portPracticeRefusalShown fills it in). */
extern const char* const kPortPracticeModeNamed;
const char* portPracticeModeRefusal(void);
/* the same in English, for the log (the game's thread: one buffer) */
const char* portPracticeRefusalEnglish(const char* why);
/* Aurora: a refusal from these tools in the menu language, kPortPracticeModeNamed with the mode's name */
const char* portPracticeRefusalShown(const char* why);

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
/* In a battle HP and FP are read from and set on Mario's battle unit as well (its HUD shows the unit's,
 * and the battle's end writes the unit's back over the pouch); max HP and FP count again on it. */
int portPracticeStat(int which);
void portPracticeSetStat(int which, int value);
/* the stat's name in the test scripts' practice stat (coins hp maxhp fp maxfp bp sp maxsp level
 * starpoints pieces shines), -1 for none */
int portPracticeStatByName(const char* name);
/* the test scripts' practice show: Mario's and the partner's HP and FP in the pouch, the battle's units
 * (in a battle) and the HUD, one line */
void portPracticeShow(void);
/* src/statuswindow.c: the HUD's shown HP, max HP, FP, max FP, partner HP and max HP (-1: no HUD) */
void portStatusWinShown(int out[6]);

/* Items: kind 0 the normal items, 1 badges, 2 key items: ids [first, end). */
int portPracticeItemFirst(int kind);
int portPracticeItemEnd(int kind);
const char* portPracticeItemName(int id, char* out, int size);
/* the id is a real item in this version's table and text (the lists show only these; Give refuses others):
 * not one of the unused "Invalid Item" entries */
int portPracticeItemListed(int id);
/* the test scripts' practice items: every kind's list checked, one line each, and any listed entry whose
 * name reads as an unused one named ("[practice] items: kind K, N listed, M left out[, BAD ...]") */
void portPracticeItemsCheck(void);
int portPracticeGiveItem(int id); /* FALSE: no room */
int portPracticeHeldItem(int i);  /* the held items, 0-19 (0: empty) */
void portPracticeRemoveItem(int id);

#ifdef __cplusplus
}
#endif

#endif
