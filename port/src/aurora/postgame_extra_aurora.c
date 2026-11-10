/* More after the story (port/include/port/postgame_extra.h): the pieces'
 * start-up and frame, and the game messages they add, in the menu language
 * (English online and in replays, so every game of a session reads alike).
 * From postgame_aurora.c's portPostgameInit. */
#include "port/statesync.h"
#include "port/postgame_extra.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/tr.h"

#include <stdio.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

/* --- texts ---------------------------------------------------------------- */

#define TEXT_LISTS 16
static struct {
    const PortPgText* texts;
    int count;
} sLists[TEXT_LISTS];
static int sListCount;
static int sTextsLang = -2, sTextsOnline = -1;

const char* portPgTr(const char* english) {
    if (portStateSyncLinked() || portReplayActive() || (portTrLanguage() == 5 && PORT_GAME_ID[3] != 'J')) {
        return english;
    }
    return portTr(english);
}

static void setOne(const PortPgText* t) {
    static char sChoice[512];
    if (t->kind == 1) {
        portModsSetMessage(t->tag, portGameLayout(portPgTr(t->english), 30, 3));
    } else if (t->kind == 2) {
        /* "<select default cancel width height>" and an option a line: the options split by '|' */
        char opts[8][64];
        const char* p = t->english;
        int n = 0, i, widest = 0, at;
        while (*p != '\0' && n < 8) {
            const char* bar = strchr(p, '|');
            const size_t len = bar != NULL ? (size_t)(bar - p) : strlen(p);
            char en[64];
            snprintf(en, sizeof(en), "%.*s", (int)len, p);
            snprintf(opts[n], sizeof(opts[n]), "%s", portPgTr(en));
            if ((int)strlen(opts[n]) > widest) {
                widest = (int)strlen(opts[n]);
            }
            n++;
            p = bar != NULL ? bar + 1 : p + len;
        }
        at = snprintf(sChoice, sizeof(sChoice), "<select 0 %d %d %d>", n - 1, 60 + widest * 13 > 400 ? 400 : 60 + widest * 13,
                      n * 35 > 140 ? 140 : n * 35); /* (top at most 140: higher, a long list covers the question) */
        for (i = 0; i < n && at < (int)sizeof(sChoice); i++) {
            at += snprintf(sChoice + at, sizeof(sChoice) - (size_t)at, "\n%s", opts[i]);
        }
        portModsSetMessage(t->tag, sChoice);
    } else {
        portModsSetMessage(t->tag, portPgTr(t->english));
    }
}

static void setAll(void) {
    int l, i;
    for (l = 0; l < sListCount; l++) {
        for (i = 0; i < sLists[l].count; i++) {
            setOne(&sLists[l].texts[i]);
        }
    }
}

void portPgAddTexts(const PortPgText* texts, int count) {
    if (sListCount < TEXT_LISTS) {
        sLists[sListCount].texts = texts;
        sLists[sListCount].count = count;
        sListCount++;
        sTextsLang = -2; /* set at the next frame */
    }
}

/* --- the frame ------------------------------------------------------------- */

static void frameHook(void) {
    const int lang = portTrLanguage();
    const int online = portStateSyncLinked() || portReplayActive();
    if (lang != sTextsLang || online != sTextsOnline) {
        sTextsLang = lang;
        sTextsOnline = online;
        setAll();
    }
    portPitWhackaFrame();
    portExhibitionFrame();
    portPgMoreFrame();
    portRestorationsFrame();
}

void portPostgameExtraInit(void) {
    static int sDone;
    if (sDone) {
        return;
    }
    sDone = 1;
    portPitWhackaRegister();
    portExhibitionRegister();
    portNpcTattlesRegister(); /* (Goombella's Tattles of our NPCs: everyone's, not only the post-game's) */
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
}
