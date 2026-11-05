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
#include "port/replay.h"
#include "port/platform.h"
#include "port/view.h"
#include "port/versus.h"
#include "port/duo.h"
#include "port/players.h"
#include "driver/seqdrv.h"
#include "port/brothers.h"
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

f32 partySetTargetSpd(PartyEntry* party, f32 spd);        /* party_move.c */
EventEntry* mobjRunEvent(MapObjectEntry* entry, EvtWord* eventCode); /* mobjdrv.c */
MapObjectWork* portMobjGetWork(void);                      /* mobjdrv.c */

extern PADStatus Pad[PAD_MAX_CONTROLLERS]; /* DEMOPad.c: the frame's controllers (online and replays: as played) */

static int* sOn; /* the second_player setting (declareOn) */

static void declareOn(void) {
    if (sOn == NULL) {
        sOn = portSettingsDeclare("second_player", 0, 0, 1);
    }
}

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
    if (portReplayActive()) {
        /* the recording's controller 2, not this computer's (the playback's input is the replay's) */
        return portReplayPadConnected(1);
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

/* duo moves (duo_pc.c): two players to play their halves */
int portCoopTwoPlayers(void) {
    declareOn();
    return coopOn() && secondPad();
}

static int padHere(int player);
static int sCompanion[2]; /* players 3 and 4's companions (the field's, below) */
static int sOwnFollower;

/* Three or four players in battle: each plays their own character's turns.
 * The partner fighting belongs to whoever has that partner in the field:
 * player 3 or 4 when it's their companion's partner (switched in during the
 * fight, as the battle menu's partner change does), else player 2 (in
 * Brothers mode, player 1: player 2 is Luigi, whose turns are theirs,
 * brothers_pc.c). */
static int partnerOwner(const BattleUnit* unit) {
    const int byDefault = portBrothersOn() ? 0 : 1;
    static const s8 kMember[TYPE_PARTNER_MAX - TYPE_PARTNER_MIN] = {
        PARTY_MEMBER_GOOMBELLA, PARTY_MEMBER_KOOPS,   PARTY_MEMBER_YOSHI,  PARTY_MEMBER_FLURRIE,
        PARTY_MEMBER_VIVIAN,    PARTY_MEMBER_BOBBERY, PARTY_MEMBER_MS_MOWZ};
    int member, k;
    if (unit == NULL || unit->currentKind < TYPE_PARTNER_MIN || unit->currentKind >= TYPE_PARTNER_MAX) {
        return byDefault;
    }
    member = kMember[unit->currentKind - TYPE_PARTNER_MIN];
    for (k = 0; k < 2; k++) {
        int form = sCompanion[k];
        if (k == 0 && !sOwnFollower) {
            /* player 3's character is the story's follower: theirs when it's a partner's form */
            const PartyEntry* follower = partyGetPtr(marioGetExtraPartyId());
            form = follower != NULL ? follower->currentMemberId : 0;
        }
        if (form >= PARTY_MEMBER_GOOMBELLA_FOLLOWER && form <= PARTY_MEMBER_MS_MOWZ_FOLLOWER &&
            form - PARTY_MEMBER_GOOMBELLA_FOLLOWER + PARTY_MEMBER_GOOMBELLA == member && padHere(k + 2)) {
            return k + 2;
        }
    }
    return byDefault;
}

/* the player whose turn it is when the partner has it (1 to 3), else 0 (and
 * 0 in Brothers mode when it's player 1's) */
static int partnerHasTurn(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* unit;
    if (gp == NULL || !gp->inBattle || wp == NULL || wp->activeUnitId < 0) {
        return 0;
    }
    unit = BattleGetUnitPtr(wp, wp->activeUnitId);
    if (unit == NULL || unit->alliance != 0 || unit->currentKind < TYPE_PARTNER_MIN ||
        unit->currentKind == UNIT_MARIO) {
        return 0;
    }
    return partnerOwner(unit);
}

/* battle_ac.c, a guard: an attack on the partner is guarded with its
 * player's controller (its record, kept every frame by BattlePadManager), so
 * each player defends the character they play. Mario's guards stay
 * controller 1's. */
BattleWorkPad* portSecondPlayerGuardPad(BattleUnit* unit) {
    int versus = 0, owner;
    BattleWorkPad* pad = (BattleWorkPad*)portVersusGuardPad(unit, &versus);
    if (versus) {
        return pad; /* a versus fight: the defender's own controller (versus_aurora.c) */
    }
    pad = (BattleWorkPad*)portBrothersGuardPad(unit, secondPad(), &versus);
    if (versus) {
        return pad; /* Brothers mode: Luigi's guards are the second player's (brothers_pc.c) */
    }
    if (!coopOn() || unit == NULL || _battleWorkPointer == NULL || unit->alliance != 0 ||
        unit->currentKind < TYPE_PARTNER_MIN || unit->currentKind == UNIT_MARIO) {
        return NULL;
    }
    owner = partnerOwner(unit);
    if (owner == 0 || !padHere(owner)) {
        return NULL;
    }
    return &_battleWorkPointer->padWork[owner];
}

/* Controller haptics (ac_cue_pc.c): whose controller feels a timed press's
 * cue: the partner's player's (1 to 3) for the partner's guards and, while
 * the partner has the turn, its action commands (as portSecondPlayerRoute
 * reads them); else the first's (0) */
int portSecondPlayerPlayerFor(void* unit, int guard) {
    int owner;
    if (guard) {
        if (portBrothersLuigiUnit(unit)) {
            return portSecondPlayerGuardPad((BattleUnit*)unit) != NULL; /* Brothers mode: Luigi's, player 2's */
        }
        return portSecondPlayerGuardPad((BattleUnit*)unit) != NULL ? partnerOwner((BattleUnit*)unit) : 0;
    }
    if (portBrothersOn() && portBrothersLuigiActs()) {
        return secondPad();
    }
    owner = coopOn() ? partnerHasTurn() : 0;
    return owner > 0 && padHere(owner) ? owner : 0;
}

/* --- The field --- */

#define DEAD_ZONE 14.0f   /* of the stick's 70 */
#define LEASH 220.0f      /* units from Mario: about the screen's reach */
#define REACH 60.0f       /* units: what the partner can talk to or hit */

/* Players 2 to 4 each walk a character of Mario's party (a helper): player 2
 * the partner, player 3 the follower (the story's when there is one, else a
 * companion: another partner, as a follower, chosen for them), player 4 a
 * companion of their own (the port's companion slot, party.c). In Brothers
 * mode player 2 walks Luigi (brothers_pc.c) and the partner just follows
 * Mario: helperHere. */
#define HELPERS 3
typedef struct {
    int free;             /* its player moves it */
    float stickX, stickY; /* its player's stick this frame */
    int jump, interact;   /* A, B pressed this frame */
    int recall;           /* Mario's player pressed Y */
    int letGo;            /* called back: the stick must be let go before it's taken again */
} Helper;
static Helper sHelp[HELPERS];
static struct {
    s32 talkEvt;          /* a talk a helper started (-1 none) */
    NpcEntry* talkNpc;
    int talkBy;           /* the helper talking */
    char map[16];
} sCoop = {-1, NULL, 0, ""};

/* Players 3 and 4's companions: the member each has (a partner's follower
 * form, PARTY_MEMBER_GOOMBELLA_FOLLOWER...), 0 none chosen yet; the
 * follower's slot holds player 3's (spawned here, never the story's); frames
 * of field control in a row (a companion comes once the room has settled).
 * All of it from the game's own state and the players' input: the same in
 * every game of an online session. */
static int sFieldFrames;

s32 portPartyEntryExtra(s32 memberId, f32 x, f32 y, f32 z); /* party.c */
void portPartyKillExtra(void);

/* a player's controller is there (1 the partner's, as secondPad; 2 and 3,
 * players 3 and 4: online, the session's seats; else a controller in that
 * port, or the replay's) */
static int padHere(int player) {
    if (player == 1) {
        return secondPad();
    }
    if (player < 1 || player >= PAD_MAX_CONTROLLERS) {
        return 0;
    }
    if (portOnlineActive()) {
        return player < portOnlinePlayers() && Pad[player].err != PAD_ERR_NO_CONTROLLER;
    }
    if (portReplayActive()) {
        return Pad[player].err != PAD_ERR_NO_CONTROLLER;
    }
#ifdef TTYD_BACKEND_AURORA
    return PADGetIndexForPort(player) >= 0 && Pad[player].err != PAD_ERR_NO_CONTROLLER;
#else
    return 0;
#endif
}

/* a player plays (1 to 3): co-op's, and in Brothers mode player 2 (Luigi's) without it */
static int playerOn(int player) {
    return (coopOn() || (player == 1 && portBrothersOn())) && padHere(player);
}

/* a helper's player is there: in Brothers mode the partner has none (player 2 walks Luigi) */
static int helperHere(int h) {
    return !(h == 0 && portBrothersOn()) && padHere(h + 1);
}

int portCoopPlayers(void) {
    int n = 1, p;
    for (p = 1; p < 4; p++) {
        n += playerOn(p);
    }
    if (portOnlineActive() && portOnlinePlayers() > n) {
        n = portOnlinePlayers();
    }
    return n;
}

int portCoopPlayerHere(int player) {
    return player == 0 || (player > 0 && player < PORT_PLAYERS_MAX && playerOn(player));
}

static PartyEntry* helperParty(int h) {
    if (h == 0) {
        return partyGetPtr(marioGetPartyId());
    }
    if (h == 1) {
        return partyGetPtr(marioGetExtraPartyId());
    }
    return partyGetPtr(PORT_PARTY_SLOT_COMPANION);
}

/* whose helper a party member is (0 the partner...), -1 none */
static int helperOf(PartyEntry* party) {
    int h;
    for (h = 0; party != NULL && h < HELPERS; h++) {
        if (helperParty(h) == party) {
            return h;
        }
    }
    return -1;
}

/* the helper's feet inside the picture (the 608x480 frame, wider in
 * widescreen), a little in from its edges */
static int onScreen(PartyEntry* party) {
    Vec pos = party->position, screen;
    const float edge = 304.0f * portWidescreenEdgeScale() - 12.0f;
    if (portOnlineActive() || portReplayActive()) {
        /* each player's picture may be a different shape (a replay's too: the aspect is the
         * device's): the leash alone, the same in every game */
        return 1;
    }
    getScreenPoint(&pos, &screen);
    return screen.x > -edge && screen.x < edge && screen.y > -228.0f && screen.y < 236.0f;
}

static int fieldControl(void) {
    return gp != NULL && !gp->inBattle && marioStGetSystemLevel() == 0 && marioChkKey() && marioChkCtrl() &&
           yoshiGetStatus() == 0;
}

/* A helper back at Mario's side (the spot it appears at), following him
 * again. */
static void recall(Helper* help, PartyEntry* party) {
    Vec pos;
    help->free = 0;
    help->letGo = 1;
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
    printf("[coop] player %d's character is back at Mario's side\n", (int)(help - sHelp) + 2);
    fflush(stdout);
}

/* B: the nearest NPC within reach's talk (as A in front of it starts it), or
 * else the nearest block or switch's event (as a hit runs it) */
static void interact(int h, PartyEntry* party) {
    NpcWork* nw = npcGetWorkPtr();
    MapObjectWork* mw = portMobjGetWork();
    NpcEntry* npc = NULL;
    MapObjectEntry* mobj = NULL;
    float best = REACH;
    int i;
    for (i = 0; nw != NULL && i < nw->total; i++) {
        NpcEntry* n = &nw->entries[i];
        const float d = hypotf(n->position.x - party->position.x, n->position.z - party->position.z);
        if ((n->flags & NPC_FLAG_USED) && n->talkEvt != NULL && fabsf(n->position.y - party->position.y) < 60.0f &&
            d < best) {
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
        sCoop.talkBy = h;
        printf("[coop] player %d's character talks to '%s'\n", h + 2, npc->description);
        fflush(stdout);
        return;
    }
    best = REACH;
    for (i = 0; mw != NULL && i < mw->count; i++) {
        MapObjectEntry* m = &mw->entries[i];
        const float d = hypotf(m->position.x - party->position.x, m->position.z - party->position.z);
        if ((m->flags & MOBJ_FLAG_USED) && m->event != 0 && m->position.y - party->position.y < 80.0f &&
            m->position.y - party->position.y > -20.0f && d < best) {
            mobj = m;
            best = d;
        }
    }
    if (mobj != NULL) {
        printf("[coop] player %d's character hits '%s'\n", h + 2, mobj->name);
        fflush(stdout);
        mobjRunEvent(mobj, (EvtWord*)mobj->event);
    }
}

/* a helper's talk ended: the NPC back to its own script, the controls back */
static void talkFrame(void) {
    int p;
    if (sCoop.talkEvt < 0) {
        return;
    }
    /* any player reads the text on */
    for (p = 1; p < 4; p++) {
        gp->buttonNew[0] |= gp->buttonNew[p] & (PAD_BUTTON_A | PAD_BUTTON_B);
        gp->button[0] |= gp->button[p] & (PAD_BUTTON_A | PAD_BUTTON_B);
    }
    if (evtCheckID(sCoop.talkEvt)) {
        return;
    }
    if (sCoop.talkNpc != NULL && (sCoop.talkNpc->flags & NPC_FLAG_USED)) {
        evtStartID(sCoop.talkNpc->regularEvtId);
    }
    marioKeyOn();
    partyKeyOn();
    sCoop.talkEvt = -1;
    sCoop.talkNpc = NULL;
}

/* --- players 3 and 4's companions --- */

/* the partners as companions, in the order they're offered */
static const int kCompanionOrder[7] = {PARTY_MEMBER_MS_MOWZ,  PARTY_MEMBER_YOSHI,  PARTY_MEMBER_VIVIAN,
                                       PARTY_MEMBER_FLURRIE,  PARTY_MEMBER_BOBBERY, PARTY_MEMBER_KOOPS,
                                       PARTY_MEMBER_GOOMBELLA};
#define FOLLOWER_FORM(member) ((member) + PARTY_MEMBER_GOOMBELLA_FOLLOWER - PARTY_MEMBER_GOOMBELLA)
#define PARTNER_OF(form) ((form) - PARTY_MEMBER_GOOMBELLA_FOLLOWER + PARTY_MEMBER_GOOMBELLA)

/* a partner on the field now (the partner, the follower or the other companion), as a partner's id */
static int partnerInUse(int member, int exceptHelper) {
    int h;
    for (h = 0; h < HELPERS; h++) {
        const PartyEntry* party = helperParty(h);
        int id;
        if (h == exceptHelper || party == NULL) {
            continue;
        }
        id = party->currentMemberId;
        if (id >= PARTY_MEMBER_GOOMBELLA_FOLLOWER) {
            id = PARTNER_OF(id);
        }
        if (id == member) {
            return 1;
        }
    }
    return 0;
}

/* the companion after this one (0: the first) for a helper (1 player 3's, 2 player 4's): one
 * of the partners not on the field, those Mario has first; the follower form */
static int nextCompanion(int h, int after) {
    int pass, i, start = 0;
    if (after != 0) {
        for (i = 0; i < 7; i++) {
            if (FOLLOWER_FORM(kCompanionOrder[i]) == after) {
                start = i + 1;
            }
        }
    }
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < 7; i++) {
            const int member = kCompanionOrder[(start + i) % 7];
            if (!partnerInUse(member, h) && (pass == 1 || partyChkJoin((PartyId)member))) {
                return FOLLOWER_FORM(member);
            }
        }
    }
    return FOLLOWER_FORM(PARTY_MEMBER_MS_MOWZ);
}

/* player 4's companion out, a little behind Mario */
static void spawnExtra(void) {
    MarioWork* mario = marioGetPtr();
    if (sCompanion[1] == 0) {
        sCompanion[1] = nextCompanion(2, 0);
    }
    /* (at his side: no sines here, whose last bits may differ between computers) */
    if (portPartyEntryExtra(sCompanion[1], mario->position.x - 24.0f, mario->position.y, mario->position.z + 8.0f) >=
        0) {
        printf("[coop] player 4's companion comes (%d)\n", sCompanion[1]);
        fflush(stdout);
    }
}

/* player 3's companion out in the follower's slot (empty: no story follower now or away) */
static void spawnFollower(void) {
    if (sCompanion[0] == 0) {
        sCompanion[0] = nextCompanion(1, 0);
    }
    if (marioPartyEntry((PartyId)sCompanion[0]) >= 0) {
        sOwnFollower = 1;
        printf("[coop] player 3's companion comes (%d)\n", sCompanion[0]);
        fflush(stdout);
    }
}

static void killFollower(void) {
    if (sOwnFollower && partyGetPtr(1) != NULL) {
        partyKill2(1); /* (forgotten: never brought back as the story's) */
    }
    sOwnFollower = 0;
    sHelp[1].free = 0;
}

/* party.c: the story puts its follower out: player 3's companion makes way */
int portCoopFollowerYield(void) {
    if (!sOwnFollower) {
        return 0;
    }
    printf("[coop] player 3's companion makes way for the story's follower\n");
    fflush(stdout);
    killFollower();
    return 1;
}

/* cardmgr.c: the follower a save keeps: never player 3's companion */
int portCoopFollowerSaveId(int memberId) {
    return sOwnFollower && memberId == sCompanion[0] ? 0 : memberId;
}

/* each frame: the companions come (the field settled, their players there) and go (their
 * players gone, co-op off, a battle or another sequence for player 4's) */
static void companionsFrame(void) {
    MarioWork* mario;
    PartyEntry* follower;
    const int field = fieldControl() && sCoop.talkEvt < 0;
    sFieldFrames = field ? sFieldFrames + 1 : 0;
    if (partyGetPtr(PORT_PARTY_SLOT_COMPANION) != NULL && (gp->inBattle || seqGetSeq() != SEQ_GAME || !coopOn() || !padHere(3))) {
        portPartyKillExtra();
        sHelp[2].free = 0;
    }
    mario = marioGetPtr();
    if (mario == NULL || seqGetSeq() != SEQ_GAME) {
        return;
    }
    follower = partyGetPtr(1);
    if (sOwnFollower && follower == NULL && mario->prevPartyMemberId[1] != sCompanion[0]) {
        sOwnFollower = 0; /* (gone, and not coming back by itself: a scene took it) */
    }
    if (sOwnFollower && follower != NULL && follower->currentMemberId != sCompanion[0]) {
        sOwnFollower = 0; /* (the story's now) */
    }
    if (!field) {
        return;
    }
    if (sOwnFollower && follower != NULL && (!coopOn() || !padHere(2))) {
        killFollower(); /* player 3 gone: the companion goes with them */
        return;
    }
    if (coopOn() && padHere(2) && follower == NULL && mario->prevPartyMemberId[1] == 0 && sFieldFrames == 30) {
        spawnFollower();
    }
    if (coopOn() && padHere(3) && partyGetPtr(PORT_PARTY_SLOT_COMPANION) == NULL && sFieldFrames == 40) {
        spawnExtra();
    }
}

/* L or R, players 3 and 4: the next companion (theirs only, not the story's follower) */
static void nextCompanionPressed(int h) {
    if (h == 1 && sOwnFollower && partyGetPtr(1) != NULL) {
        sCompanion[0] = nextCompanion(1, sCompanion[0]);
        partyKill2(1);
        sOwnFollower = 0;
        sHelp[1].free = 0;
        spawnFollower();
    } else if (h == 2 && partyGetPtr(PORT_PARTY_SLOT_COMPANION) != NULL) {
        sCompanion[1] = nextCompanion(2, sCompanion[1]);
        portPartyKillExtra();
        sHelp[2].free = 0;
        spawnExtra();
    }
}

/* makeKey, in the field: each helper's player's input for its walk; player 2's X (the
 * partner's ability: the game's X, held as long as it is held, as for Yoshi's ride or
 * Flurrie's gust) and L or R (the next partner, in the quick-swap's order, with or without
 * the Enhancements tab's setting), players 3 and 4's L or R (their next companion); Mario's
 * Y calls them all back */
static void fieldRoute(void) {
    static u32 sShoulders[HELPERS];
    int h;
    talkFrame();
    if (strncmp(sCoop.map, gp->currentMapName, sizeof(sCoop.map)) != 0) {
        snprintf(sCoop.map, sizeof(sCoop.map), "%s", gp->currentMapName);
        for (h = 0; h < HELPERS; h++) {
            sHelp[h].free = 0; /* a new room: they come in with Mario */
        }
    }
    companionsFrame();
    for (h = 0; h < HELPERS; h++) {
        const int p = h + 1;
        const u32 shoulders = gp->button[p] & (PAD_TRIGGER_L | PAD_TRIGGER_R);
        Helper* help = &sHelp[h];
        if (!helperHere(h)) {
            help->stickX = help->stickY = 0.0f;
            sShoulders[h] = shoulders;
            continue;
        }
        help->stickX = gp->stickX[p];
        help->stickY = gp->stickY[p];
        if (!fieldControl() || sCoop.talkEvt >= 0) {
            sShoulders[h] = shoulders;
            continue;
        }
        help->jump |= (gp->buttonNew[p] & PAD_BUTTON_A) != 0;
        help->interact |= (gp->buttonNew[p] & PAD_BUTTON_B) != 0;
        help->recall |= (gp->buttonNew[0] & PAD_BUTTON_Y) != 0;
        if (h == 0) {
            if (gp->button[p] & PAD_BUTTON_X) {
                gp->button[0] |= PAD_BUTTON_X;
            }
            if (gp->buttonNew[p] & PAD_BUTTON_X) {
                gp->buttonNew[0] |= PAD_BUTTON_X;
            }
            if (gp->buttonUp[p] & PAD_BUTTON_X) {
                gp->buttonUp[0] |= PAD_BUTTON_X;
            }
        }
        if (shoulders & ~sShoulders[h]) {
            if (h == 0) {
                portQuickSwapPressCoop();
            } else {
                nextCompanionPressed(h);
            }
        }
        sShoulders[h] = shoulders;
    }
}

/* party_move.c, a party member's walk mode standing or walking: 1 when its
 * player moves it this frame (in place of following Mario) */
int portCoopPartnerWalk(PartyEntry* party) {
    MarioWork* mario;
    Helper* help;
    float mag, dx, dz;
    const int h = helperOf(party);
    if (!coopOn() || h < 0 || !helperHere(h)) {
        /* (Brothers mode: the second player walks Luigi, brothers_pc.c) */
        return 0;
    }
    help = &sHelp[h];
    mario = marioGetPtr();
    if (party->forceMoveFrames != 0 && help->free) {
        party->forceMoveFrames = 0; /* a hop's (below), landed */
    }
    if (sCoop.talkEvt >= 0 && help->free && sCoop.talkBy == h) {
        /* its own talk: it stands where it is, facing the NPC */
        partySetTargetSpd(party, 0.0f);
        party->moveSpeed = 0.0f;
        party->motionId = 0;
        return 1;
    }
    if (!fieldControl()) {
        if (help->free) {
            recall(help, party); /* a scene, a menu, Yoshi's ride: with Mario */
        }
        help->jump = help->interact = help->recall = 0;
        return 0;
    }
    mag = hypotf(help->stickX, help->stickY);
    if (mag <= DEAD_ZONE) {
        help->letGo = 0;
    }
    if (mag > DEAD_ZONE && !help->free && !help->letGo) {
        help->free = 1;
        party->moveMode = 1; /* walk mode: its jump, fall and step motions for the hops */
        printf("[coop] player %d takes their character\n", h + 2);
        fflush(stdout);
    }
    if (help->recall) {
        help->recall = 0;
        if (help->free) {
            recall(help, party);
            return 1;
        }
    }
    if (!help->free) {
        help->jump = help->interact = 0;
        return 0;
    }
    dx = party->position.x - mario->position.x;
    dz = party->position.z - mario->position.z;
    if (!onScreen(party) || hypotf(dx, dz) > LEASH || party->position.y < mario->position.y - 200.0f ||
        (party->flags & PARTY_FLAG_FORCE_FALL)) {
        recall(help, party); /* off the screen, far, fallen or in deep water */
        return 1;
    }
    if (mag > DEAD_ZONE) {
        /* as marioGetStick turns Mario's stick: the camera's yaw added */
        const float m = mag > 70.0f ? 70.0f : mag;
        party->targetDir = revise360(angleABf(0.0f, 0.0f, help->stickX, -help->stickY) + mario->targetYaw);
        partySetTargetSpd(party, (m - DEAD_ZONE) / (70.0f - DEAD_ZONE) * (mario->baseDashSpeed > 0.0f
                                                                              ? mario->baseDashSpeed
                                                                              : 4.0f));
        party->motionId = 1;
    } else {
        partySetTargetSpd(party, 0.0f);
        party->moveSpeed = 0.0f;
        party->motionId = 0;
    }
    if (help->interact) {
        help->interact = 0;
        if (sCoop.talkEvt < 0) {
            interact(h, party);
        }
    }
    if (help->jump) {
        help->jump = 0;
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
 * over the partner, "3P" (green) and "4P" (yellow) over the companions,
 * faint while they follow Mario */
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
    static const GXColor kColors[HELPERS] = {{0x6C, 0xC8, 0xFF, 0xFF}, {0x7C, 0xE0, 0x6C, 0xFF}, {0xFF, 0xD8, 0x50, 0xFF}};
    static const char* kTags[HELPERS] = {"2P", "3P", "4P"};
    MarioWork* mario = marioGetPtr();
    const u32 seed = portRandState();
    Vec top;
    int h;
    (void)camId;
    (void)param;
    top = mario->position;
    top.y += 52.0f; /* above his cap */
    tag(&top, "1P", (GXColor){0xFF, 0x70, 0x60, 0xFF});
    for (h = 0; h < HELPERS; h++) {
        /* (Brothers mode: "2P" over Luigi, bright while player 2 walks him) */
        const int luigi = h == 0 && portBrothersOn();
        PartyEntry* party = luigi ? portBrothersFieldLuigi() : helperParty(h);
        GXColor color = kColors[h];
        if (party == NULL || !(luigi ? padHere(1) : coopOn() && helperHere(h))) {
            continue;
        }
        top.x = party->position.x + party->dispOfs.x;
        top.y = party->position.y + party->dispOfs.y + party->height + 8.0f;
        top.z = party->position.z + party->dispOfs.z;
        color.a = (luigi ? portBrothersDriven() : sHelp[h].free) ? 0xFF : 0x70;
        tag(&top, kTags[h], color);
    }
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
    static int sHad = 0, sHinted, sWasOn, sHadMore[2];
    const int has = secondPad();
    int k;
    if (portOnlineActive()) {
        sHad = sWasOn = 1; /* online: player 2 is the guest, its notices online_pc.c's */
    }
    if (fieldControl() && *sOn && !sWasOn && !has) {
        notice("Co-op is on: press A on another controller to join.");
    }
    if (fieldControl()) {
        sWasOn = *sOn;
    }
    if (fieldControl() && has != sHad && portBrothersOn()) {
        /* Brothers mode: player 2 is Luigi */
        notice(has ? "Player 2 joined: their stick walks Luigi." : "Player 2 left: Luigi follows Mario.");
        sHad = has;
    } else if (fieldControl() && has != sHad) {
        if (has && *sOn) {
            notice("Player 2 joined: their stick walks the partner.");
        } else if (has && !sHinted) {
            sHinted = 1;
            notice("A second controller: turn on Second player (co-op) in Multiplayer to play the partner.");
        } else if (!has && *sOn) {
            notice("Player 2 left: the partner follows Mario.");
        }
        sHad = has;
    }
    for (k = 0; k < 2; k++) {
        /* players 3 and 4 (locally; online, the seats: their names in the Online tab) */
        const int more = coopOn() && padHere(k + 2);
        if (fieldControl() && more != sHadMore[k]) {
            if (!portOnlineActive()) {
                notice(more ? (k == 0 ? "Player 3 joined: their stick walks a companion (L or R: another)."
                                      : "Player 4 joined: their stick walks a companion (L or R: another).")
                            : (k == 0 ? "Player 3 left." : "Player 4 left."));
            } else if (!more) {
                notice(k == 0 ? "Player 3 left." : "Player 4 left.");
            }
            sHadMore[k] = more;
        }
    }
    /* (an online game can end before the logos load the font: the notice waits for it) */
    if (sNotice.text != NULL && pfh != NULL) {
        if (++sNotice.age >= NOTICE_FRAMES) {
            sNotice.text = NULL;
        } else if (!gp->inBattle) {
            dispEntry(CAMERA_2D, 903.0f, 1, noticeDisp, NULL);
        }
    }
    if (portCoopPlayers() > 1 && fieldControl()) {
        dispEntry(CAMERA_2D, 902.0f, 1, tagDisp, NULL);
    }
}

void portSecondPlayerRoute(void) {
    declareOn();
    if (gp == NULL) {
        return;
    }
    seenFrame();
    portBrothersDuoFrame(secondPad()); /* (Brothers mode's Bros. moves, duo moves' players) */
    if (portVersusRoute()) {
        return; /* a versus fight routes the controllers itself (versus_aurora.c) */
    }
    if (portBrothersRoute(secondPad())) {
        return; /* Brothers mode, Luigi's turn: the second player plays it (brothers_pc.c) */
    }
    if (!coopOn()) {
        if (partyGetPtr(PORT_PARTY_SLOT_COMPANION) != NULL || sOwnFollower) {
            fieldRoute(); /* (co-op ended: the companions go, companionsFrame) */
        }
        return;
    }
    {
        const int owner = partnerHasTurn();
        if (owner == 0 || !padHere(owner)) {
            fieldRoute(); /* (no player for the partner's turn: the first one plays it) */
            return;
        }
        if (portDuoActive()) {
            return; /* a duo move: each player's controller plays their own half (duo_pc.c) */
        }
        gp->button[0] = gp->button[owner];
        gp->buttonNew[0] = gp->buttonNew[owner];
        gp->buttonRepeat[0] = gp->buttonRepeat[owner];
        gp->buttonRepeatDelay[0] = gp->buttonRepeatDelay[owner];
        gp->buttonUp[0] = gp->buttonUp[owner];
        gp->dirs[0] = gp->dirs[owner];
        gp->dirsNew[0] = gp->dirsNew[owner];
        gp->dirsRepeat[0] = gp->dirsRepeat[owner];
        gp->dirsRepeatDelay[0] = gp->dirsRepeatDelay[owner];
        gp->stickX[0] = gp->stickX[owner];
        gp->stickY[0] = gp->stickY[owner];
        gp->substickX[0] = gp->substickX[owner];
        gp->substickY[0] = gp->substickY[owner];
        gp->triggerLeft[0] = gp->triggerLeft[owner];
        gp->triggerRight[0] = gp->triggerRight[owner];
    }
}
