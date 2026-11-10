// Status window: the field HUD (HP, FP, star power gauge, coins, star points) and its show/hide logic.
#include "port_hook.h"
#include "icon_ids.h"
#include "statuswindow.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/dispdrv.h"
#include "driver/icondrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "memory.h"
#include "pmario_sound.h"
#include <string.h>
#ifdef PORT_MODS
#include "port/hooks/src_statuswindow.h"
#endif
#ifdef TARGET_PC
#include "port/platform.h"
#include "port/hud.h"
#include "port/qol.h"
int portStateSyncSceneShown(void); /* port/statesync.h */
int portStateSyncCoopGuest(void);
int portStateSyncWatcher(void);
int portStateSyncCoopPart(int player);
int portStateSyncMe(void);
#endif
#include "version.h"
#include "evt/evt_story.h"
#include <dolphin/pad.h>
#include "libc_decls.h"

typedef StatusValue SVal;

typedef StatusWindowWork SWork;

typedef struct alwaysDtEntry {
    s16 xpos;   // 0x0
    s16 ypos;   // 0x2
    u16 iconId; // 0x4
    f32 scale;  // 0x8
} alwaysDtEntry;

#ifdef TARGET_PC
#include "wide_x.h"
/* PC port, widescreen HUD: a part's x (left-based xpos) moved to its edge; HUD_SP_X the Star Power row's
 * (its star, banner and gauge: with the right group when spread) */
#define HUD_X(x, xpos) ((x) + WIDE_HUD_DX(xpos))
#define HUD_ABS(v) ((v) + WIDE_HUD_DX(0))
#define HUD_SP_X(x) ((x) + WIDE_HUD_SP_DX())
#define HUD_SP_PART(e) ((e)->iconId == ICON_1D2 || (e)->iconId == ICON_1B2)
#else
#define HUD_X(x, xpos) (x)
#define HUD_ABS(v) (v)
#define HUD_SP_X(x) (x)
#endif

//.data
alwaysDtEntry alwaysDt[] = {
    { 108, 40, ICON_1D0, 1.0f }, // red hp banner
    { 38, 40, ICON_1A3, 1.0f },  // mario hp icon
    { 124, 34, ICON_1D8, 1.0f }, // division sign
    { 288, 40, ICON_1D1, 1.0f }, // orange fp banner
    { 220, 40, ICON_1B1, 1.0f }, // fp icon
    { 305, 34, ICON_1D8, 1.0f }, // division sign
    { 492, 40, ICON_1D3, 1.0f }, // green item banner
    { 400, 40, ICON_1B3, 1.0f }, // star points
    { 490, 40, ICON_1B4, 1.0f }, // coin icon
    { 422, 34, ICON_1D6, 1.0f }, // "x"
    { 512, 34, ICON_1D6, 1.0f }, // "x"
    { 360, 76, ICON_1D2, 1.0f }, // blue sp banner
    { 220, 80, ICON_1B2, 0.9f }  // star
};

//.sdata
alwaysDtEntry partysDt[] = {
    { 108, 76, ICON_1D0, 1.0f },
    { 38, 80, ICON_1A4, 1.0f },
    { 124, 70, ICON_1D8, 1.0f },
};
u16 party_icon[8] = {ICON_1A4, ICON_1A4, ICON_1A5, ICON_1A6, ICON_1A7, ICON_1AE, ICON_1AF, ICON_1B0};
u16 gauge_back[8] = {ICON_1B7, ICON_1B8, ICON_1B9, ICON_1BA, ICON_1BB, ICON_1BC, ICON_1BD, ICON_1BE};
u16 gauge_wakka[16] = { ICON_1C0, ICON_1C1, ICON_1C2, ICON_1C3, ICON_1C4, ICON_1C5, ICON_1C6, ICON_1C7,
                        ICON_1C8, ICON_1C9, ICON_1CA, ICON_1CB, ICON_1CC, ICON_1CD, ICON_1CE, ICON_1CF };
#if VERSION == VERSION_US || VERSION == VERSION_EU
// US and PAL: pause-menu icons per language (the 2004-09 demos draw no menu hints, so they lack these)
s16 party_icon_tbl[6] = {0x215, 0x22B, 0x22C, 0x22D, 0x22E, 0x22F};
s16 gear_icon_tbl[6] = {0x216, 0x230, 0x231, 0x232, 0x233, 0x234};
s16 badge_icon_tbl[6] = {0x217, 0x235, 0x236, 0x235, 0x237, 0x238};
s16 journal_icon_tbl[6] = {0x218, 0x239, 0x239, 0x239, 0x23A, 0x23B};
#endif

//.sbss
static SWork* wp;

// The demo draws the status window icons with icon flag 0x4, retail with 0x10.
#if VERSION <= VERSION_DEMO_JP
#define SW_ICON_FLAGS 0x4
#else
#define SW_ICON_FLAGS 0x10
#endif

static void statusWinDisp(CameraId camId, void* param);
#ifdef TARGET_PC
static BOOL portOtherHpFpOnly(void);
#endif
static void gaugeDisp(s32 sp, f32 x, f32 y);
static void statusGetValue(SVal* values);
static BOOL valueCheck(void);
static void valueUpdate(void);

#ifdef TARGET_PC
/* PC port: a partner new to the HUD's row (one just joined, the first after a room's start, another swapped
 * in): its HP and max shown as they are, never counted up from the last one's (or from 0) with the change's
 * blink after, which left the row reading "/10", the face and the number blinked away, for seconds after a
 * join. TRUE when it was new (cur.party set here too).
 * The count-up it skips still runs unseen (sPartyRowCounting: the game's numbers, stepped as valueUpdate stepped
 * them): until it ends the row counts as a change, as it did, so the HUD comes down and stays when and as long
 * as the game's did (at a room's start, a join or a swap; one seen while Mario had no keys opens it once he
 * has them). */
static BOOL sPartyRowCounting;
static s32 sPartyRowHp, sPartyRowMaxHp;

static BOOL portPartyRowNew(const SVal* v) {
    if (!v->party || (wp->cur.party == v->party && wp->cur.partyMaxHp != 0)) {
        return FALSE;
    }
    sPartyRowHp = wp->cur.partyHp;
    sPartyRowMaxHp = wp->cur.partyMaxHp;
    sPartyRowCounting = sPartyRowHp != v->partyHp || sPartyRowMaxHp != v->partyMaxHp;
    wp->cur.party = v->party;
    wp->cur.partyHp = v->partyHp;
    wp->cur.partyMaxHp = v->partyMaxHp;
    wp->pendingBlink &= ~8;
    wp->blink &= ~8;
    return TRUE;
}

/* PC port: the D-pad's menu shortcuts (Party, Gear, Badges, Journal) under the HUD are Mario's player's: never on
 * the screen of an online game whose player isn't Mario (a co-op guest playing the partner or a companion, whose
 * menu is its own on Start; a watcher), where its HUD opens with the host's idle Mario */
static BOOL portMenuHintsNotOurs(void) {
    if (portStateSyncWatcher()) {
        return TRUE;
    }
    return portStateSyncCoopGuest() && portStateSyncCoopPart(portStateSyncMe()) != 1 /* PORT_COOP_PART_MARIO */;
}

/* PC port: how far down the screen the HUD's rows reach (port/hud.h portHudStatusRowsBottom): each draw of the
 * bar in the top half (the game's, Brothers mode's Luigi row under it; not versus' at the bottom) notes its
 * lowest row's bottom edge (the 2D units, y up: a row's parts sit on their anchor, y - ypos), in units below the
 * screen's top; statusWinMain hands the last frame's on. */
static f32 sRowsAcc, sRowsShown;
static u32 sRowsAt;
/* the battle's BINGO cards' small row (battle_break_slot.c), moved under the Star Power gauge when the widescreen
 * HUD spreads it over them: its bottom, in units below the screen's top (not the HUD's size: the game's own) */
static f32 sCardsAcc, sCardsShown;

static void portRowsPublish(void) {
    sRowsShown = sRowsAcc;
    sRowsAcc = 0.0f;
    sCardsShown = sCardsAcc;
    sCardsAcc = 0.0f;
    sRowsAt = gp != NULL ? (u32)gp->retraceCount : 0u;
}

void portHudCardsDrawn(float below) {
    if (below > sCardsAcc) {
        sCardsAcc = below;
    }
}

/* the bar's rows alone (no cards), in units below the screen's top at the HUD's size; 0 with no bar */
float portHudStatusBarBottom(void) {
    f32 below;
    if (gp == NULL || (u32)gp->retraceCount - sRowsAt > 3u) {
        return 0.0f;
    }
    below = sRowsShown > sRowsAcc ? sRowsShown : sRowsAcc;
    return below > 0.0f ? below * gPortHudStatusScale : 0.0f;
}

static void portRowsDrawn(f32 y) {
    f32 low;
    if (y <= 0.0f || (wp->flags & STATUSWIN_FLAG_DISP_OFF)) {
        return; /* (the bottom half's: versus' other side) */
    }
    if (portOtherHpFpOnly()) {
        low = y - 44.0f; /* the HP and FP banners' row alone */
    } else {
        low = y - 44.0f;
        if (wp->cur.party) {
            low = y - 84.0f; /* the partner's row */
        }
        if (pouchGetPtr()->starPowersObtained && low > y - 96.0f) {
            low = y - 96.0f; /* the Star Power gauge, under the FP banner */
        }
    }
    if (240.0f - low > sRowsAcc) {
        sRowsAcc = 240.0f - low;
    }
}

float portHudStatusRowsBottom(void) {
    f32 below, cards;
    if (gp == NULL || (u32)gp->retraceCount - sRowsAt > 3u) {
        return 0.0f; /* (no field or battle frame just now: no bar) */
    }
    below = sRowsShown > sRowsAcc ? sRowsShown : sRowsAcc;
    below = below > 0.0f ? below * gPortHudStatusScale : 0.0f; /* (the HUD size: about the screen's top) */
    cards = sCardsShown > sCardsAcc ? sCardsShown : sCardsAcc;
    below = cards > below ? cards : below; /* (the BINGO cards' row, the bar out or not) */
    if (below <= 0.0f) {
        return 0.0f;
    }
    return below >= 480.0f ? 1.0f : below / 480.0f;
}
#endif

/**
 * Allocates the field HUD state at boot and sets its open
 * and closed positions (it slides down from above the screen), starting open.
 */
void PORT_HOOKABLE(statusWinInit)(void) {
    wp = __memAlloc(HEAP_DEFAULT, sizeof(SWork));
    memset(wp, 0, sizeof(SWork));
    if (!wp) {
    }
    wp->speed = 1.0f;
    wp->openX = -304.0f;
    wp->openY = 208.0f;
    wp->closeX = -304.0f;
    wp->closeY = 334.0f;
    wp->idle = 0;
    wp->forceTimer = 0;
    wp->flags &= ~(STATUSWIN_FLAG_CLOSED | STATUSWIN_FLAG_CLOSE_REQ);
    wp->flags |= STATUSWIN_FLAG_OPEN | STATUSWIN_FLAG_OPEN_REQ;
}

/** Resets the HUD's idle timers on a map change and turns a pending forced open/close into a normal open/close. */
void PORT_HOOKABLE(statusWinReInit)(void) {
    wp->idle = 0;
    wp->forceTimer = 0;
    if (wp->flags & STATUSWIN_FLAG_FORCE_OPEN) {
        wp->flags &= ~STATUSWIN_FLAG_FORCE_OPEN;
        wp->flags &= ~(STATUSWIN_FLAG_CLOSED | STATUSWIN_FLAG_CLOSE_REQ);
        wp->flags |= STATUSWIN_FLAG_OPEN | STATUSWIN_FLAG_OPEN_REQ;
    } else if (wp->flags & STATUSWIN_FLAG_OPEN) {
        wp->flags &= ~(STATUSWIN_FLAG_CLOSED | STATUSWIN_FLAG_CLOSE_REQ);
        wp->flags |= STATUSWIN_FLAG_OPEN | STATUSWIN_FLAG_OPEN_REQ;
    }
    if (wp->flags & STATUSWIN_FLAG_FORCE_CLOSE) {
        wp->flags &= ~STATUSWIN_FLAG_FORCE_CLOSE;
        wp->flags |= STATUSWIN_FLAG_CLOSED | STATUSWIN_FLAG_CLOSE_REQ;
        wp->flags &= ~(STATUSWIN_FLAG_OPEN | STATUSWIN_FLAG_OPEN_REQ);
    } else if (wp->flags & STATUSWIN_FLAG_CLOSED) {
        wp->flags |= STATUSWIN_FLAG_CLOSED | STATUSWIN_FLAG_CLOSE_REQ;
        wp->flags &= ~(STATUSWIN_FLAG_OPEN | STATUSWIN_FLAG_OPEN_REQ);
    }
}

/**
 * Per-frame HUD logic in the field: hidden when Mario is not himself (a transformation or another character)
 * and left alone in the Pit of 100 Trials ("yuu").
 * While Mario can move it opens after he stands still for 10 seconds, closes when he walks,
 * toggles with Z (held for 10 seconds, with a sound) and pops open for 2 seconds when a value changes.
 * Then starts and runs the slide animation (16 frames in, 28 out).
 */
void PORT_HOOKABLE(statusWinMain)(void) {
    MarioWork* mario;
    f32 inv;
    f32 one;
    f32 t;

#ifdef TARGET_PC
    portRowsPublish();
#endif
    one = 1.0f;
    mario = marioGetPtr();
    if (mario->characterId) {
        wp->flags = STATUSWIN_FLAG_CLOSED;
        wp->duration = one;
        wp->timer = one;
        wp->tEnd = 0.0f;
        wp->t = 0.0f;
        return;
    }
    if (!strcmp(gp->currentAreaName, "yuu")) {
        return;
    }
    switch (seqGetSeq()) {
        case SEQ_GAME:
        case SEQ_MAP_CHANGE:
#ifdef TARGET_PC
            /* (online, a co-op guest's Mario has no keys all along: its HUD, the shared party's HP, FP, coins and
               Star Points, closes for the host's scenes only; port/statesync.h) */
            if (portStateSyncSceneShown() || !(mario->flags & MARIO_FLAG_ACTIVE)) {
#else
            if ((marioChkCtrl() ? FALSE : TRUE) || (marioChkKey() ? FALSE : TRUE) || !(mario->flags & MARIO_FLAG_ACTIVE)) {
#endif
                if (statusWinCheck()) {
                    statusWinClose();
                }
                valueCheck();
                wp->idle = 0;
                wp->forceTimer = 0;
            } else {
                if (VECDistance(&wp->prevPos, &mario->position) < 1.0f) {
                    wp->idle++;
                    if (wp->idle > gp->framerate * 10) {
                        wp->idle = gp->framerate * 10;
                        if (!wp->forcedClose && !wp->forceTimer && !statusWinCheck()) {
                            statusWinOpen();
#if VERSION >= VERSION_JP
                            wp->flags |= STATUSWIN_FLAG_MENU_HINTS;
#endif
                        }
                    }
                } else {
                    wp->idle = 0;
                    wp->forcedClose = 0;
                    if (!wp->forceTimer && statusWinCheck()) {
                        statusWinClose();
                    }
                }
                if (mario->buttonTrg & PAD_TRIGGER_Z) {
                    if (!statusWinCheck()) {
                        statusWinOpen();
                        wp->forceTimer = gp->framerate * 10;
                        psndSFXOn((const char*)0x2A);
                        wp->flags |= STATUSWIN_FLAG_MENU_HINTS;
                    } else {
                        statusWinClose();
                        wp->forceTimer = gp->framerate * 10;
                        wp->forcedClose = 1;
                        psndSFXOn((const char*)0x2B);
                    }
                }
                if (valueCheck()) {
                    wp->forceTimer = gp->framerate * 2;
                    if (!statusWinCheck()) {
                        statusWinOpen();
                    }
                }
            }
            if (wp->forceTimer) {
                wp->forceTimer--;
            }
            wp->prevPos = mario->position;
            if (wp->flags & STATUSWIN_FLAG_FORCE_OPEN) {
                wp->flags &= ~(STATUSWIN_FLAG_CLOSED | STATUSWIN_FLAG_CLOSE_REQ);
            }
            if (wp->flags & STATUSWIN_FLAG_FORCE_CLOSE) {
                wp->flags &= ~(STATUSWIN_FLAG_OPEN | STATUSWIN_FLAG_OPEN_REQ);
            }
            break;
        case SEQ_BATTLE:
            break;
        default:
            if (statusWinCheck()) {
                statusWinClose();
            }
            break;
    }
    if (wp->flags & STATUSWIN_FLAG_OPEN_REQ || wp->flags & STATUSWIN_FLAG_CLOSE_REQ || wp->flags & STATUSWIN_FLAG_FORCE_OPEN_REQ || wp->flags & STATUSWIN_FLAG_FORCE_CLOSE_REQ) {
        wp->tStart = wp->t;
        wp->tEnd = (wp->flags & (STATUSWIN_FLAG_OPEN_REQ | STATUSWIN_FLAG_FORCE_OPEN_REQ)) ? 1.0f : 0.0f;
        wp->timer = 0.0f;
        wp->duration = (wp->flags & (STATUSWIN_FLAG_OPEN_REQ | STATUSWIN_FLAG_FORCE_OPEN_REQ)) ? 16.0f / wp->speed : 28.0f / wp->speed;
        wp->flags &= ~STATUSWIN_FLAG_OPEN_REQ;
        wp->flags &= ~STATUSWIN_FLAG_CLOSE_REQ;
        wp->flags &= ~STATUSWIN_FLAG_FORCE_OPEN_REQ;
        wp->flags &= ~STATUSWIN_FLAG_FORCE_CLOSE_REQ;
    }
#if VERSION >= VERSION_US
    wp->timer += 60.0f / gp->framerate;
#else
    wp->timer += one;
#endif
    if (wp->timer >= wp->duration) {
        wp->timer = wp->duration;
        wp->t = wp->tEnd;
        if (wp->t == 0.0f) {
            wp->flags &= ~STATUSWIN_FLAG_MENU_HINTS;
        }
    } else {
        inv = (f32)cos(3.1415927f * wp->timer / wp->duration);
        wp->t = wp->tStart + (wp->tEnd - wp->tStart) * (one - inv) / 2.0f;
    }
    t = wp->t;
    inv = 1.0f - t;
    wp->x = wp->openX * t + wp->closeX * inv;
    wp->y = wp->openY * t + wp->closeY * inv;
    valueUpdate();
    if (!(wp->flags & STATUSWIN_FLAG_DISP_OFF)) {
        dispEntry(CAMERA_2D, 500.0f, 0, statusWinDisp, NULL);
    }
}

// iconDispGx's original prototype takes the scale first (f1 either way); calling it in that
// order gives the target's argument evaluation order.
#ifdef TARGET_PC /* Win64 assigns int/float registers by position: call with the real argument order */
static inline void iconDispGxScaleFirst(f32 scale, Vec* pos, s16 flags, u16 icon) {
    iconDispGx(pos, flags, icon, scale);
}
#else
#define iconDispGxScaleFirst ((void (*)(f32, Vec*, s16, u16))iconDispGx)
#endif

/**
 * Draws the Star Power gauge at (x, y): one 32-pixel slot per crystal star obtained,
 * full slots for each 100 of sp and the partly filled slot in 15 steps.
 */
static void gaugeDisp(s32 sp, f32 x, f32 y) {
    s32 i;
    s32 max;
    s32 full;
    s32 part;

    max = 0;
    for (i = 0; i < sizeof(gauge_back) / sizeof(gauge_back[0]); i++) {
        if (pouchGetPtr()->starPowersObtained & (1 << i)) {
            max += 100;
        }
    }
    if (sp > max) {
        sp = max;
    }
    if (sp < 0) {
        sp = 0;
    }
    full = sp / 100;
    part = sp % 100;
    part = part * 15 / 99;
    if (sp % 100 && sp && !part) {
        part = 1;
    }
    if (part) {
        Vec pos;
        pos.x = x + (f32)(full * 32);
        pos.y = y;
        pos.z = 0.0f;
        iconDispGxScaleFirst(1.0f, &pos, SW_ICON_FLAGS, gauge_wakka[part]);
    }
    for (i = 0; i < sizeof(gauge_back) / sizeof(gauge_back[0]); i++) {
        Vec pos2;
        if (!(pouchGetPtr()->starPowersObtained & (1 << i))) {
            break;
        }
        pos2.x = x + (f32)(i * 32);
        pos2.y = 12.0f + y;
        pos2.z = 0.0f;
        iconDispGxScaleFirst(1.0f, &pos2, SW_ICON_FLAGS, i < full ? gauge_back[i] : ICON_1BF);
    }
}

static inline void
numDisp(f32 x, f32 y, s32 value, BOOL small) {
    Mtx m;
    MTXTrans(m, x, y, 0.0f);
    iconNumberDispGx(m, value, small, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
}

/**
 * Draws the HUD: the HP, FP, Star Power and coin/Star Point banners with their icons and numbers,
 * Mario's and the partner's HP, and the gauge, blinking any value flagged by the stat blink functions.
 */
static void PORT_HOOKABLE(statusWinDisp)(CameraId camId, void* param) {
    alwaysDtEntry* e;
    s32 i;
    s32 blinkOn;
    s32 blink;
    f32 x, y, y2;
    Mtx m, s;
    Mtx m2, s2;

    x = wp->x;
    y = wp->y;
#ifdef TARGET_PC
    portRowsDrawn(y);
#endif
    blinkOn = 1;
    if (wp->blinkTimer > 0) {
#ifdef TARGET_PC
        if (portInterpStepsHere()) { /* a frame between the game's: the same blink */
        portInterpKeep(&wp->blinkTimer, sizeof(wp->blinkTimer)); /* (stereo's first eye: put back after) */
#endif
        wp->blinkTimer--;
#ifdef TARGET_PC
        }
#endif
        blinkOn = (wp->blinkTimer % 16) >= 6;
    }

    e = alwaysDt;
    for (i = 0; i < sizeof(alwaysDt) / sizeof(alwaysDt[0]); i++, e++) {
        if (e->iconId == ICON_1D2 && !pouchGetPtr()->starPowersObtained) {
            break;
        }
#ifdef TARGET_PC
        if (i >= 6 && portOtherHpFpOnly()) { /* (the HP and FP banners alone: Brothers mode's Luigi) */
            break;
        }
#endif
        if (!blinkOn) {
            blink = wp->blink;
            if (blink & 1 && i == 1) {
                continue;
            }
            if (blink & 2 && i == 4) {
                continue;
            }
            if (blink & 0x10 && i == 8) {
                continue;
            }
            if (blink & 4 && i == 12) {
                continue;
            }
        }
#ifdef TARGET_PC
        if (i == 10 && wp->cur.coins > 999) { /* Coins up to 9,999: the fourth digit stands where the "x" was */
            continue;
        }
#endif
#ifdef TARGET_PC
        MTXTrans(m, (HUD_SP_PART(e) ? HUD_SP_X(x) : HUD_X(x, e->xpos)) + (f32)e->xpos, y - (f32)e->ypos, 0.0f);
#else
        MTXTrans(m, HUD_X(x, e->xpos) + (f32)e->xpos, y - (f32)e->ypos, 0.0f);
#endif
        MTXScale(s, e->scale, e->scale, e->scale);
        MTXConcat(m, s, m);
        iconDispGxCol(m, SW_ICON_FLAGS, e->iconId, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    }

    if (!(wp->blink & 1) || blinkOn) {
        numDisp(120.0f + HUD_X(x, 120) - 6.0f - 10.0f, 2.0f + (y - 38.0f), wp->cur.hp, 0);
    }
    numDisp(186.0f + HUD_X(x, 186) - 6.0f - 10.0f, 3.0f + (y - 38.0f), wp->cur.maxHp, 1);
    if (!(wp->blink & 2) || blinkOn) {
        numDisp(292.0f + HUD_X(x, 292) - 6.0f, 2.0f + (y - 38.0f), wp->cur.fp, 0);
    }
    numDisp(358.0f + HUD_X(x, 358) - 6.0f, 3.0f + (y - 38.0f), wp->cur.maxFp, 1);
#ifdef TARGET_PC
    if (portOtherHpFpOnly()) {
        return;
    }
#endif
    numDisp(462.0f + HUD_X(x, 462), 2.0f + (y - 38.0f), wp->cur.starPoints, 0);
    if (!(wp->blink & 0x10) || blinkOn) {
        numDisp(572.0f + HUD_X(x, 572), 2.0f + (y - 38.0f), wp->cur.coins, 0);
    }
    if (!(wp->blink & 4) || blinkOn) {
        gaugeDisp(wp->cur.ap, 260.0f + HUD_SP_X(x), 4.0f + (y - 87.0f));
    }

    if (wp->cur.party) {
        partysDt[1].iconId = party_icon[wp->cur.party];
        if (wp->cur.party == PARTNER_YOSHI) {
            switch (pouchGetPartyColor(PARTNER_YOSHI)) {
                case 0:
                    partysDt[1].iconId = ICON_1A7;
                    break;
                case 1:
                    partysDt[1].iconId = ICON_1A8;
                    break;
                case 2:
                    partysDt[1].iconId = ICON_1A9;
                    break;
                case 3:
                    partysDt[1].iconId = ICON_1AA;
                    break;
                case 4:
                    partysDt[1].iconId = ICON_1AB;
                    break;
                case 5:
                    partysDt[1].iconId = ICON_1AC;
                    break;
                case 6:
                default:
                    partysDt[1].iconId = ICON_1AD;
                    break;
            }
        }
        e = partysDt;
        for (i = 0; i < sizeof(partysDt) / sizeof(partysDt[0]); i++, e++) {
            if (wp->blink & 8 && !blinkOn && i == 1) {
                continue;
            }
            MTXTrans(m, HUD_X(x, e->xpos) + (f32)e->xpos, y - (f32)e->ypos, 0.0f);
            MTXScale(s, e->scale, e->scale, e->scale);
            MTXConcat(m, s, m);
            iconDispGxCol(m, SW_ICON_FLAGS, e->iconId, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        }
        if (!(wp->blink & 8) || blinkOn) {
            numDisp(120.0f + HUD_X(x, 120) - 6.0f - 10.0f, 4.0f + (2.0f + (y - 78.0f)), wp->cur.partyHp, 0);
        }
        numDisp(186.0f + HUD_X(x, 186) - 6.0f - 10.0f, 4.0f + (2.0f + (y - 78.0f)), wp->cur.partyMaxHp, 1);
    }

#if VERSION == VERSION_JP || VERSION == VERSION_US || VERSION == VERSION_EU
    // Only the releases draw the menu hints; no demo has them (the 2004-09 demos still set the flag).
    if (wp->flags & STATUSWIN_FLAG_MENU_HINTS && !gp->inBattle
#ifdef TARGET_PC
        && !portMenuHintsNotOurs()
#endif
    ) {
        y2 = -180.0f + (wp->openY - wp->y);
        MTXTrans(m2, HUD_ABS(-190.0f), y2, 0.0f);
        MTXScale(s2, 0.75f, 0.75f, 0.75f);
        MTXConcat(m2, s2, m2);
        // US and PAL pick the menu icons (and the badge icon's offset) per language; JP uses fixed icons
#if VERSION >= VERSION_US
        iconDispGxCol(m2, 0x10, ICON_212, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        if (swByteGet(GSW_STORY_PROGRESS) >= STORY_STG0_07_GOOMBELLA_JOINS || partyChkJoin(PARTNER_GOOMBELLA)) {
            MTXTrans(m2, HUD_ABS(-250.0f), 16.0f + y2, 0.0f);
            MTXScale(s2, 0.75f, 0.75f, 0.75f);
            MTXConcat(m2, s2, m2);
            iconDispGxCol(m2, 0x10, (u16)party_icon_tbl[gp->language], (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        }
        MTXTrans(m2, HUD_ABS(-190.0f), 50.0f + y2, 0.0f);
        MTXScale(s2, 0.75f, 0.75f, 0.75f);
        MTXConcat(m2, s2, m2);
        iconDispGxCol(m2, 0x10, (u16)gear_icon_tbl[gp->language], (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        if (swByteGet(GSW_STORY_PROGRESS) >= STORY_STG0_17_BADGE_TUTORIAL || pouchGetHaveBadgeCnt()) {
            f32 xoff[6] = {0.0f, 10.0f, 3.0f, 8.0f, 8.0f, 0.0f};
            MTXTrans(m2, HUD_ABS(-130.0f) + xoff[gp->language], 16.0f + y2, 0.0f);
            MTXScale(s2, 0.75f, 0.75f, 0.75f);
            MTXConcat(m2, s2, m2);
            iconDispGxCol(m2, 0x10, (u16)badge_icon_tbl[gp->language], (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        }
        MTXTrans(m2, HUD_ABS(-190.0f), y2 - 25.0f, 0.0f);
        MTXScale(s2, 0.75f, 0.75f, 0.75f);
        MTXConcat(m2, s2, m2);
        iconDispGxCol(m2, 0x10, (u16)journal_icon_tbl[gp->language], (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
#else
        iconDispGxCol(m2, 0x10, 0x212, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        if (swByteGet(GSW_STORY_PROGRESS) >= STORY_STG0_07_GOOMBELLA_JOINS || partyChkJoin(PARTNER_GOOMBELLA)) {
            MTXTrans(m2, HUD_ABS(-250.0f), 16.0f + y2, 0.0f);
            MTXScale(s2, 0.75f, 0.75f, 0.75f);
            MTXConcat(m2, s2, m2);
            iconDispGxCol(m2, 0x10, 0x213, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        }
        MTXTrans(m2, HUD_ABS(-190.0f), 50.0f + y2, 0.0f);
        MTXScale(s2, 0.75f, 0.75f, 0.75f);
        MTXConcat(m2, s2, m2);
        iconDispGxCol(m2, 0x10, 0x214, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        if (swByteGet(GSW_STORY_PROGRESS) >= STORY_STG0_17_BADGE_TUTORIAL || pouchGetHaveBadgeCnt()) {
            MTXTrans(m2, HUD_ABS(-130.0f), 16.0f + y2, 0.0f);
            MTXScale(s2, 0.75f, 0.75f, 0.75f);
            MTXConcat(m2, s2, m2);
            iconDispGxCol(m2, 0x10, 0x215, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        }
        MTXTrans(m2, HUD_ABS(-190.0f), y2 - 25.0f, 0.0f);
        MTXScale(s2, 0.75f, 0.75f, 0.75f);
        MTXConcat(m2, s2, m2);
        iconDispGxCol(m2, 0x10, 0x216, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
#endif
    }
#endif
}

/**
 * Reads the values the HUD shows: from the pouch in the field (the partner from the party),
 * from Mario's and the partner's battle units in battle.
 */
static void statusGetValue(SVal* values) {
    BattleWork* bw;
    BattleUnit* mario;
    BattleUnit* party;

    if (!gp->inBattle) {
        values->hp = pouchGetHP();
        values->maxHp = pouchGetMaxHP();
        values->fp = pouchGetFP();
        values->maxFp = pouchGetMaxFP();
        values->ap = pouchGetAP();
        values->maxAp = pouchGetMaxAP();
        values->starPoints = pouchGetStarPoint();
        values->coins = pouchGetCoin();
#if VERSION <= VERSION_DEMO_JP
        // the demo remembers the last partner in cur.lastParty and shows it while in bg mode
        if (!marioBgmodeChk()) {
            values->party = marioGetParty();
            values->partyHp = pouchGetPtr()->partyData[values->party].currentHP;
            values->partyMaxHp = pouchGetPtr()->partyData[values->party].maximumHP;
            if (values->party) {
                wp->cur.lastParty = values->party;
            }
        } else {
            values->party = wp->cur.lastParty;
            values->partyHp = pouchGetPtr()->partyData[values->party].currentHP;
            values->partyMaxHp = pouchGetPtr()->partyData[values->party].maximumHP;
        }
#else
        values->party = !marioBgmodeChk() ? (s16)marioGetParty() : marioGetPtr()->prevPartyMemberId[0];
        values->partyHp = pouchGetPtr()->partyData[values->party].currentHP;
        values->partyMaxHp = pouchGetPtr()->partyData[values->party].maximumHP;
#endif
    } else {
        bw = _battleWorkPointer;
        mario = BattleGetMarioPtr(bw);
        party = BattleGetPartyPtr(bw);
        values->hp = mario->currentHp;
        values->maxHp = mario->maxHp;
        values->fp = mario->currentFp;
        values->maxFp = mario->maxFp;
        values->ap = pouchGetAP();
        values->maxAp = pouchGetMaxAP();
        values->starPoints = pouchGetStarPoint();
        values->coins = pouchGetCoin();
        if (party) {
            values->party = BattleTransPartyId(party->currentKind);
            values->partyHp = party->currentHp;
            values->partyMaxHp = party->maxHp;
        } else {
            values->party = 0;
            values->partyHp = 0;
            values->partyMaxHp = 0;
        }
    }
}

/**
 * Returns TRUE when any shown value differs from what the HUD last displayed, and marks HP,
 * FP or Star Power changes for a blink.
 */
static BOOL valueCheck(void) {
    SVal v;
    BOOL changed = FALSE;

    statusGetValue(&v);
    if (wp->cur.hp != v.hp) {
        changed = TRUE;
        wp->pendingBlink |= 1;
    }
    if (wp->cur.maxHp != v.maxHp) {
        changed = TRUE;
    }
    if (wp->cur.fp != v.fp) {
        changed = TRUE;
        wp->pendingBlink |= 2;
    }
    if (wp->cur.maxFp != v.maxFp) {
        changed = TRUE;
    }
    if (wp->cur.ap != v.ap) {
        changed = TRUE;
    }
    if (wp->cur.maxAp != v.maxAp) {
        changed = TRUE;
    }
#ifdef TARGET_PC
    if (portPartyRowNew(&v)) {
        /* (a partner new to the row: its HP shown as it is; as in the game, the swap's own frame is no change) */
    } else if (sPartyRowCounting && wp->cur.party == v.party) {
        /* (the unseen count-up: a change while the game's numbers still counted) */
        if (sPartyRowHp != v.partyHp || sPartyRowMaxHp != v.partyMaxHp) {
            changed = TRUE;
        }
    } else
#endif
    if (wp->cur.party != v.party) {
        wp->cur.party = v.party;
    } else if (wp->cur.party) {
        if (wp->cur.partyHp != v.partyHp) {
            changed = TRUE;
            wp->pendingBlink |= 8;
        }
        if (wp->cur.partyMaxHp != v.partyMaxHp) {
            changed = TRUE;
        }
    }
    if (wp->cur.starPoints != v.starPoints) {
        changed = TRUE;
    }
    if (wp->cur.coins != v.coins) {
        changed = TRUE;
        wp->pendingBlink |= 0x10;
    }
    if (!changed) {
        if (wp->pendingBlink != 0U) {
            wp->blink = wp->pendingBlink;
            wp->pendingBlink = 0;
            wp->blinkTimer = gp->framerate * 2;
        }
    } else {
        wp->blink = 0;
    }
    return changed;
}

/**
 * Step for the counting-number animation: a tenth of the remaining difference, at least 1 toward the target,
 * 0 when equal.
 */
static inline s32 calcStep(s32 target, s32 cur) {
    s32 d = target - cur;
    if (d) {
        d /= 10;
        if (!d) {
            d = -1;
            if (target > cur) {
                d = 1;
            }
        }
    }
    return d;
}

/**
 * Counts the displayed HUD numbers toward the real values every other frame while the window is open,
 * with the coin counting sound every 8th frame (6th in the demos).
 */
static void valueUpdate(void) {
    static s32 count;
    SVal v;
    s32 d;

    statusGetValue(&v);
    count++;
    count %= 1200;
    if (count & 1) {
        return;
    }
    if (!statusWinCheck()) {
        return;
    }
    d = calcStep(v.coins, wp->cur.coins);
#if VERSION <= VERSION_DEMO_JP
    if (d && !(count % 6)) {
#else
    if (d && !(count % 8)) {
#endif
        psndSFXOn("SFX_COUNT_COIN1");
    }
    wp->cur.hp += calcStep(v.hp, wp->cur.hp);
    wp->cur.maxHp += calcStep(v.maxHp, wp->cur.maxHp);
    wp->cur.fp += calcStep(v.fp, wp->cur.fp);
    wp->cur.maxFp += calcStep(v.maxFp, wp->cur.maxFp);
    wp->cur.ap += calcStep(v.ap, wp->cur.ap);
    wp->cur.maxAp += calcStep(v.maxAp, wp->cur.maxAp);
    wp->cur.starPoints += calcStep(v.starPoints, wp->cur.starPoints);
    wp->cur.coins += calcStep(v.coins, wp->cur.coins);
#ifdef TARGET_PC
    portPartyRowNew(&v);
    if (sPartyRowCounting) {
        /* (the game's count-up, unseen: portPartyRowNew) */
        sPartyRowHp += calcStep(v.partyHp, sPartyRowHp);
        sPartyRowMaxHp += calcStep(v.partyMaxHp, sPartyRowMaxHp);
        if (wp->cur.party != v.party || (sPartyRowHp == v.partyHp && sPartyRowMaxHp == v.partyMaxHp)) {
            sPartyRowCounting = FALSE;
        }
    }
#endif
    wp->cur.party = v.party;
    if (wp->cur.party) {
        wp->cur.partyHp += calcStep(v.partyHp, wp->cur.partyHp);
        wp->cur.partyMaxHp += calcStep(v.partyMaxHp, wp->cur.partyMaxHp);
    }
    if (wp->cur.starPoints > 99) {
        wp->cur.starPoints = 99;
    }
#ifdef TARGET_PC
    if (wp->cur.coins > portCoinCap()) {
        wp->cur.coins = portCoinCap();
    }
#else
    if (wp->cur.coins > 999) {
        wp->cur.coins = 999;
    }
#endif
}

/** Requests the HUD to slide in. */
void PORT_HOOKABLE(statusWinOpen)(void) {
    wp->flags &= ~(STATUSWIN_FLAG_CLOSED | STATUSWIN_FLAG_CLOSE_REQ);
    wp->flags |= STATUSWIN_FLAG_OPEN | STATUSWIN_FLAG_OPEN_REQ;
}

/** Requests the HUD to slide out. */
void PORT_HOOKABLE(statusWinClose)(void) {
    wp->flags |= STATUSWIN_FLAG_CLOSED | STATUSWIN_FLAG_CLOSE_REQ;
    wp->flags &= ~(STATUSWIN_FLAG_OPEN | STATUSWIN_FLAG_OPEN_REQ);
}

/** Slides the HUD in and pins it open regardless of Mario's movement, until statusWinForceOff. */
void PORT_HOOKABLE(statusWinForceOpen)(void) {
    wp->flags &= ~(STATUSWIN_FLAG_CLOSED | STATUSWIN_FLAG_CLOSE_REQ);
    wp->flags &= ~(STATUSWIN_FLAG_FORCE_CLOSE | STATUSWIN_FLAG_FORCE_CLOSE_REQ);
    wp->flags |= STATUSWIN_FLAG_FORCE_OPEN | STATUSWIN_FLAG_FORCE_OPEN_REQ;
}

/** Slides the HUD out and keeps it closed, until statusWinForceOff or statusWinForceCloseClear. */
void PORT_HOOKABLE(statusWinForceClose)(void) {
    wp->flags &= ~(STATUSWIN_FLAG_OPEN | STATUSWIN_FLAG_OPEN_REQ);
    wp->flags &= ~(STATUSWIN_FLAG_FORCE_OPEN | STATUSWIN_FLAG_FORCE_OPEN_REQ);
    wp->flags |= STATUSWIN_FLAG_FORCE_CLOSE | STATUSWIN_FLAG_FORCE_CLOSE_REQ;
}

/** Drops a forced close without reopening the HUD. */
void PORT_HOOKABLE(statusWinForceCloseClear)(void) {
    wp->flags &= ~(STATUSWIN_FLAG_FORCE_CLOSE | STATUSWIN_FLAG_FORCE_CLOSE_REQ);
}

/** Ends a forced open or close, leaving the HUD in the matching normal state; called when message windows close. */
void PORT_HOOKABLE(statusWinForceOff)(void) {
    if (wp->flags & STATUSWIN_FLAG_FORCE_OPEN) {
        wp->flags &= ~STATUSWIN_FLAG_FORCE_OPEN;
        statusWinOpen();
    } else if (wp->flags & STATUSWIN_FLAG_OPEN) {
        statusWinOpen();
    }
    if (wp->flags & STATUSWIN_FLAG_FORCE_CLOSE) {
        wp->flags &= ~STATUSWIN_FLAG_FORCE_CLOSE;
        statusWinClose();
    } else if (wp->flags & STATUSWIN_FLAG_CLOSED) {
        statusWinClose();
    }
}

/** Lets the HUD draw again after statusWinDispOff. */
void PORT_HOOKABLE(statusWinDispOn)(void) {
    wp->flags &= ~STATUSWIN_FLAG_DISP_OFF;
}

/** Stops drawing the HUD without changing whether it is open. */
void PORT_HOOKABLE(statusWinDispOff)(void) {
    wp->flags |= STATUSWIN_FLAG_DISP_OFF;
}

/** Returns TRUE when the HUD is open or forced open. */
BOOL PORT_HOOKABLE(statusWinCheck)(void) {
    return (wp->flags & (STATUSWIN_FLAG_OPEN | STATUSWIN_FLAG_FORCE_OPEN)) ? TRUE : FALSE;
}

/** Returns TRUE when a shown value changed since the last display (see valueCheck). */
BOOL PORT_HOOKABLE(statusWinCheckUpdate)(void) {
    return valueCheck() != 0;
}

/** Snaps every displayed HUD value to the current one without the counting animation. */
void PORT_HOOKABLE(statusWinForceUpdate)(void) {
    SVal v;

    statusGetValue(&v);
    wp->cur.hp = v.hp;
    wp->cur.maxHp = v.maxHp;
    wp->cur.fp = v.fp;
    wp->cur.maxFp = v.maxFp;
    wp->cur.ap = v.ap;
    wp->cur.maxAp = v.maxAp;
    wp->cur.starPoints = v.starPoints;
    wp->cur.coins = v.coins;
    wp->cur.party = v.party;
    if (wp->cur.party) {
        wp->cur.partyHp = v.partyHp;
        wp->cur.partyMaxHp = v.partyMaxHp;
    }
}

/** Snaps the displayed coin count to the current one. */
void PORT_HOOKABLE(statusWinForceUpdateCoin)(void) {
    SVal v;

    statusGetValue(&v);
    wp->cur.coins = v.coins;
}

/** Returns the screen position of the Star Power gauge, where the audience's Star Power sparkles fly to. */
Vec PORT_HOOKABLE(statusGetApPos)(void) {
    Vec pos;
    pos.x = wp->x + (f32)alwaysDt[12].xpos;
    pos.y = wp->y - (f32)alwaysDt[12].ypos;
    pos.z = 0.0f;
    return pos;
}

/** Makes the Star Power number blink for frames, replacing any other blink. */
void PORT_HOOKABLE(statusAPBlink)(s32 frames) {
    wp->blink = 0;
    wp->blink |= 4;
    wp->blinkTimer = frames;
}

/** The demo has no N_statusClearBlink. */
#if VERSION >= VERSION_JP
void PORT_HOOKABLE(N_statusClearBlink)(void) {
    wp->blink = 0;
}
#endif

/** Makes the FP number blink for frames. */
void PORT_HOOKABLE(statusFPBlink)(s32 frames) {
    wp->blink |= 2;
    wp->blinkTimer = frames;
}

/** Makes Mario's HP number blink for frames. */
void PORT_HOOKABLE(statusMarioHPBlink)(s32 frames) {
    wp->blink |= 1;
    wp->blinkTimer = frames;
}

/** Makes the partner's HP number blink for frames. */
void PORT_HOOKABLE(statusPartyHPBlink)(s32 frames) {
    wp->blink |= 8;
    wp->blinkTimer = frames;
}

/**
 * Stand-in for a stripped function: it is in no version's symbols and nothing calls it, so the link drops it.
 * It fills f with 0, 1, pi and 0.5 (and 60 in US and PAL), the float constants the file uses,
 * in the order the original keeps them.
 */
// FAKE: uncalled stand-in for a stripped function; orders 0, 1, pi, 0.5 (and US/PAL 60) in .sdata2
void statusWinEase(f32* f) {
    f[0] = 0.0f;
#if VERSION >= VERSION_US
    // US pool order: 0, 60 (framerate step), pi, 1, 0.5
    f[4] = 60.0f;
    f[2] = 3.1415927f;
    f[1] = 1.0f;
#else
    f[1] = 1.0f;
    f[2] = 3.1415927f;
#endif
    f[3] = 0.5f;
}

#ifdef TARGET_PC
/* PC port: the values the HUD shows now (counted up to, not the targets), for the test scripts' practice show
 * (port/include/port/practice_tools.h): HP, max HP, FP, max FP, the partner's HP and max HP */
void portStatusWinShown(int out[6]);
void portStatusWinShown(int out[6]) {
    out[0] = wp != NULL ? wp->cur.hp : -1;
    out[1] = wp != NULL ? wp->cur.maxHp : -1;
    out[2] = wp != NULL ? wp->cur.fp : -1;
    out[3] = wp != NULL ? wp->cur.maxFp : -1;
    out[4] = wp != NULL ? wp->cur.partyHp : -1;
    out[5] = wp != NULL ? wp->cur.partyMaxHp : -1;
}

/* PC port: HUD scale (port/hud.h) tells the status bar's draw apart */
int portHudIsStatusBar(DispCallback callback) {
    return callback == statusWinDisp || callback == portStatusWinDispOther;
}

/* A versus battle (port/include/port/versus.h): the HUD drawn again at the
 * bottom of the screen (its top at openY) with the other side's values (as
 * shown, without the counting or blinks), sliding in and out with the
 * game's own: down off the screen as the game's goes up. Brothers mode's
 * Luigi (port/include/port/brothers.h): his HP and FP banners alone, under
 * the game's (an openY in the top half), sliding up with it. */
static SVal sOther;
static f32 sOtherY;
static BOOL sOtherHpFpOnly; /* portStatusWinSetOtherHpFpOnly */
static BOOL sDrawingOther;

static BOOL portOtherHpFpOnly(void) {
    return sDrawingOther && sOtherHpFpOnly;
}

void portStatusWinSetOther(const StatusValue* v, f32 openY) {
    sOther = *v;
    sOtherY = openY;
    sOtherHpFpOnly = FALSE;
}

void portStatusWinSetOtherHpFpOnly(void) {
    sOtherHpFpOnly = TRUE;
}

void portStatusWinDispOther(CameraId camId, void* param) {
    const SVal keep = wp->cur;
    const f32 y = wp->y;
    const s32 blink = wp->blink, timer = wp->blinkTimer;
    if (wp->flags & STATUSWIN_FLAG_DISP_OFF) {
        return;
    }
    wp->cur = sOther;
    if (sOtherY >= 0.0f) {
        /* in the top half: up off the screen with the game's, to where it closes */
        wp->y = sOtherY + (y - wp->openY) * (wp->closeY - sOtherY) / (wp->closeY - wp->openY);
    } else {
        wp->y = sOtherY - (y - wp->openY);
    }
    wp->blink = 0;
    wp->blinkTimer = 0;
    sDrawingOther = TRUE;
    statusWinDisp(camId, param);
    sDrawingOther = FALSE;
    wp->cur = keep;
    wp->y = y;
    wp->blink = blink;
    wp->blinkTimer = timer;
}
#endif

#ifdef PORT_MODS
#include "port/hooks/src_statuswindow.inc"
#endif

#ifdef TARGET_PC
/* frame interpolation (port/src/aurora/interp_aurora.c): the HUD's slide in and out */
StatusWindowWork* portStatusWinWork(void) {
    return wp;
}
#endif
