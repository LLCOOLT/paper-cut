/* A second player for the partner (Multiplayer > Second player;
 * port/settings/features/second_player.cpp). Nothing changes until another
 * controller's Start joins (a controller is player 1's until then: localSeated).
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
 * partner back to his side with Y. At the picture's edge it is held there
 * (the camera fits everyone it can: coop_camera_pc.c); left outside it when
 * Mario walks on, it walks back toward him. It comes back to his side by
 * itself only when it falls (a pit, deep water), gets stuck, a scene starts
 * or the room changes. An online guest's character has the whole room
 * (portCoopRoams): only a fall, being stuck or a scene bring it back. Until
 * the second player moves, the partner follows Mario as always. Moving
 * Mario, menus and Mario's guards stay controller 1's.
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
#include "battle/battle_break_slot.h"
#include "battle/battle_unit.h"
#include "driver/animdrv.h"
#include "driver/itemdrv.h"
#include "driver/mobjdrv.h"
#include "driver/windowdrv.h"
#include "driver/npcdrv.h"
#include "mot/mot_stay.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mario_sbr.h"
#include "mario/mariost.h"
#include "party.h"
#include "party/party_hit.h"
#include "party/party_motion.h"
#include "party/party_move.h"
#include "version.h"
#include "party/party_yoshi.h"
#include "port/gameplus.h"
#include "port/text_fit.h"
#include "port/settings.h"
#include "port/tips.h"
#include "system.h"
#include "wide_x.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "manager/fontmgr.h"
#include "port/tr.h"
#include "port/statesync.h"
#include "port/replay.h"
#include "port/platform.h"
#include "port/view.h"
#include "port/versus.h"
#include "port/duo.h"
#include "port/players.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "data/party_data.h"
#include "port/brothers.h"
#include "port/dungeon_master.h"
#include "port/player_menu.h"
#include "port/pm64.h"
#include "port/coop_camera.h"
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
        sOn = portSettingsDeclare("second_player", 1, 0, 1); /* (on: nothing changes until a second controller joins) */
    }
}

/* co-op is on: the setting, or a shared online battle runs (player 2 is the
   other game's, port/statesync.h) */
/* players on this computer who joined by their Start (port/player_menu.h portCoopJoinStart) or were picked
   for a seat on purpose (portCoopSeatChosen: Choose player 2's controller, the Controllers page), by
   controller port. A real controller plays only so: SDL gives every controller that connects the next free
   port by itself (a handheld's built-in controls port 1, a paired Bluetooth one the next), and being there
   never makes it a player */
static int sJoined[PAD_MAX_CONTROLLERS];

static int anyJoined(void) {
    return sJoined[1] || sJoined[2] || sJoined[3];
}

/* a controller with a device in that port (Aurora's: SDL put it there), not an input with none (the keyboard
   or touch controls a seat swap took there, a test's scripted controller) */
static int deviceAt(int player) {
#ifdef TTYD_BACKEND_AURORA
    return PADGetIndexForPort((u32)player) >= 0;
#else
    (void)player;
    return 0;
#endif
}

static int coopOn(void) {
    /* (online: a seated guest still linked counts while its game loads a room, its input stale for a moment) */
    return (sOn != NULL && *sOn) || anyJoined() || portStateSyncSharedBattle() || portStateSyncCoopHost() ||
           portStateSyncCoopSeatTaken(0) || portStateSyncCoopLocalSeat() != 0;
}

/* a controller on this computer in that port (1-3) plays: one that joined by its Start or was picked for the
   seat; with the setting (or Brothers mode, Luigi's seat) also an input with no device there (the keyboard a
   seat swap took there, a test's scripted controller). Never a controller only for being there: a
   handheld's built-in controls and the first controller paired are player 1's, a second player joins with
   Start (docs/PLAYERS.md) */
/* a controller in a port whose seat is kept for another (holdFrame): not the one that left, so it plays only
   once its own Start joins (portCoopJoinStart) */
static int sStranger[PAD_MAX_CONTROLLERS];

static int localSeated(int player) {
    return (sJoined[player] && !sStranger[player]) ||
           (((sOn != NULL && *sOn) || (player == 1 && portBrothersSetting())) && !deviceAt(player));
}

int portInputUnplugged(int port); /* input_pc.c: $TTYD_PAD_UNPLUG, a test's controller pulled out */
void portInputReplug(int port);

/* frames left of a seat kept for a controller that disconnected (port 1-3; holdFrame) */
static int sHeld[PAD_MAX_CONTROLLERS];
/* the port's player was seated last frame (holdFrame: one gone since has disconnected) */
static int sWas[PAD_MAX_CONTROLLERS];
/* the seated controller's device (portPadIdentity) as last seen, and the kept seat's: "" none (an input with no
   device: the keyboard, touch, a test's scripted controller; anything back in the port is theirs) */
static char sSeen[PAD_MAX_CONTROLLERS][96];
static char sHeldId[PAD_MAX_CONTROLLERS][96];

static void padIdentity(int port, char* out, int size) {
#ifdef TTYD_BACKEND_AURORA
    portPadIdentity(port, out, size);
#else
    (void)port;
    if (size > 0) {
        out[0] = '\0';
    }
#endif
}

/* a seat (1-3) given on purpose (port/include/port/players.h): that port's controller plays */
void portCoopSeatChosen(int port) {
    if (port >= 1 && port < PAD_MAX_CONTROLLERS) {
        sJoined[port] = 1;
        sHeld[port] = 0;
        sStranger[port] = 0;
        printf("[coop] seat %d given on purpose: its controller plays\n", port + 1);
        fflush(stdout);
    }
}

/* a seat (1-3) let go on purpose (Let player 2 go, Not playing): free now, no seat kept, and the next
   controller SDL puts in that port plays only once it presses Start */
void portCoopSeatLetGo(int port) {
    if (port >= 1 && port < PAD_MAX_CONTROLLERS) {
        sJoined[port] = 0;
        sHeld[port] = 0;
        sWas[port] = 0;
        sStranger[port] = 0;
    }
}

/* a controller is in that port (1-3) on this computer, seated or not: a controller, or an input with no
 * device index there (the keyboard and touch controls a seat swap took there, or $TTYD_P2_PRESS... a
 * test's scripted controller, wherever a swap took it; input_pc.c's portInputHasAny) */
static int rawPad(int player) {
#ifdef TTYD_BACKEND_AURORA
    if (player < 1 || player >= PAD_MAX_CONTROLLERS || portInputUnplugged(player)) {
        return 0;
    }
    if (player == 1) {
        return portInputHasAny(1);
    }
    return portInputHasAny(player) && Pad[player].err != PAD_ERR_NO_CONTROLLER;
#else
    if (player < 1 || player >= PAD_MAX_CONTROLLERS || portInputUnplugged(player)) {
        return 0;
    }
    if (player == 1) {
        return 1;
    }
    /* (controllers 3 and 4: a test's scripted ones, $TTYD_P3_PRESS / $TTYD_P4_PRESS or an input script's
       "pad 3" / "pad 4" from its first press: input_pc.c's portInputVirtualAt) */
    return portInputHasAny(player) && Pad[player].err != PAD_ERR_NO_CONTROLLER;
#endif
}

/* a second controller (or $TTYD_P2_PRESS's, tests: input_pc.c; in a shared online battle,
 * the other game's) */
static int secondPad(void) {
    if (portStateSyncSharedBattle()) {
        /* its second place's game (or, that player gone, the first's: ssBattleApplyPad); none when the battle
           is player 1's alone (shared for whoever comes in later): the partner is then player 1's to play,
           never a turn waiting on a controller nobody has */
        return portStateSyncBattlePlayers() > 1;
    }
    if (portStateSyncCoopPadHere(2) || portStateSyncCoopLocalSeat() == 2) {
        /* (playing together online: the guest in seat 2's controller, statesync_coop_pc.inc; or this guest's
           own, walking its own character here) */
        return 1;
    }
    if (portReplayActive()) {
        /* the recording's controller 2, not this computer's (the playback's input is the replay's) */
        return portReplayPadConnected(1);
    }
    return rawPad(1) && localSeated(1);
}

/* duo moves (duo_pc.c): two players to play their halves */
int portCoopTwoPlayers(void) {
    declareOn();
    return coopOn() && secondPad();
}

static int padHere(int player);
static int sCompanion[2]; /* players 3 and 4's companions (the field's, below) */
static int sOwnFollower;
static int sLent2; /* player 2's lent partner while Mario has none (a follower form, 0 none chosen) */
/* each helper's (HELPERS: 0 player 2's lent partner, 1 player 3's companion, 2 player 4's) Paper Mario 64 pick
 * on the Character strip (port/pm64.h): a PM64 partner drawn on its host's companion (a copy: it takes nobody's
 * partner, and two of the same may be out), -1 the TTYD partner's own look picked, 0 the party's look. From the
 * players' input alone, as the companions are (the same in every game online); portPm64SlotLook draws it. */
static int sPm64Look[3];
/* a shared battle of "sync what matters" (port/statesync.h): its players 3 and 4's companions,
 * the same in every game (A's choice: the partner each had out in their own game, if A's party
 * has it, else another), in place of this game's field's */
static int sBattleForm[2];
static int sBattleSet;

void portCoopBattleCompanions(int players, const int* forms) {
    sBattleSet = players > 0 && forms != NULL;
    sBattleForm[0] = sBattleSet && players > 2 ? forms[0] : 0;
    sBattleForm[1] = sBattleSet && players > 3 ? forms[1] : 0;
}

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
        if (sBattleSet && portStateSyncSharedBattle()) {
            form = sBattleForm[k]; /* (a shared battle: its own, every game's alike) */
        } else if (k == 0 && !sOwnFollower) {
            /* player 3's character is the story's follower: theirs when it's a partner's form */
            const PartyEntry* follower = partyGetPtr(marioGetExtraPartyId());
            form = follower != NULL ? follower->currentMemberId : 0;
        }
        if (sPm64Look[k + 1] > 0 && !(sBattleSet && portStateSyncSharedBattle())) {
            /* (not in a shared battle: a look picked on one computer only would split who plays the partner) */
            continue; /* (a Paper Mario 64 partner's copy: the turns stay the party's partner's) */
        }
        if (form >= PARTY_MEMBER_GOOMBELLA_FOLLOWER && form <= PARTY_MEMBER_MS_MOWZ_FOLLOWER &&
            form - PARTY_MEMBER_GOOMBELLA_FOLLOWER + PARTY_MEMBER_GOOMBELLA == member && padHere(k + 2)) {
            return k + 2;
        }
    }
    return byDefault;
}

int portSecondPlayerPartnerOwner(const void* unit) {
    return partnerOwner((const BattleUnit*)unit);
}

/* the player whose turn it is when the partner has it (1 to 3), else 0 (and
 * 0 in Brothers mode when it's player 1's) */
static int partnerHasTurn(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* unit;
    if (gp == NULL || !gp->inBattle || wp == NULL || wp->activeUnitId < 0) {
        return 0;
    }
    if (wp->seqEndValue != 0) {
        /* the battle's end (the level-up's choice, the results) is player 1's, whoever acted last: with the
         * partner's blow the last, the level-up read player 2's controller, and with none there (headless
         * counts one) it waited for ever (the full run, hei_02's first level-up) */
        return 0;
    }
    if (BattleGetSeq(wp, SEQ_UNKNOWN) == 2 && BattleGetSeq(wp, SEQ_MOVE) != 0x5000009) {
        /* between actions (the phases' events, starts and ends: btlseqMove) the last actor is still
         * activeUnitId, but nobody has the turn: with the partner's blow the last, a boss's phase-event
         * message (Beldam's btl_majo3_06) read player 2's controller and, with none there, waited for ever
         * (the full run, win_00's Shadow Sirens) */
        return 0;
    }
    if (BattleGetSeq(wp, SEQ_ACT) >= 0x600000A) {
        /* the act is over (btlseqAct's audience reaction, the BINGO's spin, the status messages, the act's
         * end event): nothing left of it is the partner's. With its blow the last, the reels read player 2's
         * controller and waited for ever on the last one (the full run, gra_01's Hyper Paragoombas and
         * Grodus: "reels 10 10 8"); everyonesAB below lets any seated player's A stop them too */
        return 0;
    }
    unit = BattleGetUnitPtr(wp, wp->activeUnitId);
    if (unit == NULL || unit->alliance != 0 || unit->currentKind < TYPE_PARTNER_MIN ||
        unit->currentKind == UNIT_MARIO) {
        return 0;
    }
    return partnerOwner(unit);
}

/* the controller (1-3) the partner's turn reads in battle in player 1's stead (portSecondPlayerRoute),
 * else 0: the story test's auto-text presses A there too (story_test_aurora.c) */
int portSecondPlayerMessagePad(void) {
    int owner;
    declareOn();
    if (!coopOn() || portVersusOn() || portBrothersOn()) {
        return 0;
    }
    owner = partnerHasTurn();
    return owner > 0 && padHere(owner) ? owner : 0;
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
        /* Brothers mode: Luigi's guards are the second player's (brothers_pc.c); the first's
         * while the second plays the enemies (dungeon master: nobody guards their own hits) */
        return portDmPlaysEnemies(1) ? NULL : pad;
    }
    if (!coopOn() || unit == NULL || _battleWorkPointer == NULL || unit->alliance != 0 ||
        unit->currentKind < TYPE_PARTNER_MIN || unit->currentKind == UNIT_MARIO) {
        return NULL;
    }
    owner = partnerOwner(unit);
    if (owner == 0 || !padHere(owner) || portDmPlaysEnemies(owner)) {
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
#define REACH 60.0f       /* units: what the partner can talk to or hit */
#define TUG_EXCESS 12.0f  /* outside the picture by this much (portCoopEdgeExcess): walked back toward Mario */
#define TUG_STUCK 150     /* frames walked back without getting nearer: stuck, back at Mario's side */
#define HOP_REACH 40.0f   /* units a hop carries it: no hop out of the picture */

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
    float tugBest;        /* walked back toward Mario: the nearest it got */
    int tugFrames;        /* ... and the frames since it got nearer */
    int held;             /* held at the picture's edge (logged once a time) */
    int hopping;          /* frames since its hop began (0 none): the jump motion has it until it lands */
    float hopY;           /* where the hop left from (the floor's height) */
    const void* entry;    /* the character it walks (helperSync): its party entry and member */
    int member;
    int seated;           /* its player plays it (helperHere) */
    int idle;             /* frames its player has left the stick and buttons alone (ownStayPose's sleep) */
    int sniff;            /* Ms. Mowz's sniff where she stands: frames into its 60-frame cycle (0 none) */
    int act;              /* an online guest's own: frames its action's pose holds it still (a talk, the move) */
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
s32 portPartyEntryExtraAt(s32 slot, s32 memberId, f32 x, f32 y, f32 z);
void portPartyKillExtraAt(s32 slot);

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
    if (portStateSyncSharedBattle()) {
        /* a shared battle of "sync what matters": its players (each their own game; the battle's
           controllers cross, statesync_battle_pc.c) */
        return player < portStateSyncBattlePlayers();
    }
    if (portStateSyncCoopPadHere(player + 1) || portStateSyncCoopLocalSeat() == player + 1) {
        return 1; /* (playing together online: that seat's guest's controller; or this guest's own) */
    }
    if (portReplayActive()) {
        return Pad[player].err != PAD_ERR_NO_CONTROLLER;
    }
    /* (tests: $TTYD_P3_PRESS / $TTYD_P4_PRESS's scripted controllers, input_pc.c, count as there on every
       backend; headless too: its tests of players 3 and 4) */
    return rawPad(player) && localSeated(player);
}

/* the player's seat is theirs: their controller there, or kept while it is away (holdFrame): their
 * companion stays and nobody else takes it, but nothing is read from them. Online, the same for a seated guest
 * still linked whose input is stale (its game a step behind, loading a room): only the link's end frees it */
static int seatKept(int player) {
    /* (a co-op guest's own seat is its own while it plays together: its companion stays while the host holds it) */
    return padHere(player) || (player >= 1 && player < PAD_MAX_CONTROLLERS && sHeld[player] > 0) ||
           (player >= 1 && player < PAD_MAX_CONTROLLERS && portStateSyncCoopSeatTaken(player + 1)) ||
           (player >= 2 && player < PAD_MAX_CONTROLLERS && portStateSyncCoopGuest() &&
            portStateSyncSeatOf(portStateSyncMe()) == player + 1);
}

static int anyHeld(void) {
    return sHeld[1] > 0 || sHeld[2] > 0 || sHeld[3] > 0;
}

int portCoopSeatHeld(int port) {
    return port >= 1 && port < PAD_MAX_CONTROLLERS ? sHeld[port] : 0;
}

/* the player menu's join rule (port/include/port/player_menu.h) */
int portCoopUnseated(int port) {
    if (port < 1 || port >= PAD_MAX_CONTROLLERS || portStateSyncOn() || portReplayActive()) {
        return 0;
    }
    return rawPad(port) && !padHere(port);
}

int portCoopJoinStart(int port) {
    int seat = -1, p;
    if (!portCoopUnseated(port)) {
        return -1;
    }
    for (p = 1; p < PAD_MAX_CONTROLLERS && seat < 0; p++) {
        if (p != port && sHeld[p] > 0) {
            seat = p; /* (a seat kept for a controller that disconnected: this one takes it back) */
        }
    }
    for (p = 1; p < PAD_MAX_CONTROLLERS && seat < 0; p++) {
        if (p == port || !padHere(p)) {
            seat = p; /* (the next free seat: the lowest port nobody plays) */
        }
    }
#ifdef TTYD_BACKEND_AURORA
    if (seat != port) {
        /* this controller to that port; an unseated one there (if any) to this one */
        const s32 mine = PADGetIndexForPort((u32)port), there = PADGetIndexForPort((u32)seat);
        if (mine >= 0) {
            PADClearPort((u32)port);
            PADClearPort((u32)seat);
            PADSetPortForIndex((u32)mine, (u32)seat);
            if (there >= 0) {
                PADSetPortForIndex((u32)there, (u32)port);
            }
            PADSerializeMappings();
        } else {
            seat = port; /* (a test's scripted controller stays in its port) */
        }
    }
#else
    seat = port;
#endif
    sJoined[seat] = 1;
    sStranger[seat] = 0;
    sHeldId[seat][0] = '\0'; /* (a kept seat taken by Start: this controller's now, holdFrame's "back") */
    if (seat != port) {
        sJoined[port] = 0;
        sStranger[port] = 0;
    }
    printf("[coop] controller %d pressed Start: player %d joins\n", port + 1, seat + 1);
    fflush(stdout);
    return seat;
}

/* a player plays (1 to 3): co-op's, and in Brothers mode player 2 (Luigi's) without it */
static int playerOn(int player) {
    return (coopOn() || (player == 1 && portBrothersOn())) && padHere(player);
}

/* a helper's player is there: in Brothers mode the partner has none (player 2 walks Luigi) */
static int helperHere(int h) {
    return !(h == 0 && portBrothersOn()) && padHere(h + 1);
}

/* the same, or the helper's seat kept while its controller is away (its character waits at Mario's side) */
static int helperKept(int h) {
    return !(h == 0 && portBrothersOn()) && seatKept(h + 1);
}

int portCoopPlayers(void) {
    int n = 1, p;
    for (p = 1; p < 4; p++) {
        n += playerOn(p);
    }
    return n;
}

int portCoopPlayerHere(int player) {
    return player == 0 || (player > 0 && player < PORT_PLAYERS_MAX && playerOn(player));
}

static PartyEntry* helperParty(int h);

/* the player menu's Your character (port/include/port/player_menu.h) */
int portCoopCharacterOf(int port) {
    PartyEntry* party;
    int id;
    if (port >= 1 && port <= HELPERS &&
        (id = portStateSyncSeatCharacter(port + 1, NULL)) != PORT_STATESYNC_NO_CHARACTER) {
        return id; /* (a co-op guest: the host's game's, where the follower and the companion are) */
    }
    if (port == 1 && portBrothersOn()) {
        return -1;
    }
    if (port < 1 || port > HELPERS || (party = helperParty(port - 1)) == NULL) {
        return 0;
    }
    id = party->currentMemberId;
    if (id >= PARTY_MEMBER_GOOMBELLA_FOLLOWER && id <= PARTY_MEMBER_MS_MOWZ_FOLLOWER) {
        id = id - PARTY_MEMBER_GOOMBELLA_FOLLOWER + PARTY_MEMBER_GOOMBELLA;
    }
    return id;
}

static PartyEntry* helperParty(int h) {
    if (marioGetPtr() == NULL) {
        return NULL; /* (no Mario yet: the launcher's Settings > Multiplayer names the seats before the game boots) */
    }
    if (h == 0) {
        /* the partner, else (Mario has none yet) player 2's lent one */
        PartyEntry* partner = partyGetPtr(marioGetPartyId());
        return partner != NULL ? partner : partyGetPtr(PORT_PARTY_SLOT_LENT);
    }
    if (h == 1) {
        return partyGetPtr(marioGetExtraPartyId());
    }
    return partyGetPtr(PORT_PARTY_SLOT_COMPANION);
}

/* Each frame, before a helper's input: its player's state belongs to one character. Another one now (a
 * swap: the game's quick swap, the partner menu, a scripted party change; a partner joining or leaving; a
 * lent one coming or going), or its player not playing it now (Brothers mode on or off, their seat left),
 * starts it afresh: not taken (free 0, so the next stick takes the new character into walk mode), no hop
 * running, nothing pressed waiting. Without it a swapped-in partner kept "free" from the last one and never
 * got walk mode: player 2 could neither walk nor hop it. */
static void helperSync(int h) {
    Helper* help = &sHelp[h];
    PartyEntry* party = helperParty(h);
    const int member = party != NULL ? party->currentMemberId : 0;
    const int seated = helperHere(h);
    if ((const void*)party == help->entry && member == help->member && seated == help->seated) {
        return;
    }
    if (help->free || help->hopping) {
        printf("[coop] player %d's character changed (member %d to %d%s): theirs to take again\n", h + 2,
               help->member, member, seated ? "" : ", not played now");
        fflush(stdout);
    }
    help->entry = party;
    help->member = member;
    help->seated = seated;
    help->free = 0;
    help->letGo = 0;
    help->hopping = 0;
    help->held = 0;
    help->jump = help->interact = help->recall = 0;
    help->tugBest = 1e9f;
    help->tugFrames = 0;
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

static int fieldControl(void) {
    /* (a co-op guest walking its own character: its Mario's keys are off, the host's field is free) */
    return gp != NULL && !gp->inBattle && marioStGetSystemLevel() == 0 &&
           ((marioChkKey() && marioChkCtrl()) || portStateSyncCoopLocalSeat() != 0) && yoshiGetStatus() == 0;
}

/* helper h is this co-op guest's own character, walked here: what it does to the world (a talk, a block, the
   partner's move, a swap) is the host's game's, from the controller the guest sends */
static int guestOwn(int h) {
    return portStateSyncCoopLocalSeat() == h + 2;
}

int portCoopHelperFree(int h) {
    return h >= 0 && h < HELPERS && sHelp[h].free;
}

void portCoopHelperSet(int h, int free) {
    PartyEntry* party;
    if (h < 0 || h >= HELPERS) {
        return;
    }
    sHelp[h].free = free != 0;
    sHelp[h].letGo = 0;
    sHelp[h].hopping = 0;
    sHelp[h].jump = sHelp[h].interact = sHelp[h].recall = 0;
    party = helperParty(h);
    if (free && party != NULL) {
        party->forceMoveFrames = 0;
        if (party->runMode == 1) {
            partyChgRunMode(party, 2);
        }
        party->moveMode = 1;
        partyChgMot(party, 0);
    }
}

/* the couch's camera (coop_camera_pc.c): Luigi and the helpers this computer's players walk now */
int portCoopFramePoints(float (*out)[3], int max) {
    PartyEntry* luigi;
    int n = 0, h;
    if (gp == NULL || gp->inBattle || seqGetSeq() != SEQ_GAME || !fieldControl()) {
        return 0; /* (a talk, a scene: the game's own camera, on Mario) */
    }
    luigi = portBrothersOn() ? portBrothersFieldLuigi() : NULL;
    if (luigi != NULL && !portCoopRoams(1) && n < max) {
        out[n][0] = luigi->position.x, out[n][1] = luigi->position.y, out[n][2] = luigi->position.z;
        n++;
    }
    for (h = 0; coopOn() && h < HELPERS && n < max; h++) {
        PartyEntry* party = helperParty(h);
        if (!helperHere(h) || !sHelp[h].free || portCoopRoams(h + 1) || party == NULL ||
            (party->flags2 & PARTY_DISP_FLAG_HIDDEN)) {
            continue;
        }
        out[n][0] = party->position.x, out[n][1] = party->position.y, out[n][2] = party->position.z;
        n++;
    }
    return n;
}

/* Mario's player has no control now (a talk, a scene, a menu): whether helper h, walked by its
 * player, stays where it is (else it comes back to Mario's side: a scene expects the partner there) */
static int holdsThrough(int h) {
    if (gp != NULL && !gp->inBattle && marioChkKey() && marioChkCtrl() && yoshiGetStatus() == 0) {
        return 1; /* a menu, an item, a save block (the system level): the field waits meanwhile */
    }
    if (sCoop.talkEvt >= 0 || marioGetPtr()->currMotionId == MARIO_MOTION_TALK) {
        return 1; /* a talk, Mario's or a player's: it stands where it is */
    }
    return h == 2 || (h == 1 && sOwnFollower); /* a companion of the port's own: no scene moves it */
}

/* A helper back at Mario's side (the spot it appears at), following him
 * again. */
static void recall(Helper* help, PartyEntry* party) {
    Vec pos;
    help->free = 0;
    help->letGo = 1;
    help->tugBest = 1e9f;
    help->tugFrames = 0;
    portCoopFallReset((int)(help - sHelp) + 1); /* (online, the host: its guest takes this place) */
    portStateSyncCoopSeatMoved((int)(help - sHelp) + 2, "back at Mario's side");
    if (party == NULL) {
        return;
    }
    if (partyGetAppearPos(party, &pos)) {
        party->position = pos;
    } else {
        party->position = marioGetPtr()->position;
    }
    party->forceMoveFrames = 0;
    /* (a hop or fall it was in ends here: nothing left "jumping", which the follow walk would fly on with) */
    party->flags &= ~(PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING | PARTY_FLAG_IS_STEPPING);
    party->velY = 0.0f;
    party->accelY = 0.0f;
    help->hopping = 0;
    partySetTargetSpd(party, 0.0f);
    partyChgMot(party, 0); /* (the follow motions set its pose from here) */
    partyClearFootmark();
    printf("[coop] player %d's character is back at Mario's side\n", (int)(help - sHelp) + 2);
    fflush(stdout);
}

/* what B would work from (x, y, z), within reach: the nearest NPC with a talk, else the nearest block or switch
 * with an event (either NULL) */
static void interactFind(float x, float y, float z, float reach, NpcEntry** npcOut, MapObjectEntry** mobjOut) {
    NpcWork* nw = npcGetWorkPtr();
    MapObjectWork* mw = portMobjGetWork();
    NpcEntry* npc = NULL;
    MapObjectEntry* mobj = NULL;
    float best = reach;
    int i;
    for (i = 0; nw != NULL && i < nw->total; i++) {
        NpcEntry* n = &nw->entries[i];
        const float d = hypotf(n->position.x - x, n->position.z - z);
        if ((n->flags & NPC_FLAG_USED) && n->talkEvt != NULL && fabsf(n->position.y - y) < 60.0f && d < best) {
            npc = n;
            best = d;
        }
    }
    best = reach;
    for (i = 0; npc == NULL && mw != NULL && i < mw->count; i++) {
        MapObjectEntry* m = &mw->entries[i];
        const float d = hypotf(m->position.x - x, m->position.z - z);
        if ((m->flags & MOBJ_FLAG_USED) && m->event != 0 && m->position.y - y < 80.0f && m->position.y - y > -20.0f &&
            d < best) {
            mobj = m;
            best = d;
        }
    }
    *npcOut = npc;
    *mobjOut = mobj;
}

/* helper h's talk with this NPC, started (as A in front of it starts Mario's) */
static void helperTalk(int h, PartyEntry* party, NpcEntry* npc) {
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
}

/* B: the nearest NPC within reach's talk (as A in front of it starts it), or
 * else the nearest block or switch's event (as a hit runs it) */
static void interact(int h, PartyEntry* party) {
    NpcEntry* npc;
    MapObjectEntry* mobj;
    interactFind(party->position.x, party->position.y, party->position.z, REACH, &npc, &mobj);
    if (npc != NULL) {
        helperTalk(h, party, npc);
        return;
    }
    if (mobj != NULL) {
        printf("[coop] player %d's character hits '%s'\n", h + 2, mobj->name);
        fflush(stdout);
        mobjRunEvent(mobj, (EvtWord*)mobj->event);
    }
}

/* --- an online co-op guest's field actions (statesync_coop_pc.inc "the guest's field actions") --- */

/* the host's game: helper h's guest's game started a talk with this NPC (its character at x, y, z there) or hit
 * this block: the same here, from where it was (a little past the reach: the two games' places differ a little) */
int portCoopGuestTalk(int h, const char* name, float x, float y, float z) {
    PartyEntry* party = h >= 0 && h < HELPERS ? helperParty(h) : NULL;
    NpcEntry* npc;
    if (party == NULL || name == NULL || name[0] == '\0' || !fieldControl() || sCoop.talkEvt >= 0 ||
        (npc = npcNameToPtr_NoAssert(name)) == NULL) {
        return 0;
    }
    if (!(npc->flags & NPC_FLAG_USED) || npc->talkEvt == NULL || fabsf(npc->position.y - y) >= 60.0f ||
        hypotf(npc->position.x - x, npc->position.z - z) > REACH + 20.0f) {
        return 0;
    }
    helperTalk(h, party, npc);
    return 1;
}

int portCoopGuestHit(int h, const char* name, float x, float y, float z) {
    PartyEntry* party = h >= 0 && h < HELPERS ? helperParty(h) : NULL;
    MapObjectEntry* mobj;
    if (party == NULL || name == NULL || name[0] == '\0' || !fieldControl() || sCoop.talkEvt >= 0 ||
        (mobj = mobjNameToPtrNoAssert(name)) == NULL) {
        return 0;
    }
    if (!(mobj->flags & MOBJ_FLAG_USED) || mobj->event == 0 || mobj->position.y - y >= 80.0f ||
        mobj->position.y - y <= -20.0f || hypotf(mobj->position.x - x, mobj->position.z - z) > REACH + 20.0f) {
        return 0;
    }
    printf("[coop] player %d's character hits '%s' (its own game's B)\n", h + 2, mobj->name);
    fflush(stdout);
    mobjRunEvent(mobj, (EvtWord*)mobj->event);
    return 1;
}

/* The partner's player's field ability's text (Goombella's Tattle, X from player 2): that player reads it
 * on, as Mario's can (the game's text boxes read controller 1 alone; online, a guest's A is controller 2
 * on the host's game). sAbilityX: when player 2's X last went to the game (retrace count + 1, 0 never);
 * sAbilityWin: the text box it opened, -1 none. */
static unsigned sAbilityX;
static s32 sAbilityWin = -1;

/* evt_msg.c, a script's text box opened (its window) */
void portCoopTextOpened(s32 window) {
    if (gp != NULL && sAbilityX != 0 && (unsigned)gp->retraceCount + 1u - sAbilityX <= 120u) {
        sAbilityWin = window;
        sAbilityX = 0;
    }
}

static void abilityTextFrame(void) {
    if (sAbilityWin < 0) {
        return;
    }
    if (!windowCheckID(sAbilityWin) || gp->inBattle) {
        sAbilityWin = -1;
        return;
    }
    gp->buttonNew[0] |= gp->buttonNew[1] & (PAD_BUTTON_A | PAD_BUTTON_B);
    gp->button[0] |= gp->button[1] & (PAD_BUTTON_A | PAD_BUTTON_B);
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
/* Lent partners: when none Mario has is free, players 3 and 4 still get someone to walk, a partner
 * he hasn't met yet, in the chapter order the story brings them (Ms. Mowz, whom he may never meet,
 * last). Only a tag-along: the follower form walks, hops, talks and hits blocks, nothing more. Its
 * field ability is the partner slot's alone (marioUsePartyMain), battles take their partners from
 * the party (partnerOwner: never a partner Mario hasn't got), and nothing joins: no menu, Party tab,
 * partner swap or save ever sees it (portCoopFollowerSaveId). Once that partner really joins, the
 * same companion is simply one Mario has. */
static const int kLendOrder[7] = {PARTY_MEMBER_GOOMBELLA, PARTY_MEMBER_KOOPS,  PARTY_MEMBER_FLURRIE,
                                  PARTY_MEMBER_YOSHI,     PARTY_MEMBER_VIVIAN, PARTY_MEMBER_BOBBERY,
                                  PARTY_MEMBER_MS_MOWZ};
#define FOLLOWER_FORM(member) ((member) + PARTY_MEMBER_GOOMBELLA_FOLLOWER - PARTY_MEMBER_GOOMBELLA)
#define PARTNER_OF(form) ((form) - PARTY_MEMBER_GOOMBELLA_FOLLOWER + PARTY_MEMBER_GOOMBELLA)

/* a partner on the field now (the partner, the follower or the other companion), as a partner's id */
static int partnerInUse(int member, int exceptHelper) {
    int h;
    for (h = 0; h < HELPERS; h++) {
        const PartyEntry* party = helperParty(h);
        int id;
        if (h == exceptHelper || party == NULL || sPm64Look[h] > 0) {
            continue; /* (a Paper Mario 64 partner's copy takes nobody's) */
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

static int lendsToPlayer2(void);

/* a partner taken for a helper (0 player 2's lent one, 1 player 3's, 2 player 4's): on the field
 * for someone else, or another player's (chosen, even while it's away), so two never walk the same
 * one */
static int companionTaken(int member, int h) {
    const int form = FOLLOWER_FORM(member);
    if (partnerInUse(member, h)) {
        return 1;
    }
    return (h != 0 && lendsToPlayer2() && sLent2 == form && sPm64Look[0] <= 0) ||
           (h != 1 && seatKept(2) && sCompanion[0] == form && sPm64Look[1] <= 0) ||
           (h != 2 && seatKept(3) && sCompanion[1] == form && sPm64Look[2] <= 0);
}

/* a partner Mario has is free for this helper: then no lent one */
static int joinedFree(int h) {
    int i;
    for (i = 0; i < 7; i++) {
        if (partyChkJoin((PartyId)kCompanionOrder[i]) && !companionTaken(kCompanionOrder[i], h)) {
            return 1;
        }
    }
    return 0;
}

/* this helper may keep that companion (a follower form): nobody else's, and lent only while no
 * partner Mario has is free */
static int companionOk(int h, int form) {
    int member;
    if (form < PARTY_MEMBER_GOOMBELLA_FOLLOWER || form > PARTY_MEMBER_MS_MOWZ_FOLLOWER) {
        return 0;
    }
    member = PARTNER_OF(form);
    if (sPm64Look[h] > 0 && portPm64PartnerHost(sPm64Look[h]) == member) {
        /* a Paper Mario 64 partner's copy: anyone's, met with its host or (before) lent by the lent rules */
        return partyChkJoin((PartyId)member) || !joinedFree(h);
    }
    return !companionTaken(member, h) && (partyChkJoin((PartyId)member) || !joinedFree(h));
}

/* the companion after this one (0: the first) for a helper (1 player 3's, 2 player 4's): one
 * of the partners Mario has, not taken; when none is free, a lent one (kLendOrder); the follower
 * form */
static int nextCompanion(int h, int after) {
    const int lend = !joinedFree(h);
    const int* order = lend ? kLendOrder : kCompanionOrder;
    int i, start = 0;
    for (i = 0; after != 0 && i < 7; i++) {
        if (FOLLOWER_FORM(order[i]) == after) {
            start = i + 1;
        }
    }
    for (i = 0; i < 7; i++) {
        const int member = order[(start + i) % 7];
        if (!companionTaken(member, h) && (lend || partyChkJoin((PartyId)member))) {
            return FOLLOWER_FORM(member);
        }
    }
    for (i = 0; i < 7; i++) {
        if (!partnerInUse(kLendOrder[i], h)) {
            return FOLLOWER_FORM(kLendOrder[i]);
        }
    }
    return FOLLOWER_FORM(PARTY_MEMBER_MS_MOWZ);
}

/* a shared battle's companion (statesync_battle_pc.c, A's game): the partner the player had out
 * (wanted) when this party has it, else the next in the offered order this party has, else a lent
 * one (kLendOrder: no turns of its own, as no partner the party hasn't got fights); never A's
 * partner nor the other companion (taken: a follower form, 0 none). Plain ids, sent as numbers. */
int portCoopBattleCompanion(int wanted, int partner, int taken) {
    int pass, i;
    if (wanted >= PARTY_MEMBER_GOOMBELLA && wanted <= PARTY_MEMBER_MS_MOWZ && wanted != partner &&
        FOLLOWER_FORM(wanted) != taken && partyChkJoin((PartyId)wanted)) {
        return FOLLOWER_FORM(wanted);
    }
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < 7; i++) {
            const int member = pass == 0 ? kCompanionOrder[i] : kLendOrder[i];
            if (member != partner && FOLLOWER_FORM(member) != taken &&
                (pass == 1 || partyChkJoin((PartyId)member))) {
                return FOLLOWER_FORM(member);
            }
        }
    }
    return FOLLOWER_FORM(PARTY_MEMBER_MS_MOWZ);
}

/* player 4's companion out, a little behind Mario */
static void spawnExtra(void) {
    MarioWork* mario = marioGetPtr();
    if (!companionOk(2, sCompanion[1])) {
        sCompanion[1] = nextCompanion(2, sCompanion[1]);
    }
    /* (at his side: no sines here, whose last bits may differ between computers) */
    if (portPartyEntryExtra(sCompanion[1], mario->position.x - 24.0f, mario->position.y, mario->position.z + 8.0f) >=
        0) {
        printf("[coop] player 4's companion comes (%d%s)\n", sCompanion[1],
               partyChkJoin((PartyId)PARTNER_OF(sCompanion[1])) ? "" : ", lent");
        fflush(stdout);
        if (!partyChkJoin((PartyId)PARTNER_OF(sCompanion[1]))) {
            portTipOffer(PORT_TIP_LENT); /* (the first time on this install: what a lent partner is) */
        }
    }
}

/* player 3's companion out in the follower's slot (empty: no story follower now or away) */
static void spawnFollower(void) {
    if (!companionOk(1, sCompanion[0])) {
        sCompanion[0] = nextCompanion(1, sCompanion[0]);
    }
    if (marioPartyEntry((PartyId)sCompanion[0]) >= 0) {
        sOwnFollower = 1;
        printf("[coop] player 3's companion comes (%d%s)\n", sCompanion[0],
               partyChkJoin((PartyId)PARTNER_OF(sCompanion[0])) ? "" : ", lent");
        fflush(stdout);
        if (!partyChkJoin((PartyId)PARTNER_OF(sCompanion[0]))) {
            portTipOffer(PORT_TIP_LENT);
        }
    }
}

/* a co-op guest: helper h's (1 player 3's, 2 player 4's) character here the one the host's game has (member: its
   party member, a companion's follower form or the story's follower), brought out at Mario's side if it isn't */
void portCoopGuestHelperAs(int h, int member) {
    MarioWork* mario = marioGetPtr();
    PartyEntry* party;
    if (!portStateSyncCoopGuest() || mario == NULL || member <= 0 || (h != 1 && h != 2)) {
        return;
    }
    party = helperParty(h);
    if (party != NULL && party->currentMemberId == member) {
        return;
    }
    if (h == 1) {
        if (party != NULL && !sOwnFollower) {
            return; /* (the story's follower here: the host's is the story's too) */
        }
        if (party != NULL) {
            partyKill2(1);
        }
        sCompanion[0] = member;
        if (marioPartyEntry((PartyId)member) >= 0) {
            sOwnFollower = 1;
        }
    } else {
        if (party != NULL) {
            portPartyKillExtra();
        }
        sCompanion[1] = member;
        portPartyEntryExtra(member, mario->position.x - 24.0f, mario->position.y, mario->position.z + 8.0f);
    }
    sHelp[h].free = 0;
    printf("[coop] player %d's character here is the host's (member %d)\n", h + 2, member);
    fflush(stdout);
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

/* port/players.h: the story's follower's party id (an escort, a Trouble's client), -1 none: player 3's
 * companion, out in the same slot, is nobody's follower */
int portCoopStoryFollowerId(void) {
    const s32 id = marioGetExtraPartyId();
    const PartyEntry* party = id >= 0 ? partyGetPtr(id) : NULL;
    if (id >= 0 && sOwnFollower && (party == NULL || party->currentMemberId == sCompanion[0])) {
        return -1;
    }
    return id;
}

static void nextCompanionPressed(int h);

/* Player 2 before Mario has any partner (the prologue, before Goombella): a lent partner of their
 * own in the port's lent slot (never the partner's slot: no field ability, no battle, never saved),
 * with co-op on and Brothers mode off (then player 2 is Luigi). When Mario's first partner comes
 * out, it goes and player 2 plays that partner, as always. */
static int lendsToPlayer2(void) {
    int i;
    if (!coopOn() || !helperKept(0) || partyGetPtr(marioGetPartyId()) != NULL) {
        return 0;
    }
    for (i = 0; i < 7; i++) {
        if (partyChkJoin((PartyId)kLendOrder[i])) {
            return 0;
        }
    }
    return 1;
}

static void spawnLent2(void) {
    MarioWork* mario = marioGetPtr();
    if (!companionOk(0, sLent2)) {
        sLent2 = nextCompanion(0, sLent2);
    }
    /* (at his side: no sines here, whose last bits may differ between computers) */
    if (portPartyEntryExtraAt(PORT_PARTY_SLOT_LENT, sLent2, mario->position.x + 24.0f, mario->position.y,
                              mario->position.z + 8.0f) >= 0) {
        printf("[coop] player 2's companion comes (%d, lent)\n", sLent2);
        fflush(stdout);
        portTipOffer(PORT_TIP_LENT);
    }
}

static void killLent2(void) {
    if (partyGetPtr(PORT_PARTY_SLOT_LENT) != NULL) {
        portPartyKillExtraAt(PORT_PARTY_SLOT_LENT);
    }
    sHelp[0].free = 0;
}

/* --- the lent partners' colours ---
 * A lent partner (and a companion that is the same partner as another character out) is drawn
 * tinted in its player's colour, so the players tell it from the real one: the model's own
 * material tint (ANIM_MATERIAL_FLAG_EVT_COLOR, which multiplies its colours: the drawing stays
 * readable), each colour the tag's mixed halfway with white. Set every frame from the game's state
 * and the seats alone, so every game of an online session draws the same. */
static s32 sTintedPose[HELPERS] = {-1, -1, -1};

/* --- the players' colours (port/include/port/players.h) ---
 * Standard keeps 1P's red and moves 2P to 4P apart. The first palette (2P 6CC8FF, 3P 7CE06C, 4P FFD850)
 * had 3P and 4P 4.5 apart (CIEDE2000) for protanopia, 9.3 for deuteranopia (1P and 3P 8.0) and 2P and 3P
 * 9.6 for tritanopia (Machado 2009's simulations, full strength). Standard now: its closest pair 17.6
 * (deuteranopia, 1P-4P); High contrast: every pair 24 or more for all three. */
static const unsigned kColourSets[2][PORT_PLAYERS_MAX] = {
    {0xFF7060FFu, 0x54A5FFFFu, 0x97FDC8FFu, 0xFADA11FFu}, /* Standard */
    {0xED4E12FFu, 0x1388E8FFu, 0x47FAD6FFu, 0xFCF31DFFu}, /* High contrast */
};

unsigned portPlayerColour(int player) {
    static int* sSet;
    if (sSet == NULL) {
        sSet = portSettingsDeclare("player_colours", 0, 0, 1);
    }
    return kColourSets[sSet != NULL && *sSet == 1 ? 1 : 0][player & 3];
}

/* a player's colour, toWhite of the way to white (the lent tint: halfway) */
static GXColor playerGX(int player, float toWhite) {
    const unsigned v = portPlayerColour(player);
    const int r = (int)(v >> 24), g = (int)((v >> 16) & 0xFF), b = (int)((v >> 8) & 0xFF);
    GXColor c;
    c.r = (u8)(r + (255 - r) * toWhite);
    c.g = (u8)(g + (255 - g) * toWhite);
    c.b = (u8)(b + (255 - b) * toWhite);
    c.a = 0xFF;
    return c;
}

/* the partner (1 to 7) a party entry is, in either form; 0 another (a story follower, Luigi) */
static int partnerOfEntry(const PartyEntry* party) {
    int id = party != NULL ? party->currentMemberId : 0;
    if (id >= PARTY_MEMBER_GOOMBELLA_FOLLOWER && id <= PARTY_MEMBER_MS_MOWZ_FOLLOWER) {
        id = PARTNER_OF(id);
    }
    return id >= PARTY_MEMBER_GOOMBELLA && id <= PARTY_MEMBER_MS_MOWZ ? id : 0;
}

/* the helper's character is lent, or the same partner as another character out */
static int tintedHelper(int h, PartyEntry* party) {
    int member, o;
    if (party == NULL || !helperKept(h) || (h == 1 && !sOwnFollower) ||
        (h == 0 && party->currentSlotId != PORT_PARTY_SLOT_LENT) || (member = partnerOfEntry(party)) == 0) {
        return 0;
    }
    if (!partyChkJoin((PartyId)member)) {
        return 1;
    }
    if (partnerOfEntry(partyGetPtr(marioGetPartyId())) == member) {
        return 1;
    }
    for (o = 0; o < HELPERS; o++) {
        if (o != h && partnerOfEntry(helperParty(o)) == member) {
            if ((sPm64Look[h] > 0) != (sPm64Look[o] > 0)) {
                return sPm64Look[h] > 0; /* (the one drawn as a Paper Mario 64 partner is the copy) */
            }
            return o < h; /* (the later one of the two is the copy) */
        }
    }
    return 0;
}

/* the pose back to Mario's tint, as the game keeps a partner's (party.c, setAnimPoseMaterialFlag) */
static void untint(s32 pose) {
    MarioWork* mario = marioGetPtr();
    const s32 marioPose = mario != NULL ? mario->paperAnimGroupId[marioAnimeId()] : -1;
    if (marioPose >= 0 && (animPoseGetMaterialFlag(marioPose) & ANIM_MATERIAL_FLAG_EVT_COLOR)) {
        animPoseSetMaterialEvtColor(pose, animPoseGetMaterialEvtColor(marioPose));
    } else {
        animPoseSetMaterialEvtColor(pose, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        animPoseSetMaterialFlagOff(pose, ANIM_MATERIAL_FLAG_EVT_COLOR);
    }
}

static void tintFrame(void) {
    MarioWork* mario = marioGetPtr();
    const s32 marioPose = mario != NULL ? mario->paperAnimGroupId[marioAnimeId()] : -1;
    int h;
    for (h = 0; h < HELPERS; h++) {
        PartyEntry* party = helperParty(h);
        const s32 pose = party != NULL ? party->paperAnimGroup : -1;
        if (pose >= 0 && mario != NULL && tintedHelper(h, party)) {
            GXColor c = playerGX(h + 1, 0.5f);
            if (marioPose >= 0 && (animPoseGetMaterialFlag(marioPose) & ANIM_MATERIAL_FLAG_EVT_COLOR)) {
                /* (a scene's fade or colour on Mario, as the partners follow it: with the tint) */
                const GXColor m = animPoseGetMaterialEvtColor(marioPose);
                c.r = (u8)(c.r * m.r / 255);
                c.g = (u8)(c.g * m.g / 255);
                c.b = (u8)(c.b * m.b / 255);
                c.a = m.a;
            }
            animPoseSetMaterialFlagOn(pose, ANIM_MATERIAL_FLAG_EVT_COLOR);
            animPoseSetMaterialEvtColor(pose, c);
            sTintedPose[h] = pose;
        } else if (sTintedPose[h] >= 0) {
            if (pose == sTintedPose[h]) {
                untint(pose); /* (it joined: its own colours again) */
            }
            sTintedPose[h] = -1;
        }
    }
}

/* each frame: the companions come (the field settled, their players there) and go (their
 * players gone, co-op off, a battle or another sequence for player 4's) */
static void companionsFrame(void) {
    MarioWork* mario;
    PartyEntry* follower;
    /* (a battle put the companions away: back as soon as the field is the players' again, not after the
       new room's settling; player 4's was still gone a second after the battle) */
    static int sBackSoon;
    const int field = fieldControl() && sCoop.talkEvt < 0;
    sFieldFrames = field ? sFieldFrames + 1 : 0;
    if (gp->inBattle && coopOn() && padHere(3)) {
        sBackSoon = 1;
    }
    if (partyGetPtr(PORT_PARTY_SLOT_COMPANION) != NULL && (gp->inBattle || seqGetSeq() != SEQ_GAME || !coopOn() || !seatKept(3))) {
        portPartyKillExtra();
        sHelp[2].free = 0;
    }
    if (seqGetSeq() == SEQ_GAME && !gp->inBattle && sFieldFrames > 40) {
        sBackSoon = 0;
    }
    if (sBackSoon && field && sFieldFrames == 2 && coopOn() && padHere(3) &&
        partyGetPtr(PORT_PARTY_SLOT_COMPANION) == NULL && seqGetSeq() == SEQ_GAME) {
        sBackSoon = 0;
        spawnExtra();
        printf("[coop] player 4's companion back at once after the battle\n");
        fflush(stdout);
    }
    if (partyGetPtr(PORT_PARTY_SLOT_LENT) != NULL && (gp->inBattle || seqGetSeq() != SEQ_GAME || !lendsToPlayer2())) {
        if (partyGetPtr(marioGetPartyId()) != NULL) {
            printf("[coop] player 2's lent partner makes way: player 2 plays the partner\n");
            fflush(stdout);
        }
        killLent2();
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
    if (sOwnFollower && follower != NULL && (!coopOn() || !seatKept(2))) {
        killFollower(); /* player 3 gone: the companion goes with them */
        return;
    }
    if (lendsToPlayer2() && partyGetPtr(PORT_PARTY_SLOT_LENT) == NULL && sFieldFrames == 20) {
        spawnLent2();
    }
    if (coopOn() && padHere(2) && follower == NULL && mario->prevPartyMemberId[1] == 0 && sFieldFrames == 30) {
        spawnFollower();
    } else if (coopOn() && padHere(2) && sOwnFollower && follower == NULL && sFieldFrames > 30) {
        spawnFollower(); /* (its own put away with the partner, as a test's party change does: back) */
    }
    if (coopOn() && padHere(3) && partyGetPtr(PORT_PARTY_SLOT_COMPANION) == NULL && sFieldFrames == 40) {
        spawnExtra();
    }
    if (sFieldFrames > 40) {
        /* a companion someone else now has (player 2 switched to it, or it joined and came out as
         * the partner), or a lent one while a partner Mario has is free again: the next one */
        if (partyGetPtr(PORT_PARTY_SLOT_LENT) != NULL && !companionOk(0, sLent2)) {
            nextCompanionPressed(0);
        }
        if (sOwnFollower && partyGetPtr(1) != NULL && !companionOk(1, sCompanion[0])) {
            nextCompanionPressed(1);
        }
        if (partyGetPtr(PORT_PARTY_SLOT_COMPANION) != NULL && !companionOk(2, sCompanion[1])) {
            nextCompanionPressed(2);
        }
    }
}

/* a helper's companion (a follower form) set, and brought out again if it is out now */
static void pickCompanion(int h, int form) {
    if (h == 0 && partyGetPtr(PORT_PARTY_SLOT_LENT) != NULL) {
        sLent2 = form;
        killLent2();
        spawnLent2();
    } else if (h == 1) {
        sCompanion[0] = form;
        if (sOwnFollower && partyGetPtr(1) != NULL) {
            partyKill2(1);
            sOwnFollower = 0;
            sHelp[1].free = 0;
            spawnFollower();
        }
    } else if (h == 2) {
        sCompanion[1] = form;
        if (partyGetPtr(PORT_PARTY_SLOT_COMPANION) != NULL) {
            portPartyKillExtra();
            sHelp[2].free = 0;
            spawnExtra();
        }
    }
}

/* L or R, players 3 and 4 (and player 2 with a lent partner): the next companion (theirs only,
 * not the story's follower) */
static void nextCompanionPressed(int h) {
    if (h >= 0 && h < HELPERS) {
        sPm64Look[h] = 0; /* (the next one in the party's own look) */
    }
    if (h == 0 && partyGetPtr(PORT_PARTY_SLOT_LENT) != NULL) {
        pickCompanion(0, nextCompanion(0, sLent2));
    } else if (h == 1 && sOwnFollower && partyGetPtr(1) != NULL) {
        pickCompanion(1, nextCompanion(1, sCompanion[0]));
    } else if (h == 2 && partyGetPtr(PORT_PARTY_SLOT_COMPANION) != NULL) {
        pickCompanion(2, nextCompanion(2, sCompanion[1]));
    }
}

/* --- the Paper Mario 64 partners' copies (sPm64Look) --- */

/* the helper's companion (a follower form) as chosen: player 2's lent one, players 3 and 4's */
static int companionForm(int h) {
    return h == 0 ? sLent2 : h == 1 ? sCompanion[0] : sCompanion[1];
}

/* the helper's own companion is out (not the partner, nor the story's follower): its look is the helper's */
static PartyEntry* ownCompanion(int h) {
    PartyEntry* party;
    if (h == 0) {
        party = partyGetPtr(PORT_PARTY_SLOT_LENT);
    } else if (h == 1) {
        party = sOwnFollower ? partyGetPtr(1) : NULL;
    } else {
        party = partyGetPtr(PORT_PARTY_SLOT_COMPANION);
    }
    return party != NULL && party->currentMemberId == companionForm(h) ? party : NULL;
}

/* each frame: a pick that no longer fits goes (the partners off, another companion, Mario's own partner out
 * for player 2), and every party slot's look for the draw (port/pm64.h) */
static void lookFrame(void) {
    int looks[PORT_PARTY_SLOTS] = {0};
    int h, slot;
    for (h = 0; h < HELPERS; h++) {
        const int p = sPm64Look[h];
        const PartyEntry* party;
        if (p != 0 && (!portPm64PartnersActive() || !coopOn() ||
                       (p > 0 && FOLLOWER_FORM(portPm64PartnerHost(p)) != companionForm(h)) ||
                       (h == 0 && partyGetPtr(marioGetPartyId()) != NULL))) {
            sPm64Look[h] = 0;
        }
        if (sPm64Look[h] != 0 && (party = ownCompanion(h)) != NULL && party->currentSlotId >= 0 &&
            party->currentSlotId < PORT_PARTY_SLOTS) {
            looks[(int)party->currentSlotId] = sPm64Look[h];
        }
    }
    for (h = 0; h < HELPERS; h++) {
        /* a co-op guest: its seats' characters are the host's game's, and so are their looks (the host's word,
           -1 its TTYD look): this game's own worn words never cross online, so its own walk of its seat's
           character drew it by them (Goombario walked here as Goombella, Goombario again when the host held
           him for his move, drawn from the host's word) */
        int look = 0;
        const PartyEntry* party;
        if (portStateSyncSeatCharacter(h + 2, &look) > 0 && !(h == 0 && portBrothersOn()) &&
            (party = helperParty(h)) != NULL && partnerOfEntry(party) != 0 && party->currentSlotId >= 0 &&
            party->currentSlotId < PORT_PARTY_SLOTS) {
            looks[(int)party->currentSlotId] = look > 0 ? look : -1;
        }
    }
    for (slot = 0; slot < PORT_PARTY_SLOTS; slot++) {
        portPm64SlotLook(slot, looks[slot]);
    }
}

/* the player menu (port/include/port/player_menu.h): the PM64 partner port's (1-3) character is drawn as */
int portCoopPm64Of(int port) {
    const int h = port - 1;
    PartyEntry* party;
    int member, look = 0;
    if (h >= 0 && h < HELPERS && portStateSyncSeatCharacter(port + 1, &look) != PORT_STATESYNC_NO_CHARACTER) {
        return look > 0 ? look : 0; /* (a co-op guest: the host's game's) */
    }
    if (h < 0 || h >= HELPERS || (port == 1 && portBrothersOn()) || (party = helperParty(h)) == NULL ||
        (member = partnerOfEntry(party)) == 0 || !portPm64PartnersActive()) {
        return 0;
    }
    if (ownCompanion(h) == party && sPm64Look[h] != 0) {
        return sPm64Look[h] > 0 && portPm64PartnerHost(sPm64Look[h]) == member ? sPm64Look[h] : 0;
    }
    return portPm64PartnerOn(member); /* (the party's look) */
}

/* the strip's Paper Mario 64 partner p for the player of port (1-3): after its host, whenever it has joined
 * with it (a copy for a companion, anyone's; player 2's partner itself wears it, as the Partner wheel does),
 * or lent by the lent rules before then */
static int pm64PickState(int port, int p) {
    const int h = port - 1, host = portPm64PartnerHost(p);
    int hostState;
    if (h < 0 || h >= HELPERS || host == 0 || (h == 0 && portBrothersOn()) || !portPm64PartnersActive()) {
        return PORT_STRIP_NONE;
    }
    if (portCoopCharacterOf(port) == host && portCoopPm64Of(port) == p) {
        return PORT_STRIP_MINE;
    }
    if (h == 0 && partyGetPtr(PORT_PARTY_SLOT_LENT) == NULL) {
        hostState = portCoopPickState(port, host);
        if (!portPm64PartnerAvailable(p) || hostState == PORT_STRIP_NONE) {
            return PORT_STRIP_NONE;
        }
        return hostState == PORT_STRIP_TAKEN ? PORT_STRIP_TAKEN : PORT_STRIP_FREE;
    }
    if (portPm64PartnerAvailable(p)) {
        return PORT_STRIP_FREE;
    }
    return !partyChkJoin((PartyId)host) && !joinedFree(h) ? PORT_STRIP_LENT : PORT_STRIP_NONE;
}

/* the player menu's Character strip (port/include/port/player_menu.h): entry id (a partner member 1-7, or
 * PORT_STRIP_PM64) for the player of port (1-3) */
int portCoopPickState(int port, int id) {
    const int h = port - 1, member = id;
    PouchData* pouch;
    int lend;
    if (PORT_STRIP_PM64_OF(id) > 0) {
        return PORT_STRIP_PM64_OF(id) < PM64_PARTNER_COUNT ? pm64PickState(port, PORT_STRIP_PM64_OF(id))
                                                           : PORT_STRIP_NONE;
    }
    if (h < 0 || h >= HELPERS || member < PARTY_MEMBER_GOOMBELLA || member > PARTY_MEMBER_MS_MOWZ ||
        (h == 0 && portBrothersOn())) {
        return PORT_STRIP_NONE;
    }
    if (portCoopCharacterOf(port) == member && portCoopPm64Of(port) == 0) {
        return PORT_STRIP_MINE;
    }
    if (h == 0 && partyGetPtr(PORT_PARTY_SLOT_LENT) == NULL) {
        /* the partner: one Mario has and isn't away (the quick swap's), not out for another player */
        pouch = pouchGetPtr();
        if (!(pouch->partyData[member].flags & POUCH_PARTY_FLAG_JOINED) ||
            (pouch->partyData[member].flags & POUCH_PARTY_FLAG_AWAY)) {
            return PORT_STRIP_NONE;
        }
        return partnerInUse(member, 0) ? PORT_STRIP_TAKEN : PORT_STRIP_FREE;
    }
    lend = !joinedFree(h);
    if (!lend && !partyChkJoin((PartyId)member)) {
        return PORT_STRIP_NONE;
    }
    if (companionTaken(member, h)) {
        return PORT_STRIP_TAKEN;
    }
    return lend ? PORT_STRIP_LENT : PORT_STRIP_FREE;
}

/* the player menu's strip: the player (1-4) whose character partner member (1-7) is now (player 1's: the
 * partner out with nobody playing it), 0 none; a Paper Mario 64 partner's copy is nobody's partner */
int portCoopPartnerPlayer(int member) {
    int h;
    for (h = 0; h < HELPERS; h++) {
        if (sPm64Look[h] <= 0 && partnerOfEntry(helperParty(h)) == member) {
            return h == 0 && !helperKept(0) ? 1 : h + 2;
        }
    }
    return 0;
}

/* a pick carried in that player's input (player_menu_pc.c portPlayerMenuTakePad), in the field: a strip
 * entry (a partner, or PORT_STRIP_PM64: a Paper Mario 64 partner, on its host); 0 when it isn't free */
static int pickPressed(int h, int id) {
    const int state = portCoopPickState(h + 1, id);
    const int p = PORT_STRIP_PM64_OF(id), member = p > 0 ? portPm64PartnerHost(p) : id;
    if (state != PORT_STRIP_FREE && state != PORT_STRIP_LENT) {
        printf("[coop] player %d's pick of %s %d: not free (%d)\n", h + 2,
               p > 0 ? "Paper Mario 64 partner" : "partner", p > 0 ? p : member, state);
        fflush(stdout);
        return 0;
    }
    if (p > 0) {
        printf("[coop] player %d picks Paper Mario 64 partner %d (%s, on partner %d%s)\n", h + 2, p,
               portPm64PartnerName(p), member, state == PORT_STRIP_LENT ? ", lent" : "");
    } else {
        printf("[coop] player %d picks partner %d\n", h + 2, member);
    }
    fflush(stdout);
    if (h == 0 && partyGetPtr(PORT_PARTY_SLOT_LENT) == NULL) {
        /* player 2's partner is the party's own: it wears the pick, as the Partner wheel's does (its look,
           field ability and battle moves with it; the TTYD partner picked: themselves again) */
        if (portPm64PartnersActive()) {
            portPm64PartnerWear(member, p);
        }
        portQuickSwapPickCoop(member);
        return 1;
    }
    sPm64Look[h] = p > 0 ? p : portPm64PartnersActive() ? -1 : 0;
    if (companionForm(h) == FOLLOWER_FORM(member) && ownCompanion(h) != NULL) {
        return 1; /* (that partner out already: only its look changes) */
    }
    pickCompanion(h, FOLLOWER_FORM(member));
    return 1;
}

/* the partner's field move is under way (run mode 3, "being used"; Yoshi ridden): Mario's controls are the
 * move's then, not the field's (fieldControl is off) */
static int abilityRunning(void) {
    PartyEntry* partner = partyGetPtr(marioGetPartyId());
    if (yoshiGetStatus() != 0) {
        return 1;
    }
    return partner != NULL && ((partner->flags & PARTY_FLAG_IS_BEING_USED) || partner->runMode == 3);
}

/* player 2's X to the game's (port 1's) as the partner's field move: held as long as it is held, pressed and
 * let go with it. Not for a lent partner (it has no move). Player 1's own X still works too. */
static void abilityX(int p) {
    if (partyGetPtr(PORT_PARTY_SLOT_LENT) != NULL) {
        return;
    }
    if (gp->button[p] & PAD_BUTTON_X) {
        gp->button[0] |= PAD_BUTTON_X;
    }
    if (gp->buttonNew[p] & PAD_BUTTON_X) {
        gp->buttonNew[0] |= PAD_BUTTON_X;
        sAbilityX = (unsigned)gp->retraceCount + 1u; /* (its text box, if it opens one: theirs) */
    }
    if (gp->buttonUp[p] & PAD_BUTTON_X) {
        gp->buttonUp[0] |= PAD_BUTTON_X;
    }
}

/* --- an online co-op guest's field actions, continued (statesync_coop_pc.inc "the guest's field actions") --- */

/* The host's game: a guest's X on the partner's field move, as an action (its controller goes without it while
 * the guest walks its own character): pressed once, then held until its letting go comes, as abilityX does with
 * a controller here. Player 2's controller's own X (the seat held, the move under way) still adds to it. */
static struct {
    int press, held, up;
} sGuestX;

void portCoopGuestAbility(int down) {
    if (down) {
        sGuestX.press = sGuestX.held = 1;
        sGuestX.up = 0;
    } else if (sGuestX.held || sGuestX.press) {
        sGuestX.held = sGuestX.press = 0;
        sGuestX.up = 1;
    }
}

static void guestXFrame(void) {
    if (sGuestX.press) {
        sGuestX.press = 0;
        gp->buttonNew[0] |= PAD_BUTTON_X;
        gp->button[0] |= PAD_BUTTON_X;
        sAbilityX = (unsigned)gp->retraceCount + 1u; /* (its text box, if it opens one: player 2's to read on) */
        printf("[coop] player 2's X (an online guest's action) goes to the partner's field move\n");
        fflush(stdout);
    } else if (sGuestX.held) {
        gp->button[0] |= PAD_BUTTON_X;
    }
    if (sGuestX.up) {
        sGuestX.up = 0;
        gp->buttonUp[0] |= PAD_BUTTON_X;
    }
}

/* the next partner in the quick swap's order after this one (gameplus_pc.c portQuickSwapPoll's), one that has
   joined and isn't away; 0 none */
static int nextPartner(int current) {
    const PouchData* pouch = pouchGetPtr();
    int i, at = -1;
    for (i = 0; i < PORT_PARTNER_MENU_COUNT; i++) {
        if (gPortPartnerMenuOrder[i] == current) {
            at = i;
        }
    }
    for (i = 1; i <= PORT_PARTNER_MENU_COUNT; i++) {
        const int id = gPortPartnerMenuOrder[(at + i + PORT_PARTNER_MENU_COUNT) % PORT_PARTNER_MENU_COUNT];
        const u16 flags = pouch->partyData[id].flags;
        if (id != current && (flags & POUCH_PARTY_FLAG_JOINED) && !(flags & POUCH_PARTY_FLAG_AWAY)) {
            return id;
        }
    }
    return 0;
}

/* the host's game: helper h's guest swapped its character (L or R) to member (the partner; a companion's follower
   form for players 3 and 4; 0: the next, this game's choice), or picked a strip entry: the same here */
int portCoopGuestSwap(int h, int member) {
    PartyEntry* party = h >= 0 && h < HELPERS ? helperParty(h) : NULL;
    if (party == NULL || !fieldControl() || sCoop.talkEvt >= 0) {
        return 0;
    }
    if (h == 0 && partyGetPtr(PORT_PARTY_SLOT_LENT) != NULL) {
        nextCompanionPressed(0); /* (a lent partner: the next lent one, this game's) */
        return 1;
    }
    if (h == 0) {
        const PouchData* pouch = pouchGetPtr();
        if (member == 0) {
            portQuickSwapPressCoop();
            return 1;
        }
        if (member < 1 || member > 7 || member == party->currentMemberId ||
            !(pouch->partyData[member].flags & POUCH_PARTY_FLAG_JOINED) ||
            (pouch->partyData[member].flags & POUCH_PARTY_FLAG_AWAY)) {
            return 0;
        }
        portQuickSwapPickCoop(member); /* (the quick swap's way: at the field's next free frame) */
        return 1;
    }
    if (!(h == 1 && sOwnFollower && partyGetPtr(1) != NULL) && !(h == 2 && partyGetPtr(PORT_PARTY_SLOT_COMPANION) != NULL)) {
        return 0; /* (the story's follower: no swap, as L and R here) */
    }
    if (member == 0) {
        nextCompanionPressed(h);
        return 1;
    }
    if (!companionOk(h, member)) {
        return 0;
    }
    sPm64Look[h] = 0;
    if (companionForm(h) != member || ownCompanion(h) == NULL) {
        pickCompanion(h, member);
    }
    return 1;
}

int portCoopGuestPick(int h, int id) {
    if (h < 0 || h >= HELPERS || id <= 0 || !fieldControl() || sCoop.talkEvt >= 0) {
        return 0;
    }
    return pickPressed(h, id);
}

static void hopPose(PartyEntry* party);

/* the guest's game: its own character's action's pose (by name when its model has it, else the pose table's
   index), and it stands still for frames meanwhile */
static int partyHasAnim(PartyEntry* party, const char* name) {
    AnimationPoseData* d;
    int i;
    if (party->paperAnimGroup < 0 || name == NULL) {
        return 0;
    }
    d = animPoseGetAnimBaseDataPtr(party->paperAnimGroup);
    for (i = 0; d != NULL && i < d->animCount; i++) {
        if (!strcmp(d->anims[i].name, name)) {
            return 1;
        }
    }
    return 0;
}

static void actPose(Helper* help, PartyEntry* party, const char* name, int index, int frames) {
    if (partyHasAnim(party, name)) {
        partyChgPose(party, name);
    } else if (index > 0) {
        partyChgPoseId(party, index);
    }
    partySetTargetSpd(party, 0.0f);
    party->moveSpeed = 0.0f;
    party->motionId = 0;
    help->act = frames;
}

/* the partner's field move's first pose, by partner (party_*.c's own: Koops into his shell, Bobbery's fuse, Yoshi
   ready to be ridden, Flurrie's gust, Vivian's shadow, Ms. Mowz's sniff); Goombella's tattle: index 6's look */
static const char* abilityPose(const PartyEntry* party) {
    static const char* const kPose[8] = {NULL, NULL, "PNK_K_2", "L_1", "PYS_H_1", "PWD_A_5", "PTR_A2_1", "PCH_A2_1"};
    const int m = party->currentMemberId;
    return m >= 1 && m <= 7 ? kPose[m] : NULL;
}

/* the guest's game: its own partner swapped here at once (the host's game does it for real: its word follows) */
static void guestPartnerTo(int member) {
    marioPartyKill();
    marioPartyEntry((PartyId)member);
    printf("[coop] this guest's partner is %d here at once (its swap, sent)\n", member);
    fflush(stdout);
}

/* the guest's game, helper h is its own character, walked here: its B, X, L, R and picks played here at once and
   sent as actions (statesync_coop_pc.inc), the host's game doing them for real */
static void guestActs(int h, int p, u32 shouldersNew) {
    Helper* help = &sHelp[h];
    PartyEntry* party = helperParty(h);
    const u32 pressed = gp->buttonNew[p];
    Vec at;
    int pick;
    if (party == NULL) {
        return;
    }
    at = party->position;
    if (pressed & PAD_BUTTON_B) {
        NpcEntry* npc;
        MapObjectEntry* mobj;
        interactFind(at.x, at.y, at.z, REACH, &npc, &mobj);
        if (npc != NULL) {
            party->targetDir = party->moveDir = angleABf(at.x, at.z, npc->position.x, npc->position.z);
            if (portStateSyncCoopAct(PORT_COOP_ACT_TALK, npc->description, 0, 0, at.x, at.y, at.z, party->targetDir)) {
                actPose(help, party, NULL, 7, 40); /* (its talk pose, turned to the NPC: the host's talk follows) */
            }
        } else if (mobj != NULL) {
            party->targetDir = party->moveDir = angleABf(at.x, at.z, mobj->position.x, mobj->position.z);
            if (portStateSyncCoopAct(PORT_COOP_ACT_HIT, mobj->name, 0, 0, at.x, at.y, at.z, party->targetDir)) {
                actPose(help, party, NULL, 6, 20);
            }
        }
    }
    if (h == 0 && (pressed & PAD_BUTTON_X) && partyGetPtr(PORT_PARTY_SLOT_LENT) == NULL &&
        (party->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING)) == 0 &&
        portStateSyncCoopAct(PORT_COOP_ACT_ABILITY, NULL, party->currentMemberId, 0, at.x, at.y, at.z,
                             party->targetDir)) {
        actPose(help, party, abilityPose(party), 6, 40); /* (the move's start: the host's move follows) */
    }
    if (shouldersNew) {
        if (h == 0 && partyGetPtr(PORT_PARTY_SLOT_LENT) != NULL) {
            portStateSyncCoopAct(PORT_COOP_ACT_SWAP, NULL, 0, 0, at.x, at.y, at.z, party->targetDir); /* (the host's) */
        } else if (h == 0) {
            const int was = party->currentMemberId, next = nextPartner(was);
            if (next > 0 && portStateSyncCoopAct(PORT_COOP_ACT_SWAP, NULL, next, was, at.x, at.y, at.z, party->targetDir)) {
                guestPartnerTo(next);
            }
        } else if ((h == 1 && sOwnFollower && partyGetPtr(1) != NULL) ||
                   (h == 2 && partyGetPtr(PORT_PARTY_SLOT_COMPANION) != NULL)) {
            const int was = party->currentMemberId, next = nextCompanion(h, companionForm(h));
            if (next > 0 && next != was &&
                portStateSyncCoopAct(PORT_COOP_ACT_SWAP, NULL, next, was, at.x, at.y, at.z, party->targetDir)) {
                portCoopGuestHelperAs(h, next);
            }
        }
    }
    pick = portPlayerMenuTakePick(p);
    if (pick > 0) {
        const int pm = PORT_STRIP_PM64_OF(pick), was = party->currentMemberId;
        const int member = pm > 0 ? portPm64PartnerHost(pm) : pick;
        if (portStateSyncCoopAct(PORT_COOP_ACT_PICK, NULL, pick, was, at.x, at.y, at.z, party->targetDir) && pm <= 0 &&
            member >= 1 && member <= 7) {
            /* (a partner of the game's own: here at once; a Paper Mario 64 one's look comes with the host's word) */
            if (h == 0 && partyGetPtr(PORT_PARTY_SLOT_LENT) == NULL && member != was) {
                guestPartnerTo(member);
            } else if (h > 0 && FOLLOWER_FORM(member) != was) {
                portCoopGuestHelperAs(h, FOLLOWER_FORM(member));
            }
        }
    }
}

/* the guest's game: an action of helper h's the host refused, taken back */
void portCoopGuestActUndo(int h, int kind, int prev) {
    PartyEntry* party;
    if (h < 0 || h >= HELPERS) {
        return;
    }
    party = helperParty(h);
    sHelp[h].act = 0;
    if ((kind == PORT_COOP_ACT_SWAP || kind == PORT_COOP_ACT_PICK) && prev > 0) {
        if (h == 0 && prev >= 1 && prev <= 7 && (party == NULL || party->currentMemberId != prev)) {
            guestPartnerTo(prev);
        } else if (h > 0 && (party == NULL || party->currentMemberId != prev)) {
            portCoopGuestHelperAs(h, prev);
        }
        return;
    }
    if (party != NULL) {
        hopPose(party); /* (its stand or walk again) */
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
    abilityTextFrame();
    guestXFrame(); /* (an online guest's X on the partner's move, as an action: the host's game) */
    if (strncmp(sCoop.map, gp->currentMapName, sizeof(sCoop.map)) != 0) {
        snprintf(sCoop.map, sizeof(sCoop.map), "%s", gp->currentMapName);
        for (h = 0; h < HELPERS; h++) {
            sHelp[h].free = 0; /* a new room: they come in with Mario */
            portCoopFallReset(h + 1);
        }
        portCoopFallReset(0);
    }
    companionsFrame();
    tintFrame(); /* (after: one that just came is drawn in its colours at once) */
    for (h = 0; h < HELPERS; h++) {
        const int p = h + 1;
        const u32 shoulders = gp->button[p] & (PAD_TRIGGER_L | PAD_TRIGGER_R);
        Helper* help = &sHelp[h];
        helperSync(h); /* (a new character, or not theirs now: afresh) */
        if (h == 0 && helperHere(h) && abilityRunning()) {
            /* the partner's field move under way (Yoshi's ride, Vivian's shadow, Flurrie's float...): Mario's
               controls are the move's, so the field's input below is off, but player 2's X still goes to the
               game: the move they started they can end (as player 1's X does) */
            abilityX(p);
            if (gp->buttonNew[p] & PAD_BUTTON_X) {
                printf("[coop] player 2's X goes to the partner's field move under way\n");
                fflush(stdout);
            }
        }
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
        {
            const int member = guestOwn(h) ? 0 : portPlayerMenuTakePick(p); /* (a guest's own pick: guestActs) */
            if (member > 0) {
                pickPressed(h, member);
            }
        }
        {
            /* a partner swapped in (the quick swap, the partner menu, a scripted change) or come into the room
               stands in run mode 1, waiting for Mario to walk off before it follows (party_move_mode_wait): the
               walk below runs only in the follow modes, so its player's stick wakes it into following (run mode
               2), as Mario's walk would, and takes it from there */
            PartyEntry* party = helperParty(h);
            if (party != NULL && party->runMode == 1 && !help->free && !help->letGo &&
                hypotf(help->stickX, help->stickY) > DEAD_ZONE) {
                partyChgRunMode(party, 2);
                printf("[coop] player %d's character (member %d) was waiting: it follows, theirs to walk\n", h + 2,
                       party->currentMemberId);
                fflush(stdout);
            }
        }
        help->jump |= (gp->buttonNew[p] & PAD_BUTTON_A) != 0;
        if (guestOwn(h)) {
            /* (this co-op guest's own character: the walk and the hops here, and its talk, block, partner's move,
               swap and pick started here at once and sent as actions, done for real in the host's game; Mario's
               recall is the host's) */
            guestActs(h, p, shoulders & ~sShoulders[h]);
            sShoulders[h] = shoulders;
            continue;
        }
        help->interact |= (gp->buttonNew[p] & PAD_BUTTON_B) != 0;
        help->recall |= (gp->buttonNew[0] & PAD_BUTTON_Y) != 0;
        if (h == 0 && !abilityRunning()) {
            abilityX(p); /* (while one runs: above) */
        }
        if (shoulders & ~sShoulders[h]) {
            if (h == 0 && partyGetPtr(PORT_PARTY_SLOT_LENT) != NULL) {
                nextCompanionPressed(0); /* (a lent partner: the next lent one) */
            } else if (h == 0) {
                portQuickSwapPressCoop();
            } else {
                nextCompanionPressed(h);
            }
        }
        sShoulders[h] = shoulders;
    }
}

/* on the ground, before its walk: no vertical speed left over, so a step off a ledge falls (partyMoveNoHosei
 * starts a fall with its own -1.4 only when accelY is 0; partyWalkInit leaves the follow walk's +1.8 there,
 * which made a walked partner fall upwards, forever) */
static void groundFall(PartyEntry* party) {
    if (!(party->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING))) {
        party->velY = 0.0f;
        party->accelY = 0.0f;
    }
}

/* A hop is the game's jump motion (2): it flies the arc, lands and hands the character back (motion 0, here).
 * Its end, seen here: the jump motion's landing clears its flags and leaves the jump pose (its stand pose
 * is the follow motions', which a player's character never runs), so the pose goes back below (hopPose). A
 * hop cut short (a partyChgMot from elsewhere mid-air, a move mode changed under it) leaves "jumping" set,
 * which partyMoveMain then flies on forever, no ground search: it falls to the ground instead. */
static void hopPose(PartyEntry* party);

static void hopEnded(int h, Helper* help, PartyEntry* party) {
    const int cut = (party->flags & PARTY_FLAG_IS_JUMPING) != 0;
    char was[32];
    snprintf(was, sizeof(was), "%s", party->animName != NULL ? party->animName : "?");
    if (cut) {
        party->flags &= ~PARTY_FLAG_IS_JUMPING;
        party->flags |= PARTY_FLAG_IS_FALLING; /* (partyMoveMain's fall: the ground search lands it) */
        if (party->velY > 0.0f) {
            party->velY = 0.0f;
        }
        party->accelY = -1.4f;
    }
    hopPose(party);
    printf("[coop] player %d's character: hop %s at (%.1f, %.1f, %.1f) after %d frames, pose %s (the jump's %s)\n",
           h + 2, cut ? "cut short, falls" : "landed", party->position.x, party->position.y, party->position.z,
           help->hopping, party->animName != NULL ? party->animName : "?", was);
    fflush(stdout);
    help->hopping = 0;
}

void partyWalkSe(PartyEntry* party, s32 arg); /* party_move.c */

/* Where a helper's player's stick counts as all the way: 70 of the clamped stick (a pad's full tilt reads 72
 * once PADClamp has it, as player 1's does; Mario dashes past 55 of it). A stick whose full tilt reads short (a
 * handheld's built-in one, a worn pad: 70% of the way reads 47) never got there, so its partner never reached
 * the run's speed: a slow glide. Such a stick is learned: the furthest it goes (STICK_PEAK_MIN or more, short
 * of STICK_FULL), held there STICK_PEAK_FRAMES in all (full tilt, held while walking), is its full tilt from
 * then on. A stick that goes further later is a full one again. Per player, as the stick is theirs. */
#define STICK_FULL 70.0f
#define STICK_PEAK_MIN 44.0f   /* (a stick held two thirds of the way for a while isn't taken for a short one) */
#define STICK_PEAK_FRAMES 120
static struct {
    float peak; /* the furthest this stick has gone */
    int held;   /* frames it has been there (within 2) */
} sStickRange[HELPERS];

static float stickFull(int h) {
    if (h >= 0 && h < HELPERS && sStickRange[h].held >= STICK_PEAK_FRAMES &&
        sStickRange[h].peak >= STICK_PEAK_MIN && sStickRange[h].peak < STICK_FULL) {
        return sStickRange[h].peak - 2.0f;
    }
    return STICK_FULL;
}

/* how far helper h's player pushes the stick: 0 to 1 past the dead zone, 1 from stickFull */
static float stickTilt(int h, float mag) {
    float full;
    if (h >= 0 && h < HELPERS && mag > DEAD_ZONE) {
        if (mag > sStickRange[h].peak + 2.0f) {
            if (sStickRange[h].held >= STICK_PEAK_FRAMES && sStickRange[h].peak >= STICK_PEAK_MIN &&
                sStickRange[h].peak < STICK_FULL) {
                printf("[coop] player %d's stick goes further (%.0f): its full tilt is %.0f again\n", h + 2, mag,
                       STICK_FULL);
            }
            sStickRange[h].peak = mag;
            sStickRange[h].held = 0;
        } else if (mag >= sStickRange[h].peak - 2.0f && ++sStickRange[h].held == STICK_PEAK_FRAMES &&
                   sStickRange[h].peak >= STICK_PEAK_MIN && sStickRange[h].peak < STICK_FULL) {
            printf("[coop] player %d's stick reads short: its full tilt is %.0f of %.0f\n", h + 2,
                   sStickRange[h].peak, STICK_FULL);
            fflush(stdout);
        }
    }
    if (mag <= DEAD_ZONE) {
        return 0.0f;
    }
    full = stickFull(h);
    return mag >= full ? 1.0f : (mag - DEAD_ZONE) / (full - DEAD_ZONE);
}

/* (port/include/port/coop_camera.h: the same for Brothers mode's Luigi, player 2's stick too) */
float portCoopStickTilt(int player, float mag) {
    return stickTilt(player - 1, mag);
}

int portCoopStickRuns(int player, float mag) {
    return mag >= fminf(55.0f, stickFull(player - 1));
}

/* the walk (pose 2) or the run (pose 3) for a player's character at speed spd, as partyMoveAnime picks a
 * follower's, but by its own player's stick: partyMoveAnime takes the in-between speeds' choice from Mario's
 * stick (N_marioGetMovementSpeed: past 55 a run), which is player 1's. A partner run at full speed by
 * player 2 played the slow walk while player 1 walked (its feet a third as fast as it went: it slid), and
 * one walked slowly ran on the spot while Mario stood. The pose is only set when it changes (partyChgPoseId):
 * the animation plays on. */
static void walkPose(PartyEntry* party, float spd) {
#if VERSION < VERSION_JP
    partyMoveAnime(party, spd); /* (the demos': by speed alone, no stick) */
#else
    const int h = helperOf(party);
    const float g = party->playerPtr != NULL ? party->playerPtr->playerGravity : 1.0f;
    const float a = (1.0f == g) ? 1.65f : 2.0f;
    const float b = (1.0f == g) ? 0.8f : 0.0f;
    int run;
    if (party->flags2 & PARTY_DISP_FLAG_SLIT) {
        run = 1; /* (a paper slit: always the run, as partyMoveAnime) */
    } else if (spd >= a) {
        run = 1;
    } else if (b > 0.0f && spd > a - b && h >= 0) {
        /* (Mario's rule, past 55 a run, on this player's own stick; a short one's full tilt runs: stickFull) */
        run = hypotf(sHelp[h].stickX, sHelp[h].stickY) >= fminf(55.0f, stickFull(h));
    } else {
        run = 0;
    }
    partyChgPoseId(party, run ? 3 : 2);
    partyWalkSe(party, run);
#endif
}

extern char* mobj_list[]; /* party_chuchu.c: what Ms. Mowz sniffs out */

#define SLEEP_IDLE 3600 /* (Mario's deep sleep: a minute of his player's hands off, mot_stay.c) */

/* A player's character standing: the follow motions' stand (partyChgStayPose) reads Mario's state, so player 2's
 * partner fell asleep whenever player 1 had left the pad a minute, and Ms. Mowz, near anything she sniffs out
 * within 150 of Mario, got no stand pose at all (her walk looped on the spot: she slid). Here: asleep only
 * when Mario sleeps and her own player has been idle as long; her sniff (as party_motion_stay's: 30 frames
 * sniffing, 30 standing) where she herself stands. */
static void ownStayPose(PartyEntry* party) {
    const int h = helperOf(party);
    Helper* const help = h >= 0 ? &sHelp[h] : NULL;
    const Vec* const at = &party->position;
    if (party->runMode == 0) {
        return; /* (as partyChgStayPose) */
    }
    if ((party->flags & PARTY_FLAG_SLEEP_ON) || (marioChkDeepSleep() == TRUE && help != NULL && help->idle >= SLEEP_IDLE)) {
        partyChgPoseId(party, 6);
        return;
    }
    if (help != NULL && party->currentMemberId == PARTY_MEMBER_MS_MOWZ && !(party->flags2 & PARTY_DISP_FLAG_FAMICOM) &&
        (itemNearDistCheck(at->x, at->y, at->z, 150.0f) != NULL ||
         mobjNearDistCheck2(at->x, at->y, at->z, 150.0f, mobj_list) != NULL)) {
        help->sniff = help->sniff % 60 + 1;
        if (help->sniff <= 30) {
            partyChgPose(party, "PCH_A2_1");
        } else {
            partyChgPoseId(party, 1);
        }
        return;
    }
    if (help != NULL) {
        help->sniff = 0;
    }
    partyChgPoseId(party, 1);
}

/* the character's pose as its walk has it: walking or running by its speed, else standing (ownStayPose); in
 * the air (a hop, a fall) the air's pose stays */
static void hopPose(PartyEntry* party) {
    if (party->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING)) {
        return;
    }
    if (party->moveSpeed > 0.0f) {
        walkPose(party, party->moveSpeed);
    } else {
        ownStayPose(party);
    }
}

/* party_move.c, a party member's walk mode standing or walking: 1 when its
 * player moves it this frame (in place of following Mario) */
int portCoopPartnerWalk(PartyEntry* party) {
    MarioWork* mario;
    Helper* help;
    float mag, out;
    int roams;
    const int h = helperOf(party);
    if (!coopOn() || h < 0 || !helperHere(h)) {
        /* (Brothers mode: the second player walks Luigi, brothers_pc.c) */
        return 0;
    }
    help = &sHelp[h];
    mario = marioGetPtr();
    if (help->act > 0) {
        /* an online guest's own character's action (a talk's start, the partner's move's): it stands in its pose
           here meanwhile, the host's game doing it for real */
        help->act = guestOwn(h) ? help->act - 1 : 0;
        if (help->act > 0 || guestOwn(h)) {
            partySetTargetSpd(party, 0.0f);
            party->moveSpeed = 0.0f;
            party->motionId = 0;
            help->jump = help->interact = 0;
            return 1;
        }
    }
    if (!help->recall && portStateSyncCoopPlace(h + 2, party)) {
        /* the host: an online guest's own game walks this character; it is where that game has it. Its B (a talk,
           a block or switch within reach) still comes as the guest's controller, and is this game's */
        if (!help->free) {
            help->free = 1;
            party->moveMode = 1;
            printf("[coop] player %d's own game walks their character\n", h + 2);
            fflush(stdout);
        }
        help->jump = 0;
        help->hopping = 0;
        help->idle = 0;
        if (help->interact) {
            help->interact = 0;
            if (sCoop.talkEvt < 0) {
                interact(h, party);
            }
        }
        return 1;
    }
    if (party->forceMoveFrames != 0 && help->free) {
        party->forceMoveFrames = 0; /* a hop's (below), landed */
    }
    if (help->hopping) {
        hopEnded(h, help, party); /* (back in motion 0 or 1: the hop's motion is over) */
    }
    if (sCoop.talkEvt >= 0 && help->free && sCoop.talkBy == h) {
        /* its own talk: it stands where it is, facing the NPC */
        partySetTargetSpd(party, 0.0f);
        party->moveSpeed = 0.0f;
        party->motionId = 0;
        hopPose(party);
        return 1;
    }
    if (!fieldControl()) {
        help->jump = help->interact = help->recall = 0;
        if (help->free && holdsThrough(h)) {
            /* a menu, a talk: it waits where it is */
            partySetTargetSpd(party, 0.0f);
            party->moveSpeed = 0.0f;
            party->motionId = 0;
            hopPose(party);
            return 1;
        }
        if (help->free) {
            printf("[coop] player %d's character: Mario's player has no control now (a scene)\n", h + 2);
            recall(help, party); /* a scene, Yoshi's ride, a pipe: with Mario */
        }
        return 0;
    }
    mag = hypotf(help->stickX, help->stickY);
    if (mag <= DEAD_ZONE && !help->jump && !help->interact && !help->recall) {
        help->idle++;
    } else {
        help->idle = 0;
    }
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
    roams = portCoopRoams(h + 1); /* an online guest's: the whole room */
    if (portCoopFallen(h + 1, party) || (!roams && party->position.y < mario->position.y - 200.0f)) {
        printf("[coop] player %d's character: fallen at (%.1f, %.1f, %.1f), Mario at (%.1f, %.1f, %.1f)\n", h + 2,
               party->position.x, party->position.y, party->position.z, mario->position.x, mario->position.y,
               mario->position.z);
        recall(help, party); /* a pit, deep water, below Mario's floor, or in the air for seconds */
        return 1;
    }
    out = roams ? 0.0f : portCoopEdgeExcess(party->position.x, party->position.y, party->position.z);
    if (out > TUG_EXCESS) {
        /* left outside the picture (Mario walked on): walked back toward him, its player's stick
           aside, and back at his side when that gets nowhere */
        const float d = hypotf(party->position.x - mario->position.x, party->position.z - mario->position.z);
        if (d < help->tugBest - 1.0f) {
            help->tugBest = d;
            help->tugFrames = 0;
        } else if (++help->tugFrames > TUG_STUCK) {
            printf("[coop] player %d's character: stuck outside the picture at (%.1f, %.1f, %.1f)\n", h + 2,
                   party->position.x, party->position.y, party->position.z);
            recall(help, party);
            return 1;
        }
        party->targetDir = angleABf(party->position.x, party->position.z, mario->position.x, mario->position.z);
        partySetTargetSpd(party, mario->baseDashSpeed > 0.0f ? mario->baseDashSpeed : 4.0f);
        party->motionId = 1;
        help->jump = help->interact = 0;
        groundFall(party);
        partyMoveNoHosei(party);
        hopPose(party);
        party->moveDir = party->targetDir;
        return 1;
    }
    help->tugBest = 1e9f;
    help->tugFrames = 0;
    if (mag > DEAD_ZONE) {
        /* as marioGetStick turns Mario's stick: the camera's yaw added (an online guest's: its own camera's) */
        party->targetDir = revise360(angleABf(0.0f, 0.0f, help->stickX, -help->stickY) +
                                     portCoopStickYaw(h + 1, mario->targetYaw));
        /* (full speed from where Mario dashes, or this stick's own full tilt: stickFull) */
        /* (Enhancements' walk boost: this player's own control, this character's own speed) */
        const float want = stickTilt(h, mag) * (mario->baseDashSpeed > 0.0f ? mario->baseDashSpeed : 4.0f) *
                           portWalkBoostFor(h + 1);
        int step;
        /* the follow walk's easing (5% of the way a frame) is a tag-along's: a player's character picks up
           speed as a player's does, four of its steps a frame, to the same top speed (the member's own rate) */
        for (step = 0; step < 4; step++) {
            partySetTargetSpd(party, want);
        }
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
        if (!roams) {
            /* where the hop would come down: past the picture's edge, a hop on the spot */
            float x = party->position.x, z = party->position.z;
            movePos(&x, &z, HOP_REACH, party->targetDir);
            if (portCoopEdgeExcess(x, party->position.y, z) > fmaxf(out, 0.0f)) {
                partySetTargetSpd(party, 0.0f);
                party->moveSpeed = 0.0f;
            }
        }
        party->forceMoveFrames = 1; /* party_motion_jump: straight along targetDir at moveSpeed */
        if (party->moveMode != 1) {
            /* (walk mode's jump motion: another mode's (behind Mario's jump2) aims at a stale moveTarget and
               never reads forceMoveFrames) */
            partyChgMoveMode(party, 1);
        }
        partyChgMot(party, 2);
        help->hopping = 1;
        help->hopY = party->position.y;
        printf("[coop] player %d's character hops at (%.1f, %.1f, %.1f), speed %.2f\n", h + 2, party->position.x,
               party->position.y, party->position.z, party->moveSpeed);
        fflush(stdout);
        return 1;
    }
    {
        const Vec was = party->position;
        groundFall(party);
        partyMoveNoHosei(party);
        if (!roams) {
            const float now = portCoopEdgeExcess(party->position.x, party->position.y, party->position.z);
            if (now > 0.0f && now > out) {
                /* the picture's edge: held there, pushing against it */
                party->position.x = was.x;
                party->position.z = was.z;
                if (!help->held) {
                    printf("[coop] player %d's character: held at the picture's edge at (%.1f, %.1f, %.1f)\n", h + 2,
                           was.x, party->position.y, was.z);
                    fflush(stdout);
                }
                help->held = 1;
            } else if (now < -8.0f) {
                help->held = 0;
            }
        }
    }
    hopPose(party);
    party->moveDir = party->targetDir;
    return 1;
}

/* --- Enemies notice every player's character (port/include/port/players.h) --- */

#define NOTICED_MAX 4

/* the characters other players play in the field now, besides Mario (who: the player, 1 to 3):
 * Brothers mode's Luigi; the partner while player 2 walks it (else it only follows Mario, as in
 * one player's game) or player 2's lent partner; players 3 and 4's companions or followers. None
 * in one player's game. */
static int noticed(PartyEntry** out, int* who) {
    int n = 0, h;
    if (gp == NULL || gp->inBattle || seqGetSeq() != SEQ_GAME) {
        return 0;
    }
    if (portBrothersOn() && padHere(1)) {
        PartyEntry* luigi = portBrothersFieldLuigi();
        if (luigi != NULL) {
            out[n] = luigi;
            who[n++] = 1;
        }
    }
    if (!coopOn()) {
        return n;
    }
    for (h = 0; h < HELPERS && n < NOTICED_MAX; h++) {
        PartyEntry* party;
        if (!helperHere(h) || (party = helperParty(h)) == NULL || (party->flags2 & PARTY_DISP_FLAG_HIDDEN)) {
            continue;
        }
        if (h == 0 && !sHelp[0].free && party->currentSlotId != PORT_PARTY_SLOT_LENT) {
            continue;
        }
        out[n] = party;
        who[n++] = h + 1;
    }
    return n;
}

static Vec sNoticeWas, sNoticeAt;
static int sNoticeSwapped;

void portCoopNpcTargetBegin(NpcEntry* npc) {
    PartyEntry* chars[NOTICED_MAX];
    int who[NOTICED_MAX];
    PartyEntry* pick = NULL;
    MarioWork* mario;
    float best, dx, dy, dz;
    int n, i;
    if (sNoticeSwapped || npc == NULL ||
        (npc->flags & (NPC_FLAG_USED | NPC_FLAG_ENEMY)) != (NPC_FLAG_USED | NPC_FLAG_ENEMY) ||
        portStateSyncNpcTargetSwapped() || (mario = marioGetPtr()) == NULL || !marioChkKey() ||
        (n = noticed(chars, who)) == 0) {
        return;
    }
    dx = mario->position.x - npc->position.x;
    dy = mario->position.y - npc->position.y;
    dz = mario->position.z - npc->position.z;
    best = dx * dx + dy * dy + dz * dz;
    for (i = 0; i < n; i++) {
        float d;
        dx = chars[i]->position.x - npc->position.x;
        dy = chars[i]->position.y - npc->position.y;
        dz = chars[i]->position.z - npc->position.z;
        d = dx * dx + dy * dy + dz * dz;
        if (d < best) {
            best = d;
            pick = chars[i];
        }
    }
    if (pick == NULL) {
        return; /* (Mario is the nearest: as ever) */
    }
    sNoticeWas = mario->position;
    mario->position = pick->position;
    sNoticeAt = mario->position;
    sNoticeSwapped = 1;
}

void portCoopNpcTargetEnd(void) {
    if (sNoticeSwapped) {
        /* back where he was, with whatever the checks or events moved him by meanwhile (a push) */
        MarioWork* mario = marioGetPtr();
        mario->position.x = sNoticeWas.x + (mario->position.x - sNoticeAt.x);
        mario->position.y = sNoticeWas.y + (mario->position.y - sNoticeAt.y);
        mario->position.z = sNoticeWas.z + (mario->position.z - sNoticeAt.z);
        sNoticeSwapped = 0;
    }
}

static NpcEntry* sTouched; /* the enemy a player's character touched: its battle has no first strike */

/* fbatEncountCheck, after the game's own checks: an enemy touching another player's character
 * (the touch as the game measures Mario's, with its filters) starts the battle as a plain contact */
NpcEntry* portCoopFieldTouch(NpcEntry* found, void* kind) {
    PartyEntry* chars[NOTICED_MAX];
    int who[NOTICED_MAX];
    NpcWork* nw;
    MarioWork* mario;
    NpcEntry* hit = NULL;
    int hitWho = 0, n, c, i;
    float best = 1000.0f;
    if (gp == NULL || gp->fbatData == NULL || gp->fbatData->mode != 1) {
        return found; /* (a battle starting: its touch is kept for portCoopTouchedBy) */
    }
    sTouched = NULL;
    if (found != NULL || (mario = marioGetPtr()) == NULL || mario->mutekiTimer > 0 ||
        (nw = npcGetWorkPtr()) == NULL || (n = noticed(chars, who)) == 0) {
        return found;
    }
    for (c = 0; c < n; c++) {
        const PartyEntry* p = chars[c];
        const float width = p->width > 0.0f ? p->width : 24.0f;
        const float height = p->height > 0.0f ? p->height : 24.0f;
        for (i = 0; i < nw->total; i++) {
            NpcEntry* npc = &nw->entries[i];
            float dx, dz, d;
            if ((npc->flags & (NPC_FLAG_ACTIVE | NPC_FLAG_ENEMY)) != (NPC_FLAG_ACTIVE | NPC_FLAG_ENEMY) ||
                (npc->flags & (NPC_FLAG_NO_FIELD_ATTACK | NPC_FLAG_INTANGIBLE | NPC_FLAG_KOOPA_MODE)) ||
                (npc->jumpFlags & NPC_STATUS_DEAD) || npc->fbatHitCooldown ||
                ((npc->jumpFlags & NPC_STATUS_FROZEN) && !portStateSyncNpcFollowed(npc))) {
                continue;
            }
            if (p->position.y >= npc->position.y + npc->height || p->position.y + height <= npc->position.y) {
                continue;
            }
            dx = p->position.x - npc->position.x;
            dz = p->position.z - npc->position.z;
            d = sqrtf(dx * dx + dz * dz);
            if (d <= 0.5f * (width + npc->width) && d < best) {
                best = d;
                hit = npc;
                hitWho = who[c];
            }
        }
    }
    if (hit == NULL) {
        return NULL;
    }
    *(u32*)kind = FBAT_HIT_CONTACT;
    fbatSetAttackAnnounce(FBAT_HIT_CONTACT);
    sTouched = hit;
    printf("[coop] player %d's character touches '%s': a battle, no first strike\n", hitWho + 1, hit->description);
    fflush(stdout);
    return hit;
}

int portCoopTouchedBy(const NpcEntry* npc) {
    return npc != NULL && npc == sTouched;
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
static float sTagX[1 + HELPERS], sTagY[1 + HELPERS];
static int sTags; /* the tags placed this frame (tagDisp), so one never covers another */

static void tag(const Vec* top, const char* text, GXColor color) {
    Vec screen;
    float w;
    int k, moved = 1, tries = 0;
    getScreenPoint((Vec*)top, &screen);
    if (portViewMirrored()) {
        screen.x = -screen.x; /* mirror mode: the picture, not the 2D layer, is flipped */
    }
    /* two characters in one place (Brothers mode's Luigi with player 3's companion behind him: his "2P"
       was under its "3P"): the later tag a line higher */
    while (moved && tries++ < 1 + HELPERS) {
        moved = 0;
        for (k = 0; k < sTags; k++) {
            if (fabsf(screen.x - sTagX[k]) < 26.0f && fabsf(screen.y - sTagY[k]) < 16.0f) {
                screen.y = sTagY[k] + 18.0f;
                moved = 1;
            }
        }
    }
    if (sTags < 1 + HELPERS) {
        sTagX[sTags] = screen.x;
        sTagY[sTags] = screen.y;
        sTags++;
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
    static const char* kTags[HELPERS] = {"2P", "3P", "4P"};
    MarioWork* mario = marioGetPtr();
    const u32 seed = portRandState();
    Vec top;
    int h;
    (void)camId;
    (void)param;
    sTags = 0;
    top = mario->position;
    top.y += 52.0f; /* above his cap */
    tag(&top, "1P", playerGX(0, 0.0f));
    for (h = 0; h < HELPERS; h++) {
        /* (Brothers mode: "2P" over Luigi, bright while player 2 walks him) */
        const int luigi = h == 0 && portBrothersOn();
        PartyEntry* party = luigi ? portBrothersFieldLuigi() : helperParty(h);
        GXColor color = playerGX(h + 1, 0.0f);
        if (party == NULL || !(luigi ? seatKept(1) : coopOn() && helperKept(h))) {
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
        portTextFit("notice-line", lines[i], w, maxW, 1);
        if (w > maxW) {
            scale *= maxW / w; /* still too wide (no space to break at): smaller */
            w = maxW;
        }
        FontDrawScale(scale);
        /* (above the cards' row while a card or join note is there: the guest's "Playing together" line ran under
           its own 2P note, the multiplayer walkthrough) */
        FontDrawString(-w * 0.5f, (portPlayerMenuBottomUp() ? -40.0f : count == 2 ? -138.0f : -150.0f) - i * 22.0f,
                       lines[i]);
    }
}

static void noticeDisp(CameraId camId, void* param) {
    const u32 seed = portRandState(); /* (the game's random numbers: tagDisp's) */
    (void)camId;
    (void)param;
    if (sNotice.text == NULL) {
        /* gone since it was queued: a save state (or rewind) put back a frame whose draw list
         * had it, after this notice (here, not in the state) had ended */
        return;
    }
    noticeDraw();
    portRandSetState(seed);
}

static void notice(const char* english) {
    sNotice.text = english;
    sNotice.age = 0;
    printf("[coop] %s\n", english);
    fflush(stdout);
}

/* the same notice for online play's news (statesync_*.c) */
void portCoopNotice(const char* english) {
    notice(english);
}

/* each frame, before the partner's own: the notices, a joining or leaving
 * second controller (seen in the field while Mario has the controls, so one
 * joining in a scene is told once it's over, and one there from the start is
 * told at the first room), and the tag */
/* a player joined: on Aurora the note at their side (player_menu_pc.c), else the notice */
static void joined(int player, const char* english) {
#ifdef TTYD_BACKEND_AURORA
    printf("[coop] %s\n", english);
    fflush(stdout);
    portPlayerMenuJoined(player);
#else
    (void)player;
    notice(english);
#endif
}

static void seenFrame(void) {
    static int sHad = 0, sHinted, sHadMore[2];
    /* (a kept seat: still there; online, a seated guest still linked while its game catches up, a room change
       a step behind: never "left", only the link's end is) */
    const int has = seatKept(1);
    int k;
    if (fieldControl() && !sHinted && !portStateSyncOn()) {
        for (k = 1; k < PAD_MAX_CONTROLLERS; k++) {
            if (portCoopUnseated(k)) {
                sHinted = 1;
                notice("Another controller: press {PADSTART} on it to join.");
                break;
            }
        }
    }
    if (fieldControl() && has != sHad && !has && portStateSyncLinked() && !portStateSyncCoopOn() &&
        portStateSyncPlayersLinked() >= 2) {
        /* online, a party game or mode the host picked (the quiz, the cards...): co-op waits, so seat 2's
           controller isn't the partner's (or Luigi's) now, but its player is still in the room (linked, even
           while its game loads a room and says nothing for a moment): no "left"
           (nor "joined" again once the adventure together is back) */
    } else if (fieldControl() && has != sHad && portBrothersOn()) {
        /* Brothers mode: player 2 is Luigi */
        if (has) {
            joined(2, "Player 2 joined: their stick walks Luigi.");
        } else {
            notice("Player 2 left: Luigi follows Mario.");
        }
        sHad = has;
    } else if (fieldControl() && has != sHad) {
        if (has && (coopOn() || portStateSyncCoopHost())) {
            joined(2, "Player 2 joined: their stick walks the partner."); /* (online: the guest in seat 2) */
        } else if (!has && (coopOn() || portStateSyncOn())) {
            notice("Player 2 left: the partner follows Mario.");
        }
        sHad = has;
    }
    for (k = 0; k < 2; k++) {
        /* players 3 and 4 */
        const int more = coopOn() && seatKept(k + 2); /* (a kept seat: no "left" until it frees) */
        if (fieldControl() && more != sHadMore[k]) {
            if (more) {
                joined(k + 3, k == 0 ? "Player 3 joined: their stick walks a companion ({L} or {R}: another)."
                                     : "Player 4 joined: their stick walks a companion ({L} or {R}: another).");
            } else {
                notice(k == 0 ? "Player 3 left." : "Player 4 left.");
            }
            sHadMore[k] = more;
        }
    }
    /* (an online game can end before the logos load the font: the notice waits for it) */
    if (sNotice.text != NULL && pfh != NULL) {
        if (portPlayerMenuStartMenuUp()) {
            /* (held while player 1's Start menu is up on this screen: its help box is where it goes) */
        } else if (++sNotice.age >= NOTICE_FRAMES) {
            sNotice.text = NULL;
        } else if (!gp->inBattle) {
            dispEntry(CAMERA_2D, 903.0f, 1, noticeDisp, NULL);
        }
    }
    if ((portCoopPlayers() > 1 || anyHeld()) && fieldControl()) {
        dispEntry(CAMERA_2D, 902.0f, 1, tagDisp, NULL);
    }
}

/* --- Controllers disconnecting (port/include/port/players.h) --- */

#define HOLD_SECONDS 60

/* a controller plays in that seat this frame, before any kept seat (a replay: its recording's) */
static int seatedNow(int p) {
    if (portReplayActive()) {
        return Pad[p].err != PAD_ERR_NO_CONTROLLER;
    }
    return rawPad(p) && localSeated(p);
}

/* each frame: a seated controller gone (not a Leave: the controller itself) keeps its seat a minute, its
 * character back at Mario's side; back in time (in its port, or any unseated one's Start: portCoopJoinStart),
 * it plays on; else the seat frees, and the character is the partner's AI again (a companion goes) */
static void holdFrame(void) {
    int p;
    if (portStateSyncOn()) {
        memset(sHeld, 0, sizeof(sHeld)); /* (online: the session's seats, statesync's) */
        memset(sWas, 0, sizeof(sWas));
        return;
    }
    for (p = 1; p < PAD_MAX_CONTROLLERS; p++) {
        int seated;
        if (sHeld[p] > 0 && !portReplayActive() && sHeldId[p][0] != '\0' && deviceAt(p)) {
            /* a controller in the kept seat's port: theirs again only if it is the one that left (SDL puts any
               controller that connects in the next free port: another player's, a handheld's own controls) */
            char id[sizeof(sHeldId[0])];
            padIdentity(p, id, (int)sizeof(id));
            if (strcmp(id, sHeldId[p]) != 0) {
                if (!sStranger[p]) {
                    printf("[coop] controller %d isn't the one player %d's seat is kept for: it plays once its Start "
                           "joins\n", p + 1, p + 1);
                    fflush(stdout);
                }
                sStranger[p] = 1;
            } else {
                sStranger[p] = 0;
            }
        } else if (sStranger[p] && !deviceAt(p)) {
            sStranger[p] = 0; /* (the other controller gone again) */
        }
        seated = seatedNow(p);
        if (seated && deviceAt(p)) {
            padIdentity(p, sSeen[p], (int)sizeof(sSeen[p]));
        } else if (seated) {
            sSeen[p][0] = '\0';
        }
        if (sHeld[p] > 0) {
            if (seated) {
                sHeld[p] = 0;
                printf("[coop] player %d's controller is back: their seat again\n", p + 1);
                fflush(stdout);
            } else if (--sHeld[p] == 0) {
                sJoined[p] = 0;
                sStranger[p] = 0;
                printf("[coop] player %d's seat frees (its time ran out without their controller)\n", p + 1);
                fflush(stdout);
            }
        } else if (sWas[p] && !seated && (portReplayActive() || !rawPad(p)) &&
                   (coopOn() || (p == 1 && portBrothersOn()))) {
            const char* test = getenv("TTYD_SEAT_HOLD_FRAMES"); /* (tests: a shorter wait) */
            sHeld[p] = test != NULL && atoi(test) > 0 ? atoi(test) : HOLD_SECONDS * portFieldRate();
            memcpy(sHeldId[p], sSeen[p], sizeof(sHeldId[p])); /* (the device that left: only it is back by itself) */
            printf("[coop] player %d's controller disconnected: their seat is kept for %d frames\n", p + 1, sHeld[p]);
            fflush(stdout);
            if (p - 1 < HELPERS && sHelp[p - 1].free && helperParty(p - 1) != NULL) {
                recall(&sHelp[p - 1], helperParty(p - 1)); /* (waiting at Mario's side) */
            }
        }
        sWas[p] = seated;
    }
}

/* a seat swap on one computer (together_aurora.c's, after portInputSwapPorts): the seats' own state goes with
 * their players, so neither seat looks like a controller that went away. Mario's seat (port 0) has none. */
void portCoopSeatsSwapped(int a, int b) {
    int t;
    if (a < 0 || b < 0 || a >= PAD_MAX_CONTROLLERS || b >= PAD_MAX_CONTROLLERS || a == b) {
        return;
    }
    if (a >= 1 && b >= 1) {
        t = sJoined[a], sJoined[a] = sJoined[b], sJoined[b] = t;
        t = sHeld[a], sHeld[a] = sHeld[b], sHeld[b] = t;
        t = sWas[a], sWas[a] = sWas[b], sWas[b] = t;
        t = sStranger[a], sStranger[a] = sStranger[b], sStranger[b] = t;
        {
            char id[sizeof(sHeldId[0])];
            memcpy(id, sHeldId[a], sizeof(id)), memcpy(sHeldId[a], sHeldId[b], sizeof(id)), memcpy(sHeldId[b], id, sizeof(id));
            memcpy(id, sSeen[a], sizeof(id)), memcpy(sSeen[a], sSeen[b], sizeof(id)), memcpy(sSeen[b], id, sizeof(id));
        }
    } else {
        /* (with Mario's: the other seat's player is player 1's controller now, seated as theirs was: a player
           who sat in Mario's seat never has to join again) */
        sWas[a >= 1 ? a : b] = 0;
        sJoined[a >= 1 ? a : b] = 1;
        sStranger[a >= 1 ? a : b] = 0;
    }
}

static int sP1Lost;

int portCoopPadLost(void) {
    return sP1Lost;
}

/* each frame: player 1's controller gone (one computer, in play; $TTYD_PAD_UNPLUG's controller 1 in tests)
 * holds the game after this frame (vi_aurora.c) with its note (player_menu_pc.c). Only a controller that
 * leaves port 0 by itself: a seat swap's move (portInputSwaps) starts the watch afresh, and port 0 with
 * only inputs that have no device index (the keyboard, touch, a test's scripted controller) never holds.
 * Under a scripted run ($TTYD_INPUT) a real controller isn't watched either: one that sleeps or is unplugged
 * on the computer mid-test would hold the game for good, as the script never presses Start (only
 * $TTYD_PAD_UNPLUG's controller 1 is watched then) */
static void p1Frame(void) {
#ifdef TTYD_BACKEND_AURORA
    static int sHad = -1;
    static int sTest = -1;
    static int sScripted = -1;
    static unsigned sSwaps;
    int here;
    if (sTest < 0) {
        const char* input = getenv("TTYD_INPUT");
        sTest = getenv("TTYD_PAD_UNPLUG") != NULL;
        sScripted = input != NULL && *input != '\0';
    }
    if (!sTest && (sScripted || portInputActive())) {
        sHad = -1;
        return;
    }
    here = sTest ? !portInputUnplugged(0) : PADGetIndexForPort(0) >= 0;
    if (sSwaps != portInputSwaps()) {
        sSwaps = portInputSwaps(); /* (the controllers moved: what is there now is what to watch) */
        sHad = here;
        return;
    }
    if (sHad > 0 && !here && !sP1Lost && !portStateSyncOn() && !portReplayActive() && seqGetSeq() == SEQ_GAME) {
        sP1Lost = 1;
        printf("[coop] player 1's controller disconnected: the game holds until a Start\n");
        fflush(stdout);
    }
    sHad = here;
#endif
}

/* the hold's loop (vi_aurora.c), each redrawn frame: the controllers read; a Start pressed and let go on any
 * (or the keyboard's) ends it, that controller player 1's (one in another port moves to port 0; a seated
 * player's leaves their seat kept, holdFrame) */
int portCoopPadLostWait(void) {
#ifdef TTYD_BACKEND_AURORA
    static int sFrames;
    static u16 sLast[PAD_MAX_CONTROLLERS];
    static int sDown = -1;
    PADStatus st[PAD_MAX_CONTROLLERS];
    int p;
    if (!sP1Lost) {
        return 0;
    }
    if (sFrames++ == 0) {
        memset(sLast, 0xFF, sizeof(sLast)); /* (a Start held as it went: let go first) */
        sDown = -1;
    }
    PADRead(st);
    portInputApplyPresses(st); /* (a test's scripted controllers are there too: their Start counts) */
    if (getenv("TTYD_PAD_UNPLUG") != NULL) {
        /* tests: the picture at a moment in; $TTYD_PAD_LOST_START frames in (90), Start on controller 1 */
        const char* at = getenv("TTYD_PAD_LOST_START");
        const int startAt = at != NULL ? atoi(at) : 90;
        if (sFrames == 30) {
            portShotRequest("p1_lost");
        }
        if (sFrames >= startAt) {
            portInputReplug(0);
            st[0].err = PAD_ERR_NONE;
            st[0].button = sFrames == startAt ? PAD_BUTTON_START : 0;
        }
    }
    for (p = 0; p < PAD_MAX_CONTROLLERS; p++) {
        /* (any controller there: one with a device, or the keyboard, touch or a scripted one, which say so) */
        const u16 now = st[p].err == PAD_ERR_NONE || p == 0 || portInputVirtualAt(p) ? st[p].button : 0;
        if (sDown < 0 && (now & PAD_BUTTON_START) && !(sLast[p] & PAD_BUTTON_START)) {
            sDown = p;
        }
        sLast[p] = now;
    }
    if (sDown >= 0 && !(sLast[sDown] & PAD_BUTTON_START)) {
        /* (on its release: the game never sees this Start, so no Start menu opens) */
        const int from = sDown;
        if (from > 0) {
            /* (that port's controller to port 0, whatever kind: a controller, the keyboard a swap took there,
               a test's scripted one; the keyboard in port 0 stays) */
            portInputTakePort(from, 0);
        }
        printf("[coop] controller %d's Start: player 1 plays on (held %d frames)\n", from + 1, sFrames);
        fflush(stdout);
        sP1Lost = 0;
        sFrames = 0;
        return 0;
    }
    return 1;
#else
    return 0;
#endif
}

/* each frame: a player's hop counted until it lands (portCoopPartnerWalk's hopEnded). One still in the air
 * after HOP_STUCK frames never ends by itself (the jump motion left with nothing that lands it: a motion step
 * the motion doesn't have, a move mode changed under it): the character goes back to Mario's side */
#define HOP_STUCK 240
static void hopFrame(void) {
    int h;
    for (h = 0; h < HELPERS; h++) {
        Helper* help = &sHelp[h];
        PartyEntry* party;
        if (!help->hopping) {
            continue;
        }
        party = helperParty(h);
        if (party == NULL || !help->free) {
            help->hopping = 0;
            continue;
        }
        if (party->flags & PARTY_FLAG_IS_BEING_USED) {
            /* the partner's field ability began mid-hop (X from either player: Koops' shell, Yoshi's ride,
               Vivian's shadow...): the hop ends here, on the ground. Its "jumping" left on would be the
               ability's: Yoshi ridden in the air flutters on the spot for good, and B never gets Mario off */
            if (party->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING)) {
                /* (down on the floor under it: the highest floor from where it is to below where it left) */
                float y = help->hopY;
#if VERSION > VERSION_DEMO_JP
                const float drop = party->position.y - help->hopY + 40.0f;
                party->velY = 0.0f;
                if (_partySearchGround(party, drop, drop) != NULL) {
                    y = party->groundY;
                }
#endif
                party->position.y = y;
                party->flags &= ~(PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING | PARTY_FLAG_IS_STEPPING);
                party->flags |= PARTY_FLAG_IS_GROUNDED;
                party->velY = 0.0f;
                party->accelY = 0.0f;
                if (party->motionId == 2 || party->motionId == 3) {
                    partyChgMot(party, 0);
                }
            }
            printf("[coop] player %d's character: hop ends, the partner's ability begins (motion %d)\n", h + 2,
                   party->motionId);
            fflush(stdout);
            help->hopping = 0;
            continue;
        }
        /* (only the jump motion's own: an ability used mid-hop (Koops' shell, Vivian's shadow, a ride) has the
           character as long as it lasts, and gives it back in motion 0) */
        if (++help->hopping > HOP_STUCK && party->motionId == 2 && fieldControl()) {
            printf("[coop] player %d's character: a hop that never landed (motion %d step %d) at (%.1f, %.1f, %.1f)\n",
                   h + 2, party->motionId, party->motionStep, party->position.x, party->position.y,
                   party->position.z);
            recall(help, party);
        }
    }
}

/* Player 1's port again (at the start and each time a controller connects or goes): Aurora saves which
 * controller is in which port whenever one moves (PADSetPortForIndex, PADClearPort), and a seat isn't saved
 * (sJoined). So after Choose player 2's controller, a Start's move or Let player 2 go, the next launch put that
 * controller (or any of the same model: no serial) in port 2 by itself, or in no port, and with port 1 empty
 * nobody was player 1: the controller's Start made it player 2 while Mario stayed on the keyboard or touch. A
 * controller is always player 1 (docs/PLAYERS.md): with no controller in port 1 (player 1 on the keyboard or
 * touch), the first controller that isn't a player (in no port, or unseated in ports 2-4) goes there, as SDL
 * puts the first one there. Not online (the session's seats), not in a replay, not while a seat swap has the
 * keyboard elsewhere. Only on a change of the controllers: one let go on purpose (Not playing) stays out
 * until the next connects. */
static void homeFrame(void) {
#ifdef TTYD_BACKEND_AURORA
    static int sCount = -1;
    const int n = (int)PADCount();
    int i, p, pick = -1, from = -1;
    if (n == sCount) {
        return;
    }
    sCount = n;
    if (portStateSyncOn() || portReplayActive() || portInputHomePort() != 0 || PADGetIndexForPort(0) >= 0) {
        return;
    }
    for (i = 0; i < n && pick < 0; i++) {
        int at = -1;
        for (p = 0; p < PAD_MAX_CONTROLLERS; p++) {
            if (PADGetIndexForPort((u32)p) == i) {
                at = p;
            }
        }
        if (at < 0 || (at >= 1 && !sJoined[at])) {
            pick = i;
            from = at;
        }
    }
    if (pick < 0) {
        return;
    }
    PADSetPortForIndex((u32)pick, 0);
    PADSerializeMappings();
    if (from >= 1) {
        sWas[from] = 0; /* (moved, not gone: no seat to keep) */
    }
    if (from >= 1) {
        printf("[coop] port 1 had no controller: controller %d's (not a player) is player 1's now\n", from + 1);
    } else {
        printf("[coop] port 1 had no controller: a controller in no port is player 1's now\n");
    }
    fflush(stdout);
#endif
}

/* a battle text box is up (a boss's words, the BINGO's, a Tattle's) */
static int battleTextUp(void) {
    int i;
    for (i = 0; i < PORT_WINDOW_SLOTS; i++) {
        const WindowEntry* w = windowGetPointer(i);
        if ((w->flags & WINDOW_FLAG_USED) && w->userdata != NULL) {
            return 1;
        }
    }
    return 0;
}

/* everyone's moments of a battle, as an online shared battle has them (statesync_battle_pc.c ssBattleApplyPad):
 * the battle's start and end (the tally, the results; not the level-up's choice, player 1's), the BINGO reels
 * and the battle's text boxes. Whose turn it was is no matter then: with the partner's blow the last, the first
 * controller read its player's (the route below), and with that pad put down (or headless's scripted one,
 * idle) the BINGO's reels waited for an A for ever (the full run, gra_01's Hyper Paragoombas: "reels 10 10 8") */
static int everyonesMoment(void) {
    BattleWork* wp = _battleWorkPointer;
    s32 end;
    if (gp == NULL || !gp->inBattle || wp == NULL) {
        return 0;
    }
    end = wp->seqEndValue;
    if (end == 0x7000011 || end == 0x7000012) {
        return 0; /* (the level-up's choice: player 1's alone, battle_seq_end.c) */
    }
    if (BattleGetSeq(wp, SEQ_UNKNOWN) != 2 || end != 0 || BattleBreakSlotGetPtr()->phase == 3) {
        return 1;
    }
    return battleTextUp();
}

/* every seated player's A and B (player 1's included), read before the route below gives the first controller
 * to the partner's player: in everyone's moments the first controller takes them all. An online shared battle
 * does this itself (each game's own controller then, ssBattleApplyPad) */
typedef struct {
    u32 held, pressed, repeat;
} EveryonesAB;

static EveryonesAB everyonesAB(void) {
    const u32 ab = PAD_BUTTON_A | PAD_BUTTON_B;
    EveryonesAB e = {0, 0, 0};
    int p;
    if (portStateSyncSharedBattle() || !everyonesMoment()) {
        return e;
    }
    for (p = 0; p < PAD_MAX_CONTROLLERS; p++) {
        if (p == 0 || padHere(p)) {
            e.held |= gp->button[p] & ab;
            e.pressed |= gp->buttonNew[p] & ab;
            e.repeat |= gp->buttonRepeat[p] & ab;
        }
    }
    return e;
}

static void everyonesABApply(const EveryonesAB* e) {
    gp->button[0] |= e->held;
    gp->buttonNew[0] |= e->pressed;
    gp->buttonRepeat[0] |= e->repeat;
}

/* $TTYD_COOP_ANIM_TRACE (tests): each stretch player 2 walks the partner, how its animation went: frames
 * walked, the frames whose pose wasn't the one its speed calls for (the run from 1.65 a frame, the walk to
 * 0.85), and how far the animation got (its frames advanced, held, started over in the same pose). =2: the
 * partner's motion, pose and animation frame every frame too, co-op or not */
static void animTraceFrame(void) {
    static int sTrace = -1, sFrame;
    static struct {
        int frames, wrong, held, restarts;
        float advanced, lastRaw, topSpeed;
        const char* lastPose;
        char poses[64];
    } w;
    PartyEntry* party;
    const AnimationPose* pose;
    int walking;
    if (sTrace < 0) {
        const char* v = getenv("TTYD_COOP_ANIM_TRACE");
        sTrace = v != NULL ? atoi(v) : 0;
    }
    if (!sTrace) {
        return;
    }
    sFrame++;
    party = helperParty(0);
    if (party == NULL || party->paperAnimGroup < 0 || (pose = animPoseGetAnimPosePtr(party->paperAnimGroup)) == NULL) {
        return;
    }
    if (sTrace >= 2) {
        printf("[animtrace] f %d member %d motion %d run %d move %d speed %.2f free %d pose %s frame %.2f raw %.2f "
               "stick %.0f %.0f\n",
               sFrame, party->currentMemberId, party->motionId, party->runMode, party->moveMode, party->moveSpeed,
               sHelp[0].free, party->animName != NULL ? party->animName : "-", pose->localFrame, pose->rawFrame,
               sHelp[0].stickX, sHelp[0].stickY);
    }
    walking = sHelp[0].free && party->motionId == 1 && party->moveSpeed > 0.0f &&
              !(party->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING)) && party->animName != NULL &&
              party->currentMemberId < 20;
    if (walking) {
        const char* const walkName = partyPoseTbl[party->currentMemberId][2];
        const char* const runName = partyPoseTbl[party->currentMemberId][3];
        const char* const want = party->moveSpeed >= 1.65f ? runName : party->moveSpeed <= 0.85f ? walkName : NULL;
        if (w.frames == 0) {
            memset(&w, 0, sizeof(w));
        } else if (w.lastPose != NULL && strcmp(w.lastPose, party->animName) == 0) {
            if (pose->rawFrame > w.lastRaw) {
                w.advanced += pose->rawFrame - w.lastRaw;
            } else if (pose->rawFrame == w.lastRaw) {
                w.held++;
            } else {
                w.restarts++;
            }
        }
        if (want != NULL && strcmp(want, party->animName) != 0) {
            w.wrong++;
        }
        if (w.lastPose == NULL || strcmp(w.lastPose, party->animName) != 0) {
            const size_t n = strlen(w.poses);
            snprintf(w.poses + n, sizeof(w.poses) - n, "%s%s", n ? " " : "", party->animName);
        }
        w.frames++;
        w.lastPose = party->animName;
        w.lastRaw = pose->rawFrame;
        w.topSpeed = fmaxf(w.topSpeed, party->moveSpeed);
    } else if (w.frames > 0) {
        printf("[animtrace] player 2's walk: %d frames, top speed %.2f, poses %s, wrong pose %d, animation advanced "
               "%.0f frames, held %d, restarted %d\n",
               w.frames, w.topSpeed, w.poses, w.wrong, w.advanced, w.held, w.restarts);
        fflush(stdout);
        w.frames = 0;
    }
}

void portSecondPlayerRoute(void) {
    declareOn();
    animTraceFrame();
    if (gp == NULL) {
        return;
    }
    homeFrame();
    holdFrame();
    hopFrame();
    p1Frame();
    seenFrame();
    lookFrame(); /* (the Paper Mario 64 partners' copies' looks, every frame) */
    portPlayerMenuFrame(); /* players 2-4's own menus (player_menu_pc.c) */
    portBrothersDuoFrame(secondPad()); /* (Brothers mode's Bros. moves, duo moves' players) */
    if (portVersusRoute()) {
        return; /* a versus fight routes the controllers itself (versus_aurora.c) */
    }
    if (portBrothersRoute(secondPad())) {
        return; /* Brothers mode, Luigi's turn: the second player plays it (brothers_pc.c) */
    }
    if (!coopOn()) {
        if (partyGetPtr(PORT_PARTY_SLOT_COMPANION) != NULL || partyGetPtr(PORT_PARTY_SLOT_LENT) != NULL ||
            sOwnFollower) {
            fieldRoute(); /* (co-op ended: the companions go, companionsFrame) */
        }
        return;
    }
    {
        const int owner = partnerHasTurn();
        const EveryonesAB everyone = everyonesAB(); /* (before the route: player 1's own) */
        if (owner == 0 || !padHere(owner)) {
            fieldRoute(); /* (no player for the partner's turn: the first one plays it) */
            everyonesABApply(&everyone);
            return;
        }
        if (portDuoActive()) {
            return; /* a duo move: each player's controller plays their own half (duo_pc.c) */
        }
        /* (players 2-4's Start is never player 1's: their own menu's, player_menu_pc.c) */
        gp->button[0] = gp->button[owner] & ~PAD_BUTTON_START;
        gp->buttonNew[0] = gp->buttonNew[owner] & ~PAD_BUTTON_START;
        gp->buttonRepeat[0] = gp->buttonRepeat[owner] & ~PAD_BUTTON_START;
        gp->buttonRepeatDelay[0] = gp->buttonRepeatDelay[owner];
        gp->buttonUp[0] = gp->buttonUp[owner] & ~PAD_BUTTON_START;
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
        everyonesABApply(&everyone);
    }
}

/* --- Tests: "where coop" (port/players.h) --- */

int portCoopWhere(int print) {
    char line[1024], why[160];
    int n, h, settled = 1;
    const MarioWork* m = marioGetPtr();
    /* (an online co-op guest or a watcher: the host's world, its own keys off; no control is theirs to have) */
    const int watcher = portStateSyncWatcher();
    const int guest = portStateSyncCoopGuest() || watcher;
    const int inBattle = gp != NULL && gp->inBattle;
    const int seq = (int)seqGetSeq();
    const int control = marioChkKey() && marioChkCtrl();
    const int level = (int)marioStGetSystemLevel();
    const int menu = portPlayerMenuAny();
    const PartyEntry* partner = partyGetPtr(marioGetPartyId());
    why[0] = '\0';
#define MP_UNSETTLED(...)                                                                                              \
    do {                                                                                                               \
        if (settled) {                                                                                                 \
            snprintf(why, sizeof(why), __VA_ARGS__);                                                                   \
        }                                                                                                              \
        settled = 0;                                                                                                   \
    } while (0)
    n = snprintf(line, sizeof(line),
                 "[mp] where: map '%s' story %d seq %d battle %d control %d level %d menu %d players %d guest %d "
                 "watch %d partner %d | P1 Mario %.0f %.0f %.0f motion %d",
                 gp != NULL ? gp->currentMapName : "", (int)swByteGet(0), seq, inBattle, control, level, menu,
                 portCoopPlayers(), guest, watcher, partner != NULL ? (int)partner->currentMemberId : 0,
                 m != NULL ? m->position.x : 0.0f, m != NULL ? m->position.y : 0.0f, m != NULL ? m->position.z : 0.0f,
                 m != NULL ? (int)m->currMotionId : -1);
    if (seq != SEQ_GAME) {
        MP_UNSETTLED("not in the field (sequence %d)", seq);
    } else if (inBattle) {
        MP_UNSETTLED("in a battle");
    } else if (menu) {
        MP_UNSETTLED("a player's menu is up");
    } else if (level != 0) {
        MP_UNSETTLED("the game is paused or a menu is up (system level %d)", level);
    } else if (!guest && !control) {
        MP_UNSETTLED("Mario's player has no control");
    } else if (!guest && m != NULL && m->currMotionId > MARIO_MOTION_DASH) {
        MP_UNSETTLED("Mario mid-move (motion %d)", (int)m->currMotionId);
    }
    for (h = 0; h < HELPERS && !guest; h++) {
        const PartyEntry* party = helperParty(h);
        const int here = helperHere(h);
        if (party == NULL || (!here && h > 0)) {
            continue;
        }
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      " | P%d%s member %d %.0f %.0f %.0f motion %d pose %s flags %s%s hop %d free %d", h + 2,
                      here ? "" : "(nobody)", (int)party->currentMemberId, party->position.x, party->position.y,
                      party->position.z, (int)party->motionId, party->animName != NULL ? party->animName : "-",
                      (party->flags & PARTY_FLAG_IS_JUMPING) ? "J" : "-", (party->flags & PARTY_FLAG_IS_FALLING) ? "F" : "-",
                      (int)party->forceMoveFrames, sHelp[h].free);
        if (n >= (int)sizeof(line)) {
            n = (int)sizeof(line) - 1;
        }
        if (party->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING)) {
            MP_UNSETTLED("player %d's character mid-%s", h + 2,
                         (party->flags & PARTY_FLAG_IS_JUMPING) ? "jump" : "fall");
        } else if (party->forceMoveFrames != 0) {
            MP_UNSETTLED("player %d's character held in a forced move (%d frames)", h + 2, (int)party->forceMoveFrames);
        } else if (party->motionId <= 1 && party->animName != NULL && party->currentMemberId < 20 &&
                   partyPoseTbl[party->currentMemberId][4] != NULL &&
                   strcmp(party->animName, partyPoseTbl[party->currentMemberId][4]) == 0 &&
                   (partyPoseTbl[party->currentMemberId][0] == NULL ||
                    strcmp(partyPoseTbl[party->currentMemberId][4], partyPoseTbl[party->currentMemberId][0]) != 0) &&
                   (partyPoseTbl[party->currentMemberId][1] == NULL ||
                    strcmp(partyPoseTbl[party->currentMemberId][4], partyPoseTbl[party->currentMemberId][1]) != 0)) {
            /* (one that floats, Flurrie, "jumps" in her standing pose: not this) */
            /* landed (or never in the air) but still drawn in its jump pose: the hop that stayed for good */
            MP_UNSETTLED("player %d's character left in its jump pose (%s)", h + 2, party->animName);
        }
    }
#undef MP_UNSETTLED
    if (print) {
        printf("%s | %s%s\n", line, settled ? "settled" : "NOT SETTLED: ", why);
        fflush(stdout);
    }
    return settled;
}
