/* The disc image: the first argument, $TTYD_DISC, the one chosen last time
 * (disc.txt in Aurora's user folder), or one picked in the system's file
 * dialog. On Android the dialog gives a content:// URI, which Aurora's
 * SDL_IOFromFile opens like a path. A picked disc that Aurora can't read or
 * that is not this build's game is refused with a message box, and the dialog
 * comes back.
 *
 * iOS (no SDL file dialog there, and the app's container path changes with
 * every update, so no path is remembered): the first disc image of this game
 * in the app's Documents folder (put there with the Files app or Finder), else
 * the system document picker, which imports the chosen file into Documents
 * (port/ios/ios_app.m says why it is copied rather than opened in place). */
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/dvd.h>
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/dvd.h>
#include "port/aurora_pc.h"
#include "port/file_write.h"
#include "port/offscreen.h"
#include "port/platform.h"
#include "port/text_language.h"
#include "port/restorations.h"
#ifdef SDL_PLATFORM_IOS
#include "port/ios_pc.h"
#endif
#ifdef __EMSCRIPTEN__
#include "port/web.h"
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

/* SDL's <stdbool.h> must not leave a bool macro behind for the game's headers. */
#undef bool

#define REGION PORT_REGION_NAME
/* Disc image formats Aurora's nod reads, as a dialog filter pattern. */
#define DISC_EXTENSIONS "iso;gcm;ciso;gcz;rvz;wia;wbfs;nfs;tgc"

/* a file of the open disc (portCheckGameDisc's marker files) */
static int discHas(const char* name, void* ctx) {
    char path[64];
    (void)ctx;
    snprintf(path, sizeof(path), "/%s", name);
    return DVDConvertPathToEntrynum(path) >= 0;
}

/* the open disc's ID (6 characters) */
static void discId(char gid[6]) {
    const DVDDiskID* id = DVDGetCurrentDiskID();
    memcpy(gid, id->gameName, 4);
    memcpy(gid + 4, id->company, 2);
}

/* the disc image open now (Restart now starts with it again) */
static char sOpenPath[4096];

const char* portAuroraDiscPath(void) {
    return sOpenPath;
}

/* Aurora's disc: the image at path; in the browser, "web:<name>" is the file
 * the page was given (port/src/web/web_pc.c). */
static BOOL dvdOpen(const char* path) {
#ifdef __EMSCRIPTEN__
    if (strncmp(path, "web:", 4) == 0) {
        return portWebOpenDisc();
    }
#endif
    return aurora_dvd_open(path);
}

/* Opens path; FALSE with the reason in why if it isn't this build's disc. */
static BOOL openDisc(const char* path, char* why, size_t size) {
    char gid[6];
    if (!dvdOpen(path)) {
        snprintf(why, size, "%s\n\nis not a GameCube disc image that can be read.", path);
        return FALSE;
    }
    discId(gid);
    if (!portCheckGameDisc(gid, discHas, NULL, path, why, size)) {
        aurora_dvd_close();
        return FALSE;
    }
    /* the text of another release's disc, and a room from the player's
     * Japanese disc, if chosen: both read another disc in this one's place
     * and open this one again. If that failed (a flaky drive, a lost grant,
     * the browser's "web:" file), one more try here, else no disc to run. */
    if ((!portTextLanguageLoad(path) && !dvdOpen(path)) || (!portRestorationsDiscOpened(path) && !dvdOpen(path))) {
        snprintf(why, size, "%s\n\ncould not be opened again after reading another disc of the library.", path);
        return FALSE;
    }
    snprintf(sOpenPath, sizeof(sOpenPath), "%s", path);
    return TRUE;
}

int portAuroraOpenDiscPath(const char* path, char* why, size_t size) {
    return openDisc(path, why, size);
}

/* The version of the disc image at path (opened and closed again); NULL if
 * it can't be read or isn't a version of the game. */
const PortVersionInfo* portAuroraProbeDisc(const char* path) {
    return portAuroraProbeDiscBanner(path, NULL, NULL);
}

/* the open disc's banner (opening.bnr: 96x32 RGB5A3 in 4x4 tiles from 0x20) as RGBA8; FALSE if it has none */
static BOOL readBanner(unsigned char* rgba) {
    DVDFileInfo info;
    static unsigned char data[0x1820]; /* the header and the picture: whole 32-byte blocks */
    BOOL ok = FALSE;
    int bx, by, x, y;
    if (!DVDOpen("/opening.bnr", &info)) {
        return FALSE;
    }
    if (info.length >= sizeof(data) && DVDReadPrio(&info, data, sizeof(data), 0, 2) >= (s32)sizeof(data) &&
        memcmp(data, "BNR", 3) == 0) {
        const unsigned char* p = data + 0x20;
        for (by = 0; by < 32; by += 4) {
            for (bx = 0; bx < 96; bx += 4) {
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 4; x++, p += 2) {
                        const unsigned v = (unsigned)p[0] << 8 | p[1];
                        unsigned char* o = rgba + ((by + y) * 96 + bx + x) * 4;
                        if (v & 0x8000) {
                            o[0] = (unsigned char)(((v >> 10) & 31) * 255 / 31);
                            o[1] = (unsigned char)(((v >> 5) & 31) * 255 / 31);
                            o[2] = (unsigned char)((v & 31) * 255 / 31);
                            o[3] = 255;
                        } else {
                            o[0] = (unsigned char)(((v >> 8) & 15) * 17);
                            o[1] = (unsigned char)(((v >> 4) & 15) * 17);
                            o[2] = (unsigned char)((v & 15) * 17);
                            o[3] = (unsigned char)(((v >> 12) & 7) * 255 / 7);
                        }
                    }
                }
            }
        }
        ok = TRUE;
    }
    DVDClose(&info);
    return ok;
}

const PortVersionInfo* portAuroraProbeDiscBanner(const char* path, unsigned char* banner, int* hasBanner) {
    const PortVersionInfo* v;
    char gid[6];
    if (hasBanner != NULL) {
        *hasBanner = 0;
    }
    if (!dvdOpen(path)) {
        return NULL;
    }
    discId(gid);
    v = portVersionIdentify(gid, discHas, NULL);
    if (v != NULL && banner != NULL && hasBanner != NULL) {
        *hasBanner = readBanner(banner);
    }
    aurora_dvd_close();
    return v;
}

static char sPicked[4096];
static char sPickError[512]; /* why the last dialog didn't open ("" when it did, or was cancelled) */
static volatile int sPickDone;

static void SDLCALL onPicked(void* user, const char* const* files, int filter) {
    (void)user;
    (void)filter;
    sPicked[0] = '\0';
    if (files != NULL && files[0] != NULL) {
        snprintf(sPicked, sizeof(sPicked), "%s", files[0]);
    } else if (files == NULL) {
        fprintf(stderr, "file dialog: %s\n", SDL_GetError());
        snprintf(sPickError, sizeof(sPickError), "%s", SDL_GetError()[0] != '\0' ? SDL_GetError() : "no file dialog");
    }
    sPickDone = 1;
}

const char* portAuroraPickError(void) {
    return sPickError;
}

/* The system's file dialog; FALSE if the user cancelled it. withRom (the launcher): a Paper
 * Mario 64 ROM may be picked too, which the launcher links (port/include/port/pm64.h). */
static BOOL pickDisc(BOOL withRom) {
    static const SDL_DialogFileFilter kFilters[] = {
        {"GameCube disc image", DISC_EXTENSIONS},
        {"All files", "*"},
    };
    static const SDL_DialogFileFilter kWithRom[] = {
        {"GameCube disc image or Paper Mario 64 ROM", DISC_EXTENSIONS ";z64;v64;n64"},
        {"All files", "*"},
    };
    if (portOffscreen()) {
        printf("[offscreen] disc picker: not shown (give the disc on the command line)\n");
        return FALSE;
    }
    sPickDone = 0;
    sPickError[0] = '\0';
    portShowOpenFileDialog(onPicked, NULL, aurora_get_window(), withRom ? kWithRom : kFilters, 2, NULL, false);
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

/* TRUE if name ends in one of DISC_EXTENSIONS (any case). */
static BOOL hasDiscExtension(const char* name) {
    const char* dot = strrchr(name, '.');
    const char* ext = DISC_EXTENSIONS;
    size_t n;
    if (dot == NULL || dot[1] == '\0') {
        return FALSE;
    }
    dot++;
    n = strlen(dot);
    while (*ext != '\0') {
        size_t len = strcspn(ext, ";");
        if (len == n && SDL_strncasecmp(ext, dot, n) == 0) {
            return TRUE;
        }
        ext += len + (ext[len] == ';');
    }
    return FALSE;
}

/* Opens the first disc image of this build's game in folder dir (not its
 * subfolders); FALSE if there is none, with what was found in why. Used on
 * iOS (Documents); compiled everywhere. */
static BOOL openDiscInFolder(const char* dir, char* why, size_t size) {
    char path[4096];
    int i, count = 0, tried = 0;
    char** names = SDL_GlobDirectory(dir, NULL, 0, &count);
    BOOL ok = FALSE;
    why[0] = '\0';
    for (i = 0; names != NULL && i < count && !ok; i++) {
        SDL_PathInfo info;
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (!hasDiscExtension(names[i]) || !SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
            continue;
        }
        tried++;
        ok = openDisc(path, why, size);
    }
    SDL_free(names);
    if (!ok && tried == 0) {
        snprintf(why, size, "There is no GameCube disc image in this app's folder yet.");
    }
    return ok;
}

#ifdef SDL_PLATFORM_IOS
/* The document picker (port/ios/ios_app.m); FALSE if cancelled. */
static BOOL pickDiscIos(void) {
    int state;
    sPicked[0] = '\0';
    if (!portIosPickDiscBegin()) {
        return FALSE;
    }
    while ((state = portIosPickDiscPoll(sPicked, sizeof(sPicked))) < 0) {
        const AuroraEvent* event = aurora_update();
        for (; event != NULL && event->type != AURORA_NONE; event++) {
            if (event->type == AURORA_EXIT) {
                return FALSE;
            }
        }
        SDL_Delay(10);
    }
    return state == 1 && sPicked[0] != '\0';
}

/* A disc image in Documents, else ask for one (picker) until a good one is
 * chosen or the user quits. */
static BOOL openDiscIos(const char* title) {
    static const SDL_MessageBoxButtonData kButtons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose File"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"},
    };
    char why[4600], text[5200];
    const char* docs = portIosDocumentsDir();
    if (docs != NULL && openDiscInFolder(docs, why, sizeof(why))) {
        return TRUE;
    }
    for (;;) {
        SDL_MessageBoxData box;
        int button = 0;
        snprintf(text, sizeof(text),
                 "%s\n\nChoose your Paper Mario: The Thousand-Year Door disc image (%s, %s). It is copied into "
                 "this app's folder in the Files app (On My iPhone or On My iPad), where you can also put it "
                 "yourself.",
                 why, PORT_GAME_ID, REGION);
        SDL_zero(box);
        box.flags = SDL_MESSAGEBOX_INFORMATION;
        box.window = aurora_get_window();
        box.title = title;
        box.message = text;
        box.numbuttons = 2;
        box.buttons = kButtons;
        if (!portShowMessageBox(&box, &button) || button != 1) {
            return FALSE;
        }
        if (!pickDiscIos()) {
            snprintf(why, sizeof(why), "No disc image was chosen.");
            continue;
        }
        if (openDisc(sPicked, why, sizeof(why))) {
            return TRUE;
        }
    }
}
#endif

/* The system's file dialog (iOS: the document picker) into out; FALSE if
 * the user cancelled it. For the launcher (a Paper Mario 64 ROM may be picked too). */
int portAuroraPickDisc(char* out, size_t size) {
#ifdef SDL_PLATFORM_IOS
    if (!pickDiscIos()) {
        return FALSE;
    }
#else
    if (!pickDisc(TRUE)) {
        return FALSE;
    }
#endif
    snprintf(out, size, "%s", sPicked);
    return TRUE;
}

void portOnlineInviteEvent(const void* sdlEvent); /* invite.cpp */

#if defined(__APPLE__) && TARGET_OS_OSX
/* macOS: a disc image opened with the app in Finder (a double click, Open
 * With, dropped on its icon; Info.plist declares the types) is not an
 * argument but an open-document event, which SDL delivers as a drop-file
 * event on one of the first event pumps. Into sPicked; FALSE if none came. */
static BOOL openedInFinder(void) {
    int i;
    for (i = 0; i < 20; i++) {
        const AuroraEvent* event = aurora_update();
        for (; event != NULL && event->type != AURORA_NONE; event++) {
            if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_DROP_FILE &&
                event->sdl.drop.data != NULL && SDL_strncasecmp(event->sdl.drop.data, "papercut:", 9) == 0) {
                portOnlineInviteEvent(&event->sdl); /* an invite link, not a disc (invite.cpp) */
                continue;
            }
            if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_DROP_FILE &&
                event->sdl.drop.data != NULL) {
                snprintf(sPicked, sizeof(sPicked), "%s", event->sdl.drop.data);
                return TRUE;
            }
        }
        SDL_Delay(10);
    }
    return FALSE;
}
#endif

/* a file:// URL's path, decoded (the Linux desktop file passes %u: a disc may come
 * as one), in place */
static void fromFileUrl(char* s) {
    char* out = s;
    const char* in = s + 7;
    if (strncmp(s, "file://", 7) != 0) {
        return;
    }
    if (strncmp(in, "localhost/", 10) == 0) {
        in += 9;
    }
    for (; *in != '\0'; in++) {
        if (in[0] == '%' && isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2])) {
            char hex[3] = {in[1], in[2], '\0'};
            *out++ = (char)strtol(hex, NULL, 16);
            in += 2;
        } else {
            *out++ = *in;
        }
    }
    *out = '\0';
}

BOOL portAuroraOpenDisc(int argc, char* argv[], const char* userPath) {
    const char* given = argc > 1 ? argv[1] : getenv("TTYD_DISC");
    static char sGivenPath[4096];
    if (given != NULL && strncmp(given, "file://", 7) == 0) {
        snprintf(sGivenPath, sizeof(sGivenPath), "%s", given);
        fromFileUrl(sGivenPath);
        given = sGivenPath;
    }
    char why[4600], title[64], saved[4096];
    char* last;
#if defined(__APPLE__) && TARGET_OS_OSX
    /* Finder used to start apps with a process serial number argument. */
    if (argc > 1 && strncmp(argv[1], "-psn_", 5) == 0) {
        given = getenv("TTYD_DISC");
    }
#endif
    snprintf(title, sizeof(title), "Paper Mario: The Thousand-Year Door (%s)", REGION);
    snprintf(saved, sizeof(saved), "%s/disc.txt", userPath != NULL ? userPath : ".");
    if (given != NULL && given[0] != '\0') {
        if (!openDisc(given, why, sizeof(why))) {
            fprintf(stderr, "%s\nusage: %s <path to your Paper Mario: TTYD (%s) disc image>\n", why, argv[0],
                    PORT_GAME_ID);
            if (getenv("TTYD_NO_LAUNCHER") != NULL) {
                return FALSE; /* scripted runs: the reason on stderr only */
            }
            /* a GUI start (Windows, Finder's Open With, a launch option that
             * isn't a disc): said, then the launcher instead of no window */
            portShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, why, aurora_get_window());
        } else {
            return TRUE;
        }
    }
    /* The launcher (port/settings/launcher_ui.cpp): the player's disc images,
     * the settings, Play. A disc of another version starts that version's
     * executable from there. $TTYD_NO_LAUNCHER: the steps below instead. */
    if (getenv("TTYD_NO_LAUNCHER") == NULL) {
        char path[4096], finder[4096];
        finder[0] = '\0';
#if defined(__APPLE__) && TARGET_OS_OSX
        if (openedInFinder()) {
            snprintf(finder, sizeof(finder), "%s", sPicked);
        }
#endif
        if (!portLauncherRun(userPath, finder, path, sizeof(path))) {
            return FALSE;
        }
        if (openDisc(path, why, sizeof(why))) {
            return TRUE;
        }
        portShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, why, aurora_get_window());
        return FALSE;
    }
#ifdef SDL_PLATFORM_IOS
    (void)last;
    return openDiscIos(title);
#endif
#if defined(__APPLE__) && TARGET_OS_OSX
    if (openedInFinder()) {
        if (openDisc(sPicked, why, sizeof(why))) {
            if (!portFileWriteAllSdl(saved, sPicked, strlen(sPicked))) {
                fprintf(stderr, "could not remember the disc in %s: %s\n", saved, SDL_GetError());
            }
            return TRUE;
        }
        portShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, why, aurora_get_window());
    }
#endif
    last = SDL_LoadFile(saved, NULL);
    if (last != NULL) {
        BOOL ok = last[0] != '\0' && openDisc(last, why, sizeof(why));
        SDL_free(last);
        if (ok) {
            return TRUE;
        }
    }
    for (;;) {
        if (!pickDisc(FALSE)) {
            fprintf(stderr, "no disc image chosen\n");
            return FALSE;
        }
        if (openDisc(sPicked, why, sizeof(why))) {
            if (!portFileWriteAllSdl(saved, sPicked, strlen(sPicked))) {
                fprintf(stderr, "could not remember the disc in %s: %s\n", saved, SDL_GetError());
            }
            return TRUE;
        }
        portShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, why, aurora_get_window());
    }
}
