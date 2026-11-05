/* Shared text codes (port/include/port/code64.h). */
#include "port/code64.h"

#include <string.h>

static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int portCode64Encode(const unsigned char* in, int n, char* out, int size) {
    unsigned v = 0;
    int bits = 0, len = 0, i;
    for (i = 0; i < n; i++) {
        v = (v << 8) | in[i];
        bits += 8;
        while (bits >= 6) {
            if (len + 1 >= size) {
                return -1;
            }
            out[len++] = kAlphabet[(v >> (bits - 6)) & 63];
            bits -= 6;
        }
    }
    if (bits > 0) {
        if (len + 1 >= size) {
            return -1;
        }
        out[len++] = kAlphabet[(v << (6 - bits)) & 63];
    }
    out[len] = 0;
    return len;
}

int portCode64Decode(const char* in, unsigned char* out, int size) {
    unsigned v = 0;
    int bits = 0, len = 0;
    for (; *in; in++) {
        const char* at = strchr(kAlphabet, *in);
        if (at == NULL) {
            if (*in == ' ' || *in == '\n' || *in == '\r' || *in == '\t') {
                continue;
            }
            break; /* the code ends here */
        }
        v = (v << 6) | (unsigned)(at - kAlphabet);
        bits += 6;
        if (bits >= 8) {
            if (len + 1 >= size) {
                return -1;
            }
            out[len++] = (unsigned char)((v >> (bits - 8)) & 0xFF);
            bits -= 8;
        }
    }
    out[len] = 0;
    return len;
}
