#ifndef PORT_PARTY_H
#define PORT_PARTY_H

/* Party and race modes over the story (docs/MODES.md): bingo, the quiz party,
 * Tattle card duels and the daily seed. Each is off until started, so the
 * normal game is unchanged (and online co-op's fingerprints with it).
 *
 * - Bingo (port/src/aurora/bingo_aurora.c, port/settings/features/bingo.cpp):
 *   a 5x5 card of goals from a seed, each checked from the file's own state
 *   (its pouch, partners, Tattle Log, story), a small tracker over the
 *   picture; lockout through a bingo room on the relay, which keeps who
 *   claimed each square first. Only reads the game.
 * - The quiz party (port/src/aurora/quiz_aurora.c, port/settings/features/
 *   quiz.cpp): trivia from the game's own data for up to four players on one
 *   screen, in the game's windows. Local only.
 * - Tattle cards (port/src/aurora/cards_aurora.c, port/settings/features/
 *   cards.cpp): the Tattle Log's enemies as cards with their own numbers, a
 *   deck built on the Modes page, duels at Rogueport's Westside against its
 *   card players, against a second controller, or online against the other
 *   player (a draft from the same pool, every pick through lockstep).
 * - The daily seed (port/src/aurora/daily_aurora.c, port/settings/features/
 *   daily.cpp): the same open-world seed and challenge rules for everyone
 *   each day from the date, the file's clock, a results board on the relay.
 *   It only writes the randomizer's and the rules' settings (as a player
 *   would); online they travel with the host's settings as any do.
 *
 * The game side runs on the game's frame (party_aurora.c's marioStMain
 * hook); the settings page draws on the same thread, between frames. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- the enemies' numbers (tattle_cards_gen.c, tools/port/gen_tattle_cards.py) --- */

/* From the decomp's battle units: a kind's max HP, its strongest plain
 * attack, its normal defence and its level. */
typedef struct PortCardStats {
    short unit, hp, atk, def, level;
} PortCardStats;
extern const PortCardStats gPortCardStats[];
extern const int gPortCardStatsCount;

/* --- shared (party_aurora.c) --------------------------------------------- */

/* Once, from the play modes' init (playmodes_aurora.c). */
void portPartyInit(void);
/* src/sdk/DEMOPad.c, after online and replays have put this frame's input in
 * place (the same in every game of a session): a party game up takes it. */
struct PADStatus;
void portPartyPad(struct PADStatus* pads);
/* The field is calm: a file in play, no battle, no menu or scene, Mario free. */
int portPartyFieldReady(void);
/* A seed's text as a number (FNV-1a), and a small generator of our own (never
 * the game's random numbers). */
unsigned long long portPartyHash(const char* text);
typedef struct PortPartyRand {
    unsigned long long s;
} PortPartyRand;
unsigned portPartyNext(PortPartyRand* r);
int portPartyBelow(PortPartyRand* r, int n); /* 0..n-1 */

/* Drawing in the game's windows and font (from a dispEntry callback, the
 * game's random numbers kept around it: the font takes some per letter): a
 * system window, and text (the game's encoding) broken between words to fit
 * width; returns the lines it took (step: 24 * scale). */
void portPartyWindow(float x, float y, float w, float h);
int portPartyText(float x, float y, float scale, unsigned rgba, float width, const char* text);
/* the same, its last line at y (a window's foot: more lines go up from it) */
int portPartyTextAbove(float x, float y, float scale, unsigned rgba, float width, const char* text);
/* the lines portPartyText would take, nothing drawn (a list kept above a foot that may wrap) */
int portPartyTextLines(float scale, float width, const char* text);
float portPartyTextWidth(const char* text, float scale);
/* a list's cursor: the game's own hand (the select lists', icon 0x1F0, always loaded), pointing right at
 * a row whose text starts just right of x, its middle about y (the game's font has no ">") */
void portPartyCursor(float x, float y, float scale);
/* The controllers' buttons pressed this frame (portPartyPad's, while a party
 * game has them), per controller; connected: a bit per controller there. */
unsigned short portPartyPressed(int pad);
unsigned portPartyConnected(void);
/* An enemy kind's numbers (gPortCardStats), or NULL; its name and Tattle Log
 * text from the game's messages (its encoding), "" when it has none. */
const PortCardStats* portPartyStats(int unit);
const char* portPartyEnemyName(int unit);
/* The kind is in the Tattle Log's list (it has a log entry), and whether this
 * file has tattled it. */
int portPartyEnemyListed(int unit);
int portPartyEnemyTattled(int unit);
#define PORT_PARTY_UNITS 0xD8
/* Tattle cards: Paper Mario 64 enemy i (port/pm64_enemy.h) is card PORT_CARDS_PM64 + i, with a linked ROM */
#define PORT_CARDS_PM64 0x200

/* --- the quiz party (quiz_aurora.c) -------------------------------------- */

/* Asks for a quiz: it starts once Mario is free in the field (not online,
 * not in a replay); 0 and why when it can't. */
int portQuizStart(const char** why);
int portQuizActive(void);
void portQuizFrame(void);
int portQuizPad(void); /* the quiz has the controllers */
/* the last quiz's winner (1-4, 0 a draw, -1 none yet) and best score */
int portQuizLastWinner(int* score);

/* --- Tattle cards (cards_aurora.c) --------------------------------------- */

#define PORT_CARDS_DECK 8   /* cards in a deck */
#define PORT_CARDS_STARS 20 /* a deck's stars at most */
#define PORT_CARDS_HAND 5   /* cards drafted for a duel against a player */

enum {
    PORT_DUEL_ROOKIE,   /* the Westside's card players, easiest first */
    PORT_DUEL_REGULAR,
    PORT_DUEL_CHAMPION,
    PORT_DUEL_NPCS,
    PORT_DUEL_PLAYER = PORT_DUEL_NPCS, /* the second controller, or online the other player */
};

/* A card's cost in stars (1-5), from its numbers; 0: not a card. */
int portCardStars(int unit);
/* A card's numbers as it plays them: the kind's own (portPartyStats), the
 * strongest brought down to the top of the five-star scale. */
PortCardStats portCardStats(int unit);
/* The deck (enemy kinds): the one built, or one made from the file's Tattle
 * Log when that's not a valid deck now (fewer cards known, too many stars). */
int portCardsDeck(int* units);
/* The deck as built (as kept in <user>/cards.txt, maybe not valid now). */
int portCardsBuilt(int* units);
void portCardsSetDeck(const int* units, int n);
/* The file's Tattle Log has the kind (a card it may use). */
int portCardsOwned(int unit);
/* Mario is at Rogueport's Westside (gor_02), where its card players sit. */
int portCardsAtTable(void);
/* A duel against opponent; 0 and why when it can't now. From the Start menu
 * (online: both games ask in the same frame) or the settings page (local). */
int portCardsDuel(int opponent, const char** why);
const char* portCardsOpponent(int opponent); /* English */
int portCardsWins(int opponent);             /* wins against them on this computer */
/* gor_02.c's card players (CALL(portEvtCardsTalk, PORT_DUEL_...) at the end
 * of their talk): the duel, when the setting "cards_talk" is on and one can be */
struct EventEntry;
int portEvtCardsTalk(struct EventEntry* event, int isFirstCall);
int portCardsActive(void);
int portCardsPad(void);
void portCardsFrame(void);

/* --- the daily seed (daily_aurora.c, port/settings/features/daily.cpp) ---- */

/* Today (UTC) as YYYYMMDD: everyone's day changes at the same moment. */
int portDailyToday(void);
/* The file in play is a daily file (Paper Cut's open world stamped with a
 * "DAILY-YYYYMMDD" seed): its date, else 0. */
int portDailyFileDate(void);
/* Its frames played so far (60 a second), and the frames when its goal was
 * reached (0: not yet); the goal: portDailyGoalStars() Crystal Stars. */
unsigned portDailyFrames(void);
unsigned portDailyDoneFrames(void);
void portDailyFrame(void);
/* The days in a row, ending with this file's, whose daily goal was reached on
 * this device (0: not reached on this file yet; the achievements' streak). */
int portDailyStreak(void);
/* The Crystal Stars this build's open world has (from PORT_OPEN_STAGE, so the
 * daily follows the randomizer), and the daily's goal: this many of them, any
 * chapters' (3, a one-sitting run, or all of them while it has fewer). */
int portDailyStageStars(void);
int portDailyGoalStars(void);

/* --- bingo (bingo_aurora.c) ---------------------------------------------- */

#define PORT_BINGO_CELLS 25
#define PORT_BINGO_SEED_MAX 24

/* The card in play: its seed ("" none), made again from it. */
const char* portBingoSeed(void);
/* A new card from seed (NULL or "": a random one); its marks cleared. */
void portBingoNew(const char* seed);
void portBingoClear(void); /* no card */
/* Cell i's goal in the menu language (UTF-8), or in the game's encoding for
 * its font (game 1); out at least 128 bytes. */
void portBingoCellText(int i, int game, char* out, size_t size);
/* Cell i is done on this file (checked once, it stays). */
int portBingoDone(int i);
/* Lockout: who has cell i: 0 nobody, 1 this player, 2 someone else; set from
 * the room's answer (bingo.cpp). */
int portBingoOwner(int i);
void portBingoSetOwner(int i, int owner);
/* In a lockout room: only squares this player owns count for its lines. */
void portBingoSetLockout(int on);
int portBingoLockout(void);
/* Lines done (rows, columns and the two diagonals: 12 at most), squares. */
int portBingoLines(void);
int portBingoCount(void);
/* A count that changes each time a cell is newly done (the tracker's flash). */
int portBingoChanges(void);
/* Milliseconds since the card was made, and when the first line was (0: not yet). */
unsigned long long portBingoElapsed(void);
unsigned long long portBingoFirstLine(void);
/* Each frame (party_aurora.c). */
void portBingoFrame(void);

#ifdef __cplusplus
}
#endif

#endif
