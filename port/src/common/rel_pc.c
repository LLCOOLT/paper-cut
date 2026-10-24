/* REL loading on PC (see CMakeLists.txt, TTYD_RELS_STATIC).
 * Static mode: every REL is linked into the executable with renamed symbols;
 * linking one restores its initial data, clears its bss and runs _prolog.
 * Module mode: each REL is a loadable module next to the executable
 * (rel/<area>.so|.dylib); linking one loads it (fresh .data/.bss, as a fresh
 * load from disc) and runs its _prolog; unlinking runs _epilog and unloads it. */
#include <dolphin/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/rel_pc.h"

#ifdef PORT_RELS_STATIC

static const PortRelStatic* sCurrent;
static char** sSnapshots; /* initial data of each REL, by table index */

void portRelInit(const char* exePath) {
    int i;
    (void)exePath;
    sSnapshots = (char**)calloc((size_t)portRelStaticCount, sizeof(char*));
    for (i = 0; i < portRelStaticCount; i++) {
        const PortRelStatic* r = &portRelStaticTable[i];
        size_t n = (size_t)(r->dataEnd - r->dataStart);
        sSnapshots[i] = (char*)malloc(n ? n : 1);
        memcpy(sSnapshots[i], r->dataStart, n);
    }
}

BOOL portRelLink(const char* areaName) {
    int i;
    if (sCurrent != NULL) {
        portRelUnlink();
    }
    for (i = 0; i < portRelStaticCount; i++) {
        const PortRelStatic* r = &portRelStaticTable[i];
        if (strcmp(r->name, areaName) == 0) {
            memcpy(r->dataStart, sSnapshots[i], (size_t)(r->dataEnd - r->dataStart));
            memset(r->bssStart, 0, (size_t)(r->bssEnd - r->bssStart));
            sCurrent = r;
            if (getenv("TTYD_TRACE") != NULL) {
                printf("[trace] REL %s linked\n", r->name);
            }
            r->prolog();
            return TRUE;
        }
    }
    fprintf(stderr, "portRelLink: no REL named %s in this build\n", areaName);
    return FALSE;
}

void portRelUnlink(void) {
    if (sCurrent == NULL) {
        return;
    }
    sCurrent->epilog();
    sCurrent = NULL;
}

#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <limits.h>
#include <stdint.h>
#include <mach-o/dyld.h>
#endif

#if defined(_WIN32)
#define REL_EXT ".dll"
#elif defined(__APPLE__)
#define REL_EXT ".so" /* CMake MODULE libraries use .so on macOS */
#else
#define REL_EXT ".so"
#endif

typedef void (*RelEntry)(void);

static void* sHandle;
static RelEntry sEpilog;
static char sRelDir[1024];

static void* openModule(const char* path) {
#ifdef _WIN32
    return (void*)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static RelEntry findEntry(void* handle, const char* name) {
#ifdef _WIN32
    return (RelEntry)GetProcAddress((HMODULE)handle, name);
#else
    return (RelEntry)dlsym(handle, name);
#endif
}

static void closeModule(void* handle) {
#ifdef _WIN32
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

static const char* moduleError(void) {
#ifdef _WIN32
    return "LoadLibrary failed";
#else
    return dlerror();
#endif
}

void portRelInit(const char* exePath) {
    const char* slash;
#if defined(__linux__)
    /* argv[0] may be a bare name found on PATH: the executable's real path */
    char self[1024];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        self[n] = '\0';
        exePath = self;
    }
#elif defined(__APPLE__)
    /* argv[0] can be relative or a bare name found on PATH; the loader knows
     * the executable's real path (in an .app: Contents/MacOS/). */
    char exe[PATH_MAX], real[PATH_MAX];
    uint32_t exeSize = sizeof(exe);
    if (_NSGetExecutablePath(exe, &exeSize) == 0 && realpath(exe, real) != NULL) {
        exePath = real;
    }
#endif
    slash = strrchr(exePath, '/');
#ifdef _WIN32
    const char* bslash = strrchr(exePath, '\\');
    if (bslash != NULL && (slash == NULL || bslash > slash)) {
        slash = bslash;
    }
#endif
    if (slash == NULL) {
        strcpy(sRelDir, "rel");
    } else {
        snprintf(sRelDir, sizeof(sRelDir), "%.*s/rel", (int)(slash - exePath), exePath);
    }
}

BOOL portRelLink(const char* areaName) {
    char path[1200];
    RelEntry prolog;
    if (sHandle != NULL) {
        portRelUnlink();
    }
    snprintf(path, sizeof(path), "%s/%s" REL_EXT, sRelDir[0] ? sRelDir : "rel", areaName);
    sHandle = openModule(path);
    if (sHandle == NULL) {
        fprintf(stderr, "portRelLink: cannot load %s: %s\n", path, moduleError());
        return FALSE;
    }
    prolog = findEntry(sHandle, "port_rel_prolog");
    sEpilog = findEntry(sHandle, "port_rel_epilog");
    if (prolog == NULL) {
        fprintf(stderr, "portRelLink: %s has no port_rel_prolog\n", path);
        closeModule(sHandle);
        sHandle = NULL;
        return FALSE;
    }
    prolog();
    return TRUE;
}

void portRelUnlink(void) {
    if (sHandle == NULL) {
        return;
    }
    if (sEpilog != NULL) {
        sEpilog();
    }
    closeModule(sHandle);
    sHandle = NULL;
    sEpilog = NULL;
}

#endif
