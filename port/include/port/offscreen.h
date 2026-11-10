#ifndef PORT_OFFSCREEN_H
#define PORT_OFFSCREEN_H

/* $TTYD_OFFSCREEN=1 (Aurora, tests): the game's window is never shown, raised or
 * focused (Aurora's AuroraConfig.offscreen: created hidden, no taskbar entry on
 * Windows, never paused for being hidden), frames are drawn and captured as usual
 * ($TTYD_SHOT, $TTYD_LAUNCHER_SHOT, story.png), and the sound goes to SDL's dummy
 * device. Anything else that would open a window of its own (a message box, a
 * file or folder picker, a link or folder opened in another program, the second
 * screen's window, the focus taken back after a restart) is left out and logged
 * as "[offscreen] ... not shown", as if it had been cancelled. main_aurora.c. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_misc.h>
#include "port/tr.h" /* (portPickerError's text) */
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
int portOffscreen(void);
#ifdef __cplusplus
}
#endif

/* A link opened in the browser (or the app a scheme names): true when the system took it (SDL_OpenURL);
 * false when it didn't (SDL_GetError says why) or offscreen. A caller shows portOpenError's text on false,
 * never nothing. */
static inline bool portOpenUrl(const char* url) {
    if (portOffscreen()) {
        printf("[offscreen] open %s: not shown\n", url);
        SDL_SetError("offscreen: not shown");
        return false;
    }
    if (!SDL_OpenURL(url)) {
        printf("[open] %s: %s\n", url, SDL_GetError());
        return false;
    }
    return true;
}

/* what to show when portOpenUrl or portOpenFolder gave false, in the menu language, into out: the link or
 * the folder's path, so the player can go there by hand. Returns out. */
static inline const char* portOpenError(char* out, size_t size, const char* target, bool folder) {
    if (out != NULL && size > 0) {
        snprintf(out, size, portTr(folder ? "Couldn't open the folder. It is here: %s" : "Couldn't open the link. Go to: %s"),
                 target != NULL ? target : "");
    }
    return out;
}

/* A folder (a UTF-8 path) opened in the system's file manager, as a file:// URL: "file:///C:/..." on
 * Windows, "file:///home/..." elsewhere (not "file:////home/...", which some openers take as a network
 * path and do nothing). Off Windows the path is percent-encoded (macOS's URL parser refuses a space, as
 * in "Application Support"); Windows' shell is given the path's own characters, as it reads %xx in the
 * ANSI code page. Use it for every "Open the ... folder" button, never "file:///" + path by hand.
 * true when the system took it; false otherwise (portOpenError(..., true) shows the path), and always on a
 * phone or tablet, where a file:// folder has nothing to open it. */
static inline bool portOpenFolder(const char* path) {
    static const char kHex[] = "0123456789ABCDEF";
    size_t n, i;
    char* url;
    char* o;
    bool ok;
    if (path == NULL || path[0] == '\0') {
        path = ".";
    }
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    SDL_SetError("no file manager opens a folder here");
    printf("[open] folder %s: not on this device\n", path);
    return false;
#endif
    n = strlen(path);
    if ((url = (char*)malloc(n * 3 + 16)) == NULL) {
        return false;
    }
    memcpy(url, "file://", 7);
    o = url + 7;
    if (path[0] != '/') {
        *o++ = '/';
    }
    for (i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)path[i];
#ifdef _WIN32
        *o++ = c == '\\' ? '/' : (char)c;
#else
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '-' ||
            c == '_' || c == '.' || c == '~') {
            *o++ = (char)c;
        } else {
            *o++ = '%';
            *o++ = kHex[c >> 4];
            *o++ = kHex[c & 15];
        }
#endif
    }
    *o = '\0';
    ok = portOpenUrl(url);
    free(url);
    return ok;
}

static inline bool portShowMessageBox(const SDL_MessageBoxData* box, int* button) {
    if (portOffscreen()) {
        printf("[offscreen] message box \"%s\": not shown\n", box->title != NULL ? box->title : "");
        return false;
    }
    return SDL_ShowMessageBox(box, button);
}

static inline bool portShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char* title, const char* message,
                                            SDL_Window* window) {
    if (portOffscreen()) {
        printf("[offscreen] message box \"%s\": not shown (%s)\n", title, message);
        return false;
    }
    return SDL_ShowSimpleMessageBox(flags, title, message, window);
}

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
/* iOS: SDL has no file dialog there (its dummy one answers "unsupported" at once, so a picker
 * button did nothing): the system's document picker instead, the file imported as a copy and
 * its path given to the callback as SDL would (port/ios/ios_app.m). */
#ifdef __cplusplus
extern "C" {
#endif
void portIosShowOpenFileDialog(SDL_DialogFileCallback callback, void* userdata, bool allow_many);
/* a folder, opened in place: its security-scoped access is started for the callback and kept until
 * portReleasePickedFolder(path) (or the app ends) */
void portIosShowOpenFolderDialog(SDL_DialogFileCallback callback, void* userdata, bool allow_many);
void portIosReleasePickedFolder(const char* path);
/* "Save as" (port/settings/file_export.h): the bytes written to a file in tmp/ first, then the system's
 * export picker (a copy goes where the player picks: Files, iCloud Drive...) */
void portIosExportFile(SDL_DialogFileCallback callback, void* userdata, const char* name, const void* data,
                       size_t size);
#ifdef __cplusplus
}
#endif
#endif

/* The pickers: not shown, and their callback not called (as a picker still open) */
static inline void portShowOpenFileDialog(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window,
                                          const SDL_DialogFileFilter* filters, int nfilters,
                                          const char* default_location, bool allow_many) {
    if (portOffscreen()) {
        printf("[offscreen] file picker: not shown\n");
        return;
    }
#if defined(__APPLE__) && TARGET_OS_IPHONE
    (void)window;
    (void)filters;
    (void)nfilters;
    (void)default_location;
    portIosShowOpenFileDialog(callback, userdata, allow_many); /* (the callers check what they're given) */
    return;
#endif
    SDL_ShowOpenFileDialog(callback, userdata, window, filters, nfilters, default_location, allow_many);
}

/* Only portfile::exportFile calls this (port/settings/file_export.h: the bytes are made first, then the
 * path asked for and written): iOS's export picker needs the file before it asks, so a path asked for
 * first couldn't be written there, and this answers with an error on iOS. */
static inline void portShowSaveFileDialog(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window,
                                          const SDL_DialogFileFilter* filters, int nfilters,
                                          const char* default_location) {
    if (portOffscreen()) {
        printf("[offscreen] save picker: not shown\n");
        return;
    }
#if defined(__APPLE__) && TARGET_OS_IPHONE
    (void)window;
    (void)filters;
    (void)nfilters;
    (void)default_location;
    SDL_SetError("No save dialog on iOS: portfile::exportFile writes the file, then exports it");
    callback(userdata, NULL, -1);
    return;
#endif
    SDL_ShowSaveFileDialog(callback, userdata, window, filters, nfilters, default_location);
}

/* A folder: on iOS the system's document picker, the folder opened in place under security-scoped
 * access, kept until the caller is done with it: portReleasePickedFolder(path) */
static inline void portShowOpenFolderDialog(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window,
                                            const char* default_location, bool allow_many) {
    if (portOffscreen()) {
        printf("[offscreen] folder picker: not shown\n");
        return;
    }
#if defined(__APPLE__) && TARGET_OS_IPHONE
    (void)window;
    (void)default_location;
    portIosShowOpenFolderDialog(callback, userdata, allow_many);
    return;
#endif
    SDL_ShowOpenFolderDialog(callback, userdata, window, default_location, allow_many);
}

/* A picker's answer with no list (files == NULL: an error, SDL_GetError says why; an empty list, files[0] ==
 * NULL, is a cancel): what to show for it, in the menu language, into out: "Couldn't open the file picker:
 * <why>", and on Linux (no portal or zenity: a Steam Deck's game mode) where the file can go instead, the
 * folder where when the feature has one (else NULL). Every picker callback (open, folder, save as) says it
 * this way, never treats it as a cancel. Call it in the callback (SDL_GetError is per thread); any thread:
 * out is the caller's. Returns out. */
static inline const char* portPickerError(char* out, size_t size, const char* where) {
    const char* why = SDL_GetError();
    int n;
    if (out == NULL || size == 0) {
        return out;
    }
    if (why == NULL || why[0] == '\0') {
        why = portTr("no reason given");
    }
    n = snprintf(out, size, portTr("Couldn't open the file picker: %s"), why);
#if defined(__linux__) && !defined(__ANDROID__)
    if (n >= 0 && (size_t)n + 1 < size) {
        out[n++] = '\n';
        out[n] = '\0';
        if (where != NULL && where[0] != '\0') {
            snprintf(out + n, size - (size_t)n, portTr("Or put the file in this folder instead: %s"), where);
        } else {
            snprintf(out + n, size - (size_t)n, "%s",
                     portTr("On Linux the picker needs xdg-desktop-portal or zenity (Steam Deck: Desktop Mode)."));
        }
    }
#else
    (void)n;
    (void)where;
#endif
    printf("[picker] %s\n", out);
    return out;
}

/* a folder from portShowOpenFolderDialog no longer used (iOS: its access ends; elsewhere, or a path that
 * isn't a picked folder: nothing) */
static inline void portReleasePickedFolder(const char* path) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    portIosReleasePickedFolder(path);
#else
    (void)path;
#endif
}

#endif
