#ifndef PORT_NOTES_H
#define PORT_NOTES_H
/* Paper notes (docs/PAPER_NOTES.md): notes left on the ground for other
 * players, as a set phrase and a word (no free text), rated up or down by
 * those who read them; the best of each room kept on the relay.
 *
 * - The relay's side (port/relay/src/index.js, the Notes class): an area's
 *   notes (a map's first three letters) in one store; each map's best sent.
 * - The fetching, leaving and rating (port/settings/features/notes.cpp, on
 *   threads of their own): an area's notes fetched as the player enters it,
 *   kept for 10 minutes; the setting (Multiplayer: Paper notes).
 * - The room's notes drawn on the ground, and the Start menu's page for them
 *   (port/src/aurora/notes_aurora.c, pause_notes_aurora.c): leave one where
 *   Mario stands, rate the others.
 *
 * None of it is the game's state: a note is drawn, never touched or read by
 * the game, and the font's random numbers are put back after its text. One
 * opens with the notes' own button, never A (A by a note is a jump, as
 * anywhere): the "Read a paper note" binding (C, a controller's left
 * shoulder) or the game's L, which Mario doesn't use in the field; a tap on
 * it on a touch screen; off online. So online co-op shows them, each game its
 * own, and the games stay the same; they are off while a replay records or
 * plays, so its playback shows what its recording did. */

#ifdef __cplusplus
extern "C" {
#endif

/* The phrases (each with one %s, the word, or none: "Thank you!") and the
 * words, in English; the relay keeps their numbers, so the lists only grow at
 * the end (tools/port/notes_lists.txt, checked by testpass.py lint); every
 * language has their text. */
int portNotePhraseCount(void);
const char* portNotePhrase(int i);
int portNoteWordCount(void);
const char* portNoteWord(int i);
/* whether the phrase has a %s for a word (one without is left with word 0) */
int portNotePhraseTakesWord(int phrase);
/* whether the page offers the word: Paper Mario 64's partners with a PM64 ROM linked only (a note
 * naming one still shows his name to anyone) */
int portNoteWordOffered(int word);
/* the word as a note shows it, in the game's encoding (the game's own name where its text has one) */
const char* portNoteWordShown(int word);
/* A note's text: in the game's encoding for its font (game), else UTF-8 */
void portNoteText(int phrase, int word, int game, char* out, int size);

/* Whether notes are shown and can be left now: the setting on, a relay (or
 * $TTYD_NOTES_TEST), no replay recording or playing. Online co-op shows them
 * (the Multiplayer tab's page is off a session only). */
int portNotesOn(void);

/* An area's notes wanted (the map now): fetched unless kept less than 10
 * minutes. */
void portNotesWant(const char* map);
/* again now (the page's Refresh) */
void portNotesRefresh(void);

enum { PORT_NOTES_IDLE, PORT_NOTES_LOADING, PORT_NOTES_READY, PORT_NOTES_FAILED, PORT_NOTES_OLD_RELAY };
int portNotesState(void);

typedef struct PortNote {
    char id[16];
    char map[16];
    int x, y, z;
    int phrase, word;
    int score;
    int mine;  /* left from this device */
    int rated; /* this device's rating: 1, -1 or 0 */
} PortNote;

/* the notes of map, as fetched: their count, and each (copied) */
int portNotesCount(const char* map);
int portNotesGet(const char* map, int i, PortNote* out);

/* Leaves a note / rates one (on a thread): its answer, in English, from
 * portNotesAnswer once it came (NULL while waiting). */
void portNotesAdd(const char* map, int x, int y, int z, int phrase, int word);
void portNotesRate(const char* map, const char* id, int d);
const char* portNotesAnswer(int* serial);

/* src/system.c, each frame: the area's notes wanted, the room's drawn
 * (notes_aurora.c). */
void portNotesFrame(void);

#ifdef PORT_NOTES_DRAWING
#include <dolphin/gx.h>
/* How a note looks (notes_aurora.c), for the Start menu's page too: the
 * game's Vital Paper icon (a written sheet), and its tint (gold once rated
 * +3 or more, blue for this device's own, else none). */
u16 portNoteIcon(void);
GXColor portNoteTint(int mine, int score);
#endif

/* Tests ($TTYD_NOTES_TEST, docs/PAPER_NOTES.md): the test's notes placed
 * round Mario's place (x, y, z) in map, once, with no relay. */
void portNotesTestAnchor(const char* map, float x, float y, float z);

#ifdef __cplusplus
}
#endif

#endif
