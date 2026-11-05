/* The party modes' shared game side (port/include/port/party.h): one hook on
 * marioStMain runs each mode's frame, the pad hook hands the controllers to
 * a party game that's up, and a small generator of their own (the game's
 * random numbers are never touched, so drawing and dealing change nothing in
 * the game). Nothing happens until a mode is started. */
#include "port/party.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/platform.h"
#include "battle/battle_monosiri.h"
#include "driver/msgdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "driver/windowdrv.h"
#include "evt/evt_story.h"
#include "manager/fontmgr.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "win/win_main.h"
#include <dolphin/gx.h>
#include <dolphin/pad.h>

#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

unsigned long long portPartyHash(const char* text) {
    unsigned long long h = 1469598103934665603ull;
    if (text == NULL) {
        return h;
    }
    while (*text != '\0') {
        h ^= (unsigned char)*text++;
        h *= 1099511628211ull;
    }
    return h;
}

/* splitmix64 */
unsigned portPartyNext(PortPartyRand* r) {
    unsigned long long z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (unsigned)((z ^ (z >> 31)) >> 32);
}

int portPartyBelow(PortPartyRand* r, int n) {
    return n <= 1 ? 0 : (int)(portPartyNext(r) % (unsigned)n);
}

int portPartyFieldReady(void) {
    return seqGetSeq() == SEQ_GAME && gp != NULL && !gp->inBattle && marioStGetSystemLevel() == 0 &&
           marioChkKey() && marioChkCtrl();
}

/* --- drawing --------------------------------------------------------------- */

void portPartyWindow(float x, float y, float w, float h) {
    windowDispGX_System(0, 0xFF, x, y, w, h);
}

float portPartyTextWidth(const char* text, float scale) {
    return (float)FontGetMessageWidth(text) * scale;
}

static GXColor colour(unsigned rgba) {
    GXColor c;
    c.r = (u8)(rgba >> 24);
    c.g = (u8)(rgba >> 16);
    c.b = (u8)(rgba >> 8);
    c.a = (u8)rgba;
    return c;
}

static int sMeasure; /* portPartyTextAbove: count the lines, draw nothing */

int portPartyTextAbove(float x, float y, float scale, unsigned rgba, float width, const char* text) {
    int lines;
    sMeasure = 1;
    lines = portPartyText(x, y, scale, rgba, width, text);
    sMeasure = 0;
    return portPartyText(x, y + (float)(lines > 0 ? lines - 1 : 0) * 24.0f * scale, scale, rgba, width, text);
}

int portPartyText(float x, float y, float scale, unsigned rgba, float width, const char* text) {
    char line[256];
    const char* p = text;
    int lines = 0;
    if (!sMeasure) {
        winFontInit();
    }
    while (p != NULL && *p != '\0' && lines < 12) {
        size_t len = strlen(p), fit = len, k;
        if (len >= sizeof(line)) {
            len = fit = sizeof(line) - 1;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        if (width > 0.0f && portPartyTextWidth(line, scale) > width) {
            fit = 0;
            for (k = 1; k <= len; k++) {
                if (p[k] == ' ' || p[k] == '\0') {
                    line[k] = '\0';
                    if (portPartyTextWidth(line, scale) <= width) {
                        fit = k;
                    }
                    memcpy(line, p, len);
                    line[len] = '\0';
                }
            }
            if (fit == 0) {
                fit = len;
            }
            line[fit] = '\0';
        }
        if (!sMeasure) {
            winFontSet((Vec){x, y - (float)lines * 24.0f * scale, 0.0f}, (Vec){scale, scale, scale}, colour(rgba),
                       "%s", line);
        }
        lines++;
        p += fit;
        while (*p == ' ') {
            p++;
        }
    }
    return lines;
}

/* --- the controllers --------------------------------------------------------- */

static unsigned short sPressed[PAD_MAX_CONTROLLERS], sHeld[PAD_MAX_CONTROLLERS];
static unsigned sConnected;

unsigned short portPartyPressed(int pad) {
    return pad >= 0 && pad < PAD_MAX_CONTROLLERS ? sPressed[pad] : 0;
}

unsigned portPartyConnected(void) {
    return sConnected;
}

static int padsTaken(void) {
    return portQuizPad() || portCardsPad();
}

void portPartyPad(struct PADStatus* pads) {
    int i;
    if (!padsTaken()) {
        for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
            sHeld[i] = pads[i].button; /* what's held as a game starts doesn't count until let go */
            sPressed[i] = 0;
        }
        return;
    }
    sConnected = 0;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        unsigned short now = 0;
        if (pads[i].err == PAD_ERR_NONE) {
            sConnected |= 1u << i;
            now = pads[i].button;
            if (pads[i].stickY > 48) {
                now |= PAD_BUTTON_UP;
            } else if (pads[i].stickY < -48) {
                now |= PAD_BUTTON_DOWN;
            }
            if (pads[i].stickX > 48) {
                now |= PAD_BUTTON_RIGHT;
            } else if (pads[i].stickX < -48) {
                now |= PAD_BUTTON_LEFT;
            }
        }
        sPressed[i] = (unsigned short)(now & ~sHeld[i]);
        sHeld[i] = now;
        /* the game gets nothing while a party game is up */
        pads[i].button = 0;
        pads[i].stickX = pads[i].stickY = pads[i].substickX = pads[i].substickY = 0;
        pads[i].triggerLeft = pads[i].triggerRight = pads[i].analogA = pads[i].analogB = 0;
#ifdef TARGET_PC
        pads[i].extButton = 0;
#endif
    }
}

/* --- the enemies --------------------------------------------------------------- */

const PortCardStats* portPartyStats(int unit) {
    int lo = 0, hi = gPortCardStatsCount - 1;
    while (lo <= hi) { /* sorted by kind */
        const int mid = (lo + hi) / 2;
        if (gPortCardStats[mid].unit == unit) {
            return &gPortCardStats[mid];
        }
        if (gPortCardStats[mid].unit < unit) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

int portPartyEnemyListed(int unit) {
    const TattleEntry* t;
    if (unit <= 0 || unit >= PORT_PARTY_UNITS) {
        return 0;
    }
    /* as the Journal lists them (win_log.c): the second forms of a few are the same entry */
    if (unit == UNIT_BELDAM_CHAPTER2 || unit == UNIT_MARILYN_CHAPTER2 || unit == UNIT_DOOPLISS_CHAPTER4_FIGHT1 ||
        unit == UNIT_BOWSER_CHAPTER3) {
        return 0;
    }
    t = battleGetUnitMonosiriPtr(unit);
    return t != NULL && t->tattleLogTag != NULL && t->enemyTag != NULL && portPartyStats(unit) != NULL;
}

int portPartyEnemyTattled(int unit) {
    return gp != NULL && portPartyEnemyListed(unit) && swGet(unit + GSWF_TATTLE_BASE) != 0;
}

const char* portPartyEnemyName(int unit) {
    const TattleEntry* t;
    const char* text;
    if (unit <= 0 || unit >= PORT_PARTY_UNITS) {
        return "";
    }
    t = battleGetUnitMonosiriPtr(unit);
    if (t == NULL || t->enemyTag == NULL) {
        return "";
    }
    text = msgSearch(t->enemyTag);
    return text != NULL ? text : "";
}

/* --- the frame ------------------------------------------------------------------ */

static void frameHook(void) {
    portBingoFrame();
    portQuizFrame();
    portCardsFrame();
    portDailyFrame();
}

void portPartyInit(void) {
    static int sDone;
    if (sDone) {
        return;
    }
    sDone = 1;
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
}
