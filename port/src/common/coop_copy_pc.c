/* The guest's co-op save copy, offered in the game's file menu (port/include/port/together.h).
 *
 * A co-op guest's game keeps a copy of the host's adventure as the host's game saves (the co-op
 * engine's coop_save_pc.c; portCoopCopyPending, portCoopCopyDone). On the next launch the launcher's
 * Play tab offers it, and so does the file menu here, over the four files: "Keep your co-op save
 * with <host>?" Yes lists the files: a free one takes it (as the file select's Copy writes one, a file
 * of its own: portCardPutImage); a file in use isn't offered here (its records go with its number:
 * the launcher's Play tab can replace one, after asking). "Not now" asks again at the next launch;
 * "Don't ask again" lets it go. The menu takes no input while the offer is up.
 *
 * Drawn the way the file menu draws itself (its white window and black letters, its file windows and
 * hand), in the middle of the picture over the menu, dimmed. */
#include "port/file_pc.h"
#include "port/achievements.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/fadedrv.h"
#include "manager/cardmgr.h"
#include "manager/fontmgr.h"
#include "pmario_sound.h"
#include "mario/mariost.h"
#include "port/cardsave_pc.h"
#include "port/menu_tap.h"
#include "port/platform.h"
#include "port/save_files.h"
#include "port/text_fit.h"
#include "port/together.h"
#include "port/tr.h"
#include <demo/DEMOPad.h>
#include <dolphin/card.h>
#include <dolphin/pad.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TTYD_BACKEND_AURORA
#include "driver/windowdrv.h"
#include "port/party.h"
#include "sdk/texPalette.h"
#include "win/win_main.h"
#include <math.h>
void* portLoadMenuTpl(void); /* seq_load.c: the file menu's pictures */
#endif

BOOL portCardPutImage(s32 slot, const void* image); /* cardmgr.c */

#define SFX_MOVE ((const char*)9)
#define SFX_OK ((const char*)0x12)
#define SFX_NO ((const char*)0x13)
#define CHECK_FRAMES 600 /* while not up: a look for a copy at most this often */
#define DONE_FRAMES 120  /* the answer shown before it closes */

enum { S_OFF, S_ASK, S_FILES, S_WRITING, S_DONE };
enum { ASK_YES, ASK_LATER, ASK_NEVER, ASK_ROWS };

static struct {
    int state;
    int row;
    int slot;         /* being written */
    int later;        /* "Not now": until the next launch */
    int never;        /* taken or declined: not again this run */
    unsigned checkAt; /* frames until the next look */
    int doneT;
    const char* answer; /* English */
    char host[64];      /* the host's name, in the game's encoding */
    uint8_t image[PORT_CARD_SLOT_SIZE];
} S;

void portCoopCopyLater(void) {
    S.later = 1;
}

int portCoopCopyLaterAsked(void) {
    return S.later;
}

int portCoopCopyOfferUp(void) {
    return S.state != S_OFF;
}

static void offerClose(const char* why) {
    printf("[together] co-op copy offer closed (%s)\n", why);
    fflush(stdout);
    S.state = S_OFF;
    S.answer = NULL;
}

/* the copy taken or declined for good (the engine marks it; not offered again) */
static void copyDone(void) {
    portCoopCopyDone();
    printf("[together] the co-op copy is taken (or declined)\n");
    fflush(stdout);
}

/* tests: $TTYD_COOP_COPY=<a slot image (0x4000 bytes) or a .gci (its first file)> kept as a co-op guest's copy
   would be (portCoopCopyWrite, the engine's own path), the host's name $TTYD_COOP_COPY_HOST (else "Host"); once */
static void testCopy(void) {
    static int sDone;
    const char* path = getenv("TTYD_COOP_COPY");
    const char* host = getenv("TTYD_COOP_COPY_HOST");
    FILE* f;
    long size;
    size_t n;
    if (sDone || path == NULL) {
        return;
    }
    sDone = 1;
    if ((f = portFopen(path, "rb")) == NULL) {
        printf("[together] test co-op copy: can't read %s\n", path);
        fflush(stdout);
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, size > (long)PORT_CARD_SLOT_SIZE ? 0x40 + (long)PORT_CARD_META_SIZE : 0, SEEK_SET);
    n = fread(S.image, 1, PORT_CARD_SLOT_SIZE, f);
    fclose(f);
    printf("[together] test co-op copy from %s: %s\n", path,
           portCoopCopyWrite(host != NULL ? host : "Host", "TEST", S.image, n) ? "kept" : "not a save");
    fflush(stdout);
}

/* a look for a copy not taken yet (the file menu up, the card read) */
static int look(void) {
    char name[96];
    if (S.later || S.never || cardGetFilePtr() == NULL) {
        return 0;
    }
    testCopy();
    if (S.checkAt > 0) {
        S.checkAt--;
        return 0;
    }
    S.checkAt = CHECK_FRAMES;
    if (!portCoopCopyPending(name, sizeof(name), S.image)) {
        return 0;
    }
    portTextForGame(name[0] != '\0' ? name : "?", S.host, sizeof(S.host));
    S.state = S_ASK;
    S.row = ASK_YES;
    S.answer = NULL;
    printf("[together] co-op copy offered (the host: %s)\n", name);
    fflush(stdout);
    return 1;
}

#ifdef TTYD_BACKEND_AURORA
/* file i is free (an empty file, not a broken one) */
static int slotFree(int i) {
    const CardData* card = cardGetFilePtr();
    return card != NULL &&
           (card->saves[i].data.flags & (CARD_SAVE_FLAG_EMPTY | CARD_SAVE_FLAG_CORRUPT)) == CARD_SAVE_FLAG_EMPTY;
}

static int anyFree(void) {
    int i;
    for (i = 0; i < 4; i++) {
        if (slotFree(i)) {
            return 1;
        }
    }
    return 0;
}

#define WIN_W 440.0f
#define WIN_X (-WIN_W / 2.0f)
#define WIN_Y 136.0f
#define WIN_H 262.0f
#define ROW_X (WIN_X + 96.0f)
#define ROW_Y (WIN_Y - 128.0f)
#define ROW_STEP 26.0f

static const char* const kAsk[ASK_ROWS] = {"Choose a file", "Not now", "Don't ask again"};

static int rows(void) {
    return S.state == S_ASK ? ASK_ROWS : S.state == S_FILES ? 5 : 0;
}

static void rowText(int i, char* out, size_t size) {
    if (S.state == S_ASK) {
        snprintf(out, size, "%s", portTrGame(kAsk[i]));
    } else if (i == 4) {
        snprintf(out, size, "%s", portTrGame("Back"));
    } else {
        snprintf(out, size, portTrGame(slotFree(i) ? "File %d: empty" : "File %d: in use"), i + 1);
    }
}

/* one line in the file menu's black, its top at y, squeezed to fit w; centre: x is its middle */
static void textFit(float x, float y, float w, float scale, GXColor c, const char* text, int centre) {
    const float full = (float)FontGetMessageWidth(text) * scale;
    float sx = scale;
    if (full > w && full > 0.0f) {
        portTextFit("coop-copy", text, full, w, 1);
        sx = scale * w / full;
    }
    if (centre) {
        x -= (full > w ? w : full) / 2.0f;
    }
    winFontInit();
    winFontSet((Vec){x, y, 0.0f}, (Vec){sx, portTextFitHeight(sx, scale), 1.0f}, c, "%s", text);
}

/* Drawn as the file menu draws its own (seq_load.c's loadDraw): its white window with the black letters,
 * each choice on a file window of its own (the menu's picture 0xE, pale where it can't be taken), and its
 * hand at the choice */
static void disp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    const GXColor black = {0x20, 0x20, 0x20, 0xFF}, grey = {0x80, 0x80, 0x80, 0xFF}, red = {0xB0, 0x30, 0x20, 0xFF};
    TEXPalette* tpl = (TEXPalette*)portLoadMenuTpl();
    char text[160];
    int i;
    (void)camId;
    (void)param;
    if (S.state == S_OFF) {
        return;
    }
    /* the menu behind, dimmed: this is the question now */
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x70}, -400.0f, 260.0f, 800.0f, 520.0f, 4.0f);
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x50}, WIN_X + 5.0f, WIN_Y - 5.0f, WIN_W, WIN_H, 20.0f);
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xFF}, WIN_X, WIN_Y, WIN_W, WIN_H, 20.0f);
    snprintf(text, sizeof(text), portTrGame("Keep your co-op save with %s?"), S.host);
    textFit(0.0f, WIN_Y - 16.0f, WIN_W - 48.0f, 0.85f, black, text, 1);
    portPartyText(WIN_X + 28.0f, WIN_Y - 50.0f, 0.55f, 0x505050FFu, WIN_W - 56.0f,
                  portTrGame("Your adventure together, as the host's game last kept it: put it into one of your files "
                             "to play on alone."));
    for (i = 0; i < rows(); i++) {
        const int on = i == S.row;
        const int grey_ = S.state == S_FILES && i < 4 && !slotFree(i);
        const float y = ROW_Y - (float)i * ROW_STEP;
        rowText(i, text, sizeof(text));
        if (tpl != NULL && S.state == S_FILES && i < 4) {
            /* (the file window, small: a strip under its row) */
            winTexInit(tpl);
            winTexSet(0xE, (Vec){ROW_X + 104.0f, y - 9.0f, 0.0f}, (Vec){0.9f, 0.22f, 1.0f},
                      on ? (GXColor){0xFF, 0xFF, 0xFF, 0xFE} : (GXColor){0xA0, 0xA0, 0xA0, 0xFE});
        }
        textFit(ROW_X, y, WIN_W - 140.0f, 0.65f, grey_ ? grey : black, text, 0);
        if (on && tpl != NULL) {
            const float bob = 2.0f * sinf((float)gp->retraceCount * 0.25f);
            winTexInit(tpl);
            winTexSet(0, (Vec){ROW_X - 24.0f + bob + 2.0f, y - 12.0f, 0.0f}, (Vec){1.0f, 1.0f, 1.0f},
                      (GXColor){0, 0, 0, 0x80});
            winTexSet(0, (Vec){ROW_X - 24.0f + bob, y - 10.0f, 0.0f}, (Vec){1.0f, 1.0f, 1.0f},
                      (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        } else if (on) {
            portPartyCursor(ROW_X - 22.0f, y - 10.0f, 0.8f); /* (the game's list hand: its font has no ">") */
        }
    }
    if (S.answer != NULL) {
        snprintf(text, sizeof(text), portTrGame(S.answer), S.slot + 1);
        textFit(0.0f, WIN_Y - WIN_H + 30.0f, WIN_W - 48.0f, 0.55f, red, text, 1);
    }
    portRandSetState(seed);
}

/* the pointer over a row: it gets the cursor; a click is A */
static int pointer(void) {
    float gx, gy;
    int i;
    const int kind = portMenuPoint(&gx, &gy);
    if (kind == 0 || gx < WIN_X || gx > WIN_X + WIN_W) {
        return 0;
    }
    for (i = 0; i < rows(); i++) {
        const float y = ROW_Y - i * ROW_STEP;
        if (gy <= y + 4.0f && gy > y - ROW_STEP + 4.0f) {
            if (S.row != i) {
                S.row = i;
                psndSFXOn(SFX_MOVE);
            }
            return kind == 1 ? PAD_BUTTON_A : 0;
        }
    }
    return 0;
}
#endif

int portCoopCopyOffer(unsigned trg, unsigned dir) {
    if (S.state == S_OFF && !look()) {
        return 0;
    }
#ifdef TTYD_BACKEND_AURORA
    dispEntry(CAMERA_2D, 960.0f, 1, disp, NULL);
    if (S.state == S_WRITING) {
        if (cardIsExec()) {
            return 1;
        }
        if (cardGetCode() == CARD_RESULT_READY && !slotFree(S.slot)) {
            portAchievementsCoopCopyKept(S.slot); /* (its achievements go with it: port/include/port/achievements.h) */
            copyDone();
            S.never = 1;
            S.answer = "Kept in File %d.";
            psndSFXOn(SFX_OK);
            printf("[together] co-op copy kept in file %d\n", S.slot + 1);
        } else {
            S.answer = "Couldn't write the memory card.";
            psndSFXOn(SFX_NO);
            printf("[together] co-op copy: the card write failed (%d)\n", (int)cardGetCode());
        }
        fflush(stdout);
        S.state = S_DONE;
        S.doneT = 0;
        return 1;
    }
    if (S.state == S_DONE) {
        if (++S.doneT > DONE_FRAMES || (trg & (PAD_BUTTON_A | PAD_BUTTON_B))) {
            offerClose("done");
        }
        return 1;
    }
    if (cardIsExec()) {
        return 1;
    }
    if (trg == 0 && dir == 0) {
        trg = (unsigned)pointer();
    }
    if (dir & DEMO_STICK_UP) {
        S.row = (S.row + rows() - 1) % rows();
        S.answer = NULL;
        psndSFXOn(SFX_MOVE);
    } else if (dir & DEMO_STICK_DOWN) {
        S.row = (S.row + 1) % rows();
        S.answer = NULL;
        psndSFXOn(SFX_MOVE);
    } else if (trg & PAD_BUTTON_B) {
        psndSFXOn(SFX_NO);
        if (S.state == S_FILES) {
            S.state = S_ASK;
            S.row = ASK_YES;
            S.answer = NULL;
        } else {
            S.later = 1;
            offerClose("B: not now");
        }
    } else if (trg & PAD_BUTTON_A) {
        if (S.state == S_ASK) {
            if (S.row == ASK_YES) {
                if (!anyFree()) {
                    psndSFXOn(SFX_NO);
                    S.answer = "No free file: delete one first, or put it in from the launcher's Play tab.";
                    return 1;
                }
                psndSFXOn(SFX_OK);
                S.state = S_FILES;
                for (S.row = 0; S.row < 4 && !slotFree(S.row); S.row++) {
                }
                S.answer = NULL;
            } else if (S.row == ASK_LATER) {
                psndSFXOn(SFX_OK);
                S.later = 1;
                offerClose("not now");
            } else {
                psndSFXOn(SFX_OK);
                copyDone();
                S.never = 1;
                offerClose("don't ask again");
            }
        } else if (S.row == 4) {
            psndSFXOn(SFX_NO);
            S.state = S_ASK;
            S.row = ASK_YES;
            S.answer = NULL;
        } else if (!slotFree(S.row)) {
            /* (never over a save without asking: here only the free files) */
            psndSFXOn(SFX_NO);
            S.answer = "That file is in use: choose an empty one.";
        } else if (portCardPutImage(S.row, S.image)) {
            psndSFXOn(SFX_OK);
            S.slot = S.row;
            S.state = S_WRITING;
            S.answer = NULL;
        } else {
            psndSFXOn(SFX_NO);
            S.answer = "Couldn't write the memory card.";
        }
    }
    return 1;
#else
    (void)trg;
    (void)dir;
    S.later = 1; /* (no menus to show it in: asked again at the next launch) */
    offerClose("no menus here");
    return 0;
#endif
}
