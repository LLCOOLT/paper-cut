#ifndef PORT_ONLINE_MENU_H
#define PORT_ONLINE_MENU_H
/* Online co-op from the game itself: the Start menu's Online tab
 * (port/src/aurora/pause_online_aurora.c), on the launcher's lobby's own
 * functions (port/settings/features/online.cpp, port/include/port/online.h).
 *
 * A session begins with the game (the two games must play out the same from
 * their first frame), so a game already running can't simply go online. From
 * the Start menu the lobby runs while each plays on alone: the host's room
 * opens (its code to read out), the guest joins it, each sees the other's name
 * and the round trip. When the host starts, both games start again at once,
 * as online co-op's recovery does (portOnlineRestartRejoin): the host hosting
 * the same room (from the title, or from where it is now, through quick
 * resume's save), the guest joining it, the session beginning as from the
 * launcher. A host in the launcher works with a guest in the game too (the
 * guest says it starts again, PORT_ONLINE_BYE_REJOIN, and the host starts by
 * itself once it's back), and the other way round.
 *
 * During a session the tab shows the same rows in every game of it (it runs
 * in lockstep like the rest of the menu): what it shows differs (names, the
 * round trip), what it does doesn't.
 *
 * Strings are English (portTrGame them), names as they are. */
#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_ONLINE_MENU_OFF,       /* not online */
    PORT_ONLINE_MENU_OPENING,   /* host: the room (or port) opening */
    PORT_ONLINE_MENU_HOSTING,   /* host: waiting for the other player */
    PORT_ONLINE_MENU_PEER_HERE, /* host: they're here, start when ready */
    PORT_ONLINE_MENU_JOINING,   /* guest or watcher: reaching the room */
    PORT_ONLINE_MENU_JOINED,    /* guest: in, waiting for the host to start */
    PORT_ONLINE_MENU_WATCH_WAIT,/* watcher: in, waiting for the players */
    PORT_ONLINE_MENU_STARTING,  /* the games start again for the session */
    PORT_ONLINE_MENU_PLAYING,   /* a session: playing together */
    PORT_ONLINE_MENU_WATCHING,  /* a session: watching */
    PORT_ONLINE_MENU_FAILED,    /* portOnlineMenuReason says why */
};
int portOnlineMenuPhase(void);
/* the last failure (English), or "" */
const char* portOnlineMenuReason(void);
/* the room's code as shown (K7P2-QX9M), "" when not in a room; the address
 * to give (by address), "" otherwise */
const char* portOnlineMenuCode(void);
const char* portOnlineMenuAddress(void);
/* the other player's name ("" not known), the round trip (ms, -1 not
 * measured), how it goes ("relay", "direct (UDP)", "direct"), watchers */
const char* portOnlineMenuPeer(void);
int portOnlineMenuPing(void);
const char* portOnlineMenuLink(void);
int portOnlineMenuWatchers(void);
/* three or four players: the seats here (host, in the lobby), the session's players */
int portOnlineMenuSeats(void);
int portOnlineMenuPlayers(void);
/* this game can go online from here (it can start itself again: not on
 * phones and tablets, where online play starts from the launcher), and a
 * relay for rooms */
int portOnlineMenuCan(void);
int portOnlineMenuHasRelay(void);

/* the actions (each from the menu's A) */
void portOnlineMenuHost(int byAddress);
/* text: a room code (with or without its dash) or an address (host[:port]);
 * watch: 1 only to watch, 2 to play as player 3 or 4 (a seat), 0 the partner */
void portOnlineMenuJoin(const char* text, int watch);
void portOnlineMenuStart(void); /* host, once the other is here */
void portOnlineMenuLeave(void); /* the lobby left, or the session ended (for every game of it) */
/* the code (link 0) or the invite link (1) to the clipboard: 0 when there's none */
int portOnlineMenuCopy(int link);
/* a room code on the clipboard (as typed: K7P2QX9M), 0 when there's none */
int portOnlineMenuPaste(char* out, int size);

/* the options (saved at once) */
const char* portOnlineMenuName(void);
void portOnlineMenuSetName(const char* name);
int portOnlineMenuPort(void);
void portOnlineMenuSetPort(int port);
int portOnlineMenuStartMode(void); /* 0 from the title, 1 where the host is now */
void portOnlineMenuSetStartMode(int mode);
/* host: a versus room (every game opens on the versus set-up page, port/include/port/versus.h) */
int portOnlineMenuVersusRoom(void);
void portOnlineMenuSetVersusRoom(int on);
int portOnlineMenuListMine(void); /* "List my room": rooms this player hosts are in the room list */
void portOnlineMenuSetListMine(int on);
/* what was typed last to join (shown filled in) */
const char* portOnlineMenuLastTyped(void);

/* The room list: fetched from every relay when asked (on open, and Refresh),
 * never polled. */
enum { PORT_ROOM_LIST_NONE, PORT_ROOM_LIST_LOADING, PORT_ROOM_LIST_READY, PORT_ROOM_LIST_OLD_RELAY,
       PORT_ROOM_LIST_FAILED };
void portOnlineMenuListRefresh(void);
int portOnlineMenuListState(void);
int portOnlineMenuListOnline(void); /* players online, -1 not known */
int portOnlineMenuListCount(void);
/* room i: its code (as typed), its host's name, how many are in it; 0 past the last */
int portOnlineMenuListGet(int i, char* code, int codeSize, char* name, int nameSize, int* players);
/* room i's places (2 to 4; 2 from a relay before three or four players) */
int portOnlineMenuListMax(int i);

#ifdef __cplusplus
}
#endif

#endif
