/* Online play's end-to-end sealing (port/include/port/online_link.h): the
 * key from the room code's secret half by EC J-PAKE (P-256, SHA-256), four
 * keys from it by HKDF (each direction, the stream and the packets), then
 * ChaCha20-Poly1305. A nonce is its key's count (the stream's implicit, in
 * order; a packet's in front of it), never used twice under one key. */
#include "port/online_link.h"

#if (defined(__EMSCRIPTEN__) && !defined(PORT_WEB_ONLINE)) || !defined(TTYD_BACKEND_AURORA)

int portPakeBegin(int host, const char* secret, unsigned char* out, size_t cap, size_t* len) {
    (void)host;
    (void)secret;
    (void)out;
    (void)cap;
    (void)len;
    return 0;
}
int portPakeRoundOne(const unsigned char* in, size_t len, unsigned char* out, size_t cap, size_t* outLen) {
    (void)in;
    (void)len;
    (void)out;
    (void)cap;
    (void)outLen;
    return 0;
}
int portPakeRoundTwo(const unsigned char* in, size_t len) {
    (void)in;
    (void)len;
    return 0;
}
int portSealReady(void) {
    return 0;
}
size_t portSeal(int channel, const unsigned char* in, size_t len, unsigned char* out) {
    (void)channel;
    (void)in;
    (void)len;
    (void)out;
    return 0;
}
int portOpen(int channel, const unsigned char* in, size_t len, unsigned char* out, size_t* outLen) {
    (void)channel;
    (void)in;
    (void)len;
    (void)out;
    (void)outLen;
    return 0;
}
void portSealReset(void) {}
void portSealResetAt(int link) {
    (void)link;
}
int portPakeBeginAt(int link, int host, const char* secret, unsigned char* out, size_t cap, size_t* len) {
    (void)link;
    return portPakeBegin(host, secret, out, cap, len);
}
int portPakeRoundOneAt(int link, const unsigned char* in, size_t len, unsigned char* out, size_t cap,
                       size_t* outLen) {
    (void)link;
    return portPakeRoundOne(in, len, out, cap, outLen);
}
int portPakeRoundTwoAt(int link, const unsigned char* in, size_t len) {
    (void)link;
    return portPakeRoundTwo(in, len);
}
int portSealReadyAt(int link) {
    (void)link;
    return 0;
}
size_t portSealAt(int link, int channel, const unsigned char* in, size_t len, unsigned char* out) {
    (void)link;
    return portSeal(channel, in, len, out);
}
int portOpenAt(int link, int channel, const unsigned char* in, size_t len, unsigned char* out, size_t* outLen) {
    (void)link;
    return portOpen(channel, in, len, out, outLen);
}

#else

#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <mbedtls/chachapoly.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecjpake.h>
#include <mbedtls/entropy.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>

typedef struct {
    mbedtls_chachapoly_context aead;
    unsigned long long count; /* the next to seal, or the next expected (the stream) / the last opened + 1 (packets) */
} Way;

/* one pair of games' keys: link 0 a room's guest's, 1.. a room's players 3 and 4 (the host
   agrees a key with each) */
typedef struct {
    mbedtls_ecjpake_context pake;
    int host, paking, ready;
    Way out[2], in[2]; /* [PORT_SEAL_STREAM], [PORT_SEAL_PACKET] */
} Link;

static Link sLink[PORT_SEAL_LINKS];
static mbedtls_entropy_context sEntropy;
static mbedtls_ctr_drbg_context sDrbg;
static int sDrbgReady;
/* (a lock: the accesses could come from more than one thread) */
static SDL_Mutex* sLock;

/* PSA set up once for the whole game (online_link.h), whichever thread comes first: the relay's (TLS 1.3)
   or the game's (here). This build's Mbed TLS has no MBEDTLS_THREADING_C, so psa_crypto_init itself isn't
   safe from two threads at once */
int portOnlinePsaInit(void) {
    static SDL_SpinLock sOnce;
    static int sDone, sOk;
    int ok;
    SDL_LockSpinlock(&sOnce);
    if (!sDone) {
        sDone = 1;
        sOk = psa_crypto_init() == PSA_SUCCESS;
    }
    ok = sOk;
    SDL_UnlockSpinlock(&sOnce);
    return ok;
}

static int rng(void) {
    if (!sDrbgReady) {
        if (!portOnlinePsaInit()) {
            return 0;
        }
        mbedtls_entropy_init(&sEntropy);
        mbedtls_ctr_drbg_init(&sDrbg);
        if (mbedtls_ctr_drbg_seed(&sDrbg, mbedtls_entropy_func, &sEntropy, (const unsigned char*)"pcseal", 6) != 0) {
            return 0;
        }
        sDrbgReady = 1;
    }
    return 1;
}

static Link* linkAt(int link) {
    return link >= 0 && link < PORT_SEAL_LINKS ? &sLink[link] : NULL;
}

void portSealResetAt(int link) {
    Link* L = linkAt(link);
    int i;
    if (sLock == NULL) {
        sLock = SDL_CreateMutex();
    }
    if (L == NULL) {
        return;
    }
    SDL_LockMutex(sLock);
    if (L->paking) {
        mbedtls_ecjpake_free(&L->pake);
        L->paking = 0;
    }
    if (L->ready) {
        for (i = 0; i < 2; i++) {
            mbedtls_chachapoly_free(&L->out[i].aead);
            mbedtls_chachapoly_free(&L->in[i].aead);
        }
        L->ready = 0;
    }
    SDL_UnlockMutex(sLock);
}

int portPakeBeginAt(int link, int host, const char* secret, unsigned char* out, size_t cap, size_t* len) {
    Link* L = linkAt(link);
    portSealResetAt(link);
    if (L == NULL || !rng()) {
        return 0;
    }
    L->host = host;
    mbedtls_ecjpake_init(&L->pake);
    L->paking = 1;
    return mbedtls_ecjpake_setup(&L->pake, host ? MBEDTLS_ECJPAKE_SERVER : MBEDTLS_ECJPAKE_CLIENT, MBEDTLS_MD_SHA256,
                                 MBEDTLS_ECP_DP_SECP256R1, (const unsigned char*)secret, strlen(secret)) == 0 &&
           mbedtls_ecjpake_write_round_one(&L->pake, out, cap, len, mbedtls_ctr_drbg_random, &sDrbg) == 0;
}

int portPakeRoundOneAt(int link, const unsigned char* in, size_t len, unsigned char* out, size_t cap,
                       size_t* outLen) {
    Link* L = linkAt(link);
    return L != NULL && L->paking && mbedtls_ecjpake_read_round_one(&L->pake, in, len) == 0 &&
           mbedtls_ecjpake_write_round_two(&L->pake, out, cap, outLen, mbedtls_ctr_drbg_random, &sDrbg) == 0;
}

static void keyed(Way* way, const unsigned char* secret, const char* label) {
    unsigned char key[32];
    mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const unsigned char*)"Paper Cut online", 16, secret,
                 32, (const unsigned char*)label, strlen(label), key, sizeof(key));
    mbedtls_chachapoly_init(&way->aead);
    mbedtls_chachapoly_setkey(&way->aead, key);
    mbedtls_platform_zeroize(key, sizeof(key));
    way->count = 0;
}

int portPakeRoundTwoAt(int link, const unsigned char* in, size_t len) {
    Link* L = linkAt(link);
    unsigned char secret[32];
    size_t n = 0;
    if (L == NULL || !L->paking || mbedtls_ecjpake_read_round_two(&L->pake, in, len) != 0 ||
        mbedtls_ecjpake_derive_secret(&L->pake, secret, sizeof(secret), &n, mbedtls_ctr_drbg_random, &sDrbg) != 0 ||
        n != sizeof(secret)) {
        return 0;
    }
    mbedtls_ecjpake_free(&L->pake);
    L->paking = 0;
    SDL_LockMutex(sLock);
    /* the same key both ways would reuse its nonces: one for each direction and channel */
    keyed(&L->out[PORT_SEAL_STREAM], secret, L->host ? "stream host to guest" : "stream guest to host");
    keyed(&L->in[PORT_SEAL_STREAM], secret, L->host ? "stream guest to host" : "stream host to guest");
    keyed(&L->out[PORT_SEAL_PACKET], secret, L->host ? "packets host to guest" : "packets guest to host");
    keyed(&L->in[PORT_SEAL_PACKET], secret, L->host ? "packets guest to host" : "packets host to guest");
    L->ready = 1;
    SDL_UnlockMutex(sLock);
    mbedtls_platform_zeroize(secret, sizeof(secret));
    return 1;
}

int portSealReadyAt(int link) {
    Link* L = linkAt(link);
    return L != NULL && L->ready;
}

static void nonce(unsigned char out[12], unsigned long long count) {
    int i;
    memset(out, 0, 12);
    for (i = 0; i < 8; i++) {
        out[4 + i] = (unsigned char)(count >> (8 * i));
    }
}

size_t portSealAt(int link, int channel, const unsigned char* in, size_t len, unsigned char* out) {
    Link* L = linkAt(link);
    unsigned char iv[12];
    unsigned char* body = out;
    unsigned long long count;
    int i;
    if (L == NULL || !L->ready) {
        return 0;
    }
    SDL_LockMutex(sLock);
    count = L->out[channel].count++;
    nonce(iv, count);
    if (channel == PORT_SEAL_PACKET) { /* its count in front: packets come out of order, or not at all */
        for (i = 0; i < 8; i++) {
            out[i] = (unsigned char)(count >> (8 * i));
        }
        body = out + PORT_SEAL_COUNT;
    }
    mbedtls_chachapoly_encrypt_and_tag(&L->out[channel].aead, len, iv, NULL, 0, in, body, body + len);
    SDL_UnlockMutex(sLock);
    return len + PORT_SEAL_TAG + (channel == PORT_SEAL_PACKET ? PORT_SEAL_COUNT : 0);
}

int portOpenAt(int link, int channel, const unsigned char* in, size_t len, unsigned char* out, size_t* outLen) {
    Link* L = linkAt(link);
    unsigned char iv[12];
    unsigned long long count = 0;
    size_t body;
    int i, ok;
    if (L == NULL || !L->ready || len < PORT_SEAL_TAG + (channel == PORT_SEAL_PACKET ? PORT_SEAL_COUNT : 0)) {
        return 0;
    }
    SDL_LockMutex(sLock);
    if (channel == PORT_SEAL_PACKET) {
        for (i = 0; i < 8; i++) {
            count |= (unsigned long long)in[i] << (8 * i);
        }
        in += PORT_SEAL_COUNT;
        len -= PORT_SEAL_COUNT;
        if (count < L->in[channel].count) { /* not newer than the last: a repeat, or late */
            SDL_UnlockMutex(sLock);
            return 0;
        }
    } else {
        count = L->in[channel].count;
    }
    body = len - PORT_SEAL_TAG;
    nonce(iv, count);
    ok = mbedtls_chachapoly_auth_decrypt(&L->in[channel].aead, body, iv, NULL, 0, in + body, in, out) == 0;
    if (ok) {
        L->in[channel].count = count + 1;
        *outLen = body;
    }
    SDL_UnlockMutex(sLock);
    return ok;
}

/* link 0's (a room's guest) */
void portSealReset(void) {
    portSealResetAt(0);
}

int portPakeBegin(int host, const char* secret, unsigned char* out, size_t cap, size_t* len) {
    return portPakeBeginAt(0, host, secret, out, cap, len);
}

int portPakeRoundOne(const unsigned char* in, size_t len, unsigned char* out, size_t cap, size_t* outLen) {
    return portPakeRoundOneAt(0, in, len, out, cap, outLen);
}

int portPakeRoundTwo(const unsigned char* in, size_t len) {
    return portPakeRoundTwoAt(0, in, len);
}

int portSealReady(void) {
    return portSealReadyAt(0);
}

size_t portSeal(int channel, const unsigned char* in, size_t len, unsigned char* out) {
    return portSealAt(0, channel, in, len, out);
}

int portOpen(int channel, const unsigned char* in, size_t len, unsigned char* out, size_t* outLen) {
    return portOpenAt(0, channel, in, len, out, outLen);
}

#endif
