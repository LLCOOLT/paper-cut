/* Online play's way to the other players besides a direct TCP connection
 * (statesync_net_pc.c uses it; docs/ONLINE_RELAY.md): the relay, a WebSocket
 * (ws:// or wss://, Mbed TLS) to a room on the relay, carrying the
 * connection's byte stream, sealed end to end. */
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
/* the bytes given to send that haven't gone out yet (a sender's pacing: resends wait while it's high) */
size_t portWsBacklog(void);
/* the binary messages' bytes that came in, in order, as one stream */
size_t portWsRead(void* out, size_t cap);
/* the relay's next text message ("you <address>", "peer", "gone"), 0 when none */
int portWsText(char* out, size_t cap);
void portWsClose(void);
/* Mbed TLS's PSA set up once (online_seal_pc.c; built wherever Mbed TLS is): 1 when ready. Every PSA
 * user calls this, never psa_crypto_init (it isn't safe from two threads at once in this build) */
int portOnlinePsaInit(void);

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
/* The same for one of several pairs of games: link 0 is the one above (a room's guest); a
 * room's host agrees a key of its own with each of players 3 and 4 (links 1 and 2) */
#define PORT_SEAL_LINKS 3
int portPakeBeginAt(int link, int host, const char* secret, unsigned char* out, size_t cap, size_t* len);
int portPakeRoundOneAt(int link, const unsigned char* in, size_t len, unsigned char* out, size_t cap,
                       size_t* outLen);
int portPakeRoundTwoAt(int link, const unsigned char* in, size_t len);
int portSealReadyAt(int link);
size_t portSealAt(int link, int channel, const unsigned char* in, size_t len, unsigned char* out);
int portOpenAt(int link, int channel, const unsigned char* in, size_t len, unsigned char* out, size_t* outLen);
void portSealResetAt(int link);

#ifdef __cplusplus
}
#endif

#endif
