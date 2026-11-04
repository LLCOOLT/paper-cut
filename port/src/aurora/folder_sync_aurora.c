/* Save sync through any folder (port/include/port/cloud.h; Windows and Linux:
 * Apple builds have iCloud Drive, port/apple/cloud_apple.m).
 *
 * The memory card is a folder of .gci files (Aurora's GCI folder card,
 * <user>/<region>/Card A). The same layout is kept in a folder the player
 * picks, one a sync service keeps the same across computers: OneDrive,
 * Dropbox, Google Drive, a Syncthing folder, or iCloud for Windows' iCloud
 * Drive (its TTYD folder: the Mac's and iPhone's saves). As with iCloud, each
 * file goes the way of its newer copy: from the folder when the game starts
 * (before the card is mounted), to it after the game writes one and when the
 * app closes; a copy takes its source's modification time, so a synced pair is
 * equal and nothing goes back and forth; a local file about to be replaced is
 * kept in <user>/Backups/<region> first (where the Saves tab restores from);
 * nothing is ever deleted. The folder's path is kept in <user>/sync_folder.txt. */
#if !defined(__APPLE__) && !defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif
#include "port/cloud.h"
#include "port/settings.h"

static char sFolder[1024]; /* the sync folder, "" none */
static int sRead;
static char sStatus[160];

static void path(char* out, size_t size, const char* a, const char* b) {
    snprintf(out, size, "%s/%s", a, b);
}

static void readFolder(void) {
    const char* user = portSettingsUserPath();
    char file[1200];
    size_t size = 0;
    char* text;
    if (sRead || user == NULL || user[0] == '\0') {
        return;
    }
    sRead = 1;
    path(file, sizeof(file), user, "sync_folder.txt");
    text = SDL_LoadFile(file, &size);
    if (text != NULL) {
        size_t n = strcspn(text, "\r\n");
        snprintf(sFolder, sizeof(sFolder), "%.*s", (int)(n < sizeof(sFolder) ? n : sizeof(sFolder) - 1), text);
        SDL_free(text);
    }
}

static void saveFolder(void) {
    const char* user = portSettingsUserPath();
    char file[1200];
    if (user == NULL || user[0] == '\0') {
        return;
    }
    path(file, sizeof(file), user, "sync_folder.txt");
    SDL_SaveFile(file, sFolder, strlen(sFolder));
}

int portCloudAvailable(void) {
    SDL_PathInfo info;
    readFolder();
    return sFolder[0] != '\0' && SDL_GetPathInfo(sFolder, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

const char* portCloudFolderName(void) {
    readFolder();
    return sFolder;
}

const char* portCloudStatus(void) {
    return sStatus;
}

/* a file's modification time in whole seconds, 0 when it isn't there */
static Sint64 modified(const char* file) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(file, &info) || info.type != SDL_PATHTYPE_FILE) {
        return 0;
    }
    return info.modify_time / 1000000000;
}

/* src over dst, dst taking src's modification time */
static int copyFile(const char* src, const char* dst) {
    struct utimbuf t;
    const Sint64 when = modified(src);
    if (!SDL_CopyFile(src, dst)) {
        snprintf(sStatus, sizeof(sStatus), "Could not copy %s: %s", dst, SDL_GetError());
        return 0;
    }
    t.actime = (time_t)when;
    t.modtime = (time_t)when;
    utime(dst, &t);
    return 1;
}

/* a local save about to be replaced, kept as <user>/Backups/<region>/<name minus .gci> <date>.gci */
static void backup(const char* local, const char* name, const char* userPath, const char* region) {
    char dir[1200], file[1500], stem[256], date[32];
    const time_t now = time(NULL);
    const size_t n = strlen(name);
    snprintf(stem, sizeof(stem), "%.*s", (int)(n > 4 ? n - 4 : n), name);
    strftime(date, sizeof(date), "%Y-%m-%d %H.%M.%S", localtime(&now));
    snprintf(dir, sizeof(dir), "%s/Backups/%s", userPath, region);
    SDL_CreateDirectory(dir);
    snprintf(file, sizeof(file), "%s/%s %s.gci", dir, stem, date);
    SDL_CopyFile(local, file);
}

int portCloudSync(const char* userPath, const char* region, int pull) {
    char local[1200], remote[1200];
    char** names;
    int count = 0, copied = 0, i;
    if (!portCloudAvailable()) {
        return 0;
    }
    snprintf(local, sizeof(local), "%s/%s/Card A", userPath, region);
    snprintf(remote, sizeof(remote), "%s/%s/Card A", sFolder, region);
    SDL_CreateDirectory(local);
    SDL_CreateDirectory(remote);
    names = SDL_GlobDirectory(pull ? remote : local, "*.gci", 0, &count);
    for (i = 0; names != NULL && i < count; i++) {
        char from[1500], to[1500];
        Sint64 tf, tt;
        path(from, sizeof(from), pull ? remote : local, names[i]);
        path(to, sizeof(to), pull ? local : remote, names[i]);
        tf = modified(from);
        tt = modified(to);
        if (tf == 0 || (tt != 0 && tf - tt < 1)) {
            continue; /* the other copy is as new or newer */
        }
        if (pull && tt != 0) {
            backup(to, names[i], userPath, region);
        }
        copied += copyFile(from, to);
    }
    SDL_free(names);
    if (pull) {
        snprintf(sStatus, sizeof(sStatus),
                 copied ? "%d save file(s) brought from the sync folder" : "Saves are the same as in the sync folder",
                 copied);
    } else if (copied) {
        snprintf(sStatus, sizeof(sStatus), "%d save file(s) copied to the sync folder", copied);
    }
    return copied;
}

void portCloudFlush(void) {
    /* the copies are made at once: nothing waits */
}

static void SDLCALL picked(void* userdata, const char* const* files, int filter) {
    (void)userdata;
    (void)filter;
    if (files != NULL && files[0] != NULL) {
        snprintf(sFolder, sizeof(sFolder), "%s", files[0]);
        saveFolder();
        snprintf(sStatus, sizeof(sStatus), "The sync folder is set: saves go there as you play.");
        portSettingsCloudPush(0); /* what only this computer has, there now */
    }
}

int portCloudPickFolder(void) {
    readFolder();
    SDL_ShowOpenFolderDialog(picked, NULL, NULL, sFolder[0] != '\0' ? sFolder : NULL, false);
    return 1;
}

void portCloudForget(void) {
    sFolder[0] = '\0';
    saveFolder();
    sStatus[0] = '\0';
}
#endif
