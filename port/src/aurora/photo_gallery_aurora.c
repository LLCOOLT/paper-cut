/* The Journal's Photos (port/include/port/photo.h): the player's pictures, newest first, in a book as the Tattle
 * log's, eight a spread; one shown large. The PNGs are read and made small on a thread of their own (a 4K
 * picture takes a while), the small ones kept while the game runs; the game's thread only draws what is ready.
 * Nothing of this is the game's: not its heap, not its state (the entry isn't there online or in a replay). */
#include "port/text_fit.h"
#include "port/photo.h"
#include "port/pause_settings.h"
#include "port/replay.h"
#include "port/tr.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "icon_ids.h"
#include "manager/fontmgr.h"
#include "pmario_sound.h"
#include "win/win_main.h"
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_time.h>
#include <SDL3/SDL_timer.h>
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PHOTOS 1024
#define PER_PAGE 8 /* a spread: two pictures a page, two rows */
#define THUMB_W 128
#define THUMB_H 96
#define FULL_MAX_W 1024
#define FULL_MAX_H 768
/* the small pictures in the book: from its left (two a page) and top (two rows) */
#define CELL_W 88.0f
#define CELL_H 66.0f
static const float kCellX[4] = {18.0f, 120.0f, 248.0f, 350.0f};
static const float kCellY[2] = {-28.0f, -122.0f};
/* the book's top left, from the Journal's (x, y): where the Tattle log's is */
#define BOOK_DX (-170.0f)
#define BOOK_DY 130.0f

/* each picture: its file, when it was taken, its small picture once read */
typedef struct Photo {
    char path[600];
    char when[40];
    SDL_Time time;
    SDL_AtomicInt state; /* 0 not read, 2 ready, 3 can't be read */
    u8* thumb;           /* GX RGBA8, THUMB_W x THUMB_H */
    GXTexObj obj;
    int objMade;
} Photo;

static Photo* sPhotos[MAX_PHOTOS];
static int sCount;
static int sScanned, sChanged;

/* the open book: the cursor (an index), the picture shown large or -1 */
static int sCursor, sLarge = -1, sOpen;

/* the large picture: asked for (sWant, by the game's thread), read (by the loader) */
static SDL_Mutex* sLock;
static int sWant = -1;
static struct {
    int index;
    u8* pixels; /* GX RGBA8 */
    int w, h;   /* its size (a multiple of 4), and the picture's own within it */
    int pw, ph;
} sFull = {-1, NULL, 0, 0, 0, 0}, sShown = {-1, NULL, 0, 0, 0, 0};
static GXTexObj sShownObj;
/* the shown one's pixels replaced: freed a few frames later (the draws may still read them) */
static u8* sRetired[4];
static u32 sRetiredAt[4]; /* VIGetRetraceCount when retired */

static SDL_Thread* sLoader;
static SDL_AtomicInt sStop;
static SDL_AtomicInt sPage; /* the spread the loader reads first */

void portPhotoGalleryChanged(void) {
    sChanged = 1;
}

int portPhotoJournalEntry(void) {
    return !portReplayActive();
}

/* --- reading the pictures (the loader's thread) --- */

/* a box filter: the source's (x0, y0, cw, ch) (RGBA, pitch bytes a row) into w x h, as GX RGBA8 tiles at out
 * (out's own width ow); outside the picture (pw x ph) the paper's colour */
static void shrink(const u8* src, int pitch, int x0, int y0, int cw, int ch, u8* out, int ow, int oh, int pw,
                   int ph) {
    int x, y;
    for (y = 0; y < oh; y++) {
        for (x = 0; x < ow; x++) {
            u8* tile = out + ((y / 4) * (ow / 4) + x / 4) * 64;
            const int at = ((y & 3) * 4 + (x & 3)) * 2;
            u32 r = 0xF0, g = 0xEC, b = 0xE0;
            if (x < pw && y < ph) {
                /* (64-bit: a source thousands of pixels long times the output's overflows int) */
                const int sx0 = x0 + (int)((long long)x * cw / pw);
                const int sx1 = x0 + (int)(((long long)(x + 1) * cw + pw - 1) / pw);
                const int sy0 = y0 + (int)((long long)y * ch / ph);
                const int sy1 = y0 + (int)(((long long)(y + 1) * ch + ph - 1) / ph);
                int sx, sy;
                u32 n = 0;
                r = g = b = 0;
                for (sy = sy0; sy < sy1; sy++) {
                    const u8* row = src + (size_t)sy * pitch;
                    for (sx = sx0; sx < sx1; sx++) {
                        r += row[sx * 4];
                        g += row[sx * 4 + 1];
                        b += row[sx * 4 + 2];
                        n++;
                    }
                }
                if (n > 0) {
                    r /= n;
                    g /= n;
                    b /= n;
                }
            }
            tile[at] = 0xFF;
            tile[at + 1] = (u8)r;
            tile[32 + at] = (u8)g;
            tile[32 + at + 1] = (u8)b;
        }
    }
}

static SDL_Surface* readPng(const char* path) {
    SDL_Surface* raw = SDL_LoadPNG(path);
    SDL_Surface* rgba;
    if (raw == NULL) {
        return NULL;
    }
    rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(raw);
    return rgba;
}

static void readThumb(Photo* p) {
    SDL_Surface* s = readPng(p->path);
    int cw, ch;
    if (s == NULL || s->w < 4 || s->h < 4) {
        SDL_DestroySurface(s);
        SDL_SetAtomicInt(&p->state, 3);
        return;
    }
    /* the middle 4:3 of it */
    cw = s->w;
    ch = s->h;
    if ((long long)cw * 3 > (long long)ch * 4) {
        cw = (int)((long long)ch * 4 / 3);
    } else {
        ch = (int)((long long)cw * 3 / 4);
    }
    p->thumb = (u8*)SDL_aligned_alloc(32, THUMB_W * THUMB_H * 4);
    if (p->thumb == NULL) {
        SDL_DestroySurface(s);
        SDL_SetAtomicInt(&p->state, 3);
        return;
    }
    shrink((const u8*)s->pixels, s->pitch, (s->w - cw) / 2, (s->h - ch) / 2, cw, ch, p->thumb, THUMB_W, THUMB_H,
           THUMB_W, THUMB_H);
    SDL_DestroySurface(s);
    SDL_SetAtomicInt(&p->state, 2);
}

static void readFull(int index) {
    SDL_Surface* s = readPng(sPhotos[index]->path);
    int pw, ph, w, h;
    u8* pixels;
    if (s == NULL || s->w < 4 || s->h < 4) {
        SDL_DestroySurface(s);
        SDL_LockMutex(sLock);
        if (sWant == index) {
            sWant = -2; /* can't be read: the small one stays */
        }
        SDL_UnlockMutex(sLock);
        return;
    }
    /* fitted in FULL_MAX_W x FULL_MAX_H (64-bit: a very tall PNG overflows int), never under 4 a side (a long
     * narrow picture would scale to 0) */
    {
        long long lw = s->w, lh = s->h;
        if (lw > FULL_MAX_W) {
            lh = lh * FULL_MAX_W / lw;
            lw = FULL_MAX_W;
        }
        if (lh > FULL_MAX_H) {
            lw = lw * FULL_MAX_H / lh;
            lh = FULL_MAX_H;
        }
        pw = lw < 4 ? 4 : (int)lw;
        ph = lh < 4 ? 4 : (int)lh;
    }
    w = (pw + 3) & ~3;
    h = (ph + 3) & ~3;
    pixels = (u8*)SDL_aligned_alloc(32, (size_t)w * h * 4);
    if (pixels == NULL) {
        /* out of memory: the small one stays (not read again and again) */
        SDL_DestroySurface(s);
        SDL_LockMutex(sLock);
        if (sWant == index) {
            sWant = -2;
        }
        SDL_UnlockMutex(sLock);
        return;
    }
    shrink((const u8*)s->pixels, s->pitch, 0, 0, s->w, s->h, pixels, w, h, pw, ph);
    SDL_DestroySurface(s);
    SDL_LockMutex(sLock);
    if (sFull.pixels != NULL) {
        SDL_aligned_free(sFull.pixels); /* (never shown) */
    }
    sFull.index = pixels != NULL ? index : -1;
    sFull.pixels = pixels;
    sFull.w = w;
    sFull.h = h;
    sFull.pw = pw;
    sFull.ph = ph;
    SDL_UnlockMutex(sLock);
}

/* one picture read (the large one asked for, or the next small one): 0 when there was none to read */
static int loaderStep(void) {
    int want, i, found = -1;
    const int page = SDL_GetAtomicInt(&sPage);
    SDL_LockMutex(sLock);
    want = sWant >= 0 && sFull.index != sWant && sShown.index != sWant ? sWant : -1;
    SDL_UnlockMutex(sLock);
    if (want >= 0) {
        readFull(want);
        return 1;
    }
    /* this spread's first, then the next and the one before, then the rest in order */
    for (i = 0; i < PER_PAGE * 3 + sCount && found < 0; i++) {
        const int at = i < PER_PAGE * 2 ? page * PER_PAGE + i
                       : i < PER_PAGE * 3 ? (page - 1) * PER_PAGE + (i - PER_PAGE * 2)
                                          : i - PER_PAGE * 3;
        if (at >= 0 && at < sCount && SDL_GetAtomicInt(&sPhotos[at]->state) == 0) {
            found = at;
        }
    }
    if (found >= 0) {
        readThumb(sPhotos[found]);
        return 1;
    }
    return 0;
}

static int SDLCALL loader(void* unused) {
    (void)unused;
    while (!SDL_GetAtomicInt(&sStop)) {
        if (!loaderStep()) {
            SDL_Delay(30);
        }
    }
    return 0;
}

static void stopLoader(void) {
    if (sLoader != NULL) {
        SDL_SetAtomicInt(&sStop, 1);
        SDL_WaitThread(sLoader, NULL);
        sLoader = NULL;
    }
}

/* --- the list --- */

static int newestFirst(const void* a, const void* b) {
    const Photo* pa = *(Photo* const*)a;
    const Photo* pb = *(Photo* const*)b;
    if (pa->time != pb->time) {
        return pa->time < pb->time ? 1 : -1;
    }
    return strcmp(pb->path, pa->path);
}

typedef struct Scan {
    const char* folder;
    Photo** found;
    int count;
} Scan;

static int endsPng(const char* name) {
    const size_t n = strlen(name);
    return n > 4 && SDL_strcasecmp(name + n - 4, ".png") == 0;
}

static SDL_EnumerationResult SDLCALL listed(void* data, const char* dirname, const char* fname) {
    Scan* scan = (Scan*)data;
    char path[600];
    SDL_PathInfo info;
    Photo* p = NULL;
    int i, slot = scan->count;
    (void)dirname;
    if (!endsPng(fname)) {
        return SDL_ENUM_CONTINUE;
    }
    snprintf(path, sizeof(path), "%s/%s", scan->folder, fname);
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
        return SDL_ENUM_CONTINUE;
    }
    if (scan->count >= MAX_PHOTOS) { /* full: in the oldest one's place, when newer (the newest are shown) */
        slot = 0;
        for (i = 1; i < scan->count; i++) {
            if (scan->found[i]->time < scan->found[slot]->time) {
                slot = i;
            }
        }
        if (scan->found[slot]->time >= info.modify_time) {
            return SDL_ENUM_CONTINUE;
        }
    }
    /* one already known keeps its small picture */
    for (i = 0; i < sCount; i++) {
        if (sPhotos[i] != NULL && strcmp(sPhotos[i]->path, path) == 0) {
            p = sPhotos[i];
            sPhotos[i] = NULL;
            break;
        }
    }
    if (p == NULL) {
        SDL_DateTime dt;
        p = (Photo*)SDL_calloc(1, sizeof(Photo));
        if (p == NULL) {
            return SDL_ENUM_CONTINUE;
        }
        snprintf(p->path, sizeof(p->path), "%s", path);
        p->time = info.modify_time;
        if (SDL_TimeToDateTime(info.modify_time, &dt, true)) {
            snprintf(p->when, sizeof(p->when), "%04d-%02d-%02d  %02d:%02d", dt.year, dt.month, dt.day, dt.hour,
                     dt.minute);
        }
    }
    if (slot < scan->count) {
        SDL_aligned_free(scan->found[slot]->thumb);
        SDL_free(scan->found[slot]);
    } else {
        scan->count++;
    }
    scan->found[slot] = p;
    return SDL_ENUM_CONTINUE;
}

static void retire(u8* pixels);

static void scan(void) {
    static Photo* found[MAX_PHOTOS];
    Scan s = {NULL, found, 0};
    const char* folders[2];
    int i, f;
    folders[0] = portPhotoFolder();
    folders[1] = portPhotoOldFolder();
    for (f = 0; f < 2; f++) {
        if (f == 1 && strcmp(folders[1], folders[0]) == 0) {
            break;
        }
        s.folder = folders[f];
        SDL_EnumerateDirectory(folders[f], listed, &s);
    }
    /* the ones gone: theirs freed */
    for (i = 0; i < sCount; i++) {
        if (sPhotos[i] != NULL) {
            SDL_aligned_free(sPhotos[i]->thumb);
            SDL_free(sPhotos[i]);
        }
    }
    qsort(found, (size_t)s.count, sizeof(found[0]), newestFirst);
    memcpy(sPhotos, found, sizeof(found[0]) * (size_t)s.count);
    sCount = s.count;
    sScanned = 1;
    sChanged = 0;
    /* the large pictures kept were by their old places in the list (re-sorted now): let go, so the large view
       never shows one picture under another's date (the loader is stopped; the draws long done) */
    SDL_LockMutex(sLock);
    if (sFull.pixels != NULL) {
        SDL_aligned_free(sFull.pixels);
    }
    sFull.pixels = NULL;
    sFull.index = -1;
    if (sShown.pixels != NULL) {
        retire(sShown.pixels); /* (all four taken: the oldest freed to make room) */
    }
    sShown.pixels = NULL;
    sShown.index = -1;
    SDL_UnlockMutex(sLock);
}

/* --- the book --- */

void portPhotoJournalOpen(WindowWork* wp) {
    (void)wp;
    if (sLock == NULL) {
        sLock = SDL_CreateMutex();
    }
    stopLoader();
    if (!sScanned || sChanged) {
        scan();
        /* the list's order changed: a large picture read or shown before is by another index now */
        sShown.index = -1;
        if (sFull.pixels != NULL) {
            SDL_aligned_free(sFull.pixels); /* (never shown; the loader is stopped) */
            sFull.pixels = NULL;
        }
        sFull.index = -1;
    }
    if (sCursor >= sCount) {
        sCursor = sCount > 0 ? sCount - 1 : 0;
    }
    sLarge = -1;
    SDL_LockMutex(sLock);
    sWant = -1;
    SDL_UnlockMutex(sLock);
    SDL_SetAtomicInt(&sPage, sCursor / PER_PAGE);
    SDL_SetAtomicInt(&sStop, 0);
    sLoader = SDL_CreateThread(loader, "photo gallery", NULL);
    sOpen = 1;
}

void portPhotoJournalClose(void) {
    stopLoader();
    sLarge = -1;
    sOpen = 0;
}

static void largeEntry(CameraId camera, void* param);

/* the help box: "pc_menu_photo_page" with what it says in its tag, so a change is seen (winMsgEntry) */
static void help(WindowWork* wp) {
    static char sTag[64];
    snprintf(sTag, sizeof(sTag), "pc_menu_photo_page_%d_%d_%d", sCount, sCursor, sLarge >= 0);
    winMsgEntry(wp, 0, sTag, NULL);
}

static void moveTo(int to) {
    if (to < 0 || to >= sCount || to == sCursor) {
        return;
    }
    sCursor = to;
    SDL_SetAtomicInt(&sPage, sCursor / PER_PAGE);
    psndSFXOn(SFX_NAME_WORD(0x20035));
}

static void showLarge(int index) {
    sLarge = index;
    SDL_LockMutex(sLock);
    sWant = index;
    SDL_UnlockMutex(sLock);
}

int portPhotoJournalMain(WindowWork* wp) {
    const int pages = (sCount + PER_PAGE - 1) / PER_PAGE;
    const int page = sCursor / PER_PAGE, at = sCursor % PER_PAGE;
    if (!sOpen) {
        portPhotoJournalOpen(wp); /* (a save state loaded in here) */
    }
    if (sLoader == NULL) {
        loaderStep(); /* no threads (the browser): a picture a frame, here */
    }
    if (wp->buttonTrg & PAD_BUTTON_START) {
        portPhotoJournalClose();
        return -2;
    }
    if (sLarge >= 0) {
        if (wp->buttonTrg & (PAD_BUTTON_B | PAD_BUTTON_A)) {
            psndSFXOn(SFX_NAME_WORD(0x20013));
            sLarge = -1;
        } else if ((wp->dirRep & DEMO_STICK_LEFT) || (wp->buttonRep & PAD_TRIGGER_L)) {
            if (sLarge > 0) {
                moveTo(sLarge - 1);
                showLarge(sCursor);
            }
        } else if ((wp->dirRep & DEMO_STICK_RIGHT) || (wp->buttonRep & PAD_TRIGGER_R)) {
            if (sLarge + 1 < sCount) {
                moveTo(sLarge + 1);
                showLarge(sCursor);
            }
        }
        wp->helpTgtX = -50.0f;
        wp->helpTgtY = -150.0f;
        help(wp);
        dispEntry(CAMERA_2D, 0.0f, 0, largeEntry, NULL); /* over the menu and its help box */
        return 0;
    }
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn(SFX_NAME_WORD(0x20013));
        portPhotoJournalClose();
        return -1;
    }
    if (sCount > 0) {
        if (wp->buttonTrg & PAD_BUTTON_A) {
            psndSFXOn(SFX_NAME_WORD(0x20012));
            showLarge(sCursor);
        } else if (wp->buttonRep & PAD_TRIGGER_L) {
            if (page > 0) {
                moveTo((page - 1) * PER_PAGE);
            }
        } else if (wp->buttonRep & PAD_TRIGGER_R) {
            if (page + 1 < pages) {
                moveTo((page + 1) * PER_PAGE);
            }
        } else if (wp->dirRep & DEMO_STICK_RIGHT) {
            if (at % 4 < 3) {
                moveTo(sCursor + 1);
            } else if (page + 1 < pages) {
                moveTo((page + 1) * PER_PAGE + at - 3); /* past the right page: the next spread */
            }
        } else if (wp->dirRep & DEMO_STICK_LEFT) {
            if (at % 4 > 0) {
                moveTo(sCursor - 1);
            } else if (page > 0) {
                moveTo((page - 1) * PER_PAGE + at + 3);
            }
        } else if (wp->dirRep & DEMO_STICK_DOWN) {
            if (at < 4 && sCursor + 4 < sCount) {
                moveTo(sCursor + 4);
            } else if (at < 4 && sCount - 1 >= page * PER_PAGE + 4) {
                moveTo(sCount - 1); /* the lower row shorter */
            }
        } else if (wp->dirRep & DEMO_STICK_UP) {
            if (at >= 4) {
                moveTo(sCursor - 4);
            }
        }
    }
    {
        const int a = sCursor % PER_PAGE;
        wp->helpTgtX = BOOK_DX + kCellX[a % 4] - 12.0f;
        wp->helpTgtY = BOOK_DY + kCellY[a / 4] - CELL_H / 2.0f + 8.0f;
    }
    help(wp);
    return 0;
}

/* --- drawing --- */

static void quadState(int textured, GXColor c) {
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTevStages(1);
    GXSetTevColor(GX_TEVREG0, c);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    if (textured) {
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_ZERO);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO);
        GXSetNumTexGens(1);
        GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    } else {
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
        GXSetNumTexGens(0);
    }
    GXLoadPosMtxImm(camGetPtr(CAMERA_2D)->view, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    if (textured) {
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    }
}

/* a rectangle from its top left (y up), plain */
static void rect(f32 x, f32 y, f32 w, f32 h, GXColor c) {
    quadState(0, c);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(x, y, 0.0f);
    GXPosition3f32(x, y - h, 0.0f);
    GXPosition3f32(x + w, y - h, 0.0f);
    GXPosition3f32(x + w, y, 0.0f);
}

/* the same with a picture on it, its (s, t) to (s1, t1) */
static void picture(GXTexObj* obj, f32 x, f32 y, f32 w, f32 h, f32 s1, f32 t1, GXColor c) {
    quadState(1, c);
    GXLoadTexObj(obj, GX_TEXMAP0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(x, y, 0.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(x, y - h, 0.0f);
    GXTexCoord2f32(0.0f, t1);
    GXPosition3f32(x + w, y - h, 0.0f);
    GXTexCoord2f32(s1, t1);
    GXPosition3f32(x + w, y, 0.0f);
    GXTexCoord2f32(s1, 0.0f);
}

static void textAt(f32 x, f32 y, f32 scale, f32 room, GXColor c, const char* text) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 sx = full * scale > room ? room / full : scale;
    winFontInit();
    winFontSet((Vec){x, y, 0.0f}, (Vec){sx, portTextFitHeight(sx, scale), portTextFitHeight(sx, scale)}, c, "%s", text);
}

static void centredText(f32 cx, f32 y, f32 scale, f32 room, GXColor c, const char* text) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 sx = full * scale > room ? room / full : scale;
    textAt(cx - full * sx / 2.0f, y, scale, room, c, text);
}

/* the pixels the shown large picture replaced, freed once no draw can read them */
static int retireRoom(void) {
    int i;
    for (i = 0; i < 4; i++) {
        if (sRetired[i] == NULL) {
            return 1;
        }
    }
    return 0;
}

/* The ages are the game's frames (retraces) since each was retired, so they count on while the book is closed.
 * The large view retires only when retireRoom (never freed early, the new picture waits a frame or two
 * instead); a rescan at the book's opening (scan) can find all four taken, and then the oldest goes, which
 * has had at least the frames since the book closed (no draw of the book in between), never dropped unfreed. */
static void retire(u8* pixels) {
    const u32 now = VIGetRetraceCount();
    int i, oldest = 0;
    for (i = 0; i < 4; i++) {
        if (sRetired[i] == NULL) {
            sRetired[i] = pixels;
            sRetiredAt[i] = now;
            return;
        }
        if (now - sRetiredAt[i] > now - sRetiredAt[oldest]) {
            oldest = i;
        }
    }
    SDL_aligned_free(sRetired[oldest]);
    sRetired[oldest] = pixels;
    sRetiredAt[oldest] = now;
}

static void retireTick(void) {
    const u32 now = VIGetRetraceCount();
    int i;
    for (i = 0; i < 4; i++) {
        if (sRetired[i] != NULL && now - sRetiredAt[i] > 8) {
            SDL_aligned_free(sRetired[i]);
            sRetired[i] = NULL;
        }
    }
}

static void largeDisp(f32 cx, f32 cy) {
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    const f32 maxW = 560.0f, maxH = 350.0f;
    Photo* p = sPhotos[sLarge];
    f32 w, h, x, y;
    /* the read one, once it is here */
    SDL_LockMutex(sLock);
    if (sFull.index >= 0 && sFull.pixels != NULL && (sShown.pixels == NULL || retireRoom())) {
        if (sShown.pixels != NULL) {
            retire(sShown.pixels);
        }
        sShown.index = sFull.index;
        sShown.pixels = sFull.pixels;
        sShown.w = sFull.w;
        sShown.h = sFull.h;
        sShown.pw = sFull.pw;
        sShown.ph = sFull.ph;
        sFull.index = -1;
        sFull.pixels = NULL;
        GXInitTexObj(&sShownObj, sShown.pixels, (u16)sShown.w, (u16)sShown.h, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP,
                     GX_FALSE);
        GXInitTexObjLOD(&sShownObj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    }
    SDL_UnlockMutex(sLock);
    rect(-400.0f, 260.0f, 800.0f, 520.0f, (GXColor){0x10, 0x0C, 0x18, 0xD0}); /* the menu dimmed behind it */
    if (sShown.index == sLarge && sShown.pixels != NULL) {
        const f32 aspect = (f32)sShown.pw / (f32)sShown.ph;
        w = maxW;
        h = w / aspect;
        if (h > maxH) {
            h = maxH;
            w = h * aspect;
        }
        x = cx - w / 2.0f;
        y = cy + h / 2.0f;
        rect(x - 6.0f, y + 2.0f, w + 16.0f, h + 16.0f, (GXColor){0x00, 0x00, 0x00, 0x60}); /* its shadow */
        rect(x - 10.0f, y + 10.0f, w + 20.0f, h + 20.0f, (GXColor){0xF6, 0xF2, 0xE8, 0xFF}); /* a print's border */
        picture(&sShownObj, x, y, w, h, (f32)sShown.pw / sShown.w, (f32)sShown.ph / sShown.h, white);
    } else if (SDL_GetAtomicInt(&p->state) == 2 && p->objMade) {
        w = maxH * 4.0f / 3.0f; /* the small one meanwhile */
        h = maxH;
        x = cx - w / 2.0f;
        y = cy + h / 2.0f;
        rect(x - 10.0f, y + 10.0f, w + 20.0f, h + 20.0f, (GXColor){0xF6, 0xF2, 0xE8, 0xFF});
        picture(&p->obj, x, y, w, h, 1.0f, 1.0f, white);
    } else {
        h = maxH;
    }
    /* when, and how to go on, under it */
    {
        char line[200];
        snprintf(line, sizeof(line), "%s   %d/%d   %s", p->when, sLarge + 1, sCount,
                 portTrGame("Left, right: the one before or after"));
        centredText(cx, cy - h / 2.0f - 18.0f, 0.7f, 560.0f, white, line);
    }
}

static void largeEntry(CameraId camera, void* param) {
    (void)camera;
    (void)param;
    if (sOpen && sLarge >= 0 && sLarge < sCount) {
        largeDisp(0.0f, 14.0f);
    }
}

void portPhotoJournalDisp(WindowWork* wp, float x, float y) {
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    const f32 bx = x + BOOK_DX, by = y + BOOK_DY;
    const int pages = (sCount + PER_PAGE - 1) / PER_PAGE;
    const int page = sCursor / PER_PAGE;
    char buf[64];
    int i;
    retireTick();
    winBookGX(wp, 1, bx, by);
    /* the count, in the box beside the book as the Tattle log's */
    winKirinukiGX(wp, 0, bx - 110.0f, by - 160.0f, 100.0f, 50.0f);
    centredText(bx - 60.0f, by - 166.0f, 0.75f, 84.0f, black, portTrGame("Photos"));
    snprintf(buf, sizeof(buf), "%d/%d", sCount > 0 ? sCursor + 1 : 0, sCount);
    centredText(bx - 60.0f, by - 184.0f, 0.75f, 84.0f, black, buf);
    if (sCount == 0) {
        centredText(bx + 116.0f, by - 80.0f, 0.75f, 190.0f, black, portTrGame("No pictures yet."));
        centredText(bx + 332.0f, by - 80.0f, 0.75f, 190.0f, black, portTrGame("Photo mode takes them."));
        return;
    }
    for (i = 0; i < PER_PAGE; i++) {
        const int at = page * PER_PAGE + i;
        Photo* p;
        f32 px, py;
        const f32 tw = CELL_W, th = CELL_H;
        if (at >= sCount) {
            break;
        }
        p = sPhotos[at];
        px = bx + kCellX[i % 4];
        py = by + kCellY[i / 4];
        if (SDL_GetAtomicInt(&p->state) == 2 && !p->objMade) {
            GXInitTexObj(&p->obj, p->thumb, THUMB_W, THUMB_H, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
            GXInitTexObjLOD(&p->obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
            p->objMade = 1;
        }
        if (at == sCursor) {
            rect(px - 7.0f, py + 7.0f, tw + 14.0f, th + 14.0f, (GXColor){0xE8, 0x50, 0x40, 0xFF});
        }
        rect(px - 3.0f, py + 3.0f, tw + 6.0f, th + 6.0f, (GXColor){0xFF, 0xFE, 0xF8, 0xFF});
        if (p->objMade) {
            picture(&p->obj, px, py, tw, th, 1.0f, 1.0f, white);
        } else {
            rect(px, py, tw, th, SDL_GetAtomicInt(&p->state) == 3 ? (GXColor){0x90, 0x88, 0x80, 0xFF}
                                                                   : (GXColor){0xD8, 0xD0, 0xC0, 0xFF});
        }
    }
    /* the page arrows and L and R, as the Tattle log's */
    if (page > 0) {
        winTexInit(*wp->res->data);
        winTexSet(0x17, (Vec){440.0f + bx, 18.0f + by, 0.0f}, (Vec){-1.0f, -1.0f, -1.0f}, white);
        winIconInit();
        winIconSet(ICON_07E, (Vec){440.0f + bx, by, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, white);
    }
    if (page < pages - 1) {
        winTexInit(*wp->res->data);
        winTexSet(0x17, (Vec){440.0f + bx, (by - 15.0f) - 240.0f, 0.0f}, (Vec){1.0f, 1.0f, 1.0f}, white);
        winIconInit();
        winIconSet(ICON_080, (Vec){440.0f + bx, by - 240.0f, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, white);
    }
}

/* --- the help box --- */

const char* portPhotoJournalMsg(const char* tag) {
    static char sHelp[320];
    char text[320];
    if (strncmp(tag, "pc_menu_photo", 13) != 0) {
        return NULL;
    }
    if (strcmp(tag, "pc_menu_photo") == 0) {
        static char sEntry[320];
        portPauseWrap(portTrGame("The pictures taken in photo mode, and screenshots."), sEntry, sizeof(sEntry));
        return sEntry;
    }
    /* "pc_menu_photo_page": the picture under the cursor, or how to take one */
    if (sCount == 0) {
        snprintf(text, sizeof(text), "%s", portTrGame("Photo mode: {PHOTO}."));
    } else if (sLarge >= 0) {
        return ""; /* (the large one says it under itself, over the help box) */
    } else {
        snprintf(text, sizeof(text), "%s %s", sPhotos[sCursor]->when, portTrGame("{A}: look closer."));
    }
    portPauseWrap(text, sHelp, sizeof(sHelp));
    return sHelp;
}
