/* The speedrun timer's splits kept in LiveSplit's own format
 * (port/include/port/practice.h): each attempt that ends updates
 * <user>/splits/<category>.lss, which LiveSplit opens as it is (Open
 * Splits): the segments (Any%'s chapters, or splits.txt's), the personal
 * best's split times, the best segments and the attempt count, in game time
 * (load-free) and real time. The numbers themselves are kept beside it in
 * <category>.txt, read again at each attempt; the .lss is written from them.
 * "Every story step" has no fixed segments: nothing is kept for it. */
#include "port/file_pc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/file_write.h"
#include "port/practice.h"

#define makeDir(p) portMkdir(p) /* (UTF-8 paths: port/file_pc.h) */

#define SEG_MAX 512
#define STEPS_CATEGORY 1 /* practice_pc.c's "Every story step" */

typedef struct Kept {
    int attempts, count;
    char names[SEG_MAX][48];
    double pbGame[SEG_MAX], pbReal[SEG_MAX];     /* the personal best's split times (cumulative), <0 none */
    double goldGame[SEG_MAX], goldReal[SEG_MAX]; /* the best time of each segment alone, <0 none */
} Kept;

static Kept sKept, sBestKept;
static char sPath[1024];

static void fileBase(char* out, size_t size) {
    char name[64];
    const char* cat = portSplitCategoryName(portSplitCategory());
    int n = 0;
    for (; *cat != '\0' && n < (int)sizeof(name) - 1; cat++) {
        const char c = *cat;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '%' || c == ' ' ||
            c == '-') {
            name[n++] = c;
        }
    }
    while (n > 0 && name[n - 1] == ' ') {
        n--;
    }
    name[n] = '\0';
    snprintf(out, size, "%s/splits/%s", portPracticeUserDir(), n > 0 ? name : "splits");
}

const char* portSplitsFilePath(void) {
    char base[1000];
    fileBase(base, sizeof(base));
    snprintf(sPath, sizeof(sPath), "%s.lss", base);
    return sPath;
}

/* the kept numbers for the category now; a list of other splits starts again */
static void readKept(Kept* k) {
    char base[1000], path[1024], line[256];
    FILE* f;
    int i, n = portSplitCount();
    memset(k, 0, sizeof(*k));
    k->count = n > SEG_MAX ? SEG_MAX : n;
    for (i = 0; i < k->count; i++) {
        snprintf(k->names[i], sizeof(k->names[i]), "%s", portSplitName(i));
        k->pbGame[i] = k->pbReal[i] = k->goldGame[i] = k->goldReal[i] = -1.0;
    }
    fileBase(base, sizeof(base));
    snprintf(path, sizeof(path), "%s.txt", base);
    f = portFopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        int at;
        double a, b, c, d;
        char name[48];
        if (sscanf(line, "attempts %d", &at) == 1) {
            k->attempts = at;
        } else if (sscanf(line, "seg %d %lf %lf %lf %lf %47[^\r\n]", &at, &a, &b, &c, &d, name) == 6 && at >= 0 &&
                   at < k->count) {
            if (strcmp(name, k->names[at]) != 0) { /* the splits changed: from scratch */
                for (i = 0; i < k->count; i++) {
                    k->pbGame[i] = k->pbReal[i] = k->goldGame[i] = k->goldReal[i] = -1.0;
                }
                break;
            }
            k->pbGame[at] = a;
            k->pbReal[at] = b;
            k->goldGame[at] = c;
            k->goldReal[at] = d;
        }
    }
    fclose(f);
}

/* LiveSplit's TimeSpan: hh:mm:ss.fffffff */
static void span(FILE* f, const char* tag, double seconds) {
    long long ticks;
    if (seconds < 0.0) {
        return;
    }
    ticks = (long long)(seconds * 10000000.0 + 0.5);
    fprintf(f, "<%s>%02lld:%02lld:%02lld.%07lld</%s>", tag, ticks / 36000000000LL, ticks / 600000000LL % 60,
            ticks / 10000000LL % 60, ticks % 10000000LL, tag);
}

static void xmlText(FILE* f, const char* s) {
    for (; *s != '\0'; s++) {
        switch (*s) {
        case '&': fputs("&amp;", f); break;
        case '<': fputs("&lt;", f); break;
        case '>': fputs("&gt;", f); break;
        case '"': fputs("&quot;", f); break;
        default: fputc(*s, f); break;
        }
    }
}

static void writeAll(void) {
    char base[1000], path[1024];
    FILE* f;
    int i;
    snprintf(path, sizeof(path), "%s/splits", portPracticeUserDir());
    makeDir(path);
    fileBase(base, sizeof(base));
    snprintf(path, sizeof(path), "%s.txt", base);
    f = portFileWriteOpen(path, "w");
    if (f != NULL) {
        fprintf(f, "# Paper Cut's speedrun splits (the .lss beside it is written from these)\nattempts %d\n",
                sKept.attempts);
        for (i = 0; i < sKept.count; i++) {
            fprintf(f, "seg %d %.4f %.4f %.4f %.4f %s\n", i, sKept.pbGame[i], sKept.pbReal[i], sKept.goldGame[i],
                    sKept.goldReal[i], sKept.names[i]);
        }
        portFileWriteClose(f, path);
    }
    f = portFileWriteOpen(portSplitsFilePath(), "w");
    if (f == NULL) {
        return;
    }
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Run version=\"1.7.0\">\n  <GameIcon />\n", f);
    fputs("  <GameName>Paper Mario: The Thousand-Year Door</GameName>\n  <CategoryName>", f);
    xmlText(f, portSplitCategoryName(portSplitCategory()));
    fputs("</CategoryName>\n  <LayoutPath>\n  </LayoutPath>\n", f);
    fputs("  <Metadata>\n    <Run id=\"\" />\n    <Platform usesEmulator=\"False\">\n    </Platform>\n"
          "    <Region>\n    </Region>\n    <Variables />\n  </Metadata>\n",
          f);
    fprintf(f, "  <Offset>00:00:00</Offset>\n  <AttemptCount>%d</AttemptCount>\n  <AttemptHistory />\n  <Segments>\n",
            sKept.attempts);
    for (i = 0; i < sKept.count; i++) {
        fputs("    <Segment>\n      <Name>", f);
        xmlText(f, sKept.names[i]);
        fputs("</Name>\n      <Icon />\n      <SplitTimes>\n        <SplitTime name=\"Personal Best\">", f);
        span(f, "RealTime", sKept.pbReal[i]);
        span(f, "GameTime", sKept.pbGame[i]);
        fputs("</SplitTime>\n      </SplitTimes>\n      <BestSegmentTime>", f);
        span(f, "RealTime", sKept.goldReal[i]);
        span(f, "GameTime", sKept.goldGame[i]);
        fputs("</BestSegmentTime>\n      <SegmentHistory />\n    </Segment>\n", f);
    }
    fputs("  </Segments>\n  <AutoSplitterSettings />\n</Run>\n", f);
    portFileWriteClose(f, portSplitsFilePath());
}

void portSplitsSaveRun(int finished) {
    int i;
    double prevGame = 0.0, prevReal = 0.0;
    const int n = portSplitCount();
    if (portSplitCategory() == STEPS_CATEGORY || n <= 0) {
        return;
    }
    readKept(&sKept);
    sKept.attempts++;
    /* the best segments: each made this attempt, against its gold */
    for (i = 0; i < sKept.count && portSplitTime(i) >= 0.0; i++) {
        const double g = portSplitTime(i) - prevGame, r = portSplitRealTime(i) - prevReal;
        if (sKept.goldGame[i] < 0.0 || g < sKept.goldGame[i]) {
            sKept.goldGame[i] = g;
        }
        if (sKept.goldReal[i] < 0.0 || r < sKept.goldReal[i]) {
            sKept.goldReal[i] = r;
        }
        prevGame = portSplitTime(i);
        prevReal = portSplitRealTime(i);
    }
    /* a finished run faster (in game time) than the best: the new best */
    if (finished && i == sKept.count && sKept.count > 0 &&
        (sKept.pbGame[sKept.count - 1] < 0.0 || portSplitTime(sKept.count - 1) < sKept.pbGame[sKept.count - 1])) {
        for (i = 0; i < sKept.count; i++) {
            sKept.pbGame[i] = portSplitTime(i);
            sKept.pbReal[i] = portSplitRealTime(i);
        }
        printf("[splits] a new personal best\n");
    }
    writeAll();
    printf("[splits] attempt %d kept in %s\n", sKept.attempts, portSplitsFilePath());
    fflush(stdout);
}

double portSplitsBest(void) {
    if (portSplitCategory() == STEPS_CATEGORY || portSplitCount() <= 0) {
        return -1.0;
    }
    readKept(&sBestKept); /* (the page's thread: a copy of its own) */
    return sBestKept.count > 0 ? sBestKept.pbGame[sBestKept.count - 1] : -1.0;
}
