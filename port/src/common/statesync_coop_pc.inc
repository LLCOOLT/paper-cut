/* Playing together (co-op): one adventure, the host's world (port/include/port/statesync.h,
 * docs/STATE_SYNC.md "Playing together"). Included by statesync_pc.c.
 *
 * The host's game is the world. Every guest that joins to play (not to watch) is a co-op guest:
 * - its game follows the host's: the host's room (a room change takes it along), its own Mario and
 *   party hidden and kept where the host's Mario is (its camera follows him), its keys off; the
 *   host's Mario, partner, Luigi, follower and companion drawn there as the host's game moves them;
 *   the host's NPCs (layer 2: the host owns every room), scenes and talks (layer 4: their text boxes
 *   and camera) and story;
 * - its progress is the host's: on joining, the host's whole progress replaces what's in this game's
 *   memory (story, area flags, items, coins, Mario's level and partners), and the host's changes come
 *   as they're made. Nothing of the guest's goes the other way, and its save is never written while
 *   it plays together (cardmgr.c); leaving takes it back to the title, its own save as it was;
 * - its controller is that player's on the host's game: each frame it sends the host its last frames'
 *   controller (unreliable; the host keeps them in order and plays one a frame, at most a few frames
 *   behind), and the host's game plays it as controller 2, 3 or 4 by the player's seat, exactly as a
 *   second controller on one computer plays (second_player_pc.c): seat 2 the partner (Luigi with
 *   Brothers mode on), seats 3 and 4 the follower or a companion. The guest sees what the host's game
 *   made of it, a round trip later: the host's game alone decides, so the world can never differ.
 * - a shared battle (statesync_battle_pc.c) always takes the co-op guests as players, by seat: seat 2
 *   plays the partner's (or Luigi's) turns, seats 3 and 4 their companions'.
 * Seats, the host's pick for the lobby and the team bonus count cross as SS_R_COOP messages. */

#include "port/coop_camera.h" /* (the guests' own cameras: coop_camera_pc.c) */

#define COOP_PAD_HISTORY 8 /* frames of controller in each message (a lost one is covered by the next) */
#define COOP_PAD_QUEUE 16
/* the host plays a guest's controller at most this many frames behind: the newest that came (a room's batch
   brings three at once, a late message two; the older ones' buttons go on with it, padDropOldest) */
#define COOP_PAD_BEHIND 1
#define COOP_PAD_STALE_MS 1500
#define PAD_SAME_MS 250 /* a room's: an unchanged controller sent again this often (the host's stale is 1500) */

/* a controller's frame, as it crosses (PADStatus's fields, fixed width) */
typedef struct PadRec {
    uint16_t button;
    int8_t sx, sy, cx, cy;
    uint8_t l, r, a, b;
    uint8_t yaw[2]; /* the guest's camera yaw, low byte first (coop_camera_pc.c; 0 in a menu or battle) */
    uint32_t ext; /* the port's own buttons and the co-op ping (extButton) */
} PadRec;

typedef struct PadMsg {
    uint32_t frame; /* rec[0]'s frame (the guest's count); rec[i] is frame - i */
    uint8_t n;
    uint8_t sub; /* 0 the guest's own controller; 1 its second controller (a second player at that computer) */
    uint8_t pad[2];
    PadRec rec[COOP_PAD_HISTORY];
} PadMsg;

/* (after C_RESYNC, the room's people, statesync_coop_pc.inc's end: C_BYE the host leaves for good, C_KICK
   the host removes this player, C_INFO a guest's computer, C_ROSTER every player's computer and link; C_USE
   the guest playing Mario's Start menu: an item's HP, FP and SP or a partner switch, done in the host's game;
   C_AWAY a player stepped away or is back: v[0] 1 away, 0 back, v[1] the seconds they may be, v[2] who, the
   host passing a guest's on to the others) */
/* (C_SUB, a guest to the host: v[0] 1 its second controller asks for a seat, 0 it lets its seat go) */
enum { C_SEATS = 1, C_ASK, C_ASK_TO, C_ANSWER, C_PICK, C_BONUS, C_RESYNC, C_BYE, C_KICK, C_INFO, C_ROSTER, C_USE, C_WORN,
       C_SUB, C_AWAY };
enum { USE_ITEM = 1, USE_PARTNER };

typedef struct CoopMsg {
    uint8_t kind;
    uint8_t pad[3];
    int32_t v[6];
} CoopMsg;

WIRE_SIZE(PadRec, 16);
WIRE_SIZE(PadMsg, 136);
WIRE_SIZE(CoopMsg, 28);

static struct {
    /* the host: each guest's controller frames, in order */
    struct {
        PadRec q[COOP_PAD_QUEUE];
        int head, count;
        uint32_t last; /* the newest frame queued */
        int any;
        PadRec cur;
        unsigned long long at;
    } in[2 * (SS_PLAYERS + 1)]; /* by player; SUB_IN(p): that guest's second controller's */
    /* a guest: its own last frames */
    PadRec hist[COOP_PAD_HISTORY];
    uint32_t padFrame;
    /* seats: seat s's player (1..4; 0 nobody); the host decides, every game knows */
    int seat[5];
    /* seat s is its player's second controller's: a guest with two controllers plays two seats (seatOf is its
       first controller's, subSeatOf the second's). Never seat 1: Mario is a game's own controller's */
    int sub[5];
    int pendSeat[5], pendSub[5]; /* the host: agreed, waiting for a calm moment (pendOn) */
    int pendOn;
    int askFrom, askSeat; /* this player: a request waiting for its answer */
    /* the host's pick */
    int pick, pickA, pickB;
    unsigned pickVersion;
    /* a guest: the host's picks as they came, each followed in turn (two in one pump, a party game
       begun and over at once, never one lost behind the other) */
    struct {
        int pick, a, b;
        unsigned version;
    } pickQ[4];
    int pickQN;
    unsigned bonus;
    int was;        /* this game played as a co-op guest in this session (leaving: back to the title) */
    int took;       /* the host's whole progress taken (this session) */
    int reenter;    /* the room entered again once settled: the host's progress made the file another kind (an
                       open-world file's items were written into this room's area as the room was entered) */
    int areaPending; /* the host's area flags, for when this game gets to that area */
    char area[4];
    uint32_t lswf[0x10];
    uint8_t lsw[0x400];
    unsigned noticeAt;
    unsigned hostUnseen; /* a guest: frames in a row the host's Mario wasn't drawn here (coopFollowFrame) */
    /* each seat's character (2-4) in the host's game, and its Paper Mario 64 look (port/player_menu.h's
       portCoopCharacterOf and portCoopPm64Of there), with the seats: a guest has no follower or companion
       of its own (the host's are drawn), so its player cards take them from here */
    int chr[5], chrPm64[5];
    int chrKnown;  /* a guest: the host's come */
    int chrSent;   /* the host: chr/chrPm64 are what the guests have */
    /* the host's Paper Mario 64 looks worn, by TTYD partner (1-7: PM64_GSW_WORN's words, each game's own, never
       crossing in the progress): a guest's game draws the host's party, so its HUD, Party tab, wheel and
       partners take them from here (portStateSyncCoopHostWorn) */
    unsigned char worn[8];
    int wornKnown; /* a guest: the host's come */
    int wornSent;  /* the host: worn is what the guests have */
} C;

/* This game's memory holds a host's world (a co-op guest's, from the first merge on). Unlike C.was it
   outlives the session: "Try again", the no-service window's Back, a rejoin that fails, the moments before
   "Back to your own adventure" loads the file. Cleared only when a file is loaded, a new game begins or the
   title is reached (portStateSyncHostWorldGone); every save path checks it (portStateSyncSaveHeld,
   portStateSyncCopyHeld: cardWrite, autosave's cardCopy, quick resume, save states, rewind, the records). */
static int sHostWorld;

/* the room's people (kept across a Leave, unlike C): the players list, leaving and coming back */
static struct {
    int ownSlot;                   /* a guest: the card file it played before the host's world came (-1 none) */
    int lastSeat[SS_PLAYERS + 1];  /* the host: each player's seat when they went (theirs again on coming back) */
    int hostSeat;                  /* the host: its seat when the guest playing Mario went (back there if they
                                      come back to Mario's), 0 none */
    char lastRoom[24];             /* a guest: the room it left or lost, and its seat there (welcome back) */
    int lastMySeat;
    int welcomed;
    int platform[SS_PLAYERS + 1];  /* PORT_SYNC_OS_* by player (the host's roster) */
    int quality[SS_PLAYERS + 1];   /* 1 good, 2 fair, 3 poor (the host's link to them), 0 not known */
    int infoSent, bye, kicked;
    unsigned long long lostAt;     /* a guest: since when the host is gone (0 here) */
    unsigned long long kickAt[SS_PLAYERS + 1]; /* the host: removed, their connection closed a moment after */
    int rulesNoted;
    /* each player who said they stepped away (their phone's app in the background), until when (ssNetMs), 0
       not: their place, link and seat kept meanwhile, a shared battle plays their units (C_AWAY) */
    unsigned long long awayUntil[SS_PLAYERS + 1];
    int lostAway;    /* a guest: the host had stepped away when (or while) its game went (the longer wait) */
    int awaySelf;    /* this game told the others it stepped away (portStateSyncBackground) */
    int subWant[SS_PLAYERS + 1]; /* the host: that guest's second controller wants a seat (C_SUB) */
    int subLast[SS_PLAYERS + 1]; /* the host: its seat when its player's link went (its again on coming back) */
    int subFull[SS_PLAYERS + 1]; /* the host: "no free seat" said for it */
} K = {-1};

static void coopAwayGot(int from, const CoopMsg* m);

/* A co-op guest's second controller (two players at one computer online): its Start asks the host for a seat
   (C_SUB), the lowest free one, up to the four seats; its frames go to the host as that seat's controller (a
   PadMsg with sub 1), as the guest's own go for its seat. The host's game plays it as any seat's controller
   (second_player_pc.c), so every game draws the same; this guest's game draws that character from the host's
   word, as the other players'. Unplugged, its seat is kept a minute (the host keeps the seat while its link
   is there and nothing comes: the character waits at Mario's side); back in time, or another controller's
   Start here meanwhile, and it plays on; else the seat frees. Start held on it lets the seat go. Kept across a
   Leave (as K): a rejoin brings it back to the seat it had. */
static struct {
    int port;       /* its controller port here (1-3), 0 none */
    int want;       /* it asked for a seat and hasn't let it go */
    int seat;       /* its seat, as the host's seats say (2-4), 0 none (yet) */
    unsigned awayAt; /* G.frame it went (unplugged), 0 here */
    unsigned askedAt, resentAt;
    int startHeld;  /* frames its Start is held (SUB_LEAVE_FRAMES: its seat let go) */
    int full;       /* "no free seat" said */
    uint16_t last[4]; /* each port's buttons last frame (a Start's press) */
    PadRec hist[COOP_PAD_HISTORY];
    uint32_t frame;
} SP;
#define SUB_LEAVE_FRAMES 90 /* Start held this long on the second controller: its seat let go */
#define SUB_FULL_FRAMES 180 /* no seat this long after asking: "no free seat" */
#define SUB_RESEND_FRAMES 120 /* no seat: asked again this often */

static void coopHideOwn(int hidden);
static void coopRemember(void);
/* the delay log ($TTYD_NET_DELAY_LOG) */
static void ndGuestFrame(int afterDraw);
static void ndHostArrived(int from, uint32_t f, const PadRec* r);
static void ndHostApplied(int p, int seat, uint32_t f, int queued);
static void ndGuestRead(void);
static void ndHostSent(void);
static void sendPad(int sub, uint32_t frame, const PadRec* hist);
static void ownReset(int s, const char* why);
/* a guest's own character (below, "each guest's own character") */
static struct {
    int seat;       /* this guest walks seat's character now (portStateSyncCoopLocalSeat), 0 not */
    uint32_t epoch; /* the host's epoch for this guest's seat, as last taken */
    int known;
    unsigned swapAt;
    unsigned partnerAt; /* (players 3 and 4: the host's partner last put out here) */
    int drawn; /* its own drawn here (taken by its player), not the host's word of it */
} OG;
/* the guest's field actions (below, "the guest's field actions") */
#define ACT_PENDING 8
static struct {
    /* the guest */
    uint32_t next;     /* its actions' numbers */
    uint32_t pressPad; /* the pad read (C.padFrame) with its newest field press: an action's delay counts from it */
    uint16_t raw;      /* its controller's buttons as last read (a press: not in them then) */
    struct {
        uint32_t id;
        int kind, seat, prev, answered;
        unsigned long long at;
    } pend[ACT_PENDING];
    unsigned showUntil;           /* G.frame: its own character drawn here (an action's pose), not the host's word */
    unsigned long long swapUntil; /* ms: the host's partner or companion not put back here meanwhile (a swap's) */
    int swapTo;                   /* ... the one this game swapped to */
    int xDown;                    /* its X went as the partner's move: its letting go goes too */
    /* the host */
    int xHeldBy; /* the player whose X holds the partner's move (0 none) */
} FA;
static void actPressRead(const PADStatus* pad);
static void actStrip(PadRec* r);
static uint32_t actPendNew(int kind, int seat, int prev);
static void actAnswerUse(int to, int kind, uint32_t id, int ok, const char* what);
static int actDelay(void);
static PartyEntry* seatParty(int s);
static void gotUse(int from, const CoopMsg* m);
static void marioMenuRelease(const char* why);
static int coopOwnFile(void);
#define PICK_TOGETHER 1 /* the host's pick: the adventure (sent once the host is in a file; $TTYD_SYNC_PICK in tests) */

/* a pick that is the adventure together: none yet (a room is the host's adventure by default) or the
   adventure's pick (1). A party game's (10-15) is not; nor -1 (tests: the layers alone, each player free).
   The dungeon master's (16) is: its player commands the enemies of the host's boss rush, so every guest's
   game goes along with the host's (its battles shared, the dungeon master's controller in them); each in
   their own game, a guest stayed in the room the rush left and its dungeon master was never in a battle */
static int pickTogether(int pick) {
    return pick == 0 || pick == PICK_TOGETHER || pick == PORT_PICK_DM;
}

/* playing together: the room's default (no pick, or the adventure's) and no party mode runs. A party game
   the host picked (and $TTYD_SYNC_PICK=-1 in tests) is the games' own, as before: each player free in their
   own game, the progress merged both ways, nobody following; the party game's end (the Online screen sets
   the pick back) is the adventure together again */
static int coopTogether(void) {
    return pickTogether(C.pick) && !partyModeHold();
}

static int coopModeHeld(void) {
    return !coopTogether();
}

int portStateSyncMe(void) {
    return G.on ? ssNetMe() : 0;
}

/* a guest of a room playing together, linked to the host this moment or not: what a whole progress from the host
   asks (taken whole, never merged). Just back after the host's blip, a new session's first messages arrive before
   its hello marks the link up, so a guest that asked portStateSyncCoopGuest() merged the host's progress into its
   own, as each their own game's would (fuzz round 31: and then left the room for a file of "another kind") */
static int coopGuestRole(void) {
    return G.on && !G.watch && !ssNetIsHost() && ssNetMe() >= 2 && !coopModeHeld();
}

int portStateSyncCoopGuest(void) {
    return coopGuestRole() && ssNetConnected();
}

int portStateSyncCoopOn(void) {
    return G.on && ssNetConnected() && !coopModeHeld();
}

static int coopPlayer(int p) {
    return present(p) && G.pl[p].info.coop && !G.pl[p].info.watch;
}

int portStateSyncCoopHost(void) {
    int p;
    if (!G.on || !ssNetIsHost() || !ssNetConnected() || coopModeHeld()) {
        return 0;
    }
    for (p = 2; p <= SS_PLAYERS; p++) {
        if (coopPlayer(p)) {
            return 1;
        }
    }
    return 0;
}

/* the host's word every frame (at the frame's end) while a co-op guest plays in its field: the character the
   guest plays moved as soon as it is (else every 3 frames, SEND_EVERY) */
static int coopSendsEveryFrame(void) {
    return portStateSyncCoopHost() && G.seq == SEQ_GAME;
}

/* seat s's player: seat 1 the host's unless a guest took it (C.seat[1] 0: the host's) */
static int holder(int s) {
    if (s == 1) {
        return C.seat[1] >= 1 ? C.seat[1] : 1;
    }
    return s >= 2 && s <= 4 ? C.seat[s] : 0;
}

#define SUB_IN(p) ((p) + SS_PLAYERS + 1)

/* seat s is its player's second controller's */
static int seatSub(int s) {
    return s >= 2 && s <= 4 && C.seat[s] != 0 && C.sub[s];
}

/* the host: seat s's controller frames (C.in: its player's, or that player's second controller's) */
static int seatIn(int s) {
    const int p = holder(s);
    return seatSub(s) ? SUB_IN(p) : p;
}

/* a guest's second controller's seat (2-4), 0 none */
static int subSeatOf(int p) {
    int s;
    for (s = 2; p >= 2 && s <= 4; s++) {
        if (C.seat[s] == p && C.sub[s]) {
            return s;
        }
    }
    return 0;
}

int portStateSyncSeatSub(int seat) {
    return G.on && seatSub(seat);
}

/* the host's game: seat s's controller is here now (a guest's, fresh; or the host's own when the host sits
   in a seat other than Mario's) */
int portStateSyncCoopPadHere(int seat) {
    int p;
    if (seat < 1 || seat > 4 || !portStateSyncCoopHost()) {
        return 0;
    }
    if (gp != NULL && gp->inBattle && !portStateSyncSharedBattle()) {
        /* a battle the guests aren't in (nobody came, or the sharing ended): their controllers play the field
           only; this game's own plays every character, as alone (never a turn waiting on a seat whose player
           can't see it: coopApplyPad leaves the controllers as this computer's) */
        return 0;
    }
    p = holder(seat);
    if (p == 1) {
        return seat >= 2; /* (the host's own controller, moved to that seat) */
    }
    return p >= 2 && coopPlayer(p) && C.in[seatIn(seat)].any &&
           ssNetMs() - C.in[seatIn(seat)].at < COOP_PAD_STALE_MS;
}

int portStateSyncCoopSeatTaken(int seat) {
    int s;
    if (!G.on || !ssNetIsHost() || !ssNetConnected() || coopModeHeld()) {
        return 0;
    }
    for (s = seat == 0 ? 2 : seat; s >= 2 && s <= 4 && s <= (seat == 0 ? 4 : seat); s++) {
        const int p = holder(s);
        /* (a seat is freed once its player's link is gone, coopSeatsFrame: until then theirs) */
        if (p >= 2 && p <= SS_PLAYERS && ssNetLinked(p) && G.pl[p].info.valid && !G.pl[p].info.watch) {
            return 1;
        }
    }
    return 0;
}

int portStateSyncSeatPlayer(int seat) {
    if (seat == 1) {
        return G.on ? holder(1) : 1;
    }
    return seat >= 2 && seat <= 4 && G.on ? C.seat[seat] : 0;
}

/* a player's seat: its own controller's (a second controller's, subSeatOf) */
static int seatOf(int p) {
    int s;
    for (s = 1; s <= 4; s++) {
        if (holder(s) == p && !seatSub(s)) {
            return s;
        }
    }
    return 0;
}

/* a player's seat (1-4), 0 none (statesync_battle_pc.c: who plays which character in a shared battle) */
int portStateSyncSeatOf(int player) {
    return G.on && coopTogether() ? seatOf(player) : 0;
}

int portStateSyncCoopPart(int player) {
    int s;
    if (!G.on || !ssNetConnected() || player < 1 || player > SS_PLAYERS) {
        return PORT_COOP_PART_NONE;
    }
    if (player != 1 && (player == ssNetMe() ? G.watch : (present(player) && G.pl[player].info.watch))) {
        return PORT_COOP_PART_WATCH;
    }
    s = seatOf(player);
    if (s == 1) {
        return PORT_COOP_PART_MARIO;
    }
    if (s == 2) {
        return portBrothersOn() ? PORT_COOP_PART_LUIGI : PORT_COOP_PART_PARTNER;
    }
    return s >= 3 ? PORT_COOP_PART_COMPANION : PORT_COOP_PART_NONE;
}

static void coopSend(int to, const CoopMsg* m) {
    if (to == SS_ALL) {
        ssNetSendRel(SS_R_COOP, m, sizeof(*m));
    } else {
        ssNetSendRelTo(to, SS_R_COOP, m, sizeof(*m));
    }
}

/* the host: its worn looks (C.worn) to a guest, or to every one */
static void sendWorn(int to) {
    CoopMsg m;
    memset(&m, 0, sizeof(m));
    m.kind = C_WORN;
    memcpy(&m.v[0], C.worn, sizeof(C.worn)); /* (seven bytes and a spare: v[0] and v[1]) */
    coopSend(to, &m);
}

/* the host each frame: its worn looks, sent when one changes */
static void coopWornFrame(void) {
    unsigned char now[8];
    if (gp == NULL) {
        return;
    }
    portPm64WornGet(now);
    if (memcmp(now, C.worn, sizeof(now)) != 0 || !C.wornSent) {
        memcpy(C.worn, now, sizeof(now));
        C.wornSent = 1;
        ssLog("co-op: the host's PM64 looks %d %d %d %d %d %d %d: to the guests", now[0], now[1], now[2], now[3],
              now[4], now[5], now[6]);
        sendWorn(SS_ALL);
    }
}

int portStateSyncCoopHostWorn(int party, int* p) {
    if (!C.wornKnown || party < 1 || party > 7 || !portStateSyncCoopGuest()) {
        return 0;
    }
    *p = C.worn[party - 1];
    return 1;
}

static void sendSeats(int to) {
    CoopMsg m;
    int s;
    memset(&m, 0, sizeof(m));
    m.kind = C_SEATS;
    for (s = 1; s <= 4; s++) {
        m.v[s - 1] = holder(s) | (seatSub(s) ? 0x100 : 0); /* (0x100: its player's second controller's) */
    }
    /* (seats 2-4's characters and looks, a byte each, + 1: Brothers mode's Luigi is -1) */
    for (s = 2; s <= 4; s++) {
        m.v[4] |= (int32_t)(((unsigned)(C.chr[s] + 1) & 0xFFu) << (8 * (s - 2)));
        m.v[5] |= (int32_t)(((unsigned)(C.chrPm64[s] + 1) & 0xFFu) << (8 * (s - 2)));
    }
    m.v[4] |= 1 << 24; /* (the characters are in it) */
    coopSend(to, &m);
}

/* the host, each frame in the field: seats 2-4's characters, sent with the seats when one changes */
static int coopCharsChanged(void) {
    int s, changed = 0;
    if (gp == NULL || G.seq != SEQ_GAME || seqCheckSeq() || gp->inBattle) {
        return 0;
    }
    for (s = 2; s <= 4; s++) {
        const int c = portCoopCharacterOf(s - 1), pm = c > 0 ? portCoopPm64Of(s - 1) : 0;
        if (c != C.chr[s] || pm != C.chrPm64[s]) {
            C.chr[s] = c;
            C.chrPm64[s] = pm;
            changed = 1;
        }
    }
    if (changed || !C.chrSent) {
        C.chrSent = 1;
        ssLog("co-op: seats' characters 2:%d 3:%d 4:%d (looks %d %d %d): to the guests", C.chr[2], C.chr[3], C.chr[4],
              C.chrPm64[2], C.chrPm64[3], C.chrPm64[4]);
        return 1;
    }
    return 0;
}

/* a guest: seat (2-4)'s character in the host's game (port/statesync.h) */
int portStateSyncSeatCharacter(int seat, int* pm64) {
    if (!C.chrKnown || seat < 2 || seat > 4 || !portStateSyncCoopGuest()) {
        return PORT_STATESYNC_NO_CHARACTER;
    }
    if (pm64 != NULL) {
        *pm64 = C.chrPm64[seat];
    }
    return C.chr[seat];
}

static void sendPick(int to) {
    CoopMsg m;
    memset(&m, 0, sizeof(m));
    m.kind = C_PICK;
    m.v[0] = C.pick;
    m.v[1] = C.pickA;
    m.v[2] = C.pickB;
    m.v[3] = (int32_t)C.pickVersion;
    coopSend(to, &m);
}

/* the host: a new session with that player (statesync_pc.c ssOnSession): the seats and the pick */
static void coopSession(int player, int fresh) {
    int s;
    if (ssNetIsHost()) {
        sendSeats(player);
        if (C.wornSent) {
            sendWorn(player);
        }
        if (C.pickVersion != 0) {
            sendPick(player);
        }
        C.in[player].count = 0;
        C.in[player].any = 0;
        C.in[SUB_IN(player)].count = 0;
        C.in[SUB_IN(player)].any = 0;
        if (fresh) {
            /* (a new game in that player's place: no second controller of its until it asks; the seat its last
               one had stays its first choice, as K.lastSeat for its own) */
            K.subWant[player] = K.subFull[player] = 0;
        }
        for (s = 2; s <= 4; s++) {
            if (C.seat[s] == player) {
                ownReset(s, "its player's new session"); /* (maybe another clock: its reports start over) */
            }
        }
    } else {
        if (fresh) {
            /* (the host's whole progress again: its snapshot, which the host sends a new session alone. A resumed
               one keeps what it took: its reliable messages go on, no snapshot comes, and a guest whose link
               dropped and resumed before its join's progress step was through stayed "joining the room (step 4)"
               for good, the join's "taking long" window up) */
            C.took = 0;
        }
        if (player == 1 && SP.want) {
            CoopMsg m;
            memset(&m, 0, sizeof(m));
            m.kind = C_SUB;
            m.v[0] = 1;
            coopSend(1, &m); /* (its second controller's seat again: the host's session with it is new) */
            ssLog("co-op: this game's second controller asks for its seat again (a new session with the host)");
        }
    }
}

void portStateSyncPickSet(int pick, int a, int b) {
    if (!G.on || !ssNetIsHost()) {
        return;
    }
    C.pick = pick;
    C.pickA = a;
    C.pickB = b;
    C.pickVersion++;
    sendPick(SS_ALL);
    ssLog("co-op: the host's pick %d (%d, %d)", pick, a, b);
}

int portStateSyncPick(int* a, int* b, unsigned* version) {
    if (G.on && !ssNetIsHost() && C.pickQN > 0) {
        /* (a guest: the oldest pick not followed yet; then the next, the frame after) */
        const int pick = C.pickQ[0].pick;
        if (a != NULL) {
            *a = C.pickQ[0].a;
        }
        if (b != NULL) {
            *b = C.pickQ[0].b;
        }
        if (version != NULL) {
            *version = C.pickQ[0].version;
        }
        C.pickQN--;
        memmove(&C.pickQ[0], &C.pickQ[1], (size_t)C.pickQN * sizeof(C.pickQ[0]));
        return pick;
    }
    if (a != NULL) {
        *a = C.pickA;
    }
    if (b != NULL) {
        *b = C.pickB;
    }
    if (version != NULL) {
        *version = C.pickVersion;
    }
    return G.on ? C.pick : 0;
}

unsigned portStateSyncTeamBonusCount(void) {
    return C.bonus;
}

/* the host: a team bonus given (statesync_battle_pc.c): every game's count */
void ssCoopBonus(void) {
    CoopMsg m;
    C.bonus++;
    if (!ssNetIsHost()) {
        return;
    }
    memset(&m, 0, sizeof(m));
    m.kind = C_BONUS;
    m.v[0] = (int32_t)C.bonus;
    coopSend(SS_ALL, &m);
}

/* the host: seat s to player p (swapping with its holder), now or at the next calm moment. Seat 1 is Mario's:
   a guest there plays Mario in the host's game (its controller as controller 1), the host then the seat the
   guest had */
static void pendBegin(void) {
    int k;
    if (!C.pendOn) {
        for (k = 1; k <= 4; k++) {
            C.pendSeat[k] = holder(k);
            C.pendSub[k] = seatSub(k);
        }
    }
}

/* the host: seats a and b's players swapped, each with its controller (a second controller's seat moves as
   its seat), at the next calm moment. Mario's seat is never a second controller's, nor nobody's */
static int seatSwapPend(int a, int b, const char** why) {
    int t;
    if (a < 1 || a > 4 || b < 1 || b > 4 || a == b) {
        *why = "Choose two seats to swap.";
        return 0;
    }
    pendBegin();
    if ((a == 1 && (C.pendSub[b] || C.pendSeat[b] == 0)) || (b == 1 && (C.pendSub[a] || C.pendSeat[a] == 0))) {
        *why = "That seat has no controller.";
        ssLog("co-op: seats %d and %d not swapped: Mario's seat would be a second controller's or nobody's", a, b);
        return 0;
    }
    t = C.pendSeat[a], C.pendSeat[a] = C.pendSeat[b], C.pendSeat[b] = t;
    t = C.pendSub[a], C.pendSub[a] = C.pendSub[b], C.pendSub[b] = t;
    C.pendOn = 1;
    return 1;
}

static void seatMove(int s, int p) {
    int from = 0, k, was;
    const char* why = NULL;
    if (s < 1 || s > 4 || p < 1 || p > SS_PLAYERS) {
        return;
    }
    pendBegin();
    for (k = 1; k <= 4; k++) {
        if (C.pendSeat[k] == p && !C.pendSub[k]) {
            from = k; /* (its own controller's seat: a second controller's stays where it is) */
        }
    }
    if (from == s) {
        return;
    }
    was = C.pendSeat[s];
    if (from != 0) {
        if (!seatSwapPend(s, from, &why)) {
            return;
        }
    } else if (was == 1 && !C.pendSub[s]) {
        return; /* (the host never leaves the seats: it takes the one it gives up) */
    } else {
        C.pendSeat[s] = p; /* (a player with no seat: the one there, a second controller too, has none now) */
        C.pendSub[s] = 0;
        C.pendOn = 1;
    }
    ssLog("co-op: player %d to seat %d (seat %d's player %d to seat %d), at the next calm moment", p, s, s, was,
          from);
}

void portStateSyncSeatSet(int seat, int player) {
    if (G.on && ssNetIsHost()) {
        seatMove(seat, player);
    }
}

int portStateSyncSeatSwap(int a, int b, const char** why) {
    *why = NULL;
    if (!G.on || !ssNetIsHost()) {
        *why = "Choose two seats to swap.";
        return 0;
    }
    if (!seatSwapPend(a, b, why)) {
        return 0;
    }
    ssLog("co-op: seats %d and %d swap (players %d and %d), at the next calm moment", a, b, C.pendSeat[b],
          C.pendSeat[a]);
    return 1;
}

int portStateSyncSeatPending(void) {
    return G.on && C.pendOn;
}

/* the host: player from asks for seat: an empty seat taken; else its holder is asked (the host itself here) */
static void askSeat(int from, int seat) {
    const int h = holder(seat);
    if (seat < 1 || seat > 4 || h == from) {
        return;
    }
    if (h == 0 || (h != 1 && !present(h))) {
        seatMove(seat, from);
    } else if (h == 1) {
        C.askFrom = from; /* (the host's own answer, on its Online screen) */
        C.askSeat = seat;
        ssLog("co-op: player %d asks for seat %d: this game (the host) is asked", from, seat);
    } else if (from == 1) {
        CoopMsg q;
        memset(&q, 0, sizeof(q));
        q.kind = C_ASK_TO;
        q.v[0] = from;
        q.v[1] = seat;
        coopSend(h, &q);
        ssLog("co-op: the host asks for seat %d: player %d is asked", seat, h);
    } else {
        CoopMsg q;
        memset(&q, 0, sizeof(q));
        q.kind = C_ASK_TO;
        q.v[0] = from;
        q.v[1] = seat;
        coopSend(h, &q);
        ssLog("co-op: player %d asks for seat %d: player %d is asked", from, seat, h);
    }
}

void portStateSyncSeatAsk(int seat) {
    CoopMsg m;
    if (!G.on || seat < 1 || seat > 4 || ssNetMe() < 1) {
        return;
    }
    if (ssNetIsHost()) {
        askSeat(1, seat);
        return;
    }
    memset(&m, 0, sizeof(m));
    m.kind = C_ASK;
    m.v[0] = seat;
    coopSend(1, &m);
}

int portStateSyncSeatAsked(int* fromPlayer, int* seat) {
    if (!G.on || C.askFrom == 0) {
        return 0;
    }
    if (fromPlayer != NULL) {
        *fromPlayer = C.askFrom;
    }
    if (seat != NULL) {
        *seat = C.askSeat;
    }
    return 1;
}

void portStateSyncSeatAnswer(int yes) {
    CoopMsg m;
    if (!G.on || C.askFrom == 0) {
        return;
    }
    memset(&m, 0, sizeof(m));
    m.kind = C_ANSWER;
    m.v[0] = yes != 0;
    m.v[1] = C.askFrom;
    m.v[2] = C.askSeat;
    C.askFrom = C.askSeat = 0;
    if (ssNetIsHost()) {
        if (m.v[0]) {
            seatMove(m.v[2], m.v[1]); /* (the host said yes itself) */
        }
        return;
    }
    coopSend(1, &m);
}

static void coopRel(int from, const uint8_t* p, size_t n) {
    CoopMsg m;
    int s;
    if (n < sizeof(m)) {
        return;
    }
    memcpy(&m, p, sizeof(m));
    switch (m.kind) {
        case C_SEATS:
            if (from == 1 && !ssNetIsHost()) {
                const int before = seatOf(ssNetMe());
                for (s = 1; s <= 4; s++) {
                    const int who = m.v[s - 1] & 0xFF;
                    C.seat[s] = who >= 1 && who <= SS_PLAYERS ? who : 0;
                    C.sub[s] = s >= 2 && C.seat[s] != 0 && (m.v[s - 1] & 0x100) != 0;
                }
                if (seatOf(ssNetMe()) != before) {
                    C.noticeAt = 0; /* (a new seat: said again) */
                }
                ssLog("co-op: seats now 1:%d 2:%d%s 3:%d%s 4:%d%s (this game player %d)", holder(1), C.seat[2],
                      C.sub[2] ? "b" : "", C.seat[3], C.sub[3] ? "b" : "", C.seat[4], C.sub[4] ? "b" : "", ssNetMe());
                {
                    /* this game's second controller's seat, as the host has it */
                    static const char* const kPlays[3] = {
                        "Your second controller plays 2P (hold {PADSTART} on it to stop).",
                        "Your second controller plays 3P (hold {PADSTART} on it to stop).",
                        "Your second controller plays 4P (hold {PADSTART} on it to stop)."};
                    const int sub = subSeatOf(ssNetMe());
                    if (sub != SP.seat) {
                        ssLog("co-op: this game's second controller: seat %d (was %d)", sub, SP.seat);
                        if (sub >= 2 && SP.want) {
                            portCoopNotice(kPlays[sub - 2]);
                        }
                        SP.seat = sub;
                        SP.full = 0;
                    }
                }
                if (m.v[4] & (1 << 24)) {
                    int changedChr = !C.chrKnown;
                    for (s = 2; s <= 4; s++) {
                        const int c = (int)(((unsigned)m.v[4] >> (8 * (s - 2))) & 0xFFu) - 1;
                        const int pm = (int)(((unsigned)m.v[5] >> (8 * (s - 2))) & 0xFFu) - 1;
                        changedChr |= c != C.chr[s] || pm != C.chrPm64[s];
                        C.chr[s] = c;
                        C.chrPm64[s] = pm;
                    }
                    C.chrKnown = 1;
                    if (changedChr) {
                        ssLog("co-op: the host's seats' characters 2:%d 3:%d 4:%d (looks %d %d %d)", C.chr[2], C.chr[3],
                              C.chr[4], C.chrPm64[2], C.chrPm64[3], C.chrPm64[4]);
                    }
                }
                {
                    /* back in the room it left or lost: its seat said (the same one again, as a rule) */
                    static const char* const kAgain[4] = {
                        "Welcome back: you're Mario again.", "Welcome back: you're 2P again.",
                        "Welcome back: you're 3P again.", "Welcome back: you're 4P again."};
                    static const char* const kNow[3] = {"Welcome back: you're 2P now.", "Welcome back: you're 3P now.",
                                                        "Welcome back: you're 4P now."};
                    const int mine = seatOf(ssNetMe());
                    /* (Mario's seat comes back only as the same one: the host gives it, never another's) */
                    if (mine >= 1 && K.lastMySeat >= 1 && (mine >= 2 || K.lastMySeat == 1) && !K.welcomed &&
                        !strcmp(K.lastRoom, ssNetCode())) {
                        K.welcomed = 1;
                        portCoopNotice(mine == K.lastMySeat ? kAgain[mine - 1] : kNow[mine - 2]);
                        ssLog("co-op: welcome back: seat %d (%s)", mine, mine == K.lastMySeat ? "again" : "now");
                    }
                }
            }
            break;
        case C_PICK:
            if (from == 1 && !ssNetIsHost()) {
                const int was = C.pick;
                C.pick = m.v[0];
                C.pickA = m.v[1];
                C.pickB = m.v[2];
                C.pickVersion = (unsigned)m.v[3];
                if (C.pickQN == (int)(sizeof(C.pickQ) / sizeof(C.pickQ[0]))) {
                    memmove(&C.pickQ[0], &C.pickQ[1], sizeof(C.pickQ) - sizeof(C.pickQ[0]));
                    C.pickQN--;
                }
                C.pickQ[C.pickQN].pick = C.pick;
                C.pickQ[C.pickQN].a = C.pickA;
                C.pickQ[C.pickQN].b = C.pickB;
                C.pickQ[C.pickQN].version = C.pickVersion;
                C.pickQN++;
                ssLog("co-op: the host picked %d (%d, %d)", C.pick, C.pickA, C.pickB);
                if (pickTogether(C.pick) && !pickTogether(was) && G.baseline) {
                    CoopMsg q;
                    memset(&q, 0, sizeof(q));
                    q.kind = C_RESYNC; /* (playing together from now: the host's whole world here) */
                    coopSend(1, &q);
                } else if (!pickTogether(C.pick) && pickTogether(was)) {
                    coopHideOwn(0); /* (a party game: this player's own again) */
                    watchShow(0);
                    if (gp != NULL && seqGetSeq() == SEQ_GAME && !marioChkKey()) {
                        marioKeyOn();
                    }
                }
            }
            break;
        case C_BONUS:
            if (from == 1 && !ssNetIsHost()) {
                C.bonus = (unsigned)m.v[0];
            }
            break;
        case C_ASK:
            if (ssNetIsHost() && from >= 2) {
                askSeat(from, m.v[0]);
            }
            break;
        case C_SUB:
            if (ssNetIsHost() && from >= 2 && from <= SS_PLAYERS && K.subWant[from] != (m.v[0] != 0)) {
                /* (an ask again while it waits, or a let-go said twice: nothing new) */
                K.subWant[from] = m.v[0] != 0;
                K.subFull[from] = 0;
                if (!K.subWant[from]) {
                    K.subLast[from] = 0;
                }
                ssLog("co-op: player %d's second controller %s", from,
                      K.subWant[from] ? "asks for a seat" : "lets its seat go");
            }
            break;
        case C_RESYNC:
            if (ssNetIsHost() && from >= 2 && G.baseline) {
                ssLog("co-op: player %d's progress differs from this game's: the whole of it sent again", from);
                sendSnapshot(from);
            }
            break;
        case C_ASK_TO:
            if (from == 1 && m.v[0] >= 1 && m.v[0] <= SS_PLAYERS && m.v[1] >= 1 && m.v[1] <= 4) {
                C.askFrom = m.v[0];
                C.askSeat = m.v[1];
            } else if (from == 1) {
                ssLog("co-op: the host's ask for player %d, seat %d: not a player or seat, dropped", m.v[0], m.v[1]);
            }
            break;
        case C_ANSWER:
            if (ssNetIsHost() && m.v[0] && m.v[2] >= 1 && m.v[2] <= 4 && holder(m.v[2]) == from) {
                seatMove(m.v[2], m.v[1]);
            }
            break;
        case C_BYE:
        case C_KICK:
            if (from == 1 && !ssNetIsHost()) {
                ssLog("co-op: %s", m.kind == C_BYE ? "the host left the room" : "the host removed this player");
                if (m.kind == C_BYE) {
                    K.bye = 1;
                } else {
                    K.kicked = 1;
                }
            }
            break;
        case C_INFO:
            if (ssNetIsHost() && from >= 2 && from <= SS_PLAYERS) {
                K.platform[from] = m.v[0] >= PORT_SYNC_OS_NONE && m.v[0] <= PORT_SYNC_OS_WEB ? m.v[0] : PORT_SYNC_OS_NONE;
            }
            break;
        case C_USE:
            gotUse(from, &m);
            break;
        case C_ROSTER:
            if (from == 1 && !ssNetIsHost()) {
                for (s = 1; s <= SS_PLAYERS; s++) {
                    const int os = m.v[s - 1] & 0xFF, q = (m.v[s - 1] >> 8) & 0xFF;
                    K.platform[s] = os <= PORT_SYNC_OS_WEB ? os : PORT_SYNC_OS_NONE; /* (else not known) */
                    K.quality[s] = q <= 3 ? q : 0;
                }
            }
            break;
        case C_WORN:
            if (from == 1 && !ssNetIsHost()) {
                unsigned char w[8];
                memcpy(w, &m.v[0], sizeof(w));
                if (!C.wornKnown || memcmp(w, C.worn, sizeof(w)) != 0) {
                    ssLog("co-op: the host's PM64 looks %d %d %d %d %d %d %d", w[0], w[1], w[2], w[3], w[4], w[5], w[6]);
                }
                memcpy(C.worn, w, sizeof(w));
                C.wornKnown = 1;
            }
            break;
        case C_AWAY:
            coopAwayGot(from, &m);
            break;
        default:
            break;
    }
}

/* a controller plugged into the host's computer plays seat s itself (two on one computer and a friend
   online): an online guest isn't put there */
int portInputHomePort(void); /* input_pc.c (port/platform.h): the keyboard's port, a seat swap's */
static int localSeat(int s) {
#ifdef TTYD_BACKEND_AURORA
    /* (a controller there, or the keyboard a seat swap took to another seat than Mario's) */
    return PADGetIndexForPort((u32)(s - 1)) >= 0 || (s > 1 && portInputHomePort() == s - 1);
#else
    (void)s;
    return 0;
#endif
}

/* the host: seat s kept for another player than p, who stepped away from it (their phone's app in the
   background) and may come back to it: never given to a newcomer meanwhile */
static int seatKeptFor(int s, int p) {
    int q;
    for (q = 2; q <= SS_PLAYERS; q++) {
        if (q != p && K.lastSeat[q] == s && ssPlayerAway(q)) {
            return 1;
        }
    }
    return 0;
}

/* the host, each frame: every co-op guest in a seat (the lowest free one, in the order they came), the
   agreed changes made when it's calm, the seats sent when they change */
static void coopSeatsFrame(void) {
    int p, s, changed = 0;
    {
        /* tests (the headless build has no Online screen): $TTYD_SYNC_SEAT_ASK_AT=<frame>:<seat> this player
           asks for a seat then; $TTYD_SYNC_SEAT_YES=1 this player says yes to any ask */
        static int sAskAt = -1, sAskSeat, sYes = -1;
        if (sAskAt < 0) {
            const char* v = getenv("TTYD_SYNC_SEAT_ASK_AT");
            sAskAt = v != NULL && strchr(v, ':') != NULL ? (int)strtol(v, NULL, 10) : 0;
            sAskSeat = v != NULL && strchr(v, ':') != NULL ? (int)strtol(strchr(v, ':') + 1, NULL, 10) : 0;
            v = getenv("TTYD_SYNC_SEAT_YES");
            sYes = v != NULL && *v == '1';
        }
        if (sAskAt > 0 && G.frame >= (unsigned)sAskAt && ssNetConnected()) {
            sAskAt = 0;
            ssLog("test: this player asks for seat %d", sAskSeat);
            portStateSyncSeatAsk(sAskSeat);
        }
        if (sYes && C.askFrom != 0) {
            ssLog("test: this player says yes to player %d for seat %d", C.askFrom, C.askSeat);
            portStateSyncSeatAnswer(1);
        }
    }
    if (!ssNetIsHost()) {
        return;
    }
    {
        /* $TTYD_SYNC_PICK=<n> (tests, the headless build has no Online screen): the host's pick */
        static int sEnvPick, sEnvRead;
        if (!sEnvRead) {
            const char* v = getenv("TTYD_SYNC_PICK");
            sEnvRead = 1;
            sEnvPick = v != NULL ? (int)strtol(v, NULL, 10) : 0;
        }
        if (sEnvPick != 0 && C.pickVersion == 0) { /* (before anyone joins: their session starts with it) */
            portStateSyncPickSet(sEnvPick, 0, 0);
        }
    }
    for (s = 1; s <= 4; s++) {
        if (s == 1 || C.seat[s] == 0) {
            C.sub[s] = 0; /* (a seat freed or given anywhere: whoever takes it next plays it with their own) */
        }
    }
    for (s = 1; s <= 4; s++) {
        const int h = holder(s);
        if (h >= 2 && !ssNetLinked(h) && seatSub(s)) {
            /* (its second controller's: the same seat again when its player comes back, if it's free) */
            ssLog("co-op: seat %d's player %d (its second controller) is gone: the seat is free", s, h);
            K.subLast[h] = s;
            C.seat[s] = 0;
            C.sub[s] = 0;
            changed = 1;
        } else if (h >= 2 && !ssNetLinked(h)) {
            ssLog("co-op: seat %d's player %d is gone: the seat is free", s, h);
            K.lastSeat[h] = s; /* (theirs again if they come back before another takes it) */
            if (s == 1) {
                /* (Mario's: the host takes it back, its own seat free; the host's seat kept in mind for when
                   the guest comes back to Mario's) */
                const int mine = seatOf(1);
                if (mine >= 2) {
                    C.seat[mine] = 0;
                }
                K.hostSeat = mine;
                C.seat[1] = 1;
            } else {
                C.seat[s] = 0;
            }
            changed = 1;
        }
    }
    for (p = 2; p <= SS_PLAYERS; p++) {
        if (coopPlayer(p) && seatOf(p) == 0 && K.kickAt[p] == 0) {
            const int last = K.lastSeat[p];
            K.lastSeat[p] = 0;
            if (last >= 2 && last <= 4 && C.seat[last] == 0 && !localSeat(last)) {
                C.seat[last] = p;
                changed = 1;
                ssLog("co-op: player %d is back: seat %d again", p, last);
                continue;
            }
            if (last == 1 && holder(1) == 1) {
                /* back to Mario's seat: the host to the seat it had while this player was Mario (else the
                   lowest free one; none free: this player takes a free seat as anyone would) */
                int to = K.hostSeat >= 2 && K.hostSeat <= 4 && C.seat[K.hostSeat] == 0 && !localSeat(K.hostSeat)
                             ? K.hostSeat
                             : 0;
                for (s = 2; to == 0 && s <= 4; s++) {
                    if (holder(s) == 0 && !localSeat(s)) {
                        to = s;
                    }
                }
                if (to != 0 && !C.pendOn) {
                    C.seat[to] = 1;
                    C.seat[1] = p;
                    K.hostSeat = 0;
                    changed = 1;
                    ssLog("co-op: player %d is back: seat 1 again (the host to seat %d)", p, to);
                    continue;
                }
            }
            for (s = 2; s <= 4; s++) {
                if (holder(s) == 0 && !localSeat(s) && !seatKeptFor(s, p)) {
                    C.seat[s] = p;
                    C.sub[s] = 0;
                    changed = 1;
                    ssLog("co-op: player %d plays seat %d (%s)", p, s,
                          s == 2 ? (portBrothersOn() ? "Luigi" : "the partner") : "a companion");
                    break;
                }
            }
        }
    }
    for (p = 2; p <= SS_PLAYERS; p++) {
        /* each guest's second controller: a seat while it wants one (after its own controller's: the lowest free
           one, or the one it had), none once it lets go or its player has none */
        const int sub = subSeatOf(p);
        if (sub != 0 && (!K.subWant[p] || seatOf(p) == 0 || K.kickAt[p] != 0)) {
            ssLog("co-op: seat %d (player %d's second controller) is free", sub, p);
            C.seat[sub] = 0;
            C.sub[sub] = 0;
            for (s = 2; C.pendOn && s <= 4; s++) {
                if (C.pendSeat[s] == p && C.pendSub[s]) {
                    C.pendSeat[s] = C.pendSub[s] = 0; /* (nor back with a swap agreed meanwhile) */
                }
            }
            changed = 1;
        } else if (sub == 0 && K.subWant[p] && coopPlayer(p) && seatOf(p) != 0 && K.kickAt[p] == 0 && !C.pendOn) {
            int to = K.subLast[p] >= 2 && K.subLast[p] <= 4 && C.seat[K.subLast[p]] == 0 && !localSeat(K.subLast[p])
                         ? K.subLast[p]
                         : 0;
            for (s = 2; to == 0 && s <= 4; s++) {
                if (holder(s) == 0 && !localSeat(s) && !seatKeptFor(s, p)) {
                    to = s;
                }
            }
            if (to != 0) {
                C.seat[to] = p;
                C.sub[to] = 1;
                K.subLast[p] = 0;
                K.subFull[p] = 0;
                changed = 1;
                ssLog("co-op: player %d's second controller plays seat %d (%s)", p, to,
                      to == 2 ? (portBrothersOn() ? "Luigi" : "the partner") : "a companion");
            } else if (!K.subFull[p]) {
                K.subFull[p] = 1;
                ssLog("co-op: player %d's second controller waits: no free seat (the four are taken)", p);
            }
        }
    }
    if (C.pendOn && G.seq == SEQ_GAME && !seqCheckSeq() && !gp->inBattle && !ssBattleHeld()) {
        memcpy(C.seat, C.pendSeat, sizeof(C.seat));
        memcpy(C.sub, C.pendSub, sizeof(C.sub));
        C.sub[1] = 0;
        C.pendOn = 0;
        changed = 1;
        ssLog("co-op: the seats changed: 1:%d 2:%d%s 3:%d%s 4:%d%s", holder(1), C.seat[2], C.sub[2] ? "b" : "",
              C.seat[3], C.sub[3] ? "b" : "", C.seat[4], C.sub[4] ? "b" : "");
    }
    if (coopCharsChanged()) {
        changed = 1; /* (a companion lent or switched: every guest's player cards show it) */
    }
    coopWornFrame();
    if (changed) {
        sendSeats(SS_ALL);
    }
}

/* ---- the controllers ---- */

static void padTo(PadRec* r, const PADStatus* s) {
    memset(r, 0, sizeof(*r));
    r->button = (uint16_t)(s->button & ~PAD_BUTTON_START); /* (Start is each game's own: its menu) */
    r->sx = s->stickX;
    r->sy = s->stickY;
    r->cx = s->substickX;
    r->cy = s->substickY;
    r->l = s->triggerLeft;
    r->r = s->triggerRight;
    r->a = s->analogA;
    r->b = s->analogB;
#ifdef TTYD_BACKEND_AURORA
    r->ext = s->extButton;
#endif
}

/* the host's status line: whose controllers play, and where the partner they move is */
static void coopHostLog(void) {
    PartyEntry* party;
    int s;
    if (!portStateSyncCoopHost() || G.frame % G.logEvery != 0 || G.seq != SEQ_GAME) {
        return;
    }
    party = partyGetPtr(marioGetPartyId());
    if (holder(1) >= 2 && marioGetPtr() != NULL) {
        ssLog("co-op: Mario at (%.1f, %.1f, %.1f), moved by player %d", marioGetPtr()->position.x,
              marioGetPtr()->position.y, marioGetPtr()->position.z, holder(1));
    }
    for (s = 1; s <= 4; s++) {
        if (portStateSyncCoopPadHere(s) || (s == 1 && holder(1) >= 2)) {
            const int h = holder(s);
            const PadRec* r = &C.in[seatIn(s)].cur;
            if (h == 1) {
                ssLog("co-op: seat %d is this game's own controller (the host's)", s);
            } else {
                ssLog("co-op: seat %d is player %d's %scontroller (stick %d %d, buttons %04x, %d frames queued)", s, h,
                      seatSub(s) ? "second " : "", r->sx, r->sy, r->button, C.in[seatIn(s)].count);
            }
            if (s == 2 && party != NULL) {
                ssLog("co-op: the partner at (%.1f, %.1f, %.1f), moved by player %d", party->position.x,
                      party->position.y, party->position.z, h);
            }
        }
    }
}

/* the host: guest p's oldest queued frame dropped (behind, or the queue full). A frame's analogA may carry a
   one-frame mark (player_menu_pc.c: an emote, a partner picked, top three bits set): it goes on with the
   next frame, so a catch-up under load never loses a guest's pick (pm64x_online_both) */
static void padDropOldest(int p) {
    const uint8_t a = C.in[p].q[C.in[p].head].a;
    const uint16_t button = C.in[p].q[C.in[p].head].button;
    C.in[p].head = (C.in[p].head + 1) % COOP_PAD_QUEUE;
    C.in[p].count--;
    if ((a & 0xE0) == 0xE0 && C.in[p].count > 0 && (C.in[p].q[C.in[p].head].a & 0xE0) != 0xE0) {
        C.in[p].q[C.in[p].head].a = a;
    }
    if (C.in[p].count > 0) {
        /* (a button pressed in a dropped frame is pressed in the next: a tap shorter than the catch-up isn't
           lost; one let go shows a frame later) */
        C.in[p].q[C.in[p].head].button |= button;
    }
}

static void padFrom(PADStatus* s, const PadRec* r) {
    s->button = r->button;
    s->stickX = r->sx;
    s->stickY = r->sy;
    s->substickX = r->cx;
    s->substickY = r->cy;
    s->triggerLeft = r->l;
    s->triggerRight = r->r;
    s->analogA = r->a;
    s->analogB = r->b;
#ifdef TTYD_BACKEND_AURORA
    s->extButton = r->ext;
#endif
    s->err = PAD_ERR_NONE;
}

/* $TTYD_SYNC_OWN_SEEK=<frame> (tests): from that frame this co-op guest's stick walks its own character
   (walked here) straight at the nearest enemy (a field battle's NPC, as the host's game has them here), so
   a test shows the host's enemy touched by a character the guest's game walks, whatever the map's layout
   between them. "<frame>:block": at the nearest block or switch with an event instead, A (a hop, a jump)
   pressed once beneath it */
MapObjectWork* portMobjGetWork(void); /* mobjdrv.c */
static int seatFree(int s);

static void ownSeekTest(PADStatus* pad) {
    static int sAt = -1, sBlock, sJumped, sNpc;
    static u16 sPress; /* a button pressed once on getting there, the seek over (the field actions' tests) */
    PartyEntry* party;
    NpcWork* wp;
    float best = 1e30f, bx = 0.0f, bz = 0.0f, want, yaw, bestErr = 1e9f;
    int i, k;
    if (sAt < 0) {
        /* "<frame>[:block|:npc][:a|:b|:x]": ":npc" at the nearest NPC to talk to (not an enemy); a button: pressed
           once within reach of the NPC or enemy (26 units: B's talk reaches 60, Luigi's hammer 34), facing it, and
           the seek over */
        const char* v = getenv("TTYD_SYNC_OWN_SEEK");
        const char* t = v != NULL ? strchr(v, ':') : NULL;
        sAt = v != NULL ? (int)strtol(v, NULL, 10) : 0;
        for (; t != NULL; t = strchr(t + 1, ':')) {
            const size_t len = strcspn(t + 1, ":");
            sBlock |= len == 5 && strncmp(t + 1, "block", 5) == 0;
            sNpc |= len == 3 && strncmp(t + 1, "npc", 3) == 0;
            if (len == 1) {
                sPress = t[1] == 'a' ? PAD_BUTTON_A : t[1] == 'b' ? PAD_BUTTON_B : t[1] == 'x' ? PAD_BUTTON_X : sPress;
            }
        }
    }
    if (sAt <= 0 || G.frame < (unsigned)sAt || (party = seatParty(OG.seat)) == NULL || (wp = npcGetWorkPtr()) == NULL) {
        return;
    }
    if (sBlock) {
        const MapObjectWork* mw = portMobjGetWork();
        for (i = 0; mw != NULL && i < mw->count; i++) {
            const MapObjectEntry* m = &mw->entries[i];
            float d;
            if (m->name[0] == 0 || m->position.y < party->position.y + 20.0f || m->position.y > party->position.y + 120.0f) {
                continue; /* (a block above the ground, to be hit from below) */
            }
            d = hypotf(m->position.x - party->position.x, m->position.z - party->position.z);
            if (d < best) {
                best = d, bx = m->position.x, bz = m->position.z;
            }
        }
        if (G.frame % 60 == 0) {
            ssLog("test: this guest's own character at (%.0f, %.0f) seeks the block at (%.0f, %.0f), %.0f away",
                  party->position.x, party->position.z, bx, bz, best);
        }
        if (best < 6.0f) {
            pad->stickX = pad->stickY = 0;
            if (!sJumped) {
                sJumped = 1;
                pad->button |= PAD_BUTTON_A;
                ssLog("test: this guest's own character jumps beneath the block at (%.0f, %.0f)", bx, bz);
            }
            return;
        }
        sJumped = 0;
        if (best > 1e29f) {
            return;
        }
        i = wp->total; /* (no enemy sought) */
    }
    for (i = sBlock ? wp->total : 0; i < wp->total; i++) {
        const NpcEntry* npc = &wp->entries[i];
        float d;
        if (!(npc->flags & NPC_FLAG_USED) ||
            (sNpc ? (npc->talkEvt == NULL || (npc->flags & (NPC_FLAG_ENEMY | NPC_FLAG_HIDDEN)) ||
                     npc->deadEvent != NULL || fabsf(npc->position.y - party->position.y) >= 60.0f)
                  : (npc->battleInfo.groupSetup == NULL || npc->deadEvent == NULL ||
                     /* (one standing in the room, as enemyToSeatTest's: not a hidden, untouchable spare, e.g.
                        the Pale Piranhas' "_s0" copies kept under win_00, which Luigi swung at from above) */
                     !(npc->flags & NPC_FLAG_ACTIVE) ||
                     (npc->flags & (NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_DISPLAY)) ||
                     npc->position.y <= -500.0f))) {
            /* (an NPC to talk to: a talk, on this floor, as B's finds it (second_player_pc.c's interactFind); not
               an enemy (a dead event). Townsfolk have a battle group too: the game's default) */
            continue;
        }
        d = hypotf(npc->position.x - party->position.x, npc->position.z - party->position.z);
        if (d < best) {
            best = d, bx = npc->position.x, bz = npc->position.z;
        }
    }
    if (best > 1e29f) {
        if (G.frame % 60 == 0) {
            ssLog("test: this guest's own character at (%.0f, %.0f) finds no %s here (%d NPCs)", party->position.x,
                  party->position.z, sNpc ? "NPC to talk to" : "enemy", wp->total);
        }
        return;
    }
    if (sPress != 0 && !sBlock && best < 26.0f) {
        /* (there: facing it, the button pressed once, as its player's press read this frame) */
        pad->stickX = pad->stickY = 0;
        pad->button |= sPress;
        FA.pressPad = C.padFrame;
        party->targetDir = party->moveDir = angleABf(party->position.x, party->position.z, bx, bz);
        sAt = 0;
        ssLog("test: this guest's own character, %.0f from the %s at (%.0f, %.0f), presses %s", best,
              sNpc ? "NPC" : "enemy", bx, bz, sPress == PAD_BUTTON_B ? "B" : sPress == PAD_BUTTON_X ? "X" : "A");
        return;
    }
    want = angleABf(party->position.x, party->position.z, bx, bz);
    yaw = portCoopStickYaw(OG.seat - 1, marioGetPtr()->targetYaw);
    for (k = 0; k < 64; k++) {
        /* (the stick whose walk, as the partner's walk turns it by the camera's yaw, points there) */
        const float a = (float)k * (360.0f / 64.0f);
        const float sx = 70.0f * sinf(a * 0.0174533f), sy = 70.0f * cosf(a * 0.0174533f);
        const float dir = revise360(angleABf(0.0f, 0.0f, sx, -sy) + yaw);
        float err = fabsf(dir - want);
        err = err > 180.0f ? 360.0f - err : err;
        if (err < bestErr) {
            bestErr = err;
            pad->stickX = (s8)sx;
            pad->stickY = (s8)sy;
        }
    }
    if (G.frame % 60 == 0) {
        ssLog("test: this guest's own character at (%.0f, %.0f) seeks the %s at (%.0f, %.0f), %.0f away (stick %d %d, "
              "taken %d)",
              party->position.x, party->position.z, sNpc ? "NPC" : "enemy", bx, bz, best, pad->stickX, pad->stickY,
              seatFree(OG.seat));
    }
}

/* the pad read (DEMOPad.c, before a shared battle's): a co-op guest keeps its controller to send; the
   host plays each seated guest's as that seat's controller */
/* a controller in that port here (1-3): a device, the keyboard or touch a swap took there, a test's scripted one */
int portInputUnplugged(int port); /* input_pc.c */
int portInputHasAny(int port);    /* input_pc.c (port/platform.h) */
static int subThere(const PADStatus* pads, int k) {
    return k >= 1 && k < PAD_MAX_CONTROLLERS && pads[k].err == PAD_ERR_NONE && portInputHasAny(k) &&
           !portInputUnplugged(k);
}

static void subLetGo(const char* why, const char* notice) {
    CoopMsg m;
    memset(&m, 0, sizeof(m));
    m.kind = C_SUB;
    m.v[0] = 0;
    coopSend(1, &m);
    ssLog("co-op: this game's second controller (port %d) lets its seat go (%s)", SP.port + 1, why);
    if (notice != NULL) {
        portCoopNotice(notice);
    }
    SP.want = 0;
    SP.port = 0;
    SP.awayAt = 0;
    SP.startHeld = 0;
}

/* a co-op guest each frame (its controllers read): its second controller's Start, its seat kept while it's away,
   its frames to the host (SP above) */
static void subGuestFrame(PADStatus* pads) {
    static int sHold = -1;
    int k, pressed = 0;
    if (sHold < 0) {
        const char* test = getenv("TTYD_SEAT_HOLD_FRAMES"); /* (tests: a shorter wait, as on one computer) */
        sHold = test != NULL && atoi(test) > 0 ? atoi(test) : 60 * 60;
    }
    for (k = 1; k < PAD_MAX_CONTROLLERS && k < 4; k++) {
        const uint16_t now = subThere(pads, k) ? (uint16_t)pads[k].button : 0;
        if ((now & PAD_BUTTON_START) && !(SP.last[k] & PAD_BUTTON_START) && k != SP.port && pressed == 0) {
            pressed = k;
        }
        SP.last[k] = now;
    }
    if (!SP.want) {
        if (pressed != 0) {
            CoopMsg m;
            memset(&m, 0, sizeof(m));
            m.kind = C_SUB;
            m.v[0] = 1;
            coopSend(1, &m);
            SP.want = 1;
            SP.port = pressed;
            SP.awayAt = 0;
            SP.startHeld = 0;
            SP.full = 0;
            SP.askedAt = SP.resentAt = G.frame;
            SP.seat = subSeatOf(ssNetMe());
            ssLog("co-op: controller %d here pressed Start: this game's second controller asks the host for a seat",
                  pressed + 1);
        }
        return;
    }
    if (!subThere(pads, SP.port) && pressed != 0) {
        ssLog("co-op: controller %d here pressed Start: it plays the second controller's seat", pressed + 1);
        SP.port = pressed; /* (its seat kept, and another controller here takes it, as on one computer) */
    }
    if (!subThere(pads, SP.port)) {
        if (SP.awayAt == 0) {
            SP.awayAt = G.frame | 1;
            ssLog("co-op: this game's second controller disconnected: its seat is kept for %d frames", sHold);
            portCoopNotice("Your second controller is away: its seat is kept.");
        } else if (G.frame - SP.awayAt > (unsigned)sHold) {
            subLetGo("its time ran out without it", "Your second controller left its seat.");
        }
        return;
    }
    if (SP.awayAt != 0) {
        SP.awayAt = 0;
        ssLog("co-op: this game's second controller is back: its seat again");
        portCoopNotice("Your second controller is back.");
    }
    if (pads[SP.port].button & PAD_BUTTON_START) {
        if (++SP.startHeld == SUB_LEAVE_FRAMES) {
            subLetGo("its Start held", "Your second controller left its seat.");
            return;
        }
    } else {
        SP.startHeld = 0;
    }
    if (SP.seat == 0 && G.frame - SP.resentAt >= SUB_RESEND_FRAMES) {
        /* (no seat yet: asked again now and then, so an ask the host had before its session with this game began
           afresh, or one it dropped, never leaves the controller waiting for good) */
        CoopMsg m;
        memset(&m, 0, sizeof(m));
        m.kind = C_SUB;
        m.v[0] = 1;
        coopSend(1, &m);
        SP.resentAt = G.frame;
    }
    if (SP.seat == 0 && !SP.full && G.frame - SP.askedAt > SUB_FULL_FRAMES) {
        SP.full = 1;
        ssLog("co-op: this game's second controller has no seat yet (the host's four are taken)");
        portCoopNotice("No free seat for your second controller.");
    }
    memmove(&SP.hist[1], &SP.hist[0], sizeof(SP.hist[0]) * (COOP_PAD_HISTORY - 1));
    if (G.seq == SEQ_GAME && !gp->inBattle && !winCheck()) {
        padTo(&SP.hist[0], &pads[SP.port]);
        {
            const uint16_t yaw = portCoopCameraYawWire(); /* (one screen: the same picture as its own controller's) */
            SP.hist[0].yaw[0] = (uint8_t)(yaw & 0xFF);
            SP.hist[0].yaw[1] = (uint8_t)(yaw >> 8);
        }
    } else {
        memset(&SP.hist[0], 0, sizeof(SP.hist[0])); /* (a menu, a battle: nothing for the field) */
    }
    SP.frame++;
    sendPad(1, SP.frame, SP.hist);
}

static void coopApplyPad(PADStatus* pads) {
    int s;
    if (portStateSyncCoopGuest()) {
        subGuestFrame(pads);
        memmove(&C.hist[1], &C.hist[0], sizeof(C.hist[0]) * (COOP_PAD_HISTORY - 1));
        if (pads[0].err == PAD_ERR_NONE && G.seq == SEQ_GAME && !gp->inBattle && !winCheck()) {
            padTo(&C.hist[0], &pads[0]);
            {
                /* this guest's camera yaw, so its stick walks as its own picture shows (coop_camera_pc.c) */
                const uint16_t yaw = portCoopCameraYawWire();
                C.hist[0].yaw[0] = (uint8_t)(yaw & 0xFF);
                C.hist[0].yaw[1] = (uint8_t)(yaw >> 8);
            }
            ndGuestRead();
            actStrip(&C.hist[0]); /* (its own character's actions: played here, sent as actions) */
        } else {
            memset(&C.hist[0], 0, sizeof(C.hist[0])); /* (a menu, a battle: nothing for the field) */
        }
        C.padFrame++;
        actPressRead(&pads[0]); /* (after the count: this frame's pad read) */
        sendPad(0, C.padFrame, C.hist); /* (as it's read: on its way before the frame's update) */
        if (SP.want && SP.port >= 1 && SP.port < PAD_MAX_CONTROLLERS) {
            /* (the second controller is the host's game's, for its seat: nothing of it played here) */
            const s8 err = pads[SP.port].err;
            memset(&pads[SP.port], 0, sizeof(pads[SP.port]));
            pads[SP.port].err = err;
        }
        if (OG.seat >= 2 && OG.seat <= 4 && pads[0].err == PAD_ERR_NONE) {
            /* this game walks its seat's character: its controller is that seat's here too (Mario's keys are off
               here; Start stays the player's own menu) */
            pads[OG.seat - 1] = pads[0];
            pads[OG.seat - 1].button &= (u16)~PAD_BUTTON_START;
            ownSeekTest(&pads[OG.seat - 1]);
        }
        return;
    }
    if (!portStateSyncCoopHost()) {
        return;
    }
    if (gp != NULL && gp->inBattle) {
        /* (a battle: a shared one's controllers are its places', ssBattleApplyPad, from this computer's own in
           pads[0]; one the guests aren't in plays from this computer's controllers, the host's own as Mario's) */
        return;
    }
    {
        /* the host's own controller where the host sits (Mario's seat, or the one it took from a guest
           playing Mario), the guests' at theirs */
        const PADStatus own = pads[0];
        const int mine = seatOf(1);
        if (mine >= 2 && mine <= 4) {
            pads[mine - 1] = own;
            pads[mine - 1].button &= (u16)~PAD_BUTTON_START; /* (the pause menu is Mario's player's) */
            memset(&pads[0], 0, sizeof(pads[0])); /* (until the guest playing Mario's controller comes) */
            pads[0].err = PAD_ERR_NONE;
        }
    }
    for (s = 1; s <= 4; s++) {
        const int p = holder(s), q = seatIn(s); /* (q: its controller's frames, a second controller's own) */
        if (p < 2 || !portStateSyncCoopPadHere(s)) {
            continue;
        }
        while (C.in[q].count > COOP_PAD_BEHIND) {
            padDropOldest(q); /* (behind: catch up) */
        }
        if (C.in[q].count > 0) {
            const uint32_t f = C.in[q].last - (uint32_t)(C.in[q].count - 1); /* (the head's frame) */
            C.in[q].cur = C.in[q].q[C.in[q].head];
            C.in[q].head = (C.in[q].head + 1) % COOP_PAD_QUEUE;
            C.in[q].count--;
            if (q == p) {
                ndHostApplied(p, s, f, C.in[q].count);
            }
        }
        padFrom(&pads[s - 1], &C.in[q].cur);
        if ((C.in[q].cur.a & 0xE0) == 0xE0) {
            C.in[q].cur.a = 0; /* (a mark is one frame's: not again while no newer frame has come) */
        }
        if (s == 1 && G.pl[p].info.keysOff) {
            /* (the guest playing Mario is in their own pause menu: Mario stands still meanwhile) */
            pads[0].button = 0;
            pads[0].stickX = pads[0].stickY = pads[0].substickX = pads[0].substickY = 0;
        }
        portCoopCameraSeatYaw(s, (uint16_t)(C.in[q].cur.yaw[0] | (C.in[q].cur.yaw[1] << 8)));
    }
}

/* ---- the guest playing Mario: its Start is Mario's Start menu ---- */

/* The guest's game draws the big menu over its copy of the host's progress (its pouch is the host's, kept
   so by the progress's changes). While it's up the host's world stands still as for the host's own menu
   (MenuHold: the system level and the field battles held, everyone's "Paused by" the guest). What it
   changes for the shared game comes to the host: an item's removal through the pouch's changes (sendDiffs),
   its HP, FP and SP and a partner switch as C_USE. Badges worn from it stay on the guest's copy. */
static int sMarioMenuHold; /* the host: player number whose Start menu holds the world, 0 none */

static void marioMenuRelease(const char* why) {
    if (sMarioMenuHold == 0) {
        return;
    }
    if (gp != NULL && marioStGetSystemLevel() == 0xF) {
        fbatChangeMode(1);
        marioCtrlOn2();
        partyCtrlOn();
        marioStSystemLevel(0);
    }
    ssLog("co-op: player %d's Start menu (Mario's) %s: the world goes on", sMarioMenuHold, why);
    sMarioMenuHold = 0;
}

/* the host each frame: the guest playing Mario has its Start menu up (its pages, 1-99): the world held */
static void coopMarioMenuFrame(void) {
    const int p = portStateSyncCoopHost() ? holder(1) : 0;
    /* (bit 1 alone: its own menu; bit 4 is a test's word that it sees the host's last pause, statesync_pc.c) */
    const int want = p >= 2 && present(p) && (G.pl[p].info.paused & 1) && G.pl[p].info.seq == SEQ_GAME && gp != NULL &&
                     G.seq == SEQ_GAME && !seqCheckSeq() && !gp->inBattle && !ssBattleBusy() && !winCheck();
    if (sMarioMenuHold != 0 && (!want || sMarioMenuHold != p)) {
        marioMenuRelease(want ? "changed hands" : "closed");
    }
    if (want && sMarioMenuHold == 0 && marioStGetSystemLevel() == 0) {
        marioStSystemLevel(4);
        marioCtrlOff2();
        partyCtrlOff();
        fbatChangeMode(7);
        sMarioMenuHold = p;
        ssLog("co-op: player %d (Mario) opened the Start menu: the world stands still", p);
    }
}

int portStateSyncCoopMarioMenuHeld(void) {
    return sMarioMenuHold != 0 && portStateSyncCoopHost();
}

/* the guest's game: this player plays Mario and the host's Mario is free (the field, no scene, no battle,
   no menu of the host's): Mario's Start menu may open here (win_main.c, past its own Mario's keys, which a
   co-op guest's never has) */
int portStateSyncCoopMarioMenuOk(void) {
    const SsPeer* h;
    if (!portStateSyncCoopGuest() || !coopTogether() || holder(1) != ssNetMe() || gp == NULL || G.seq != SEQ_GAME ||
        gp->inBattle || ssBattleBusy() || ssScenePulled()) {
        return 0;
    }
    h = ssPeerOf(1);
    return h->valid && h->seq == SEQ_GAME && !h->inBattle && !h->keysOff && !h->paused;
}

/* (v[5]: the guest's number for it, the host's answer names it: "the guest's field actions") */
static void sendUse(int what, int a, int b, int c, int d, uint32_t id) {
    CoopMsg m;
    memset(&m, 0, sizeof(m));
    m.kind = C_USE;
    m.v[0] = what;
    m.v[1] = a;
    m.v[2] = b;
    m.v[3] = c;
    m.v[4] = d;
    m.v[5] = (int32_t)id;
    coopSend(1, &m);
}

void portStateSyncCoopItemUsed(int item, int target) {
    const ItemData* d;
    uint32_t id;
    if (!portStateSyncCoopGuest() || holder(1) != ssNetMe() || item <= 0 || item >= ITEM_TYPE_MAX) {
        return;
    }
    d = &itemDataTable[item];
    id = actPendNew(PORT_COOP_ACT_ITEM, 1, item);
    sendUse(USE_ITEM, item, target, d->hp_restored | (d->fp_restored << 8), d->sp_restored, id);
    ssLog("co-op: item %d used from Mario's Start menu (on %d): to the host's game", item, target);
    ssLog("fieldact: item #%u (%d on %d) shown here %d frames after its press", id, item, target, actDelay());
}

void portStateSyncCoopPartnerPicked(int partner) {
    uint32_t id;
    if (!portStateSyncCoopGuest() || holder(1) != ssNetMe() || partner <= 0 || partner > 7) {
        return;
    }
    id = actPendNew(PORT_COOP_ACT_MENU_PARTNER, 1, partner);
    sendUse(USE_PARTNER, partner, 0, 0, 0, id);
    ssLog("co-op: partner %d picked from Mario's Start menu: to the host's game", partner);
    ssLog("fieldact: menu partner #%u (%d) shown here %d frames after its press", id, partner, actDelay());
}

/* the host: what the guest playing Mario did from its Start menu, here (itemUseFunc's numbers), and its answer */
static void gotUse(int from, const CoopMsg* m) {
    const int kind = m->v[0] == USE_ITEM ? PORT_COOP_ACT_ITEM : PORT_COOP_ACT_MENU_PARTNER;
    const uint32_t id = (uint32_t)m->v[5];
    if (!ssNetIsHost() || from < 2) {
        return;
    }
    if (holder(1) != from || gp == NULL || G.seq != SEQ_GAME || gp->inBattle) {
        actAnswerUse(from, kind, id, 0, holder(1) != from ? "not Mario's player" : "the host's field isn't free");
        return;
    }
    if (m->v[0] == USE_ITEM && (m->v[1] <= 0 || m->v[1] >= ITEM_TYPE_MAX || m->v[2] < 0 || m->v[2] > 7)) {
        actAnswerUse(from, kind, id, 0, "no such item or target");
        return;
    }
    if (m->v[0] == USE_PARTNER &&
        (m->v[1] < 1 || m->v[1] > 7 || !(pouchGetPtr()->partyData[m->v[1]].flags & POUCH_PARTY_FLAG_JOINED) ||
         (pouchGetPtr()->partyData[m->v[1]].flags & POUCH_PARTY_FLAG_AWAY))) {
        actAnswerUse(from, kind, id, 0, "that partner isn't here");
        return;
    }
    actAnswerUse(from, kind, id, 1, m->v[0] == USE_ITEM ? "used" : "switched");
    if (m->v[0] == USE_ITEM) {
        const int target = m->v[2], hp = m->v[3] & 0xFF, fp = (m->v[3] >> 8) & 0xFF, sp = m->v[4] & 0xFF;
        MarioWork* mario = marioGetPtr();
        if (target == 0) {
            pouchSetHP((s16)(pouchGetHP() + hp));
            if (hp > 0 && mario != NULL) {
                effRecoveryEntry(0, hp, mario->position.x, mario->position.y + mario->wPlayerCollisionBox.y,
                                 mario->position.z);
            }
        } else if (target >= 1 && target <= 7) {
            pouchSetPartyHP(target, pouchGetPartyHP((PartyId)target) + hp);
            portHealOtherPartners(target, hp);
        }
        pouchSetFP((s16)(pouchGetFP() + fp));
        pouchSetAP((s16)(pouchGetAP() + sp));
        ssLog("co-op: player %d's item %d from Mario's Start menu: HP +%d (on %d), FP +%d, SP +%d", from, m->v[1], hp,
              target, fp, sp);
    } else if (m->v[0] == USE_PARTNER && m->v[1] >= 1 && m->v[1] <= 7) {
        portQuickSwapPickCoop(m->v[1]); /* (the quick swap's way: at the field's next free frame) */
        ssLog("co-op: player %d picked partner %d from Mario's Start menu", from, m->v[1]);
    }
}

/* the delay log's presses followed (below, "the delay log") */
static struct {
    /* the guest: the press followed */
    int on;
    uint32_t frame;
    unsigned long long readMs, queuedMs, outMs, inMs;
    float baseIn[3], baseDrawn[3];
    int haveIn, haveDrawn;
    unsigned startFrame;
    /* the host: each guest's press followed */
    struct {
        int on, applied;
        uint32_t frame;
        unsigned long long arrivedMs;
        PadRec prev;
        float base[3];
        int haveBase;
    } h[SS_PLAYERS + 1];
} ND;

/* a guest: its controller's last frames to the host (sub 1: its second controller's). A room on the relay
   (each message part of its day): a frame the same as the last sent isn't sent again for PAD_SAME_MS (the host
   plays the last it has until a newer comes: the same thing; a changed one goes at once, its frames before it
   in its history). By address every frame goes, as before */
static void sendPad(int sub, uint32_t frame, const PadRec* hist) {
    static PadRec sLast[2];
    static unsigned long long sLastMs[2];
    static int sHave[2];
    const int k = sub != 0;
    PadMsg m;
    if (ssNetRoom() && sHave[k] && !memcmp(&hist[0], &sLast[k], sizeof(sLast[k])) &&
        ssNetMs() - sLastMs[k] < PAD_SAME_MS && !(k == 0 && ND.on && ND.queuedMs == 0)) {
        if (k == 0) {
            ndGuestFrame(0);
        }
        return;
    }
    sLast[k] = hist[0];
    sLastMs[k] = ssNetMs();
    sHave[k] = 1;
    memset(&m, 0, sizeof(m));
    m.frame = frame;
    m.n = COOP_PAD_HISTORY;
    m.sub = (uint8_t)(sub != 0);
    memcpy(m.rec, hist, sizeof(m.rec));
    if (sub == 0 && ND.on && ND.queuedMs == 0 && (int32_t)(C.padFrame - ND.frame) >= 0) {
        ND.queuedMs = ssNetMs();
    }
    ssNetSendTo(1, SS_MSG_PAD, &m, sizeof(m));
    if (sub == 0) {
        ndGuestFrame(0);
    }
}

static void gotPad(int from, const uint8_t* p, size_t n) {
    PadMsg m;
    int i, k, q;
    int32_t ahead;
    if (!ssNetIsHost() || from < 2 || from > SS_PLAYERS || n < sizeof(m)) {
        return;
    }
    memcpy(&m, p, sizeof(m));
    q = m.sub ? SUB_IN(from) : from; /* (its second controller's frames: their own queue) */
    k = m.n > COOP_PAD_HISTORY ? COOP_PAD_HISTORY : m.n;
    ahead = (int32_t)(m.frame - C.in[q].last);
    if (C.in[q].any && (ahead > 1000 || ahead < -1000)) {
        C.in[q].any = 0; /* (its game began again, or another game in its place: a count of its own) */
    }
    if (!C.in[q].any) {
        C.in[q].last = m.frame - 1;
        C.in[q].count = 0;
        C.in[q].any = 1;
        k = 1;
    }
    for (i = k - 1; i >= 0; i--) {
        const uint32_t f = m.frame - (uint32_t)i;
        if ((int32_t)(f - C.in[q].last) > 0) {
            if (C.in[q].count >= COOP_PAD_QUEUE) {
                padDropOldest(q);
            }
            C.in[q].q[(C.in[q].head + C.in[q].count) % COOP_PAD_QUEUE] = m.rec[i];
            C.in[q].count++;
            C.in[q].last = f;
            if (q == from) {
                ndHostArrived(from, f, &m.rec[i]);
            }
        }
    }
    C.in[q].at = ssNetMs();
}

/* ---- the guest: the host's progress, whole ---- */

/* the host's whole progress in place of this game's (a co-op guest's first, and after a new session) */
static void takeSnapshot(const Progress* in) {
    static Progress sTaken; /* (the payload sits at any offset: copied out, then checked) */
    const Progress* r = &sTaken;
    PouchData* pd = pouchGetPtr();
    Progress mine;
    int i, l, id, items = 0;
    uint8_t kindWas[64], kindNow[64];
    memcpy(&sTaken, in, sizeof(sTaken));
    progressCheck(&sTaken, "the host's snapshot"); /* (statesync_pc.c) */
    readProgress(&mine);
    {
        /* what differed, for the log (a DRIFT's cause): the first few story flags and words */
        char diff[400];
        int at = 0, shown = 0;
        for (i = 0; i < 0x100 * 32 && shown < 8 && at < (int)sizeof(diff) - 24; i++) {
            if (((mine.gswf[i >> 5] ^ r->gswf[i >> 5]) >> (i & 31)) & 1) {
                at += snprintf(diff + at, sizeof(diff) - (size_t)at, " F%d=%u", i, (r->gswf[i >> 5] >> (i & 31)) & 1);
                shown++;
            }
        }
        for (i = 1; i < 0x800 && shown < 12 && at < (int)sizeof(diff) - 24; i++) {
            if (!ownWord(i) && mine.gsw[i] != r->gsw[i]) {
                at += snprintf(diff + at, sizeof(diff) - (size_t)at, " W%d=%d(%d)", i, r->gsw[i], mine.gsw[i]);
                shown++;
            }
        }
#define SS_DIFF_STAT(f)                                                                                          \
    if (mine.stats.f != r->stats.f && at < (int)sizeof(diff) - 24) {                                            \
        at += snprintf(diff + at, sizeof(diff) - (size_t)at, " %s=%d(%d)", #f, (int)r->stats.f, (int)mine.stats.f); \
        shown++;                                                                                                 \
    }
        SS_DIFF_STAT(level)
        SS_DIFF_STAT(baseMaxHP)
        SS_DIFF_STAT(baseMaxFP)
        SS_DIFF_STAT(maximumBP)
        SS_DIFF_STAT(starPoints)
        SS_DIFF_STAT(maximumSP)
        SS_DIFF_STAT(rank)
        SS_DIFF_STAT(starPowers)
        SS_DIFF_STAT(jump)
        SS_DIFF_STAT(hammer)
#undef SS_DIFF_STAT
        if (!memcmp(r->area, mine.area, 4)) {
            for (i = 0; i < 0x10 * 32 && at < (int)sizeof(diff) - 24; i++) {
                if (((mine.lswf[i >> 5] ^ r->lswf[i >> 5]) >> (i & 31)) & 1) {
                    at += snprintf(diff + at, sizeof(diff) - (size_t)at, " LF%d=%u", i, (r->lswf[i >> 5] >> (i & 31)) & 1);
                    shown++;
                }
            }
            for (i = 0; i < 0x400 && at < (int)sizeof(diff) - 24; i++) {
                if (mine.lsw[i] != r->lsw[i]) {
                    at += snprintf(diff + at, sizeof(diff) - (size_t)at, " LW%d=%d(%d)", i, r->lsw[i], mine.lsw[i]);
                    shown++;
                }
            }
        }
        for (l = 1; l < 8 && at < (int)sizeof(diff) - 64; l++) { /* (slot 0: no partner, readStats) */
            if (mine.stats.partyFlags[l] != r->stats.partyFlags[l] ||
                mine.stats.partyHPLevel[l] != r->stats.partyHPLevel[l] ||
                mine.stats.partyAttack[l] != r->stats.partyAttack[l] ||
                mine.stats.partyTech[l] != r->stats.partyTech[l]) {
                /* (not its base max HP alone: the HP level's, as pouchRevisePartyParam sets it, and a partner never
                   joined keeps pouchInit's 10 in one file where another's was revised: s51-001's party3..7) */
                at += snprintf(diff + at, sizeof(diff) - (size_t)at, " party%d=%x/%d/%d/%d/%d(%x/%d/%d/%d/%d)", l,
                               r->stats.partyFlags[l], r->stats.partyHPLevel[l], r->stats.partyAttack[l],
                               r->stats.partyTech[l], r->stats.partyBaseMaxHP[l], mine.stats.partyFlags[l],
                               mine.stats.partyHPLevel[l], mine.stats.partyAttack[l], mine.stats.partyTech[l],
                               mine.stats.partyBaseMaxHP[l]);
                shown++;
            }
        }
        if (shown > 0) {
            ssLog("co-op: the host's progress differs here in:%s", diff);
        }
    }
    fileKind(gp->gsw, kindWas);
    memcpy(gp->gswf, r->gswf, sizeof(gp->gswf));
    for (i = 1; i < 0x800; i++) {
        if (!ownWord(i)) {
            gp->gsw[i] = r->gsw[i];
        }
    }
    gp->gsw0 = r->gsw0;
    fileKind(gp->gsw, kindNow);
    if (memcmp(kindWas, kindNow, sizeof(kindWas)) != 0 && seqGetSeq() == SEQ_GAME) {
        /* (this guest's own file another kind, an open-world one: its seed's items were written into this room's
           area as it was entered, and stay until the area is entered again; the host's room has the host's items.
           The room entered again, as the host's file has it: nm51f l51-024's "an item here is gone there" DRIFT) */
        C.reenter = 1;
        ssLog("co-op: the host's file is another kind than this one was: this room entered again as the host's");
    }
    if (!memcmp(r->area, mine.area, 4)) {
        memcpy(gp->lswf, r->lswf, sizeof(gp->lswf));
        memcpy(gp->lsw, r->lsw, sizeof(gp->lsw));
        C.areaPending = 0;
    } else {
        /* (for when this game gets to the host's area: its flags start again as it comes) */
        memcpy(C.area, r->area, 4);
        memcpy(C.lswf, r->lswf, sizeof(C.lswf));
        memcpy(C.lsw, r->lsw, sizeof(C.lsw));
        C.areaPending = 1;
    }
    for (l = 0; l < LISTS; l++) {
        for (id = 1; id < IDS; id++) {
            int k;
            for (k = mine.count[l][id]; k < r->count[l][id] && addItem(l, id); k++) {
                items++;
            }
            for (k = r->count[l][id]; k < mine.count[l][id] && removeItem(l, id); k++) {
                items++;
            }
        }
    }
    pd->coins = r->coins;
    pd->starPieceCount = r->starPieces;
    pd->shineSpriteCount = r->shines;
    pd->level = r->stats.level;
    pd->baseMaxHP = r->stats.baseMaxHP;
    pd->baseMaxFP = r->stats.baseMaxFP;
    pd->maximumBP = r->stats.maximumBP;
    pd->starPoints = r->stats.starPoints;
    pd->maximumSP = r->stats.maximumSP;
    pd->rank = r->stats.rank;
    pd->starPowersObtained = r->stats.starPowers;
    pd->jumpLevel = r->stats.jump;
    pd->hammerLevel = r->stats.hammer;
    for (i = 1; i < 8; i++) { /* (slot 0: no partner, readStats) */
        PouchPartyData* d = &pd->partyData[i];
        d->flags = r->stats.partyFlags[i];
        d->HPLevel = r->stats.partyHPLevel[i];
        d->attackLevel = r->stats.partyAttack[i];
        d->techLevel = r->stats.partyTech[i];
        d->baseMaxHP = r->stats.partyBaseMaxHP[i];
    }
    pouchReviseMarioParam();
    pouchRevisePartyParam();
    {
        const int was = sRelFrom;
        sRelFrom = 1; /* (the host's party as this game now has it: its leaves from here on, peerParty) */
        (void)peerParty(&r->stats, 0);
        sRelFrom = was;
    }
    C.took = 1;
    if (!C.was) {
        K.ownSlot = coopOwnFile(); /* (before the host's world: the file to go back to) */
    }
    C.was = 1;
    sHostWorld = 1;
    G.merges++;
    ssLog("co-op: the host's whole progress taken here (story %d, %d item changes, coins %d, level %d)", gp->gsw0,
          items, pd->coins, pd->level);
}

/* a guest, at each digest of the host's (parts: what differs, by bit: 1 the story, 2 the pouch, 4 the stats, 8 the
   area; 0 nothing; -1 not to compare: either game's progress held by a battle, a scene or a mode, or the host's
   digest kept here for after this game's battle, from before its changes crossed): the same part differing at two
   digests in a row (a change on its way comes within a round trip: an item picked up just as one digest went,
   then another part just as the next, is two changes on their way, not a drift) is a drift (a change lost on the
   way, or this game's own), healed at once: a
   co-op guest asks for the host's whole progress again (the host's world is the world); a guest playing its
   own game (a party game's pick) merges both ways again as a session's start does (its whole progress to the
   host, the host's back), so the two end the same. what: what differs, for the log */
#define FIGHT_SKIPPED_WAIT 3600 /* a fight left to the host (sFightSkipped): its fight begun within this */
#define DIGEST_APART_FRAMES 120 /* two differing digests "in a row": this far apart at least (they go every 300) */
static void coopDigest(int parts, const char* what) {
    static int sDiffer, sParts;
    static unsigned sAskedAt, sDifferAt;
    const int same = parts == 0;
    CoopMsg m;
    if (parts < 0 || G.watch || ssNetIsHost() || ssBattleBusy() || gp == NULL || gp->inBattle || modeHold()) {
        sDiffer = 0; /* (this game's own progress held by a mode: nothing to compare) */
        return;
    }
    if (portStateSyncCoopGuest() &&
        ((sStoryQuietUntil != 0 && G.frame < sStoryQuietUntil) || (ssPeerOf(1)->valid && ssPeerOf(1)->inBattle))) {
        /* (a co-op guest whose script went past a fight the host is still fighting, ssProgressQuiet: its story a
           step ahead until the host's win comes, not a drift. A guest back in the room after leaving the host's
           battle was "healed" back to before the fight mid-battle, mp_online_goons_leave3) */
        if (sFightSkipped == 1 && ssPeerOf(1)->valid && ssPeerOf(1)->inBattle) {
            sFightSkipped = 2;
        }
        sDiffer = 0;
        return;
    }
    if (portStateSyncCoopGuest() && sFightSkipped == 1 && G.frame - sFightSkippedAt < FIGHT_SKIPPED_WAIT) {
        sDiffer = 0; /* (the host's fight not begun yet: it comes to it, its players with it) */
        return;
    }
    if (portStateSyncCoopGuest() && sFightSkipped != 0) {
        /* (the host's fight over: this game's story after it was its script's, as if won, its flags never sent
           (ssProgressQuiet). The host's may be another: s51-001, the host lost the Iron Clefts match and the
           guest kept their win's flags and the ranking's, a DRIFT at the next digests. The host's whole progress,
           asked for now, as the outcome it is) */
        sFightSkipped = 0;
        sDiffer = 0;
        if (!same) {
            sAskedAt = G.frame;
            memset(&m, 0, sizeof(m));
            m.kind = C_RESYNC;
            ssLog("co-op: this game's script went past a fight the host fought, and its story differs from the host's "
                  "(%s): the host's whole progress asked for",
                  what);
            coopSend(1, &m);
        }
        return;
    }
    if (!same && sDiffer > 0 && G.frame - sDifferAt < DIGEST_APART_FRAMES) {
        /* (digests that waited while the progress was held (a battle, a scene: flushDeferred) come together
           once it's free: in a row means a digest's time apart, never two taken in the same moment, so a
           change still on its way isn't called a drift) */
        return;
    }
    if (!same && sDiffer > 0 && (parts & sParts) == 0) {
        sDiffer = 0; /* (another part than the last digest's: each a change on its way, the count starts again) */
    }
    sParts = same ? 0 : sDiffer > 0 ? sParts & parts : parts;
    sDiffer = same ? 0 : sDiffer + 1;
    sDifferAt = G.frame;
    if (sDiffer < 2 || (sAskedAt != 0 && G.frame - sAskedAt < 600)) {
        return;
    }
    sAskedAt = G.frame;
    sDiffer = 0;
    G.progressDrifts++;
    memset(&m, 0, sizeof(m));
    m.kind = C_RESYNC;
    if (portStateSyncCoopGuest()) {
        ssLog("DRIFT progress: %s differs from the host's at two digests in a row: the host's whole progress asked for "
              "again",
              what);
    } else {
        ssLog("DRIFT progress: %s differs from the host's at two digests in a row: merged again both ways (this "
              "game's whole progress to the host, the host's back)",
              what);
        sendSnapshot(1);
    }
    coopSend(1, &m);
}

/* ---- the guest: following the host's world ---- */

/* the host's helpers (the follower, the companion) where its game was, as interpolate() does */
static int helperPose(int p, int k, SsGhostPose* out) {
    const long long target = interpTarget(p);
    const PlayerMsg *a = NULL, *b = NULL;
    float t = 0.0f;
    int i;
    for (i = 0; i < G.pl[p].count; i++) {
        const PlayerMsg* s = &G.pl[p].s[(G.pl[p].head + SAMPLES - G.pl[p].count + i) % SAMPLES];
        if ((long long)s->ms <= target) {
            a = s;
        } else {
            b = s;
            break;
        }
    }
    if (a == NULL) {
        a = b;
        b = NULL;
    }
    if (a == NULL) {
        return 0;
    }
    if (b != NULL && b->ms != a->ms) {
        t = (float)(target - (long long)a->ms) / (float)(b->ms - a->ms);
    } else {
        b = a;
    }
    if (!a->hasHelper[k] || !b->hasHelper[k] || strcmp(a->hmodel[k], b->hmodel[k]) != 0) {
        return 0;
    }
    between(out, t, a->hx[k], a->hy[k], a->hz[k], a->hdir[k], b->hx[k], b->hy[k], b->hz[k], b->hdir[k], a->hmodel[k],
            a->hanim[k], b->hanim[k]);
    return 1;
}

static void hideParty(PartyEntry* party, int hidden) {
    if (party != NULL) {
        party->flags2 = hidden ? (party->flags2 | PARTY_DISP_FLAG_HIDDEN) : (party->flags2 & ~PARTY_DISP_FLAG_HIDDEN);
    }
}

/* this game's own party hidden (the host's is drawn): the partner, the follower, the companion, Luigi */
static void coopHideOwn(int hidden) {
    hideParty(partyGetPtr(marioGetExtraPartyId()), hidden);
    hideParty(partyGetPtr(PORT_PARTY_SLOT_COMPANION), hidden);
    hideParty(portBrothersFieldLuigi(), hidden);
}

/* the guest each frame: where the host's world is (the room, Mario's place), its controller to the host */
static void coopFollowFrame(void) {
    const Other* h = &G.pl[1];
    MarioWork* mario = marioGetPtr();
    char myArea[4];
    if (!portStateSyncCoopGuest()) {
        return;
    }
    ndGuestFrame(0); /* (the delay log: its controller on the wire, the host's word come) */
    if (G.seq != SEQ_GAME || seqCheckSeq() || gp->inBattle || mario == NULL) {
        return;
    }
    area(gp->currentMapName, myArea);
    if (C.areaPending && !memcmp(myArea, C.area, 4) && G.baseline) {
        memcpy(gp->lswf, C.lswf, sizeof(gp->lswf));
        memcpy(gp->lsw, C.lsw, sizeof(gp->lsw));
        memcpy(G.shadow.lswf, C.lswf, sizeof(G.shadow.lswf));
        memcpy(G.shadow.lsw, C.lsw, sizeof(G.shadow.lsw));
        C.areaPending = 0;
        ssLog("co-op: the host's flags for area %.3s taken here", myArea);
    }
    {
        /* this game's own Mario and party hidden while the host's are drawn here (the ghosts, statesync_pc.c);
           while they aren't (just come into the room before the host's word from it, the host's word late or
           its game busy a moment) this game's own stand in, so the screen never shows nobody. A short
           gap keeps them hidden (no flicker). Held in the host's scene too (below): this game's own Mario,
           shown while the host's word was late, stood beside the host's as a second Mario till it ended */
        const int unseen = !G.pl[1].ghostShown;
        C.hostUnseen = unseen ? C.hostUnseen + 1 : 0;
        if (C.hostUnseen == 30) {
            ssLog("co-op: the host's Mario not drawn here (%s): this game's own shown meanwhile",
                  !present(1) ? "no word from the host" : G.pl[1].info.map);
        }
        watchShow(C.hostUnseen < 30);
        coopHideOwn(C.hostUnseen < 30);
        if (G.frame % G.logEvery == 0) {
            ssLog("co-op: this game's own Mario %s (the host's %s here)%s",
                  (mario->dispFlags & MARIO_DISP_FLAG_HIDDEN) ? "hidden" : "shown",
                  G.pl[1].ghostShown ? "drawn" : "not drawn", ssBattleBusy() ? ", held in a scene" : "");
        }
    }
    if (ssBattleBusy()) {
        return; /* (held in the host's scene: statesync_battle_pc.c) */
    }
    if (marioChkKey()) {
        marioKeyOff();
    }
    if (C.noticeAt == 0) {
        C.noticeAt = G.frame;
        portCoopNotice(seatOf(ssNetMe()) == 1                       ? "Playing together: you're Mario."
                       : portBrothersOn() && seatOf(ssNetMe()) == 2 ? "Playing together: you're Luigi."
                       : seatOf(ssNetMe()) == 2                     ? "Playing together: you're the partner."
                                                                    : "Playing together: you're a companion.");
    }
    if (!present(1) || h->count == 0 || (h->info.seq != SEQ_GAME && h->info.seq != SEQ_BATTLE)) {
        return;
    }
    if (strcmp(h->info.map, gp->currentMapName) != 0) {
        /* (once, then again every few seconds while this game is settled in another room: an entry script that
           moved it on, a warp that didn't take) */
        const int again = !strcmp(G.watchWarp, h->info.map) && G.frame - G.watchWarpAt >= FOLLOW_RETRY_FRAMES;
        if ((strcmp(G.watchWarp, h->info.map) != 0 || again) && ssMapNameOk(h->info.map, sizeof(h->info.map)) &&
            !followHeld("co-op", h->info.map)) {
            snprintf(G.watchWarp, sizeof(G.watchWarp), "%s", h->info.map);
            G.watchWarpAt = G.frame;
            ssLog("co-op: the host went to %s: along%s", h->info.map, again ? " (again: this game is elsewhere)" : "");
            seqSetSeq(SEQ_MAP_CHANGE, G.watchWarp, NULL); /* (this game's copy: seqSetSeq keeps the pointer) */
        }
        return;
    }
    G.watchWarp[0] = '\0';
    if (C.reenter && h->info.seq == SEQ_GAME && seqGetSeq() == SEQ_GAME && !seqCheckSeq() &&
        !followHeld("co-op", gp->currentMapName)) {
        /* (takeSnapshot: the file became the host's kind; its area entered again, so its items are the host's) */
        static char sAgain[16];
        C.reenter = 0;
        snprintf(sAgain, sizeof(sAgain), "%s", gp->currentMapName);
        ssLog("co-op: %s entered again as the host's file has it", sAgain);
        seqSetSeq(SEQ_MAP_CHANGE, sAgain, NULL); /* (seqSetSeq keeps the pointer) */
        return;
    }
    screenNote("co-op");
    if (h->info.seq != SEQ_GAME) {
        return;
    }
    {
        /* Mario where the host's is drawn (the camera follows him there) */
        SsGhostPose me, partner, luigi;
        int hasPartner = 0, hasLuigi = 0;
        if (interpolate(1, &me, &partner, &hasPartner, &luigi, &hasLuigi) && isfinite(me.x) && isfinite(me.y) &&
            isfinite(me.z) && fabsf(me.x) < 1e6f && fabsf(me.y) < 1e6f && fabsf(me.z) < 1e6f) {
            /* (the host's numbers: only a place within any map) */
            mario->position.x = me.x;
            mario->position.y = me.y;
            mario->position.z = me.z;
        }
    }
}

/* a co-op guest: the character its seat plays, while the host's game walks it, is drawn as the host's word has it,
   interpolated with the host's Mario and the rest (once from the newest word alone: it stood still between the words
   and jumped at each, walking in place, and ran ahead of the Mario it follows); while this game walks it, its own */
static void coopOwnNewest(SsGhostPose* partner, int* hasPartner, SsGhostPose* luigi, int* hasLuigi) {
    (void)partner;
    (void)luigi;
    if (!portStateSyncCoopGuest() || seatOf(ssNetMe()) != 2) {
        return;
    }
    if (OG.drawn && OG.seat == 2) {
        /* (this game walks it: its own drawn, not the host's) */
        if (portBrothersOn()) {
            *hasLuigi = 0;
        } else {
            *hasPartner = 0;
        }
    }
}

/* the ghosts of the host's helpers (the follower, the companion), drawn as its partner's (which 3, 4) */
static void coopHelperGhosts(int p, int slot, int shown) {
    int k;
    for (k = 0; k < 2; k++) {
        SsGhostPose pose;
        int got;
        const int own = p == 1 && portStateSyncCoopGuest() && seatOf(ssNetMe()) == 3 + k;
        got = shown && !(own && OG.drawn && OG.seat == 3 + k) && helperPose(p, k, &pose); /* (this game's own) */
        if (got) {
            /* (its Paper Mario 64 look, the host's word: drawn as the host's game and this game's own walk of it
               draw it, never Goombella held and Goombario walked) */
            int look = 0;
            pose.pm64 = portStateSyncSeatCharacter(3 + k, &look) > 0 && look > 0 ? look : 0;
            ssGhostDraw(slot, 3 + k, &pose);
        } else {
            ssGhostHide(slot, 3 + k);
        }
    }
}

/* the helpers this game sends (the host's: the follower and the companion; anyone's, it costs little) */
static void coopFillHelpers(PlayerMsg* m) {
    PartyEntry* party[2];
    int k;
    party[0] = G.seq == SEQ_GAME ? partyGetPtr(marioGetExtraPartyId()) : NULL;
    party[1] = G.seq == SEQ_GAME ? partyGetPtr(PORT_PARTY_SLOT_COMPANION) : NULL;
    for (k = 0; k < 2; k++) {
        if (party[k] != NULL && party[k]->paperAnimGroup >= 0 && !(party[k]->flags2 & PARTY_DISP_FLAG_HIDDEN)) {
            m->hasHelper[k] = 1;
            m->hx[k] = party[k]->position.x;
            m->hy[k] = party[k]->position.y;
            m->hz[k] = party[k]->position.z;
            m->hdir[k] = party[k]->displayDirection;
            snprintf(m->hmodel[k], sizeof(m->hmodel[k]), "%s", poseModel(party[k]->paperAnimGroup));
            snprintf(m->hanim[k], sizeof(m->hanim[k]), "%s", party[k]->animName != NULL ? party[k]->animName : "");
        }
    }
}

/* ---- the delay log ($TTYD_NET_DELAY_LOG=1, tests; tools/port/net_delay.py reads it) ----
 * A guest's press (its stick out of the middle, or A) followed to its own character moving on its screen,
 * each step's time on the computer's clock (every game on one computer reads the same one):
 *   guest "press" (the controller read) -> guest "out" (on the wire) -> host "arrived" -> host "applied"
 *   (played as the seat's controller; queue depth) -> host "state out" (its character moved, sent) ->
 *   guest "state in" (the newest word from the host shows it moved) -> guest "seen" (drawn moved).
 * Matched by the guest's controller frame number. */
static int ndOn(void) {
    static int sOn = -1;
    if (sOn < 0) {
        const char* v = getenv("TTYD_NET_DELAY_LOG");
        sOn = v != NULL && *v != '\0' && *v != '0';
    }
    return sOn;
}

/* a controller frame that is a press: the stick out of the middle, or A, from a frame without */
static int ndPress(const PadRec* now, const PadRec* before) {
    const int was = before->sx * before->sx + before->sy * before->sy, is = now->sx * now->sx + now->sy * now->sy;
    return (is > 30 * 30 && was < 10 * 10) || ((now->button & PAD_BUTTON_A) && !(before->button & PAD_BUTTON_A));
}

/* seat s's character in a word from the host (seat 2 the partner, or Luigi; 3 the follower; 4 the companion) */
static int ndSamplePos(const PlayerMsg* m, int s, float out[3]) {
    if (s == 2 && m->hasLuigi && portBrothersOn()) {
        out[0] = m->lx, out[1] = m->ly, out[2] = m->lz;
        return 1;
    }
    if (s == 2 && m->hasPartner) {
        out[0] = m->px, out[1] = m->py, out[2] = m->pz;
        return 1;
    }
    if ((s == 3 || s == 4) && m->hasHelper[s - 3]) {
        out[0] = m->hx[s - 3], out[1] = m->hy[s - 3], out[2] = m->hz[s - 3];
        return 1;
    }
    return 0;
}

/* seat s's character in this (the host's) game */
static PartyEntry* seatParty(int s) {
    if (s == 2) {
        /* (Luigi drawn or not: a co-op guest's own is hidden while the host holds him) */
        PartyEntry* luigi = portBrothersOn() && portBrothersInField() ? portBrothersLuigiAny() : NULL;
        return luigi != NULL ? luigi : partyGetPtr(marioGetPartyId());
    }
    if (s == 3) {
        return partyGetPtr(marioGetExtraPartyId());
    }
    return s == 4 ? partyGetPtr(PORT_PARTY_SLOT_COMPANION) : NULL;
}

static float ndDist(const float a[3], const float b[3]) {
    return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

#define ND_MOVED 0.25f

/* the guest's own character as drawn here now, and as the newest word from the host has it */
static int ndDrawnPos(float out[3]);

static int ndNewestPos(int s, float out[3]) {
    const Other* h = &G.pl[1];
    if (h->count == 0) {
        return 0;
    }
    return ndSamplePos(&h->s[(h->head + SAMPLES - 1) % SAMPLES], s, out);
}

static int ndDrawnPos(float out[3]) {
    SsGhostPose me, partner, luigi, pose;
    int hasPartner = 0, hasLuigi = 0;
    const int s = seatOf(ssNetMe());
    const SsGhostPose* at = NULL;
    if (OG.drawn && OG.seat == s && s >= 2 && seatParty(s) != NULL) {
        /* (this game walks it: where it is here) */
        const PartyEntry* party = seatParty(s);
        out[0] = party->position.x, out[1] = party->position.y, out[2] = party->position.z;
        return 1;
    }
    if (s == 3 || s == 4) {
        int got;
        got = helperPose(1, s - 3, &pose);
        if (got) {
            at = &pose;
        }
    } else if (s == 2 && interpolate(1, &me, &partner, &hasPartner, &luigi, &hasLuigi)) {
        coopOwnNewest(&partner, &hasPartner, &luigi, &hasLuigi); /* (as drawn) */
        at = hasLuigi && portBrothersOn() ? &luigi : hasPartner ? &partner : NULL;
    }
    if (at == NULL) {
        return 0;
    }
    out[0] = at->x, out[1] = at->y, out[2] = at->z;
    return 1;
}

/* the guest, as its controller is read (rec: this frame's, its number C.padFrame) */
static void ndGuestRead(void) {
    if (!ndOn() || ND.on || seatOf(ssNetMe()) < 2 || !ndPress(&C.hist[0], &C.hist[1])) {
        return;
    }
    memset(&ND.baseIn, 0, sizeof(ND.baseIn));
    ND.haveIn = ndNewestPos(seatOf(ssNetMe()), ND.baseIn);
    ND.haveDrawn = ndDrawnPos(ND.baseDrawn);
    if (!ND.haveIn || !ND.haveDrawn) {
        return;
    }
    ND.on = 1;
    ND.frame = C.padFrame + 1; /* (coopApplyPad counts it next) */
    ND.readMs = ssNetMs();
    ND.queuedMs = ND.outMs = ND.inMs = 0;
    ND.startFrame = G.frame + 1; /* (read before the frame's count goes up: this frame's number) */
    ssLog("netdelay: guest press pad %u read %llu", ND.frame, ND.readMs);
}

/* the guest, each frame after its controller went (queued) and after the ghosts were drawn */
static void ndGuestFrame(int afterDraw) {
    float p[3];
    const int s = seatOf(ssNetMe());
    if (!ND.on) {
        return;
    }
    if ((int)(G.frame - ND.startFrame) > 600 || s < 2) {
        ssLog("netdelay: guest press pad %u: never seen moving (given up)", ND.frame);
        ND.on = 0;
        return;
    }
    if (ND.queuedMs != 0 && ND.outMs == 0 && ssNetOutMs() >= ND.queuedMs) {
        ND.outMs = ssNetOutMs();
        ssLog("netdelay: guest pad %u out %llu", ND.frame, ND.outMs);
    }
    if (ND.inMs == 0 && ndNewestPos(s, p) && ndDist(p, ND.baseIn) > ND_MOVED) {
        ND.inMs = ssNetMs();
        ssLog("netdelay: guest pad %u state in %llu", ND.frame, ND.inMs);
    }
    if (afterDraw && ndDrawnPos(p) && ndDist(p, ND.baseDrawn) > ND_MOVED) {
        const unsigned long long now = ssNetMs();
        ssLog("netdelay: guest pad %u seen %llu: %llu ms, %u frames (read->out %lld, out->in %lld, in->seen %lld; "
              "rtt %d)",
              ND.frame, now, now - ND.readMs, G.frame - ND.startFrame,
              ND.outMs ? (long long)(ND.outMs - ND.readMs) : -1LL, ND.outMs && ND.inMs ? (long long)(ND.inMs - ND.outMs) : -1LL,
              ND.inMs ? (long long)(now - ND.inMs) : -1LL, ssNetRttMs(1));
        ND.on = 0;
    }
}

/* the host, each guest controller frame as it arrives (in order; f its number) */
static void ndHostArrived(int from, uint32_t f, const PadRec* r) {
    if (!ndOn()) {
        return;
    }
    if (!ND.h[from].on && ndPress(r, &ND.h[from].prev)) {
        ND.h[from].on = 1;
        ND.h[from].applied = 0;
        ND.h[from].frame = f;
        ND.h[from].arrivedMs = ssNetMs();
        ssLog("netdelay: host pad %u from player %d arrived %llu", f, from, ND.h[from].arrivedMs);
    }
    ND.h[from].prev = *r;
}

/* the host, a guest's frame f played now (queued: frames still waiting) */
static void ndHostApplied(int p, int seat, uint32_t f, int queued) {
    PartyEntry* party;
    if (!ndOn() || !ND.h[p].on || ND.h[p].applied || (int32_t)(f - ND.h[p].frame) < 0) {
        return;
    }
    ND.h[p].applied = 1;
    party = seatParty(seat);
    ND.h[p].haveBase = party != NULL;
    if (party != NULL) {
        ND.h[p].base[0] = party->position.x, ND.h[p].base[1] = party->position.y, ND.h[p].base[2] = party->position.z;
    }
    ssLog("netdelay: host pad %u from player %d applied %llu as frame %u (queued %d)", ND.h[p].frame, p, ssNetMs(), f,
          queued);
}

/* the host, sending its word: each followed press whose character moved since it was played */
static void ndHostSent(void) {
    int p;
    for (p = 2; ndOn() && p <= SS_PLAYERS; p++) {
        PartyEntry* party;
        float at[3];
        if (!ND.h[p].on || !ND.h[p].applied) {
            continue;
        }
        party = seatParty(seatOf(p));
        if (party == NULL || !ND.h[p].haveBase) {
            ND.h[p].on = 0;
            continue;
        }
        at[0] = party->position.x, at[1] = party->position.y, at[2] = party->position.z;
        if (ndDist(at, ND.h[p].base) > ND_MOVED) {
            ssLog("netdelay: host pad %u from player %d state out %llu", ND.h[p].frame, p, ssNetMs());
            ND.h[p].on = 0;
        }
    }
}

/* ---- each guest's own character (docs/STATE_SYNC.md "The guest's own character") ----
 * As sm64coopdx: each player's game walks its own character. A co-op guest in seat 2, 3 or 4 walks the
 * character its seat plays (the partner, Brothers mode's Luigi, the follower or the companion) in its own
 * game, from its own controller (second_player_pc.c's walk and brothers_pc.c's, as a second controller on one
 * computer walks it: walls, floors, falls and hops the game's own), shown on its screen at once. Each frame
 * its game sends the host where it is (SS_MSG_OWN), and the host's game puts that seat's character there
 * instead of walking it from the guest's controller. The world stays the host's: its enemies touch the
 * character where it is put, and its talks, doors, scenes, items and battles are its own; B, X, L and R
 * still go to the host as the controller (a talk, the partner's move, a swap), and Luigi's hammer.
 *
 * The host holds a seat (its game moves the character, the guest's follows the host's word) whenever its
 * field isn't free (a talk, a scene, a menu, a door's or pipe's walk, a battle), the partner's field move or
 * Luigi's swing runs, or the guest's word is late. Each hold starts a new epoch; a guest's word counts only
 * with the current one, so a word sent before the guest saw the hold can't put the character back. When a
 * hold ends, the guest's character starts where the host's word has it (the host's place wins), taken or
 * following as the host's is. A step past what a walk can do is cut to it; a jump far from where the host
 * has it and from Mario is refused and held. */

typedef struct OwnMsg {
    uint32_t ms;
    uint32_t epoch; /* the host's epoch for this seat, as this guest last saw it */
    uint8_t seat;
    uint8_t owned; /* its player has taken it (else it follows Mario: the host's game walks it) */
    int16_t face;  /* the way its paper faces there (its display direction in tenths + OWN_FACE), 0 not said */
    int32_t member; /* the character (its party member), to be sure it is the one the host has */
    float x, y, z, dir;
    char anim[32];
    uint32_t hitSeq; /* its Luigi's head hits so far (a new one: hit names the block, worked in the host's game) */
    char hit[28];
} OwnMsg;

WIRE_SIZE(OwnMsg, 96);

#define OWN_STALE_MS 300
#define OWN_FACE 10000 /* (OwnMsg.face's bias: a display direction of -100 to 360 degrees, never 0) */
#define OWN_HIST 16    /* the host's: each seat's last reports, its character played back smoothly from them */
#define OWN_BEHIND_MAX 250 /* the most the host plays a seat's character back behind its reports (ms) */
#define OWN_SAME_MS 100 /* a room's: an unchanged character sent again this often (coopOwnSend) */
#define OWN_STEP 14.0f     /* a frame's walk at most (a boosted run is under 8) */
#define OWN_TELEPORT 160.0f /* a jump this far, away from Mario, is refused */

static struct {
    /* the host, by seat */
    OwnMsg rep;
    unsigned long long at, placedAt;
    int any, held, placed, logged;
    uint32_t epoch;
    uint32_t hitSeq; /* the guest's head hits taken */
    int from;        /* the player whose reports rep is (any) */
    int who;         /* the seat's holder as last seen (holder | 0x100 a second controller's): ownWhoFrame */
    /* its reports lately, oldest first (ownAt plays them back), and how late they come by its clock: the most of
       it lately (each report's lateness; the largest held, let go a little at each report) */
    OwnMsg hist[OWN_HIST];
    int histN;
    float lateMax;
    int lateKnown;
} O[5];

/* a backward step this big in a report's clock is another clock (its game began again, or another game in the
   seat), not a reordered report */
#define OWN_CLOCK_JUMP_MS 5000

/* the host: seat s's reports forgotten (a new holder, a new session, a clock begun again): the next report, on
   whatever clock its game has, is taken. Each report's ms is its sender's own clock, so one kept from an earlier
   holder (or game) refused a new one's as "older" for as long as its clock was behind: the seat stayed held and
   its guest's character never walked. A new epoch: the guest takes the host's place first */
static void ownReset(int s, const char* why) {
    uint32_t epoch;
    int who;
    if (s < 2 || s > 4) {
        return;
    }
    epoch = O[s].epoch + 1;
    who = O[s].who;
    memset(&O[s], 0, sizeof(O[s]));
    O[s].epoch = epoch;
    O[s].who = who;
    O[s].held = 1;
    ssLog("co-op: seat %d's own-character reports forgotten (%s): epoch %u", s, why, epoch);
}

/* the host each frame: a seat whose holder changed (a swap, a guest gone, another come, a second controller) starts
   its reports over */
static void ownWhoFrame(void) {
    int s;
    for (s = 2; s <= 4; s++) {
        const int who = holder(s) | (seatSub(s) ? 0x100 : 0);
        if (who != O[s].who) {
            O[s].who = who;
            ownReset(s, "its holder changed");
        }
    }
}

/* the guest: its own Luigi's last head hit (every word carries it: a lost one costs nothing) */
static struct {
    uint32_t seq;
    char name[28];
} sOwnHit;

void portStateSyncCoopLuigiHit(const char* mobjName) {
    if (!portStateSyncCoopGuest() || mobjName == NULL) {
        return;
    }
    sOwnHit.seq++;
    snprintf(sOwnHit.name, sizeof(sOwnHit.name), "%s", mobjName);
    ssLog("co-op: this guest's own Luigi hit '%s' from below: to the host", sOwnHit.name);
}

static PartyEntry* seatParty(int s);

/* the partner's field move under way (Yoshi's ride, Vivian's shadow...), or Luigi's own: the host's */
static int seatBusy(int s) {
    if (s == 2 && portBrothersOn()) {
        return portBrothersBusy();
    }
    if (s == 2) {
        const PartyEntry* partner = partyGetPtr(marioGetPartyId());
        return yoshiGetStatus() != 0 ||
               (partner != NULL && ((partner->flags & PARTY_FLAG_IS_BEING_USED) || partner->runMode == 3));
    }
    return 0;
}

/* the seat's character taken by its player (else following Mario) */
static int seatFree(int s) {
    return s == 2 && portBrothersOn() ? portBrothersDrivenNow() : portCoopHelperFree(s - 2);
}

static void seatFreeSet(int s, int free) {
    if (s == 2 && portBrothersOn()) {
        portBrothersDrivenSet(free);
    } else {
        portCoopHelperSet(s - 2, free);
    }
}

/* the host's field is free for its guests' characters */
static int hostFieldFree(void) {
    return gp != NULL && G.seq == SEQ_GAME && !seqCheckSeq() && !gp->inBattle && !ssBattleBusy() &&
           marioStGetSystemLevel() == 0 && marioChkKey() && marioChkCtrl() && sMarioMenuHold == 0 && !winCheck();
}

static void gotOwn(int from, const uint8_t* p, size_t n) {
    OwnMsg m;
    int s;
    if (!ssNetIsHost() || from < 2 || from > SS_PLAYERS || n < sizeof(m)) {
        return;
    }
    memcpy(&m, p, sizeof(m));
    s = m.seat;
    if (s < 2 || s > 4 || holder(s) != from || seatSub(s) || !isfinite(m.x) || !isfinite(m.y) || !isfinite(m.z) ||
        !isfinite(m.dir) || fabsf(m.x) > 1e5f || fabsf(m.y) > 1e5f || fabsf(m.z) > 1e5f) {
        return; /* (not that player's seat, a second controller's (the host's game walks it), or numbers no map has) */
    }
    if (O[s].any && O[s].from != from) {
        ownReset(s, "another player's reports"); /* (a seat changed hands before ownWhoFrame saw it) */
    }
    if (O[s].any && (int32_t)(m.ms - O[s].rep.ms) <= 0) {
        if ((int32_t)(m.ms - O[s].rep.ms) > -OWN_CLOCK_JUMP_MS) {
            return; /* (an older one) */
        }
        ownReset(s, "its game's clock began again");
    }
    O[s].from = from;
    if (m.face != 0 && (m.face < OWN_FACE - 1000 || m.face > OWN_FACE + 3600)) {
        m.face = 0; /* (no display direction a game has: not said) */
    }
    m.anim[sizeof(m.anim) - 1] = '\0';
    m.hit[sizeof(m.hit) - 1] = '\0';
    if (O[s].any && m.hitSeq != O[s].hitSeq && m.hit[0] != '\0' && s == 2 && portBrothersOn() &&
        portStateSyncCoopHost() && hostFieldFree()) {
        ssLog("co-op: player %d's own Luigi hit '%s' from below: worked here", from, m.hit);
        portBrothersHitByName(m.hit);
    }
    O[s].hitSeq = m.hitSeq;
    O[s].rep = m;
    O[s].at = ssNetMs();
    O[s].any = 1;
    if (O[s].histN == OWN_HIST) {
        memmove(&O[s].hist[0], &O[s].hist[1], sizeof(O[s].hist[0]) * (OWN_HIST - 1));
        O[s].histN--;
    }
    O[s].hist[O[s].histN++] = m;
    {
        /* (its lateness by its own clock, as its places' are measured: the batch it rode, the link's wait; the most
           of it lately is how far behind the host plays it back, so the next report is there in time) */
        const float late = (float)((long long)(uint32_t)ssNetMs() - G.pl[from].offset - (long long)m.ms);
        if (!O[s].lateKnown || late > O[s].lateMax) {
            O[s].lateMax = late;
            O[s].lateKnown = 1;
        } else {
            O[s].lateMax -= 0.5f; /* (a quieter link: less behind again, some 30 ms a second) */
        }
    }
}

/* the host: seat s's character as its guest's game had it a moment ago, its reports played back as they were made
   (placed at the newest report alone, it stood still between the reports and jumped at each, walking in place) */
static OwnMsg ownAt(int s) {
    const float behindF = (O[s].lateMax > 0.0f ? O[s].lateMax : 0.0f) + 8.0f;
    const long long behind = (long long)(behindF < (float)OWN_BEHIND_MAX ? behindF : (float)OWN_BEHIND_MAX);
    const long long target = (long long)(uint32_t)ssNetMs() - G.pl[O[s].from].offset - behind;
    const OwnMsg *a = NULL, *b = NULL;
    OwnMsg out;
    float t;
    int i;
    if (O[s].histN == 0) {
        return O[s].rep;
    }
    for (i = 0; i < O[s].histN; i++) {
        if (O[s].hist[i].epoch != O[s].rep.epoch || O[s].hist[i].owned != O[s].rep.owned) {
            continue; /* (from before a hold of the host's, or before its player took it: not glided from) */
        }
        if ((long long)O[s].hist[i].ms <= target) {
            a = &O[s].hist[i];
        } else {
            b = &O[s].hist[i];
            break;
        }
    }
    if (a == NULL) {
        return b != NULL ? *b : O[s].rep; /* (every one newer: the oldest) */
    }
    if (b == NULL || b->ms == a->ms) {
        return *a; /* (none newer yet: the newest, held) */
    }
    t = (float)(target - (long long)a->ms) / (float)(b->ms - a->ms);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    out = t < 0.5f ? *a : *b;
    out.x = a->x + (b->x - a->x) * t;
    out.y = a->y + (b->y - a->y) * t;
    out.z = a->z + (b->z - a->z) * t;
    out.dir = lerpAngle(a->dir, b->dir, t);
    if (a->face != 0 && b->face != 0) {
        out.face = (int16_t)lroundf((float)a->face + (float)(b->face - a->face) * t);
    }
    return out;
}

void portStateSyncCoopSeatMoved(int seat, const char* why) {
    if (seat < 2 || seat > 4 || !portStateSyncCoopHost() || holder(seat) < 2) {
        return;
    }
    O[seat].epoch++;
    O[seat].placed = 0;
    ssLog("co-op: seat %d's character moved by the host (%s): epoch %u, its guest takes the host's place", seat, why,
          O[seat].epoch);
}

/* $TTYD_SYNC_ENEMY_TO_SEAT=<frame>:<seat>:<dx>:<dz> (tests): then the host's nearest field enemy is put that far
   from that seat's character (on its floor), so a test can walk a guest's own character into it whatever the
   map's layout; seat 1: from Mario (the host's own first strike: its hammer at it). ":still" after it: the enemy
   notices nobody from then on (no find events: its chase or lunge never starts), so the test's own strike is
   the first. A Pale Piranha put by a player sank (untouchable) and bit first whenever a character came near */
static void enemyToSeatTest(void) {
    static int sAt = -1, sSeat, sStill;
    static float sDx = 70.0f, sDz;
    NpcWork* wp = npcGetWorkPtr();
    NpcEntry* best = NULL;
    PartyEntry* party;
    const Vec* at;
    float bd = 1e30f;
    int i;
    if (sAt < 0) {
        const char* v = getenv("TTYD_SYNC_ENEMY_TO_SEAT");
        sAt = v != NULL ? (int)strtol(v, NULL, 10) : 0;
        sSeat = 2;
        if (v != NULL) {
            sscanf(v, "%*d:%d:%f:%f", &sSeat, &sDx, &sDz);
            sStill = strstr(v, ":still") != NULL;
        }
    }
    if (sAt <= 0 || G.frame < (unsigned)sAt || wp == NULL || !hostFieldFree()) {
        return;
    }
    party = sSeat == 1 ? NULL : seatParty(sSeat);
    at = sSeat == 1 ? (marioGetPtr() != NULL ? &marioGetPtr()->position : NULL) : party != NULL ? &party->position : NULL;
    if (at == NULL) {
        return;
    }
    for (i = 0; i < wp->total; i++) {
        NpcEntry* npc = &wp->entries[i];
        const float d = hypotf(npc->position.x - at->x, npc->position.z - at->z);
        /* (one standing in the room: not a hidden, untouchable spare, e.g. the Pale Piranhas' "_s0" copies
           kept under the map) */
        if ((npc->flags & (NPC_FLAG_USED | NPC_FLAG_ACTIVE)) == (NPC_FLAG_USED | NPC_FLAG_ACTIVE) &&
            !(npc->flags & (NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_DISPLAY)) && npc->position.y > -500.0f &&
            npc->battleInfo.groupSetup != NULL && npc->deadEvent != NULL && d < bd) {
            bd = d, best = npc;
        }
    }
    if (best != NULL) {
        sAt = 0;
        best->position.x = at->x + sDx;
        best->position.y = at->y;
        best->position.z = at->z + sDz;
        if (sStill) {
            best->flags |= NPC_FLAG_NO_FIND_EVENTS;
        }
        ssLog("test: the enemy '%s' put by seat %d's character, at (%.0f, %.0f, %.0f)%s", best->description, sSeat,
              best->position.x, best->position.y, best->position.z, sStill ? ", noticing nobody" : "");
    }
}

/* the host each frame: which seats' characters its guests walk now */
static void coopOwnHostFrame(void) {
    int s;
    const int field = portStateSyncCoopHost() && hostFieldFree();
    if (field) {
        enemyToSeatTest();
    }
    ownWhoFrame();
    for (s = 2; s <= 4; s++) {
        const int p = holder(s);
        PartyEntry* party = seatParty(s);
        int held;
        if (p < 2 || !coopPlayer(p) || seatSub(s)) {
            /* (nobody's, the host's own, or a guest's second controller's: the host's game walks it from that
               controller, never from a report) */
            O[s].held = 1;
            O[s].placed = 0;
            continue;
        }
        held = !field || party == NULL || seatBusy(s) || !O[s].any || ssNetMs() - O[s].at > OWN_STALE_MS ||
               !present(p) || strcmp(G.pl[p].info.map, gp->currentMapName) != 0;
        if (held && !O[s].held) {
            O[s].epoch++;
            ssLog("co-op: seat %d's character held by the host (%s): epoch %u", s,
                  !field ? "its field isn't free" : party == NULL ? "no character"
                  : seatBusy(s) ? "its move under way" : "the guest's word late", O[s].epoch);
        } else if (!held && O[s].held) {
            ssLog("co-op: seat %d's character free for its guest's game (epoch %u)", s, O[s].epoch);
        }
        O[s].held = held;
        O[s].placed = !held && O[s].rep.owned && O[s].rep.epoch == O[s].epoch &&
                      (party != NULL && O[s].rep.member == party->currentMemberId);
        if (O[s].placed && !O[s].logged) {
            O[s].logged = 1;
            ssLog("co-op: seat %d's character walked by player %d's own game", s, p);
        } else if (!O[s].placed) {
            O[s].logged = 0;
        }
        {
            /* (free for its guest's game, no report of it placed yet: nothing walks it here meanwhile, and one left
               in the air (a battle's end, its entry's hop) stayed mid-jump for good, the room never settled: mp
               l51-008, l51-032: a companion's entry hop begun and never played, fixed in second_player_pc.c's
               companionEntry). A last resort: a second in the air so, its height standing still, landed. One really
               falling (its height changing: off a ledge, into the void under a map) is the game's own fall, which
               lands it or, below -2000, puts it back by Mario (party_motion.c); taken here mid-fall it was put
               back over the gap it fell through: online_companion_entry3 under load) */
            static unsigned sAir[5];
            static float sAirY[5];
            const int limbo = !held && !O[s].placed && field && party != NULL &&
                              (party->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING)) &&
                              fabsf(party->position.y - sAirY[s]) < 0.5f;
            sAirY[s] = party != NULL ? party->position.y : 0.0f;
            sAir[s] = limbo ? sAir[s] + 1 : 0;
            if (sAir[s] > 60 && portCoopHelperLand(party)) {
                sAir[s] = 0;
                ssLog("co-op: seat %d's character was in the air with nothing to land it: on the floor", s);
            }
        }
    }
}

int portStateSyncCoopPlace(int seat, PartyEntry* party) {
    float dx, dy, dz, d;
    const MarioWork* mario = marioGetPtr();
    const unsigned long long now = ssNetMs();
    OwnMsg at;
    const OwnMsg* r;
    if (seat < 2 || seat > 4 || !O[seat].placed || party == NULL || party != seatParty(seat) || mario == NULL ||
        !portStateSyncCoopHost()) {
        return 0;
    }
    at = ownAt(seat);
    r = &at;
    dx = r->x - party->position.x, dy = r->y - party->position.y, dz = r->z - party->position.z;
    d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d > OWN_TELEPORT && hypotf(r->x - mario->position.x, r->z - mario->position.z) > 80.0f) {
        ssLog("co-op: seat %d's character: a jump of %.0f refused, held where it is", seat, d);
        portStateSyncCoopSeatMoved(seat, "a jump refused");
        return 0;
    }
    {
        /* (frames since the last place, by the clock: a late word walks as far as its frames could) */
        const float frames = O[seat].placedAt != 0 && now > O[seat].placedAt
                                 ? fminf((float)(now - O[seat].placedAt) / 16.0f + 1.0f, 10.0f)
                                 : 1.0f;
        const float most = OWN_STEP * frames;
        if (d > most && d <= OWN_TELEPORT) {
            const float k = most / d;
            dx *= k, dy *= k, dz *= k;
        }
    }
    O[seat].placedAt = now;
    party->position.x += dx;
    party->position.y += dy;
    party->position.z += dz;
    party->targetDir = party->moveDir = r->dir;
    party->moveSpeed = 0.0f;
    party->motionId = 0;
    party->velY = 0.0f;
    party->accelY = 0.0f;
    party->flags &= ~(PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING);
    if (r->face != 0) {
        /* the way its paper faces there, turning as it turns there (put here standing, partyMakeDispDir never
           turned it: it faced whichever way it last walked in this game, seen backwards) */
        const float face = (float)(r->face - OWN_FACE) / 10.0f;
        const float side = fmodf(face + 360.0f, 360.0f);
        party->displayDirection = face;
        party->displayDirectionTarget = side >= 90.0f && side <= 270.0f ? 180.0f : 0.0f;
    }
    if (r->anim[0] != '\0' && (party->animName == NULL || strcmp(party->animName, r->anim) != 0)) {
        partyChgPose(party, r->anim);
    }
    return 1;
}

/* the host's word: each seat's epoch, hold and taken */
static void coopOwnFill(PlayerMsg* m) {
    int s;
    if (!portStateSyncCoopHost()) {
        return;
    }
    for (s = 2; s <= 4; s++) {
        m->seatEpoch[s - 2] = O[s].epoch;
        m->seatHeld[s - 2] = (uint8_t)(O[s].held != 0);
        m->seatFree[s - 2] = (uint8_t)(seatParty(s) != NULL && seatFree(s));
    }
}

int portStateSyncCoopLocalSeat(void) {
    return portStateSyncCoopGuest() ? OG.seat : 0;
}

/* the guest: its seat's character where the host's word has it */
static void ownSnap(int s, const PlayerMsg* w) {
    PartyEntry* party = seatParty(s);
    float at[3];
    if (party == NULL || !ndSamplePos(w, s, at)) {
        return;
    }
    party->position.x = at[0];
    party->position.y = at[1];
    party->position.z = at[2];
    party->flags &= ~(PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING);
    party->velY = party->accelY = 0.0f;
    party->moveSpeed = 0.0f;
}

/* the guest each frame: whether it walks its own character now; else it follows the host's word */
static void coopOwnGuestFrame(void) {
    const Other* h = &G.pl[1];
    const int s = seatOf(ssNetMe()), was = OG.seat;
    const PlayerMsg* w;
    PartyEntry* party;
    int ok;
    OG.seat = 0;
    OG.drawn = 0;
    if (!portStateSyncCoopGuest() || s < 2 || s > 4 || gp == NULL || G.seq != SEQ_GAME || seqCheckSeq() ||
        gp->inBattle || ssBattleBusy() || !present(1) || h->count == 0 || h->info.seq != SEQ_GAME ||
        strcmp(h->info.map, gp->currentMapName) != 0) {
        if (was) {
            ssLog("co-op: this guest's own character: the host's again (not in the field together)");
        }
        OG.known = 0;
        return;
    }
    w = &h->s[(h->head + SAMPLES - 1) % SAMPLES];
    party = seatParty(s);
    if (FA.swapUntil != 0 && (ssNetMs() > FA.swapUntil || (party != NULL && s == 2 && w->partner == FA.swapTo) ||
                              (party != NULL && s >= 3 && C.chr[s] > 0 &&
                               C.chr[s] - PARTY_MEMBER_GOOMBELLA + PARTY_MEMBER_GOOMBELLA_FOLLOWER == FA.swapTo))) {
        FA.swapUntil = 0; /* (the host's game has this game's swap now, or it never came: the host's again) */
    }
    if (FA.swapUntil != 0) {
        /* (this game's own swap on its way: its character stays the one it swapped to, not put back) */
    } else if (s == 2 && !portBrothersOn() && w->hasPartner && (party == NULL || party->currentMemberId != w->partner) &&
               w->partner > 0 && w->partner <= 7) {
        /* (not the partner the host has out, or none at all: that one, here too. A guest that came from the
           title has a new file's state with no partner out, one from its own field had its own: both walk the
           host's partner in their own game, as the seat says) */
        party = NULL;
        if (G.frame - OG.swapAt > 60) {
            OG.swapAt = G.frame;
            portPartySwapPartner(w->partner); /* (the follower kept: Punio, an escort) */
            ssLog("co-op: this guest's partner is the host's (%d) again", w->partner);
        }
    }
    if (s >= 3 && FA.swapUntil == 0 && !portBrothersOn() && w->hasPartner && w->partner > 0 && w->partner <= 7) {
        /* (players 3 and 4: the host's partner out here too, as player 2's game has it. Before, only seat 2's game
           put it back: a player 3 whose link came back after a while (nm51s l51-016, half-open, resumed) was
           left with no partner out in the host's world, its game unlike the others') */
        const PartyEntry* partner = partyGetPtr(marioGetPartyId());
        if ((partner == NULL || partner->currentMemberId != w->partner) && G.frame - OG.partnerAt > 60) {
            OG.partnerAt = G.frame;
            portPartySwapPartner(w->partner);
            ssLog("co-op: the host's partner (%d) out here again", w->partner);
        }
    }
    if (s >= 3 && C.chrKnown && C.chr[s] > 0 && FA.swapUntil == 0) {
        /* (players 3 and 4: the follower or companion the host's game has for this seat, here too) */
        const int c = C.chr[s];
        const int form = c >= PARTY_MEMBER_GOOMBELLA && c <= PARTY_MEMBER_MS_MOWZ
                             ? c - PARTY_MEMBER_GOOMBELLA + PARTY_MEMBER_GOOMBELLA_FOLLOWER
                             : c;
        if ((party == NULL || party->currentMemberId != form) && G.frame - OG.swapAt > 60) {
            OG.swapAt = G.frame;
            portCoopGuestHelperAs(s - 2, form);
            party = seatParty(s);
            ssLog("co-op: this guest's companion is the host's (%d) here", form);
        }
        if (party != NULL && party->currentMemberId != form) {
            party = NULL;
        }
    }
    if (party == NULL) {
        if (was) {
            ssLog("co-op: this guest's own character: none here (the host's drawn)");
        }
        return;
    }
    if (!OG.known || w->seatEpoch[s - 2] != OG.epoch) {
        /* a hold of the host's began (or this guest just came): the host's place and taken, here */
        OG.known = 1;
        OG.epoch = w->seatEpoch[s - 2];
        ownSnap(s, w);
        seatFreeSet(s, w->seatFree[s - 2]);
        ssLog("co-op: this guest's own character at the host's place (epoch %u, %s)", OG.epoch,
              w->seatFree[s - 2] ? "taken" : "following");
    }
    ok = !w->seatHeld[s - 2] && !h->info.keysOff && !h->info.paused && !winCheck() && C.hostUnseen < 30;
    if (!ok) {
        ownSnap(s, w); /* (the host holds it: here where the host's word has it, for when it's this game's again) */
        if (was) {
            ssLog("co-op: this guest's own character: held by the host");
        }
        return;
    }
    OG.seat = s;
    if (!was) {
        ssLog("co-op: this guest walks its own character (seat %d)", s);
    }
    if (!seatFree(s)) {
        /* (not taken: it follows Mario, and the host's game walks that, so it is where the host's word has it,
           and drawn from it, until its player's stick takes it, from there) */
        ownSnap(s, w);
        if (FA.showUntil != 0 && (int)(FA.showUntil - G.frame) > 0) {
            /* (an action of this game's shows on it: its own drawn, where the host's word has it, till the host's
               word shows the host's doing it) */
            OG.drawn = 1;
            party->flags2 &= ~PARTY_DISP_FLAG_HIDDEN;
        }
        return;
    }
    OG.drawn = 1;
    party->flags2 &= ~PARTY_DISP_FLAG_HIDDEN; /* (shown here: the host's drawing of it hidden, coopOwnNewest) */
}

static void coopOwnFrame(void) {
    const MarioWork* mario = marioGetPtr();
    int s;
    if (ssNetIsHost() && mario != NULL && mario->animName != NULL) {
        /* (tests: the host's own Mario, what its guests' ghost of him is measured against) */
        portLookCheck(29, "mario", mario->position.x, mario->position.y, mario->position.z, mario->displayDirection,
                      mario->animName);
    }
    for (s = 2; s <= 4; s++) {
        /* (tests: each seat's character as this game draws it; Brothers mode's Luigi checked by brothers_pc.c) */
        const PartyEntry* party = s == 2 && portBrothersOn() ? NULL : seatParty(s);
        if (party != NULL && !(party->flags2 & PARTY_DISP_FLAG_HIDDEN) && party->animName != NULL) {
            portLookCheck(30 + s, s == 2 ? "seat2-partner" : s == 3 ? "seat3" : "seat4", party->position.x,
                          party->position.y, party->position.z, party->displayDirection, party->animName);
        }
    }
    if (ssNetIsHost()) {
        coopOwnHostFrame();
    } else {
        coopOwnGuestFrame();
    }
}

/* the guest, at the frame's end: its own character as this frame left it */
static void coopOwnSend(void) {
    OwnMsg m;
    PartyEntry* party;
    const int s = seatOf(ssNetMe());
    if (!portStateSyncCoopGuest() || s < 2 || s > 4 || !OG.known || G.seq != SEQ_GAME ||
        (party = seatParty(s)) == NULL) {
        return;
    }
    if (OG.seat == s && !OG.drawn && seatFree(s) && otherIndex(1) >= 0) {
        /* (taken by its player's stick in this frame's update: its own drawn from this frame, the host's word
           of it hidden: ghost 1 the partner, 2 Luigi, 3 and 4 the follower and the companion) */
        OG.drawn = 1;
        party->flags2 &= ~PARTY_DISP_FLAG_HIDDEN;
        ssGhostHide(otherIndex(1), s == 2 ? (portBrothersOn() ? 2 : 1) : s);
    }
    memset(&m, 0, sizeof(m));
    m.ms = (uint32_t)ssNetMs();
    m.epoch = OG.epoch;
    m.seat = (uint8_t)s;
    m.owned = (uint8_t)(OG.seat == s && seatFree(s));
    m.member = party->currentMemberId;
    m.x = party->position.x;
    m.y = party->position.y;
    m.z = party->position.z;
    m.dir = party->targetDir;
    if (isfinite(party->displayDirection) && party->displayDirection > -100.0f && party->displayDirection < 360.0f) {
        m.face = (int16_t)(lroundf(party->displayDirection * 10.0f) + OWN_FACE);
    }
    snprintf(m.anim, sizeof(m.anim), "%s", party->animName != NULL ? party->animName : "");
    m.hitSeq = sOwnHit.seq;
    snprintf(m.hit, sizeof(m.hit), "%s", sOwnHit.name);
    {
        /* a room on the relay: the same as the last (but for its clock) isn't sent again for OWN_SAME_MS, well
           inside the host's OWN_STALE_MS; a changed one goes at once. By address every one, as before */
        static OwnMsg sLast;
        static unsigned long long sLastMs;
        const uint32_t ms = m.ms;
        m.ms = sLast.ms;
        if (ssNetRoom() && sLastMs != 0 && !memcmp(&m, &sLast, sizeof(m)) && ssNetMs() - sLastMs < OWN_SAME_MS) {
            return;
        }
        m.ms = ms;
        sLast = m;
        sLastMs = ssNetMs();
    }
    ssNetSendTo(1, SS_MSG_OWN, &m, sizeof(m));
}

/* ---- the guest's field actions (docs/STATE_SYNC.md "The guest's field actions") ----
 * A co-op guest walking its own character (OG.seat) plays what its buttons do in its own game at once, as its
 * walk and hops are: Luigi's swing (brothers_pc.c), a talk's start (the character turns to the NPC and talks),
 * a block's hit, the partner's field move starting (its pose), a swap of the partner or companion (L, R, the
 * Character strip). Its game sends each as an action (reliable, SS_R_COOP_ACT), and its controller goes to the
 * host without those buttons meanwhile. The host's game does it for real from where the guest's game had the
 * character (its swing's hits, the NPC's talk script, the block's event, the partner's move, the swap) and
 * answers: done, refused (the guest's game takes its own back: the swing stopped, the pose, the partner it
 * had), or a swing's result (what it hit). The talk, the move and the swap hold the seat in the host's game as
 * before, so the guest's screen shows the host's doing it a round trip later, after its own start. X held: the
 * partner's move is the host's X as long as the guest's is down (its letting go is an action too). The guest
 * playing Mario's items and partner switches from its Start menu (C_USE) are answered the same way; a refused
 * item takes the host's whole progress again (the pouch as it is). */

typedef struct ActMsg {
    uint8_t kind;    /* PORT_COOP_ACT_* */
    uint8_t seat;
    uint8_t verdict; /* ACT_ASK (the guest's), or the host's answer */
    uint8_t pad;
    uint32_t id;    /* the guest's number for it: the answer names it */
    uint32_t epoch; /* the seat's epoch as the guest had it */
    uint32_t frame; /* the guest's pad read it came with (the logs) */
    int32_t a, b;   /* the kind's (a: the partner, companion or strip entry; b: what a refusal puts back) */
    float x, y, z, dir; /* the guest's character, as its game had it */
    char name[32];      /* the NPC or block; the host's answer: what came of it, or why not */
} ActMsg;

WIRE_SIZE(ActMsg, 72);

enum { ACT_ASK, ACT_DONE, ACT_REFUSED, ACT_RESULT };

#define ACT_ANSWER_MS 5000 /* the guest: an action never answered is forgotten after this (a link lost) */
#define ACT_SHOW_FRAMES 45 /* the guest: its own character drawn this long for an action's pose */
#define ACT_SWAP_MS 2500   /* the guest: its swap kept this long for the host's to show */
#define ACT_NEAR 80.0f     /* the host: an action this far from where it has the character is refused */
#define ACT_WATCH 60       /* the host: frames for a move or swap to show in its game */

static const char* actName(int kind) {
    switch (kind) {
        case PORT_COOP_ACT_HAMMER: return "hammer";
        case PORT_COOP_ACT_TALK: return "talk";
        case PORT_COOP_ACT_HIT: return "hit";
        case PORT_COOP_ACT_ABILITY: return "ability";
        case PORT_COOP_ACT_ABILITY_UP: return "ability up";
        case PORT_COOP_ACT_SWAP: return "swap";
        case PORT_COOP_ACT_PICK: return "pick";
        case PORT_COOP_ACT_ITEM: return "item";
        case PORT_COOP_ACT_MENU_PARTNER: return "menu partner";
        case PORT_COOP_ACT_SPIN: return "spin";
    }
    return "?";
}

/* the guest, its controller read (after the count): a new press of a field button, or a pick (the Character
   strip's mark), is the one an action's delay counts from */
static void actPressRead(const PADStatus* pad) {
    const uint16_t keys = PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_TRIGGER_L | PAD_TRIGGER_R;
    const uint16_t now = pad->err == PAD_ERR_NONE ? pad->button : 0;
    if (!portStateSyncCoopGuest()) {
        FA.raw = 0;
        return;
    }
    if ((now & keys & ~FA.raw) || (pad->err == PAD_ERR_NONE && ((pad->analogA & 0xF8) == 0xE8 || (pad->analogA & 0xF8) == 0xF0))) {
        FA.pressPad = C.padFrame;
    }
    FA.raw = now;
    if (FA.xDown && !(now & PAD_BUTTON_X)) {
        /* (its X let go: the partner's move it started is let go in the host's game too, wherever the seat is) */
        portStateSyncCoopAct(PORT_COOP_ACT_ABILITY_UP, NULL, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f);
    }
}

/* the guest: its controller as it goes to the host while it walks its own character: the buttons its own game
   plays as actions taken off (A: a hop or Luigi's talk; B: a talk, a block, Luigi's swing; X: the partner's
   move; L, R: a swap), and a pick's mark (an emote's stays: shown by every game) */
static void actStrip(PadRec* r) {
    if (OG.seat < 2 || OG.seat > 4) {
        return;
    }
    r->button &= (uint16_t)~(PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_TRIGGER_L | PAD_TRIGGER_R);
    r->l = r->r = 0;
    if ((r->a & 0xE0) != 0xE0 || (r->a & 0xF8) == 0xE8 || (r->a & 0xF8) == 0xF0) {
        r->a = 0; /* (A's pressure, or a pick: this game's own) */
    }
    r->b = 0;
}

/* frames between this action and the press it answers (-1: no press of the guest's own seen lately) */
static int actDelay(void) {
    const uint32_t d = C.padFrame - FA.pressPad;
    return FA.pressPad != 0 && d < 600u ? (int)d : -1;
}

static uint32_t actPendNew(int kind, int seat, int prev) {
    int i, slot = 0;
    for (i = 0; i < ACT_PENDING; i++) {
        if (FA.pend[i].id == 0) {
            slot = i;
            break;
        }
        if (FA.pend[i].at < FA.pend[slot].at) {
            slot = i; /* (full: the oldest goes) */
        }
    }
    if (++FA.next == 0) {
        FA.next = 1;
    }
    FA.pend[slot].id = FA.next;
    FA.pend[slot].kind = kind;
    FA.pend[slot].seat = seat;
    FA.pend[slot].prev = prev;
    FA.pend[slot].answered = 0;
    FA.pend[slot].at = ssNetMs();
    return FA.next;
}

unsigned portStateSyncCoopAct(int kind, const char* name, int a, int prev, float x, float y, float z, float dir) {
    ActMsg m;
    const int up = kind == PORT_COOP_ACT_ABILITY_UP;
    const int s = up ? seatOf(ssNetMe()) : OG.seat;
    if (!portStateSyncCoopGuest() || s < 2 || s > 4 || kind < PORT_COOP_ACT_HAMMER ||
        (kind > PORT_COOP_ACT_PICK && kind != PORT_COOP_ACT_SPIN) || (up && !FA.xDown)) {
        return 0;
    }
    memset(&m, 0, sizeof(m));
    m.kind = (uint8_t)kind;
    m.seat = (uint8_t)s;
    m.verdict = ACT_ASK;
    m.epoch = OG.epoch;
    m.frame = C.padFrame;
    m.a = a;
    m.b = prev;
    m.x = x, m.y = y, m.z = z, m.dir = dir;
    snprintf(m.name, sizeof(m.name), "%s", name != NULL ? name : "");
    if (up) {
        FA.xDown = 0;
        m.id = 0; /* (no answer to it) */
        ssNetSendRelTo(1, SS_R_COOP_ACT, &m, sizeof(m));
        ssLog("fieldact: ability let go: to the host");
        return 1;
    }
    m.id = actPendNew(kind, s, prev);
    ssNetSendRelTo(1, SS_R_COOP_ACT, &m, sizeof(m));
    FA.showUntil = G.frame + ACT_SHOW_FRAMES;
    if (kind == PORT_COOP_ACT_SWAP || kind == PORT_COOP_ACT_PICK) {
        FA.swapUntil = ssNetMs() + ACT_SWAP_MS;
        FA.swapTo = a;
    }
    if (kind == PORT_COOP_ACT_ABILITY) {
        FA.xDown = 1;
    }
    ssLog("fieldact: %s #%u%s%s%s (seat %d, %d) shown here %d frames after its press", actName(kind), m.id,
          m.name[0] ? " '" : "", m.name, m.name[0] ? "'" : "", s, a, actDelay());
    return m.id;
}

/* the guest: an action the host refused, taken back here */
static void actUndo(int kind, int seat, int prev) {
    FA.showUntil = 0;
    if (kind == PORT_COOP_ACT_SWAP || kind == PORT_COOP_ACT_PICK) {
        FA.swapUntil = 0;
    }
    if (kind == PORT_COOP_ACT_ITEM) {
        /* (its pouch, HP and FP as the host's game has them: the host's whole progress again) */
        CoopMsg q;
        memset(&q, 0, sizeof(q));
        q.kind = C_RESYNC;
        coopSend(1, &q);
        return;
    }
    if (kind == PORT_COOP_ACT_MENU_PARTNER) {
        return; /* (the host's partner: the host's word puts it back, coopOwnGuestFrame) */
    }
    if (kind == PORT_COOP_ACT_ABILITY) {
        FA.xDown = 0;
    }
    if (seat == 2 && portBrothersOn()) {
        portBrothersGuestUndo(kind);
    } else if (seat >= 2 && seat <= 4) {
        portCoopGuestActUndo(seat - 2, kind, prev);
    }
}

/* the guest: the host's answer */
static void actAnswered(const ActMsg* m) {
    int i;
    for (i = 0; i < ACT_PENDING; i++) {
        if (FA.pend[i].id != 0 && FA.pend[i].id == m->id) {
            break;
        }
    }
    if (i == ACT_PENDING) {
        ssLog("fieldact: the host's word on %s #%u came late (forgotten here)", actName(m->kind), m->id);
        return;
    }
    if (m->verdict == ACT_REFUSED) {
        ssLog("fieldact: the host's word on %s #%u: refused (%s): taken back here", actName(FA.pend[i].kind), m->id,
              m->name);
        actUndo(FA.pend[i].kind, FA.pend[i].seat, FA.pend[i].prev);
        FA.pend[i].id = 0;
        return;
    }
    if (m->verdict == ACT_RESULT) {
        ssLog("fieldact: the host's word on %s #%u: %s%s%s", actName(FA.pend[i].kind), m->id,
              m->name[0] ? "hit '" : "a miss", m->name, m->name[0] ? "'" : "");
        FA.pend[i].id = 0;
        return;
    }
    ssLog("fieldact: the host's word on %s #%u: done (%s)", actName(FA.pend[i].kind), m->id, m->name);
    FA.pend[i].answered = 1;
    if (FA.pend[i].kind != PORT_COOP_ACT_HAMMER) {
        FA.pend[i].id = 0; /* (a swing's result comes after: what it hit) */
    }
}

/* the host: an answer to player `to`'s action */
static void actReply(int to, const ActMsg* q, int verdict, const char* what) {
    ActMsg r = *q;
    r.verdict = (uint8_t)verdict;
    snprintf(r.name, sizeof(r.name), "%s", what != NULL ? what : "");
    ssNetSendRelTo(to, SS_R_COOP_ACT, &r, sizeof(r));
}

static void actAnswerUse(int to, int kind, uint32_t id, int ok, const char* what) {
    ActMsg r;
    if (id == 0) {
        return; /* (an older build's: no number to answer) */
    }
    memset(&r, 0, sizeof(r));
    r.kind = (uint8_t)kind;
    r.seat = 1;
    r.id = id;
    ssLog("fieldact: player %d's %s #%u: %s (%s)", to, actName(kind), id, ok ? "done here" : "refused", what);
    actReply(to, &r, ok ? ACT_DONE : ACT_REFUSED, what);
}

/* the host: an action waiting for its game to show it (the partner's move under way, the swap made) */
static struct {
    ActMsg m;
    int from, want;
    unsigned until;
} sActWatch[5];

/* the host: the guest's swing under way here, its result told when it lands */
static struct {
    ActMsg m;
    int from;
    unsigned at; /* (G.frame: a swing whose result never came is forgotten after 2 seconds) */
} sActSwing;

void portStateSyncCoopActResult(int seat, const char* what) {
    if (seat != 2 || sActSwing.from == 0 || !ssNetIsHost()) {
        return;
    }
    ssLog("fieldact: player %d's hammer #%u: %s%s%s", sActSwing.from, sActSwing.m.id, what != NULL && what[0] ? "hit '" : "a miss",
          what != NULL ? what : "", what != NULL && what[0] ? "'" : "");
    if (ssNetLinked(sActSwing.from)) {
        actReply(sActSwing.from, &sActSwing.m, ACT_RESULT, what);
    }
    sActSwing.from = 0;
}

static void gotActHost(int from, ActMsg* m) {
    const int s = m->seat;
    PartyEntry* party = s >= 2 && s <= 4 ? seatParty(s) : NULL;
    const int bros = s == 2 && portBrothersOn();
    const char* why = NULL;
    const char* done = "done";
    int watch = 0;
    m->name[sizeof(m->name) - 1] = '\0';
    if (m->kind == PORT_COOP_ACT_ABILITY_UP) {
        if (FA.xHeldBy == from) {
            portCoopGuestAbility(0);
            FA.xHeldBy = 0;
            ssLog("fieldact: player %d's X let go: the partner's move's X here too", from);
        }
        return; /* (no answer to it) */
    }
    if (s < 2 || s > 4 || holder(s) != from || seatSub(s) || !coopPlayer(from) || !portStateSyncCoopHost()) {
        why = "not that player's seat";
    } else if (!isfinite(m->x) || !isfinite(m->y) || !isfinite(m->z) || !isfinite(m->dir) || fabsf(m->x) > 1e5f ||
               fabsf(m->y) > 1e5f || fabsf(m->z) > 1e5f) {
        why = "no such place";
    } else if (!hostFieldFree()) {
        why = "the host's field isn't free";
    } else if (party == NULL) {
        why = "no character";
    } else if ((m->kind == PORT_COOP_ACT_HAMMER || m->kind == PORT_COOP_ACT_TALK || m->kind == PORT_COOP_ACT_HIT ||
                m->kind == PORT_COOP_ACT_SPIN) &&
               sqrtf((m->x - party->position.x) * (m->x - party->position.x) +
                     (m->y - party->position.y) * (m->y - party->position.y) +
                     (m->z - party->position.z) * (m->z - party->position.z)) > ACT_NEAR) {
        why = "too far from where the host has it";
    } else {
        switch (m->kind) {
            case PORT_COOP_ACT_HAMMER:
                if (!bros) {
                    why = "not Luigi";
                } else if ((sActSwing.from != 0 && G.frame - sActSwing.at < 120u) ||
                           !portBrothersGuestSwing(m->x, m->y, m->z, m->dir)) {
                    why = "Luigi can't swing now";
                } else {
                    sActSwing.m = *m;
                    sActSwing.from = from;
                    sActSwing.at = G.frame;
                    done = "swung";
                }
                break;
            case PORT_COOP_ACT_SPIN:
                /* Luigi's Spin Dash: its game spins him (its own walk); here it counts for what he touches, as
                   his swing's hit does (pm64_spin.h) */
                if (!bros) {
                    why = "not Luigi";
                } else if (!portBrothersGuestSpin(m->x, m->y, m->z, m->dir)) {
                    why = "Luigi can't spin now";
                } else {
                    done = "spun";
                }
                break;
            case PORT_COOP_ACT_TALK:
                if (!(bros ? portBrothersGuestTalk(m->name, m->x, m->y, m->z)
                            : portCoopGuestTalk(s - 2, m->name, m->x, m->y, m->z))) {
                    why = "no talk starts here";
                } else {
                    done = "started";
                }
                break;
            case PORT_COOP_ACT_HIT:
                if (bros || !portCoopGuestHit(s - 2, m->name, m->x, m->y, m->z)) {
                    why = "nothing to hit here";
                } else {
                    done = "hit";
                }
                break;
            case PORT_COOP_ACT_ABILITY:
                if (bros || s != 2) {
                    why = "no partner's move";
                } else if (party->currentMemberId != m->a || seatBusy(2) || FA.xHeldBy != 0) {
                    why = party->currentMemberId != m->a ? "another partner here" : "a move under way";
                } else {
                    portCoopGuestAbility(1);
                    FA.xHeldBy = from;
                    watch = 1;
                }
                break;
            case PORT_COOP_ACT_SWAP:
            case PORT_COOP_ACT_PICK:
                if (bros) {
                    why = "Luigi has no swap";
                } else if (!(m->kind == PORT_COOP_ACT_SWAP ? portCoopGuestSwap(s - 2, m->a) : portCoopGuestPick(s - 2, m->a))) {
                    why = "that one isn't free";
                } else if (s == 2 && m->a >= 1 && m->a <= 7 && party->currentMemberId != m->a) {
                    watch = 1; /* (the partner's swap: at the field's next free frame, gameplus_pc.c) */
                } else {
                    done = "swapped";
                }
                break;
            default:
                why = "not an action";
                break;
        }
    }
    if (why != NULL) {
        ssLog("fieldact: player %d's %s #%u%s%s%s: refused (%s)", from, actName(m->kind), m->id, m->name[0] ? " '" : "",
              m->name, m->name[0] ? "'" : "", why);
        actReply(from, m, ACT_REFUSED, why);
        return;
    }
    if (watch) {
        sActWatch[s].m = *m;
        sActWatch[s].from = from;
        sActWatch[s].want = m->kind == PORT_COOP_ACT_ABILITY ? 0 : m->a;
        sActWatch[s].until = G.frame + ACT_WATCH;
        ssLog("fieldact: player %d's %s #%u (%d): begun here, watched", from, actName(m->kind), m->id, m->a);
        return;
    }
    ssLog("fieldact: player %d's %s #%u%s%s%s: %s here", from, actName(m->kind), m->id, m->name[0] ? " '" : "", m->name,
          m->name[0] ? "'" : "", done);
    actReply(from, m, ACT_DONE, done);
}

static void coopActRel(int from, const uint8_t* p, size_t n) {
    ActMsg m;
    if (n < sizeof(m)) {
        return;
    }
    memcpy(&m, p, sizeof(m));
    if (ssNetIsHost()) {
        if (from >= 2 && from <= SS_PLAYERS && m.verdict == ACT_ASK) {
            gotActHost(from, &m);
        }
    } else if (from == 1 && m.verdict != ACT_ASK) {
        m.name[sizeof(m.name) - 1] = '\0';
        actAnswered(&m);
    }
}

/* $TTYD_SYNC_ITEM_TEST=<frame>:<item> (tests; the headless build has no Start menu to walk through): the co-op
   guest playing Mario uses that item on Mario then, as its Start menu's item list does (here at once: the pouch,
   HP, FP and SP; the host's game told) */
static void itemTest(void) {
    static int sAt = -1, sItem;
    const ItemData* d;
    MarioWork* mario = marioGetPtr();
    if (sAt < 0) {
        const char* v = getenv("TTYD_SYNC_ITEM_TEST");
        sAt = v != NULL && strchr(v, ':') != NULL ? (int)strtol(v, NULL, 10) : 0;
        sItem = v != NULL && strchr(v, ':') != NULL ? (int)strtol(strchr(v, ':') + 1, NULL, 0) : 0;
    }
    if (sAt <= 0 || G.frame < (unsigned)sAt || !portStateSyncCoopMarioMenuOk() || mario == NULL) {
        return;
    }
    sAt = 0;
    if (sItem <= 0 || sItem >= ITEM_TYPE_MAX || !pouchCheckItem((ItemType)sItem)) {
        ssLog("test: item %d isn't in this guest's pouch", sItem);
        return;
    }
    d = &itemDataTable[sItem];
    pouchRemoveItem((ItemType)sItem);
    pouchSetHP((s16)(pouchGetHP() + d->hp_restored));
    pouchSetFP((s16)(pouchGetFP() + d->fp_restored));
    pouchSetAP((s16)(pouchGetAP() + d->sp_restored));
    if (d->hp_restored > 0) {
        effRecoveryEntry(0, d->hp_restored, mario->position.x, mario->position.y + mario->wPlayerCollisionBox.y,
                         mario->position.z);
    }
    ssLog("test: this guest (Mario) uses item %d from its Start menu", sItem);
    FA.pressPad = C.padFrame; /* (as the menu's A, this frame) */
    portStateSyncCoopItemUsed(sItem, 0);
}

/* each frame: the guest's actions never answered forgotten, its test's item; the host's watched actions */
static void coopActFrame(void) {
    int i, s;
    if (!ssNetConnected()) {
        memset(&FA.pend, 0, sizeof(FA.pend));
        FA.xDown = FA.xHeldBy = 0;
        FA.swapUntil = 0;
        sActSwing.from = 0;
        memset(sActWatch, 0, sizeof(sActWatch));
        return;
    }
    if (!ssNetIsHost()) {
        const unsigned long long now = ssNetMs();
        for (i = 0; i < ACT_PENDING; i++) {
            if (FA.pend[i].id != 0 && now - FA.pend[i].at > ACT_ANSWER_MS) {
                ssLog("fieldact: %s #%u: no word from the host (%s): kept as it is here", actName(FA.pend[i].kind),
                      FA.pend[i].id, FA.pend[i].answered ? "its result" : "its answer");
                FA.pend[i].id = 0;
            }
        }
        if (portStateSyncCoopGuest() && G.seq == SEQ_GAME && gp != NULL && !gp->inBattle) {
            itemTest();
        }
        return;
    }
    if (FA.xHeldBy != 0 && (holder(2) != FA.xHeldBy || !coopPlayer(FA.xHeldBy) || !portStateSyncCoopPadHere(2))) {
        ssLog("fieldact: player %d's X let go (the seat or its controller gone)", FA.xHeldBy);
        portCoopGuestAbility(0);
        FA.xHeldBy = 0;
    }
    if (sActSwing.from != 0 && !portBrothersOn()) {
        sActSwing.from = 0; /* (Brothers mode off meanwhile: no swing to tell of) */
    }
    for (s = 2; s <= 4; s++) {
        PartyEntry* party;
        int shown;
        if (sActWatch[s].from == 0) {
            continue;
        }
        party = seatParty(s);
        if (sActWatch[s].m.kind == PORT_COOP_ACT_ABILITY) {
            shown = seatBusy(2) || (gp != NULL && !marioChkKey()) || winCheck();
        } else {
            shown = party != NULL && party->currentMemberId == sActWatch[s].want;
        }
        if (shown || (int)(G.frame - sActWatch[s].until) > 0 || !ssNetLinked(sActWatch[s].from)) {
            const int from = sActWatch[s].from;
            const char* what = sActWatch[s].m.kind == PORT_COOP_ACT_ABILITY ? (shown ? "started" : "nothing started")
                                                                             : (shown ? "swapped" : "no swap came");
            ssLog("fieldact: player %d's %s #%u: %s here", from, actName(sActWatch[s].m.kind), sActWatch[s].m.id, what);
            if (ssNetLinked(from)) {
                actReply(from, &sActWatch[s].m, shown ? ACT_DONE : ACT_REFUSED, what);
            }
            if (!shown && sActWatch[s].m.kind == PORT_COOP_ACT_ABILITY && FA.xHeldBy == from) {
                portCoopGuestAbility(0);
                FA.xHeldBy = 0;
            }
            sActWatch[s].from = 0;
        }
    }
}

/* leaving: a co-op guest's game had the host's world: back to the title (its own save as it was) */
static void coopLeave(void) {
    marioMenuRelease("let go (this game leaves the room)");
    if (ssNetIsHost() && ssNetConnected()) {
        /* (the guests told it's for good: each asked where to go at once, no 10 seconds' wait) */
        CoopMsg m;
        memset(&m, 0, sizeof(m));
        m.kind = C_BYE;
        coopSend(SS_ALL, &m);
        ssLog("co-op: the host leaves: the guests told");
        memset(K.lastSeat, 0, sizeof(K.lastSeat));
    }
    if (C.was && gp != NULL && !ssNetIsHost()) {
        coopHideOwn(0);
        coopRemember();
        /* where now (online_end_pc.c: the player's own adventure or the title; the copy is kept). A reason
           already asked about (the host gone, removed) stands. */
        portOnlineEnd(PORT_END_LEFT, portStateSyncPlayerName(1), K.ownSlot);
        ssLog("co-op: left: asked where to go");
    }
    memset(&C, 0, sizeof(C));
    if (FA.xHeldBy != 0) {
        portCoopGuestAbility(0); /* (a guest's X held here: let go) */
    }
    memset(&FA, 0, sizeof(FA));
    memset(O, 0, sizeof(O)); /* (every seat's own-character reports: the next session's start over) */
    memset(&OG, 0, sizeof(OG));
    memset(SP.hist, 0, sizeof(SP.hist)); /* (its second controller's seat itself kept: a rejoin asks again) */
    SP.seat = 0;
    SP.awayAt = 0;
    SP.startHeld = 0;
    if (ssNetIsHost()) {
        memset(K.subWant, 0, sizeof(K.subWant));
        memset(K.subLast, 0, sizeof(K.subLast));
        memset(K.subFull, 0, sizeof(K.subFull));
    }
    memset(sActWatch, 0, sizeof(sActWatch));
    sActSwing.from = 0;
    K.infoSent = K.bye = K.kicked = 0;
    K.lostAt = 0;
    K.rulesNoted = 0;
    memset(K.awayUntil, 0, sizeof(K.awayUntil));
    K.lostAway = K.awaySelf = 0;
}

/* ---- each guest's copy of the host's adventure (coop_save_pc.c) ---- */

#define SAVE_EVERY_FRAMES (60 * 60 * 3) /* every 3 minutes */
#define SAVE_ROOM_FRAMES (60 * 30)      /* a room change: at most every 30 seconds */

int portCardLiveImage(uint8_t* image); /* cardmgr.c */

static struct {
    unsigned lastAt;
    int lastCount;
    char lastMap[16];
} sSaveOut;

/* the host: its save as it stands to every co-op guest when it saved, changed rooms (at most every 30
   seconds) or every 3 minutes */
static void coopSaveFrame(void) {
    static uint8_t sImage[PORT_CARD_SLOT_SIZE];
    int p, why = 0;
    if (!portStateSyncCoopHost() || G.seq != SEQ_GAME || seqCheckSeq() || gp->inBattle || !G.baseline) {
        return;
    }
    if (gp->saveCount != sSaveOut.lastCount) {
        why = 1;
    } else if (strcmp(sSaveOut.lastMap, gp->currentMapName) != 0 && G.frame - sSaveOut.lastAt >= SAVE_ROOM_FRAMES) {
        why = 2;
    } else if (G.frame - sSaveOut.lastAt >= SAVE_EVERY_FRAMES || sSaveOut.lastAt == 0) {
        why = 3;
    }
    if (why == 0 || !portCardLiveImage(sImage)) {
        return;
    }
    sSaveOut.lastAt = G.frame;
    sSaveOut.lastCount = gp->saveCount;
    snprintf(sSaveOut.lastMap, sizeof(sSaveOut.lastMap), "%s", gp->currentMapName);
    for (p = 2; p <= SS_PLAYERS; p++) {
        if (coopPlayer(p)) {
            ssNetSendRelTo(p, SS_R_COOP_SAVE, sImage, sizeof(sImage));
        }
    }
    ssLog("co-op: the host's save to the guests for their copies (%s)",
          why == 1 ? "it saved" : why == 2 ? "a new room" : "every few minutes");
}

/* a guest: the host's save, kept as this player's copy (written whole, then renamed over the last) */
static void coopSaveRel(int from, const uint8_t* p, size_t n) {
    if (from != 1 || ssNetIsHost() || G.watch) {
        return;
    }
    if (p == NULL || n != PORT_CARD_SLOT_SIZE) {
        ssLog("co-op: the host's save came the wrong size (%u bytes, a slot is %u): not kept", (unsigned)n,
              (unsigned)PORT_CARD_SLOT_SIZE);
        return; /* (portCoopCopyWrite checks it too, and the image itself: portSaveImageInfo) */
    }
    if (portCoopCopyWrite(portStateSyncPlayerName(1), ssNetCode(), p, n)) {
        ssLog("co-op: the host's save kept as this player's copy (%u bytes)", (unsigned)n);
    } else {
        ssLog("co-op: the host's save came but wasn't kept (%u bytes: not a save this game reads)", (unsigned)n);
    }
}

/* ---- single-player runs ---- */

int portHardcoreActive(void);                           /* gameplus_pc.c's, or the remix modes' */
int portBoardRunState(void); /* board_run_aurora.c (headless.c: none) */

const char* portStateSyncSoloOnly(void) {
    if (portHardcoreActive()) {
        return "Hardcore files are single-player: they can't play online.";
    }
    if (portBoardRunState() != 0) {
        return "Leaderboard runs are single-player: they can't play online.";
    }
    return NULL;
}

/* cardmgr.c cardWrite: a co-op guest's game never saves (the adventure is the host's) */
int portStateSyncSaveHeld(void) {
    if (portStateSyncCoopGuest() || (G.on && C.was && !ssNetIsHost()) ||
        (portOnlineEndUp() && portOnlineEndWhy() != PORT_END_NO_SERVICE)) {
        portCoopNotice("Playing together: the host's game keeps the adventure. Your own save stays as it was.");
        return 1;
    }
    if (sHostWorld) {
        /* (the session over, the host's world still here: never saved over the player's own file) */
        portCoopNotice("This is the host's adventure: load your own file to save.");
        ssLog("co-op: a save refused: the host's world is still in memory");
        return 1;
    }
    return 0;
}

int portStateSyncHostWorld(void) {
    return sHostWorld;
}

void portStateSyncHostWorldGone(const char* why) {
    ssBattleFieldForget(why); /* (a battle's field part after it was that field's: statesync_battle_pc.c) */
    if (sHostWorld) {
        sHostWorld = 0;
        printf("[sync] the host's world is gone from memory (%s): saving is the player's own again\n", why);
        fflush(stdout);
    }
}

/* ---- each guest's own camera (coop_camera_pc.c) ---- */

int portStateSyncCoopOwnPose(float* x, float* y, float* z) {
    const Other* h = &G.pl[1];
    SsGhostPose me, partner, luigi, pose;
    int hasPartner = 0, hasLuigi = 0, ok = 0;
    const int s = seatOf(ssNetMe());
    if (!portStateSyncCoopGuest() || gp == NULL || ssBattleBusy() || !present(1) || h->count == 0 ||
        h->info.seq != SEQ_GAME || strcmp(h->info.map, gp->currentMapName) != 0) {
        return 0;
    }
    if (s == 1 && interpolate(1, &me, &partner, &hasPartner, &luigi, &hasLuigi)) {
        pose = me; /* (a guest playing Mario: the camera on the host's Mario) */
        ok = 1;
    } else if (s >= 2 && s <= 4) {
        /* (the guest's own character where it's drawn: the host's newest word) */
        float at[3];
        if (ndDrawnPos(at)) {
            *x = at[0], *y = at[1], *z = at[2];
            return 1;
        }
    }
    if (ok) {
        *x = pose.x, *y = pose.y, *z = pose.z;
    }
    return ok;
}

/* ---- the room's people: the players list, the host gone, removals, coming back (port/online_end.h) ---- */

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

static int coopPlatformSelf(void) {
#if defined(__EMSCRIPTEN__)
    return PORT_SYNC_OS_WEB;
#elif defined(__ANDROID__)
    return PORT_SYNC_OS_ANDROID;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return PORT_SYNC_OS_IOS;
#elif defined(__APPLE__)
    return PORT_SYNC_OS_MAC;
#elif defined(_WIN32)
    return PORT_SYNC_OS_WINDOWS;
#else
    return PORT_SYNC_OS_LINUX;
#endif
}

/* milliseconds from a stamp to now; 0 for a stamp past now (one taken later in the same frame, or the
   odd millisecond a "| 1" stamp adds): never the unsigned wrap that would end a wait at once */
static unsigned long long msSince(unsigned long long at, unsigned long long now) {
    return now > at ? now - at : 0;
}

/* a round trip as the page says it: good under a tenth of a second, fair under a quarter, else poor */
static int rttQuality(int ms) {
    return ms < 0 ? 0 : ms < 100 ? 1 : ms < 250 ? 2 : 3;
}

#ifdef TTYD_BACKEND_AURORA
int portTitleTempFile(void); /* title_menu_aurora.c: the title menu's file for the modes, never saved */
#endif

/* the card file this game plays now, if it's the player's own (not the title's file for the modes): -1 */
static int coopOwnFile(void) {
    if (gp == NULL || gp->saveSlot < 0 || gp->saveSlot > 3) {
        return -1;
    }
#ifdef TTYD_BACKEND_AURORA
    if (portTitleTempFile()) {
        return -1;
    }
#endif
    return gp->saveSlot;
}

/* a guest: the room and its seat, for "Welcome back" when it's in that room again */
static void coopRemember(void) {
    const int mine = seatOf(ssNetMe());
    if (mine >= 1) { /* (1: this guest played Mario) */
        snprintf(K.lastRoom, sizeof(K.lastRoom), "%s", ssNetCode());
        K.lastMySeat = mine;
        K.welcomed = 0;
    }
}

int portStateSyncPlayerPlatform(int player) {
    if (player < 1 || player > SS_PLAYERS || !G.on) {
        return PORT_SYNC_OS_NONE;
    }
    return player == ssNetMe() ? coopPlatformSelf() : K.platform[player];
}

int portStateSyncPlayerRtt(int player) {
    if (!G.on || player < 1 || player > SS_PLAYERS) {
        return -1;
    }
    if (ssNetIsHost()) {
        return player == 1 ? -1 : ssNetRttMs(player);
    }
    return player == 1 || player == ssNetMe() ? ssNetRttMs(1) : -1;
}

/* a guest's join, its last steps (port/include/port/statesync.h; online_status_pc.c times them): the host in a
   game, its whole progress here (a watcher's file isn't shared: none), this game in the host's room */
int ssJoinWorldStep(void) {
    const Other* h = &G.pl[1];
    if (!G.on || ssNetIsHost() || !ssNetLinked(1)) {
        return -1;
    }
    if (!G.watch && !coopTogether()) {
        return 0; /* (a party game, or each player's own game: nothing of the host's to come into) */
    }
    if (present(1) && h->info.seq != SEQ_GAME && h->info.seq != SEQ_BATTLE && h->info.seq != SEQ_MAP_CHANGE) {
        return 1; /* (the title, its files: the room waits for the host's Play or Party...) */
    }
    if (!G.watch && !C.took) {
        return 2;
    }
    if (present(1) && gp != NULL && (G.seq == SEQ_GAME || G.seq == SEQ_BATTLE) &&
        strcmp(h->info.map, gp->currentMapName) == 0) {
        return 4;
    }
    return 3;
}

int portStateSyncPlayerQuality(int player) {
    const int rtt = portStateSyncPlayerRtt(player);
    if (!G.on || player < 1 || player > SS_PLAYERS || (player == 1 && ssNetIsHost())) {
        return 0;
    }
    if (rtt >= 0) {
        /* (the slowest tenth of the last 10 seconds' round trips and the quiet, online_status_pc.c: the last
           ping alone flickered between good and poor on a jittery link) */
        const int q = portOnlineLinkQuality(player == ssNetMe() ? 1 : player);
        return q >= PORT_LINK_GOOD ? (q > PORT_LINK_POOR ? PORT_LINK_POOR : q) : rttQuality(rtt);
    }
    return ssNetIsHost() ? 0 : K.quality[player];
}

int portStateSyncRemove(int player) {
    CoopMsg m;
    int s;
    if (!G.on || !ssNetIsHost() || player < 2 || player > SS_PLAYERS) {
        return 0;
    }
    if (!ssNetLinked(player)) {
        /* (not linked, but a game holds its place on the relay: one reconnecting, one stuck before its key.
           It can't be told; the relay takes it out, and it's turned away if it comes back) */
        if (!ssNetPlaceHeld(player)) {
            return 0;
        }
        ssLog("co-op: the host removes player %d (not linked: the relay takes its place's game out)", player);
        ssNetRemove(player);
        return 1;
    }
    memset(&m, 0, sizeof(m));
    m.kind = C_KICK;
    coopSend(player, &m);
    for (s = 2; s <= 4; s++) {
        if (C.seat[s] == player) {
            C.seat[s] = 0;
            C.sub[s] = 0; /* (its second controller's too) */
        }
    }
    sendSeats(SS_ALL);
    K.lastSeat[player] = 0;
    K.subWant[player] = K.subLast[player] = K.subFull[player] = 0;
    K.kickAt[player] = ssNetMs() | 1;
    /* (turned away from now: told, its game leaves at once and can join again before the close a moment
       later, and was let back in as new) */
    ssNetBan(player);
    ssLog("co-op: the host removes player %d (told; the connection closes in a moment)", player);
    return 1;
}

/* ---- stepping away: a phone's app in the background (port/include/port/statesync.h
   portStateSyncBackground; docs/STATE_SYNC.md "Leaving and coming back") ---- */

#define AWAY_SECONDS 600 /* a player who stepped away is waited for this long: their place, link and seat */

int ssPlayerAway(int player) {
    return G.on && player >= 1 && player <= SS_PLAYERS && K.awayUntil[player] > ssNetMs();
}

int portStateSyncPlayerAway(int player) {
    return ssPlayerAway(player);
}

/* a guest: the host gone this long is the room's end (2 minutes; 10 when it said it stepped away) */
static unsigned long long hostGraceMs(int away) {
    const char* v = getenv("TTYD_SYNC_HOST_GRACE");
    if (v != NULL && atoi(v) > 0) {
        return (unsigned long long)atoi(v) * 1000ull;
    }
    return away ? AWAY_SECONDS * 1000ull : 120000ull;
}

/* a player's stepping away or coming back, said on every screen: "<name> stepped away." / "<name> is back." */
static void awayNotice(int player, int away) {
    static char sLine[4][SS_NAME * 2 + 64];
    static int sAt;
    char name[SS_NAME * 2];
    char* line = sLine[sAt++ % 4]; /* (a notice keeps the pointer: a few in a row each their own) */
    portStateSyncPlayerNameShown(player, name, sizeof(name));
    if (name[0] == '\0') {
        snprintf(name, sizeof(name), "%s", portTrGame(player == 1 ? "The host" : "A player"));
    }
    snprintf(line, sizeof(sLine[0]), portTrGame(away ? "%s stepped away." : "%s is back."), name);
    portCoopNotice(line);
}

/* who stepped away (or is back) as a message says: the host's own and a guest's to it (from), or the host
   passing a guest's on (v[2]) */
static void coopAwayGot(int from, const CoopMsg* m) {
    const int who = ssNetIsHost() ? from : m->v[2];
    const int away = m->v[0] != 0;
    const int seconds = m->v[1] > 0 && m->v[1] <= AWAY_SECONDS ? m->v[1] : AWAY_SECONDS;
    if (who < 1 || who > SS_PLAYERS || who == ssNetMe() || (!ssNetIsHost() && from != 1)) {
        return;
    }
    if (away == (K.awayUntil[who] > ssNetMs())) {
        if (away) {
            K.awayUntil[who] = ssNetMs() + (unsigned long long)seconds * 1000ull; /* (said again: from now) */
        }
        return;
    }
    K.awayUntil[who] = away ? ssNetMs() + (unsigned long long)seconds * 1000ull : 0;
    ssLog("co-op: player %d %s", who, away ? "stepped away (their app in the background): their place is kept"
                                          : "is back");
    if (ssNetIsHost() || who == 1) {
        ssNetPeerAway(who, away ? seconds : 0); /* (the link to them: their quiet isn't a drop meanwhile) */
    }
    if (ssNetIsHost()) {
        CoopMsg q = *m;
        q.v[2] = who;
        coopSend(SS_ALL, &q); /* (the others told too; its own game leaves its own out) */
    }
    awayNotice(who, away);
}

/* a new session with that player's game (it was relaunched: it never says it's back): not away any more, and
   the host tells the others */
static void coopAwayForget(int player) {
    if (player < 1 || player > SS_PLAYERS || K.awayUntil[player] == 0) {
        return;
    }
    K.awayUntil[player] = 0;
    ssLog("co-op: player %d is back (a new session: their game started again)", player);
    if (ssNetIsHost() || player == 1) {
        ssNetPeerAway(player, 0);
    }
    if (ssNetIsHost() && player >= 2) {
        CoopMsg q;
        memset(&q, 0, sizeof(q));
        q.kind = C_AWAY;
        q.v[2] = player;
        coopSend(SS_ALL, &q);
    }
}

/* this game's player steps away (1) or is back (0): the others told (a guest's through the host) */
static void coopAwaySend(int away) {
    CoopMsg m;
    if (!G.on || ssNetMe() < 1 || K.awaySelf == away) {
        return;
    }
    K.awaySelf = away;
    memset(&m, 0, sizeof(m));
    m.kind = C_AWAY;
    m.v[0] = away;
    m.v[1] = away ? AWAY_SECONDS : 0;
    m.v[2] = ssNetMe();
    coopSend(ssNetIsHost() ? SS_ALL : 1, &m);
    ssLog("co-op: this player %s", away ? "steps away (the app to the background): the others told"
                                        : "is back (the app in front again): the others told");
}

/* each frame: the roster (the host's, every 2 seconds), removals, and a guest's room ending; 1: this game
   left (the caller stops for this frame) */
static int coopPeopleFrame(void) {
    const unsigned long long now = ssNetMs();
    char host[SS_NAME];
    int p, why = 0;
    if (ssNetIsHost()) {
        static int sRemoveAt = -1, sRemoveWho;
        if (sRemoveAt < 0) {
            /* $TTYD_SYNC_REMOVE_AT=<frame>:<player> (tests): the host removes that player then, as the
               Players page does */
            const char* v = getenv("TTYD_SYNC_REMOVE_AT");
            sRemoveAt = v != NULL ? (int)strtol(v, NULL, 10) : 0;
            sRemoveWho = v != NULL && strchr(v, ':') != NULL ? (int)strtol(strchr(v, ':') + 1, NULL, 10) : 2;
        }
        if (sRemoveAt > 0 && G.frame >= (unsigned)sRemoveAt) {
            sRemoveAt = 0;
            ssLog("test: the Players page's remove for player %d: %s", sRemoveWho,
                  portStateSyncRemove(sRemoveWho) ? "told" : "not in the room");
        }
        K.platform[1] = coopPlatformSelf();
        for (p = 2; p <= SS_PLAYERS; p++) {
            if (K.kickAt[p] != 0 && msSince(K.kickAt[p], now) > 1000) {
                K.kickAt[p] = 0;
                ssNetRemove(p);
            }
        }
        if (G.frame % 120 == 0 && ssNetConnected()) {
            CoopMsg m;
            memset(&m, 0, sizeof(m));
            m.kind = C_ROSTER;
            for (p = 1; p <= SS_PLAYERS; p++) {
                K.quality[p] = p == 1 ? 0 : portStateSyncPlayerQuality(p); /* (0..3, as the roster's always were) */
                m.v[p - 1] = (K.platform[p] & 0xFF) | (K.quality[p] << 8);
            }
            coopSend(SS_ALL, &m);
        }
        return 0;
    }
    if (ssNetLinked(1) && !K.infoSent) {
        CoopMsg m;
        memset(&m, 0, sizeof(m));
        m.kind = C_INFO;
        m.v[0] = coopPlatformSelf();
        coopSend(1, &m);
        K.infoSent = 1;
    }
    if (portStateSyncCoopGuest() && C.noticeAt != 0 && !K.rulesNoted && G.frame - C.noticeAt > 240) {
        /* the join note's second half: whose rules (the list: Multiplayer > Rules) */
        static char sRules[160];
        char name[SS_NAME * 2];
        K.rulesNoted = 1;
        if (portStateSyncRuleDiffs() == 0) {
            portCoopNotice("Same rules as yours.");
        } else {
            portStateSyncPlayerNameShown(1, name, sizeof(name));
            snprintf(sRules, sizeof(sRules), portTrGame("Playing on %s's rules: Rules, on the Multiplayer page, lists them."),
                     name[0] != '\0' ? name : portTrGame("the host"));
            portCoopNotice(sRules);
        }
        ssLog("co-op: rules: %d of the host's differ from this player's own", portStateSyncRuleDiffs());
    }
    /* The host gone (no link) or stepped away (its phone's app in the background): a long wait, never the
       10 seconds that ended a room on a Wi-Fi blip or a host's relaunch after a crash. After 3 seconds a window
       says who it's waiting for and how long it waits yet, with Leave always there (online_end_pc.c); the world
       stays held meanwhile, this player's keys off as ever. Over on the host's coming back (the same session:
       a relaunched host opens the same room again, online_resume_pc.c), its real goodbye (K.bye: at once), or
       the wait's end: 2 minutes, 10 when the host said it stepped away ($TTYD_SYNC_HOST_GRACE=<seconds>,
       tests). No host migration: the host's game is the world, the hub and a shared battle's decider, and no
       guest has its world as it stands (docs/STATE_SYNC.md). */
    if (ssNetLinked(1) && !ssPlayerAway(1)) {
        if (K.lostAt != 0 && msSince(K.lostAt, now) > 3000 && !K.welcomed && !K.lostAway && seatOf(ssNetMe()) >= 2) {
            /* (the reconnect path: the same session back after a drop) */
            static const char* const kBack[3] = {"Welcome back: you're 2P again.", "Welcome back: you're 3P again.",
                                                 "Welcome back: you're 4P again."};
            K.welcomed = 1;
            portCoopNotice(kBack[seatOf(ssNetMe()) - 2]);
            ssLog("co-op: welcome back: seat %d again (the connection came back)", seatOf(ssNetMe()));
        }
        if (K.lostAt != 0) {
            ssLog("co-op: the host's game is back after %llu ms: the wait is over", msSince(K.lostAt, now));
        }
        K.lostAt = 0;
        K.lostAway = 0;
    } else if (C.was && K.lostAt == 0 && !K.bye && !K.kicked) { /* (told why already: no wait) */
        K.lostAt = now | 1;
        K.lostAway = ssPlayerAway(1);
        coopRemember();
        if (K.lostAway) {
            ssLog("co-op: the host stepped away: waiting %llu seconds for them", hostGraceMs(1) / 1000ull);
        } else {
            ssLog("co-op: the host's game is gone: waiting %llu seconds for it", hostGraceMs(0) / 1000ull);
        }
    }
    if (C.was && K.lostAt != 0 && !K.bye && !K.kicked) {
        const unsigned long long grace = hostGraceMs(K.lostAway |= ssPlayerAway(1));
        const unsigned long long waited = msSince(K.lostAt, now);
        if (waited > 3000 && waited < grace) {
            /* (reconnecting: a room's own relay connection down, the host maybe still there. A guest by address
               has one connection, to the host: down when the host's game is gone too, so it can't tell, and the
               host's the one it waits for) */
            portOnlineWait(ssNetRoom() && ssNetSelfDown() ? PORT_WAIT_RECONNECTING
                                           : K.lostAway ? PORT_WAIT_HOST_AWAY : PORT_WAIT_HOST_GONE,
                           portStateSyncPlayerName(1), (int)((grace - waited + 999) / 1000));
        }
    }
    {
        const int noService = !strcmp(ssNetNote(), PORT_SYNC_NO_SERVICE);
        if (K.kicked) {
            why = PORT_END_REMOVED;
        } else if (K.bye) {
            why = PORT_END_HOST_LEFT;
        } else if (C.was && K.lostAt != 0 && (noService || msSince(K.lostAt, now) > hostGraceMs(K.lostAway))) {
            why = noService || (ssNetRoom() && strstr(ssNetNote(), "relay") != NULL) ? PORT_END_SERVICE
                                                                                   : PORT_END_HOST_LEFT;
        }
    }
    if (why == 0) {
        return 0;
    }
    snprintf(host, sizeof(host), "%s", portStateSyncPlayerName(1));
    if (!C.was) {
        /* (not in the host's world yet: this game's own goes on, told why) */
        portCoopNotice(why == PORT_END_REMOVED ? "The host removed you from the room." : "The host left the room.");
        portStateSyncLeave();
        return 1;
    }
    coopRemember();
    portOnlineEnd(why, host, K.ownSlot);
    portStateSyncLeave();
    return 1;
}

int portStateSyncCopyHeld(void) {
    return portStateSyncCoopGuest() || (G.on && C.was && !ssNetIsHost()) ||
           (portOnlineEndUp() && portOnlineEndWhy() != PORT_END_NO_SERVICE) || sHostWorld;
}

/* camdrv.c's cutscene bars and statuswindow.c's HUD: a scene on this screen now. A co-op guest's (or a
   watcher's) own Mario has no keys for the whole session, so for them it's the host's Mario that tells: the
   host in a scene or a talk (their keys off), or this game pulled into one; else the screen is a normal
   game's, filling it at this player's own aspect, with the HUD (the shared party's HP, FP, coins and Star
   Points: the host's progress is this game's) */
int portStateSyncSceneShown(void) {
    /* (a co-op guest waiting for the host's game to come back, its keys still off: the same; it was the bars
       and no HUD for the whole wait, over a room in the field) */
    const int waiting = G.on && C.was && !ssNetIsHost() && !G.watch && !ssNetLinked(1);
    if (G.on && (portStateSyncCoopGuest() || G.watch || waiting) && !portOnlineEndUp()) {
        const SsPeer* h = ssPeerOf(1);
        if (!ssNetLinked(1)) {
            return ssScenePulled(); /* (no word from the host: what it last said may be long over) */
        }
        return ssScenePulled() || (h != NULL && h->valid && h->keysOff);
    }
    return !marioChkKey() || !marioChkCtrl();
}
