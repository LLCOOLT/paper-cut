#ifndef PORT_ONLINE_STATUS_H
#define PORT_ONLINE_STATUS_H

/* What an online game tells its player about the connection itself (port/src/common/online_status_pc.c):
 *
 * - Joining, step by step: Reaching the online service (or the host's address), a secure connection, the
 *   same version, the host's adventure, into the host's world. Each step is logged with its time
 *   ("[sync] join: step 2 key done in 120 ms"), shown under the picture while it runs, and has a time of
 *   its own: past it, or when a step fails, a window says which step and what to do (Keep waiting, Copy
 *   diagnostics, Leave the room; Check for updates when the friend's Paper Cut is newer).
 * - The connection's quality: each other player's round trips over the last 10 seconds (the slowest
 *   tenth), and how long nothing has come from them. A small corner icon in the field and battle (bars:
 *   good, fair, poor; the setting "Connection icon": Always, When it's bad (the default), Never),
 *   "Reconnecting..." while the link is down, and "<name>'s connection is unstable." once when a player
 *   stays poor for 10 seconds. Changes are logged ("[sync] quality: player 1 good -> poor ...").
 * - Copy diagnostics: a short text for a report (the build, the protocol, the platform, the path, the
 *   round trips, the join's steps and the last online log lines, with names and the code's secret half
 *   left out). It only goes to the clipboard: nothing is sent anywhere.
 *
 * No message between games changes for any of it. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the join's steps, in order */
enum {
    PORT_JOIN_NONE,
    PORT_JOIN_CONNECT,  /* the relay's room reached (by address: the host's game) */
    PORT_JOIN_KEY,      /* the room's key agreed (by address: none, done with the connection) */
    PORT_JOIN_VERSION,  /* the hellos: the same disc, protocol and mods */
    PORT_JOIN_PROGRESS, /* the host's whole progress taken (co-op) */
    PORT_JOIN_WORLD,    /* in the host's world: its room, in the field */
    PORT_JOIN_DONE
};

/* the connection's quality, as the icon shows it */
enum { PORT_LINK_NONE, PORT_LINK_GOOD, PORT_LINK_FAIR, PORT_LINK_POOR, PORT_LINK_DOWN };

/* statesync_net_pc.c: a session began (ssNetBegin), a step done, the session over (ssNetStop) */
void portOnlineJoinBegin(int host, int room, int watch);
void portOnlineJoinStep(int step);
void portOnlineJoinEnd(void);
/* statesync_net_pc.c: one round trip measured to that player (its ping's answer), and one log line (ssLog's,
   without "[sync] f<frame> ") for the diagnostics */
void portOnlineQualityRtt(int player, int ms);
void portOnlineStatusLogLine(const char* line);

/* each game frame (portStateSyncGameFrame): the steps' times, the quality, what's drawn */
void portOnlineStatusFrame(void);

/* the join now: its step (PORT_JOIN_*; PORT_JOIN_NONE: no join under way, PORT_JOIN_DONE: done), and the
   step's name for the menus (English); 1 when its time has passed (taking long) */
int portOnlineJoinStepNow(void);
const char* portOnlineJoinStepName(int step);
int portOnlineJoinSlow(void);
/* the join's line for the Multiplayer page ("Joining (2/5): making a secure connection..."), translated, in the
   game's encoding: 0 when no join is under way */
int portOnlineJoinLine(char* out, size_t cap);
/* what to do about the join now (English, translated when shown): its step's advice once its time has passed,
   NULL while it goes as it should */
const char* portOnlineJoinAdvice(void);
/* the host isn't in a game yet (the title, its files): the guest waits in the room */
int portOnlineJoinLobby(void);

/* a player's link as measured here (PORT_LINK_*: NONE when not measured here), its round trip (the slowest
   tenth of the last 10 seconds, ms; -1 none) */
int portOnlineLinkQuality(int player);
int portOnlineLinkRttP90(int player);

/* the diagnostics' text (UTF-8); its length */
size_t portOnlineDiagnostics(char* out, size_t cap);

/* a room's host on a relay whose free day is mostly used ("busy", portStateSyncRelayBusy): the page's line,
   translated, in the game's encoding (what it means, when it resets in this computer's time, what still
   works); 0 when it isn't busy. Said once as a notice too, as the relay first says it */
int portOnlineBusyLine(char* out, size_t cap);

/* the refusal says the friend's Paper Cut is newer (this one should update) */
int portOnlineRefusedNewer(void);

/* Copy diagnostics: to the clipboard (the headless build: the log); 1 done. The join's window and the wait
   for a lost host's both use it. */
int portOnlineCopyDiagnostics(void);

/* The one window for an online wait (Aurora only): the join's that takes long or failed, and online_end_pc.c's
   wait for a lost host ("Waiting for <host> to come back...") look and work the same. Shown this frame: its
   line and advice (translated, the game's encoding), its rows (labels English, translated as drawn; values in
   the game's encoding or NULL), the cursor's row, and a short answer under the rows (the game's encoding, or
   NULL). portOnlineNoticePointer: the pointer over its rows (*row moved), PAD_BUTTON_A on a click, else 0. */
void portOnlineNoticeShow(const char* line, const char* advice, const char* const* rows, const char* const* values,
                          int count, int row, const char* answer);
unsigned portOnlineNoticePointer(int count, int* row);

#ifdef __cplusplus
}
#endif

#endif
