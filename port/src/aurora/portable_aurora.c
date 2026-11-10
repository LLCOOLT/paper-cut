/* Portable mode (docs/PORTABLE.md): with a file named portable.txt beside the
 * program, its settings, saves, mods and caches live beside it too, in User
 * and Cache, instead of the system's folder for app data. Read once at the
 * start, before anything else: the user folder is $TTYD_USER_DIR from there
 * on (the port reads that everywhere), set only when it isn't set already.
 * "Beside the program": the folder of the .exe on Windows, of the AppImage
 * (or the binary) on Linux, of the .app on macOS. Phones have no such place. */
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include "port/file_pc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
#define PORTABLE_NONE 1
#endif

static char sDir[1024]; /* the folder beside the program, with a trailing slash ("" when none) */
static int sActive;

/* path's folder (its last separator kept), in place */
static void cutToFolder(char* path) {
    char* slash = strrchr(path, '/');
    char* back = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash)) {
        slash = back;
    }
    if (slash != NULL) {
        slash[1] = '\0';
    }
}

static void findDir(void) {
#ifndef PORTABLE_NONE
    const char* appImage = getenv("APPIMAGE");
    const char* test = getenv("TTYD_PORTABLE_BASE");
    const char* base;
    if (test != NULL && test[0] != '\0') {
        /* tests: this folder stands for the program's (testpass's "portable" scenarios, which can't put
         * a portable.txt beside a shared build) */
        size_t n;
        snprintf(sDir, sizeof(sDir) - 1, "%s", test);
        n = strlen(sDir);
        if (sDir[n - 1] != '/' && sDir[n - 1] != '\\') {
            sDir[n] = '/';
            sDir[n + 1] = '\0';
        }
        return;
    }
    if (appImage != NULL && appImage[0] != '\0') {
        snprintf(sDir, sizeof(sDir), "%s", appImage);
        cutToFolder(sDir);
        return;
    }
    base = SDL_GetBasePath();
    if (base == NULL) {
        return;
    }
    snprintf(sDir, sizeof(sDir), "%s", base);
#if defined(__APPLE__)
    {
        /* .../Paper Cut.app/Contents/Resources/: the .app's folder */
        char* app = strstr(sDir, ".app/");
        if (app != NULL) {
            app[4] = '\0';
            cutToFolder(sDir);
        }
    }
#endif
#endif
}

const char* portPortableDir(void) { return sDir; }
int portPortableActive(void) { return sActive; }

/* a UTF-8 path (SDL_GetBasePath's) for getenv, the OS environment a restarted or updated copy inherits
 * (the wide value on Windows: port/file_pc.h), and SDL's own copy (SDL_getenv, which SDL_CreateProcess
 * passes on) in case SDL took it already */
static void setEnv(const char* name, const char* value) {
    portSetEnvUtf8(name, value);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
}

void portPortableInit(void) {
    char path[1100];
    SDL_PathInfo info;
    const char* env;
    findDir();
    if (sDir[0] == '\0') {
        return;
    }
    snprintf(path, sizeof(path), "%sportable.txt", sDir);
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
        return;
    }
    sActive = 1;
    env = getenv("TTYD_USER_DIR"); /* (read once: two calls can differ) */
    if (env == NULL || env[0] == '\0') {
        snprintf(path, sizeof(path), "%sUser", sDir);
        SDL_CreateDirectory(path);
        setEnv("TTYD_USER_DIR", path);
    }
    env = getenv("TTYD_CACHE_DIR");
    if (env == NULL || env[0] == '\0') {
        snprintf(path, sizeof(path), "%sCache", sDir);
        SDL_CreateDirectory(path);
        setEnv("TTYD_CACHE_DIR", path);
    }
}
