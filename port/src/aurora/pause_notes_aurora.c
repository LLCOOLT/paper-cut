/* The Online tab's Paper notes page (port/include/port/pause_notes.h): a page
 * over the tab's, in the game's windows and font, as the room list's.
 *
 *   Phrase      < Try ... >        left and right pick it
 *   Word        < a Star Piece >
 *   Leave this note here           A: where Mario stands
 *   Refresh
 *   <this room's notes, nearest first>   right rates one up, left down
 *
 * Off in replays (portNotesOn); online co-op shows the notes in the room, but
 * the Multiplayer tab shows this page's row only off a session, so a
 * session's menus stay the same in every game. */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/button_markup.h"
#include "port/text_fit.h"
#include "port/menu_tap.h"
#define PORT_NOTES_DRAWING
#include "port/notes.h"
#include "port/pause_notes.h"
#include "port/tr.h"
#include "driver/icondrv.h"
#include "driver/seqdrv.h"
#include "icon_ids.h"
#include <dolphin/mtx.h>
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
#define STATUS_TOP 142.0f
#define STATUS_H 74.0f
#define ROW_X (-236.0f)
#define VALUE_X 236.0f
#define NOTE_X (ROW_X + 30.0f) /* a listed note's text, after its letter */
#define ROW_STEP 24.0f
#define VISIBLE 6
#define ROWS_TOP 52.0f
#define ROW_Y0 42.0f
#define ROWS_H (VISIBLE * ROW_STEP + 22.0f)
#define NOTES_LISTED 24

enum { R_PHRASE, R_WORD, R_LEAVE, R_REFRESH, R_FIRST_NOTE };

static const GXColor kHeading = {0x7A, 0x36, 0x08, 0xFF};
static const GXColor kBlack = {0x00, 0x00, 0x00, 0xFF};
static const GXColor kGrey = {0x60, 0x60, 0x60, 0xFF};
static const GXColor kFaint = {0xA8, 0xA8, 0xA8, 0xFF}; /* a row with nothing to pick */
static const GXColor kBlue = {0x28, 0x60, 0xC0, 0xFF};

/* a note's sheet, as the room shows it (notes_aurora.c's portNoteIcon and portNoteTint): gold once rated +3
   or more, blue for this device's own; (x, y) its foot's middle, in the 2D camera */
static void letter(f32 x, f32 y, f32 scale, int mine, int score) {
    Mtx m, t;
    PSMTXTrans(m, x, y, 0.0f);
    PSMTXScale(t, scale, scale, 1.0f);
    PSMTXConcat(m, t, m);
    iconDispGxCol(m, ICON_DISP_2D, portNoteIcon(), portNoteTint(mine, score));
}

/* the game's list arrow (the up one, turned): dir 0 up, 1 down, 2 left, 3 right; (x, y) its middle */
static void arrow(f32 x, f32 y, int dir, f32 scale) {
    static const f32 kTurn[4] = {0.0f, 3.14159265f, 1.57079633f, -1.57079633f};
    Mtx m, t;
    PSMTXTrans(m, x, y, 0.0f);
    PSMTXRotRad(t, 'z', kTurn[dir]);
    PSMTXConcat(m, t, m);
    PSMTXScale(t, scale, scale, 1.0f);
    PSMTXConcat(m, t, m);
    PSMTXTrans(t, 0.0f, -12.0f, 0.0f); /* (the icon's foot at its middle: turned round it) */
    PSMTXConcat(m, t, m);
    iconDispGxCol(m, ICON_DISP_2D, ICON_1B5, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
}

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
    const f32 textWas = portButtonsGameTextScale(s); /* (a {A} in it: centred on the letters at s, button_markup.h) */
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    portTextFit("notes-text", text, full * s, w, 1);
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, portTextFitHeight(k, s), portTextFitHeight(k, s)}, c, "%s", text);
    portButtonsGameTextScale(textWas);
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

/* Tests ($TTYD_TEXT_FIT): every phrase with every word it takes measured as the balloon draws it
 * (notes_aurora.c's balloonDraw: 0.8 of the font, shrunk past 400), as the page opens; the longest
 * printed. A note that has to shrink under 0.8 of that is written to the fit log as squeezed. */
static void fitEveryNote(void) {
    static int sDone = -1;
    char text[200], longest[200] = "";
    f32 most = 0.0f;
    int p, w;
    if (!portTextFitOn() || sDone == portTrLanguage()) {
        return;
    }
    sDone = portTrLanguage();
    for (p = 0; p < portNotePhraseCount(); p++) {
        for (w = 0; w < (portNotePhraseTakesWord(p) ? portNoteWordCount() : 1); w++) {
            f32 width;
            portNoteText(p, w, 1, text, sizeof(text));
            width = (f32)FontGetMessageWidth(text) * 0.8f;
            portTextFit("notes-balloon", text, width, 400.0f, 1);
            if (width > most) {
                most = width;
                snprintf(longest, sizeof(longest), "%s", text);
            }
        }
    }
    portTextFromGame(longest, text, sizeof(text));
    printf("[notes] fit: the longest note %.0f wide (the balloon 400): %s\n", most, text);
    fflush(stdout);
}

void portPauseNotesOpen(void) {
    fitEveryNote();
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
        case R_WORD: {
            int k;
            if (!portNotePhraseTakesWord(sPage.phrase)) {
                return; /* (the phrase has no word: the row is passed over) */
            }
            for (k = 0; k < w; k++) { /* the next word offered (the PM64 partners with the ROM linked) */
                sPage.word = (sPage.word + (dir != 0 ? dir : 1) + w) % w;
                if (portNoteWordOffered(sPage.word)) {
                    break;
                }
            }
            psndSFXOn((const char*)0x20005);
            return;
        }
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
                if (i == R_WORD && !portNotePhraseTakesWord(sPage.phrase)) {
                    break; /* (no word for this phrase: its row can't be chosen) */
                }
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
        if (sPage.row == R_WORD && !portNotePhraseTakesWord(sPage.phrase)) {
            sPage.row = R_PHRASE; /* (a phrase with no word: its row passed over) */
        }
    } else if (wp->dirRep & DEMO_STICK_DOWN) {
        sPage.row = (sPage.row + 1) % count;
        if (sPage.row == R_WORD && !portNotePhraseTakesWord(sPage.phrase)) {
            sPage.row = R_LEAVE;
        }
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
    /* the status: what the page is, and the note being written, as a sheet with its words */
    winKirinukiGX(wp, 0, x + PANEL_X, y + STATUS_TOP, PANEL_W, STATUS_H);
    letter(x + PANEL_X + 34.0f, y + STATUS_TOP - 68.0f, 0.75f, 1, 0);
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
    fitText(x + PANEL_X + 60.0f, y + STATUS_TOP - 40.0f, PANEL_W - 80.0f, 0.85f, kBlack, game, 0);
    /* the rows: the note's phrase and word, leaving it, Refresh, then the room's notes, each with its sheet */
    winKirinukiGX(wp, 0, x + PANEL_X, y + ROWS_TOP + 10.0f, PANEL_W, ROWS_H);
    for (i = sPage.top; i < count && i < sPage.top + VISIBLE; i++) {
        const f32 ry = y + rowY(i);
        if (i >= R_FIRST_NOTE) {
            const PortNote* n = &sListed[i - R_FIRST_NOTE];
            letter(x + ROW_X + 11.0f, ry - 22.0f, 0.55f, n->mine, n->score);
        } else if ((i == R_PHRASE || i == R_WORD) && sPage.row == i) {
            arrow(x + VALUE_X - 6.0f, ry - 10.0f, 3, 0.45f); /* (its value's left one: below, once measured) */
        }
    }
    /* more above or below: the game's list arrows on the panel's edges, in its middle */
    if (sPage.top > 0) {
        arrow(x + PANEL_X + PANEL_W * 0.5f, y + ROWS_TOP + 10.0f, 0, 0.5f);
    }
    if (sPage.top + VISIBLE < count) {
        arrow(x + PANEL_X + PANEL_W * 0.5f, y + ROWS_TOP + 10.0f - ROWS_H, 1, 0.5f);
    }
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
            } else if (!portNotePhraseTakesWord(sPage.phrase)) {
                /* (the phrase has no word: the row greyed, with none in it) */
                fitText(x + ROW_X, ry, 150.0f, 0.8f, kFaint, portTrGame("Word"), 0);
                fitText(x + VALUE_X - 16.0f, ry, 280.0f, 0.8f, kFaint, "-", 1);
                continue;
            } else {
                snprintf(shown, sizeof(shown), "%s", portNoteWordShown(sPage.word));
            }
            fitText(x + ROW_X, ry, 150.0f, 0.8f, c, portTrGame(i == R_PHRASE ? "Phrase" : "Word"), 0);
            /* (the game's font takes "<...>" for a tag: the chosen row's value between the game's arrows) */
            if (sPage.row == i) {
                const f32 w = fitText(x + VALUE_X - 16.0f, ry, 280.0f, 0.8f, c, shown, 1);
                arrow(x + VALUE_X - 26.0f - w, ry - 10.0f, 2, 0.45f);
                winFontInit();
            } else {
                fitText(x + VALUE_X - 16.0f, ry, 280.0f, 0.8f, c, shown, 1);
            }
        } else if (i == R_LEAVE || i == R_REFRESH) {
            fitText(x + ROW_X, ry, 400.0f, 0.8f, c, portTrGame(i == R_LEAVE ? "Leave this note here" : "Refresh"), 0);
        } else {
            const PortNote* n = &sListed[i - R_FIRST_NOTE];
            char score[16];
            const char* whose = n->mine           ? portTrGame("yours")
                                : n->rated > 0    ? portTrGame("rated up")
                                : n->rated < 0    ? portTrGame("rated down")
                                                  : NULL;
            f32 right = x + VALUE_X;
            portNoteText(n->phrase, n->word, 1, game, sizeof(game));
            snprintf(score, sizeof(score), "%+d", n->score);
            right -= fitText(right, ry, 50.0f, 0.75f, n->score >= 3 ? kHeading : n->score > 0 ? kBlack : kGrey, score,
                             1) + 10.0f;
            if (whose != NULL) {
                right -= fitText(right, ry + 1.0f, 110.0f, 0.62f, n->mine ? kBlue : kGrey, whose, 1) + 12.0f;
            }
            fitText(x + NOTE_X, ry, right - (x + NOTE_X), 0.75f, c, game, 0);
        }
    }
}
