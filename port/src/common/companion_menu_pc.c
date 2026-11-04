/* The second screen's Start menu's own memory, files and effects
 * (port/include/port/companion_menu.h): while its frame runs
 * (gPortCompanionMenu), memory.c, filemgr.c and effdrv.c hand its requests
 * here, so the game's heaps, file list and effect list are never touched.
 * Its memory is the host's; its files are read from the disc at once and
 * kept for the run (a few textures and models); its effects are entries of
 * its own that effMain never sees. */
#include "port/companion_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/dvd.h>
#include "driver/effdrv.h"
#include "manager/filemgr.h"
#include "memory.h"
#ifdef PORT_MODS
#include "port/mods.h"
#endif

int gPortCompanionMenu;

void portFileDecode(void* data, s32 type, u32 size, const char* name); /* filemgr.c */

/* --- memory: 32-byte aligned, zeroed, with a mark in front --- */

#define MARK 0x43504D4Eu /* "CPMN" */
#define FRONT 32

typedef struct Block {
    unsigned int mark;
    unsigned int size;
    void* raw;
} Block;

static void sayOnce(const char* what) {
    static int sSaid;
    if (sSaid < 8) {
        sSaid++;
        printf("[companion] menu: %s\n", what);
        fflush(stdout);
    }
}

void* portCompanionAlloc(unsigned int size) {
    unsigned char* raw = (unsigned char*)calloc(1, (size_t)size + FRONT + 31);
    unsigned char* p;
    Block* b;
    if (raw == NULL) {
        return NULL;
    }
    p = (unsigned char*)(((uintptr_t)raw + FRONT + 31) & ~(uintptr_t)31);
    b = (Block*)(p - sizeof(Block));
    b->mark = MARK;
    b->size = size;
    b->raw = raw;
    return p;
}

void portCompanionFree(void* p) {
    Block* b;
    if (p == NULL) {
        return;
    }
    b = (Block*)((unsigned char*)p - sizeof(Block));
    if (b->mark != MARK) {
        sayOnce("a free of memory not its own, left alone");
        return;
    }
    b->mark = 0;
    free(b->raw);
}

/* --- the smart heap's blocks --- */

#define SMART_MAX 512
static smartEntry sSmart[SMART_MAX]; /* inUse 1 while allocated */

static int ownSmart(const smartEntry* e) {
    return e >= &sSmart[0] && e < &sSmart[SMART_MAX];
}

smartEntry* portCompanionSmartAlloc(unsigned int size, unsigned char type) {
    int i;
    for (i = 0; i < SMART_MAX; i++) {
        if (!sSmart[i].inUse) {
            smartEntry* e = &sSmart[i];
            memset(e, 0, sizeof(*e));
            e->address = portCompanionAlloc(size);
            if (e->address == NULL) {
                return NULL;
            }
            e->size = size;
            e->type = type;
            e->inUse = 1;
            return e;
        }
    }
    sayOnce("out of smart blocks");
    return NULL;
}

void portCompanionSmartFree(smartEntry* entry) {
    if (entry == NULL) {
        return;
    }
    if (!ownSmart(entry)) {
        sayOnce("a smartFree of a block not its own, left alone");
        return;
    }
    if (entry->inUse) {
        portCompanionFree(entry->address);
        memset(entry, 0, sizeof(*entry));
    }
}

void portCompanionFrameEnd(void) {
    int i;
    /* type 3 is the frame's own (smartAutoFree(3) each game frame) */
    for (i = 0; i < SMART_MAX; i++) {
        if (sSmart[i].inUse && (sSmart[i].type == 3 || sSmart[i].type == 4)) {
            portCompanionSmartFree(&sSmart[i]);
        }
    }
}

/* --- files --- */

typedef struct CFile {
    FileEntry entry; /* what the game's code holds */
    struct CFile* next;
} CFile;

static CFile* sFiles;

FileEntry* portCompanionFileAlloc(const char* name, int type) {
    CFile* f;
    DVDFileInfo info;
    const char* path = name;
    unsigned char* data;
    u32 size;
    for (f = sFiles; f != NULL; f = f->next) {
        if (strcmp(f->entry.filename, name) == 0) {
            f->entry.state = 1;
            f->entry.references++;
            return &f->entry;
        }
    }
#ifdef PORT_MODS
    path = portModsPathAlias(path);
#endif
    if (DVDConvertPathToEntrynum(path) == -1 || !DVDOpen(path, &info)) {
        return NULL;
    }
    size = (info.length + 31) & ~31u;
    if (size == 0) {
        DVDClose(&info);
        return NULL;
    }
    /* never let go: decoded files are known to the decoders by address */
    data = (unsigned char*)portCompanionAlloc(size);
    if (data == NULL || DVDReadPrio(&info, data, (s32)size, 0, 2) < 0) {
        DVDClose(&info);
        sayOnce("a file it could not read");
        return NULL;
    }
    DVDClose(&info);
    f = (CFile*)calloc(1, sizeof(CFile));
    if (f == NULL) {
        return NULL;
    }
    f->entry.state = 1;
    f->entry.type = (s8)type;
    f->entry.references = 1;
    f->entry.dataSlot = data;
    f->entry.data = &f->entry.dataSlot;
    snprintf(f->entry.filename, sizeof(f->entry.filename), "%s", name);
    portFileDecode(data, type, size, name);
    f->next = sFiles;
    sFiles = f;
    return &f->entry;
}

void portCompanionFileFree(FileEntry* entry) {
    if (entry != NULL && entry->references > 0) {
        entry->references--; /* kept for the run all the same */
    }
}

/* --- effects --- */

#define EFF_MAX 32
static EffectEntry sEff[EFF_MAX];

EffectEntry* portCompanionEffEntry(void) {
    int i;
    for (i = 0; i < EFF_MAX; i++) {
        if (!sEff[i].flags) {
            EffectEntry* e = &sEff[i];
            memset(e, 0, sizeof(*e));
            e->flags = EFFECT_FLAG_IN_USE;
            return e;
        }
    }
    sayOnce("out of effect entries");
    return &sEff[EFF_MAX - 1];
}

void portCompanionEffDelete(EffectEntry* entry) {
    if (entry == NULL || entry < &sEff[0] || entry >= &sEff[EFF_MAX]) {
        sayOnce("an effDelete of an effect not its own, left alone");
        return;
    }
    if (entry->flags) {
        portCompanionFree(entry->userdata);
        memset(entry, 0, sizeof(*entry));
    }
}

/* --- the pointer, this frame --- */

static int sPointKind;
static float sPointX, sPointY;

void portCompanionMenuSetPoint(int kind, float gx, float gy) {
    sPointKind = kind;
    sPointX = gx;
    sPointY = gy;
}

int portCompanionMenuPoint(float* gx, float* gy) {
    *gx = sPointX;
    *gy = sPointY;
    return sPointKind;
}

/* --- its input, this frame --- */

static unsigned sKeys[8];

void portCompanionMenuSetKeys(const unsigned* keys) {
    memcpy(sKeys, keys, sizeof(sKeys));
}

unsigned portCompanionMenuKey(int which) {
    return which >= 0 && which < 8 ? sKeys[which] : 0;
}

#ifndef TTYD_BACKEND_AURORA
/* without Aurora there's no picture to draw it into: the second screen's menu is off */
void portCompanionMenuFrame(void) {}
unsigned portCompanionMenuPicture(unsigned char* rgba, unsigned* seen) {
    (void)rgba;
    (void)seen;
    return 0;
}
int portCompanionMenuReady(void) {
    return 0;
}
void portCompanionMenuTap(float nx, float ny) {
    (void)nx;
    (void)ny;
}
void portCompanionMenuButton(int button) {
    (void)button;
}
void portCompanionMenuShot(const char* name) {
    (void)name;
}
#endif
