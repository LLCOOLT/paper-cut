#ifndef PORT_ONLINE_H
#define PORT_ONLINE_H

/* What online play (port/include/port/statesync.h), replays and the tests share:
 * room codes, the relays, the controllers as bytes and the game's fingerprint. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_ONLINE_DEFAULT_PORT 47700
#define PORT_ONLINE_PAD_BYTES 16 /* one controller's state for a frame (PADStatus, extButton too) */

/* Rooms on the relay (docs/ONLINE_RELAY.md): the host opens one by its code,
 * the others join with the code, no address or router to set up. */
#define PORT_ONLINE_CODE_CHARS "23456789ABCDEFGHJKMNPQRSTUVWXYZ" /* nothing to read wrong (0/O, 1/I/L) */
/* ROOM-SECRET, shown with a dash: the relay sees only the room's half (its
 * first character names the relay); the secret half makes the session's key
 * between the games (port/include/port/online_link.h), never sent */
#define PORT_ONLINE_CODE_LENGTH 8
#define PORT_ONLINE_ROOM_HALF 4

/* port/src/common/online_relay_pc.c: the relay's address (the build's
 * TTYD_ONLINE_RELAY, or $TTYD_ONLINE_RELAY), "" when there's none; the build's
 * relays, the i-th's address, or NULL past the last; this computer's address
 * on its network (for a host to share), or 0 */
const char* portOnlineRelay(void);
const char* portOnlineRelayAt(int i);
int portOnlineLocalAddress(char* out, size_t size);

/* port/src/common/fingerprint_pc.c (replays, the tests and shared online battles use these):
 * one controller's state as PORT_ONLINE_PAD_BYTES, and back */
struct PADStatus;
void portOnlinePackPad(const struct PADStatus* s, unsigned char* p);
void portOnlineUnpackPad(const unsigned char* p, struct PADStatus* s);
/* A hash of what the game plays out this frame, the same in two games on the
 * same input. */
unsigned long long portOnlineFingerprint(void);
/* each frame, after the game's (vi_aurora.c, headless vi_pc.c): the tests' fingerprint file
 * ($TTYD_FINGERPRINT) */
void portFingerprintFrame(void);

#ifdef __cplusplus
}
#endif

#endif
