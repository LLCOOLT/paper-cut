/* Memory card (headless): the Dolphin CARD API over a folder of .gci files,
 * the same files at the same paths as Aurora's CARD (extern/aurora:
 * lib/dolphin/card.cpp with lib/card/CardGciFolder.cpp), so a save written by
 * one backend loads on the other (and in Dolphin: its GCI folder format).
 *
 *   <user>/<JAP|USA|EUR>/Card A/<maker>-<game>-<file name>.gci
 *
 * <user> is $TTYD_USER_DIR, else Aurora's default (SDL_GetPrefPath(NULL,
 * "ttyd")): %APPDATA%\ttyd on Windows, ~/Library/Application Support/ttyd on
 * macOS, $XDG_DATA_HOME/ttyd (~/.local/share/ttyd) elsewhere.
 *
 * A .gci is the 0x40-byte big-endian directory entry followed by the file's
 * blocks. Results and callbacks follow Aurora's: every call is synchronous,
 * an async call runs the operation and calls its callback before returning.
 * One difference: Aurora formats (creates) the "Card A" folder at CARDInit
 * when no card exists; here the card is formatted then too, but the folder is
 * only made when a file is first written, so runs that never save (tests,
 * sweeps) leave nothing behind. */
#include <dolphin.h>
#include <dolphin/card.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CARD_BLOCK_SIZE 0x2000u
#define CARD_FST_BLOCKS 5u
#define CARD_SIZE_MBIT 0x80u /* a 2043-block card (Card2043Mb) */
#define CARD_MBIT_TO_BLOCKS 0x10u
#define GCI_HEADER_SIZE 0x40u
#define GCI_NAME_MAX (12 + CARD_FILENAME_MAX + 1)
#define CARD_PATH_MAX 1024

/* The directory entry (Aurora's aurora::card::File), host byte order. */
typedef struct CardGciHeader {
    u8 game[4];
    u8 maker[2];
    u8 reserved;
    u8 bannerFlags;
    char fileName[CARD_FILENAME_MAX];
    u32 modifiedTime;
    u32 iconAddress;
    u16 iconFmt;
    u16 animSpeed;
    u8 permissions;
    u8 copyCounter;
    u16 firstBlock;
    u16 blockCount;
    u16 reserved2;
    u32 commentAddr;
} CardGciHeader;

typedef struct CardGciFile {
    CardGciHeader header;
    u32 fileSize; /* header + blocks */
    char diskName[GCI_NAME_MAX];
    BOOL opened;
    BOOL deleted;
    BOOL dirty;
} CardGciFile;

typedef struct CardChannel {
    char folder[CARD_PATH_MAX]; /* empty until opened (Aurora's m_folderPath) */
    s32 error;
    BOOL folderPending; /* formatted at CARDInit; the folder is made on first write */
    s32 fileCount;
    CardGciFile files[CARD_MAX_FILE];
} CardChannel;

static BOOL sInitialized;
static BOOL sFastMode;
static char sGame[5];
static char sMaker[3];
static char sCardPaths[2][CARD_PATH_MAX];
static CardChannel sChannels[2];

/* --- files, UTF-8 paths ------------------------------------------------- */

#ifdef _WIN32
static wchar_t* widen(const char* path) {
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t* w;
    if (n <= 0) {
        return NULL;
    }
    w = malloc((size_t)n * sizeof(wchar_t));
    if (w != NULL) {
        MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);
    }
    return w;
}

static BOOL narrow(const wchar_t* w, char* out, size_t cap) {
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL) > 0;
}

/* 0: missing, 1: regular file, 2: directory, 3: other */
static int pathKind(const char* path) {
    wchar_t* w = widen(path);
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
}

static FILE* fileOpen(const char* path, const char* mode) {
    wchar_t* w = widen(path);
    wchar_t wmode[8];
    FILE* f;
    size_t i;
    if (w == NULL) {
        return NULL;
    }
    for (i = 0; mode[i] != '\0' && i < 7; i++) {
        wmode[i] = (wchar_t)mode[i];
    }
    wmode[i] = 0;
    f = _wfopen(w, wmode);
    free(w);
    return f;
}

static BOOL makeDir(const char* path) {
    wchar_t* w = widen(path);
    BOOL ok;
    if (w == NULL) {
        return FALSE;
    }
    ok = CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
    free(w);
    return ok;
}

static BOOL renameReplace(const char* from, const char* to) {
    wchar_t* wf = widen(from);
    wchar_t* wt = widen(to);
    BOOL ok = wf != NULL && wt != NULL && MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING);
    free(wf);
    free(wt);
    return ok;
}

static void removeFile(const char* path) {
    wchar_t* w = widen(path);
    if (w != NULL) {
        DeleteFileW(w);
        free(w);
    }
}
#else
static int pathKind(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return lstat(path, &st) == 0 ? 3 : 0;
    }
    return S_ISDIR(st.st_mode) ? 2 : S_ISREG(st.st_mode) ? 1 : 3;
}

static FILE* fileOpen(const char* path, const char* mode) {
    return fopen(path, mode);
}

static BOOL makeDir(const char* path) {
    return mkdir(path, 0755) == 0 || pathKind(path) == 2;
}

static BOOL renameReplace(const char* from, const char* to) {
    return rename(from, to) == 0;
}

static void removeFile(const char* path) {
    remove(path);
}
#endif

static BOOL isSeparator(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/* mkdir -p */
static BOOL makeDirs(const char* path) {
    char buf[CARD_PATH_MAX];
    size_t len = strlen(path);
    size_t i;
    if (len == 0 || len >= sizeof(buf)) {
        return FALSE;
    }
    memcpy(buf, path, len + 1);
    for (i = 1; i < len; i++) {
        if (isSeparator(buf[i]) && !isSeparator(buf[i - 1]) && buf[i - 1] != ':') {
            char c = buf[i];
            buf[i] = '\0';
            if (pathKind(buf) != 2) {
                makeDir(buf);
            }
            buf[i] = c;
        }
    }
    if (pathKind(buf) != 2) {
        makeDir(buf);
    }
    return pathKind(buf) == 2;
}

static BOOL joinPath(char* out, size_t cap, const char* dir, const char* name) {
    size_t len = strlen(dir);
    while (len > 1 && isSeparator(dir[len - 1])) {
        len--;
    }
    return snprintf(out, cap, "%.*s/%s", (int)len, dir, name) < (int)cap;
}

static long fileSizeOf(FILE* f) {
    long size;
    if (fseek(f, 0, SEEK_END) != 0) {
        return -1;
    }
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    return size;
}

/* Write a whole file through a temporary and a rename (Aurora's
 * io::write_file_atomic), making its folder first. */
static BOOL writeFileAtomic(const char* path, const void* data, size_t size) {
    static unsigned sCounter;
    char dir[CARD_PATH_MAX];
    char tmp[CARD_PATH_MAX + 32];
    const char* slash = NULL;
    const char* p;
    FILE* f;
    BOOL ok;

    for (p = path; *p != '\0'; p++) {
        if (isSeparator(*p)) {
            slash = p;
        }
    }
    if (slash != NULL && slash != path) {
        snprintf(dir, sizeof(dir), "%.*s", (int)(slash - path), path);
        if (!makeDirs(dir)) {
            return FALSE;
        }
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp-%u-%u", path, (unsigned)time(NULL), sCounter++);
    f = fileOpen(tmp, "wb");
    if (f == NULL) {
        return FALSE;
    }
    ok = fwrite(data, 1, size, f) == size;
    ok = (fflush(f) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (!ok || !renameReplace(tmp, path)) {
        removeFile(tmp);
        return FALSE;
    }
    return TRUE;
}

/* Read a whole file; NULL if it can't be read. */
static u8* readFile(const char* path, long* sizeOut) {
    FILE* f = fileOpen(path, "rb");
    long size;
    u8* data;
    if (f == NULL) {
        return NULL;
    }
    size = fileSizeOf(f);
    data = size >= 0 ? malloc(size > 0 ? (size_t)size : 1) : NULL;
    if (data != NULL && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *sizeOut = size;
    return data;
}

/* --- directory entries ---------------------------------------------------- */

static u16 be16(const u8* p) {
    return (u16)((p[0] << 8) | p[1]);
}

static u32 be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static void put16(u8* p, u16 v) {
    p[0] = (u8)(v >> 8);
    p[1] = (u8)v;
}

static void put32(u8* p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static void headerDecode(const u8* raw, CardGciHeader* h) {
    memcpy(h->game, raw + 0x00, 4);
    memcpy(h->maker, raw + 0x04, 2);
    h->reserved = raw[0x06];
    h->bannerFlags = raw[0x07];
    memcpy(h->fileName, raw + 0x08, CARD_FILENAME_MAX);
    h->modifiedTime = be32(raw + 0x28);
    h->iconAddress = be32(raw + 0x2C);
    h->iconFmt = be16(raw + 0x30);
    h->animSpeed = be16(raw + 0x32);
    h->permissions = raw[0x34];
    h->copyCounter = raw[0x35];
    h->firstBlock = be16(raw + 0x36);
    h->blockCount = be16(raw + 0x38);
    h->reserved2 = be16(raw + 0x3A);
    h->commentAddr = be32(raw + 0x3C);
}

static void headerEncode(const CardGciHeader* h, u8* raw) {
    memcpy(raw + 0x00, h->game, 4);
    memcpy(raw + 0x04, h->maker, 2);
    raw[0x06] = h->reserved;
    raw[0x07] = h->bannerFlags;
    memcpy(raw + 0x08, h->fileName, CARD_FILENAME_MAX);
    put32(raw + 0x28, h->modifiedTime);
    put32(raw + 0x2C, h->iconAddress);
    put16(raw + 0x30, h->iconFmt);
    put16(raw + 0x32, h->animSpeed);
    raw[0x34] = h->permissions;
    raw[0x35] = h->copyCounter;
    put16(raw + 0x36, h->firstBlock);
    put16(raw + 0x38, h->blockCount);
    put16(raw + 0x3A, h->reserved2);
    put32(raw + 0x3C, h->commentAddr);
}

/* "<maker>-<game>-<name>.gci", characters a file system refuses as '_'
 * (Aurora's gci_filename). FALSE if the parts are malformed. */
static BOOL gciFileName(const char* game, size_t gameLen, const char* maker, size_t makerLen, const char* name,
                        size_t nameLen, char* out) {
    size_t i;
    size_t len;
    if (gameLen != 4 || makerLen != 2 || nameLen > CARD_FILENAME_MAX || memchr(game, '\0', 4) != NULL ||
        memchr(maker, '\0', 2) != NULL || memchr(name, '\0', nameLen) != NULL) {
        return FALSE;
    }
    memcpy(out, maker, 2);
    out[2] = '-';
    memcpy(out + 3, game, 4);
    out[7] = '-';
    memcpy(out + 8, name, nameLen);
    len = 8 + nameLen;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)out[i];
        if (c < 32 || strchr("<>:\"/\\|?*", c) != NULL) {
            out[i] = '_';
        }
    }
    memcpy(out + len, ".gci", 5);
    return TRUE;
}

static BOOL isGciFileName(const char* name) {
    char canonical[GCI_NAME_MAX];
    size_t len = strlen(name);
    if (len < 13 || len > 12 + CARD_FILENAME_MAX || name[2] != '-' || name[7] != '-' ||
        strcmp(name + len - 4, ".gci") != 0) {
        return FALSE;
    }
    return gciFileName(name + 3, 4, name, 2, name + 8, len - 12, canonical) && strcmp(canonical, name) == 0;
}

/* Seconds since 2000-01-01, local time (Aurora's getGCTime). */
static u32 cardTime(void) {
    time_t sysTime;
    time_t tzDST;
    time_t tzDiff;
    struct tm* tmp;

    time(&sysTime);
    tmp = localtime(&sysTime);
    tzDST = (tmp != NULL && tmp->tm_isdst == 1) ? 3600 : 0;
    tmp = gmtime(&sysTime);
    tzDiff = tmp != NULL ? sysTime - mktime(tmp) : 0;
    return (u32)((u64)(sysTime + tzDiff + tzDST) - 0x386D4380u);
}

static u32 bannerSize(u32 fmt) {
    return fmt == CARD_STAT_BANNER_C8 ? 3584 : fmt == CARD_STAT_BANNER_RGB5A3 ? 6144 : 0;
}

static u32 iconSize(u32 fmt) {
    return fmt == CARD_STAT_ICON_C8 ? 1024 : fmt == CARD_STAT_ICON_RGB5A3 ? 2048 : 0;
}

static u32 tlutSize(u32 fmt) {
    return fmt == CARD_STAT_ICON_C8 ? 512 : 0;
}

/* --- the card: a folder of .gci files (Aurora's CardGciFolder) -------------- */

static CardGciFile* cardFile(CardChannel* card, s32 fileNo) {
    if (card->error == CARD_RESULT_READY && fileNo >= 0 && fileNo < card->fileCount && !card->files[fileNo].deleted) {
        return &card->files[fileNo];
    }
    return NULL;
}

static CardGciFile* cardOpenFile(CardChannel* card, s32 fileNo) {
    CardGciFile* file = cardFile(card, fileNo);
    return file != NULL && file->opened ? file : NULL;
}

static s32 cardFind(CardChannel* card, const char* name) {
    s32 i;
    if (strlen(name) > CARD_FILENAME_MAX) {
        return -1;
    }
    for (i = 0; i < card->fileCount; i++) {
        const CardGciFile* f = &card->files[i];
        if (!f->deleted && memcmp(f->header.game, sGame, 4) == 0 && memcmp(f->header.maker, sMaker, 2) == 0 &&
            strncmp(f->header.fileName, name, CARD_FILENAME_MAX) == 0) {
            return i;
        }
    }
    return -1;
}

static void cardFreeBlocks(const CardChannel* card, s32* bytesNotUsed, s32* filesNotUsed) {
    s32 bytes = (s32)((CARD_SIZE_MBIT * CARD_MBIT_TO_BLOCKS - CARD_FST_BLOCKS) * CARD_BLOCK_SIZE);
    s32 files = CARD_MAX_FILE;
    s32 i;
    for (i = 0; i < card->fileCount; i++) {
        if (!card->files[i].deleted) {
            bytes -= (s32)(card->files[i].header.blockCount * CARD_BLOCK_SIZE);
            files--;
        }
    }
    *bytesNotUsed = bytes > 0 ? bytes : 0;
    *filesNotUsed = files;
}

static BOOL cardDiskPath(const CardChannel* card, const char* diskName, char* out) {
    return joinPath(out, CARD_PATH_MAX, card->folder, diskName);
}

/* Load the folder's .gci files. FALSE (NOCARD: no folder; IOERROR/BROKEN: a
 * bad file) leaves the card unusable, as Aurora's CardGciFolder::open. */
static BOOL cardLoadEntry(CardChannel* card, const char* diskName, CardGciFile* files, s32* count) {
    char path[CARD_PATH_MAX];
    char canonical[GCI_NAME_MAX];
    u8 raw[GCI_HEADER_SIZE];
    CardGciHeader header;
    const char* nameEnd;
    long size;
    FILE* f;

    if (strlen(diskName) < 4 || strcmp(diskName + strlen(diskName) - 4, ".gci") != 0 || !isGciFileName(diskName)) {
        return TRUE; /* not a card file */
    }
    if (!cardDiskPath(card, diskName, path)) {
        return FALSE;
    }
    if (pathKind(path) != 1) {
        return TRUE;
    }
    f = fileOpen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "[card] can't open %s\n", path);
        return FALSE;
    }
    size = fileSizeOf(f);
    if (fread(raw, 1, sizeof(raw), f) != sizeof(raw)) {
        fclose(f);
        fprintf(stderr, "[card] can't read %s\n", path);
        return FALSE;
    }
    fclose(f);
    headerDecode(raw, &header);
    nameEnd = memchr(header.fileName, '\0', CARD_FILENAME_MAX);
    if (!gciFileName((const char*)header.game, 4, (const char*)header.maker, 2, header.fileName,
                     nameEnd != NULL ? (size_t)(nameEnd - header.fileName) : CARD_FILENAME_MAX, canonical) ||
        strcmp(canonical, diskName) != 0) {
        return TRUE; /* its name doesn't match its CARD identity: ignored */
    }
    if (*count == CARD_MAX_FILE) {
        card->error = CARD_RESULT_BROKEN;
        return FALSE;
    }
    if (header.blockCount == 0 || size != (long)(GCI_HEADER_SIZE + header.blockCount * CARD_BLOCK_SIZE)) {
        fprintf(stderr, "[card] invalid GCI file size: %s\n", path);
        return FALSE;
    }
    memset(&files[*count], 0, sizeof(files[0]));
    files[*count].header = header;
    files[*count].fileSize = (u32)size;
    snprintf(files[*count].diskName, GCI_NAME_MAX, "%s", diskName);
    (*count)++;
    return TRUE;
}

static BOOL cardOpen(CardChannel* card, const char* folder) {
    static CardGciFile sFiles[CARD_MAX_FILE];
    s32 count = 0;
    BOOL ok = TRUE;

    card->fileCount = 0;
    card->error = CARD_RESULT_NOCARD;
    card->folderPending = FALSE;
    snprintf(card->folder, sizeof(card->folder), "%s", folder);
    if (pathKind(folder) != 2) {
        return FALSE;
    }
    card->error = CARD_RESULT_IOERROR;
#ifdef _WIN32
    {
        char pattern[CARD_PATH_MAX];
        wchar_t* w;
        WIN32_FIND_DATAW fd;
        HANDLE h;
        if (!joinPath(pattern, sizeof(pattern), folder, "*") || (w = widen(pattern)) == NULL) {
            return FALSE;
        }
        h = FindFirstFileW(w, &fd);
        free(w);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                char name[MAX_PATH * 3];
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !narrow(fd.cFileName, name, sizeof(name))) {
                    continue;
                }
                ok = cardLoadEntry(card, name, sFiles, &count);
            } while (ok && FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
#else
    {
        DIR* d = opendir(folder);
        struct dirent* e;
        if (d == NULL) {
            return FALSE;
        }
        while (ok && (e = readdir(d)) != NULL) {
            ok = cardLoadEntry(card, e->d_name, sFiles, &count);
        }
        closedir(d);
    }
#endif
    if (!ok) {
        return FALSE;
    }
    memcpy(card->files, sFiles, (size_t)count * sizeof(sFiles[0]));
    card->fileCount = count;
    card->error = CARD_RESULT_READY;
    return TRUE;
}

static void cardClose(CardChannel* card) {
    card->fileCount = 0;
    card->folder[0] = '\0';
    card->folderPending = FALSE;
    card->error = CARD_RESULT_NOCARD;
}

static s32 cardFormat(CardChannel* card) {
    if (card->folder[0] == '\0' || !makeDirs(card->folder)) {
        return CARD_RESULT_IOERROR;
    }
    card->folderPending = FALSE;
    return card->error = CARD_RESULT_READY;
}

/* Write dirty directory entries back to their files (Aurora's commit). */
static s32 cardCommit(CardChannel* card) {
    s32 i;
    if (card->error != CARD_RESULT_READY) {
        return card->error;
    }
    for (i = 0; i < card->fileCount; i++) {
        CardGciFile* file = &card->files[i];
        char path[CARD_PATH_MAX];
        long size;
        u8* data;
        if (file->deleted || !file->dirty) {
            continue;
        }
        if (!cardDiskPath(card, file->diskName, path) || (data = readFile(path, &size)) == NULL) {
            card->error = CARD_RESULT_IOERROR;
            return CARD_RESULT_IOERROR;
        }
        if (size < (long)GCI_HEADER_SIZE) {
            free(data);
            card->error = CARD_RESULT_IOERROR;
            return CARD_RESULT_IOERROR;
        }
        headerEncode(&file->header, data);
        if (!writeFileAtomic(path, data, (size_t)size)) {
            headerDecode(data, &file->header); /* unchanged on disk: take it back */
            file->dirty = FALSE;
            free(data);
            return CARD_RESULT_IOERROR;
        }
        free(data);
        file->dirty = FALSE;
    }
    return CARD_RESULT_READY;
}

/* A file at this card name's disk path that the card did not load (Aurora's
 * check_new_path). */
static BOOL cardPathTaken(const CardChannel* card, const char* diskName) {
    char path[CARD_PATH_MAX];
    return !cardDiskPath(card, diskName, path) || pathKind(path) != 0;
}

static s32 cardCreate(CardChannel* card, const char* name, u32 size, s32* fileNoOut) {
    char diskName[GCI_NAME_MAX];
    char path[CARD_PATH_MAX];
    CardGciHeader header;
    s32 freeBytes, freeFiles;
    s32 idx;
    u8* data;
    BOOL written;

    if (card->error != CARD_RESULT_READY) {
        return card->error;
    }
    if (strlen(name) > CARD_FILENAME_MAX) {
        return CARD_RESULT_NAMETOOLONG;
    }
    if (size == 0 || size % CARD_BLOCK_SIZE != 0) {
        return CARD_RESULT_FATAL_ERROR;
    }
    if (cardFind(card, name) >= 0) {
        return CARD_RESULT_EXIST;
    }
    cardFreeBlocks(card, &freeBytes, &freeFiles);
    if (freeFiles <= 0) {
        return CARD_RESULT_NOENT;
    }
    if (size > (u32)freeBytes) {
        return CARD_RESULT_INSSPACE;
    }
    if (!gciFileName(sGame, 4, sMaker, 2, name, strlen(name), diskName) || name[0] == '\0' ||
        !cardDiskPath(card, diskName, path)) {
        return CARD_RESULT_FATAL_ERROR;
    }
    if (pathKind(path) != 0) {
        return CARD_RESULT_EXIST;
    }

    memset(&header, 0, sizeof(header));
    strncpy(header.fileName, name, CARD_FILENAME_MAX);
    memcpy(header.game, sGame, 4);
    memcpy(header.maker, sMaker, 2);
    header.modifiedTime = cardTime();
    header.blockCount = (u16)(size / CARD_BLOCK_SIZE);
    header.firstBlock = CARD_FST_BLOCKS;
    header.permissions = CARD_ATTR_PUBLIC;
    header.iconAddress = 0xFFFFFFFFu;
    header.commentAddr = 0xFFFFFFFFu;
    header.animSpeed = 1;

    data = calloc(1, GCI_HEADER_SIZE + size);
    if (data == NULL) {
        return CARD_RESULT_IOERROR;
    }
    headerEncode(&header, data);
    written = writeFileAtomic(path, data, GCI_HEADER_SIZE + size);
    free(data);
    if (!written) {
        return CARD_RESULT_IOERROR;
    }
    card->folderPending = FALSE;

    for (idx = 0; idx < card->fileCount && !card->files[idx].deleted; idx++) {
    }
    if (idx == card->fileCount) {
        card->fileCount++;
    }
    memset(&card->files[idx], 0, sizeof(card->files[idx]));
    card->files[idx].header = header;
    card->files[idx].fileSize = GCI_HEADER_SIZE + size;
    snprintf(card->files[idx].diskName, GCI_NAME_MAX, "%s", diskName);
    card->files[idx].opened = TRUE;
    *fileNoOut = idx;
    return CARD_RESULT_READY;
}

static s32 cardDelete(CardChannel* card, s32 fileNo) {
    char deletedDir[CARD_PATH_MAX];
    char source[CARD_PATH_MAX];
    char destination[CARD_PATH_MAX];
    CardGciFile* file;
    if (card->error != CARD_RESULT_READY) {
        return card->error;
    }
    file = cardFile(card, fileNo);
    if (file == NULL) {
        return CARD_RESULT_NOFILE;
    }
    /* Aurora keeps deleted files in "_deleted" (overwriting an older one). */
    if (!joinPath(deletedDir, sizeof(deletedDir), card->folder, "_deleted") ||
        !cardDiskPath(card, file->diskName, source) ||
        !joinPath(destination, sizeof(destination), deletedDir, file->diskName) || !makeDirs(deletedDir) ||
        !renameReplace(source, destination)) {
        return CARD_RESULT_IOERROR;
    }
    file->opened = FALSE;
    file->deleted = TRUE;
    file->dirty = FALSE;
    return CARD_RESULT_READY;
}

/* Read or write [offset, offset + length) of an open file's data. */
static s32 cardAccess(CardChannel* card, s32 fileNo, void* buf, s32 length, s32 offset, BOOL write) {
    CardGciFile* file = cardOpenFile(card, fileNo);
    char path[CARD_PATH_MAX];
    u32 dataSize;
    long size;
    u8* data;
    s32 result;

    if (file == NULL) {
        return CARD_RESULT_NOCARD;
    }
    dataSize = file->fileSize - GCI_HEADER_SIZE;
    if (offset < 0 || (u32)offset > dataSize || length < 0 || (u32)length > dataSize - (u32)offset) {
        return CARD_RESULT_LIMIT;
    }
    if (!cardDiskPath(card, file->diskName, path) || (data = readFile(path, &size)) == NULL) {
        return CARD_RESULT_NOFILE;
    }
    result = CARD_RESULT_IOERROR;
    if (size >= (long)(GCI_HEADER_SIZE + (u32)offset + (u32)length)) {
        if (write) {
            memcpy(data + GCI_HEADER_SIZE + offset, buf, (size_t)length);
            if (writeFileAtomic(path, data, (size_t)size)) {
                result = CARD_RESULT_READY;
            }
        } else {
            memcpy(buf, data + GCI_HEADER_SIZE + offset, (size_t)length);
            result = CARD_RESULT_READY;
        }
    }
    free(data);
    return result;
}

/* --- the CARD API ------------------------------------------------------------ */

#define CHECK_CHAN(chan)                                                                                           \
    if ((chan) < 0 || (chan) >= 2) {                                                                               \
        return CARD_RESULT_FATAL_ERROR;                                                                            \
    }

/* Aurora's default user folder: SDL_GetPrefPath(NULL, "ttyd"). */
static void userDir(char* out, size_t cap) {
    const char* env = getenv("TTYD_USER_DIR");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, cap, "%s", env);
        return;
    }
#ifdef _WIN32
    {
        const wchar_t* appData = _wgetenv(L"APPDATA");
        char base[CARD_PATH_MAX];
        if (appData != NULL && narrow(appData, base, sizeof(base))) {
            snprintf(out, cap, "%s\\ttyd", base);
            return;
        }
    }
#elif defined(__APPLE__)
    if (getenv("HOME") != NULL) {
        snprintf(out, cap, "%s/Library/Application Support/ttyd", getenv("HOME"));
        return;
    }
#else
    if (getenv("XDG_DATA_HOME") != NULL && getenv("XDG_DATA_HOME")[0] == '/') {
        snprintf(out, cap, "%s/ttyd", getenv("XDG_DATA_HOME"));
        return;
    }
    if (getenv("HOME") != NULL) {
        snprintf(out, cap, "%s/.local/share/ttyd", getenv("HOME"));
        return;
    }
#endif
    snprintf(out, cap, ".");
}

void CARDInit(void) {
    static const char sGameId[] = PORT_GAME_ID;
    char user[CARD_PATH_MAX];
    char regionDir[CARD_PATH_MAX];
    const char* region;
    BOOL loaded = FALSE;
    s32 chan;

    if (sInitialized) {
        return;
    }
    sInitialized = TRUE;
    /* game and maker codes from the disc ID ("G8MJ01" -> "G8MJ", "01"), as
     * cardmgr hands Aurora's CARDInit */
    memcpy(sGame, sGameId, 4);
    memcpy(sMaker, sGameId + 4, 2);
    region = sGame[3] == 'J' ? "JAP" : sGame[3] == 'P' ? "EUR" : "USA";
    userDir(user, sizeof(user));
    joinPath(regionDir, sizeof(regionDir), user, region);

    for (chan = 0; chan < 2; chan++) {
        cardClose(&sChannels[chan]);
        joinPath(sCardPaths[chan], sizeof(sCardPaths[chan]), regionDir, chan == 0 ? "Card A" : "Card B");
        if (pathKind(sCardPaths[chan]) != 0) {
            if (cardOpen(&sChannels[chan], sCardPaths[chan])) {
                loaded = TRUE;
            } else {
                fprintf(stderr, "[card] failed to load %s\n", sCardPaths[chan]);
            }
        }
    }
    /* No card: Aurora formats slot A, an empty card (the folder: on first write). */
    if (!loaded && pathKind(sCardPaths[0]) == 0) {
        cardClose(&sChannels[0]);
        snprintf(sChannels[0].folder, sizeof(sChannels[0].folder), "%s", sCardPaths[0]);
        sChannels[0].error = CARD_RESULT_READY;
        sChannels[0].folderPending = TRUE;
    }
}

BOOL CARDSetFastMode(BOOL enable) {
    BOOL old = sFastMode;
    sFastMode = enable;
    return old;
}

static s32 cardProbe(s32 chan, s32* memSize, s32* sectorSize) {
    BOOL present = pathKind(sCardPaths[chan]) == 2 || sChannels[chan].folderPending;
    if (memSize != NULL) {
        *memSize = present ? CARD_SIZE_MBIT : 0;
    }
    if (sectorSize != NULL) {
        *sectorSize = present ? (s32)CARD_BLOCK_SIZE : 0;
    }
    return present ? CARD_RESULT_READY : CARD_RESULT_NOCARD;
}

int CARDProbe(s32 chan) {
    CHECK_CHAN(chan);
    return cardProbe(chan, NULL, NULL) != CARD_RESULT_NOCARD;
}

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize) {
    CHECK_CHAN(chan);
    return cardProbe(chan, memSize, sectorSize);
}

s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback) {
    (void)workArea;
    (void)detachCallback;
    CHECK_CHAN(chan);
    if (attachCallback != NULL) {
        attachCallback(chan, sChannels[chan].error);
    }
    return CARD_RESULT_READY;
}

s32 CARDUnmount(s32 chan) {
    CHECK_CHAN(chan);
    return CARD_RESULT_READY; /* as Aurora (ttyd-port-storage): the folder stays loaded */
}

s32 CARDGetResultCode(s32 chan) {
    CHECK_CHAN(chan);
    return sChannels[chan].error;
}

s32 CARDCheckAsync(s32 chan, CARDCallback callback) {
    CHECK_CHAN(chan);
    if (callback != NULL) {
        callback(chan, sChannels[chan].error);
    }
    return sChannels[chan].error;
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback) {
    s32 result;
    CHECK_CHAN(chan);
    result = cardFormat(&sChannels[chan]);
    if (callback != NULL) {
        callback(chan, result);
    }
    return result;
}

s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed) {
    CHECK_CHAN(chan);
    cardFreeBlocks(&sChannels[chan], byteNotUsed, filesNotUsed);
    return sChannels[chan].error;
}

s32 CARDGetSerialNo(s32 chan, u64* serialNo) {
    CHECK_CHAN(chan);
    *serialNo = 0;
    return CARD_RESULT_READY;
}

s32 CARDOpen(s32 chan, const char* fileName, CARDFileInfo* fileInfo) {
    CardChannel* card;
    char diskName[GCI_NAME_MAX];
    s32 idx;
    CHECK_CHAN(chan);
    card = &sChannels[chan];
    if (card->error != CARD_RESULT_READY) {
        return card->error;
    }
    idx = cardFind(card, fileName);
    if (idx < 0) {
        if (!gciFileName(sGame, 4, sMaker, 2, fileName, strlen(fileName), diskName) || fileName[0] == '\0') {
            return CARD_RESULT_FATAL_ERROR;
        }
        if (cardPathTaken(card, diskName)) {
            fprintf(stderr, "[card] '%s' is taken by a file that does not match its CARD identity\n", diskName);
            return card->error = CARD_RESULT_IOERROR;
        }
        return CARD_RESULT_NOFILE;
    }
    card->files[idx].opened = TRUE;
    fileInfo->chan = chan;
    fileInfo->fileNo = idx;
    fileInfo->offset = 0;
    return CARD_RESULT_READY;
}

s32 CARDClose(CARDFileInfo* fileInfo) {
    CardGciFile* file;
    CHECK_CHAN(fileInfo->chan);
    file = cardOpenFile(&sChannels[fileInfo->chan], fileInfo->fileNo);
    if (file == NULL) {
        return CARD_RESULT_NOFILE;
    }
    file->opened = FALSE;
    return CARD_RESULT_READY;
}

s32 CARDCancel(CARDFileInfo* fileInfo) {
    (void)fileInfo;
    return CARD_RESULT_NOCARD; /* nothing is ever in flight (as Aurora) */
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback) {
    s32 fileNo = -1;
    s32 result;
    CHECK_CHAN(chan);
    result = cardCreate(&sChannels[chan], fileName, size, &fileNo);
    if (result == CARD_RESULT_READY) {
        fileInfo->chan = chan;
        fileInfo->fileNo = fileNo;
        fileInfo->offset = 0;
        result = cardCommit(&sChannels[chan]);
    } else {
        fprintf(stderr, "[card] failed to create %s: %d\n", fileName, (int)result);
    }
    if (callback != NULL) {
        callback(chan, result);
    }
    return result;
}

s32 CARDDelete(s32 chan, const char* fileName) {
    CardChannel* card;
    s32 result;
    s32 idx;
    CHECK_CHAN(chan);
    card = &sChannels[chan];
    if (card->error != CARD_RESULT_READY) {
        return card->error;
    }
    idx = cardFind(card, fileName);
    result = idx < 0 ? CARD_RESULT_NOFILE : cardDelete(card, idx);
    return result == CARD_RESULT_READY ? cardCommit(card) : result;
}

s32 CARDReadAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset, CARDCallback callback) {
    s32 result = CARD_RESULT_FATAL_ERROR;
    if (fileInfo->chan >= 0 && fileInfo->chan < 2) {
        result = cardAccess(&sChannels[fileInfo->chan], fileInfo->fileNo, buf, length, offset, FALSE);
    }
    if (callback != NULL) {
        callback(fileInfo->chan, result);
    }
    return result;
}

s32 CARDWriteAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset, CARDCallback callback) {
    s32 result = CARD_RESULT_FATAL_ERROR;
    if (fileInfo->chan >= 0 && fileInfo->chan < 2) {
        result = cardAccess(&sChannels[fileInfo->chan], fileInfo->fileNo, buf, length, offset, TRUE);
        if (result == CARD_RESULT_READY) {
            result = cardCommit(&sChannels[fileInfo->chan]);
        }
    }
    if (callback != NULL) {
        callback(fileInfo->chan, result);
    }
    return result;
}

s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat) {
    const CardGciHeader* h;
    CardGciFile* file;
    CHECK_CHAN(chan);
    file = cardFile(&sChannels[chan], fileNo);
    if (file == NULL) {
        return CARD_RESULT_NOFILE;
    }
    h = &file->header;
    memset(stat->fileName, 0, sizeof(stat->fileName));
    strncpy(stat->fileName, h->fileName, CARD_FILENAME_MAX);
    stat->length = h->blockCount * CARD_BLOCK_SIZE;
    stat->time = h->modifiedTime;
    memcpy(stat->gameName, h->game, 4);
    memcpy(stat->company, h->maker, 2);
    stat->bannerFormat = h->bannerFlags;
    stat->iconAddr = h->iconAddress;
    stat->iconFormat = h->iconFmt;
    stat->iconSpeed = h->animSpeed;
    stat->commentAddr = h->commentAddr;
    if (h->iconAddress == 0xFFFFFFFFu) {
        s32 i;
        stat->offsetBanner = 0xFFFFFFFFu;
        stat->offsetBannerTlut = 0xFFFFFFFFu;
        for (i = 0; i < CARD_ICON_MAX; i++) {
            stat->offsetIcon[i] = 0xFFFFFFFFu;
        }
        stat->offsetIconTlut = 0xFFFFFFFFu;
        stat->offsetData = h->commentAddr + 64;
    } else {
        u32 cur = h->iconAddress;
        BOOL palette = FALSE;
        s32 i;
        stat->offsetBanner = cur;
        cur += bannerSize(CARDGetBannerFormat(stat));
        stat->offsetBannerTlut = cur;
        cur += tlutSize(CARDGetBannerFormat(stat));
        for (i = 0; i < CARD_ICON_MAX; i++) {
            u32 fmt = CARDGetIconFormat(stat, i);
            stat->offsetIcon[i] = cur;
            if (fmt == CARD_STAT_ICON_C8) {
                palette = TRUE;
            }
            cur += iconSize(fmt);
        }
        if (palette) {
            stat->offsetIconTlut = cur;
            cur += tlutSize(CARD_STAT_ICON_C8);
        } else {
            stat->offsetIconTlut = 0xFFFFFFFFu;
        }
        stat->offsetData = cur;
    }
    return CARD_RESULT_READY;
}

s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat, CARDCallback callback) {
    CardGciFile* file;
    s32 result;
    CHECK_CHAN(chan);
    file = cardFile(&sChannels[chan], fileNo);
    if (file == NULL) {
        result = CARD_RESULT_NOFILE;
    } else {
        file->header.bannerFlags = stat->bannerFormat;
        file->header.iconAddress = stat->iconAddr;
        file->header.iconFmt = stat->iconFormat;
        file->header.animSpeed = stat->iconSpeed;
        file->header.commentAddr = stat->commentAddr;
        file->dirty = TRUE;
        result = cardCommit(&sChannels[chan]);
    }
    if (callback != NULL) {
        callback(chan, result);
    }
    return result;
}
