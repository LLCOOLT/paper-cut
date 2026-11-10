/* Where now, when a room is over for this player (port/include/port/online_end.h).
 *
 * A co-op guest's game has the host's world in its memory: it never just goes on, and never times out
 * without a word. Its window says why ("<host> left the room.", "The host removed you from the room.",
 * "You left the room.", the online service gone) and that the copy of the host's adventure is kept
 * (<user>/coop, coop_save_pc.c), and offers:
 *   Back to your own adventure   the player's own last save as it was (the file they played before
 *                                joining, or the newest on the card), loaded as Continue loads one
 *   Title screen
 * (and Try again first when it was the online service). When the online service can't be reached at all
 * (Host, Join by code, the launcher's, an invite link), "Can't reach the online service. Try again, or play
 * on one computer." with Try again and Back. Shown at the next calm moment (the field with no battle and no
 * Start menu, or the title), Mario's controls off meanwhile; nothing is saved while it's up (the world is
 * the host's: portStateSyncCopyHeld). Drawn as the file menu's own window (coop_copy_pc.c's look).
 * Nothing is chosen without the player's press or click. Tests: $TTYD_SYNC_END_PICK=own|title|retry|back,
 * taken 60 frames after it's up; the headless build (no window, no player) takes it, else the last choice. */
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "manager/cardmgr.h"
#include "pmario_sound.h"
#include "system.h"
#include "win/win_main.h"
#include "port/cardsave_pc.h"
#include "port/online_end.h"
#include "port/platform.h"
#include "port/save_files.h"
#include "port/statesync.h"
#include "port/tr.h"
#include <demo/DEMOPad.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TTYD_BACKEND_AURORA
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/windowdrv.h"
#include "manager/fontmgr.h"
#include "port/menu_tap.h"
#include "port/party.h"
#include "port/text_fit.h"
#include "sdk/texPalette.h"
#include <math.h>
void* portLoadMenuTpl(void); /* seq_load.c: the file menu's pictures */


#endif

#define SFX_MOVE ((const char*)9)
#define SFX_OK ((const char*)0x12)

enum { R_OWN, R_TITLE, R_RETRY, R_BACK };

static struct {
    int why;      /* PORT_END_*, 0 none */
    int shown;    /* drawn and taking input now (else waiting for a calm moment) */
    int ownSlot;  /* the player's own file, -1 none on the card */
    int row;
    int rows[3];
    int count;
    unsigned at;  /* frames shown */
    int noServiceSeen; /* (the service's failure asked about once per failure) */
    char host[96];     /* UTF-8 */
} E;

static const char* rowLabel(int r) {
    switch (r) {
        case R_OWN:
            return "Back to your own adventure";
        case R_TITLE:
            return "Title screen";
        case R_RETRY:
            return "Try again";
        default:
            return "Back";
    }
}

/* card file i's save time: -1 when it holds no save (empty or broken), -2 when the card isn't read (not
   known: taken as there) */
static long long slotSaved(int i) {
    static uint8_t image[PORT_CARD_SLOT_SIZE];
    const CardData* card = cardGetFilePtr();
    PortSaveInfo info;
    if (i < 0 || i > 3) {
        return -1;
    }
    if (card == NULL) {
        return -2;
    }
    if ((card->saves[i].data.flags & (CARD_SAVE_FLAG_EMPTY | CARD_SAVE_FLAG_CORRUPT)) != 0) {
        return -1;
    }
    if (!portCardSlotToImage(&card->saves[i], image) || !portSaveImageInfo(image, &info)) {
        return -1;
    }
    return info.saveTime >= 0 ? info.saveTime : 0;
}

/* the newest save on the card (the player's own, as it was: a guest's game never wrote it), -1 none */
static int newestSlot(void) {
    long long best = -1;
    int i, at = -1;
    for (i = 0; i < 4; i++) {
        const long long t = slotSaved(i);
        if (t > best) {
            best = t;
            at = i;
        }
    }
    return at;
}

/* the main line, English (with the host's name: out) */
static void mainLine(char* out, size_t cap, int game) {
    const char* name = E.host[0] != '\0' ? E.host : "The host";
    char shown[96];
    switch (E.why) {
        case PORT_END_HOST_LEFT:
            if (game) {
                portTextForGame(name, shown, sizeof(shown));
                snprintf(out, cap, portTrGame("%s left the room."), shown);
            } else {
                snprintf(out, cap, "%s left the room.", name);
            }
            return;
        case PORT_END_REMOVED:
            snprintf(out, cap, "%s", game ? portTrGame("The host removed you from the room.")
                                         : "The host removed you from the room.");
            return;
        case PORT_END_LEFT:
            snprintf(out, cap, "%s", game ? portTrGame("You left the room.") : "You left the room.");
            return;
        default:
            snprintf(out, cap, "%s", game ? portTrGame(PORT_SYNC_NO_SERVICE) : PORT_SYNC_NO_SERVICE);
            return;
    }
}

static int keeps(void) {
    return E.why != PORT_END_NO_SERVICE;
}

static void open_(int why, const char* host, int ownSlot) {
    char line[256];
    memset(&E.rows, 0, sizeof(E.rows));
    E.why = why;
    E.shown = 0;
    E.at = 0;
    E.row = 0;
    E.count = 0;
    snprintf(E.host, sizeof(E.host), "%s", host != NULL ? host : "");
    /* (a file that is really there: a game started without one, as a test's or the launcher's warp, still
       says file 1, and loading an empty file leaves the screen black; none: the title screen alone) */
    E.ownSlot = slotSaved(ownSlot) != -1 ? ownSlot : newestSlot();
    if (why == PORT_END_NO_SERVICE || why == PORT_END_SERVICE) {
        E.rows[E.count++] = R_RETRY;
    }
    if (why == PORT_END_NO_SERVICE) {
        E.rows[E.count++] = R_BACK;
    } else {
        if (E.ownSlot >= 0) {
            E.rows[E.count++] = R_OWN;
        }
        E.rows[E.count++] = R_TITLE;
    }
    mainLine(line, sizeof(line), 0);
    ssLog("end: %s%s (%s)", line, keeps() ? " Your copy of the adventure is kept." : "",
          E.ownSlot >= 0 && keeps() ? "the player's own file is there" : "no file of the player's own");
}

void portOnlineEnd(int why, const char* host, int ownSlot) {
    if (E.why != PORT_END_NONE && E.why != PORT_END_NO_SERVICE) {
        return; /* (already asking: the first reason stands) */
    }
    open_(why, host, ownSlot);
}

int portOnlineEndUp(void) {
    return E.why != PORT_END_NONE;
}

int portOnlineEndWhy(void) {
    return E.why;
}

static void close_(void) {
    E.why = PORT_END_NONE;
    E.shown = 0;
    if (gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && !marioChkKey()) {
        marioKeyOn();
    }
}

void portOnlineEndDrop(void) {
    if (E.why == PORT_END_NONE) {
        return;
    }
    ssLog("end: dropped (%s): the player chose where to go", E.shown ? "shown" : "not shown yet");
    close_();
}

void portOnlineEndStale(void) {
    if (E.why == PORT_END_NONE) {
        return;
    }
    /* (a Host or Join again with the last room's "where now" still waiting for a calm moment: a guest that
       left mid-battle and joined back took its seat again, then the old window came up, was answered, and
       left the room a second time) */
    ssLog("end: dropped (%s): in a room again", E.shown ? "shown" : "not shown yet");
    close_();
}

static void choose(int r) {
    ssLog("end: chose \"%s\"", rowLabel(r));
    switch (r) {
        case R_RETRY:
            close_();
            if (!portStateSyncRetry()) {
                ssLog("end: nothing to try again (%s)", portStateSyncNote());
            }
            return;
        /* (the room left first, then the window closed: left after, a game still in a room asked "where now"
           once more for its own leave, and the answer to the first was that second window's) */
        case R_BACK:
            portStateSyncLeave();
            close_();
            return;
        case R_OWN: {
            const int slot = E.ownSlot;
            if (slotSaved(slot) == -1) {
                ssLog("end: the player's own file %d is gone: the title screen", slot + 1);
                choose(R_TITLE);
                return;
            }
            portStateSyncLeave();
            close_();
            ssLog("end: back to the player's own adventure (file %d)", slot + 1);
            portWarpRequestSlot(slot);
            return;
        }
        default:
            portStateSyncLeave();
            close_();
            if (gp != NULL) {
                gp->resetType = 1; /* (soft_reset_pc.c: the title, the game's own way) */
            }
            return;
    }
}

/* a calm moment to ask: the field (no battle, no Start menu, no map change) or the title */
static int calm(void) {
    const int seq = seqGetSeq();
    if (gp == NULL) {
        return 0;
    }
    return (seq == SEQ_GAME && !gp->inBattle && !seqCheckSeq() && !winCheck()) || seq == SEQ_TITLE;
}

#ifdef TTYD_BACKEND_AURORA
#define WIN_W 460.0f
#define WIN_X (-WIN_W / 2.0f)
#define WIN_Y 120.0f
#define WIN_H 230.0f
#define ROW_X (WIN_X + 110.0f)
#define ROW_Y (WIN_Y - 120.0f)
#define ROW_STEP 28.0f

static void textFit(float x, float y, float w, float scale, GXColor c, const char* text, int centre) {
    const float full = (float)FontGetMessageWidth(text) * scale;
    float sx = scale;
    if (full > w && full > 0.0f) {
        portTextFit("online-end", text, full, w, 1);
        sx = scale * w / full;
    }
    if (centre) {
        x -= (full > w ? w : full) / 2.0f;
    }
    winFontInit();
    winFontSet((Vec){x, y, 0.0f}, (Vec){sx, portTextFitHeight(sx, scale), 1.0f}, c, "%s", text);
}

static void disp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    const GXColor black = {0x20, 0x20, 0x20, 0xFF};
    TEXPalette* tpl = (TEXPalette*)portLoadMenuTpl();
    char line[256];
    int i;
    (void)camId;
    (void)param;
    if (!E.shown) {
        return;
    }
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x70}, -400.0f, 260.0f, 800.0f, 520.0f, 4.0f);
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x50}, WIN_X + 5.0f, WIN_Y - 5.0f, WIN_W, WIN_H, 20.0f);
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xFF}, WIN_X, WIN_Y, WIN_W, WIN_H, 20.0f);
    mainLine(line, sizeof(line), 1);
    portPartyText(WIN_X + 28.0f, WIN_Y - 18.0f, 0.75f, 0x202020FFu, WIN_W - 56.0f, line);
    if (keeps()) {
        portPartyText(WIN_X + 28.0f, WIN_Y - 70.0f, 0.55f, 0x505050FFu, WIN_W - 56.0f,
                      portTrGame("Your copy of the adventure is kept."));
    }
    for (i = 0; i < E.count; i++) {
        const float y = ROW_Y - (float)i * ROW_STEP;
        textFit(ROW_X, y, WIN_W - 150.0f, 0.65f, black, portTrGame(rowLabel(E.rows[i])), 0);
        if (i == E.row && tpl != NULL) {
            const float bob = 2.0f * sinf((float)gp->retraceCount * 0.25f);
            winTexInit(tpl);
            winTexSet(0, (Vec){ROW_X - 24.0f + bob + 2.0f, y - 12.0f, 0.0f}, (Vec){1.0f, 1.0f, 1.0f},
                      (GXColor){0, 0, 0, 0x80});
            winTexSet(0, (Vec){ROW_X - 24.0f + bob, y - 10.0f, 0.0f}, (Vec){1.0f, 1.0f, 1.0f},
                      (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        } else if (i == E.row) {
            /* (the file menu's hand only while its pictures are loaded: else the game's own list hand) */
            portPartyCursor(ROW_X - 22.0f, y - 10.0f, 0.8f);
        }
    }
    portRandSetState(seed);
}

/* the pointer over a choice: it gets the cursor; a click is A */
static int pointer(void) {
    float gx, gy;
    int i;
    const int kind = portMenuPoint(&gx, &gy);
    if (kind == 0 || gx < WIN_X || gx > WIN_X + WIN_W) {
        return 0;
    }
    for (i = 0; i < E.count; i++) {
        const float y = ROW_Y - i * ROW_STEP;
        if (gy <= y + 4.0f && gy > y - ROW_STEP + 4.0f) {
            if (E.row != i) {
                E.row = i;
                psndSFXOn(SFX_MOVE);
            }
            return kind == 1 ? PAD_BUTTON_A : 0;
        }
    }
    return 0;
}
#endif

void portOnlineEndFrame(void) {
    unsigned trg, dir;
    if (E.why == PORT_END_NONE) {
        /* the online service never reached (any of the ways in): asked once per failure, not from the
           Start menu's page (it has its own Try again and Back) */
        const int failed = portStateSyncPhase() == PORT_SYNC_FAILED && !strcmp(portStateSyncNote(), PORT_SYNC_NO_SERVICE);
        if (failed && !E.noServiceSeen) {
            E.noServiceSeen = 1;
            open_(PORT_END_NO_SERVICE, NULL, -1);
        } else if (!failed) {
            E.noServiceSeen = 0;
        }
        if (E.why == PORT_END_NONE) {
            return;
        }
    }
    if (E.why == PORT_END_NO_SERVICE && portStateSyncPhase() != PORT_SYNC_FAILED) {
        close_(); /* (gone by itself: the Start menu's Back or Try again) */
        return;
    }
    if (!E.shown) {
        if (!calm()) {
            return;
        }
        E.shown = 1;
        E.at = 0;
    }
    E.at++;
    if (seqGetSeq() == SEQ_GAME && marioChkKey()) {
        marioKeyOff(); /* (nothing goes on in the world meanwhile) */
    }
#ifdef TTYD_BACKEND_AURORA
    dispEntry(CAMERA_2D, 960.0f, 1, disp, NULL);
    portMenuMark(1); /* B backs out on every controller (port/include/port/menu_buttons.h) */
    trg = keyGetButtonTrg(0);
    dir = keyGetDirRep(0);
    if (trg == 0 && dir == 0) {
        trg = (unsigned)pointer();
    }
#else
    trg = dir = 0;
#endif
    {
        /* tests: the choice by name ($TTYD_SYNC_END_PICK), after a second; headless always chooses (nobody
           there to press); Aurora only when asked (the auto-text's A stays off it, story_test_aurora.c) */
        const char* want = getenv("TTYD_SYNC_END_PICK");
#ifdef TTYD_BACKEND_AURORA
        const int autoPick = want != NULL;
#else
        const int autoPick = 1;
#endif
        if (autoPick && E.at == 60) {
            int i, pick = E.count - 1;
            for (i = 0; want != NULL && i < E.count; i++) {
                const int r = E.rows[i];
                if ((r == R_OWN && !strcmp(want, "own")) || (r == R_TITLE && !strcmp(want, "title")) ||
                    (r == R_RETRY && !strcmp(want, "retry")) || (r == R_BACK && !strcmp(want, "back"))) {
                    pick = i;
                }
            }
            choose(E.rows[pick]);
            return;
        }
    }
    if (dir & DEMO_STICK_UP) {
        E.row = (E.row + E.count - 1) % E.count;
        psndSFXOn(SFX_MOVE);
    } else if (dir & DEMO_STICK_DOWN) {
        E.row = (E.row + 1) % E.count;
        psndSFXOn(SFX_MOVE);
    } else if (trg & PAD_BUTTON_A) {
        psndSFXOn(SFX_OK);
        choose(E.rows[E.row]);
    } else if (trg & PAD_BUTTON_B) {
        int i;
        for (i = 0; i < E.count; i++) { /* B (Esc, the touch controls' B, a right click): its Back row, if it has one */
            if (E.rows[i] == R_BACK) {
                psndSFXOn(SFX_OK);
                choose(R_BACK);
                break;
            }
        }
    }
}
