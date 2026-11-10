/* DVD replacement: reads files from the extracted disc filesystem.
 * The data root is $TTYD_DATA_DIR, or "data" next to the working directory;
 * it must contain the disc's files (the directory holding "a/", "rel/", ...).
 * Reads complete synchronously; async callbacks run before the call returns. */
#include "port/file_pc.h"
#include <dolphin.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "port/dtk_pc.h"
#include <stdatomic.h>
#ifdef _WIN32
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#endif

/* a regular file at path (POSIX fopen also opens directories) */
static BOOL isFile(const char* path) {
#ifdef _WIN32
    FILE* f = portFopen(path, "rb");
    if (f != NULL) {
        fclose(f);
    }
    return f != NULL;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

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

/* A GameCube disc image instead of the extracted disc (main_headless.c: the
 * game's .iso as the first argument, as on Aurora). Its file system table
 * (FST, offset at 0x424): 12-byte entries, a directory's byte 0 set, its
 * last word the index past its contents; a file's words its offset and
 * size. An entry number is the FST index. */
static FILE* sIso;
/* the whole image mapped (read-only): a read is a copy, and parallel test
 * runs share the system's cache of the disc */
static const u8* sIsoMap;
static u64 sIsoSize;
static u8* sFst;
static u32 sFstCount;
static const char* sFstNames;
static u32 sFstNamesSize;
/* reads through sIso (no mapping): a seek and a read, one host thread at a time */
static atomic_flag sIsoLock = ATOMIC_FLAG_INIT;

static u32 be32(const u8* p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static BOOL isoSeek(u64 offset) {
#ifdef _WIN32
    return _fseeki64(sIso, (long long)offset, SEEK_SET) == 0;
#else
    return fseeko(sIso, (off_t)offset, SEEK_SET) == 0;
#endif
}

static void mapIso(const char* path) {
#ifdef _WIN32
    wchar_t w[1024]; /* (UTF-8 paths: port/file_pc.h) */
    HANDLE file = MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024) > 0
                      ? CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
                      : INVALID_HANDLE_VALUE;
    LARGE_INTEGER size;
    HANDLE mapping;
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    if (GetFileSizeEx(file, &size) && (mapping = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL)) != NULL) {
        sIsoMap = (const u8*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        sIsoSize = sIsoMap != NULL ? (u64)size.QuadPart : 0;
        CloseHandle(mapping); /* (the view keeps it) */
    }
    CloseHandle(file);
#else
    const int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0) {
        return;
    }
    if (fstat(fd, &st) == 0) {
        void* p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (p != MAP_FAILED) {
            sIsoMap = (const u8*)p;
            sIsoSize = (u64)st.st_size;
        }
    }
    close(fd);
#endif
}

BOOL portDvdOpenIso(const char* path, char id[6]) {
    u8 head[0x430];
    u32 fstOff, fstSize;
    FILE* f = portFopen(path, "rb");
    if (f == NULL) {
        return FALSE;
    }
    if (fread(head, 1, sizeof(head), f) != sizeof(head) || be32(head + 0x1C) != 0xC2339F3Du) {
        fclose(f); /* (0x1C: the GameCube disc magic) */
        return FALSE;
    }
    fstOff = be32(head + 0x424);
    fstSize = be32(head + 0x428);
    sIso = f;
    /* one byte more, a NUL: the last name ends within the table even on a
     * damaged disc */
    sFst = (u8*)malloc((size_t)fstSize + 1);
    if (sFst == NULL || !isoSeek(fstOff) || fread(sFst, 1, fstSize, f) != fstSize || fstSize < 12) {
        fclose(f);
        free(sFst);
        sFst = NULL;
        sIso = NULL;
        return FALSE;
    }
    sFst[fstSize] = 0;
    sFstCount = be32(sFst + 8);
    if (sFstCount == 0 || (u64)sFstCount * 12 > fstSize) {
        fclose(f);
        free(sFst);
        sFst = NULL;
        sIso = NULL;
        return FALSE;
    }
    sFstNames = (const char*)sFst + sFstCount * 12;
    sFstNamesSize = fstSize - sFstCount * 12;
    memcpy(id, head, 6);
    snprintf(sRoot, sizeof(sRoot), "%s", path);
    mapIso(path);
    return TRUE;
}

static const char* fstName(u32 i) {
    const u32 off = be32(sFst + i * 12) & 0xFFFFFF;
    return off < sFstNamesSize ? sFstNames + off : "";
}

/* the FST index of a disc path, or -1 */
static s32 isoLookup(const char* discPath) {
    u32 dir = 0, i, end;
    const char* p = discPath;
    for (;;) {
        char comp[DVD_PATH_MAX];
        size_t n;
        while (*p == '/') {
            p++;
        }
        n = strcspn(p, "/");
        if (n == 0) {
            return (s32)dir;
        }
        if (n >= sizeof(comp)) {
            return -1;
        }
        memcpy(comp, p, n);
        comp[n] = '\0';
        p += n;
        if (strcmp(comp, ".") == 0) {
            continue;
        }
        /* (a directory's end and a subdirectory's next index from the image: kept within the table and
           moving forward, so a malformed image reads nothing past it and never loops) */
        end = dir == 0 ? sFstCount : be32(sFst + dir * 12 + 8);
        if (end > sFstCount) {
            end = sFstCount;
        }
        for (i = dir + 1; i < end;) {
            const BOOL isDir = sFst[i * 12] != 0;
#ifdef _WIN32
            if (_stricmp(fstName(i), comp) == 0) {
#else
            if (strcasecmp(fstName(i), comp) == 0) {
#endif
                break;
            }
            if (isDir) {
                const u32 next = be32(sFst + i * 12 + 8);
                i = next > i ? next : end;
            } else {
                i++;
            }
        }
        if (i >= end) {
            return -1;
        }
        if (sFst[i * 12] == 0) {
            while (*p == '/') {
                p++;
            }
            return *p == '\0' ? (s32)i : -1;
        }
        dir = i;
    }
}

/* main_headless.c: a disc file by its path (the disc check's marker files) */
BOOL portDvdHasFile(const char* discPath) {
    char path[DVD_PATH_MAX];
    if (sIso != NULL) {
        const s32 i = isoLookup(discPath);
        return i > 0 && sFst[i * 12] == 0;
    }
    while (*discPath == '/') {
        discPath++;
    }
    snprintf(path, sizeof(path), "%s/%s", dataRoot(), discPath);
    return isFile(path);
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
    while (*discPath == '/') {
        discPath++;
    }
    snprintf(out, cap, "%s/%s", dataRoot(), discPath);
    if (isFile(out)) {
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
        if (isFile(out)) {
            return TRUE;
        }
    }
#endif
    return FALSE;
}

/* Disc files the port itself adds (port/include/port/mods.h; Aurora's are in
 * port/mods/mods.cpp): the Play as models made at startup
 * (port/src/common/luigi_model_pc.c), so headless plays the same animations
 * as Aurora. They win over the disc's; their entry numbers are HOST_ENTRY up. */
#define HOST_MAX 16
#define HOST_ENTRY 0x40000000
static struct {
    char* path; /* without the leading '/' */
    unsigned char* data;
    u32 size;
} sHost[HOST_MAX];
static int sHostCount;

static int samePath(const char* a, const char* b) {
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcasecmp(a, b) == 0;
#endif
}

void portModsSetHostFile(const char* discPath, const void* data, size_t size) {
    int i;
    while (*discPath == '/') {
        discPath++;
    }
    for (i = 0; i < sHostCount && !samePath(sHost[i].path, discPath); i++) {
    }
    if (i == HOST_MAX) {
        OSReport("DVD: too many host files (%s)\n", discPath);
        return;
    }
    if (i == sHostCount) {
        sHost[i].path = strdup(discPath);
        sHostCount++;
    }
    free(sHost[i].data);
    sHost[i].data = (unsigned char*)malloc(size != 0 ? size : 1);
    memcpy(sHost[i].data, data, size);
    sHost[i].size = (u32)size;
}

/* the host file at a disc path: its entry number, or -1 */
static s32 hostLookup(const char* discPath) {
    int i;
    while (*discPath == '/') {
        discPath++;
    }
    for (i = 0; i < sHostCount; i++) {
        if (samePath(sHost[i].path, discPath)) {
            return HOST_ENTRY + i;
        }
    }
    return -1;
}

/* the host file an entry number is (its index), or -1 */
static int hostIndex(s32 entry) {
    return entry >= HOST_ENTRY && entry < HOST_ENTRY + sHostCount ? entry - HOST_ENTRY : -1;
}

/* the loose file an entry number is (its host path), or NULL; unsigned, so a
 * startAddr past 0x7FFFFFFF is never a negative index */
static const char* entryPath(u32 entry) {
    return entry < (u32)sEntryCount && entry < DVD_MAX_ENTRIES ? sEntries[entry] : NULL;
}

s32 DVDConvertPathToEntrynum(const char* pathPtr) {
    char path[DVD_PATH_MAX];
    s32 i;
    if ((i = hostLookup(pathPtr)) >= 0) {
        return i;
    }
    if (sIso != NULL) {
        return isoLookup(pathPtr);
    }
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
    if (hostIndex(entrynum) >= 0) {
        memset(fileInfo, 0, sizeof(*fileInfo));
        fileInfo->startAddr = (u32)entrynum;
        fileInfo->length = sHost[hostIndex(entrynum)].size;
        return TRUE;
    }
    if (sIso != NULL) {
        if (entrynum <= 0 || (u32)entrynum >= sFstCount || sFst[entrynum * 12] != 0) {
            return FALSE;
        }
        memset(fileInfo, 0, sizeof(*fileInfo));
        fileInfo->startAddr = (u32)entrynum;
        fileInfo->length = be32(sFst + entrynum * 12 + 8);
        return TRUE;
    }
    if (entrynum < 0 || entryPath((u32)entrynum) == NULL) {
        return FALSE;
    }
    if ((f = portFopen(entryPath((u32)entrynum), "rb")) == NULL) {
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
    const char* path;
    const int host = hostIndex((s32)fileInfo->startAddr);
    if (host >= 0) {
        got = offset < 0 || (u32)offset >= sHost[host].size ? 0 : sHost[host].size - (u32)offset;
        got = got < (size_t)length ? got : (size_t)length;
        if (got != 0 && addr == NULL) {
            return -1;
        }
        if (got != 0) {
            memcpy(addr, sHost[host].data + offset, got);
        }
        fileInfo->cb.transferredSize = (u32)got;
        fileInfo->cb.state = 0;
        return (s32)got;
    }
    if (sIso != NULL) {
        const u32 i = fileInfo->startAddr;
        u32 size;
        if (i == 0 || i >= sFstCount || sFst[i * 12] != 0) {
            return -1;
        }
        size = be32(sFst + i * 12 + 8);
        if (length == 0 || (u32)offset >= size) {
            fileInfo->cb.transferredSize = 0;
            fileInfo->cb.state = 0;
            return 0;
        }
        if (addr == NULL) {
            return -1;
        }
        if ((u32)length > size - (u32)offset) {
            length = (s32)(size - (u32)offset); /* (as a loose file's short read) */
        }
        if (sIsoMap != NULL) {
            const u64 at = (u64)be32(sFst + i * 12 + 4) + (u32)offset;
            got = at >= sIsoSize ? 0 : (size_t)(sIsoSize - at < (u64)length ? sIsoSize - at : (u64)length);
            memcpy(addr, sIsoMap + at, got);
            fileInfo->cb.transferredSize = (u32)got;
            fileInfo->cb.state = 0;
            return (s32)got;
        }
        while (atomic_flag_test_and_set_explicit(&sIsoLock, memory_order_acquire)) {
        }
        if (!isoSeek((u64)be32(sFst + i * 12 + 4) + (u32)offset)) {
            atomic_flag_clear_explicit(&sIsoLock, memory_order_release);
            return -1;
        }
        got = fread(addr, 1, (size_t)length, sIso);
        atomic_flag_clear_explicit(&sIsoLock, memory_order_release);
        fileInfo->cb.transferredSize = (u32)got;
        fileInfo->cb.state = 0;
        return (s32)got;
    }
    path = entryPath(fileInfo->startAddr);
    if (path == NULL) {
        return -1;
    }
    if (length == 0) {
        /* empty files exist on the disc (msg/hom_10.txt): the game reads them
         * into _mapAlloc(0), which is NULL; a 0-byte read completes */
        fileInfo->cb.transferredSize = 0;
        fileInfo->cb.state = 0;
        return 0;
    }
    if (addr == NULL) {
        OSReport("DVD: read of %s (0x%X bytes at 0x%X) into a NULL buffer\n", path, (unsigned)length,
                 (unsigned)offset);
        return -1;
    }
    if ((f = portFopen(path, "rb")) == NULL) {
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
