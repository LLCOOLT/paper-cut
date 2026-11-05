/* The Online tab's Paper notes page (port/include/port/pause_notes.h): a page
 * over the tab's, in the game's windows and font, as the room list's.
 *
 *   Phrase      < Try ... >        left and right pick it
 *   Word        < a Star Piece >
 *   Leave this note here           A: where Mario stands
 *   Refresh
 *   <this room's notes, nearest first>   right rates one up, left down
 *
 * Off during online co-op and replays (portNotesOn): the Online tab shows the
 * row only off a session, so a session's menus stay the same in both games. */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/menu_tap.h"
#include "port/notes.h"
#include "port/pause_notes.h"
#include "port/tr.h"
#include "driver/seqdrv.h"
#include "manager/fontmgr.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include <demo/DEMOPad.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* the page: the status at the top, the rows under it (pause_online_aurora.c's places) */
#define PANEL_X (-262.0f)
#define PANEL_W 524.0f
#define STATUS_TOP 140.0f
#define ROW_X (-236.0f)
#define VALUE_X 240.0f
#define ROW_STEP 24.0f
#define VISIBLE 7
#define ROWS_TOP 78.0f
#define ROW_Y0 68.0f
#define NOTES_LISTED 24

enum { R_PHRASE, R_WORD, R_LEAVE, R_REFRESH, R_FIRST_NOTE };

static const GXColor kHeading = {0x7A, 0x36, 0x08, 0xFF};
static const GXColor kBlack = {0x00, 0x00, 0x00, 0xFF};
static const GXColor kGrey = {0x60, 0x60, 0x60, 0xFF};

static struct {
    int open;
    int row, top;
    int phrase, word;
    const char* answer; /* an action's answer (English) until the cursor moves */
    int answerSerial;
    int asked; /* waiting for portNotesAnswer */
} sPage;

static PortNote sListed[NOTES_LISTED];
static int sListedCount;

static f32 fitText(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* text, int right) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, s, s}, c, "%s", text);
    return full * k;
}

static int inField(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && marioGetPtr() != NULL;
}

/* this room's notes, nearest Mario first */
static void listNotes(void) {
    MarioWork* mario = marioGetPtr();
    float d[NOTES_LISTED];
    int i, j, total;
    sListedCount = 0;
    if (gp == NULL || mario == NULL) {
        return;
    }
    total = portNotesCount(gp->currentMapName);
    for (i = 0; i < total; i++) { /* the nearest NOTES_LISTED of all the room's, nearest first */
        PortNote n;
        float dx, dz, here;
        if (!portNotesGet(gp->currentMapName, i, &n)) {
            break;
        }
        dx = (float)n.x - mario->position.x;
        dz = (float)n.z - mario->position.z;
        here = dx * dx + dz * dz;
        if (sListedCount == NOTES_LISTED && d[NOTES_LISTED - 1] <= here) {
            continue;
        }
        j = sListedCount < NOTES_LISTED ? sListedCount++ : NOTES_LISTED - 1; /* (full: the farthest goes) */
        for (; j > 0 && d[j - 1] > here; j--) {
            sListed[j] = sListed[j - 1];
            d[j] = d[j - 1];
        }
        sListed[j] = n;
        d[j] = here;
    }
}

static int rowCount(void) {
    return R_FIRST_NOTE + sListedCount;
}

static f32 rowY(int i) {
    return ROW_Y0 - (i - sPage.top) * ROW_STEP;
}

void portPauseNotesOpen(void) {
    memset(&sPage, 0, sizeof(sPage));
    sPage.open = 1;
    portNotesAnswer(&sPage.answerSerial);
    if (gp != NULL) {
        portNotesWant(gp->currentMapName);
    }
    listNotes();
}

void portPauseNotesClose(void) {
    sPage.open = 0;
}

int portPauseNotesIsOpen(void) {
    return sPage.open;
}

static void say(const char* english) {
    sPage.answer = english;
}

/* A (dir 0), or left and right, on the row */
static void act(int dir) {
    const int n = portNotePhraseCount(), w = portNoteWordCount();
    switch (sPage.row) {
        case R_PHRASE:
            sPage.phrase = (sPage.phrase + (dir != 0 ? dir : 1) + n) % n;
            psndSFXOn((const char*)0x20005);
            return;
        case R_WORD:
            sPage.word = (sPage.word + (dir != 0 ? dir : 1) + w) % w;
            psndSFXOn((const char*)0x20005);
            return;
        case R_LEAVE:
            if (dir != 0) {
                return;
            }
            if (!inField()) {
                say("Notes can only be left in the field.");
            } else {
                const MarioWork* m = marioGetPtr();
                portNotesAdd(gp->currentMapName, (int)floorf(m->position.x + 0.5f), (int)floorf(m->position.y + 0.5f),
                             (int)floorf(m->position.z + 0.5f), sPage.phrase, sPage.word);
                sPage.asked = 1;
                say("Leaving your note...");
            }
            psndSFXOn((const char*)0x20012);
            return;
        case R_REFRESH:
            if (dir == 0) {
                portNotesRefresh();
                psndSFXOn((const char*)0x20012);
            }
            return;
        default: {
            const PortNote* note = &sListed[sPage.row - R_FIRST_NOTE];
            if (sPage.row - R_FIRST_NOTE >= sListedCount) {
                return;
            }
            if (dir == 0) {
                say("Right rates this note up, left rates it down.");
                return;
            }
            if (note->mine) {
                say("That one is yours.");
            } else if (note->rated != 0) {
                say("You rated that one already.");
            } else {
                portNotesRate(note->map, note->id, dir);
                sPage.asked = 1;
                say("Rating it...");
            }
            psndSFXOn((const char*)0x20012);
            return;
        }
    }
}

int portPauseNotesMain(WindowWork* wp) {
    const int was = sPage.row;
    int count, serial;
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    int press = (wp->buttonTrg & PAD_BUTTON_A) != 0;
    const char* answer;
    listNotes();
    count = rowCount();
    answer = portNotesAnswer(&serial);
    if (sPage.asked && answer != NULL && serial != sPage.answerSerial) {
        sPage.answerSerial = serial;
        sPage.asked = 0;
        sPage.answer = answer;
    }
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        sPage.open = 0;
        return 0;
    }
    if (kind != 0) { /* the row under the pointer */
        int i;
        for (i = sPage.top; i < count && i < sPage.top + VISIBLE; i++) {
            const f32 ry = rowY(i) - 8.0f;
            if (gx >= ROW_X - 16.0f && gx <= VALUE_X + 8.0f && gy <= ry + ROW_STEP / 2 && gy > ry - ROW_STEP / 2) {
                if (i == sPage.row) {
                    press |= kind == 1;
                } else {
                    sPage.row = i;
                }
                break;
            }
        }
    }
    if (press) {
        act(0);
    } else if (wp->dirRep & DEMO_STICK_UP) {
        sPage.row = (sPage.row + count - 1) % count;
    } else if (wp->dirRep & DEMO_STICK_DOWN) {
        sPage.row = (sPage.row + 1) % count;
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        act((wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1);
    }
    if (sPage.row >= count) {
        sPage.row = count - 1;
    }
    if (sPage.row != was) {
        psndSFXOn((const char*)0x20005);
        if (!sPage.asked) {
            sPage.answer = NULL;
        }
    }
    if (sPage.row < sPage.top) {
        sPage.top = sPage.row;
    }
    if (sPage.row >= sPage.top + VISIBLE) {
        sPage.top = sPage.row - VISIBLE + 1;
    }
    wp->helpTgtX = ROW_X - 24.0f;
    wp->helpTgtY = rowY(sPage.row);
    return 1;
}

const char* portPauseNotesHelp(void) {
    if (sPage.answer != NULL) {
        return sPage.answer;
    }
    switch (sPage.row) {
        case R_PHRASE: return "Left and right pick the note's phrase.";
        case R_WORD: return "Left and right pick the word in it.";
        case R_LEAVE: return "Leaves the note where Mario stands, for others to rate.";
        case R_REFRESH: return "Asks the relay again for this area's notes.";
    }
    return "Right: rate up, left: rate down. The best notes stay.";
}

void portPauseNotesDisp(WindowWork* wp, f32 x, f32 y) {
    const int state = portNotesState();
    const int count = rowCount();
    char text[200], game[200];
    int i;
    /* the status: what the page is, the note being written */
    winKirinukiGX(wp, 0, x + PANEL_X, y + STATUS_TOP, PANEL_W, 58.0f);
    winFontInit();
    fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 8.0f, 300.0f, 0.9f, kHeading, portTrGame("Paper notes"), 0);
    {
        const char* say = state == PORT_NOTES_LOADING      ? "Fetching this area's notes..."
                          : state == PORT_NOTES_OLD_RELAY ? "Paper notes aren't on this relay yet."
                          : state == PORT_NOTES_FAILED    ? "The notes couldn't be fetched."
                          : sListedCount == 0              ? "No notes in this room yet: leave the first."
                                                           : NULL;
        if (say != NULL) {
            fitText(x + PANEL_X + PANEL_W - 20.0f, y + STATUS_TOP - 10.0f, 260.0f, 0.65f, kGrey, portTrGame(say), 1);
        } else {
            snprintf(text, sizeof(text), portTrGame(sListedCount == 1 ? "%d note in this room" : "%d notes in this room"),
                     sListedCount);
            fitText(x + PANEL_X + PANEL_W - 20.0f, y + STATUS_TOP - 10.0f, 260.0f, 0.65f, kGrey, text, 1);
        }
    }
    portNoteText(sPage.phrase, sPage.word, 1, game, sizeof(game));
    fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 34.0f, PANEL_W - 40.0f, 0.8f, kBlack, game, 0);
    /* the rows */
    winKirinukiGX(wp, 0, x + PANEL_X, y + ROWS_TOP + 10.0f, PANEL_W, 190.0f);
    winFontInit();
    for (i = sPage.top; i < count && i < sPage.top + VISIBLE; i++) {
        const f32 ry = y + rowY(i);
        const GXColor c = sPage.row == i ? kBlack : kGrey;
        if (i == R_PHRASE || i == R_WORD) {
            char shown[160];
            if (i == R_PHRASE) {
                const char* p = portTrGame(portNotePhrase(sPage.phrase));
                const char* s = strstr(p, "%s");
                if (s != NULL) {
                    snprintf(shown, sizeof(shown), "%.*s...%s", (int)(s - p), p, s + 2);
                } else {
                    snprintf(shown, sizeof(shown), "%s", p);
                }
            } else {
                snprintf(shown, sizeof(shown), "%s", portTrGame(portNoteWord(sPage.word)));
            }
            fitText(x + ROW_X, ry, 150.0f, 0.8f, c, portTrGame(i == R_PHRASE ? "Phrase" : "Word"), 0);
            /* (as the other pages' values: plain; the game's font takes "<...>" for a tag and drew nothing) */
            fitText(x + VALUE_X, ry, 300.0f, 0.8f, c, shown, 1);
        } else if (i == R_LEAVE || i == R_REFRESH) {
            fitText(x + ROW_X, ry, 400.0f, 0.8f, c, portTrGame(i == R_LEAVE ? "Leave this note here" : "Refresh"), 0);
        } else {
            const PortNote* n = &sListed[i - R_FIRST_NOTE];
            char score[48];
            portNoteText(n->phrase, n->word, 1, game, sizeof(game));
            fitText(x + ROW_X, ry, 360.0f, 0.75f, c, game, 0);
            if (n->mine) {
                snprintf(score, sizeof(score), "%+d %s", n->score, portTrGame("(yours)"));
            } else if (n->rated != 0) {
                snprintf(score, sizeof(score), "%+d %s", n->score, n->rated > 0 ? "+" : "-");
            } else {
                snprintf(score, sizeof(score), "%+d", n->score);
            }
            fitText(x + VALUE_X, ry, 110.0f, 0.75f, n->score > 0 ? kHeading : c, score, 1);
        }
    }
    if (sPage.top > 0 || sPage.top + VISIBLE < count) {
        char pos[16];
        snprintf(pos, sizeof(pos), "%d/%d", sPage.row + 1, count);
        winFontInit();
        fitText(x + VALUE_X, y + ROWS_TOP - 170.0f, 80.0f, 0.6f, kGrey, pos, 1);
    }
}
