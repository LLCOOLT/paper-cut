/* The disc image: the first argument, $TTYD_DISC, the one chosen last time
 * (disc.txt in Aurora's user folder), or one picked in the system's file
 * dialog. On Android the dialog gives a content:// URI, which Aurora's
 * SDL_IOFromFile opens like a path. A picked disc that Aurora can't read or
 * that is not this build's game is refused with a message box, and the dialog
 * comes back. */
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/dvd.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/dvd.h>
#include "port/aurora_pc.h"
#include "port/platform.h"

/* SDL's <stdbool.h> must not leave a bool macro behind for the game's headers. */
#undef bool

#define REGION (strcmp(PORT_GAME_ID, "G8ME01") == 0 ? "US" : "Japan")

/* Opens path; FALSE with the reason in why if it isn't this build's disc. */
static BOOL openDisc(const char* path, char* why, size_t size) {
    const DVDDiskID* id;
    char gid[6];
    if (!aurora_dvd_open(path)) {
        snprintf(why, size, "%s\n\nis not a GameCube disc image that can be read.", path);
        return FALSE;
    }
    id = DVDGetCurrentDiskID();
    memcpy(gid, id->gameName, 4);
    memcpy(gid + 4, id->company, 2);
    if (memcmp(gid, PORT_GAME_ID, 6) != 0) {
        snprintf(why, size, "%s\n\nis %.6s, but this build is for %s (Paper Mario: The Thousand-Year Door, %s).",
                 path, gid, PORT_GAME_ID, REGION);
        aurora_dvd_close();
        return FALSE;
    }
    return TRUE;
}

static char sPicked[4096];
static volatile int sPickDone;

static void SDLCALL onPicked(void* user, const char* const* files, int filter) {
    (void)user;
    (void)filter;
    sPicked[0] = '\0';
    if (files != NULL && files[0] != NULL) {
        snprintf(sPicked, sizeof(sPicked), "%s", files[0]);
    } else if (files == NULL) {
        fprintf(stderr, "file dialog: %s\n", SDL_GetError());
    }
    sPickDone = 1;
}

/* The system's file dialog; FALSE if the user cancelled it. */
static BOOL pickDisc(void) {
    static const SDL_DialogFileFilter kFilters[] = {
        {"GameCube disc image", "iso;gcm;ciso;gcz;rvz;wia;wbfs;nfs;tgc"},
        {"All files", "*"},
    };
    sPickDone = 0;
    SDL_ShowOpenFileDialog(onPicked, NULL, aurora_get_window(), kFilters, 2, NULL, false);
    while (!sPickDone) {
        const AuroraEvent* event = aurora_update();
        for (; event != NULL && event->type != AURORA_NONE; event++) {
            if (event->type == AURORA_EXIT) {
                return FALSE;
            }
        }
        SDL_Delay(10);
    }
    return sPicked[0] != '\0';
}

BOOL portAuroraOpenDisc(int argc, char* argv[], const char* userPath) {
    const char* given = argc > 1 ? argv[1] : getenv("TTYD_DISC");
    char why[4600], title[64], saved[4096];
    char* last;
    snprintf(title, sizeof(title), "Paper Mario: The Thousand-Year Door (%s)", REGION);
    snprintf(saved, sizeof(saved), "%s/disc.txt", userPath != NULL ? userPath : ".");
    if (given != NULL && given[0] != '\0') {
        if (!openDisc(given, why, sizeof(why))) {
            fprintf(stderr, "%s\nusage: %s <path to your Paper Mario: TTYD (%s) disc image>\n", why, argv[0],
                    PORT_GAME_ID);
            return FALSE;
        }
        return TRUE;
    }
    last = SDL_LoadFile(saved, NULL);
    if (last != NULL) {
        BOOL ok = last[0] != '\0' && openDisc(last, why, sizeof(why));
        SDL_free(last);
        if (ok) {
            return TRUE;
        }
    }
    for (;;) {
        if (!pickDisc()) {
            fprintf(stderr, "no disc image chosen\n");
            return FALSE;
        }
        if (openDisc(sPicked, why, sizeof(why))) {
            if (!SDL_SaveFile(saved, sPicked, strlen(sPicked))) {
                fprintf(stderr, "could not remember the disc in %s: %s\n", saved, SDL_GetError());
            }
            return TRUE;
        }
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, why, aurora_get_window());
    }
}
