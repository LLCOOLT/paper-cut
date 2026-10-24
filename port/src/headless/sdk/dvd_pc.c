/* DVD replacement: reads files from the extracted disc filesystem.
 * The data root is $TTYD_DATA_DIR, or "data" next to the working directory;
 * it must contain the disc's files (the directory holding "a/", "rel/", ...).
 * Reads complete synchronously; async callbacks run before the call returns. */
#include <dolphin.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "port/dtk_pc.h"
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#include <strings.h>
#endif

#define DVD_MAX_ENTRIES 8192
#define DVD_PATH_MAX 512

static char sRoot[DVD_PATH_MAX];
static char* sEntries[DVD_MAX_ENTRIES]; /* host path per entry number */
static s32 sEntryCount;

/* main_headless.c: the root it checked (the disc's files/ when given the
 * extracted disc's top directory) */
void portDvdSetRoot(const char* root) {
    snprintf(sRoot, sizeof(sRoot), "%s", root);
}

static const char* dataRoot(void) {
    if (sRoot[0] == '\0') {
        const char* env = getenv("TTYD_DATA_DIR");
        snprintf(sRoot, sizeof(sRoot), "%s", env != NULL && env[0] != '\0' ? env : "data");
    }
    return sRoot;
}

void DVDInit(void) {
    dataRoot();
}

#ifndef _WIN32
/* Disc paths are case-insensitive; resolve each component on the host. */
static BOOL resolveComponent(char* dir, const char* name, size_t cap) {
    DIR* d = opendir(dir[0] ? dir : ".");
    struct dirent* ent;
    BOOL found = FALSE;
    if (d == NULL) {
        return FALSE;
    }
    while ((ent = readdir(d)) != NULL) {
        if (strcasecmp(ent->d_name, name) == 0) {
            size_t len = strlen(dir);
            snprintf(dir + len, cap - len, "/%s", ent->d_name);
            found = TRUE;
            break;
        }
    }
    closedir(d);
    return found;
}
#endif

static BOOL hostPath(const char* discPath, char* out, size_t cap) {
    FILE* f;
    while (*discPath == '/') {
        discPath++;
    }
    snprintf(out, cap, "%s/%s", dataRoot(), discPath);
    if ((f = fopen(out, "rb")) != NULL) {
        fclose(f);
        return TRUE;
    }
#ifndef _WIN32
    {
        char comp[DVD_PATH_MAX];
        const char* p = discPath;
        snprintf(out, cap, "%s", dataRoot());
        while (*p != '\0') {
            size_t n = strcspn(p, "/");
            if (n >= sizeof(comp)) {
                return FALSE;
            }
            memcpy(comp, p, n);
            comp[n] = '\0';
            if (n != 0 && !resolveComponent(out, comp, cap)) {
                return FALSE;
            }
            p += n;
            while (*p == '/') {
                p++;
            }
        }
        if ((f = fopen(out, "rb")) != NULL) {
            fclose(f);
            return TRUE;
        }
    }
#endif
    return FALSE;
}

s32 DVDConvertPathToEntrynum(const char* pathPtr) {
    char path[DVD_PATH_MAX];
    s32 i;
    if (!hostPath(pathPtr, path, sizeof(path))) {
        return -1;
    }
    for (i = 0; i < sEntryCount; i++) {
        if (strcmp(sEntries[i], path) == 0) {
            return i;
        }
    }
    if (sEntryCount == DVD_MAX_ENTRIES) {
        OSPanic(__FILE__, __LINE__, "DVD: too many distinct files");
    }
    sEntries[sEntryCount] = strdup(path);
    return sEntryCount++;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
    FILE* f;
    long size;
    if (entrynum < 0 || entrynum >= sEntryCount) {
        return FALSE;
    }
    if ((f = fopen(sEntries[entrynum], "rb")) == NULL) {
        return FALSE;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fclose(f);
    memset(fileInfo, 0, sizeof(*fileInfo));
    fileInfo->startAddr = (u32)entrynum; /* entry number, not a disc offset */
    fileInfo->length = (u32)size;
    return TRUE;
}

BOOL DVDOpen(const char* fileName, DVDFileInfo* fileInfo) {
    s32 entry = DVDConvertPathToEntrynum(fileName);
    if (entry < 0) {
        OSReport("DVDOpen: %s not found under %s\n", fileName, dataRoot());
        return FALSE;
    }
    return DVDFastOpen(entry, fileInfo);
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
    return TRUE;
}

static s32 readEntry(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset) {
    FILE* f;
    size_t got;
    if ((s32)fileInfo->startAddr >= sEntryCount) {
        return -1;
    }
    if (addr == NULL) {
        OSReport("DVD: read of %s (0x%X bytes at 0x%X) into a NULL buffer\n", sEntries[fileInfo->startAddr],
                 (unsigned)length, (unsigned)offset);
        return -1;
    }
    if ((f = fopen(sEntries[fileInfo->startAddr], "rb")) == NULL) {
        return -1;
    }
    fseek(f, offset, SEEK_SET);
    got = fread(addr, 1, (size_t)length, f);
    fclose(f);
    fileInfo->cb.transferredSize = (u32)got;
    fileInfo->cb.state = 0;
    return (s32)got;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
    return readEntry(fileInfo, addr, length, offset);
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset,
                      DVDCallback callback, s32 prio) {
    s32 result = readEntry(fileInfo, addr, length, offset);
    fileInfo->callback = callback;
    if (callback != NULL) {
        callback(result, fileInfo);
    }
    return result >= 0;
}

s32 DVDGetFileInfoStatus(const DVDFileInfo* fileInfo) {
    return 0; /* DVD_STATE_END */
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
    return 0;
}

s32 DVDGetDriveStatus(void) {
    return 0;
}

s32 DVDCancel(volatile DVDCommandBlock* block) {
    return 0;
}

/* Streamed disc audio (ADP): port/src/common/dtk_core.c. The stream source
 * is the file's entry number, so it survives the game closing its handle. */
static s32 dtkRead(void* handle, void* dst, u32 length, u32 offset) {
    DVDFileInfo info;
    memset(&info, 0, sizeof(info));
    info.startAddr = (u32)(uintptr_t)handle;
    return readEntry(&info, dst, (s32)length, (s32)offset);
}

BOOL DVDPrepareStreamAsync(DVDFileInfo* fileInfo, u32 length, u32 offset, DVDCallback callback) {
    if (length == 0 && offset <= fileInfo->length) {
        length = fileInfo->length - offset;
    }
    portDTKPrepare((void*)(uintptr_t)fileInfo->startAddr, dtkRead, offset, length);
    fileInfo->cb.state = 0;
    fileInfo->callback = callback;
    if (callback != NULL) {
        callback(0, fileInfo);
    }
    return TRUE;
}

s32 DVDPrepareStream(DVDFileInfo* fileInfo, u32 length, u32 offset) {
    DVDPrepareStreamAsync(fileInfo, length, offset, NULL);
    return 0;
}

s32 DVDCancelStream(DVDCommandBlock* block) {
    portDTKCancel();
    return 0;
}

s32 DVDStopStreamAtEnd(DVDCommandBlock* block) {
    portDTKStopAtEnd();
    return 0;
}

BOOL DVDCheckDisk(void) {
    return TRUE;
}
