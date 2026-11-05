/* Online co-op's relay link (port/include/port/online_link.h): one WebSocket
 * to a room on the relay (port/relay, docs/ONLINE_RELAY.md), ws:// for tests
 * on this computer, wss:// otherwise, with Mbed TLS and the roots in
 * online_roots.inc. A thread of its own connects (the address, TLS, the
 * HTTP upgrade), then sends what's queued and reads what comes, every couple
 * of milliseconds; the game's thread only queues and takes, under a mutex.
 * Binary messages are the connection's byte stream, in order; text ones are
 * the relay's own news. */
#include "port/online_link.h"

#if defined(__EMSCRIPTEN__) || !defined(TTYD_BACKEND_AURORA)

int portWsOpen(const char* url) {
    (void)url;
    return 0;
}
int portWsState(void) {
    return PORT_WS_CLOSED;
}
const char* portWsError(void) {
    return "Not in this build";
}
void portWsSend(const void* data, size_t len) {
    (void)data;
    (void)len;
}
void portWsSendText(const char* text) {
    (void)text;
}
size_t portWsRead(void* out, size_t cap) {
    (void)out;
    (void)cap;
    return 0;
}
int portWsText(char* out, size_t cap) {
    (void)out;
    (void)cap;
    return 0;
}
void portWsClose(void) {}

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef select /* port_prefix.h's rename for the game (tik_05.c): this is the host's */
#include "port/online_sock.h"
#include <SDL3/SDL.h>
#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/sha1.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include "online_roots.inc"

#define MAX_FRAME (2u << 20) /* the relay passes up to 1 MB */
#define TEXTS 8

typedef struct {
    unsigned char* p;
    size_t len, cap;
} Buf;

static void bufAdd(Buf* b, const void* data, size_t len) {
    if (b->len + len > b->cap) {
        b->cap = (b->len + len) * 2 + 4096;
        b->p = (unsigned char*)realloc(b->p, b->cap);
    }
    memcpy(b->p + b->len, data, len);
    b->len += len;
}

static void bufTake(Buf* b, size_t n) {
    memmove(b->p, b->p + n, b->len - n);
    b->len -= n;
}

/* shared with the thread, under sLock */
static SDL_Mutex* sLock;
static SDL_Thread* sThread;
static int sState = PORT_WS_IDLE, sStop;
static char sError[160];
static Buf sOut;  /* whole frames, ready to go */
static Buf sIn;   /* the binary messages' bytes */
static char sTexts[TEXTS][96];
static int sTextHead, sTextCount;
static unsigned sSent; /* binary messages sent (what the relay counts), logged at the end */

/* the thread's own */
typedef struct {
    char host[256], path[512], url[1024];
    int port, tls;
    Socket sock;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    Buf rx;
    unsigned maskSeed;
} Conn;

static void setError(const char* why) {
    SDL_LockMutex(sLock);
    if (sError[0] == '\0') {
        snprintf(sError, sizeof(sError), "%s", why);
    }
    sState = PORT_WS_CLOSED;
    SDL_UnlockMutex(sLock);
}

static int parseUrl(Conn* c, const char* url) {
    const char* p;
    const char* slash;
    const char* colon;
    size_t hostLen;
    if (strncmp(url, "wss://", 6) == 0) {
        c->tls = 1;
        c->port = 443;
        p = url + 6;
    } else if (strncmp(url, "ws://", 5) == 0) {
        c->tls = 0;
        c->port = 80;
        p = url + 5;
    } else {
        return 0;
    }
    slash = strchr(p, '/');
    if (slash == NULL) {
        slash = p + strlen(p);
    }
    colon = memchr(p, ':', (size_t)(slash - p));
    hostLen = (size_t)((colon != NULL ? colon : slash) - p);
    if (hostLen == 0 || hostLen >= sizeof(c->host)) {
        return 0;
    }
    memcpy(c->host, p, hostLen);
    c->host[hostLen] = '\0';
    if (colon != NULL) {
        c->port = atoi(colon + 1);
    }
    snprintf(c->path, sizeof(c->path), "%s", *slash != '\0' ? slash : "/");
    return c->port > 0;
}

/* --- the bytes, through TLS or not --- */

static int bioSend(void* ctx, const unsigned char* buf, size_t len) {
    Conn* c = (Conn*)ctx;
    const int n = (int)send(c->sock, (const char*)buf, (int)len, 0);
    return n >= 0 ? n : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bioRecv(void* ctx, unsigned char* buf, size_t len) {
    Conn* c = (Conn*)ctx;
    int n;
    if (!onlineReadable(c->sock, 10000)) {
        return MBEDTLS_ERR_SSL_TIMEOUT;
    }
    n = (int)recv(c->sock, (char*)buf, (int)len, 0);
    return n > 0 ? n : n == 0 ? MBEDTLS_ERR_NET_CONN_RESET : MBEDTLS_ERR_NET_RECV_FAILED;
}

static int writeAll(Conn* c, const unsigned char* p, size_t len) {
    while (len > 0) {
        int n;
        if (c->tls) {
            n = mbedtls_ssl_write(&c->ssl, p, len);
            if (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) {
                continue;
            }
        } else {
            n = (int)send(c->sock, (const char*)p, (int)len, 0);
        }
        if (n <= 0) {
            return 0;
        }
        p += n;
        len -= (size_t)n;
    }
    return 1;
}

/* what's there now (or within ms), into rx; -1 when the connection closed */
static int readSome(Conn* c, int ms) {
    unsigned char tmp[16384];
    int n;
    if (c->tls) {
        if (mbedtls_ssl_get_bytes_avail(&c->ssl) == 0 && !onlineReadable(c->sock, ms)) {
            return 0;
        }
        n = mbedtls_ssl_read(&c->ssl, tmp, sizeof(tmp));
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE ||
            n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            return 0;
        }
    } else {
        if (!onlineReadable(c->sock, ms)) {
            return 0;
        }
        n = (int)recv(c->sock, (char*)tmp, sizeof(tmp), 0);
    }
    if (n <= 0) {
        return -1;
    }
    bufAdd(&c->rx, tmp, (size_t)n);
    return n;
}

/* --- connecting --- */

static int connectTcp(Conn* c) {
    struct addrinfo hints, *found = NULL, *a;
    char service[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%d", c->port);
    if (getaddrinfo(c->host, service, &hints, &found) != 0 || found == NULL) {
        setError("The relay wasn't found (no internet?).");
        return 0;
    }
    c->sock = BAD_SOCKET;
    for (a = found; a != NULL && c->sock == BAD_SOCKET; a = a->ai_next) {
        c->sock = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (c->sock != BAD_SOCKET && connect(c->sock, a->ai_addr, (int)a->ai_addrlen) != 0) {
            closeSocket(c->sock);
            c->sock = BAD_SOCKET;
        }
    }
    freeaddrinfo(found);
    if (c->sock == BAD_SOCKET) {
        setError("The relay couldn't be reached (no internet?).");
        return 0;
    }
    {
        int one = 1; /* each batch at once */
        setsockopt(c->sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    }
    onlineNonBlocking(c->sock, 0);
    return 1;
}

static int startTls(Conn* c) {
    static int sPsa;
    int ret;
    if (!sPsa) {
        sPsa = psa_crypto_init() == PSA_SUCCESS; /* TLS 1.3's keys come from PSA */
    }
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_x509_crt_init(&c->ca);
    mbedtls_entropy_init(&c->entropy);
    mbedtls_ctr_drbg_init(&c->drbg);
    if (!sPsa || mbedtls_ctr_drbg_seed(&c->drbg, mbedtls_entropy_func, &c->entropy, (const unsigned char*)"pc", 2) ||
        mbedtls_x509_crt_parse(&c->ca, (const unsigned char*)kOnlineRoots, sizeof(kOnlineRoots)) < 0 ||
        mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        setError("A secure connection couldn't be set up.");
        return 0;
    }
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&c->conf, &c->ca, NULL);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);
    if (mbedtls_ssl_setup(&c->ssl, &c->conf) != 0 || mbedtls_ssl_set_hostname(&c->ssl, c->host) != 0) {
        setError("A secure connection couldn't be set up.");
        return 0;
    }
    mbedtls_ssl_set_bio(&c->ssl, c, bioSend, bioRecv, NULL);
    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char why[100];
            mbedtls_strerror(ret, why, sizeof(why));
            SDL_Log("online: relay TLS: %s", why);
            setError(ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
                         ? "The relay's certificate wasn't recognised: is the computer's clock right?"
                         : "A secure connection to the relay failed.");
            return 0;
        }
    }
    return 1;
}

/* the HTTP upgrade: 101 and the key's answer, or the relay's reason */
static int upgrade(Conn* c) {
    unsigned char nonce[16], expected[64], digest[20];
    char key[32], request[1600], concat[96];
    size_t keyLen = 0, expLen = 0;
    const char* end;
    int i, status;
    for (i = 0; i < 16; i++) {
        nonce[i] = (unsigned char)(SDL_rand(256));
    }
    mbedtls_base64_encode((unsigned char*)key, sizeof(key), &keyLen, nonce, sizeof(nonce));
    key[keyLen] = '\0';
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: PaperCut\r\n\r\n",
             c->path, c->host, key);
    if (!writeAll(c, (const unsigned char*)request, strlen(request))) {
        setError("The relay couldn't be reached.");
        return 0;
    }
    for (;;) {
        bufAdd(&c->rx, "", 1); /* a terminator, taken off below */
        c->rx.len--;
        end = strstr((const char*)c->rx.p, "\r\n\r\n");
        if (end != NULL) {
            break;
        }
        if (c->rx.len > 16384 || readSome(c, 10000) <= 0) {
            setError("The relay didn't answer.");
            return 0;
        }
    }
    status = c->rx.len > 12 ? atoi((const char*)c->rx.p + 9) : 0;
    if (status != 101) {
        /* the relay's own reasons are a line of English in the body */
        const char* body = end + 4;
        char line[120];
        size_t n = 0;
        readSome(c, 300);
        bufAdd(&c->rx, "", 1);
        body = strstr((const char*)c->rx.p, "\r\n\r\n") + 4;
        while (body[n] != '\0' && body[n] != '\n' && n < sizeof(line) - 1) {
            line[n] = body[n];
            n++;
        }
        line[n] = '\0';
        SDL_Log("online: relay answered %d: %s", status, line);
        if (status == 429 || status >= 500) {
            setError("The relay is full for today: it opens again at midnight UTC. Connecting by address "
                     "still works.");
        } else if (status == 404 && strstr(line, "hosting") != NULL) {
            setError("No one is hosting that room: check the code.");
        } else if (status == 409 && strstr(line, "full") != NULL) {
            setError("That room already has two players.");
        } else if (status == 409) {
            setError("That room code is taken.");
        } else {
            setError("The relay refused the connection.");
        }
        return 0;
    }
    snprintf(concat, sizeof(concat), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    mbedtls_sha1((const unsigned char*)concat, strlen(concat), digest);
    mbedtls_base64_encode(expected, sizeof(expected), &expLen, digest, sizeof(digest));
    expected[expLen] = '\0';
    if (strstr((const char*)c->rx.p, (const char*)expected) == NULL) {
        setError("That isn't Paper Cut's relay.");
        return 0;
    }
    bufTake(&c->rx, (size_t)(end + 4 - (const char*)c->rx.p));
    return 1;
}

/* --- frames --- */

static void frame(Buf* out, int opcode, const void* data, size_t len, unsigned* seed) {
    unsigned char head[14], mask[4];
    size_t n = 0, i;
    head[n++] = (unsigned char)(0x80 | opcode);
    if (len < 126) {
        head[n++] = (unsigned char)(0x80 | len);
    } else if (len < 65536) {
        head[n++] = 0x80 | 126;
        head[n++] = (unsigned char)(len >> 8);
        head[n++] = (unsigned char)len;
    } else {
        head[n++] = 0x80 | 127;
        for (i = 0; i < 8; i++) {
            head[n++] = (unsigned char)((unsigned long long)len >> (56 - 8 * i));
        }
    }
    *seed = *seed * 1664525u + 1013904223u; /* a client's mask: any four bytes */
    for (i = 0; i < 4; i++) {
        mask[i] = (unsigned char)(*seed >> (8 * i));
    }
    bufAdd(out, head, n);
    bufAdd(out, mask, 4);
    if (out->len + len > out->cap) {
        out->cap = (out->len + len) * 2;
        out->p = (unsigned char*)realloc(out->p, out->cap);
    }
    for (i = 0; i < len; i++) {
        out->p[out->len + i] = ((const unsigned char*)data)[i] ^ mask[i & 3];
    }
    out->len += len;
}

/* the whole frames in rx, handled; 0 when the relay closed */
static int handleFrames(Conn* c) {
    for (;;) {
        const unsigned char* p = c->rx.p;
        size_t n = 2, len, i;
        int opcode;
        if (c->rx.len < 2) {
            return 1;
        }
        opcode = p[0] & 0x0F;
        len = p[1] & 0x7F;
        if (len == 126) {
            if (c->rx.len < 4) {
                return 1;
            }
            len = ((size_t)p[2] << 8) | p[3];
            n = 4;
        } else if (len == 127) {
            if (c->rx.len < 10) {
                return 1;
            }
            len = 0;
            for (i = 0; i < 8; i++) {
                len = (len << 8) | p[2 + i];
            }
            n = 10;
        }
        if (len > MAX_FRAME) {
            setError("The relay sent something that isn't Paper Cut's.");
            return 0;
        }
        if (c->rx.len < n + len) {
            return 1;
        }
        p += n;
        if (opcode == 2 || opcode == 0) { /* binary (and its continuations: the relay doesn't split) */
            SDL_LockMutex(sLock);
            bufAdd(&sIn, p, len);
            SDL_UnlockMutex(sLock);
        } else if (opcode == 1) {
            SDL_LockMutex(sLock);
            if (sTextCount < TEXTS) {
                const size_t m = len < sizeof(sTexts[0]) - 1 ? len : sizeof(sTexts[0]) - 1;
                char* t = sTexts[(sTextHead + sTextCount) % TEXTS];
                memcpy(t, p, m);
                t[m] = '\0';
                sTextCount++;
            }
            SDL_UnlockMutex(sLock);
        } else if (opcode == 9) {
            Buf pong = {0};
            frame(&pong, 10, p, len, &c->maskSeed);
            writeAll(c, pong.p, pong.len);
            free(pong.p);
        } else if (opcode == 8) {
            bufTake(&c->rx, n + len);
            setError("The relay closed the room.");
            return 0;
        }
        bufTake(&c->rx, n + len);
    }
}

static int SDLCALL run(void* data) {
    Conn* c = (Conn*)data;
    int ok = connectTcp(c) && (!c->tls || startTls(c)) && upgrade(c) && handleFrames(c);
    if (ok) {
        SDL_LockMutex(sLock);
        sState = PORT_WS_OPEN;
        SDL_UnlockMutex(sLock);
    }
    while (ok) {
        Buf out = {0};
        int stop;
        SDL_LockMutex(sLock);
        stop = sStop;
        out = sOut;
        memset(&sOut, 0, sizeof(sOut));
        SDL_UnlockMutex(sLock);
        if (out.len > 0 && !writeAll(c, out.p, out.len)) {
            setError("The connection to the relay was lost.");
            ok = 0;
        }
        free(out.p);
        if (stop) {
            Buf bye = {0};
            frame(&bye, 8, "\x03\xE8", 2, &c->maskSeed); /* 1000: normal */
            writeAll(c, bye.p, bye.len);
            free(bye.p);
            break;
        }
        if (ok) {
            const int got = readSome(c, 2);
            if (got < 0) {
                setError("The connection to the relay was lost.");
                ok = 0;
            } else if (got > 0) {
                ok = handleFrames(c);
            }
        }
    }
    if (c->tls) {
        mbedtls_ssl_free(&c->ssl);
        mbedtls_ssl_config_free(&c->conf);
        mbedtls_x509_crt_free(&c->ca);
        mbedtls_ctr_drbg_free(&c->drbg);
        mbedtls_entropy_free(&c->entropy);
    }
    if (c->sock != BAD_SOCKET) {
        closeSocket(c->sock);
    }
    free(c->rx.p);
    free(c);
    return 0;
}

int portWsOpen(const char* url) {
    Conn* c;
    portWsClose();
    if (sLock == NULL) {
        sLock = SDL_CreateMutex();
    }
    sError[0] = '\0';
    sStop = 0;
    sIn.len = sOut.len = 0;
    sTextCount = 0;
    c = (Conn*)calloc(1, sizeof(Conn));
    c->sock = BAD_SOCKET;
    c->maskSeed = (unsigned)SDL_GetTicksNS();
    snprintf(c->url, sizeof(c->url), "%s", url);
    if (!onlineNetStart() || !parseUrl(c, url)) {
        free(c);
        sState = PORT_WS_CLOSED;
        snprintf(sError, sizeof(sError), "%s", "The relay's address isn't right.");
        return 0;
    }
    sState = PORT_WS_OPENING;
    sThread = SDL_CreateThread(run, "online relay", c);
    if (sThread == NULL) {
        free(c);
        sState = PORT_WS_CLOSED;
        return 0;
    }
    return 1;
}

int portWsState(void) {
    int s;
    if (sLock == NULL) {
        return PORT_WS_IDLE;
    }
    SDL_LockMutex(sLock);
    s = sState;
    SDL_UnlockMutex(sLock);
    return s;
}

const char* portWsError(void) {
    return sError;
}

void portWsSend(const void* data, size_t len) {
    static unsigned sSeed = 0x9E3779B9u;
    if (sLock == NULL || len == 0) {
        return;
    }
    SDL_LockMutex(sLock);
    if (sState == PORT_WS_OPEN || sState == PORT_WS_OPENING) {
        frame(&sOut, 2, data, len, &sSeed);
        sSent++;
    }
    SDL_UnlockMutex(sLock);
}

void portWsSendText(const char* text) {
    static unsigned sSeed = 0x7F4A7C15u;
    if (sLock == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    SDL_LockMutex(sLock);
    if (sState == PORT_WS_OPEN) {
        frame(&sOut, 1, text, strlen(text), &sSeed);
        sSent++;
    }
    SDL_UnlockMutex(sLock);
}

size_t portWsRead(void* out, size_t cap) {
    size_t n;
    if (sLock == NULL) {
        return 0;
    }
    SDL_LockMutex(sLock);
    n = sIn.len < cap ? sIn.len : cap;
    memcpy(out, sIn.p, n);
    bufTake(&sIn, n);
    SDL_UnlockMutex(sLock);
    return n;
}

int portWsText(char* out, size_t cap) {
    int got = 0;
    if (sLock == NULL) {
        return 0;
    }
    SDL_LockMutex(sLock);
    if (sTextCount > 0) {
        snprintf(out, cap, "%s", sTexts[sTextHead]);
        sTextHead = (sTextHead + 1) % TEXTS;
        sTextCount--;
        got = 1;
    }
    SDL_UnlockMutex(sLock);
    return got;
}

void portWsClose(void) {
    if (sThread != NULL) {
        SDL_LockMutex(sLock);
        sStop = 1;
        SDL_UnlockMutex(sLock);
        SDL_WaitThread(sThread, NULL);
        sThread = NULL;
        SDL_Log("online: relay: %u messages sent", sSent);
    }
    sSent = 0;
    if (sLock != NULL) {
        SDL_LockMutex(sLock);
        sState = PORT_WS_IDLE;
        sIn.len = sOut.len = 0;
        sTextCount = 0;
        SDL_UnlockMutex(sLock);
    }
}

#endif
