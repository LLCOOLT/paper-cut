/* Online play, "sync what matters" (docs/STATE_SYNC.md). Each player runs
 * their own game and the games exchange chosen state: where each player is, the
 * story, the pouch, the room's enemies and objects. Nothing must stay identical,
 * nothing restarts, and the players can be in different rooms. Up to four, by
 * address (the host the hub: guests' messages pass through it) or in a relay
 * room. A battle the players in a room share (up to four) runs in every game,
 * the one who started it deciding and each player choosing on their own screen.
 *
 * Players start it from the Start menu's Online tab (pause_online_aurora.c):
 * Host a room / Join by code (the relay's rooms) or by address; or from the
 * launcher's Online tab, before the game (portStateSyncLaunch), or an invite
 * link. Tests start it from the environment, with one of:
 *   TTYD_SYNC_HOST=<port>           listen there (player 1; up to three guests)
 *   TTYD_SYNC_JOIN=<address>:<port> join (players 2-4): [::1]:47800, 127.0.0.1:47800
 *   TTYD_SYNC_HOST_ROOM=<code|new>  host a room on the relay (two players)
 *   TTYD_SYNC_JOIN_ROOM=<code>      join it
 * Optional: TTYD_SYNC_AT=<frames> connects only after that many game frames
 * (mid-game joins in tests), TTYD_SYNC_LOG=<n> a status line every n frames
 * (default 60), TTYD_NET_FAULTS (port/online_faults.h). */
#ifndef PORT_STATESYNC_H
#define PORT_STATESYNC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Playing together (co-op, statesync_coop_pc.inc; docs/STATE_SYNC.md "Playing together"): every linked
 * story session is one adventure in the host's world. The host's game is the world: each guest's game
 * follows it (the same room, Mario where the host's is, the host's story, items and NPCs; the guest's
 * own save is never written), and each guest plays a character of the host's party from their own
 * controller, which goes to the host's game as that player's (players 2-4 there, as on one computer):
 * seat 2 the partner (Luigi with Brothers mode on), seats 3 and 4 the follower or a companion. In a
 * shared battle each plays that character's turns. Watchers watch; the party modes (versus, the raid,
 * the quiz, card duels) run as before. */
#define PORT_STATESYNC_LOBBY 1
int portStateSyncCoopOn(void); /* 1: linked in a co-op story session now (no party mode running) */
/* a player's part (player 1..4): 0 none or not here, 1 Mario, 2 the partner, 3 Luigi (Brothers mode),
   4 a follower or companion, 5 watching */
enum { PORT_COOP_PART_NONE, PORT_COOP_PART_MARIO, PORT_COOP_PART_PARTNER, PORT_COOP_PART_LUIGI, PORT_COOP_PART_COMPANION, PORT_COOP_PART_WATCH };
int portStateSyncCoopPart(int player);
int portStateSyncMe(void); /* this game's player number (1 the host, 2-4 a guest; 0 before the host says) */
/* this game follows the host's world as a co-op guest now (its own Mario hidden, its keys off) */
int portStateSyncCoopGuest(void);
/* the host's game, with co-op guests playing: their controllers are players 2-4's (second_player_pc.c) */
int portStateSyncCoopHost(void);
/* the host's game: seat s's (2-4) player has a controller here now (an online guest's) */
int portStateSyncCoopPadHere(int seat);
/* the host's game: seat s (2-4; 0 any of them) is a linked guest's, its controller fresh or not (a guest a
   step behind, loading a room or held in one long frame, still plays: only the link's end frees the seat,
   so no "left" and nothing of theirs taken away meanwhile) */
int portStateSyncCoopSeatTaken(int seat);
/* A co-op guest playing Mario (seat 1): its Start is Mario's Start menu, the big one. MenuOk (the guest's
   game, win_main.c): it may open now (the host's Mario is free). MenuHeld (the host's game): the host's world
   stands still while that menu is up ("Paused by <guest>" on every screen). ItemUsed and PartnerPicked (the
   guest's menu, win_main.c): the item's HP, FP and SP and the partner switch done in the host's game too,
   whose pouch and party are the shared ones (the item itself leaves through the pouch's own changes). */
int portStateSyncCoopMarioMenuOk(void);
int portStateSyncCoopMarioMenuHeld(void);
void portStateSyncCoopItemUsed(int item, int target);
void portStateSyncCoopPartnerPicked(int partner);
/* tests (input_pc.c's wait_peer): another game in the session shows what: "card" (its player's card open),
   "paused" (the host's world held by a Start menu); "!" before either: not */
int portStateSyncPeerShows(const char* what);
/* The host's pick for everyone (the Online screen's lobby): pick 1 the adventure, 10-16 the party games;
   a, b its arguments. Set: the host only (to every guest, and to a guest joining later). Get: 0 none,
   *version +1 per new pick. */
void portStateSyncPickSet(int pick, int a, int b);
int portStateSyncPick(int* a, int* b, unsigned* version);
/* Seats: 1 Mario (the host's, or a guest's who took it: their controller is then the host's game's
   controller 1, and the host plays the seat the guest had), 2 the partner (Luigi with Brothers mode on), 3 and 4 a companion
   each. SeatPlayer: who sits there (0 nobody). Ask: this player asks for that seat (its holder is asked;
   an empty one is taken); Asked: 1 while a request waits for this player's answer; Answer: yes or no;
   Set: the host, at once; Pending: 1 while an agreed change waits for a calm moment (the field, no
   scene or battle). */
int portStateSyncSeatPlayer(int seat);
void portStateSyncSeatAsk(int seat);
int portStateSyncSeatAsked(int* fromPlayer, int* seat);
void portStateSyncSeatAnswer(int yes);
void portStateSyncSeatSet(int seat, int player);
int portStateSyncSeatPending(void);
/* a player's seat (1-4) while playing together, 0 none (a shared battle's characters go by it) */
int portStateSyncSeatOf(int player);
/* a co-op guest: seat (2-4)'s character in the host's game (port/player_menu.h's portCoopCharacterOf there:
   -1 Luigi, 0 nobody, else a party member) and its Paper Mario 64 look (pm64, may be NULL);
   PORT_STATESYNC_NO_CHARACTER when this game isn't a co-op guest or the host's haven't come yet */
#define PORT_STATESYNC_NO_CHARACTER (-100)
int portStateSyncSeatCharacter(int seat, int* pm64);
/* a co-op guest: the Paper Mario 64 look the host's game has on TTYD partner party (1-7) in *p (0 its own
   look), 1 if known (else this game's own, 0) */
int portStateSyncCoopHostWorn(int party, int* p);
/* Team bonus (the host's setting team_bonus, on unless it's 0): in a shared battle, a turn of the players'
   side where everyone who acted landed their action command Nice or better gives a quarter bar of Star
   Power, in the host's world (every game takes it from the battle's own snapshot). +1 each time one was
   given, in every game (the "Team bonus!" pop-up). */
unsigned portStateSyncTeamBonusCount(void);
/* The guest's copy of the host's adventure (coop_save_pc.c): kept in <user>/coop/ as the host's game
   saves, changes rooms and every few minutes, written whole each time (a crash never leaves half a copy).
   Pending: 1 when a copy wasn't taken yet (never while playing together), with the host's name and its
   slot image (PORT_CARD_SLOT_SIZE bytes, a GameCube image; image may be NULL); Done: it's taken (or
   declined for good). Write: a guest's game, the copy that came. */
int portCoopCopyPending(char* hostName, size_t cap, uint8_t* image);
void portCoopCopyDone(void);
int portCoopCopyWrite(const char* hostName, const char* room, const uint8_t* image, size_t n);
/* every copy kept (the Save files page's Co-op copies), newest first: its file's name in <user>/coop, the
   host's name and the room as the file's name has them (made file-safe), when it was last written (seconds
   since 1970), taken (put into a file already, or declined). Read: its slot image (a name from the list);
   MarkTaken: not offered again; Delete: gone for good. */
typedef struct PortCoopCopyInfo {
    char file[128];
    char host[48];
    char room[24];
    long long mtime;
    int taken;
} PortCoopCopyInfo;
int portCoopCopyList(PortCoopCopyInfo* out, int max);
int portCoopCopyRead(const char* file, uint8_t* image);
void portCoopCopyMarkTaken(const char* file);
int portCoopCopyDelete(const char* file);
/* The copy's name without ".sav" (<host>_<room>): the guest's achievements are kept under it
   (achievements_pc.c); and the name of the copy offered now (0: none). */
void portCoopCopyKey(const char* hostName, const char* room, char* out, size_t cap);
int portCoopCopyPendingKey(char* out, size_t cap);
/* Hardcore and leaderboard runs are single-player: such a file neither hosts nor joins (why, English) */
const char* portStateSyncSoloOnly(void);

/* ---- the room's people (online_end_pc.c, pause_online_aurora.c; docs/STATE_SYNC.md "Leaving and coming
   back") ----
   Platform: a player's computer (PORT_SYNC_OS_*, 0 not known); Quality: their link (the host's to a guest,
   a guest's to the host: 0 not known, 1 good, 2 fair, 3 poor, from the round trip). Remove: the host only:
   that player is told and goes back to their own adventure; their game can't come in again this session. */
enum { PORT_SYNC_OS_NONE, PORT_SYNC_OS_WINDOWS, PORT_SYNC_OS_MAC, PORT_SYNC_OS_LINUX, PORT_SYNC_OS_IOS,
       PORT_SYNC_OS_ANDROID, PORT_SYNC_OS_WEB };
int portStateSyncPlayerPlatform(int player);
int portStateSyncPlayerQuality(int player);
int portStateSyncPlayerRtt(int player); /* ms, -1 not known */
int portStateSyncRemove(int player);    /* 1 told */
/* the rule settings the host plays this session on that differ from this player's own (a guest's; display,
   sound and controls never): how many; At: the i-th's key, this player's own value and the host's */
int portStateSyncRuleDiffs(void);
const char* portStateSyncRuleDiffAt(int i, int* own, int* host);
/* a co-op guest's game may not keep the world it has as a save of its own now (quick resume, autosave):
   it's the host's (playing together, or just after, until the player chose where to go) */
int portStateSyncCopyHeld(void);
/* camdrv.c's cutscene bars and statuswindow.c's HUD: 1 when this screen shows a scene now (Mario's keys
   off; a co-op guest's or a watcher's: the host's, as theirs are off all session) */
int portStateSyncSceneShown(void);
/* Try again after a failure (the last Host or Join, as it was asked); 0 none to try */
int portStateSyncRetry(void);
/* the online service's failure (the relay can't be reached, or dropped for everyone), English */
#define PORT_SYNC_NO_SERVICE "Can't reach the online service. Try again, or play on one computer."

/* The game's side (statesync_pc.c) */
int portStateSyncOn(void);       /* the mode is on in this game (it may not be connected) */
int portStateSyncLinked(void);   /* connected to the other player now */
void portStateSyncGameFrame(void); /* each game frame, from makeKey (system.c) */
void portStateSyncFrameEnd(void);  /* each game frame's end, after every system's update (marioStMain) */
/* Playing together, each guest's own character (docs/STATE_SYNC.md "The guest's own character"):
 * a co-op guest's game walks the character its seat plays itself, from its own controller, and the host's
 * game puts it where the guest's says; the world (enemies, doors, talks, scenes) stays the host's. */
struct PartyEntry;
/* the guest: the seat (2-4) whose character this game walks now, its controller as that seat's
   (second_player_pc.c, brothers_pc.c), 0 none (the host holds it, or not in the field) */
int portStateSyncCoopLocalSeat(void);
/* the host: seat s's character placed where its guest's game has it, this frame (1: its walk is done) */
int portStateSyncCoopPlace(int seat, struct PartyEntry* party);
/* the host moved seat s's character itself (a recall, a fall, back at Mario's side): its guest takes the
   host's place for it */
void portStateSyncCoopSeatMoved(int seat, const char* why);
/* second_player_pc.c: helper h's (0 the partner, 1 player 3's, 2 player 4's) player has taken it, and
   set so (a co-op guest following the host's word) */
int portCoopHelperFree(int h);
void portCoopHelperSet(int h, int free);
/* a co-op guest: helper h's character (1, 2: players 3 and 4's) the host's one here (its party member) */
void portCoopGuestHelperAs(int h, int member);
/* brothers_pc.c: the second player walks Luigi now; set so; Luigi busy with something of his own (a
   hammer swing, tucked away) */
int portBrothersDrivenNow(void);
void portBrothersDrivenSet(int driven);
int portBrothersBusy(void);
struct PartyEntry* portBrothersLuigiAny(void); /* his party entry, drawn or hidden */
/* a co-op guest's own Luigi hit a block from below (its name): the host's game works it
   (portBrothersHitByName, brothers_pc.c) */
void portStateSyncCoopLuigiHit(const char* mobjName);
void portBrothersHitByName(const char* name);
/* The guest's field actions (docs/STATE_SYNC.md "The guest's field actions"): a co-op guest walking its own
 * character plays its actions in its own game at once (Luigi's swing, a talk's start, a block's hit, the
 * partner's move starting, a swap, a pick, an item from Mario's Start menu) and tells the host; the host's game
 * does them for real (hits, talks, abilities, swaps, items, the story) and answers; a refusal takes the
 * guest's back. Its A, B, X, L and R (and picks) go no more as the controller while it walks its character. */
enum {
    PORT_COOP_ACT_HAMMER = 1, /* Luigi's swing (Brothers mode): where and which way */
    PORT_COOP_ACT_TALK,       /* a talk (Luigi's A, a helper's B): name the NPC */
    PORT_COOP_ACT_HIT,        /* a helper's B on a block or switch: name it */
    PORT_COOP_ACT_ABILITY,    /* the partner's field move (X down): a the partner */
    PORT_COOP_ACT_ABILITY_UP, /* X let go */
    PORT_COOP_ACT_SWAP,       /* L or R: a the partner (seat 2) or companion form (3, 4) now, 0 the host's next */
    PORT_COOP_ACT_PICK,       /* the Character strip or wheel: a the strip's entry (port/player_menu.h) */
    PORT_COOP_ACT_ITEM,       /* an item from Mario's Start menu (seat 1): the host's answer to it */
    PORT_COOP_ACT_MENU_PARTNER, /* a partner from Mario's Start menu (seat 1): the host's answer */
};
/* the guest: an action of its seat's character, shown here now (at x, y, z facing dir; name: the NPC or block;
   a, b: the kind's numbers; prev: what a refusal puts back, the partner or companion it had). Its number, 0:
   not sent (this game doesn't walk its character now) */
unsigned portStateSyncCoopAct(int kind, const char* name, int a, int prev, float x, float y, float z, float dir);
/* the host: what came of a guest's swing (the block or NPC hit; "" nothing) */
void portStateSyncCoopActResult(int seat, const char* what);
/* second_player_pc.c, the host: helper h's (0-2) guest's actions, done here as its own controller's would be (1:
   done; 0 refused). Swap: member the partner or companion form its game swapped to (0: the next, this game's
   choice). Ability: X down (1) or let go (0) on the partner's field move. */
int portCoopGuestTalk(int h, const char* npc, float x, float y, float z);
int portCoopGuestHit(int h, const char* mobj, float x, float y, float z);
int portCoopGuestSwap(int h, int member);
int portCoopGuestPick(int h, int id);
void portCoopGuestAbility(int down);
/* second_player_pc.c, the guest: a refused action of helper h's taken back (prev: the partner or companion before
   a swap or pick) */
void portCoopGuestActUndo(int h, int kind, int prev);
/* brothers_pc.c: the host, a guest's swing or talk (1 done); the guest, one refused taken back */
int portBrothersGuestSwing(float x, float y, float z, float dir);
int portBrothersGuestTalk(const char* npc, float x, float y, float z);
void portBrothersGuestUndo(int kind);
struct PADStatus;
void portStateSyncApplyPad(struct PADStatus* pads); /* the pad read (DEMOPad.c): a shared battle's pads */
/* a shared battle runs now (each game runs it itself; player 1's game decides, the others follow its
 * steps, and each player's choices come from their own game): co-op's second player is another
 * game's, players 3 and 4 too when they share it (second_player_pc.c) */
int portStateSyncSharedBattle(void);
/* the players in the shared battle under way (2 to 4: controller 1 player 1's, Mario, 2 the
 * partner's, 3 and 4 the companions'), 0 none */
int portStateSyncBattlePlayers(void);
/* this game's player in the shared battle running now (0 to 3, its controller there: this
 * computer's own controller 1 plays it), -1 a watcher or none */
int portStateSyncBattleSelf(void);
/* seq_battle.c before battle_init: 1 while a shared battle waits for the other game to get there */
int portStateSyncBattleGate(void);
/* The shared battle's hooks in the battle's own code (battle_seq.c, battle_event_cmd.c; each returns
 * at once with nothing touched when no shared battle runs). work: the BattleWork.
 * The step gates (btlseqMove; gate 0 the next actor, 1 an action over, 2 a phase event over, 3 a
 * phase begins): Wait is 1 while a follower lacks player 1's step (the sequence stays where it is);
 * Step gives player 1's verdict (player 1's game: its own, sent with its snapshot; a follower's:
 * player 1's, its snapshot taken). */
int portStateSyncBattleWait(void* work, int gate);
int portStateSyncBattleStep(void* work, int gate, int ours);
/* tests (a hang's report): where the shared battle's waits stand, in the log */
void portStateSyncBattleDebug(void);
/* tests (the auto-battle's hang watch): how long this game has waited on another game's word in its shared
   battle, in ms (0: not waiting). A slower game's turn isn't this game's hang: its frames race on meanwhile */
unsigned portStateSyncBattleWaitMs(void);
/* evt_npc_battle_start: 1 when this game already won this NPC's script battle with the other players, here and
   not long ago, as another player's (its own script coming to the same fight later): the script goes on as after
   that win, with no second battle (statesync_battle_pc.c) */
int portStateSyncScriptBattleFought(const void* npc);
/* btlseqAct 0x6000006: 1 while this game waits for the random numbers for the action (from the game of the
 * player whose unit acts, A's for the enemies: that game itself never waits) */
int portStateSyncBattleExec(void* work);
/* evtmgrMain, each event's turn (its unit, EventEntry.unitId): the acting unit's scripts draw from the
 * action's own random numbers in a shared battle, the same in every game, never from those the rest of
 * each game draws from (Begin swaps them in, End back) */
void portStateSyncBattleEvtBegin(int unitId);
void portStateSyncBattleEvtEnd(void);
/* a draw by chance of the acting unit's scripts in a shared battle (btlevtcmd_ChoiceSamplingEnemy,
 * btlevtcmd_DrawLots, btlevtcmd_GetRandomValue, evt_sub_random): the deciding game's (A's for an enemy).
 * Wait, before this game's own draw: 1 the script waits for the decider's; Done, after it: the decider's
 * game sends its draw, another takes the decider's in its place (a target: in targets'
 * BattleWorkWeaponTargets; a value: in [0, max)) */
int portStateSyncBattleDrawWait(void* event, int unitId, int first);
void portStateSyncBattleTargetDone(void* event, int unitId, const void* targets, int* unit, int* part);
void portStateSyncBattleDrawDone(void* event, int unitId, int* value, int max);
/* btlseqAct 0x6000002: -1 this game's player chooses (BattleCommandInput), 0 another player's choice
 * hasn't come, 1 it's taken; Made: this game's player chose (the others take it) */
int portStateSyncBattleCommand(void* work);
void portStateSyncBattleCommandMade(void* work);
/* btlevtcmd_ResultAC (running: the command still going here): 1 the script waits for its player's
 * outcome; btlevtcmd_ResultACDefence: the guard (checked here, or its player's; 1 waits);
 * btlevtcmd_ACRGetResult: the Stylish timing (1 waits) */
int portStateSyncBattleAcResult(void* work, int running);
int portStateSyncBattleGuard(void* event, void* unit, void* weapon, int first);
int portStateSyncBattleStylish(void* event, int first, int* result, int* frame);
/* the dungeon master's picks (dungeon_master_aurora.c): that place's decisions are another game's
 * now; its pick sent (kind 0 the move: the option, its target, all the enemy's own; 1 a target as
 * the script runs: the answer and the target); another game's taken: 1 taken, 0 not yet, 2 never
 * (this game's own stands) */
int portStateSyncBattleRemote(int place);
/* the shared battle's place of a co-op seat's player (-1 when it isn't in the battle; seat - 1
 * outside one); the place that plays character r (0 Mario, 1 the partner, 2-3 the companions) and the
 * character a place plays (a guest in seat 1 plays Mario: Go.roles); themselves outside a shared battle */
int portStateSyncSeatPlace(int seat);
int portStateSyncBattlePlaceOf(int character);
int portStateSyncBattleCharacterOf(int place);
/* a duo move's half (duo_pc.c; half 0 the hero's, 1 the second unit's) in a shared battle: Mine -1 no
 * shared battle (the game's own controllers as ever), -2 another game judges it, else the controller
 * this game reads it from; Put: judged here (the result, the press's frame, the presses mashed), the
 * others take it; Take: 1 taken, 0 not yet, -1 never (a miss) */
int portStateSyncBattleDuoMine(void* unit);
void portStateSyncBattleDuoPut(int half, int result, int at, int count);
int portStateSyncBattleDuoTake(void* unit, int half, int* result, int* at, int* count);
void portStateSyncBattlePut(int kind, int a, int b, int c);
int portStateSyncBattleTake(int kind, int place, int* a, int* b, int* c);
/* the other game was refused (another build of Paper Cut, another disc version, other mods on):
 * why, in English (portTr it); "" when not */
const char* portStateSyncRefused(void);

/* The Start menu's Online tab (pause_online_aurora.c). Strings are English (portTrGame them). */
enum {
    PORT_SYNC_OFF,     /* not on */
    PORT_SYNC_WAITING, /* on: the host waiting for the other, the guest reaching the host */
    PORT_SYNC_LINKED,  /* playing together, each in their own game */
    PORT_SYNC_FAILED,  /* can't go on: portStateSyncNote says why (Leave, then try again) */
};
/* this player hosts: a room on the relay (byAddress 0), or on port (by address); 0 when it can't
 * start (portStateSyncNote) */
int portStateSyncHost(int byAddress, int port);
/* joins: a room code (with or without its dash) or an address (host[:port]) */
int portStateSyncJoin(const char* text);
void portStateSyncLeave(void);
int portStateSyncPhase(void);
const char* portStateSyncCode(void); /* the room's code as shown (K7P2-QX9M), "" when not in a room */
int portStateSyncPort(void);         /* hosting by address: the port, 0 otherwise */
const char* portStateSyncNote(void); /* the last refusal or failure (English), "" none */
/* the host, linked: a versus fight with the other player (the Versus page's set-up; the host's
 * side Mario's, the guest's Luigi's), started in both games; 0 and why (English) when it can't.
 * 1: asked; the host's own fight starts once the other player says yes (dropped, with a notice,
 * on a no or no answer) */
int portStateSyncVersus(const char** why);
/* the host's versus fight asked for and not begun yet: waiting for the other player's yes, then for the
   fight's start (1) */
int portStateSyncVersusPending(void);
/* the host's opponent for it (a player number, 0 none; a guest's: the host), how many it can
   choose from, and the next one (dir -1 or 1; 0: back to the first, the lowest player number): players 3
   and 4 too */
int portStateSyncVersusOpponent(void);
int portStateSyncVersusChoices(void);
void portStateSyncVersusStep(int dir);
/* a scene of the other player's holds this one (layer 4): the notice over the picture */
int portStateSyncWatching(void);
/* watchers: a guest that joins to watch follows the host's game (warped along, its own Mario
 * hidden) and plays shared battles from the players' controllers, never waited for. The choice
 * for the next join (or $TTYD_SYNC_WATCH=1); this game watches now */
void portStateSyncWatchNext(int on);
int portStateSyncWatcher(void);
/* The room list (the relay's: docs/ONLINE_RELAY.md): the next room this game
 * hosts is listed under this name, open to anyone (NULL: not listed; $TTYD_SYNC_LIST=<name> in
 * tests); the version its listing names ("sync-p" and PORT_SYNC_PROTOCOL, below: every game that can
 * play with this one, whatever its build, lists and finds it there; "-h" after it on the headless test
 * build; 0.4.x's rooms, listed under their release, never mix in);
 * the list of these rooms fetched again (port/settings/features/online_prefs.cpp, Aurora: read with
 * port/include/port/online_menu.h's portOnlineMenuList*) */
void portStateSyncListAs(const char* name);
const char* portStateSyncListVersion(void);
void portStateSyncListRefresh(void);
/* the note (portStateSyncNote) says the other game plays another protocol (PORT_SYNC_PROTOCOL, below):
 * 1, with the two games' builds (the other's, this one's: "0.5.0+b8bd3cb92"), shown for information
 * under the note; 0 otherwise */
int portStateSyncRefusedBuilds(const char** theirs, const char** mine);
/* a game that joined a shared battle under way: how many times faster than real time its frames
 * come until it has caught up with the players (speed_aurora.c), 1 otherwise */
int portStateSyncCatchUp(void);
/* evt_msg.c: a script's text box opened (its message's name, or "DIRECT" and the text; the NPC
 * speaking; its window): a scene's, shown to the player pulled into it (layer 4) */
void portStateSyncSceneText(const char* tag, const char* text, const char* speaker, int window);

/* Players' names: each game's PLAYER messages carry its player's (the Online tab's "Your name",
 * port/include/port/online_menu.h; $TTYD_SYNC_NAME in tests). Player 1..4's (this game's own too)
 * as UTF-8, at most SS_NAME - 1 bytes of whole characters, without control characters or the
 * game's and menus' markup (< > { }): another player's is untrusted text. "" when that player
 * hasn't been in this session (one gone quiet keeps theirs: ssPeerOf says who plays now), or has
 * no name. */
#define SS_NAME 32
const char* portStateSyncPlayerName(int player);
/* the same in the game's encoding (Latin-1 on the US and EU releases, ASCII on the JP one), for
 * the game's font; "" as above */
void portStateSyncPlayerNameShown(int player, char* out, size_t size);
/* the Lua mods (port/include/port/lua_mods.h) run fully only when every player playing with this
 * game has the same set (each game's PLAYER messages carry its hash), as 0.4's hello
 * decided: 1 the same (or not online), 0 another's differs (they're paused; HUD-only ones draw) */
int portStateSyncLuaSame(void);
/* fast travel online (port/include/port/fast_travel.h) only when every player agrees: each game's
 * PLAYER messages carry its own setting; 1 every other player playing with this game has it on (or
 * not online; watchers don't count), 0 one hasn't */
int portStateSyncAllTravel(void);

/* Invite links, papercut://join/<CODE> (port/settings/features/invite.cpp hands them in from the
 * system: the arguments, an open-URL event, Android's intent). The link's room code (8 characters,
 * no dash), 1; 0 when the text isn't one. The format is 0.4's, unchanged. */
int portStateSyncInviteCode(const char* url, char* out, size_t size);
/* the link to this game's room (papercut://join/K7P2-QX9M); 0 when it isn't in one */
int portStateSyncInviteLink(char* out, size_t size);
/* The launcher's Online tab: the game about to start goes online once it runs (host: a new room,
 * or by address on port when byAddress; else join code, a room code or an address; watch: join to
 * watch). $TTYD_SYNC_INVITE=<link> (tests) joins the link's room the same way. */
void portStateSyncLaunch(int host, int byAddress, const char* code, int port, int watch);

/* ---- internal, between the statesync files ---- */
/* Every message crosses between different programs (Windows, macOS, Linux, Android and iOS builds of
   the same version): a message struct is fixed-width fields only, never a pointer, a long or a
   bit-field, and its size is checked here at compile time on every compiler that builds the game.
   A size that differs means a layout that differs: the build fails instead of the games disagreeing. */
#define WIRE_SIZE(T, n) _Static_assert(sizeof(T) == (n), #T ": a message's layout must be the same in every build")
/* Which games play together online: the same disc and the same PORT_SYNC_PROTOCOL, whatever their
   builds (two builds of one release a commit apart, a phone's and a computer's). Bump it by one when
   the games would no longer agree: a message's layout or meaning (these files' messages, the battle
   sync's, the progress snapshot), a rule every player's game must apply alike (what is synced, what
   a shared battle or the room decides), or the relays' list (a room code names its relay by its place
   in it: port/include/port/online.h's TTYD_ONLINE_RELAY). The hello carries it, the room list lists by it ("sync-p<N>"),
   and the refusal says which side is older. tools/port/sync_protocol.py (testpass.py lint) fails when
   the messages' layouts change and it doesn't; after the bump, "sync_protocol.py --update" writes its
   record (tests/port/sync_protocol.txt) again. (0: a build from before it, which compared whole build
   strings: always older.) */
#define PORT_SYNC_PROTOCOL 3 /* (2: a co-op guest walks its own character, SS_MSG_OWN, and plays its field actions,
                                SS_R_COOP_ACT; in a shared battle each player's own unit's action starts from their
                                game's random numbers (B_EXEC from its place), each follower beats to A (B_AT sub 1)
                                and A takes an outcome that came too late as a miss, sent marked (Hdr.pad):
                                statesync_battle_pc.c. 3: in a shared battle an action's scripts draw from random
                                numbers of their own, and its draws by chance (an enemy's target, its move) are
                                the deciding game's, sent as it draws them (B_RES R_PICK)) */
/* the connection (statesync_net_pc.c) */
enum {
    SS_MSG_HELLO = 1,  /* never through the faults */
    SS_MSG_ACK = 2,    /* u32 next seq expected */
    SS_MSG_PING = 3,
    SS_MSG_REL = 4,    /* u32 seq, u8 inner type, payload: reliable, in order, once */
    SS_MSG_PLAYER = 10, /* unreliable: the latest wins */
    SS_MSG_NPCS = 11,
    SS_MSG_MOBJS = 12,
    SS_MSG_FWD = 13,    /* the host's: u8 origin player, u8 type, payload: a guest's message for the others */
    SS_MSG_PAD = 14,    /* a co-op guest's: its controller, the last frames (to the host) */
    SS_MSG_OWN = 15,    /* a co-op guest's: its own character as its game walks it (to the host) */
};
/* inner types of reliable messages */
enum {
    SS_R_SNAPSHOT = 1, /* the whole shared progress: merged on a (re)join */
    SS_R_GSWF = 2,     /* (u16 word, u32 set, u32 clear)... */
    SS_R_GSW = 3,      /* (u16 index, u8 value)... */
    SS_R_GSW0 = 4,     /* s32: the story's step (max) */
    SS_R_LSWF = 5,     /* area[4], (u16 word, u32 set, u32 clear)... */
    SS_R_LSW = 6,      /* area[4], (u16 index, u8 value)... */
    SS_R_ITEMS = 7,    /* (u8 list, s16 id, s8 delta)... */
    SS_R_COUNTS = 8,   /* s16 coins, star pieces, shine sprites deltas */
    SS_R_STATS = 9,    /* Mario's level and the partners: max */
    SS_R_DIGEST = 10,  /* hashes of the shared progress, for the logs */
    SS_R_COUNTS_ABS = 11, /* the host's: s16 coins, star pieces, shine sprites as they are (deltas clamped at 0
                             don't add up in every order: the host's word settles them) */
    SS_R_NPC_DEAD = 20,  /* map[16], name[32] */
    SS_R_ITEM_GONE = 21, /* map[16], name[16], s32 id, f32 x,y,z */
    SS_R_MOBJ_GONE = 22, /* map[16], name[16] */
    SS_R_MOBJ_HIT = 23,  /* map[16], name[16], s32 state */
    SS_R_BATTLE = 30,    /* battle together (statesync_battle_pc.c) */
    SS_R_SETTINGS = 31,  /* the host's settings that change play: a shared battle's in both games */
    SS_R_COOP = 35,      /* co-op: seats, the host's pick, the team bonus (statesync_coop_pc.inc) */
    SS_R_COOP_SAVE = 36, /* co-op: the host's save as it stands, for each guest's copy (coop_save_pc.c) */
    SS_R_COOP_ACT = 37,  /* co-op: a guest's field action and the host's answer (statesync_coop_pc.inc) */
    SS_R_SCENE = 40,     /* scenes together */
    SS_R_FWD = 50,       /* the host's: u8 origin player, u8 inner type, payload: a guest's for the others */
    SS_R_MODES = 60,     /* the party modes' (statesync_modes_pc.c): every game's, through the host */
    SS_R_MODES_END = 70,
};
/* the party modes' messages (statesync_modes_pc.c), from player `from` */
void ssModesRel(int from, int type, const uint8_t* p, size_t n);

/* The party games (quiz_aurora.c, cards_aurora.c) with each player in their own game: the host's
 * game plays the show and sends its state; a guest's shows it and sends its player's presses.
 * portStateSyncPartyRole: 0 not linked (the game's own), 1 the host, 2 a guest. game: the party
 * game's own number (the same in every game). */
#define PORT_PARTY_QUIZ 1
#define PORT_PARTY_CARDS 2
int portStateSyncPartyRole(void);
void portStateSyncPartyState(int game, const void* state, size_t n); /* the host: to every guest */
/* a guest: the host's last state (its length, -1 none yet or another game's); *version grows by
 * one each new one */
int portStateSyncPartyStateGet(int game, void* out, size_t cap, unsigned* version);
void portStateSyncPartyPress(int game, unsigned short buttons); /* a guest: its player's presses */
unsigned short portStateSyncPartyPressed(int player);          /* the host: player 2..4's, taken */

/* The raid boss (raid_aurora.c) with each player in their own game: the host starts it, and each
 * guest's game starts the same raid; the boss fight is one shared battle (the host's script's,
 * the guests join it). While a raid runs, the progress isn't shared either way (the lent loadout
 * isn't anyone's): its changes wait, and the game's own is the shadow again after. */
int portStateSyncPlayersNow(void);  /* this game and every other playing with it now (1: not linked) */
/* the same, counting every player still linked, heard from lately or not (one loading a room is still in it:
   for "left", never PlayersNow's) */
int portStateSyncPlayersLinked(void);
int portStateSyncPlayerHere(int player); /* player 1-4 playing in this room now (this game's own too; 0 not linked) */
int portStateSyncOthersAway(void);  /* the others playing with it now not free in this room's field */
void portStateSyncRaidSend(int week, int players);  /* the host: a raid begun here */
/* The Pit of 100 Trials (PORT_TOGETHER_PIT) or the Paper Tower (PORT_TOGETHER_TOWER) together: the
   host starts it here and in every guest's game (the tower on the host's seed); then whoever goes
   on to the next floor, the others follow them there. 0 and why when it can't. */
struct EventEntry;
#define PORT_TOGETHER_PIT 1
#define PORT_TOGETHER_TOWER 2
int portStateSyncTogether(int kind, const char** why);
/* the Pit's floor pipe (rels/jon floor_inc, before it adds one): from this room's own floor, so a
   player following the others never goes two floors at once */
int portEvtPitFloorShared(struct EventEntry* event, int isFirstCall);
int portStateSyncRaidTake(int* week, int* players); /* a guest: the host's start, once (1) */

#define SS_PLAYERS 4 /* the host (player 1) and up to three guests, by address */
#define SS_ALL 0     /* a message to every other player */

int ssNetStart(void);    /* reads the environment; 0: not on */
/* starts it (the menu): host or join, by a room (where: its code; a host's NULL or "new": a new
 * one) or by address (where: the host's, port: a host's own) */
int ssNetBegin(int host, int room, const char* where, int port);
void ssNetStop(void);
int ssNetRoom(void);
const char* ssNetCode(void);
int ssNetPort(void);
const char* ssNetNote(void);
void ssNetPump(unsigned frame);
int ssNetConnected(void);       /* linked to any other player */
int ssNetLinked(int player);    /* linked to that player (a guest: only the host, 1) */
int ssNetMe(void);              /* this game's player number: the host 1, a guest 2..4 (0 before the host says) */
int ssNetIsHost(void);
unsigned long long ssNetMs(void);
/* to one player (a guest reaches only the host, 1) or SS_ALL; unreliable, reliable */
void ssNetSendTo(int player, int type, const void* p, size_t n);
void ssNetSendRelTo(int player, int type, const void* p, size_t n);
void ssNetSend(int type, const void* p, size_t n);    /* unreliable, to all */
void ssNetSendRel(int type, const void* p, size_t n); /* reliable, to all */
/* blocks for up to ms waiting for something to arrive (a shared battle's set-up) */
void ssNetWait(int ms);
void ssNetSleep(int ms); /* the tests' pace */
/* a game ending: pumps until every reliable message is acknowledged (at most ms) */
void ssNetDrain(int ms);
int ssNetRttMs(int player);   /* the last round trip to that player (the host's guests, a guest's host), -1 none */
unsigned long long ssNetOutMs(void); /* when bytes last went onto the wire (the delay log's "out") */
void ssNetPoll(void);     /* the host, as the controllers are read: what came since the frame's pump */
void ssNetFrameEnd(void); /* the frame's end: a room's batch (every 3 frames) */
void ssNetRemove(int player); /* the host: that player's game turned away for the rest of the session */
const char* ssNetSummary(void);
/* the game side's (statesync_pc.c); from: the player it came from (forwarded ones: their origin) */
void ssOnSession(int player, int fresh); /* a session with that player's game (again) */
void ssOnMessage(int from, int type, const uint8_t* p, size_t n);
void ssOnRel(int from, int type, const uint8_t* p, size_t n);
int ssWatchChosen(void); /* the next join watches (the menu's choice or $TTYD_SYNC_WATCH) */

/* the room's enemies and objects (statesync_room_pc.c) */
void ssRoomFrame(unsigned frame, int peerHere, int iOwn);
void ssRoomMessage(int type, const uint8_t* p, size_t n);
void ssRoomRel(int type, const uint8_t* p, size_t n);
void ssRoomReset(void);
void ssRoomStop(void); /* the mode off: the room's NPCs this game's own again */

/* the ghost (statesync_ghost_pc.c) */
typedef struct SsGhostPose {
    float x, y, z, dir;
    char model[24];
    char anim[32];
    int chr;     /* the player's Play as pick (port/luigi.h; 0 Mario) */
    int costume; /* the player's costume (portCostumeCurrent: 0 none) */
    int pm64;    /* the partner's: the Paper Mario 64 partner in its place (port/pm64.h), 0 none */
} SsGhostPose;
/* other: which other player (0..SS_PLAYERS-2, the statesync_pc.c's order); which: 0 the player,
   1 the partner, 2 Brothers mode's Luigi */
void ssGhostDraw(int other, int which, const SsGhostPose* pose);
void ssGhostHide(int other, int which); /* other -1: every one; which -1: all */
/* the other players' ghosts drawn see-through (the setting ghost_see_through: this player's own) */
int portStateSyncGhostSeeThrough(void);
void portStateSyncGhostSetSeeThrough(int on);

/* battles and scenes together (statesync_battle_pc.c) */
void ssBattleFrame(unsigned frame, int peerHere);
void ssBattleRel(int from, int type, const uint8_t* p, size_t n);
int ssBattleApplyPad(struct PADStatus* pads);
int ssBattleBusy(void);
int ssBattleHeld(void);  /* a shared battle or scene asked for, under way or waiting (its player kept) */
void ssBattleStop(void); /* the mode off: a pull let go, a battle's sharing ended */
/* each game frame, on or off line: a battle taken from another game ends in the field (its script's
   field part, its own partner back) even when the session ended meanwhile */
void ssBattleFieldFrame(void);
/* that field part forgotten: it belonged to a file's field no longer there (a file loaded, the title) */
void ssBattleFieldForget(const char* why);
/* a session with that player (again): a new one with the battle's player ends a shared battle */
void ssBattleSession(int player, int fresh);
int ssScenePulled(void); /* held in the other player's scene (layer 4) */ /* the progress diff paused (a shared battle's both games alike) */

/* what another player's game says now (statesync_pc.c); ssPeer: the one a shared battle or scene
   is with (a guest's: the host; the host's: a guest in its room) */
typedef struct SsPeer {
    int valid;
    char map[16];
    int seq;        /* their seqGetSeq() */
    int inBattle;
    int keysOff;    /* their Mario's keys are off (an event, a scene) */
    int paused;     /* their Start menu is up (1-99: its pages; the host's pauses the world) */
    unsigned char card[8]; /* their player's card (port/player_menu.h portPlayerMenuCard) */
    int owns;       /* they own their room's objects */
    unsigned roomFrames; /* frames they've been in that room */
    int watch;      /* they watch (portStateSyncWatcher) */
    uint32_t battleId; /* the shared battle they're player 1 of, 0 none */
    int modeFloor;     /* the Pit's (1000 + floor) or the Paper Tower's (2000 + floor) floor they're on, 0 */
    int coop;          /* a co-op guest: it follows the host's world (statesync_coop_pc.inc) */
} SsPeer;
int portStateSyncSaveHeld(void); /* cardmgr.c: 1 when this game mustn't save now (a co-op guest's) */
/* the host's world is gone from memory (a file loaded, the title): saves are the player's own again */
void portStateSyncHostWorldGone(const char* why);
/* a file loaded during a session (save_files.cpp): a session rule's value from the file kept as the player's
   own for after the session; 1 when the key is a held rule */
int portStateSyncRuleSetOwn(const char* key, int value);
/* a session rule held now (save_files.cpp's per-file settings): 1 with the player's own value in *own, 2 one
   the player never had a value of, 0 not held */
int portStateSyncRuleOwn(const char* key, int* own);
/* the session over but the host's world still in memory (save states, rewind refused) */
int portStateSyncHostWorld(void);
void ssCoopBonus(void);          /* statesync_battle_pc.c: the host gave a team bonus */
uint32_t ssBattleSharedId(void); /* this game's shared battle as its player 1 (under way), 0 none */
const SsPeer* ssPeer(void);
int ssBattlePeer(void);   /* that player's number */
int ssBattleLinked(void); /* linked to it */
const SsPeer* ssPeerOf(int player); /* any other player's (not playing with this game now: !valid) */
int ssPlayerHere(int player);       /* that player in this game's room, in the field */
/* another player in this room nearer (on the ground's plane) to x, z than this game's Mario: 1 and
   where they are now */
int ssNearestOther(float x, float y, float z, float* ox, float* oy, float* oz);
/* npcdrv.c (an NPC's find checks) and evtmgr.c (an NPC's events): in a room this game owns with other
   players in it, an enemy sees Mario where the nearest player is, put back right after */
struct NpcEntry;
void portStateSyncNpcTargetBegin(struct NpcEntry* npc);
/* 1: this field NPC is held still here to follow the room owner's (its touch still starts a battle) */
int portStateSyncNpcFollowed(const struct NpcEntry* npc);
void portStateSyncNpcTargetEnd(void);
int portStateSyncNpcTargetSwapped(void); /* 1 between a Begin that moved Mario and its End */
/* a shared battle's message to the players in mask (bit p - 1: player p): the host's straight to
   each, a guest's through the host (which takes its own and passes the others on) */
void ssBattleSendTo(unsigned mask, int type, const void* p, size_t n);
/* second_player_pc.c: a shared battle's players 3 and 4's companions (a partner's follower form;
   players 0: none, the field's own again); the companion for a player who had partner `wanted`
   out (A's party's partner `partner` and the other companion `taken` not it); the player (0 to 3)
   who plays a battle's partner unit */
void portCoopBattleCompanions(int players, const int* forms);
int portCoopBattleCompanion(int wanted, int partner, int taken);
int portSecondPlayerPartnerOwner(const void* unit);
void ssProgressRebase(void); /* this game's progress as it is now is the shadow's: nothing of it sent */
int ssSameRoom(void);
void ssLog(const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
