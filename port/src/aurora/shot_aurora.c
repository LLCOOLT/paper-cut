/* Frame captures to PNG, so a run can be checked without looking at the window.
 *
 * $TTYD_SHOT=<frame>[,<frame>...]   capture those retrace counts
 * $TTYD_SHOT_DIR=<dir>              where to write them (default: .), as
 *                                   shot_<frame>.png, or <name>.png for
 *                                   portShotRequest(name) (the map warp)
 *
 * The capture copies the EFB with GXCopyTex just before the game's GXCopyDisp
 * (which clears it), and asks Aurora to read that copy back
 * (AuroraReadbackCaptureCopies). Aurora delivers it a frame or two later in
 * GameCube RGBA8 texture layout (4x4 tiles, AR then GB), which is untiled and
 * written as an uncompressed PNG.
 *
 * $TTYD_USER_SHOT=<frame>           the player's screenshot (Photos/Pictures) then
 * $TTYD_USER_SHOT=<f>,<f>,...       a burst (media captures, tools/port/media): the whole window as shown,
 * $TTYD_USER_SHOT=<start>:<count>[:<step>]  post-effects and ImGui included (aurora_capture_window), at the
 *                                   window's size, every listed frame (or count frames, one every step from
 *                                   start), as burst_00000.qoi on in $TTYD_USER_SHOT_DIR (else $TTYD_SHOT_DIR).
 *                                   No frame is dropped: when the writers fall behind, the game waits for them
 *                                   (a $TTYD_FAST run's frames stay the same; only the wall clock slows).
 *                                   $TTYD_USER_SHOT_FORMAT=qoi (default: fast, lossless, ffmpeg and Pillow
 *                                   read it), bmp (uncompressed) or png; $TTYD_USER_SHOT_THREADS (default 4)
 * $TTYD_USER_SHOT_INBETWEEN=1       a burst takes every frame shown: a higher frame rate's in-between frames
 *                                   too, each in its own file in the order shown (the edge check, edge_check.py)
 * $TTYD_SHOT_EFB=1                  the player's screenshots from the EFB copy
 *
 * The player's screenshots and photo mode's pictures are read back instead
 * from the picture as it is shown (aurora_capture_picture: the window's size,
 * the picture filters and photo mode's look on it, not the settings page),
 * saved off the game's thread; where that can't be read, the EFB copy as
 * above. They go to portPhotoFolder (port/include/port/photo.h).
 *
 * $TTYD_SHOT_INBETWEEN=<k>          capture instead the k-th in-between frame
 *                                   after each listed one (a higher
 *                                   $TTYD_FRAMERATE), as shot_<frame>_<k> */
#include "port/file_pc.h"
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#undef GXCopyDisp /* the real one, from the wrapper below */
#include <SDL3/SDL_atomic.h>
#include "port/stereo.h"
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <aurora/picture.h>
#include <time.h>
#include "port/settings.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "port/ios_pc.h"
#endif
#endif
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/achievements.h"
#include "port/photo.h"
#include "port/platform.h"
#include "port/ui_shot.h"
#include "port/widescreen.h"
#include "port/tr.h"

#define SHOT_W 640
#define SHOT_H 480
#define SHOT_BYTES (SHOT_W * SHOT_H * 4)

static AuroraReadbackStream sStream;
static int sCapturing;   /* a copy is in flight */
static u32 sWaited;      /* frames since the copy */
/* Each capture's own readback key: a stream returns its latest completed
 * capture, so with one key the next lookup found the previous capture. */
static u64 sKey;
static char sName[260];  /* file name of the in-flight capture */
static char sNext[260];  /* a requested capture, taken at the next GXCopyDisp */
static int sListRead;
static char* sList;      /* $TTYD_SHOT as given (a copy of any length) */
static u8 sCopyId;       /* GXCopyTex dest: only an identifier for Aurora */
static u8* sPixels;
static int sUserNext;    /* the requested capture is the player's screenshot */
static int sUser;        /* the in-flight capture is */
static char sStatus[256];
static Uint64 sStatusTime;

static u32 crcTable[256];

static u32 crc(u32 c, const u8* p, size_t n) {
    size_t i;
    if (crcTable[1] == 0) {
        u32 k, j;
        for (k = 0; k < 256; k++) {
            u32 v = k;
            for (j = 0; j < 8; j++) {
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            }
            crcTable[k] = v;
        }
    }
    for (i = 0; i < n; i++) {
        c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c;
}

static void be32(u8* p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

/* RGB, filter 0 rows, zlib "stored" blocks: no compression library needed. */
static const char* dir(void) {
    const char* d = getenv("TTYD_SHOT_DIR");
    return d != NULL && *d ? d : ".";
}

/* the file's bytes, allocated at their full size first (encodePng) */
typedef struct PngBuf {
    u8* data;
    size_t len;
} PngBuf;

static void put(PngBuf* b, const void* p, size_t n) {
    if (n != 0) {
        memcpy(b->data + b->len, p, n);
    }
    b->len += n;
}

static void chunkMem(PngBuf* b, const char* type, const u8* data, u32 len) {
    u8 hdr[8];
    u32 c;
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    put(b, hdr, 8);
    put(b, data, len);
    c = crc(0xFFFFFFFFu, (const u8*)type, 4);
    c = crc(c, data, len) ^ 0xFFFFFFFFu;
    be32(hdr, c);
    put(b, hdr, 4);
}

/* The PNG file's bytes (malloc'd, *len set), or NULL (out of memory). */
static u8* encodePng(const u8* rgb, u32 w, u32 h, size_t* len) {
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    u8 ihdr[13];
    u32 row = w * 3 + 1, raw = row * h, blocks = (raw + 65534) / 65535;
    u32 zlen = 2 + raw + blocks * 5 + 4, i, a = 1, b = 0, pos = 0, done = 0;
    u8* z = (u8*)malloc(zlen);
    if (z == NULL) {
        return NULL;
    }
    z[pos++] = 0x78;
    z[pos++] = 0x01;
    while (done < raw) {
        u32 n = raw - done > 65535 ? 65535 : raw - done;
        z[pos++] = done + n == raw;
        z[pos++] = (u8)n;
        z[pos++] = (u8)(n >> 8);
        z[pos++] = (u8)~n;
        z[pos++] = (u8)(~n >> 8);
        for (i = 0; i < n; i++, done++) {
            u32 y = done / row, x = done % row;
            u8 v = x == 0 ? 0 : rgb[(y * w) * 3 + (x - 1)];
            z[pos++] = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
    }
    be32(z + pos, (b << 16) | a);
    pos += 4;
    be32(ihdr, w);
    be32(ihdr + 4, h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    {
        /* the signature, then three chunks of 12 bytes each around their data */
        PngBuf b = {(u8*)malloc(8 + 12 * 3 + 13 + (size_t)pos), 0};
        if (b.data == NULL) {
            free(z);
            return NULL;
        }
        put(&b, sig, 8);
        chunkMem(&b, "IHDR", ihdr, 13);
        chunkMem(&b, "IDAT", z, pos);
        chunkMem(&b, "IEND", NULL, 0);
        free(z);
        *len = b.len;
        return b.data;
    }
}

static int writeFile(const char* path, const u8* data, size_t len) {
    FILE* f = portFopen(path, "wb");
    if (f == NULL) {
        printf("[shot] cannot write %s\n", path);
        fflush(stdout);
        return 0;
    }
    fwrite(data, 1, len, f);
    fclose(f);
    printf("[shot] wrote %s\n", path);
    fflush(stdout);
    return 1;
}

static void writePng(const char* path, const u8* rgb, u32 w, u32 h) {
    size_t len;
    u8* png = encodePng(rgb, w, h, &len);
    if (png == NULL) {
        printf("[shot] out of memory for %s\n", path);
        return;
    }
    SDL_CreateDirectory(dir()); /* with its parents */
    writeFile(path, png, len);
    free(png);
}

/* name.png in $TTYD_SHOT_DIR (the second screen's Start menu's picture, tests). */
void portShotWriteNamed(const char* name, const u8* rgb, u32 w, u32 h) {
    char path[300];
    snprintf(path, sizeof(path), "%s/%s.png", dir(), name);
    writePng(path, rgb, w, h);
}

/* GameCube RGBA8: 4x4 tiles of 64 bytes, 16 AR pairs then 16 GB pairs. */
static void untile(const u8* tex, u8* rgb) {
    u32 tx, ty, i;
    for (ty = 0; ty < SHOT_H / 4; ty++) {
        for (tx = 0; tx < SHOT_W / 4; tx++) {
            const u8* t = tex + (ty * (SHOT_W / 4) + tx) * 64;
            for (i = 0; i < 16; i++) {
                u32 x = tx * 4 + (i & 3), y = ty * 4 + (i >> 2);
                u8* o = rgb + (y * SHOT_W + x) * 3;
                o[0] = t[i * 2 + 1];
                o[1] = t[32 + i * 2];
                o[2] = t[32 + i * 2 + 1];
            }
        }
    }
}

/* tests: captures queued for a frame to come (portShotRequestIn), taken one at a time as the one before is
 * done, so none is dropped while another is in flight */
#define SHOT_QUEUE 32
static struct {
    char name[sizeof sNext];
    u32 due;
    int used;
    int listed; /* a listed frame's (listedWait): counted in sListedWaiting */
} sQueue[SHOT_QUEUE];
static int sListedWaiting; /* listed frames in the queue (listedWait, below) */

/* 1 if queued, 0 if the queue was full (logged) */
static int enqueue(const char* name, int frames, int listedFrame) {
    int i;
    for (i = 0; i < SHOT_QUEUE && sQueue[i].used; i++) {
    }
    if (i == SHOT_QUEUE) {
        printf("[shot] queue full: %s dropped\n", name);
        return 0;
    }
    snprintf(sQueue[i].name, sizeof(sQueue[i].name), "%s", name);
    sQueue[i].due = VIGetRetraceCount() + (u32)(frames > 0 ? frames : 0);
    sQueue[i].used = 1;
    sQueue[i].listed = listedFrame;
    sListedWaiting += listedFrame;
    return 1;
}

void portShotRequestIn(const char* name, int frames) { enqueue(name, frames, 0); }

/* A capture at the next GXCopyDisp; while another is asked for or in flight, it waits in the queue for
 * its turn (an input script's `shot` every few frames once replaced the one before it under load). */
static void burstMark(const char* name);

void portShotRequest(const char* name) {
    burstMark(name); /* (a burst given as @NAME starts here) */
    if (sCapturing || sNext[0] != '\0') {
        portShotRequestIn(name, 0);
        return;
    }
    snprintf(sNext, sizeof(sNext), "%s", name);
    portUiShotRequest(name); /* tests: with $TTYD_SHOT_UI, <name>_ui.png too (port/ui_shot.h) */
}

/* the earliest queued capture that's due, into sNext */
static void queuedDue(u32 frame) {
    int i, best = -1;
    for (i = 0; i < SHOT_QUEUE; i++) {
        if (sQueue[i].used && (s32)(frame - sQueue[i].due) >= 0 &&
            (best < 0 || (s32)(sQueue[i].due - sQueue[best].due) < 0)) {
            best = i;
        }
    }
    if (best >= 0) {
        snprintf(sNext, sizeof(sNext), "%s", sQueue[best].name);
        portUiShotRequest(sQueue[best].name);
        sQueue[best].used = 0;
        sListedWaiting -= sQueue[best].listed;
    }
}

/* The player's screenshot (the settings page, F12): the next frame, to Photos
 * on iOS, else a PNG in the Pictures folder (portShotStatus says where). */
/* the picture as shown, read back (below): 0 none, 1 asked for, 2 here
 * (sPicture), 3 can't be read (the EFB copy instead), 4 given up on, 5 being
 * written by the GPU's callback (only it leaves 5; the game's thread gives up
 * on 1 only, by compare-and-swap, so a picture is never lost between them).
 * sPictureGen: the request's number, so a late answer to one given up on is
 * never taken for a newer one's. */
static SDL_AtomicInt sPictureState;
static SDL_AtomicInt sPictureGen;
static u8* sPicture;
static u32 sPictureW, sPictureH, sPictureWaited;
static void pictureRead(void* user, const unsigned char* rgba, unsigned int w, unsigned int h);

void portShotRequestUser(void) {
    const int state = SDL_GetAtomicInt(&sPictureState);
    if (sCapturing || sNext[0] != '\0' || state == 1 || state == 2 || state == 5) {
        return;
    }
    if (getenv("TTYD_SHOT_EFB") == NULL) {
        /* asked for before the call: the answer can come on the GPU's thread before it returns */
        const int gen = SDL_AddAtomicInt(&sPictureGen, 1) + 1;
        SDL_SetAtomicInt(&sPictureState, 1);
        sPictureWaited = 0;
        if (aurora_capture_picture(pictureRead, (void*)(intptr_t)gen)) {
            return;
        }
        SDL_AddAtomicInt(&sPictureGen, 1); /* (no answer will come; any that did is dropped) */
        SDL_SetAtomicInt(&sPictureState, 0);
    }
    snprintf(sNext, sizeof(sNext), "screenshot");
    sUserNext = 1;
}

const char* portShotStatus(void) {
    return sStatus[0] != '\0' && SDL_GetTicks() - sStatusTime < 3000 ? sStatus : NULL;
}

static void status(const char* text) {
    snprintf(sStatus, sizeof(sStatus), "%s", portTr(text)); /* (shown in the menu language) */
    sStatusTime = SDL_GetTicks();
}

/* Where the player's pictures go: the Pictures folder's Paper Cut (the user
 * folder's Photos for a set $TTYD_USER_DIR: tests, a portable copy; and on
 * phones, where iOS also puts them in Photos) */
const char* portPhotoFolder(void) {
    static char sFolder[1024];
    if (sFolder[0] == '\0') {
        const char* pictures = NULL;
#if !(defined(__APPLE__) && TARGET_OS_IPHONE) && !defined(__ANDROID__)
        const char* own = getenv("TTYD_USER_DIR");
        if (own == NULL || own[0] == '\0') {
            pictures = SDL_GetUserFolder(SDL_FOLDER_PICTURES);
        }
#endif
        if (pictures != NULL && pictures[0] != '\0') {
            const size_t n = strlen(pictures);
            const char last = pictures[n - 1];
            snprintf(sFolder, sizeof(sFolder), "%s%sPaper Cut", pictures, last == '/' || last == 92 ? "" : "/");
        } else {
            snprintf(sFolder, sizeof(sFolder), "%s/Photos", portSettingsUserPath());
        }
    }
    return sFolder;
}

/* The earlier releases' folder for them on Windows and Linux (the gallery
 * shows those too) */
const char* portPhotoOldFolder(void) {
    static char sOld[1024];
    if (sOld[0] == '\0') {
        snprintf(sOld, sizeof(sOld), "%s/Screenshots", portSettingsUserPath());
    }
    return sOld;
}

/* a new picture's file, made here: Paper Cut <date> at <time>.png, (2) and on
 * if taken in the same second. Made, not only looked for (exclusive create,
 * "x"): two pictures saved at once (each photo's own save thread, the EFB
 * copy's save on the game's thread) in one second each get their own name,
 * never one written over the other. NULL: none could be made. */
static FILE* userShotFile(char* out, size_t size) {
    char when[32];
    time_t now = time(NULL);
    struct tm tm;
    const char* folder = portPhotoFolder();
    int n;
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    strftime(when, sizeof(when), "%Y-%m-%d at %H.%M.%S", &tm);
    SDL_CreateDirectory(folder);
    snprintf(out, size, "%s/Paper Cut %s.png", folder, when);
    for (n = 2; n <= 100; n++) {
        FILE* f;
        errno = 0;
        f = portFopen(out, "wbx");
        if (f != NULL) {
            return f;
        }
        if (errno != EEXIST && portPathKind(out) == 0) {
            return portFopen(out, "wb"); /* (a C library without "x": looked for, as before) */
        }
        snprintf(out, size, "%s/Paper Cut %s (%d).png", folder, when, n);
    }
    return NULL;
}

/* the outcome of a picture saved off the game's thread, for portShotFrame's
 * status line: 0 none yet, 1 saved, 2 not saved, 3 no access to Photos (savePng's
 * 4, under way in Photos, is never stored: portShotPhotosDone stores its answer) */
static SDL_AtomicInt sSaved;
/* and how many were saved since portShotFrame last looked: two save threads
 * done in one frame each count (sSaved shows the later's status only) */
static SDL_AtomicInt sSavedCount;

#if defined(__APPLE__) && TARGET_OS_IPHONE
/* Photos' answer for a picture portIosSaveToPhotos started (port/include/port/ios_pc.h): "Picture saved
 * to Photos" only once it is there, "no access to Photos" when it was refused (any thread) */
void portShotPhotosDone(int ok) {
    if (ok) {
        SDL_AddAtomicInt(&sSavedCount, 1);
    }
    SDL_SetAtomicInt(&sSaved, ok ? 1 : 3);
}
#endif

static void savedStatus(int result) {
    if (result == 1) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
        status("Picture saved to Photos");
#elif defined(__ANDROID__)
        status("Picture saved");
#else
        status("Picture saved to Pictures, Paper Cut");
#endif
    } else if (result == 3) {
        status("Picture: no access to Photos");
    } else if (result == 2) {
        status("Picture could not be saved");
    }
}

/* the PNG made in memory first, then its file made and written (userShotFile):
 * the file is there, empty, only for the write's moment */
static int savePng(SDL_Surface* surface) {
    char path[1200] = "";
    SDL_IOStream* io = SDL_IOFromDynamicMem();
    const u8* bytes = NULL;
    size_t size = 0;
    FILE* f = NULL;
    int ok = 0;
    if (io != NULL && SDL_SavePNG_IO(surface, io, false)) {
        bytes = (const u8*)SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER,
                                                  NULL);
        size = (size_t)SDL_GetIOSize(io);
    }
    if (bytes != NULL && size > 0) {
        f = userShotFile(path, sizeof(path));
    }
    if (f != NULL) {
        ok = fwrite(bytes, 1, size, f) == size;
        ok = fclose(f) == 0 && ok;
        if (!ok) {
            portRemove(path);
        }
    }
    printf(ok ? "[shot] wrote %s\n" : "[shot] cannot write %s\n", path[0] != '\0' ? path : "a picture");
    fflush(stdout);
#if defined(__APPLE__) && TARGET_OS_IPHONE
    {
        const int put = bytes != NULL && size > 0 && portIosSaveToPhotos(bytes, size);
        if (io != NULL) {
            SDL_CloseIO(io);
        }
        return put ? 4 : 3; /* (under way: Photos' answer comes to portShotPhotosDone) */
    }
#else
    if (io != NULL) {
        SDL_CloseIO(io);
    }
    return ok ? 1 : 2;
#endif
}

static void saveUserShot(const u8* rgb, u32 w, u32 h) {
    SDL_Surface* surface = SDL_CreateSurfaceFrom((int)w, (int)h, SDL_PIXELFORMAT_RGB24, (void*)rgb, (int)w * 3);
    savedStatus(surface != NULL ? savePng(surface) : 2);
    SDL_DestroySurface(surface);
    portPhotoGalleryChanged();
}

/* --- the picture as shown (aurora_capture_picture) --- */

/* on the GPU's map callback's thread */
static void pictureRead(void* user, const unsigned char* rgba, unsigned int w, unsigned int h) {
    const int gen = (int)(intptr_t)user;
    u8* copy;
    if (gen != SDL_GetAtomicInt(&sPictureGen)) {
        return; /* a request given up on */
    }
    if (rgba == NULL || w == 0 || h == 0) {
        SDL_CompareAndSwapAtomicInt(&sPictureState, 1, 3);
        return;
    }
    if (!SDL_CompareAndSwapAtomicInt(&sPictureState, 1, 5)) {
        return; /* given up on */
    }
    if (gen != SDL_GetAtomicInt(&sPictureGen)) {
        SDL_SetAtomicInt(&sPictureState, 1); /* a newer request's, asked for meanwhile: its own answer comes */
        return;
    }
    copy = (u8*)malloc((size_t)w * h * 4);
    if (copy == NULL) {
        SDL_SetAtomicInt(&sPictureState, 3);
        return;
    }
    memcpy(copy, rgba, (size_t)w * h * 4);
    sPicture = copy;
    sPictureW = w;
    sPictureH = h;
    SDL_SetAtomicInt(&sPictureState, 2);
}

typedef struct SaveJob {
    u8* rgba;
    u32 w, h;
} SaveJob;

static int SDLCALL saveThread(void* data) {
    SaveJob* job = (SaveJob*)data;
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom((int)job->w, (int)job->h, SDL_PIXELFORMAT_RGBA32, job->rgba, (int)job->w * 4);
    const int result = surface != NULL ? savePng(surface) : 2;
    if (result == 1) {
        SDL_AddAtomicInt(&sSavedCount, 1);
    }
    if (result != 4) { /* (4: Photos answers later, portShotPhotosDone) */
        SDL_SetAtomicInt(&sSaved, result);
    }
    SDL_DestroySurface(surface);
    free(job->rgba);
    free(job);
    return 0;
}

/* portShotFrame: a picture read back, saved; or the EFB copy asked for */
static void pictureFrame(void) {
    const int state = SDL_GetAtomicInt(&sPictureState);
    const int saved = SDL_GetAtomicInt(&sSaved);
    if (saved != 0) {
        const int count = SDL_SetAtomicInt(&sSavedCount, 0); /* (the count before) */
        SDL_CompareAndSwapAtomicInt(&sSaved, saved, 0);      /* (one stored meanwhile: next frame's) */
        savedStatus(saved);
        portPhotoGalleryChanged();
        if (count > 0) {
            portAchievementCounterAdd("photos", count); /* photo mode pictures saved (port/achievements.h) */
        }
    }
    if (state == 2) {
        SaveJob* job = (SaveJob*)malloc(sizeof(SaveJob));
        SDL_Thread* thread = NULL;
        if (job != NULL) {
            job->rgba = sPicture;
            job->w = sPictureW;
            job->h = sPictureH;
            thread = SDL_CreateThread(saveThread, "photo save", job);
            if (thread != NULL) {
                SDL_DetachThread(thread);
            } else {
                saveThread(job); /* (sSaved: next frame's status) */
            }
        } else {
            free(sPicture);
        }
        sPicture = NULL;
        SDL_SetAtomicInt(&sPictureState, 0);
    } else if (state == 3 ||
               (state == 1 && ++sPictureWaited > 120 && SDL_CompareAndSwapAtomicInt(&sPictureState, 1, 4))) {
        /* not readable here (or never came): the game's own frame copy; a
         * late answer (state 4) is dropped (its own request number) */
        if (state == 3) {
            SDL_SetAtomicInt(&sPictureState, 0);
        } else {
            SDL_AddAtomicInt(&sPictureGen, 1);
        }
        printf("[shot] the picture as shown %s: the 640x480 frame copy instead\n",
               state == 3 ? "can't be read here" : "never came");
        fflush(stdout);
        snprintf(sNext, sizeof(sNext), "screenshot");
        sUserNext = 1;
    }
}

/* --- $TTYD_USER_SHOT's bursts: the whole window, every listed frame (see the top) --- */

#define BURST_QUEUE 24 /* frames read back and waiting for a writer (about 6 MB each at 1920x1080) */
#define BURST_THREADS_MAX 8

typedef struct BurstJob {
    struct BurstJob* next;
    u8* rgb;
    size_t cap; /* rgb's size */
    u32 w, h, index;
} BurstJob;

static int sBurstMode = -1; /* 0 none (or the single frame above), 1 a list, 2 start:count:step */
static u32 sBurstStart, sBurstCount, sBurstStep;
static u32* sBurstList;
static u32 sBurstListN;
static u32 sBurstAsked;              /* frames asked for (each one's index) */
static SDL_AtomicInt sBurstWritten;  /* written (or failed to write) */
static SDL_AtomicInt sBurstFailed;   /* not read back or not written */
static SDL_AtomicInt sBurstQueued;   /* read back, waiting for a writer */
static SDL_Mutex* sBurstLock;
static SDL_Condition* sBurstWork;    /* a job for the writers */
static SDL_Condition* sBurstRoom;    /* a writer took one: room in the queue */
static BurstJob* sBurstHead;
static BurstJob* sBurstTail;
static char sBurstDir[1024];
static char sBurstExt[8];
static Uint64 sBurstT0, sBurstWaitNs;
static u32 sBurstW, sBurstH;
static int sBurstReported;
static char sBurstMark[64]; /* @NAME: the script's shot it starts at */

/* the frames' buffers, used again (a fresh 8 MB a frame at 1080p cost its page faults on the drawing thread) */
static BurstJob* sBurstFree;
static int sBurstFreeN;

static BurstJob* burstJob(size_t bytes) {
    BurstJob* job;
    SDL_LockMutex(sBurstLock);
    job = sBurstFree;
    if (job != NULL) {
        sBurstFree = job->next;
        sBurstFreeN--;
    }
    SDL_UnlockMutex(sBurstLock);
    if (job != NULL && job->cap < bytes) {
        free(job->rgb);
        job->rgb = NULL;
        job->cap = 0;
    }
    if (job == NULL && (job = (BurstJob*)calloc(1, sizeof(BurstJob))) == NULL) {
        return NULL;
    }
    if (job->rgb == NULL) {
        job->rgb = (u8*)malloc(bytes);
        job->cap = job->rgb != NULL ? bytes : 0;
    }
    if (job->rgb == NULL) {
        free(job);
        return NULL;
    }
    return job;
}

static void burstRecycle(BurstJob* job) {
    SDL_LockMutex(sBurstLock);
    if (sBurstFreeN < BURST_QUEUE + BURST_THREADS_MAX) {
        job->next = sBurstFree;
        sBurstFree = job;
        sBurstFreeN++;
        job = NULL;
    }
    SDL_UnlockMutex(sBurstLock);
    if (job != NULL) {
        free(job->rgb);
        free(job);
    }
}

/* QOI (qoiformat.org): lossless, a few ms a 1920x1080 frame on one core; ffmpeg and Pillow read it */
static u8* encodeQoi(const u8* rgb, u32 w, u32 h, size_t* len) {
    const size_t px = (size_t)w * h;
    u8* out = (u8*)malloc(14 + px * 4 + 8);
    /* the format's index: r, g, b and a; an empty slot is (0, 0, 0, 0), so an opaque black never matches one
     * (a decoder's empty slot gives alpha 0, and Pillow then hashed what followed into other slots: streaks) */
    u8 seen[64][4];
    u8 prev[3] = {0, 0, 0};
    u8 prevA = 255;
    size_t i, p = 0;
    int run = 0;
    (void)prevA;
    if (out == NULL) {
        return NULL;
    }
    memset(seen, 0, sizeof(seen));
    memcpy(out, "qoif", 4);
    be32(out + 4, w);
    be32(out + 8, h);
    out[12] = 3; /* RGB */
    out[13] = 0; /* sRGB */
    p = 14;
    for (i = 0; i < px; i++) {
        const u8* c = rgb + i * 3;
        if (c[0] == prev[0] && c[1] == prev[1] && c[2] == prev[2]) {
            if (++run == 62 || i == px - 1) {
                out[p++] = (u8)(0xC0 | (run - 1));
                run = 0;
            }
            continue;
        }
        if (run > 0) {
            out[p++] = (u8)(0xC0 | (run - 1));
            run = 0;
        }
        {
            /* (alpha 255 in the hash, as the format's) */
            const int h6 = (c[0] * 3 + c[1] * 5 + c[2] * 7 + 255 * 11) % 64;
            if (seen[h6][3] == 255 && seen[h6][0] == c[0] && seen[h6][1] == c[1] && seen[h6][2] == c[2]) {
                out[p++] = (u8)h6;
            } else {
                const int dr = (int)(s8)(c[0] - prev[0]), dg = (int)(s8)(c[1] - prev[1]),
                          db = (int)(s8)(c[2] - prev[2]);
                const int drg = dr - dg, dbg = db - dg;
                seen[h6][0] = c[0];
                seen[h6][1] = c[1];
                seen[h6][2] = c[2];
                seen[h6][3] = 255;
                if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1) {
                    out[p++] = (u8)(0x40 | (dr + 2) << 4 | (dg + 2) << 2 | (db + 2));
                } else if (dg >= -32 && dg <= 31 && drg >= -8 && drg <= 7 && dbg >= -8 && dbg <= 7) {
                    out[p++] = (u8)(0x80 | (dg + 32));
                    out[p++] = (u8)((drg + 8) << 4 | (dbg + 8));
                } else {
                    out[p++] = 0xFE;
                    out[p++] = c[0];
                    out[p++] = c[1];
                    out[p++] = c[2];
                }
            }
        }
        prev[0] = c[0];
        prev[1] = c[1];
        prev[2] = c[2];
    }
    memset(out + p, 0, 7);
    out[p + 7] = 1;
    *len = p + 8;
    return out;
}

/* BMP: uncompressed, bottom-up BGR rows padded to 4 bytes */
static u8* encodeBmp(const u8* rgb, u32 w, u32 h, size_t* len) {
    const u32 row = (w * 3 + 3) & ~3u, size = 54 + row * h;
    u8* out = (u8*)calloc(1, size);
    u32 x, y;
    if (out == NULL) {
        return NULL;
    }
    out[0] = 'B';
    out[1] = 'M';
#define LE32(o, v) (out[o] = (u8)(v), out[o + 1] = (u8)((v) >> 8), out[o + 2] = (u8)((v) >> 16), out[o + 3] = (u8)((v) >> 24))
    LE32(2, size);
    LE32(10, 54u);
    LE32(14, 40u);
    LE32(18, w);
    LE32(22, h);
    out[26] = 1;
    out[28] = 24;
    LE32(34, row * h);
#undef LE32
    for (y = 0; y < h; y++) {
        const u8* s = rgb + (size_t)(h - 1 - y) * w * 3;
        u8* d = out + 54 + (size_t)y * row;
        for (x = 0; x < w; x++) {
            d[x * 3 + 0] = s[x * 3 + 2];
            d[x * 3 + 1] = s[x * 3 + 1];
            d[x * 3 + 2] = s[x * 3 + 0];
        }
    }
    *len = size;
    return out;
}

static void burstWrite(BurstJob* job) {
    char path[1200];
    size_t len = 0, i, n = (size_t)job->w * job->h;
    u8* bytes = NULL;
    int ok = 0;
    for (i = 0; i < n; i++) { /* RGBA to RGB, in place */
        job->rgb[i * 3 + 0] = job->rgb[i * 4 + 0];
        job->rgb[i * 3 + 1] = job->rgb[i * 4 + 1];
        job->rgb[i * 3 + 2] = job->rgb[i * 4 + 2];
    }
    snprintf(path, sizeof(path), "%s/burst_%05u.%s", sBurstDir, (unsigned)job->index, sBurstExt);
    if (strcmp(sBurstExt, "png") == 0) {
        SDL_Surface* s = SDL_CreateSurfaceFrom((int)job->w, (int)job->h, SDL_PIXELFORMAT_RGB24, job->rgb, (int)job->w * 3);
        ok = s != NULL && SDL_SavePNG(s, path);
        SDL_DestroySurface(s);
    } else {
        bytes = strcmp(sBurstExt, "bmp") == 0 ? encodeBmp(job->rgb, job->w, job->h, &len)
                                               : encodeQoi(job->rgb, job->w, job->h, &len);
        if (bytes != NULL) {
            FILE* f = portFopen(path, "wb");
            if (f != NULL) {
                ok = fwrite(bytes, 1, len, f) == len;
                ok = fclose(f) == 0 && ok;
            }
            free(bytes);
        }
    }
    if (!ok) {
        printf("[burst] cannot write %s\n", path);
        fflush(stdout);
        SDL_AddAtomicInt(&sBurstFailed, 1);
    }
    SDL_AddAtomicInt(&sBurstWritten, 1);
    burstRecycle(job);
}

static int SDLCALL burstWriter(void* unused) {
    (void)unused;
    for (;;) {
        BurstJob* job;
        SDL_LockMutex(sBurstLock);
        while (sBurstHead == NULL) {
            SDL_WaitCondition(sBurstWork, sBurstLock);
        }
        job = sBurstHead;
        sBurstHead = job->next;
        if (sBurstHead == NULL) {
            sBurstTail = NULL;
        }
        SDL_AddAtomicInt(&sBurstQueued, -1);
        SDL_SignalCondition(sBurstRoom);
        SDL_UnlockMutex(sBurstLock);
        burstWrite(job);
    }
    return 0;
}

/* on the GPU's map callback's thread: the window's picture, kept as RGB for a writer */
static void burstRead(void* user, const unsigned char* rgba, unsigned int w, unsigned int h) {
    BurstJob* job = NULL;
    const u32 index = (u32)(uintptr_t)user;
    size_t n = (size_t)w * h;
    if (rgba != NULL && w != 0 && h != 0) {
        job = burstJob(n * 4); /* (RGBA: made RGB by the writer) */
    }
    if (job == NULL) {
        printf("[burst] frame %u: no picture (%s)\n", (unsigned)index, rgba == NULL ? "can't be read" : "no memory");
        fflush(stdout);
        SDL_AddAtomicInt(&sBurstFailed, 1);
        SDL_AddAtomicInt(&sBurstWritten, 1);
        return;
    }
    memcpy(job->rgb, rgba, n * 4); /* (only a copy here: this thread draws the frames) */
    job->w = w;
    job->h = h;
    job->index = index;
    job->next = NULL;
    sBurstW = w;
    sBurstH = h;
    SDL_LockMutex(sBurstLock);
    if (sBurstTail != NULL) {
        sBurstTail->next = job;
    } else {
        sBurstHead = job;
    }
    sBurstTail = job;
    SDL_AddAtomicInt(&sBurstQueued, 1);
    SDL_SignalCondition(sBurstWork);
    SDL_UnlockMutex(sBurstLock);
}

/* $TTYD_USER_SHOT read: a burst (1 list, 2 start:count:step) or not (0) */
static int burstMode(void) {
    if (sBurstMode < 0) {
        const char* u = getenv("TTYD_USER_SHOT");
        sBurstMode = 0;
        if (u != NULL && u[0] == '@') {
            /* @NAME:count[:step]: from the frame the input script's `shot NAME` comes (burstMark) */
            unsigned c = 1, s = 1;
            const char* colon = strchr(u, ':');
            const size_t n = colon != NULL ? (size_t)(colon - u - 1) : strlen(u + 1);
            snprintf(sBurstMark, sizeof(sBurstMark), "%.*s", (int)(n < sizeof(sBurstMark) ? n : sizeof(sBurstMark) - 1),
                     u + 1);
            if (colon != NULL) {
                sscanf(colon + 1, "%u:%u", &c, &s);
            }
            sBurstMode = 2;
            sBurstStart = 0xFFFFFFFFu; /* (not yet) */
            sBurstCount = c;
            sBurstStep = s ? s : 1;
        } else if (u != NULL && strchr(u, ':') != NULL) {
            unsigned a = 0, c = 1, s = 1;
            if (sscanf(u, "%u:%u:%u", &a, &c, &s) >= 1) {
                sBurstMode = 2;
                sBurstStart = a;
                sBurstCount = c;
                sBurstStep = s ? s : 1;
            }
        } else if (u != NULL && strchr(u, ',') != NULL) {
            const char* p;
            u32 n = 1;
            for (p = u; *p; p++) {
                n += *p == ',';
            }
            sBurstList = (u32*)malloc(n * sizeof(u32));
            for (p = u; sBurstList != NULL && p != NULL && *p;) {
                sBurstList[sBurstListN++] = (u32)strtoul(p, NULL, 10);
                p = strchr(p, ',');
                p = p != NULL ? p + 1 : NULL;
            }
            sBurstMode = sBurstList != NULL ? 1 : 0;
            sBurstCount = sBurstListN;
        }
        if (sBurstMode != 0) {
            const char* d = getenv("TTYD_USER_SHOT_DIR");
            const char* fmt = getenv("TTYD_USER_SHOT_FORMAT");
            const char* t = getenv("TTYD_USER_SHOT_THREADS");
            int threads = t != NULL && atoi(t) > 0 ? atoi(t) : 4, i;
            threads = threads > BURST_THREADS_MAX ? BURST_THREADS_MAX : threads;
            snprintf(sBurstDir, sizeof(sBurstDir), "%s", d != NULL && *d ? d : dir());
            snprintf(sBurstExt, sizeof(sBurstExt), "%s",
                     fmt != NULL && (!strcmp(fmt, "png") || !strcmp(fmt, "bmp")) ? fmt : "qoi");
            SDL_CreateDirectory(sBurstDir);
            sBurstLock = SDL_CreateMutex();
            sBurstWork = SDL_CreateCondition();
            sBurstRoom = SDL_CreateCondition();
            for (i = 0; i < threads; i++) {
                SDL_Thread* th = SDL_CreateThread(burstWriter, "burst writer", NULL);
                if (th != NULL) {
                    SDL_DetachThread(th);
                }
            }
            printf("[burst] %u frames to %s as %s, %d writers\n", (unsigned)sBurstCount, sBurstDir, sBurstExt,
                   threads);
            fflush(stdout);
        }
    }
    return sBurstMode;
}

/* the input script's `shot NAME` (portShotRequest): a burst given as @NAME starts this frame */
static void burstMark(const char* name) {
    const size_t n = strlen(name), m = strlen(sBurstMark);
    /* (a TTYD_MAP run's script names its captures map__NAME) */
    const int match = m != 0 && (strcmp(name, sBurstMark) == 0 ||
                                 (n > m + 2 && strcmp(name + n - m, sBurstMark) == 0 && name[n - m - 1] == '_' &&
                                  name[n - m - 2] == '_'));
    if (burstMode() == 2 && match && sBurstStart == 0xFFFFFFFFu) {
        sBurstStart = VIGetRetraceCount();
        printf("[burst] %s: from frame %u\n", name, (unsigned)sBurstStart);
        fflush(stdout);
    }
}

static BOOL burstWants(u32 frame) {
    u32 i;
    if (burstMode() == 2) {
        return sBurstStart != 0xFFFFFFFFu && frame >= sBurstStart && (frame - sBurstStart) % sBurstStep == 0 &&
               (frame - sBurstStart) / sBurstStep < sBurstCount;
    }
    if (sBurstMode == 1) {
        for (i = 0; i < sBurstListN; i++) {
            if (sBurstList[i] == frame) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* the frame's picture asked for; first, while the writers are BURST_QUEUE behind, the game waits for them */
static void burstFrame(u32 frame) {
    Uint64 t;
    if (!burstWants(frame) || sBurstAsked >= sBurstCount) {
        return;
    }
    t = SDL_GetTicksNS();
    if (sBurstAsked == 0) {
        sBurstT0 = t;
    }
    if (SDL_GetAtomicInt(&sBurstQueued) >= BURST_QUEUE) {
        SDL_LockMutex(sBurstLock);
        while (SDL_GetAtomicInt(&sBurstQueued) >= BURST_QUEUE) {
            SDL_WaitConditionTimeout(sBurstRoom, sBurstLock, 100);
        }
        SDL_UnlockMutex(sBurstLock);
        sBurstWaitNs += SDL_GetTicksNS() - t;
    }
    if (!aurora_capture_window(burstRead, (void*)(uintptr_t)sBurstAsked)) {
        printf("[burst] frame %u: a capture was still waiting; not taken\n", (unsigned)frame);
        fflush(stdout);
        SDL_AddAtomicInt(&sBurstFailed, 1);
        SDL_AddAtomicInt(&sBurstWritten, 1);
    }
    sBurstAsked++;
}

static void burstReport(void);

/* a burst asked for and not all written yet (once it is, its report: an exit can come before the next frame) */
static BOOL burstPending(void) {
    const BOOL pending = burstMode() != 0 && (sBurstAsked < sBurstCount
                                                  ? sBurstAsked > 0
                                                  : (u32)SDL_GetAtomicInt(&sBurstWritten) < sBurstCount);
    if (!pending) {
        burstReport();
    }
    return pending;
}

/* once all are written: what it kept up with */
static void burstReport(void) {
    if (sBurstMode > 0 && !sBurstReported && sBurstAsked >= sBurstCount &&
        (u32)SDL_GetAtomicInt(&sBurstWritten) >= sBurstCount) {
        const double secs = (double)(SDL_GetTicksNS() - sBurstT0) / 1e9;
        sBurstReported = 1;
        printf("[burst] done: %u frames (%d failed) at %ux%u in %.2f s: %.1f frames a second; the game waited "
               "%.2f s for the writers\n",
               (unsigned)sBurstCount, SDL_GetAtomicInt(&sBurstFailed), (unsigned)sBurstW, (unsigned)sBurstH, secs,
               secs > 0 ? sBurstCount / secs : 0.0, (double)sBurstWaitNs / 1e9);
        fflush(stdout);
    }
}

BOOL portShotPending(void) {
    const int state = SDL_GetAtomicInt(&sPictureState);
    const u32 now = VIGetRetraceCount();
    int i;
    for (i = 0; i < SHOT_QUEUE; i++) {
        if (sQueue[i].used && (s32)(now - sQueue[i].due) >= 0) {
            return TRUE; /* (one waiting its turn: an exit after it waits too) */
        }
    }
    /* (and the <name>_ui.png: begun a frame after the game's capture, its readback in real time, it could
     * still be on its way once the game's was written, and a test's end left without it) */
    return sNext[0] != '\0' || sCapturing || state == 1 || state == 2 || state == 5 || burstPending() ||
           portUiShotPending();
}

/* $TTYD_SHOT: frames, and ranges A-B (every frame) or A-B/S (every S-th from A) */
static BOOL listed(u32 frame) {
    const char* p;
    if (!sListRead) {
        const char* v = getenv("TTYD_SHOT");
        sListRead = 1;
        sList = SDL_strdup(v ? v : "");
    }
    for (p = sList; p != NULL && *p; ) {
        char* end;
        const u32 a = (u32)strtoul(p, &end, 10);
        if (*end == '-') {
            u32 b = (u32)strtoul(end + 1, &end, 10), step = 1;
            if (*end == '/') {
                step = (u32)strtoul(end + 1, &end, 10);
                step = step ? step : 1;
            }
            if (frame >= a && frame <= b && (frame - a) % step == 0) {
                return TRUE;
            }
        } else if (a == frame) {
            return TRUE;
        }
        p = strchr(p, ',');
        if (p == NULL) {
            break;
        }
        p++;
    }
    return FALSE;
}

static void capture(void);

/* $TTYD_SHOT_INBETWEEN=k: the listed frames' captures are of the k-th frame
 * drawn after them by a higher $TTYD_FRAMERATE (interp_aurora.c). */
static int inbetween(void) {
    static int sInbetween = -1;
    if (sInbetween < 0) {
        const char* v = getenv("TTYD_SHOT_INBETWEEN");
        sInbetween = v != NULL ? atoi(v) : 0;
    }
    return sInbetween;
}

/* A listed frame ($TTYD_SHOT) while another capture is asked for or in flight: queued, taken (as shot_N) as
 * soon as that one is done, not dropped (a script's `shot` once cost the listed frame after it). At most a few
 * wait at once, so an every-frame range A-B skips frames as before instead of falling ever further behind. */
static void listedWait(u32 frame) {
    if (inbetween() == 0 && sListedWaiting < 4 && listed(frame)) {
        char name[32];
        snprintf(name, sizeof(name), "shot_%u", (unsigned)frame);
        enqueue(name, 0, 1); /* (counted only when queued: a full queue's drop doesn't lower the cap) */
    }
}

/* Called just before the game's GXCopyDisp (aurora_overlay/dolphin/gx.h). */
void portShotBeforeCopyDisp(u32 frame) {
    burstFrame(frame); /* $TTYD_USER_SHOT's burst: the window's picture this frame */
    if (sCapturing) {
        listedWait(frame);
        return;
    }
    {
        /* $TTYD_USER_SHOT=<frame> (tests): the player's screenshot then */
        const char* u = getenv("TTYD_USER_SHOT");
        if (u != NULL && burstMode() == 0 && (u32)strtoul(u, NULL, 10) == frame) {
            portShotRequestUser();
        }
    }
    if (sNext[0] == '\0') {
        queuedDue(frame);
    }
    if (sNext[0] == '\0' && inbetween() == 0 && listed(frame)) {
        snprintf(sNext, sizeof(sNext), "shot_%u", (unsigned)frame);
        portUiShotRequest(sNext); /* with $TTYD_SHOT_UI, the listed frames' shot_N_ui.png too */
    } else {
        listedWait(frame);
    }
    if (sNext[0] != '\0') {
        capture();
    }
}

/* The game held (frame advance, photo mode: vi_aurora.c), its frame drawn
 * again: a capture asked for now is of this picture (the game's own frame
 * copy won't come until it goes on). */
void portShotHeld(void) {
    burstFrame(VIGetRetraceCount()); /* (a burst from photo mode's picture: @photo_clean, the held frame) */
    if (!sCapturing && sNext[0] == '\0') {
        /* one queued behind a capture still in flight when the hold began: taken now, of the held picture
         * it was asked for (no GXCopyDisp comes to take it while held; the retrace count stands still, so
         * "due now" entries qualify) */
        queuedDue(VIGetRetraceCount());
    }
    if (!sCapturing && sNext[0] != '\0') {
        capture();
    }
}

/* In-between frame k, drawn after the game's frame (vi_aurora.c). */
void portShotInbetween(u32 frame, int k) {
    static int sBurstAll = -1;
    if (sBurstAll < 0) {
        const char* v = getenv("TTYD_USER_SHOT_INBETWEEN");
        sBurstAll = v != NULL && v[0] == '1';
    }
    if (sBurstAll) {
        burstFrame(frame); /* $TTYD_USER_SHOT_INBETWEEN=1: a burst takes the in-between frames shown too */
    }
    if (!sCapturing && sNext[0] == '\0' && k == inbetween() && listed(frame)) {
        snprintf(sNext, sizeof(sNext), "shot_%u_%d", (unsigned)frame, k);
        capture();
    }
}

/* Copies the frame drawn so far, for the capture named sNext. */
/* The copy's 640 x 480 source is wider than the game's 608-wide EFB (fbWidth): Aurora clamps the copy's rectangle
 * to the EFB (map_logical_scissor) and scales all of it into the 640 x 480 copy, as the VI spreads the 608 pixels
 * across the 4:3 screen, so the picture has the shape shown (not 5% too wide, nothing cut or repeated). */
static void capture(void) {
    AuroraReadbackCopySrc src;
    if (sStream == 0) {
        sStream = AuroraReadbackCreateStream();
    }
    GXSetTexCopySrc(0, 0, SHOT_W, SHOT_H);
    GXSetTexCopyDst(SHOT_W, SHOT_H, GX_TF_RGBA8, GX_FALSE);
    GXCopyTex(&sCopyId, GX_FALSE);
    src.key = ++sKey;
    src.src = &sCopyId;
    if (!AuroraReadbackCaptureCopies(sStream, &src, 1)) {
        printf("[shot] readback unavailable on this GPU\n");
        if (sUserNext) {
            status("Screenshots are not available on this GPU");
        }
        sNext[0] = '\0';
        sUserNext = 0;
        return;
    }
    snprintf(sName, sizeof(sName), "%s/%s.png", dir(), sNext);
    sNext[0] = '\0';
    sUser = sUserNext;
    sUserNext = 0;
    sCapturing = 1;
    sWaited = 0;
}

void portGXCopyDisp(void* dest, GXBool clear) {
    portStereoCompose(); /* stereoscopic 3D: the two eyes put together first, so a capture has both */
    portShotBeforeCopyDisp(VIGetRetraceCount());
    GXCopyDisp(dest, clear);
}

/* out of memory: this capture skipped */
static void shotFailed(void) {
    printf("[shot] out of memory for %s\n", sName);
    fflush(stdout);
    if (sUser) {
        status("Screenshot failed");
    }
    sCapturing = 0;
}

/* Called once per retrace: collects a finished capture. */
void portShotFrame(void) {
    AuroraReadbackDest dest;
    burstReport();
    pictureFrame();
    if (!sCapturing) {
        return;
    }
    if (sPixels == NULL) {
        sPixels = (u8*)malloc(SHOT_BYTES);
        if (sPixels == NULL) {
            shotFailed();
            return;
        }
    }
    dest.key = sKey;
    dest.dest = sPixels;
    dest.size = SHOT_BYTES;
    dest.found = GX_FALSE;
    if (AuroraReadbackGet(sStream, &dest, 1) && dest.found) {
        u8* rgb = (u8*)malloc(SHOT_W * SHOT_H * 3);
        if (rgb == NULL) {
            shotFailed();
            return;
        }
        untile(sPixels, rgb);
        if (portWidescreenWide()) {
            /* the copy holds the whole widened frame in 4:3: widen it back */
            const int w = (int)(SHOT_W * portWidescreenAspect() * 0.75f + 0.5f);
            u8* wide = (u8*)malloc((size_t)w * SHOT_H * 3);
            int x, y, c;
            if (wide == NULL) {
                free(rgb);
                shotFailed();
                return;
            }
            for (y = 0; y < SHOT_H; y++) {
                for (x = 0; x < w; x++) {
                    const float fx = (x + 0.5f) * SHOT_W / w - 0.5f;
                    const int x0 = fx < 0 ? 0 : (int)fx;
                    const int x1 = x0 + 1 < SHOT_W ? x0 + 1 : x0;
                    const float t = fx < 0 ? 0.f : fx - x0;
                    for (c = 0; c < 3; c++) {
                        const u8 a = rgb[(y * SHOT_W + x0) * 3 + c], b = rgb[(y * SHOT_W + x1) * 3 + c];
                        wide[(y * w + x) * 3 + c] = (u8)(a + (b - a) * t + 0.5f);
                    }
                }
            }
            if (sUser) {
                saveUserShot(wide, w, SHOT_H);
            } else {
                writePng(sName, wide, w, SHOT_H);
            }
            free(wide);
        } else if (sUser) {
            saveUserShot(rgb, SHOT_W, SHOT_H);
        } else {
            writePng(sName, rgb, SHOT_W, SHOT_H);
        }
        free(rgb);
        sCapturing = 0;
    } else if (++sWaited > 30) {
        printf("[shot] no readback for %s after 30 frames\n", sName);
        if (sUser) {
            status("Screenshot failed");
        }
        fflush(stdout);
        sCapturing = 0;
    }
}
