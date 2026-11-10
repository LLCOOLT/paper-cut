/* Playing together (docs/PLAYERS.md, docs/STATE_SYNC.md): one Multiplayer page
 * for the couch and online alike (the Start menu's Multiplayer tab, the title
 * menu's Multiplayer button; an invite link starts the game on it).
 *
 * - The adventure needs no button: co-op ("second_player") is on by default and
 *   a controller's Start joins; online, guests wait in the room until the host
 *   is in a file (or picks a party game), then come into the host's adventure. Player 1 is Mario, player 2 the partner (Luigi
 *   with Brothers mode on), players 3 and 4 companions.
 * - Party: a picture menu the host picks from; everyone goes straight in.
 * - Who's who: who sits where; swaps take effect at the next calm moment.
 * - Online: hosting a room, joining by code, the room list or by address, and
 *   invites. Friends take the next seats, as a controller on this computer does.
 *
 * Seats are the same for both: seat 1 Mario, 2 the partner (or Luigi), 3 and 4
 * companions. On one computer a seat is a controller port (seat n: port n - 1);
 * online it is a player of the room (port/include/port/statesync.h). */
#ifndef PORT_TOGETHER_H
#define PORT_TOGETHER_H

#include <stddef.h>
#include "port/statesync.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The co-op engine's lobby calls (port/include/port/statesync.h declares them with
   PORT_STATESYNC_LOBBY; together_pc.c stands in until then). The same prototypes here. */
void portStateSyncPickSet(int pick, int a, int b);
int portStateSyncPick(int* a, int* b, unsigned* version);
int portStateSyncSeatPlayer(int seat);
void portStateSyncSeatAsk(int seat);
int portStateSyncSeatAsked(int* fromPlayer, int* seat);
void portStateSyncSeatAnswer(int yes);
void portStateSyncSeatSet(int seat, int player);
int portStateSyncSeatPending(void);
unsigned portStateSyncTeamBonusCount(void);
int portStateSyncCoopOn(void);
int portStateSyncCoopPart(int player);
int portStateSyncMe(void);
int portStateSyncCoopGuest(void); /* this game follows the host's world as a co-op guest now */
/* The guest's copy of the host's adventure (the engine's coop_save_pc.c): Pending 1 when one wasn't taken
   yet, with the host's name (UTF-8) and its slot image (PORT_CARD_SLOT_SIZE bytes; image may be NULL);
   Done: taken, or declined for good. Offered by the launcher's Play tab and the game's file menu
   (port/src/common/coop_copy_pc.c), into a file of the player's choice, never over one without asking. */
int portCoopCopyPending(char* hostName, size_t cap, unsigned char* image);
void portCoopCopyDone(void);

/* the host's pick (sent to every guest: portStateSyncPickSet) */
enum {
    PORT_PICK_NONE = 0,
    PORT_PICK_TOGETHER = 1, /* the adventure: picked by itself once a host is in a file (no button) */
    PORT_PICK_VERSUS = 10,  /* the party games, in the picture menu's order */
    PORT_PICK_RAID,
    PORT_PICK_BOSS_RUSH,
    PORT_PICK_QUIZ,
    PORT_PICK_CARDS,
    PORT_PICK_BINGO,
    PORT_PICK_DM, /* one player commands the enemies (a boss rush with a dungeon master) */
    PORT_PICK_END
};
#define PORT_PARTY_GAMES (PORT_PICK_END - PORT_PICK_VERSUS)

/* a pick's name and its one-line note (English: portTrGame them); its picture (an item's id) */
const char* portTogetherPickName(int pick);
const char* portTogetherPickNote(int pick);
int portTogetherPickItem(int pick);
/* this game may pick (on one computer, or the host online; a guest follows the host's) */
int portTogetherCanPick(void);
/* the pick made here (and online sent to the guests); arg: the dungeon master's seat (2..4).
   0 and why (English) when it can't now. It starts once the menu is closed and Mario is free. */
int portTogetherChoose(int pick, int arg, const char** why);
/* the host's last pick (a guest's view too), PORT_PICK_NONE before any */
int portTogetherPicked(void);
/* each game frame (marioStMain) and each title frame: a pick waiting for the field started, a guest
   following the host's pick, seat swaps at the calm moment */
void portTogetherFrame(void);
void portTogetherInit(void); /* once, with the title menu's (its frame hook) */
extern int gPortTogetherJoinOpen; /* the Multiplayer page is up: a controller's A takes the next seat */

/* --- seats --- */
#define PORT_SEATS 4
/* (statesync.h names its own PORT_COOP_PART_*, same values, plus watching) */
enum { PORT_PART_NONE, PORT_PART_MARIO, PORT_PART_PARTNER, PORT_PART_LUIGI, PORT_PART_COMPANION };
int portTogetherSeatPart(int seat); /* seat 1..4: what it plays (PORT_PART_*) */
/* who sits there, in the game's encoding (a controller's or a player's name), ""; 1 someone, 0 open */
int portTogetherSeatWho(int seat, char* out, size_t size);
/* asks for that seat (online: its holder is asked; on one computer, or the host: at once).
   with: the seat to swap with (this player's own online, 0). 0 and why when it can't. */
int portTogetherSeatSwap(int seat, int with, const char** why);
/* a player's own ask (players 2-4's menus): as portTogetherSeatSwap, but on one computer a swap with
   Mario's seat waits for player 1's yes (their card in the field, portTogetherSeatAsked): 2 then */
int portTogetherSeatRequest(int seat, int from, const char** why);
/* a swap another player asks of this one: 1 and who, and for which seat */
int portTogetherSeatAsked(char* who, size_t size, int* seat);
void portTogetherSeatAnswer(int yes);
int portTogetherSeatWaiting(void); /* a swap agreed, waiting for the calm moment */
/* tests (an input script's "together PICK" or "together seat S W", input_pc.c; Aurora): as the page's own
   button at that step of the script ($TTYD_TOGETHER_PICK / _SEAT at a frame): 1, or 0 and why */
int portTogetherTest(const char* what, const char** why);
int portTogetherSeatLocked(int seat); /* 1: the seat can't change hands here (online, Mario's) */

/* --- the team bonus (the setting "team_bonus"; on with two players or more) --- */
int portTeamBonusOn(void);
/* btlevtcmd_ResultAC, an action command's outcome known (BattleWork): on one computer, every player
   of a turn landing theirs gives a little Star Power (online the host's world decides) */
void portTeamBonusAc(void* battleWork);
unsigned portTeamBonusShown(void); /* team bonuses given (local and online), for the pop-up */
void portPopupTeamBonus(void);     /* battle_popup_pc.c: the "Team bonus!" over Mario */

/* --- the title menu (title_menu_aurora.c) --- */
/* from the Multiplayer page over the title: 1 the file select (the adventure, bingo), 2 the
   title's no-save file (a party game; a guest's adventure, taken over by the host's world) */
void portTitleGo(int where);
/* the title opens on the Multiplayer page (the launcher's tab, a launch into a room) */
void portTitleOpenMultiplayer(void);
int portTitleOnPages(void); /* the title's pages are up (no file) */

/* --- starting a file from outside the file menu (the launcher) --- */
/* a new file in slot (0-3) at the next file menu, its name entry skipped; rogueport: Start in Rogueport.
   seq_load.c takes it once (1 and the two), the title goes to the file menu while one waits */
void portBootRequestNew(int slot, int rogueport);
int portBootTakeNew(int* slot, int* rogueport);
int portBootNewPending(void);
/* an online guest's way into the host's pick, kept until taken (portTitleGo's where: 1 the files, 2 the
   no-save file the host's world takes over; 0 none): the host's pick comes once, at the link, wherever this
   game is then (the logos, the file menu, the title, the field). The title takes it; the logos go on by
   themselves and the file menu goes back to the title for it (seq_logo.c, seq_load.c); one in a game
   already is brought along by the co-op engine */
void portTogetherGuestOwedSet(int where);
int portTogetherGuestOwed(void);
/* the title going to the file select (its Play, a pick from the page): tests' $TTYD_FILES_NEW put in */
void portTogetherFilesTest(void);

/* --- the guest's co-op save copy offer (port/src/common/coop_copy_pc.c) --- */
/* the game's file menu (seq_load.c), its main state each frame: 1 while the offer is up (the menu takes no
   input meanwhile; trg and dir: the frame's presses) */
int portCoopCopyOffer(unsigned trg, unsigned dir);
int portCoopCopyOfferUp(void);
/* "Not now" (the launcher's or the file menu's): not asked again until the next launch */
void portCoopCopyLater(void);
int portCoopCopyLaterAsked(void);
/* save_files.cpp (the launcher, the card not in use): the image put into the card's file (0-3) as a file
   of its own (a new id, the source's own settings); the save there before kept in Save files. 0 and why
   (English) when it can't */
int portSaveFilesPutImage(const unsigned char* image, int file, char* why, int cap);

/* bingo.cpp: the lockout card in the online room (its code), under this player's name, on seed */
void portBingoJoinOnline(const char* room, const char* name, const char* seed);

#ifdef __cplusplus
}
#endif

#endif
