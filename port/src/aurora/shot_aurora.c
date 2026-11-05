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
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#undef GXCopyDisp /* the real one, from the wrapper below */
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_surface.h>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/achievements.h"
#include "port/photo.h"
#include "port/platform.h"
#include "port/widescreen.h"

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
static char sList[512];
static u8 sCopyId;       /* GXCopyTex dest: only an identifier for Aurora */
static u8* sPixels;
static int sUserNext;    /* the requested capture is the player's screenshot */
static int sUser;        /* the in-flight capture is */
static char sStatus[128];
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

typedef struct PngBuf {
    u8* data;
    size_t len, cap;
} PngBuf;

static void put(PngBuf* b, const void* p, size_t n) {
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2;
        b->data = (u8*)realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, p, n);
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

/* The PNG file's bytes (malloc'd, *len set). */
static u8* encodePng(const u8* rgb, u32 w, u32 h, size_t* len) {
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    u8 ihdr[13];
    u32 row = w * 3 + 1, raw = row * h, blocks = (raw + 65534) / 65535;
    u32 zlen = 2 + raw + blocks * 5 + 4, i, a = 1, b = 0, pos = 0, done = 0;
    u8* z = (u8*)malloc(zlen);
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
        PngBuf b = {NULL, 0, 0};
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
    FILE* f = fopen(path, "wb");
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

void portShotRequest(const char* name) {
    snprintf(sNext, sizeof(sNext), "%s", name);
}

/* The player's screenshot (the settings page, F12): the next frame, to Photos
 * on iOS, else a PNG in the Pictures folder (portShotStatus says where). */
/* the picture as shown, read back (below): 0 none, 1 asked for, 2 here
 * (sPicture), 3 can't be read (the EFB copy instead), 4 given up on */
static SDL_AtomicInt sPictureState;
static u8* sPicture;
static u32 sPictureW, sPictureH, sPictureWaited;
static void pictureRead(void* user, const unsigned char* rgba, unsigned int w, unsigned int h);

void portShotRequestUser(void) {
    const int state = SDL_GetAtomicInt(&sPictureState);
    if (sCapturing || sNext[0] != '\0' || state == 1 || state == 2) {
        return;
    }
    if (getenv("TTYD_SHOT_EFB") == NULL && aurora_capture_picture(pictureRead, NULL)) {
        SDL_SetAtomicInt(&sPictureState, 1);
        sPictureWaited = 0;
        return;
    }
    snprintf(sNext, sizeof(sNext), "screenshot");
    sUserNext = 1;
}

const char* portShotStatus(void) {
    return sStatus[0] != '\0' && SDL_GetTicks() - sStatusTime < 3000 ? sStatus : NULL;
}

static void status(const char* text) {
    snprintf(sStatus, sizeof(sStatus), "%s", text);
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

/* a new picture's file: Paper Cut <date> at <time>.png, (2) and on if taken
 * in the same second */
static void userShotPath(char* out, size_t size) {
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
    for (n = 2; n < 100 && SDL_GetPathInfo(out, NULL); n++) {
        snprintf(out, size, "%s/Paper Cut %s (%d).png", folder, when, n);
    }
}

/* the outcome of a picture saved off the game's thread, for portShotFrame's
 * status line: 0 none yet, 1 saved, 2 not saved, 3 no access to Photos */
static SDL_AtomicInt sSaved;

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

static int savePng(SDL_Surface* surface) {
    char path[1200];
    int ok;
    userShotPath(path, sizeof(path));
    ok = SDL_SavePNG(surface, path);
    printf(ok ? "[shot] wrote %s\n" : "[shot] cannot write %s\n", path);
    fflush(stdout);
#if defined(__APPLE__) && TARGET_OS_IPHONE
    {
        SDL_IOStream* io = SDL_IOFromDynamicMem();
        int put = 0;
        if (io != NULL && SDL_SavePNG_IO(surface, io, false)) {
            const u8* bytes = (const u8*)SDL_GetPointerProperty(SDL_GetIOProperties(io),
                                                                SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
            put = bytes != NULL && portIosSaveToPhotos(bytes, (size_t)SDL_GetIOSize(io));
        }
        if (io != NULL) {
            SDL_CloseIO(io);
        }
        return put ? 1 : 3;
    }
#else
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
    (void)user;
    if (rgba == NULL || w == 0 || h == 0) {
        SDL_CompareAndSwapAtomicInt(&sPictureState, 1, 3);
        return;
    }
    if (SDL_GetAtomicInt(&sPictureState) != 1) {
        return; /* given up on */
    }
    sPicture = (u8*)malloc((size_t)w * h * 4);
    if (sPicture == NULL) {
        SDL_CompareAndSwapAtomicInt(&sPictureState, 1, 3);
        return;
    }
    memcpy(sPicture, rgba, (size_t)w * h * 4);
    sPictureW = w;
    sPictureH = h;
    SDL_CompareAndSwapAtomicInt(&sPictureState, 1, 2);
}

typedef struct SaveJob {
    u8* rgba;
    u32 w, h;
} SaveJob;

static int SDLCALL saveThread(void* data) {
    SaveJob* job = (SaveJob*)data;
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom((int)job->w, (int)job->h, SDL_PIXELFORMAT_RGBA32, job->rgba, (int)job->w * 4);
    SDL_SetAtomicInt(&sSaved, surface != NULL ? savePng(surface) : 2);
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
        SDL_SetAtomicInt(&sSaved, 0);
        savedStatus(saved);
        portPhotoGalleryChanged();
        if (saved == 1) {
            portAchievementCounterAdd("photos", 1); /* a photo mode picture saved (port/achievements.h) */
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
    } else if (state == 3 || (state == 1 && ++sPictureWaited > 120)) {
        /* not readable here (or never came): the game's own frame copy; a
         * late answer (state 4) is dropped */
        SDL_SetAtomicInt(&sPictureState, state == 3 ? 0 : 4);
        snprintf(sNext, sizeof(sNext), "screenshot");
        sUserNext = 1;
    }
}

BOOL portShotPending(void) {
    const int state = SDL_GetAtomicInt(&sPictureState);
    return sNext[0] != '\0' || sCapturing || state == 1 || state == 2;
}

static BOOL listed(u32 frame) {
    const char* p;
    if (!sListRead) {
        const char* v = getenv("TTYD_SHOT");
        sListRead = 1;
        snprintf(sList, sizeof(sList), "%s", v ? v : "");
    }
    for (p = sList; *p; ) {
        if ((u32)strtoul(p, NULL, 10) == frame) {
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

/* Called just before the game's GXCopyDisp (aurora_overlay/dolphin/gx.h). */
void portShotBeforeCopyDisp(u32 frame) {
    if (sCapturing) {
        return;
    }
    {
        /* $TTYD_USER_SHOT=<frame> (tests): the player's screenshot then */
        const char* u = getenv("TTYD_USER_SHOT");
        if (u != NULL && (u32)strtoul(u, NULL, 10) == frame) {
            portShotRequestUser();
        }
    }
    if (sNext[0] == '\0' && inbetween() == 0 && listed(frame)) {
        snprintf(sNext, sizeof(sNext), "shot_%u", (unsigned)frame);
    }
    if (sNext[0] != '\0') {
        capture();
    }
}

/* The game held (frame advance, photo mode: vi_aurora.c), its frame drawn
 * again: a capture asked for now is of this picture (the game's own frame
 * copy won't come until it goes on). */
void portShotHeld(void) {
    if (!sCapturing && sNext[0] != '\0') {
        capture();
    }
}

/* In-between frame k, drawn after the game's frame (vi_aurora.c). */
void portShotInbetween(u32 frame, int k) {
    if (!sCapturing && sNext[0] == '\0' && k == inbetween() && listed(frame)) {
        snprintf(sNext, sizeof(sNext), "shot_%u_%d", (unsigned)frame, k);
        capture();
    }
}

/* Copies the frame drawn so far, for the capture named sNext. */
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
    portShotBeforeCopyDisp(VIGetRetraceCount());
    GXCopyDisp(dest, clear);
}

/* Called once per retrace: collects a finished capture. */
void portShotFrame(void) {
    AuroraReadbackDest dest;
    pictureFrame();
    if (!sCapturing) {
        return;
    }
    if (sPixels == NULL) {
        sPixels = (u8*)malloc(SHOT_BYTES);
    }
    dest.key = sKey;
    dest.dest = sPixels;
    dest.size = SHOT_BYTES;
    dest.found = GX_FALSE;
    if (AuroraReadbackGet(sStream, &dest, 1) && dest.found) {
        u8* rgb = (u8*)malloc(SHOT_W * SHOT_H * 3);
        untile(sPixels, rgb);
        if (portWidescreenWide()) {
            /* the copy holds the whole widened frame in 4:3: widen it back */
            const int w = (int)(SHOT_W * portWidescreenAspect() * 0.75f + 0.5f);
            u8* wide = (u8*)malloc((size_t)w * SHOT_H * 3);
            int x, y, c;
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
