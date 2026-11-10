/* Playing together online: each guest keeps a copy of the host's adventure (port/include/port/statesync.h,
 * docs/STATE_SYNC.md "Playing together"). The host's game sends its save as it stands (a slot's GameCube
 * image, the same in every build: cardmgr.c portCardLiveImage) when it saves, when it changes rooms (at
 * most every 30 seconds) and every 3 minutes; each co-op guest's game writes it to
 *   <user>/coop/<host's name>_<room>.sav
 * through a temporary file and a rename, so a crash never leaves a half-written one. A copy not taken yet
 * is offered on the guest's next launch (or after a drop, or the host leaving): portCoopCopyPending, and
 * the menus put it into a save file of the player's choice (never over one without asking). Only the
 * host's save data crosses: 0x4000 bytes the guest's game checks before it keeps them
 * (portSaveImageInfo), nothing else of the host's computer. */
#include "port/cardsave_pc.h"
#include "port/save_files.h"
#include "port/settings.h"
#include "port/statesync.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define COPY_EXT ".sav"
#define TAKEN_EXT ".taken"
#define NAME_EXT ".name" /* beside a copy: the host's name as it is (UTF-8) */

static void dirPath(char* out, size_t cap) {
    snprintf(out, cap, "%s/coop", portSettingsUserPath());
}

#ifdef _WIN32
static int widen(const char* path, wchar_t* w, int cap) {
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, w, cap) > 0;
}
#endif

static FILE* openPath(const char* path, const char* mode) {
#ifdef _WIN32
    wchar_t w[1024], m[8];
    if (!widen(path, w, 1024) || !widen(mode, m, 8)) {
        return NULL;
    }
    return _wfopen(w, m);
#else
    return fopen(path, mode);
#endif
}

static void makeDir(const char* path) {
#ifdef _WIN32
    wchar_t w[1024];
    if (widen(path, w, 1024)) {
        CreateDirectoryW(w, NULL);
    }
#else
    mkdir(path, 0755);
#endif
}

static int replaceFile(const char* from, const char* to) {
#ifdef _WIN32
    wchar_t wf[1024], wt[1024];
    return widen(from, wf, 1024) && widen(to, wt, 1024) && MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING);
#else
    return rename(from, to) == 0;
#endif
}

static void removePath(const char* path) {
#ifdef _WIN32
    wchar_t w[1024];
    if (widen(path, w, 1024)) {
        DeleteFileW(w);
    }
#else
    remove(path);
#endif
}

/* a name fit for a file: letters, digits, - and _ (others as _), at most 40 */
static void fileSafe(const char* in, char* out, size_t cap) {
    size_t n = 0;
    for (; in != NULL && *in != '\0' && n + 1 < cap && n < 40; in++) {
        const unsigned char c = (unsigned char)*in;
        out[n++] = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' ? (char)c : '_';
    }
    if (n == 0 && cap > 4) {
        memcpy(out, "host", 4);
        n = 4;
    }
    out[n] = '\0';
}

/* the host's name fit for a file: fileSafe's, and when that changed it ("Bob!" and "Bob?" both "Bob_"), a short
   hash of the name as it is, so two hosts never share one copy: "Bob_-h1a2b3c4d" */
#define HASH_TAG_LEN 10 /* "-h" and 8 hex digits */
static void hostSafe(const char* in, char* out, size_t cap) {
    fileSafe(in, out, cap);
    if (in != NULL && in[0] != '\0' && strcmp(in, out) != 0 && strlen(out) + HASH_TAG_LEN < cap) {
        uint32_t h = 2166136261u;
        const unsigned char* p;
        for (p = (const unsigned char*)in; *p != '\0'; p++) {
            h = (h ^ *p) * 16777619u;
        }
        snprintf(out + strlen(out), cap - strlen(out), "-h%08x", (unsigned)h);
    }
}

/* the host's name as a copy's file name shows it: up to its last _, without hostSafe's hash */
static void hostOfName(const char* name, char* out, size_t cap) {
    char* us;
    size_t n, i;
    snprintf(out, cap, "%s", name);
    if ((us = strrchr(out, '_')) != NULL) {
        *us = '\0';
    }
    n = strlen(out);
    if (n > HASH_TAG_LEN && out[n - HASH_TAG_LEN] == '-' && out[n - HASH_TAG_LEN + 1] == 'h') {
        for (i = n - 8; i < n && ((out[i] >= '0' && out[i] <= '9') || (out[i] >= 'a' && out[i] <= 'f')); i++) {
        }
        if (i == n) {
            out[n - HASH_TAG_LEN] = '\0';
        }
    }
}

/* a guest: the host's save as it came; 1 written */
int portCoopCopyWrite(const char* hostName, const char* room, const uint8_t* image, size_t n) {
    char dir[768], name[64], code[24], path[1024], tmp[1040];
    PortSaveInfo info;
    FILE* f;
    if (image == NULL || n != PORT_CARD_SLOT_SIZE || !portSaveImageInfo(image, &info)) {
        return 0; /* (only a save the game reads as one is ever kept) */
    }
    dirPath(dir, sizeof(dir));
    makeDir(dir);
    hostSafe(hostName, name, sizeof(name));
    fileSafe(room != NULL && room[0] != '\0' ? room : "address", code, sizeof(code));
    snprintf(path, sizeof(path), "%s/%s_%s" COPY_EXT, dir, name, code);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = openPath(tmp, "wb");
    if (f == NULL) {
        return 0;
    }
    if (fwrite(image, 1, n, f) != n) {
        fclose(f);
        removePath(tmp);
        return 0;
    }
    fflush(f);
    fclose(f);
    if (!replaceFile(tmp, path)) {
        removePath(tmp);
        return 0;
    }
    {
        /* (a new copy is to be offered again, even if an older one of this session was taken) */
        char taken[1040];
        snprintf(taken, sizeof(taken), "%s" TAKEN_EXT, path);
        removePath(taken);
    }
    {
        /* the host's name as it is ("Test Host", not the file's "Test_Host"), for the menus that offer it */
        char named[1040];
        snprintf(named, sizeof(named), "%s" NAME_EXT, path);
        if ((f = openPath(named, "wb")) != NULL) {
            fputs(hostName != NULL && hostName[0] != '\0' ? hostName : name, f);
            fclose(f);
        }
    }
    return 1;
}

/* a copy's host name as it came (its NAME_EXT beside it); else the file's own up to its last _, without a hash
   (hostOfName); 1 from the file beside it */
static int hostNameOf(const char* dir, const char* file, char* out, size_t cap) {
    char named[1300];
    FILE* f;
    snprintf(named, sizeof(named), "%s/%s" NAME_EXT, dir, file);
    if ((f = openPath(named, "rb")) != NULL) {
        size_t n = fread(out, 1, cap - 1, f);
        fclose(f);
        out[n] = '\0';
        n = strlen(out);
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
            out[--n] = '\0';
        }
        if (out[0] != '\0') {
            return 1;
        }
    }
    hostOfName(file, out, cap);
    return 0;
}

static int exists(const char* path) {
    FILE* f = openPath(path, "rb");
    if (f != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}

/* the copy at path a save the game reads (into image): 1 */
static int readCopy(const char* path, uint8_t* image) {
    PortSaveInfo info;
    size_t n;
    FILE* f = openPath(path, "rb");
    if (f == NULL) {
        return 0;
    }
    n = fread(image, 1, PORT_CARD_SLOT_SIZE, f);
    fclose(f);
    return n == PORT_CARD_SLOT_SIZE && portSaveImageInfo(image, &info);
}

/* a copy fit to offer: not taken, and readable (one short or damaged never hides the older ones behind it) */
static int offerable(const char* path, const char* taken) {
    static uint8_t sCheck[PORT_CARD_SLOT_SIZE];
    return !exists(taken) && readCopy(path, sCheck);
}

/* the copy portCoopCopyPending offered last (its file name): the one Done marks taken, whatever came since */
static char sOffered[512];

/* the newest copy not taken yet: its file name (in <user>/coop), 0 none */
static int newest(char* out, size_t cap) {
    char dir[768], path[1024], taken[1040];
    int found = 0;
#ifdef _WIN32
    wchar_t w[1024];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    FILETIME best = {0, 0};
    dirPath(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/*" COPY_EXT, dir);
    if (!widen(path, w, 1024) || (h = FindFirstFileW(w, &fd)) == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        char name[512];
        if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0) {
            continue;
        }
        snprintf(taken, sizeof(taken), "%s/%s" TAKEN_EXT, dir, name);
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        if ((found && CompareFileTime(&fd.ftLastWriteTime, &best) <= 0) || !offerable(path, taken)) {
            continue;
        }
        best = fd.ftLastWriteTime;
        snprintf(out, cap, "%s", name);
        found = 1;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR* d;
    struct dirent* e;
    time_t best = 0;
    dirPath(dir, sizeof(dir));
    if ((d = opendir(dir)) == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        struct stat st;
        const size_t len = strlen(e->d_name);
        if (len < 5 || strcmp(e->d_name + len - 4, COPY_EXT) != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        snprintf(taken, sizeof(taken), "%s" TAKEN_EXT, path);
        if (stat(path, &st) != 0 || (found && st.st_mtime <= best) || !offerable(path, taken)) {
            continue;
        }
        best = st.st_mtime;
        snprintf(out, cap, "%s", e->d_name);
        found = 1;
    }
    closedir(d);
#endif
    return found;
}

int portCoopCopyPending(char* hostName, size_t cap, uint8_t* image) {
    char name[512], dir[768], path[1300];
    if (portStateSyncCoopGuest() || !newest(name, sizeof(name))) {
        return 0; /* (playing together now: offered once it's over) */
    }
    dirPath(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (image != NULL && !readCopy(path, image)) {
        return 0;
    }
    snprintf(sOffered, sizeof(sOffered), "%s", name);
    if (hostName != NULL && cap > 0) {
        hostNameOf(dir, name, hostName, cap);
    }
    return 1;
}

/* the copy offered last if it's still there and not taken, else the newest: its file name */
static int offered(char* out, size_t cap) {
    char dir[768], path[1300], taken[1310];
    if (sOffered[0] != '\0') {
        dirPath(dir, sizeof(dir));
        snprintf(path, sizeof(path), "%s/%s", dir, sOffered);
        snprintf(taken, sizeof(taken), "%s" TAKEN_EXT, path);
        if (exists(path) && !exists(taken)) {
            snprintf(out, cap, "%s", sOffered);
            return 1;
        }
    }
    return newest(out, cap);
}

/* the copy's name without ".sav" (<host>_<room>, as portCoopCopyWrite names it): the guest's achievements' record */
void portCoopCopyKey(const char* hostName, const char* room, char* out, size_t cap) {
    char name[64], code[24];
    hostSafe(hostName, name, sizeof(name));
    fileSafe(room != NULL && room[0] != '\0' ? room : "address", code, sizeof(code));
    snprintf(out, cap, "%s_%s", name, code);
}

/* the copy offered now (portCoopCopyPending's), by that name; 0 none */
int portCoopCopyPendingKey(char* out, size_t cap) {
    char name[512];
    size_t len;
    if (!offered(name, sizeof(name))) {
        return 0;
    }
    len = strlen(name);
    if (len > 4 && strcmp(name + len - 4, COPY_EXT) == 0) {
        name[len - 4] = '\0';
    }
    snprintf(out, cap, "%s", name);
    return 1;
}

void portCoopCopyDone(void) {
    char name[512], dir[768], taken[1300];
    FILE* f;
    if (!offered(name, sizeof(name))) {
        return;
    }
    dirPath(dir, sizeof(dir));
    snprintf(taken, sizeof(taken), "%s/%s" TAKEN_EXT, dir, name);
    f = openPath(taken, "wb");
    if (f != NULL) {
        fclose(f);
    }
}

/* ---- every copy kept (the Save files page's Co-op copies) ---- */

static void infoFor(const char* dir, const char* name, long long mtime, int taken, PortCoopCopyInfo* out) {
    const char* us = strrchr(name, '_');
    memset(out, 0, sizeof(*out));
    snprintf(out->file, sizeof(out->file), "%s", name);
    hostNameOf(dir, name, out->host, sizeof(out->host));
    if (us != NULL) {
        snprintf(out->room, sizeof(out->room), "%s", us + 1);
        if (strlen(out->room) > 4 && !strcmp(out->room + strlen(out->room) - 4, COPY_EXT)) {
            out->room[strlen(out->room) - 4] = '\0';
        }
    }
    out->mtime = mtime;
    out->taken = taken;
}

int portCoopCopyList(PortCoopCopyInfo* out, int max) {
    char dir[768], path[1024], taken[1040];
    int n = 0, i, j;
#ifdef _WIN32
    wchar_t w[1024];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    dirPath(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/*" COPY_EXT, dir);
    if (!widen(path, w, 1024) || (h = FindFirstFileW(w, &fd)) == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        char name[512];
        ULARGE_INTEGER t;
        if (n >= max || WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0 ||
            strlen(name) >= sizeof(out[0].file)) {
            continue;
        }
        snprintf(taken, sizeof(taken), "%s/%s" TAKEN_EXT, dir, name);
        t.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        infoFor(dir, name, (long long)(t.QuadPart / 10000000ull) - 11644473600ll, exists(taken), &out[n++]);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR* d;
    struct dirent* e;
    dirPath(dir, sizeof(dir));
    if ((d = opendir(dir)) == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && n < max) {
        struct stat st;
        const size_t len = strlen(e->d_name);
        if (len < 5 || strcmp(e->d_name + len - 4, COPY_EXT) != 0 || len >= sizeof(out[0].file)) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        snprintf(taken, sizeof(taken), "%s" TAKEN_EXT, path);
        if (stat(path, &st) != 0) {
            continue;
        }
        infoFor(dir, e->d_name, (long long)st.st_mtime, exists(taken), &out[n++]);
    }
    closedir(d);
#endif
    for (i = 1; i < n; i++) { /* newest first */
        for (j = i; j > 0 && out[j].mtime > out[j - 1].mtime; j--) {
            PortCoopCopyInfo t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    }
    return n;
}

/* one copy's slot image (a file name from the list, never a path); 1 read and a save */
int portCoopCopyRead(const char* file, uint8_t* image) {
    char dir[768], path[1300];
    PortSaveInfo info;
    FILE* f;
    size_t n;
    if (file == NULL || strchr(file, '/') != NULL || strchr(file, '\\') != NULL || strstr(file, "..") != NULL) {
        return 0;
    }
    dirPath(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    if ((f = openPath(path, "rb")) == NULL) {
        return 0;
    }
    n = fread(image, 1, PORT_CARD_SLOT_SIZE, f);
    fclose(f);
    return n == PORT_CARD_SLOT_SIZE && portSaveImageInfo(image, &info);
}

void portCoopCopyMarkTaken(const char* file) {
    char dir[768], taken[1300];
    FILE* f;
    if (file == NULL || strchr(file, '/') != NULL || strchr(file, '\\') != NULL) {
        return;
    }
    dirPath(dir, sizeof(dir));
    snprintf(taken, sizeof(taken), "%s/%s" TAKEN_EXT, dir, file);
    if ((f = openPath(taken, "wb")) != NULL) {
        fclose(f);
    }
}

int portCoopCopyDelete(const char* file) {
    char dir[768], path[1300], taken[1320];
    if (file == NULL || strchr(file, '/') != NULL || strchr(file, '\\') != NULL || strstr(file, "..") != NULL) {
        return 0;
    }
    dirPath(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    snprintf(taken, sizeof(taken), "%s" TAKEN_EXT, path);
    removePath(taken);
    snprintf(taken, sizeof(taken), "%s" NAME_EXT, path);
    removePath(taken);
    removePath(path);
    return !exists(path);
}
