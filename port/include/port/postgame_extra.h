#ifndef PORT_POSTGAME_EXTRA_H
#define PORT_POSTGAME_EXTRA_H
#include "port/save_words.h"

/* More after the story (docs/POSTGAME.md), each off until its setting is on
 * or it is started; none runs online or in a replay unless it says so:
 *
 * - Whacka in the Pit (port/src/aurora/pit_whacka_aurora.c): after Bonetail
 *   has fallen once, Whacka waits on the 100th floor; a fight of Paper Cut's
 *   own moves on his disc model, a Whacka Bump each time he's beaten, and
 *   after the first win the Whacka on Keelhaul Key never runs out of bumps.
 *   The Pit Mover: from the Pit's entrance straight to the 50th floor, once
 *   the Pit has been cleared.
 * - Exhibition matches, the returning champion and Nighttail
 *   (port/src/aurora/exhibition_aurora.c).
 * - The Gold Medal and the Gallery (port/src/aurora/gold_medal_aurora.c).
 * - Second-fight Tattles, the Boo quiz, Newsletter Trouble, the crane
 *   game's post-game prizes, Pianta tokens for coins, the Japanese release's
 *   Trial Stew recipe (port/src/aurora/postgame_more_aurora.c, and the
 *   game-side calls in port/src/common/postgame_extra_pc.c).
 * - Japanese restorations, talk blips and the battle theme picker
 *   (port/src/aurora/restorations_aurora.c, postgame_extra_pc.c).
 *
 * What a save file has done is kept in the save itself, in GSWF bits the
 * game never uses (PORT_GSWF_PG_*), so online sessions (which sync the save's
 * state) and every device see it alike. Plain C; the settings page and the
 * Start menu's forms (port/settings/features/postgame_extra.cpp) call the
 * starts from their threads, and the game side does them on its frame. */

#ifdef __cplusplus
extern "C" {
#endif

/* The save's own record (GSWF 8100-8131: past every flag the game has) */
#define PORT_GSWF_PG_BASE PORT_GSWF_POSTGAME_FIRST
enum {
    PORT_GSWF_PG_WHACKA_BEATEN = PORT_GSWF_PG_BASE + 0, /* Whacka beaten in the Pit, ever */
    PORT_GSWF_PG_NEWS_TAKEN = PORT_GSWF_PG_BASE + 1,    /* Newsletter Trouble: the papers taken */
    PORT_GSWF_PG_NEWS_1 = PORT_GSWF_PG_BASE + 2,        /* delivered: Petalburg, Glitzville, Twilight Town */
    PORT_GSWF_PG_NEWS_2 = PORT_GSWF_PG_BASE + 3,
    PORT_GSWF_PG_NEWS_3 = PORT_GSWF_PG_BASE + 4,
    PORT_GSWF_PG_NEWS_DONE = PORT_GSWF_PG_BASE + 5,     /* the reward given */
    PORT_GSWF_PG_QUIZ_WON = PORT_GSWF_PG_BASE + 6,      /* the Boo quiz's prize given */
    PORT_GSWF_PG_CRANE_0 = PORT_GSWF_PG_BASE + 7,       /* the crane's prize batch (two bits) */
    PORT_GSWF_PG_CRANE_1 = PORT_GSWF_PG_BASE + 8,
    PORT_GSWF_PG_CHAMPION_BEATEN = PORT_GSWF_PG_BASE + 9,
    PORT_GSWF_PG_NIGHTTAIL_BEATEN = PORT_GSWF_PG_BASE + 10,
};

/* --- Whacka and the Pit Mover --------------------------------------------- */

/* Whacka waits on the 100th floor ("pg_pit_whacka" on, not online). */
int portPitWhackaOn(void);
/* The file has beaten him in the Pit. */
int portPitWhackaBeaten(void);
/* The Pit Mover: Mario stands at the Pit's entrance (tik_06) and the Pit has
 * been cleared; 0 and why when it can't now. Done on the next field frame. */
int portPitMoverGo(const char** why);
int portPitMoverReady(void);

/* --- Exhibition matches and the post-game bosses -------------------------- */

#define PORT_EXHIBITION_TEAMS 22
/* The Glitz Pit's teams (the game's roster, its FighterData order without
 * the player's), their English names, and whether one can be picked now
 * (ranked below Mario: beaten in the story, or all of them once he's champion). */
int portExhibitionCount(void);
const char* portExhibitionName(int team);
int portExhibitionOpen(int team);
int portExhibitionStart(int team, const char** why);
/* The returning champion and Nighttail: open, beaten, started. */
int portChampionOpen(void);
int portChampionStart(const char** why);
int portNighttailOpen(void);
int portNighttailStart(const char** why);
/* A fight of these is on (its English name), or NULL. */
const char* portExhibitionRunning(void);
/* 1 while one runs or Mario is in one of their rooms (tou_pg, las_pg: no way out but the fight's end):
 * quick resume and autosave never save there */
int portExhibitionRoom(void);

/* --- Gold Medal and the Gallery ------------------------------------------- */

/* The Journal's counts: Tattles, badges and recipes, and their totals. */
void portJournalCounts(int* tattles, int* tattleTotal, int* badges, int* badgeTotal, int* recipes, int* recipeTotal);
/* Every Tattle, badge and recipe on this file (the Gold Medal, and the gold look). */
int portGoldMedal(void);
/* The Gallery's chapters (0 the prologue .. 8), their English names, the
 * Star Pieces of their areas found and how many there are, and whether open. */
int portGalleryChapters(void);
const char* portGalleryChapterName(int chapter);
int portGalleryStarPieces(int chapter, int* total);
int portGalleryOpen(int chapter);
/* The models of a chapter: how many, and one's English name. */
int portGalleryModels(int chapter);
const char* portGalleryModelName(int chapter, int i);
/* Opens the Gallery page on the Start menu at a chapter (aurora). */
void portGalleryOpenPage(int chapter);

/* --- The rest of the post-game -------------------------------------------- */

/* gor_01.c cook_chk2 through portRecipeCook: the Japanese release's recipes
 * ("pg_jp_recipes"): the dish, or 0. */
int portPgRecipeCook(int a, int b);
/* aji_03.c: the crane's prize in slot (0-8) after the story ("pg_crane_prizes"),
 * or fallback (the game's; also a randomizer's item that isn't a coin). */
int portPgCranePrize(int slot, int fallback);
/* The Boo quiz: its state for the page (questions answered right in the last
 * try, best ever), and the newsletter's step (0 not taken, 1-4: taken and 0-3
 * delivered, 5 rewarded). */
int portBooQuizBest(void);
int portNewsletterStep(void);

/* --- Restorations, talk blips, battle themes ------------------------------ */

/* The game's scripts' calls (USER_FUNCs): aji_03.c's prize for a crane place
 * (in: the place 0-8 and the name of the coin evt_item_entry just put there,
 * swapped for the prize), las_29.c's choice of the Japanese bad
 * ending (out: 1 on). */
struct EventEntry;
int portEvtCranePrize(struct EventEntry* event, int isFirstCall);
int portEvtJpBadEnding(struct EventEntry* event, int isFirstCall);

/* las_29.c: the Japanese release's bad ending ("pg_jp_bad_ending", US and EU). */
int portPgJpBadEnding(void);
/* pmario_sound.c through portMusicPick: the battle theme chosen for the
 * area Mario is in, or id. */
int portPgBattleTheme(int id);
/* The battle tracks on the disc to choose from (0 the game's own). */
int portPgBattleThemeCount(void);
const char* portPgBattleThemeName(int i);
#define PORT_PG_THEME_AREAS 10
const char* portPgThemeAreaName(int area);

/* the game side, once (postgame_aurora.c's portPostgameInit) */
void portPostgameExtraInit(void);
/* each frame of the game */
void portPitWhackaFrame(void);
void portExhibitionFrame(void);
void portPgMoreFrame(void);
void portRestorationsFrame(void);
void portPitWhackaRegister(void);
void portExhibitionRegister(void);

/* battlex_aurora.c's text layout for the game's windows (UTF-8 in and out,
 * before portModsSetMessage): lines of at most cols columns, pages of
 * pageLines lines (0: one page) */
const char* portGameLayout(const char* in, int cols, int pageLines);
/* the menu language's text, English online and in replays (and in Japanese
 * only on the JP release, whose font has it) */
const char* portPgTr(const char* english);

/* The game's messages these add (tags "pc_pg_..."), set again whenever the
 * language changes. kind: 0 plain, 1 speech (laid out in pages), 2 a choice
 * (english: the options split by '|', made a <select> list). */
typedef struct PortPgText {
    const char* tag;
    const char* english;
    int kind;
} PortPgText;
void portPgAddTexts(const PortPgText* texts, int count);
/* Goombella's Tattles of Paper Cut's own NPCs (npc_tattles_aurora.c, portTattleTag) */
void portNpcTattlesRegister(void);

#ifdef __cplusplus
}
#endif

#endif
