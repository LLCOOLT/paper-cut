/* port/include/port/path_link.h: is a path a link (symlink, junction)? */
#include "port/path_link.h"

#include <stddef.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>

int portPathIsLink(const char* path) {
    int n;
    wchar_t* w;
    WIN32_FIND_DATAW found;
    HANDLE h;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (n <= 0) {
        return 0;
    }
    w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (w == NULL) {
        return 0;
    }
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);
    h = FindFirstFileW(w, &found);
    free(w);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    FindClose(h);
    /* (only links and junctions, as portfile::isLink: a cloud folder's placeholders are reparse points too,
       and their files are a mod's like any other) */
    return (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
           (found.dwReserved0 == IO_REPARSE_TAG_SYMLINK || found.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
}
#else
#include <sys/stat.h>

int portPathIsLink(const char* path) {
    struct stat st;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}
#endif
