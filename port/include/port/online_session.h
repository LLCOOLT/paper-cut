#ifndef PORT_ONLINE_SESSION_H
#define PORT_ONLINE_SESSION_H

/* The modes' session data (online co-op, port/settings/features/online_session.cpp).
 *
 * Some modes play from a file that is each player's own: the Enemy maker's
 * enemies (enemies.txt), the Tattle card deck (cards.txt) and the stickers
 * (Stickers/<game id>/file<n>.txt). Online they come from the host: its
 * setup's extension block (online.cpp, the setup flags' 8) carries each as a
 * named block of text, and every game of the session (the host's too, and
 * watchers and seats) plays from those for the rest of the run, never saving
 * them to this player's own files. A block the host didn't send is empty for
 * the session (none of that mode's own data), so no game falls back to its
 * own. A name this build doesn't know is skipped (a newer host's).
 *
 * Each mode has a pair, plain C:
 *   get: the host's own data as text into out (size bytes); its length, or
 *        -1 when it didn't fit (sent empty then)
 *   put: the session's data (len bytes, not 0-terminated; len 0: empty), or
 *        data NULL: the session is over before it began (back to this
 *        player's own). */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_SESSION_BLOCK_MAX (64 * 1024)

/* online.cpp: the blocks into the host's setup, and in place on both sides */
int portOnlineSessionCount(void);
const char* portOnlineSessionName(int i);
int portOnlineSessionGet(int i, char* out, int size);
void portOnlineSessionPut(int i, const char* data, int len);

/* the Enemy maker (port/settings/features/battlex.cpp): enemies.txt */
int portBattlexSessionGet(char* out, int size);
void portBattlexSessionPut(const char* data, int len);
/* Tattle cards (port/src/aurora/cards_aurora.c): the deck built */
int portCardsSessionGet(char* out, int size);
void portCardsSessionPut(const char* data, int len);
/* stickers (port/src/common/sticker_pc.c): the four files' records, one
 * block ("file<n>" lines start each) */
int portStickersSessionGet(char* out, int size);
void portStickersSessionPut(const char* data, int len);

#ifdef __cplusplus
}
#endif

#endif
