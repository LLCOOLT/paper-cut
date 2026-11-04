/* A second player for the partner (Multiplayer > Second player;
 * port/settings/features/second_player.cpp). Off by default, and nothing
 * changes without a second controller.
 *
 * In battle: while the partner has the turn (choosing its move, aiming, its
 * action commands) the game reads controller 2 as controller 1, so the second
 * player plays the partner and the first plays Mario; an enemy's attack on
 * the partner is guarded by the second player too (portSecondPlayerGuardPad).
 *
 * In the field, while Mario has the controls: the second player's stick walks
 * the partner wherever they like (portCoopPartnerWalk, in place of following
 * Mario's footmarks; walls, floors and falls as the partner's own), A hops, B
 * talks to an NPC or hits a block or switch within reach, X is the partner's
 * field ability and L or R switches partners. Mario's player calls the
 * partner back to his side with Y, and it comes back by itself when it gets
 * far from Mario (off the screen) or falls, when a scene starts or the room
 * changes. Until the second player moves, the partner follows Mario as
 * always. Moving Mario, menus and Mario's guards stay controller 1's.
 *
 * In the field "1P" over Mario and "2P" over the partner show whose is whose (2P bright while
 * the second player moves it, faint while it follows Mario), and a notice at
 * the bottom of the screen says when a second controller joins or leaves;
 * with Second player off, one joining says where to turn it on (once a run).
 *
 * makeKey (system.c) fills the game's per-controller input each frame, then
 * calls portSecondPlayerRoute, which copies controller 2's state over
 * controller 1's for the partner's turn, or takes the second player's field
 * input. */
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/mobjdrv.h"
#include "driver/npcdrv.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_sbr.h"
#include "mario/mariost.h"
#include "party.h"
#include "party/party_move.h"
#include "party/party_yoshi.h"
#include "port/gameplus.h"
#include "port/settings.h"
#include "system.h"
#include "wide_x.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "manager/fontmgr.h"
#include "port/tr.h"
#include "port/online.h"
#include "port/platform.h"
#include "port/view.h"
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

f32 partySetTargetSpd(PartyEntry* party, f32 spd);        /* party_move.c */
EventEntry* mobjRunEvent(MapObjectEntry* entry, EvtWord* eventCode); /* mobjdrv.c */
MapObjectWork* portMobjGetWork(void);                      /* mobjdrv.c */

static int* sOn;

/* co-op is on: the setting, or an online session (player 2 is the guest) */
static int coopOn(void) {
    return (sOn != NULL && *sOn) || portOnlineActive();
}

/* a second controller (or $TTYD_P2_PRESS's, tests: input_pc.c; online, the
 * guest's) */
static int secondPad(void) {
    if (portOnlineActive()) {
        return 1;
    }
#ifdef TTYD_BACKEND_AURORA
    static int sTest = -1;
    if (sTest < 0) {
        sTest = getenv("TTYD_P2_PRESS") != NULL;
    }
    return sTest || PADGetIndexForPort(1) >= 0;
#else
    return 1;
#endif
}

static int partnerHasTurn(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* unit;
    if (gp == NULL || !gp->inBattle || wp == NULL || wp->activeUnitId < 0) {
        return 0;
    }
    unit = BattleGetUnitPtr(wp, wp->activeUnitId);
    return unit != NULL && unit->alliance == 0 && unit->currentKind >= TYPE_PARTNER_MIN &&
           unit->currentKind != UNIT_MARIO;
}

/* battle_ac.c, a guard: an attack on the partner is guarded with controller
 * 2 (its record, kept every frame by BattlePadManager), so the second player
 * defends the partner they play. Mario's guards stay controller 1's. */
BattleWorkPad* portSecondPlayerGuardPad(BattleUnit* unit) {
    if (!coopOn() || unit == NULL || _battleWorkPointer == NULL || unit->alliance != 0 ||
        unit->currentKind < TYPE_PARTNER_MIN || unit->currentKind == UNIT_MARIO) {
        return NULL;
    }
    if (!secondPad()) {
        return NULL;
    }
    return &_battleWorkPointer->padWork[1];
}

/* Controller haptics (ac_cue_pc.c): whose controller feels a timed press's
 * cue: the second player's (1) for the partner's guards and, while the
 * partner has the turn, its action commands (as portSecondPlayerRoute reads
 * them); else the first's (0) */
int portSecondPlayerPlayerFor(void* unit, int guard) {
    if (guard) {
        return portSecondPlayerGuardPad((BattleUnit*)unit) != NULL;
    }
    return coopOn() && secondPad() && partnerHasTurn();
}

/* --- The field --- */

#define DEAD_ZONE 14.0f   /* of the stick's 70 */
#define LEASH 220.0f      /* units from Mario: about the screen's reach */
#define REACH 60.0f       /* units: what the partner can talk to or hit */

static struct {
    int free;             /* the second player moves the partner */
    float stickX, stickY; /* the second player's stick this frame */
    int jump, interact;   /* A, B pressed this frame */
    int recall;           /* Mario's player pressed Y */
    s32 talkEvt;          /* a talk the partner started (-1 none) */
    NpcEntry* talkNpc;
    char map[16];
    int letGo;            /* called back: the stick must be let go before the partner is taken again */
} sCoop = {0, 0.0f, 0.0f, 0, 0, 0, -1, NULL, "", 0};

/* the partner's feet inside the picture (the 608x480 frame, wider in
 * widescreen), a little in from its edges */
static int onScreen(PartyEntry* party) {
    Vec pos = party->position, screen;
    const float edge = 304.0f * portWidescreenEdgeScale() - 12.0f;
    if (portOnlineActive()) {
        return 1; /* each player's picture may be a different shape: the leash alone, the same in both games */
    }
    getScreenPoint(&pos, &screen);
    return screen.x > -edge && screen.x < edge && screen.y > -228.0f && screen.y < 236.0f;
}

static int fieldControl(void) {
    return gp != NULL && !gp->inBattle && marioStGetSystemLevel() == 0 && marioChkKey() && marioChkCtrl() &&
           yoshiGetStatus() == 0;
}

/* The partner back at Mario's side (the spot it appears at), following him
 * again. */
static void recall(PartyEntry* party) {
    Vec pos;
    sCoop.free = 0;
    sCoop.letGo = 1;
    if (party == NULL) {
        return;
    }
    if (partyGetAppearPos(party, &pos)) {
        party->position = pos;
    } else {
        party->position = marioGetPtr()->position;
    }
    party->forceMoveFrames = 0;
    partySetTargetSpd(party, 0.0f);
    partyChgMot(party, 0);
    partyClearFootmark();
    printf("[coop] the partner is back at Mario's side\n");
    fflush(stdout);
}

/* B: the nearest NPC within reach's talk (as A in front of it starts it), or
 * else the nearest block or switch's event (as a hit runs it) */
static void interact(PartyEntry* party) {
    NpcWork* nw = npcGetWorkPtr();
    MapObjectWork* mw = portMobjGetWork();
    NpcEntry* npc = NULL;
    MapObjectEntry* mobj = NULL;
    float best = REACH;
    int i;
    for (i = 0; nw != NULL && i < nw->count; i++) {
        NpcEntry* n = &nw->entries[i];
        const float d = hypotf(n->position.x - party->position.x, n->position.z - party->position.z);
        if ((n->flags & 1) && n->talkEvt != NULL && fabsf(n->position.y - party->position.y) < 60.0f && d < best) {
            npc = n;
            best = d;
        }
    }
    if (npc != NULL) {
        EventEntry* e;
        evtStopID(npc->regularEvtId);
        marioKeyOff();
        partyKeyOff();
        e = evtEntry(npc->talkEvt, 0, 0);
        e->wNpcEventType = 6;
        e->ownerNPC = npc;
        party->targetDir = angleABf(party->position.x, party->position.z, npc->position.x, npc->position.z);
        sCoop.talkEvt = e->eventId;
        sCoop.talkNpc = npc;
        printf("[coop] the partner talks to '%s'\n", npc->description);
        fflush(stdout);
        return;
    }
    best = REACH;
    for (i = 0; mw != NULL && i < mw->count; i++) {
        MapObjectEntry* m = &mw->entries[i];
        const float d = hypotf(m->position.x - party->position.x, m->position.z - party->position.z);
        if ((m->flags & 1) && m->event != 0 && m->position.y - party->position.y < 80.0f &&
            m->position.y - party->position.y > -20.0f && d < best) {
            mobj = m;
            best = d;
        }
    }
    if (mobj != NULL) {
        printf("[coop] the partner hits '%s'\n", mobj->name);
        fflush(stdout);
        mobjRunEvent(mobj, (EvtWord*)mobj->event);
    }
}

/* the partner's talk ended: the NPC back to its own script, the controls back */
static void talkFrame(void) {
    if (sCoop.talkEvt < 0) {
        return;
    }
    /* either player reads the text on */
    gp->buttonNew[0] |= gp->buttonNew[1] & (PAD_BUTTON_A | PAD_BUTTON_B);
    gp->button[0] |= gp->button[1] & (PAD_BUTTON_A | PAD_BUTTON_B);
    if (evtCheckID(sCoop.talkEvt)) {
        return;
    }
    if (sCoop.talkNpc != NULL && (sCoop.talkNpc->flags & 1)) {
        evtStartID(sCoop.talkNpc->regularEvtId);
    }
    marioKeyOn();
    partyKeyOn();
    sCoop.talkEvt = -1;
    sCoop.talkNpc = NULL;
}

/* makeKey, in the field: the second player's input for the partner's walk,
 * X (the partner's ability: the game's X, held as long as it is held, as for
 * Yoshi's ride or Flurrie's gust) and L or R (the next partner, in the
 * quick-swap's order, with or without the Enhancements tab's setting); Mario's Y */
static void fieldRoute(void) {
    static u32 sShoulders;
    const u32 shoulders = gp->button[1] & (PAD_TRIGGER_L | PAD_TRIGGER_R);
    talkFrame();
    if (strncmp(sCoop.map, gp->currentMapName, sizeof(sCoop.map)) != 0) {
        snprintf(sCoop.map, sizeof(sCoop.map), "%s", gp->currentMapName);
        sCoop.free = 0; /* a new room: the partner comes in with Mario */
    }
    sCoop.stickX = gp->stickX[1];
    sCoop.stickY = gp->stickY[1];
    if (!fieldControl() || sCoop.talkEvt >= 0) {
        sShoulders = shoulders;
        return;
    }
    sCoop.jump |= (gp->buttonNew[1] & PAD_BUTTON_A) != 0;
    sCoop.interact |= (gp->buttonNew[1] & PAD_BUTTON_B) != 0;
    sCoop.recall |= (gp->buttonNew[0] & PAD_BUTTON_Y) != 0;
    if (gp->button[1] & PAD_BUTTON_X) {
        gp->button[0] |= PAD_BUTTON_X;
    }
    if (gp->buttonNew[1] & PAD_BUTTON_X) {
        gp->buttonNew[0] |= PAD_BUTTON_X;
    }
    if (gp->buttonUp[1] & PAD_BUTTON_X) {
        gp->buttonUp[0] |= PAD_BUTTON_X;
    }
    if (shoulders & ~sShoulders) {
        portQuickSwapPressCoop();
    }
    sShoulders = shoulders;
}

/* party_move.c, the partner's walk mode standing or walking: 1 when the
 * second player moves it this frame (in place of following Mario) */
int portCoopPartnerWalk(PartyEntry* party) {
    MarioWork* mario;
    float mag, dx, dz;
    if (!coopOn() || !secondPad() || party == NULL || party != partyGetPtr(marioGetPartyId())) {
        return 0;
    }
    mario = marioGetPtr();
    if (party->forceMoveFrames != 0 && sCoop.free) {
        party->forceMoveFrames = 0; /* a hop's (below), landed */
    }
    if (sCoop.talkEvt >= 0 && sCoop.free) {
        /* its own talk: it stands where it is, facing the NPC */
        partySetTargetSpd(party, 0.0f);
        party->moveSpeed = 0.0f;
        party->motionId = 0;
        return 1;
    }
    if (!fieldControl()) {
        if (sCoop.free) {
            recall(party); /* a scene, a menu, Yoshi's ride: with Mario */
        }
        sCoop.jump = sCoop.interact = sCoop.recall = 0;
        return 0;
    }
    mag = hypotf(sCoop.stickX, sCoop.stickY);
    if (mag <= DEAD_ZONE) {
        sCoop.letGo = 0;
    }
    if (mag > DEAD_ZONE && !sCoop.free && !sCoop.letGo) {
        sCoop.free = 1;
        party->moveMode = 1; /* walk mode: its jump, fall and step motions for the hops */
        printf("[coop] the second player takes the partner\n");
        fflush(stdout);
    }
    if (sCoop.recall) {
        sCoop.recall = 0;
        if (sCoop.free) {
            recall(party);
            return 1;
        }
    }
    if (!sCoop.free) {
        sCoop.jump = sCoop.interact = 0;
        return 0;
    }
    dx = party->position.x - mario->position.x;
    dz = party->position.z - mario->position.z;
    if (!onScreen(party) || hypotf(dx, dz) > LEASH || party->position.y < mario->position.y - 200.0f ||
        (party->flags & PARTY_FLAG_FORCE_FALL)) {
        recall(party); /* off the screen, far, fallen or in deep water */
        return 1;
    }
    if (mag > DEAD_ZONE) {
        /* as marioGetStick turns Mario's stick: the camera's yaw added */
        const float m = mag > 70.0f ? 70.0f : mag;
        party->targetDir = revise360(angleABf(0.0f, 0.0f, sCoop.stickX, -sCoop.stickY) + mario->targetYaw);
        partySetTargetSpd(party, (m - DEAD_ZONE) / (70.0f - DEAD_ZONE) * (mario->baseDashSpeed > 0.0f
                                                                              ? mario->baseDashSpeed
                                                                              : 4.0f));
        party->motionId = 1;
    } else {
        partySetTargetSpd(party, 0.0f);
        party->moveSpeed = 0.0f;
        party->motionId = 0;
    }
    if (sCoop.interact) {
        sCoop.interact = 0;
        interact(party);
    }
    if (sCoop.jump) {
        sCoop.jump = 0;
        party->forceMoveFrames = 1; /* party_motion_jump: straight along targetDir at moveSpeed */
        partyChgMot(party, 2);
        return 1;
    }
    partyMoveNoHosei(party);
    party->moveDir = party->targetDir;
    return 1;
}

/* --- What the players see: the partner's tag, the join and leave notices --- */

#define NOTICE_FRAMES 210

extern BinaryFontHeader* pfh; /* fontmgr.c: the font, NULL until the logos load it */

static struct {
    const char* text; /* English (portTrGame at drawing) */
    int age;
} sNotice;

/* a player's tag over whom they play: "1P" (red) over Mario, "2P" (blue)
 * over the partner, faint while it follows Mario */
static void tag(const Vec* top, const char* text, GXColor color) {
    Vec screen;
    float w;
    getScreenPoint((Vec*)top, &screen);
    if (portViewMirrored()) {
        screen.x = -screen.x; /* mirror mode: the picture, not the 2D layer, is flipped */
    }
    FontDrawStart_alpha(color.a);
    FontDrawEdge();
    FontDrawColor(color);
    FontDrawScale(0.7f);
    w = FontGetMessageWidth(text) * 0.7f;
    FontDrawString(screen.x - w * 0.5f, screen.y + 18.0f, text);
}

/* The game's font draws each letter with two of the game's random numbers
 * (fontmgr.c, _JUTFont_DrawPos: irand for its noise texture), and the frame's
 * drawing comes before its logic. So the port's own words over the picture keep
 * the game's random numbers as they were: else what one player sees and the
 * other doesn't (a notice shown a frame apart, in another language, a setting
 * of one's own) puts an online game out of step. */
static void tagDisp(CameraId camId, void* param) {
    PartyEntry* party = (PartyEntry*)param;
    MarioWork* mario = marioGetPtr();
    const u32 seed = portRandState();
    Vec top;
    (void)camId;
    top = mario->position;
    top.y += 52.0f; /* above his cap */
    tag(&top, "1P", (GXColor){0xFF, 0x70, 0x60, 0xFF});
    top.x = party->position.x + party->dispOfs.x;
    top.y = party->position.y + party->dispOfs.y + party->height + 8.0f;
    top.z = party->position.z + party->dispOfs.z;
    tag(&top, "2P", (GXColor){0x6C, 0xC8, 0xFF, sCoop.free ? 0xFF : 0x70});
    portRandSetState(seed);
}

static void noticeDraw(void) {
#ifdef TTYD_BACKEND_AURORA
    const char* text = portTrGame(sNotice.text);
#else
    const char* text = sNotice.text; /* the headless build has no translations */
#endif
    const int left = NOTICE_FRAMES - sNotice.age;
    const u8 alpha = left < 30 ? (u8)(0xFF * left / 30) : 0xFF;
    const float maxW = 560.0f; /* inside the 4:3 picture, a little in from its edges */
    char lines[2][160];
    int count = 1, i;
    snprintf(lines[0], sizeof(lines[0]), "%s", text);
    lines[1][0] = '\0';
    if (FontGetMessageWidth(text) * 0.75f > maxW) {
        /* two lines, broken at the space nearest the middle (a translation runs longer) */
        const int len = (int)strlen(lines[0]);
        int at = -1, k;
        for (k = 0; k < len; k++) {
            if (lines[0][k] == ' ' && (at < 0 || abs(k - len / 2) < abs(at - len / 2))) {
                at = k;
            }
        }
        if (at > 0) {
            snprintf(lines[1], sizeof(lines[1]), "%s", lines[0] + at + 1);
            lines[0][at] = '\0';
            count = 2;
        }
    }
    FontDrawStart_alpha(alpha);
    FontDrawEdge();
    FontDrawColor((GXColor){0xFF, 0xFF, 0xFF, alpha});
    for (i = 0; i < count; i++) {
        float scale = 0.75f, w = FontGetMessageWidth(lines[i]) * scale;
        if (w > maxW) {
            scale *= maxW / w; /* still too wide (no space to break at): smaller */
            w = maxW;
        }
        FontDrawScale(scale);
        FontDrawString(-w * 0.5f, (count == 2 ? -138.0f : -150.0f) - i * 22.0f, lines[i]);
    }
}

static void noticeDisp(CameraId camId, void* param) {
    const u32 seed = portRandState(); /* (the game's random numbers: tagDisp's) */
    (void)camId;
    (void)param;
    noticeDraw();
    portRandSetState(seed);
}

static void notice(const char* english) {
    sNotice.text = english;
    sNotice.age = 0;
    printf("[coop] %s\n", english);
    fflush(stdout);
}

/* the same notice for online co-op's news (online_pc.c) */
void portCoopNotice(const char* english) {
    notice(english);
}

/* each frame, before the partner's own: the notices, a joining or leaving
 * second controller (seen in the field while Mario has the controls, so one
 * joining in a scene is told once it's over, and one there from the start is
 * told at the first room), and the tag */
static void seenFrame(void) {
    static int sHad = 0, sHinted, sWasOn;
    const int has = secondPad();
    if (portOnlineActive()) {
        sHad = sWasOn = 1; /* online: player 2 is the guest, its notices online_pc.c's */
    }
    if (fieldControl() && *sOn && !sWasOn && !has) {
        notice("Co-op is on: press A on another controller to join.");
    }
    if (fieldControl()) {
        sWasOn = *sOn;
    }
    if (fieldControl() && has != sHad) {
        if (has && *sOn) {
            notice("Player 2 joined: their stick walks the partner.");
        } else if (has && !sHinted) {
            sHinted = 1;
            notice("A second controller: turn on Second player (Controls) to play the partner.");
        } else if (!has && *sOn) {
            notice("Player 2 left: the partner follows Mario.");
        }
        sHad = has;
    }
    /* (an online game can end before the logos load the font: the notice waits for it) */
    if (sNotice.text != NULL && pfh != NULL) {
        if (++sNotice.age >= NOTICE_FRAMES) {
            sNotice.text = NULL;
        } else if (!gp->inBattle) {
            dispEntry(CAMERA_2D, 903.0f, 1, noticeDisp, NULL);
        }
    }
    if (coopOn() && has && fieldControl()) {
        PartyEntry* party = partyGetPtr(marioGetPartyId());
        if (party != NULL) {
            dispEntry(CAMERA_2D, 902.0f, 1, tagDisp, party);
        }
    }
}

void portSecondPlayerRoute(void) {
    if (sOn == NULL) {
        sOn = portSettingsDeclare("second_player", 0, 0, 1);
    }
    if (gp == NULL) {
        return;
    }
    seenFrame();
    if (!coopOn()) {
        return;
    }
    if (!secondPad()) {
        return; /* no second controller: the first one plays the partner too */
    }
    if (!partnerHasTurn()) {
        fieldRoute();
        return;
    }
    gp->button[0] = gp->button[1];
    gp->buttonNew[0] = gp->buttonNew[1];
    gp->buttonRepeat[0] = gp->buttonRepeat[1];
    gp->buttonRepeatDelay[0] = gp->buttonRepeatDelay[1];
    gp->buttonUp[0] = gp->buttonUp[1];
    gp->dirs[0] = gp->dirs[1];
    gp->dirsNew[0] = gp->dirsNew[1];
    gp->dirsRepeat[0] = gp->dirsRepeat[1];
    gp->dirsRepeatDelay[0] = gp->dirsRepeatDelay[1];
    gp->stickX[0] = gp->stickX[1];
    gp->stickY[0] = gp->stickY[1];
    gp->substickX[0] = gp->substickX[1];
    gp->substickY[0] = gp->substickY[1];
    gp->triggerLeft[0] = gp->triggerLeft[1];
    gp->triggerRight[0] = gp->triggerRight[1];
}
