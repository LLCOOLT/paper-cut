/* Online co-op's two ways to the other player besides a direct TCP
 * connection (online_net_pc.c uses them; docs/ONLINE_RELAY.md):
 * - the relay: a WebSocket (ws:// or wss://, Mbed TLS) to a room on the
 *   relay, carrying the connection's byte stream in batches;
 * - a direct UDP path, found with Cloudflare's STUN server and both sides
 *   sending at once (hole punching), carrying each frame's input with the
 *   ones not yet confirmed repeated, so none is lost. */
#ifndef PORT_ONLINE_LINK_H
#define PORT_ONLINE_LINK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- the relay's WebSocket (one at a time) --- */
enum { PORT_WS_IDLE, PORT_WS_OPENING, PORT_WS_OPEN, PORT_WS_CLOSED };
/* opens url (ws://host[:port]/path or wss://...) on a thread of its own */
int portWsOpen(const char* url);
int portWsState(void);
const char* portWsError(void); /* English: why it closed or never opened */
/* one binary message out (the bytes are copied) */
void portWsSend(const void* data, size_t len);
/* one text message out, to the relay itself (a listed host's "list") */
void portWsSendText(const char* text);
/* the binary messages' bytes that came in, in order, as one stream */
size_t portWsRead(void* out, size_t cap);
/* the relay's next text message ("you <address>", "peer", "gone"), 0 when none */
int portWsText(char* out, size_t cap);
void portWsClose(void);

/* --- the direct UDP path (ICE: libjuice, online_ice_pc.c) --- */
/* an agent of its own gathering this side's addresses: the network's, the
 * public IPv4 one the STUN server sees (stun.cloudflare.com), IPv6 */
int portIceStart(int controlling);
/* the next line for the other side ("d <description>", "c <candidate>", "e"
 * when gathering is done), passed through the relay's room; 0 when none */
int portIceNextSignal(char* out, size_t cap);
void portIceRemoteSignal(const char* line);
enum { PORT_ICE_OFF, PORT_ICE_TRYING, PORT_ICE_OK, PORT_ICE_FAILED };
int portIceState(void);
int portIceRoundTrip(void); /* milliseconds over the path, -1 before */
/* the session over the path: each frame's input, sent with the ones the peer
 * hasn't confirmed, and the fingerprints */
void portIceSendInput(unsigned frame, const unsigned char* pad, size_t padBytes);
int portIcePeerInput(unsigned frame, unsigned char* pad, size_t padBytes);
void portIceSendHash(unsigned frame, unsigned long long hash);
int portIcePeerHash(unsigned frame, unsigned long long* hash);
int portIceSilentMs(void); /* since the peer's last packet, -1 before any */
/* leaving: the reason, sent a few times; the other's, when it came */
void portIceSendBye(const char* why);
const char* portIceBye(void);
void portIceStop(void);

/* --- end-to-end: what passes the relay, sealed (online_seal_pc.c) ---
 * A room's code is ROOM-SECRET: the relay sees the room's half, never the
 * secret. The two games agree on a key from the secret with EC J-PAKE
 * (Mbed TLS: a relay can't work it out from what it carries, and one that
 * meddles gets one guess per try), then seal everything with
 * ChaCha20-Poly1305: the stream (counted in order) and the UDP packets (each
 * with its count; one not newer than the last is dropped). */
#define PORT_SEAL_TAG 16
#define PORT_SEAL_COUNT 8 /* a packet's own count, in front */
/* this side's first message (host: the server's role), or 0 */
int portPakeBegin(int host, const char* secret, unsigned char* out, size_t cap, size_t* len);
/* the other's first message read, this side's second written */
int portPakeRoundOne(const unsigned char* in, size_t len, unsigned char* out, size_t cap, size_t* outLen);
/* the other's second read: the keys are there (1), or the secret differs */
int portPakeRoundTwo(const unsigned char* in, size_t len);
int portSealReady(void);
enum { PORT_SEAL_STREAM, PORT_SEAL_PACKET };
/* out: in's length + PORT_SEAL_TAG (+ PORT_SEAL_COUNT for a packet) */
size_t portSeal(int channel, const unsigned char* in, size_t len, unsigned char* out);
/* out: the opened bytes; 0 when they don't open (changed, replayed, the wrong key) */
int portOpen(int channel, const unsigned char* in, size_t len, unsigned char* out, size_t* outLen);
void portSealReset(void);

/* the watchers' channel (host to watchers, through the relay): one message
 * sealed for them all, the key from the whole code (PBKDF2), the nonce (its
 * random 4 bytes and count) in front */
#define PORT_WATCH_OVERHEAD (12 + PORT_SEAL_TAG)
int portWatchKey(const char* code);
size_t portWatchSeal(const unsigned char* in, size_t len, unsigned char* out);
int portWatchOpen(const unsigned char* in, size_t len, unsigned char* out, size_t* outLen);
void portWatchReset(void);

/* the direct path after a reconnect: the first of this side's frames the other
 * still needs, the first of the other's this side needs */
void portIceResume(unsigned peerNeeds, unsigned theirsNext);

#ifdef __cplusplus
}
#endif

#endif
