#ifndef PORT_PRACTICE_JUMP_H
#define PORT_PRACTICE_JUMP_H

/* The Practice tab's "Jump to chapter" (port/src/common/practice_jump_pc.c; the question in the game's
 * window: port/src/aurora/practice_jump_aurora.c; the page: port/settings/features/practice.cpp): the
 * loaded file set, in memory, to a point of the story as a real file has it there, and Mario taken to
 * that point's first room.
 *
 * The story's state comes from the game's own story table (src/event.c's event_dt, which its scenario
 * select reads, seq_e3.c): the story (GSW 0) just before that point, every set-up function up to it
 * (the partners who joined, the flags of what has happened, the key items, boots, hammer and Crystal
 * Stars with their Star Powers), the partner out, and the point's map and entrance. Mario's level,
 * HP, FP, BP, the partners' rank, coins and a few recovery items are the late-game test saves' (the
 * same points, local-docs/playtest1); the story's badges are worn as far as the BP goes.
 *
 * The file's progress is replaced in memory only: nothing is saved until the player saves, and the
 * change counts as practice (portPracticeTaint: no achievements, no autosave until a file is loaded).
 * Never online or in a replay (portPracticeCanEdit), nor while a mode lends Mario anything, a chapter
 * is being played again, or on an open-world randomizer file. */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_JUMP_PROLOGUE,       /* the Prologue's end: Rogueport's plaza, the Blooper beaten */
    PORT_JUMP_CHAPTER1,       /* ... the start of chapters 1 to 8 (PORT_JUMP_CHAPTER1 + n - 1) */
    PORT_JUMP_CHAPTER8 = PORT_JUMP_CHAPTER1 + 7,
    PORT_JUMP_SHADOW_QUEEN,   /* the throne room, Grodus and Bowser beaten: on to her */
    PORT_JUMP_POSTGAME,       /* Rogueport after the credits */
    PORT_JUMP_COUNT
};

/* the point's name on the page (English: portTrf it with the chapter, "Start of Chapter %d") and its
 * chapter (0 for the others) */
const char* portPracticeJumpName(int point);
int portPracticeJumpChapter(int point);

/* NULL: a jump can be asked for now; else why not (English) */
const char* portPracticeJumpRefusal(void);

/* Asks in the game's own window first (Yes / No), on the next field frame once no scene or menu is up;
 * Yes jumps. FALSE: refused now (*why: English). Aurora builds; elsewhere portPracticeJumpRequest. */
int portPracticeJumpAsk(int point, const char** why);
/* The jump itself, without the question, on the next field frame (the question's Yes). */
int portPracticeJumpRequest(int point, const char** why);
/* each field frame (rules_pc.c's portRulesFrame) */
void portPracticeJumpFrame(void);
/* the question's part of it (practice_jump_aurora.c) */
void portPracticeJumpAskFrame(void);

/* The game's story table, as its scenario select reads it (shared with port/src/aurora/story_test_aurora.c's
 * TTYD_EVENT): the row counted from 0 over every stage, its stage and index there, and the id of the row
 * before it (the story it starts from); NULL past the end. */
struct EventData;
struct EventData* portEventTableRow(int row, int* stage, int* index, int* prevId);
/* the row's place found by its stage and text id (the nth row of that id there, from 0): its row number,
 * or -1 */
int portEventTableFind(int stage, const char* textId, int nth);
/* A new file's pouch and saved words and flags, then the story up to the row: GSW 0 the id before it,
 * every set-up function up to and including the row's, Mario standing (or on the ship), and the row's
 * partners joined and out. (The scenario select's own HP, items and badges are the caller's.) */
void portEventTableSetUp(const struct EventData* row, int stage, int index, int prevId);

#ifdef __cplusplus
}
#endif

#endif
