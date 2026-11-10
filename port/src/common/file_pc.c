/* The player's files by UTF-8 path (port/include/port/file_pc.h). */
#include "port/file_pc.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#ifdef _WIN32
/* the path as UTF-16 (malloc'd), NULL when it isn't UTF-8 */
static wchar_t* widen(const char* s) {
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t* w;
    if (n <= 0 || (w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t))) == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}
#endif

FILE* portFopen(const char* path, const char* mode) {
#ifdef _WIN32
    wchar_t* w;
    wchar_t m[16];
    FILE* f;
    size_t i;
    if (path == NULL || mode == NULL) {
        return NULL;
    }
    for (i = 0; mode[i] != '\0' && i + 1 < sizeof(m) / sizeof(m[0]); i++) {
        m[i] = (wchar_t)(unsigned char)mode[i];
    }
    m[i] = 0;
    if ((w = widen(path)) == NULL) {
        return NULL;
    }
    f = _wfopen(w, m);
    free(w);
    return f;
#else
    return path != NULL && mode != NULL ? fopen(path, mode) : NULL;
#endif
}

int portRemove(const char* path) {
#ifdef _WIN32
    wchar_t* w = path != NULL ? widen(path) : NULL;
    int r;
    if (w == NULL) {
        return -1;
    }
    r = _wremove(w);
    free(w);
    return r;
#else
    return path != NULL ? remove(path) : -1;
#endif
}

static int renameWith(const char* from, const char* to, int writeThrough) {
#ifdef _WIN32
    wchar_t* wf = from != NULL ? widen(from) : NULL;
    wchar_t* wt = to != NULL ? widen(to) : NULL;
    const int ok = wf != NULL && wt != NULL &&
                   MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | (writeThrough ? MOVEFILE_WRITE_THROUGH : 0));
    free(wf);
    free(wt);
    return ok ? 0 : -1;
#else
    (void)writeThrough;
    return from != NULL && to != NULL ? rename(from, to) : -1;
#endif
}

int portRename(const char* from, const char* to) {
    return renameWith(from, to, 1);
}

int portRenameQuick(const char* from, const char* to) {
    return renameWith(from, to, 0);
}

int portMkdir(const char* path) {
#ifdef _WIN32
    wchar_t* w = path != NULL ? widen(path) : NULL;
    int ok;
    if (w == NULL) {
        return -1;
    }
    ok = CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
    free(w);
    return ok ? 0 : -1;
#else
    return path != NULL && (mkdir(path, 0755) == 0 || portPathKind(path) == 2) ? 0 : -1;
#endif
}

int portPathKind(const char* path) {
#ifdef _WIN32
    wchar_t* w = path != NULL ? widen(path) : NULL;
    DWORD attr;
    if (w == NULL) {
        return 0;
    }
    attr = GetFileAttributesW(w);
    free(w);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 2 : 1;
#else
    struct stat st;
    if (path == NULL || stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 2 : 1;
#endif
}

long long portFileSize(const char* path) {
#ifdef _WIN32
    wchar_t* w = path != NULL ? widen(path) : NULL;
    WIN32_FILE_ATTRIBUTE_DATA d;
    int ok;
    if (w == NULL) {
        return -1;
    }
    ok = GetFileAttributesExW(w, GetFileExInfoStandard, &d) && !(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    free(w);
    return ok ? (long long)(((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow) : -1;
#else
    struct stat st;
    if (path == NULL || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return -1;
    }
    return (long long)st.st_size;
#endif
}

#ifdef _WIN32
/* name=utf8 in the C library's narrow table (getenv), the OS environment (what child processes inherit)
   left with the right value: the narrow _putenv_s also sets the OS environment, reading the value in the
   ANSI code page, so "José"'s UTF-8 bytes became "JosÃ©" there; the wide value is put back over it */
static void setNarrowKeepOs(const char* name, const char* utf8, const wchar_t* wname, const wchar_t* wvalue) {
    _putenv_s(name, utf8);
    SetEnvironmentVariableW(wname, wvalue);
}
#endif

void portSetEnvUtf8(const char* name, const char* utf8) {
#ifdef _WIN32
    wchar_t* wname = name != NULL ? widen(name) : NULL;
    wchar_t* wvalue = utf8 != NULL ? widen(utf8) : NULL;
    if (wname != NULL && wvalue != NULL) {
        setNarrowKeepOs(name, utf8, wname, wvalue);
    }
    free(wname);
    free(wvalue);
#else
    if (name != NULL && utf8 != NULL) {
        setenv(name, utf8, 1);
    }
#endif
}

void portUtf8EnvInit(void) {
#ifdef _WIN32
    /* the variables that carry a path as UTF-8, as every other path is: the C library's getenv gives a
       narrow one in the ANSI code page, and a folder named "José" was another folder. A restart or the
       launcher hands TTYD_LOAD_SAVE (a save in the user folder) and TTYD_DISC to the next one. */
    static const wchar_t* const kWide[] = {L"TTYD_USER_DIR", L"TTYD_CACHE_DIR", L"TTYD_LOAD_SAVE", L"TTYD_DISC",
                                           L"TTYD_SHOT_DIR", L"TTYD_DATA_DIR",  L"TTYD_ISO"};
    static const char* const kNarrow[] = {"TTYD_USER_DIR", "TTYD_CACHE_DIR", "TTYD_LOAD_SAVE", "TTYD_DISC",
                                          "TTYD_SHOT_DIR", "TTYD_DATA_DIR",  "TTYD_ISO"};
    size_t i;
    for (i = 0; i < sizeof(kWide) / sizeof(kWide[0]); i++) {
        /* (copied first: the C library's tables change under the _putenv_s) */
        static wchar_t wide[2048];
        static char utf8[2048 * 3];
        const wchar_t* w = _wgetenv(kWide[i]);
        if (w == NULL || wcslen(w) >= sizeof(wide) / sizeof(wide[0])) {
            continue;
        }
        wcscpy(wide, w);
        if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, (int)sizeof(utf8), NULL, NULL) > 0) {
            setNarrowKeepOs(kNarrow[i], utf8, kWide[i], wide);
        }
    }
#endif
}
