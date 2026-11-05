/* Paper Cut's own text in the game's font: which texts are Paper Cut's, and
 * the game's random numbers put back around them (port/include/port/port_text.h). */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "port/platform.h"
#include "port/port_text.h"

/* The texts, by where they start (sorted, none overlapping), with their
 * length and a hash of their letters: memory a text was in may later hold
 * another (a buffer written again, a freed one used again), and only the
 * same letters count. */
typedef struct OwnText {
    const char* start;
    u32 len;
    u32 hash;
} OwnText;

#define OWN_MAX 0x10000 /* (more, and the list starts again: what is drawn is given again) */

static OwnText* sOwn;
static int sCount;
static int sCap;
static volatile char sLock; /* portTrGame can be asked off the game's thread */

static u32 hashOf(const char* s, u32 len) {
    u32 h = 2166136261u;
    u32 i;
    for (i = 0; i < len; i++) {
        h = (h ^ (u8)s[i]) * 16777619u;
    }
    return h;
}

static void lock(void) {
    while (__atomic_test_and_set(&sLock, __ATOMIC_ACQUIRE)) {
    }
}

static void unlock(void) {
    __atomic_clear(&sLock, __ATOMIC_RELEASE);
}

/* the last text starting at or before p (-1: none) */
static int find(const char* p) {
    int lo = 0, hi = sCount - 1, at = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (sOwn[mid].start <= p) {
            at = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return at;
}

const char* portTextOwn(const char* text) {
    size_t n;
    u32 len, hash;
    const char* end;
    int at, first, last;

    if (text == NULL || text[0] == '\0') {
        return text;
    }
    n = strlen(text);
    len = n > 0xFFFFFFu ? 0xFFFFFFu : (u32)n;
    hash = hashOf(text, len);
    end = text + len;
    lock();
    at = find(text);
    if (at >= 0 && sOwn[at].start == text && sOwn[at].len == len && sOwn[at].hash == hash) {
        unlock();
        return text; /* (as it was) */
    }
    /* what it overlaps goes: [first, last) */
    first = at >= 0 && sOwn[at].start + sOwn[at].len > text ? at : at + 1;
    last = at + 1;
    while (last < sCount && sOwn[last].start < end) {
        last++;
    }
    if (last - first != 1) {
        if (sCount - (last - first) + 1 > sCap) {
            int cap = sCap != 0 ? sCap * 2 : 256;
            OwnText* grown;
            if (cap > OWN_MAX) {
                cap = OWN_MAX;
            }
            if (sCount - (last - first) + 1 > cap) {
                sCount = 0; /* full: start again */
                first = last = 0;
            } else {
                grown = (OwnText*)realloc(sOwn, sizeof(OwnText) * cap);
                if (grown == NULL) {
                    unlock();
                    return text;
                }
                sOwn = grown;
                sCap = cap;
            }
        }
        memmove(&sOwn[first + 1], &sOwn[last], sizeof(OwnText) * (sCount - last));
        sCount += 1 - (last - first);
    }
    sOwn[first].start = text;
    sOwn[first].len = len;
    sOwn[first].hash = hash;
    unlock();
    return text;
}

int portTextIsOwn(const char* text) {
    int at, own = 0;
    if (text == NULL || sCount == 0) {
        return 0;
    }
    lock();
    at = find(text);
    if (at >= 0 && text < sOwn[at].start + sOwn[at].len) {
        const OwnText* t = &sOwn[at];
        own = memchr(t->start, '\0', t->len) == NULL && t->start[t->len] == '\0' && hashOf(t->start, t->len) == t->hash;
    }
    unlock();
    return own;
}

void portTextCopied(const char* copy, const char* a, const char* b) {
    if (copy != NULL && (portTextIsOwn(a) || portTextIsOwn(b))) {
        portTextOwn(copy);
    }
}

int portTextFormatOwn(const char* fmt, va_list args) {
    va_list ap;
    const char* f;
    int own;
    if (fmt == NULL) {
        return 0;
    }
    if (portTextIsOwn(fmt)) {
        return 1;
    }
    if (strchr(fmt, '%') == NULL || strchr(fmt, 's') == NULL) {
        return 0; /* (no string in it) */
    }
    own = 0;
    va_copy(ap, args);
    for (f = fmt; *f != '\0' && !own; f++) {
        int longs = 0;
        if (*f != '%') {
            continue;
        }
        f++;
        if (*f == '%') {
            continue;
        }
        while (*f != '\0' && strchr("-+ #0", *f) != NULL) {
            f++;
        }
        for (; *f == '*' || (*f >= '0' && *f <= '9') || *f == '.'; f++) {
            if (*f == '*') {
                (void)va_arg(ap, int);
            }
        }
        for (; *f != '\0' && strchr("hlLqjzt", *f) != NULL; f++) {
            longs += *f == 'l' || *f == 'q' || *f == 'j' || *f == 'z' || *f == 't';
        }
        switch (*f) {
            case 's':
                own = portTextIsOwn(va_arg(ap, const char*));
                break;
            case 'p':
            case 'n':
                (void)va_arg(ap, void*);
                break;
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a':
            case 'A':
                (void)va_arg(ap, double); /* (the game passes no long double) */
                break;
            case '\0':
                f--;
                break;
            default: /* d i u o x X c */
                if (longs >= 2 || (longs == 1 && sizeof(long) == 8)) {
                    (void)va_arg(ap, long long);
                } else {
                    (void)va_arg(ap, int);
                }
                break;
        }
    }
    va_end(ap);
    return own;
}

static int sDepth;
static u32 sSeed;

void portTextBegin(void) {
    if (sDepth++ == 0) {
        sSeed = portRandState();
    }
}

void portTextEnd(void) {
    if (sDepth > 0 && --sDepth == 0) {
        portRandSetState(sSeed);
    }
}

int portTextDrawing(void) {
    return sDepth > 0;
}
