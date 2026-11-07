/* Tests: the port's text measured against its boxes (port/include/port/text_fit.h). */
#include "port/text_fit.h"
#include "port/tr.h"
#include "manager/fontmgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sOn = -1;
static FILE* sFile;
static char sScreen[96] = "?";

#define SEEN 16384
static unsigned sSeen[SEEN]; /* hashes of the lines written (0: free) */

/* the translations seen: shown -> english, by the shown text's hash */
#define KEYS 8192
static struct {
    unsigned h;
    const char* shown;
    const char* english;
} sKeys[KEYS];

static unsigned hash(unsigned h, const char* s) {
    for (; s != NULL && *s != '\0'; s++) {
        h = (h ^ (unsigned char)*s) * 16777619u;
    }
    return h;
}

int portTextFitOn(void) {
    if (sOn < 0) {
        const char* path = getenv("TTYD_TEXT_FIT");
        sOn = path != NULL && path[0] != '\0';
        if (sOn) {
            sFile = fopen(path, "ab");
            sOn = sFile != NULL;
            if (sOn) {
                printf("[fit] text fit log: %s\n", path);
            }
        }
    }
    return sOn;
}

void portTextFitScreen(const char* screen) {
    if (portTextFitOn() && screen != NULL) {
        snprintf(sScreen, sizeof(sScreen), "%s", screen);
    }
}

void portTextFitKey(const char* english, const char* shown) {
    unsigned h, i;
    if (!portTextFitOn() || english == NULL || shown == NULL) {
        return;
    }
    h = hash(2166136261u, shown) | 1u;
    for (i = 0; i < KEYS; i++) {
        const unsigned k = (h + i) & (KEYS - 1);
        if (sKeys[k].h == 0) {
            sKeys[k].h = h;
            sKeys[k].shown = shown;
            sKeys[k].english = english;
            return;
        }
        if (sKeys[k].h == h && strcmp(sKeys[k].shown, shown) == 0) {
            return;
        }
    }
}

/* the English text drawn's came from: the whole of it, else a translation its first line is in */
static const char* keyOf(const char* text) {
    const unsigned h = hash(2166136261u, text) | 1u;
    char first[25];
    size_t n = 0;
    unsigned i;
    for (i = 0; i < KEYS; i++) {
        const unsigned k = (h + i) & (KEYS - 1);
        if (sKeys[k].h == 0) {
            break;
        }
        if (sKeys[k].h == h && strcmp(sKeys[k].shown, text) == 0) {
            return sKeys[k].english;
        }
    }
    while (text[n] != '\0' && text[n] != '\n' && n < sizeof(first) - 1) {
        first[n] = text[n];
        n++;
    }
    first[n] = '\0';
    if (n < 4) {
        return "?";
    }
    for (i = 0; i < KEYS; i++) {
        if (sKeys[i].h != 0 && strstr(sKeys[i].shown, first) != NULL) {
            return sKeys[i].english;
        }
    }
    return "?";
}

static const char* langCode(void) {
#ifdef TTYD_BACKEND_AURORA
    static const char* const kCodes[] = {"en", "de", "fr", "es", "it", "ja"};
    const int l = portTrLanguage();
    return l >= 0 && l < 6 ? kCodes[l] : "pack";
#else
    return "en";
#endif
}

/* tabs and breaks shown as spaces and "|", so a line is a line */
static void put(const char* s) {
    for (; *s != '\0'; s++) {
        fputc(*s == '\t' ? ' ' : *s == '\n' ? '|' : *s, sFile);
    }
}

static void line(const char* kind, const char* where, const char* text, float width, float room) {
    unsigned h = hash(hash(hash(2166136261u, kind), where), text), i;
    h = hash(h, sScreen) | 1u;
    for (i = 0; i < SEEN; i++) {
        const unsigned k = (h + i) & (SEEN - 1);
        if (sSeen[k] == h) {
            return; /* written already */
        }
        if (sSeen[k] == 0) {
            sSeen[k] = h;
            break;
        }
    }
    fprintf(sFile, "%s\t%s\t%s\t", kind, langCode(), PORT_GAME_ID);
    put(sScreen);
    fprintf(sFile, "\t%s\t%.2f\t%.0f\t%.0f\t", where, width > 0.0f ? room / width : 0.0f, width, room);
    put(keyOf(text));
    fputc('\t', sFile);
    put(text);
    fputc('\n', sFile);
    fflush(sFile);
}

void portTextFit(const char* where, const char* text, float width, float room, int shrunk) {
    if (!portTextFitOn() || text == NULL || text[0] == '\0' || width <= room + 0.5f) {
        return;
    }
    if (shrunk && room / width >= 0.8f) {
        return; /* a little narrower: as meant */
    }
    line(shrunk ? "squeeze" : "over", where, text, width, room);
}

void portTextFitLines(const char* where, const char* text, int lines, int most) {
    if (!portTextFitOn() || text == NULL || lines <= most) {
        return;
    }
    line("lines", where, text, (float)lines, (float)most);
}

void portTextFitHelp(const char* tag, const char* text) {
    char one[256];
    if (!portTextFitOn() || tag == NULL || text == NULL || strncmp(tag, "pc_", 3) != 0) {
        return; /* (the port's own notes: their tags are "pc_...") */
    }
    while (*text != '\0') {
        size_t n = 0;
        while (text[n] != '\0' && text[n] != '\n') {
            n++;
        }
        if (n > 0 && n < sizeof(one)) {
            memcpy(one, text, n);
            one[n] = '\0';
            /* the help box's text: from about 18 in to its arrows, of its 500 */
            portTextFit("help-box", one, (float)FontGetMessageWidth(one), 460.0f, 0);
        }
        text += n;
        if (*text == '\n') {
            text++;
        }
    }
}

void portTextFitEnglish(const char* english, int translated) {
    if (portTextFitOn() && english != NULL && english[0] != '\0') {
        line("english", translated ? "font-lacks" : "untranslated", english, 0.0f, 0.0f);
    }
}
