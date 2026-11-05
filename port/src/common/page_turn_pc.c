/* Page-turn transitions (port/include/port/toggles.h): a room change turns
 * the old room away like a page instead of fading to black. The game has the
 * page animations on every disc, made for this: the fade driver's page flip
 * (fadedrv.c FADE_PAGE_FLIP_Z1 / _A1: the screen captured as a page, held
 * while the next room loads, then turned away over it, with the "map change"
 * page sound), which only the E3 demo's menu ever used. This chooses it for
 * the plain fades and the pipes, and its animation file: OFF_d_meku_right (a
 * pop-up book's page), OFF_d_meku (a page flipped over) or OFF_d_ori (folded
 * like paper); all three have the Z_1 and A_1 the flip plays.
 *
 * Scripted fades (curtains, chapter cards, the story's interludes) stay the
 * game's own. A room change that turned a page but arrives with a fade of its
 * own lets the page go at once (fadeReset). The page takes the animation's
 * time, not the fade's: it changes play's timing, so online it is the host's
 * setting, as every one that changes play. */
#include "port/toggles.h"
#include "port/settings.h"
#include "driver/fadedrv.h"
#include <stdio.h>

static int sTurning;         /* a page was turned leaving the room: the arrival turns it away */
static const char* sFile;    /* its animation (the arrival's must be the same: the page is its pose) */
static int sOurs;            /* the next page flip fadeEntry starts is ours */

static int setting(void) {
    static int* sStyle;
    if (sStyle == NULL) {
        sStyle = portSettingsDeclare("page_turn", 0, 0, 3);
    }
    return portConsoleAccurate() ? 0 : *sStyle;
}

static int isPipe(int type, int second) {
    switch (type) {
        case FADE_PIPE_UP_A1:
        case FADE_PIPE_DOWN_A1:
        case FADE_PIPE_LEFT_A1:
        case FADE_PIPE_RIGHT_A1:
            return !second;
        case FADE_PIPE_UP_A2:
        case FADE_PIPE_DOWN_A2:
        case FADE_PIPE_LEFT_A2:
        case FADE_PIPE_RIGHT_A2:
            return second;
        default:
            return 0;
    }
}

int portPageTurnLeave(int type) {
    sTurning = 0;
    if (setting() == 0 || (type != FADE_OUT_BLACK && !isPipe(type, 0))) {
        return type;
    }
    static const char* const kFiles[] = {"OFF_d_meku", "OFF_d_meku_right", "OFF_d_meku", "OFF_d_ori"};
    sTurning = 1;
    sOurs = 1;
    sFile = kFiles[setting()];
    printf("[page_turn] leaving with a page (fade %d)\n", type);
    fflush(stdout);
    return FADE_PAGE_FLIP_Z1;
}

int portPageTurnArrive(int type) {
    if (!sTurning) {
        return type;
    }
    sTurning = 0;
    if (type == FADE_IN_BLACK || isPipe(type, 1)) {
        sOurs = 1;
        return FADE_PAGE_FLIP_A1;
    }
    fadeReset(3); /* the room's own arrival: the held page goes */
    return type;
}

const char* portPageTurnFile(const char* file) {
    if (!sOurs || sFile == NULL) {
        return file; /* (the E3 demo's own) */
    }
    sOurs = 0;
    return sFile;
}
