/* Tests: the port's 2D boxes checked for overlaps (port/include/port/ui_boxes.h). */
#include "port/ui_boxes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOXES 256
#define SEEN 1024 /* (a power of two) */
#define SLACK 3.0f /* (edges and shadows touching: not an overlap) */

static int sOn = -1;
static struct {
    char name[48];
    float x0, y0, x1, y1;
} sBox[BOXES];
static int sCount;
static unsigned sSeen[SEEN]; /* the pairs logged (their names' hash; 0 free) */
static int sFullSaid;        /* a table ran out: said once, as an overlap line, so the check fails loudly */

/* (the "[ui-overlap]" tag: a scenario's log_must_not sees it, never a silent pass) */
static void full(int bit, const char* what) {
    if (!(sFullSaid & bit)) {
        sFullSaid |= bit;
        printf("[ui-overlap] %s: the rest are not checked\n", what);
        fflush(stdout);
    }
}

int portUiBoxOn(void) {
    if (sOn < 0) {
        const char* v = getenv("TTYD_UI_OVERLAP");
        sOn = v != NULL && atoi(v) != 0;
        if (sOn) {
            printf("[ui-boxes] overlap check on\n");
            fflush(stdout);
        }
    }
    return sOn;
}

void portUiBox(const char* name, float x0, float y0, float x1, float y1) {
    if (!portUiBoxOn() || name == NULL) {
        return;
    }
    if (sCount >= BOXES) {
        full(1, "more than 256 boxes in a frame");
        return;
    }
    snprintf(sBox[sCount].name, sizeof(sBox[sCount].name), "%s", name);
    sBox[sCount].x0 = x0 < x1 ? x0 : x1;
    sBox[sCount].x1 = x0 < x1 ? x1 : x0;
    sBox[sCount].y0 = y0 < y1 ? y0 : y1;
    sBox[sCount].y1 = y0 < y1 ? y1 : y0;
    sCount++;
}

/* the group: the name up to its ':' */
static int sameGroup(const char* a, const char* b) {
    const char* ca = strchr(a, ':');
    const char* cb = strchr(b, ':');
    const size_t la = ca != NULL ? (size_t)(ca - a) : strlen(a);
    const size_t lb = cb != NULL ? (size_t)(cb - b) : strlen(b);
    return la == lb && strncmp(a, b, la) == 0;
}

static int firstTime(const char* a, const char* b) {
    unsigned h = 2166136261u, i;
    const char* s;
    for (s = a; *s != '\0'; s++) {
        h = (h ^ (unsigned char)*s) * 16777619u;
    }
    h = (h ^ '|') * 16777619u;
    for (s = b; *s != '\0'; s++) {
        h = (h ^ (unsigned char)*s) * 16777619u;
    }
    h |= 1u;
    for (i = 0; i < SEEN; i++) {
        const unsigned k = (h + i) & (SEEN - 1);
        if (sSeen[k] == h) {
            return 0;
        }
        if (sSeen[k] == 0) {
            sSeen[k] = h;
            return 1;
        }
    }
    full(2, "more than 1024 overlapping pairs");
    return 0;
}

void portUiBoxFrame(void) {
    int i, j;
    if (!portUiBoxOn()) {
        return;
    }
    for (i = 0; i < sCount; i++) {
        for (j = i + 1; j < sCount; j++) {
            const float w = (sBox[i].x1 < sBox[j].x1 ? sBox[i].x1 : sBox[j].x1) -
                            (sBox[i].x0 > sBox[j].x0 ? sBox[i].x0 : sBox[j].x0);
            const float h = (sBox[i].y1 < sBox[j].y1 ? sBox[i].y1 : sBox[j].y1) -
                            (sBox[i].y0 > sBox[j].y0 ? sBox[i].y0 : sBox[j].y0);
            if (w <= SLACK || h <= SLACK || sameGroup(sBox[i].name, sBox[j].name) ||
                !firstTime(sBox[i].name, sBox[j].name)) {
                continue;
            }
            printf("[ui-overlap] %s over %s (%.0f x %.0f)\n", sBox[j].name, sBox[i].name, w, h);
            fflush(stdout);
        }
    }
    sCount = 0;
}
