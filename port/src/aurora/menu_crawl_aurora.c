/* Tests: the port's menus walked through by themselves (port/include/port/menu_crawl.h). */
#include "port/menu_crawl.h"
#include "port/platform.h"
#include "port/text_fit.h"
#include "win/win_main.h"
#include <demo/DEMOPad.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sEnding = -1; /* frames to the end, once done */

const char* portMenuCrawl(void) {
    static int sRead;
    static const char* sMode;
    if (!sRead) {
        const char* e = getenv("TTYD_MENU_CRAWL");
        sRead = 1;
        sMode = e == NULL || e[0] == '\0' || strcmp(e, "0") == 0 ? NULL : strcmp(e, "online") == 0 ? "online"
                                                                                                    : "settings";
    }
    return sMode;
}

void portMenuCrawlShot(const char* name) {
    char clean[96];
    size_t i;
    for (i = 0; name[i] != '\0' && i < sizeof(clean) - 1; i++) {
        const char c = name[i];
        clean[i] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'
                       ? c
                       : '_';
    }
    clean[i] = '\0';
    printf("[crawl] shot %s\n", clean);
    portShotRequest(clean);
}

int portMenuCrawlPad(WindowWork* wp, const char* name, int downs) {
    static const char* sName;
    static int sK, sWait, sShot, sN;
    char shot[96];
    wp->buttonTrg = 0;
    wp->dirRep = 0;
    if (name != sName) {
        sName = name;
        sK = 0;
        sWait = 30; /* (its slide in) */
        sShot = 1;
        sN = 0;
    }
    if (sWait > 0) {
        sWait--;
        return 1;
    }
    if (sShot) {
        snprintf(shot, sizeof(shot), "%s_%02d", name, sN++);
        portMenuCrawlShot(shot);
        sShot = 0;
        sWait = 10;
        return 1;
    }
    if (sK < downs) {
        wp->dirRep = DEMO_STICK_DOWN;
        sK++;
        sShot = sK % 6 == 0;
        sWait = sShot ? 24 : 8;
        return 1;
    }
    sName = NULL;
    return 0;
}

void portMenuCrawlDone(void) {
    if (sEnding < 0) {
        printf("[crawl] done\n");
        fflush(stdout);
        sEnding = 30; /* (the last picture written first) */
    } else if (--sEnding <= 0) {
        fflush(stdout);
        _Exit(0); /* (no clean-up: the renderer's threads still run) */
    }
}
