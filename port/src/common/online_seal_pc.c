/* Online co-op's end-to-end sealing (port/include/port/online_link.h): the
 * key from the room code's secret half by EC J-PAKE (P-256, SHA-256), four
 * keys from it by HKDF (each direction, the stream and the packets), then
 * ChaCha20-Poly1305. A nonce is its key's count (the stream's implicit, in
 * order; a packet's in front of it), never used twice under one key. */
#include "port/online_link.h"

#if defined(__EMSCRIPTEN__) || !defined(TTYD_BACKEND_AURORA)

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

int portWatchKey(const char* code) {
    (void)code;
    return 0;
}
size_t portWatchSeal(const unsigned char* in, size_t len, unsigned char* out) {
    (void)in;
    (void)len;
    (void)out;
    return 0;
}
int portWatchOpen(const unsigned char* in, size_t len, unsigned char* out, size_t* outLen) {
    (void)in;
    (void)len;
    (void)out;
    (void)outLen;
    return 0;
}
void portWatchReset(void) {}

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
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>

typedef struct {
    mbedtls_chachapoly_context aead;
    unsigned long long count; /* the next to seal, or the next expected (the stream) / the last opened + 1 (packets) */
} Way;

static mbedtls_ecjpake_context sPake;
static mbedtls_entropy_context sEntropy;
static mbedtls_ctr_drbg_context sDrbg;
static int sHost, sPaking, sReady, sDrbgReady;
static Way sOut[2], sIn[2]; /* [PORT_SEAL_STREAM], [PORT_SEAL_PACKET] */
/* the packets' accesses come from the game's thread and libjuice's */
static SDL_Mutex* sLock;

static int rng(void) {
    if (!sDrbgReady) {
        if (psa_crypto_init() != PSA_SUCCESS) {
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

void portSealReset(void) {
    int i;
    if (sLock == NULL) {
        sLock = SDL_CreateMutex();
    }
    SDL_LockMutex(sLock);
    if (sPaking) {
        mbedtls_ecjpake_free(&sPake);
        sPaking = 0;
    }
    if (sReady) {
        for (i = 0; i < 2; i++) {
            mbedtls_chachapoly_free(&sOut[i].aead);
            mbedtls_chachapoly_free(&sIn[i].aead);
        }
        sReady = 0;
    }
    SDL_UnlockMutex(sLock);
}

int portPakeBegin(int host, const char* secret, unsigned char* out, size_t cap, size_t* len) {
    portSealReset();
    if (!rng()) {
        return 0;
    }
    sHost = host;
    mbedtls_ecjpake_init(&sPake);
    sPaking = 1;
    return mbedtls_ecjpake_setup(&sPake, host ? MBEDTLS_ECJPAKE_SERVER : MBEDTLS_ECJPAKE_CLIENT, MBEDTLS_MD_SHA256,
                                 MBEDTLS_ECP_DP_SECP256R1, (const unsigned char*)secret, strlen(secret)) == 0 &&
           mbedtls_ecjpake_write_round_one(&sPake, out, cap, len, mbedtls_ctr_drbg_random, &sDrbg) == 0;
}

int portPakeRoundOne(const unsigned char* in, size_t len, unsigned char* out, size_t cap, size_t* outLen) {
    return sPaking && mbedtls_ecjpake_read_round_one(&sPake, in, len) == 0 &&
           mbedtls_ecjpake_write_round_two(&sPake, out, cap, outLen, mbedtls_ctr_drbg_random, &sDrbg) == 0;
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

int portPakeRoundTwo(const unsigned char* in, size_t len) {
    unsigned char secret[32];
    size_t n = 0;
    if (!sPaking || mbedtls_ecjpake_read_round_two(&sPake, in, len) != 0 ||
        mbedtls_ecjpake_derive_secret(&sPake, secret, sizeof(secret), &n, mbedtls_ctr_drbg_random, &sDrbg) != 0 ||
        n != sizeof(secret)) {
        return 0;
    }
    mbedtls_ecjpake_free(&sPake);
    sPaking = 0;
    SDL_LockMutex(sLock);
    /* the same key both ways would reuse its nonces: one for each direction and channel */
    keyed(&sOut[PORT_SEAL_STREAM], secret, sHost ? "stream host to guest" : "stream guest to host");
    keyed(&sIn[PORT_SEAL_STREAM], secret, sHost ? "stream guest to host" : "stream host to guest");
    keyed(&sOut[PORT_SEAL_PACKET], secret, sHost ? "packets host to guest" : "packets guest to host");
    keyed(&sIn[PORT_SEAL_PACKET], secret, sHost ? "packets guest to host" : "packets host to guest");
    sReady = 1;
    SDL_UnlockMutex(sLock);
    mbedtls_platform_zeroize(secret, sizeof(secret));
    return 1;
}

int portSealReady(void) {
    return sReady;
}

static void nonce(unsigned char out[12], unsigned long long count) {
    int i;
    memset(out, 0, 12);
    for (i = 0; i < 8; i++) {
        out[4 + i] = (unsigned char)(count >> (8 * i));
    }
}

size_t portSeal(int channel, const unsigned char* in, size_t len, unsigned char* out) {
    unsigned char iv[12];
    unsigned char* body = out;
    unsigned long long count;
    int i;
    if (!sReady) {
        return 0;
    }
    SDL_LockMutex(sLock);
    count = sOut[channel].count++;
    nonce(iv, count);
    if (channel == PORT_SEAL_PACKET) { /* its count in front: packets come out of order, or not at all */
        for (i = 0; i < 8; i++) {
            out[i] = (unsigned char)(count >> (8 * i));
        }
        body = out + PORT_SEAL_COUNT;
    }
    mbedtls_chachapoly_encrypt_and_tag(&sOut[channel].aead, len, iv, NULL, 0, in, body, body + len);
    SDL_UnlockMutex(sLock);
    return len + PORT_SEAL_TAG + (channel == PORT_SEAL_PACKET ? PORT_SEAL_COUNT : 0);
}

int portOpen(int channel, const unsigned char* in, size_t len, unsigned char* out, size_t* outLen) {
    unsigned char iv[12];
    unsigned long long count = 0;
    size_t body;
    int i, ok;
    if (!sReady || len < PORT_SEAL_TAG + (channel == PORT_SEAL_PACKET ? PORT_SEAL_COUNT : 0)) {
        return 0;
    }
    SDL_LockMutex(sLock);
    if (channel == PORT_SEAL_PACKET) {
        for (i = 0; i < 8; i++) {
            count |= (unsigned long long)in[i] << (8 * i);
        }
        in += PORT_SEAL_COUNT;
        len -= PORT_SEAL_COUNT;
        if (count < sIn[channel].count) { /* not newer than the last: a repeat, or late */
            SDL_UnlockMutex(sLock);
            return 0;
        }
    } else {
        count = sIn[channel].count;
    }
    body = len - PORT_SEAL_TAG;
    nonce(iv, count);
    ok = mbedtls_chachapoly_auth_decrypt(&sIn[channel].aead, body, iv, NULL, 0, in + body, in, out) == 0;
    if (ok) {
        sIn[channel].count = count + 1;
        *outLen = body;
    }
    SDL_UnlockMutex(sLock);
    return ok;
}

/* --- the watchers' channel: host to watchers only, its key from the whole code ---
 * One message sealed once reaches every watcher (the relay copies it), so it
 * can't have a key per watcher, and watchers send nothing (no J-PAKE). The key
 * is PBKDF2 (SHA-256, many rounds) of the whole code: slow to guess, but a
 * relay that keeps these messages could try the secret half's 31^4 values
 * offline, which J-PAKE doesn't let it do with the players' own messages
 * (docs/ONLINE_RELAY.md). The nonce: this program's random 4 bytes (a host
 * started again keeps the code) and a count, both in front; a watcher takes a
 * count only newer than the last under the same 4 bytes. Seats (players 3 and
 * 4) send their controller to the host on it too, each under its own 4 bytes:
 * the last count is kept for each sender (a few), and one under this side's
 * own 4 bytes (its message sent back) is never taken. */
#define WATCH_ROUNDS 200000
static mbedtls_chachapoly_context sWatchAead;
static int sWatchReady;
static unsigned char sWatchSalt[4], sWatchCode[16];
static unsigned long long sWatchCount;
#define WATCH_SENDERS 8
static struct {
    unsigned char salt[4];
    unsigned long long last;
    int heard;
} sWatchFrom[WATCH_SENDERS];
static int sWatchNextFrom;

void portWatchReset(void) {
    if (sWatchReady) {
        mbedtls_chachapoly_free(&sWatchAead);
    }
    sWatchReady = 0;
    memset(sWatchFrom, 0, sizeof(sWatchFrom));
    sWatchNextFrom = 0;
    sWatchCode[0] = '\0';
}

int portWatchKey(const char* code) {
    unsigned char key[32];
    if (sWatchReady && strcmp((const char*)sWatchCode, code) == 0) {
        return 1; /* (the same code: its key already) */
    }
    portWatchReset();
    if (!rng() || strlen(code) >= sizeof(sWatchCode)) {
        return 0;
    }
    if (mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char*)code, strlen(code),
                                      (const unsigned char*)"Paper Cut online watching", 25, WATCH_ROUNDS,
                                      sizeof(key), key) != 0) {
        return 0;
    }
    mbedtls_chachapoly_init(&sWatchAead);
    mbedtls_chachapoly_setkey(&sWatchAead, key);
    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_ctr_drbg_random(&sDrbg, sWatchSalt, sizeof(sWatchSalt));
    sWatchCount = 0;
    snprintf((char*)sWatchCode, sizeof(sWatchCode), "%s", code);
    sWatchReady = 1;
    return 1;
}

size_t portWatchSeal(const unsigned char* in, size_t len, unsigned char* out) {
    unsigned char iv[12];
    int i;
    if (!sWatchReady) {
        return 0;
    }
    memcpy(iv, sWatchSalt, 4);
    for (i = 0; i < 8; i++) {
        iv[4 + i] = (unsigned char)(sWatchCount >> (8 * i));
    }
    sWatchCount++;
    memcpy(out, iv, 12);
    mbedtls_chachapoly_encrypt_and_tag(&sWatchAead, len, iv, NULL, 0, in, out + 12, out + 12 + len);
    return len + PORT_WATCH_OVERHEAD;
}

int portWatchOpen(const unsigned char* in, size_t len, unsigned char* out, size_t* outLen) {
    unsigned long long count = 0;
    size_t body;
    int i, from = -1;
    if (!sWatchReady || len < PORT_WATCH_OVERHEAD || memcmp(in, sWatchSalt, 4) == 0) {
        return 0; /* (this side's own, back) */
    }
    for (i = 0; i < 8; i++) {
        count |= (unsigned long long)in[4 + i] << (8 * i);
    }
    for (i = 0; i < WATCH_SENDERS; i++) {
        if (sWatchFrom[i].heard && memcmp(in, sWatchFrom[i].salt, 4) == 0) {
            from = i;
        }
    }
    if (from >= 0 && count <= sWatchFrom[from].last) {
        return 0; /* not newer than the last: a repeat */
    }
    body = len - PORT_WATCH_OVERHEAD;
    if (mbedtls_chachapoly_auth_decrypt(&sWatchAead, body, in, NULL, 0, in + 12 + body, in + 12, out) != 0) {
        return 0;
    }
    if (from < 0) { /* a sender not heard before (the oldest one's place) */
        from = sWatchNextFrom;
        sWatchNextFrom = (sWatchNextFrom + 1) % WATCH_SENDERS;
        memcpy(sWatchFrom[from].salt, in, 4);
        sWatchFrom[from].heard = 1;
    }
    sWatchFrom[from].last = count;
    *outLen = body;
    return 1;
}

#endif
