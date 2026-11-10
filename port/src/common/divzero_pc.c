/* Integer division by zero, as the GameCube computes it.
 *
 * PowerPC's divw/divwu never trap: divided by zero (or INT_MIN by -1, for
 * divw) the quotient is 0, or -1 for a negative signed dividend (what Dolphin's
 * interpreter, matching the hardware, returns), and the compiled remainder
 * (divw, mullw, subf: a - q * b) is then the dividend. The game relies on it
 * (eff_stardust_n64's rand() % 0 for a zero-radius burst; the X-Naut platoon's
 * pose step / 0). ARM64 does not trap either (quotient 0, remainder the
 * dividend); x86-64 raises an exception, so its handler (crash_pc.c: Windows'
 * vectored handler, SIGFPE on Linux and macOS) calls this, which decodes the
 * faulting div/idiv, writes the PowerPC result into rax/rdx and steps over it.
 * There are thousands of non-constant divisions in the game: this covers them
 * all, as the hardware does. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "port/platform.h"

#if defined(__x86_64__) || defined(_M_X64)
/* Length of the div/idiv r/m instruction at p (0 if it is not one), its operand
 * size in bits and whether it is signed (idiv). */
static int decodeDivide(const uint8_t* p, int* bits, int* isSigned) {
    int n = 0, rexW = 0, operand16 = 0;
    uint8_t modrm, mod, rm;
    for (;;) {
        uint8_t b = p[n];
        if (b == 0x66) {
            operand16 = 1;
        } else if (!(b == 0x67 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65 ||
                     b == 0xF0 || b == 0xF2 || b == 0xF3)) {
            break;
        }
        if (++n > 14) {
            return 0;
        }
    }
    if ((p[n] & 0xF0) == 0x40) { /* REX */
        rexW = p[n] & 8;
        n++;
    }
    if (p[n] != 0xF7) { /* F6 (8-bit) is never emitted for C's int arithmetic */
        return 0;
    }
    n++;
    modrm = p[n++];
    mod = modrm >> 6;
    rm = modrm & 7;
    if (((modrm >> 3) & 7) != 6 && ((modrm >> 3) & 7) != 7) {
        return 0;
    }
    if (mod != 3) {
        if (rm == 4) { /* SIB */
            uint8_t sib = p[n++];
            if (mod == 0 && (sib & 7) == 5) {
                n += 4;
            }
        } else if (mod == 0 && rm == 5) { /* rip-relative */
            n += 4;
        }
        n += mod == 1 ? 1 : mod == 2 ? 4 : 0;
    }
    *bits = rexW ? 64 : operand16 ? 16 : 32;
    *isSigned = ((modrm >> 3) & 7) == 7;
    return n;
}

static void traceSite(uint64_t ip) {
    static uint64_t sSeen[64];
    static int sCount, sTrace = -1;
    char name[160];
    int i;
    if (sTrace < 0) {
        sTrace = getenv("TTYD_TRACE") != NULL;
    }
    if (!sTrace) {
        return;
    }
    for (i = 0; i < sCount; i++) {
        if (sSeen[i] == ip) {
            return;
        }
    }
    if (sCount < 64) {
        sSeen[sCount++] = ip;
    }
    printf("[divzero] %s: division by zero, the PowerPC result\n", portSymbolName((void*)(uintptr_t)ip, name, sizeof(name)));
    fflush(stdout);
}

int portEmulateIntDivide(unsigned long long* rax, unsigned long long* rdx, unsigned long long* rip, int overflow) {
    int bits, isSigned;
    int len = decodeDivide((const uint8_t*)(uintptr_t)*rip, &bits, &isSigned);
    if (len == 0 || (overflow && !isSigned)) {
        return 0;
    }
    traceSite(*rip);
    if (bits == 64) {
        int64_t a = (int64_t)*rax;
        *rax = isSigned && a < 0 ? UINT64_MAX : 0;
        *rdx = overflow ? (uint64_t)a - 1u : (uint64_t)a; /* a - q * b (unsigned: INT64_MIN - 1 wraps, defined) */
    } else if (bits == 32) {
        int32_t a = (int32_t)(uint32_t)*rax;
        *rax = isSigned && a < 0 ? 0xFFFFFFFFu : 0; /* 32-bit writes clear the top half */
        *rdx = overflow ? (uint32_t)a - 1u : (uint32_t)a;
    } else {
        int16_t a = (int16_t)(uint16_t)*rax;
        *rax = (*rax & ~(uint64_t)0xFFFF) | (isSigned && a < 0 ? 0xFFFFu : 0);
        *rdx = (*rdx & ~(uint64_t)0xFFFF) | (uint16_t)(overflow ? (uint16_t)a - 1u : (uint16_t)a);
    }
    *rip += (uint64_t)len;
    return 1;
}
#else
int portEmulateIntDivide(unsigned long long* rax, unsigned long long* rdx, unsigned long long* rip, int overflow) {
    (void)rax;
    (void)rdx;
    (void)rip;
    (void)overflow;
    return 0; /* only x86-64 traps */
}
#endif
