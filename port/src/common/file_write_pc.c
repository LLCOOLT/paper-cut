/* A player's file written whole or not at all: to <path>.tmp, then renamed over <path>
 * (port/include/port/file_write.h). The paths are UTF-8, as every user folder path is (port/file_pc.h: the
 * wide calls on Windows, where the C library's fopen read them in the ANSI code page and an account named
 * "José" never had a file written). The file is synced before the rename: a power cut just after can't
 * leave an empty one in its place. */
#include "port/file_write.h"
#include "port/file_pc.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#elif !defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif

#define PATH_CAP 1100

static int tmpPath(const char* path, char* out, size_t cap) {
    const int n = snprintf(out, cap, "%s.tmp", path);
    return n > 0 && (size_t)n < cap;
}

/* (portRename replaces a file that is there, on Windows too) */
static int replaceFile(const char* from, const char* to) {
    return portRename(from, to) == 0;
}

static int syncFile(FILE* f) {
#ifdef _WIN32
    return _commit(_fileno(f)) == 0;
#elif defined(__EMSCRIPTEN__)
    (void)f;
    return 1;
#else
    return fsync(fileno(f)) == 0;
#endif
}

FILE* portFileWriteOpen(const char* path, const char* mode) {
    char tmp[PATH_CAP];
    if (path == NULL || path[0] == '\0' || !tmpPath(path, tmp, sizeof(tmp))) {
        return NULL;
    }
    return portFopen(tmp, mode);
}

static int closeAndPlace(FILE* f, const char* path, int sync) {
    char tmp[PATH_CAP];
    int ok;
    if (f == NULL) {
        return 0;
    }
    ok = fflush(f) == 0 && ferror(f) == 0 && (!sync || syncFile(f));
    ok = fclose(f) == 0 && ok;
    if (path == NULL || !tmpPath(path, tmp, sizeof(tmp))) {
        return 0;
    }
    if (ok && (sync ? replaceFile(tmp, path) : portRenameQuick(tmp, path) == 0)) {
        return 1;
    }
    portRemove(tmp);
    return 0;
}

int portFileWriteClose(FILE* f, const char* path) {
    return closeAndPlace(f, path, 1);
}

int portFileWriteCloseQuick(FILE* f, const char* path) {
    return closeAndPlace(f, path, 0);
}

int portFileWriteAll(const char* path, const void* data, size_t size) {
    FILE* f = portFileWriteOpen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    if (size > 0 && fwrite(data, 1, size, f) != size) {
        char tmp[PATH_CAP];
        fclose(f);
        if (tmpPath(path, tmp, sizeof(tmp))) {
            portRemove(tmp);
        }
        return 0;
    }
    return portFileWriteClose(f, path);
}
