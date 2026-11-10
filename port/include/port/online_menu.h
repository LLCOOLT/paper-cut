#ifndef PORT_ONLINE_MENU_H
#define PORT_ONLINE_MENU_H
/* Online play's preferences and the relay's room list, for the Start menu's
 * Online tab (port/src/aurora/pause_online_aurora.c) and the launcher's
 * (port/settings/features/online_prefs.cpp; online play itself is
 * port/include/port/statesync.h's).
 *
 * Strings are English (portTrGame them), names as they are. */
#ifdef __cplusplus
extern "C" {
#endif

/* a relay for rooms in this build */
int portOnlineMenuHasRelay(void);
/* a room code on the clipboard (as typed: K7P2QX9M), 0 when there's none */
int portOnlineMenuPaste(char* out, int size);

/* the options (saved at once) */
const char* portOnlineMenuName(void);
void portOnlineMenuSetName(const char* name);
int portOnlineMenuPort(void);
void portOnlineMenuSetPort(int port);
int portOnlineMenuListMine(void); /* "List my room": rooms this player hosts are in the room list */
void portOnlineMenuSetListMine(int on);
/* what was typed last to join (shown filled in) */
const char* portOnlineMenuLastTyped(void);

/* The room list: fetched from every relay when asked (portStateSyncListRefresh: on open, and
 * Refresh), never polled. */
enum { PORT_ROOM_LIST_NONE, PORT_ROOM_LIST_LOADING, PORT_ROOM_LIST_READY, PORT_ROOM_LIST_OLD_RELAY,
       PORT_ROOM_LIST_FAILED };
int portOnlineMenuListState(void);
int portOnlineMenuListOnline(void); /* players online, -1 not known */
int portOnlineMenuListCount(void);
/* room i: its code (as typed), its host's name, how many are in it; 0 past the last */
int portOnlineMenuListGet(int i, char* code, int codeSize, char* name, int nameSize, int* players);
/* room i's places (2 to 4) */
int portOnlineMenuListMax(int i);

#ifdef __cplusplus
}
#endif

#endif
