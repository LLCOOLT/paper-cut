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
 * the game, and the font's random numbers are put back after its text. So
 * notes are off during online co-op and replays anyway (a game of a session
 * shows the same as the other, a replay plays the same). */

#ifdef __cplusplus
extern "C" {
#endif

/* The phrases (each with one %s, the word) and the words, in English; the
 * relay keeps their numbers, every language has their text. */
int portNotePhraseCount(void);
const char* portNotePhrase(int i);
int portNoteWordCount(void);
const char* portNoteWord(int i);
/* A note's text: in the game's encoding for its font (game), else UTF-8 */
void portNoteText(int phrase, int word, int game, char* out, int size);

/* Whether notes are shown and can be left now: the setting on, a relay, no
 * online co-op or replay. */
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

#ifdef __cplusplus
}
#endif

#endif
