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
 * $TTYD_SHOT_INBETWEEN=<k>          capture instead the k-th in-between frame
 *                                   after each listed one (a higher
 *                                   $TTYD_FRAMERATE), as shot_<frame>_<k> */
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#undef GXCopyDisp /* the real one, from the wrapper below */
#include <SDL3/SDL_filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void chunk(FILE* f, const char* type, const u8* data, u32 len) {
    u8 hdr[8];
    u32 c;
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    fwrite(data, 1, len, f);
    c = crc(0xFFFFFFFFu, (const u8*)type, 4);
    c = crc(c, data, len) ^ 0xFFFFFFFFu;
    be32(hdr, c);
    fwrite(hdr, 1, 4, f);
}

/* RGB, filter 0 rows, zlib "stored" blocks: no compression library needed. */
static const char* dir(void) {
    const char* d = getenv("TTYD_SHOT_DIR");
    return d != NULL && *d ? d : ".";
}

static void writePng(const char* path, const u8* rgb, u32 w, u32 h) {
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    u8 ihdr[13];
    u32 row = w * 3 + 1, raw = row * h, blocks = (raw + 65534) / 65535;
    u32 zlen = 2 + raw + blocks * 5 + 4, i, a = 1, b = 0, pos = 0, done = 0;
    u8* z = (u8*)malloc(zlen);
    FILE* f;
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
    SDL_CreateDirectory(dir()); /* with its parents */
    f = fopen(path, "wb");
    if (f != NULL) {
        fwrite(sig, 1, 8, f);
        chunk(f, "IHDR", ihdr, 13);
        chunk(f, "IDAT", z, pos);
        chunk(f, "IEND", NULL, 0);
        fclose(f);
        printf("[shot] wrote %s\n", path);
    } else {
        printf("[shot] cannot write %s\n", path);
    }
    fflush(stdout);
    free(z);
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

BOOL portShotPending(void) {
    return sNext[0] != '\0' || sCapturing;
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
    if (sNext[0] == '\0' && inbetween() == 0 && listed(frame)) {
        snprintf(sNext, sizeof(sNext), "shot_%u", (unsigned)frame);
    }
    if (sNext[0] != '\0') {
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
        sNext[0] = '\0';
        return;
    }
    snprintf(sName, sizeof(sName), "%s/%s.png", dir(), sNext);
    sNext[0] = '\0';
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
        if (portWidescreenEnabled()) {
            /* the copy holds the whole 16:9 frame in 4:3: widen it back */
            const int w = SHOT_W * 4 / 3;
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
            writePng(sName, wide, w, SHOT_H);
            free(wide);
        } else {
            writePng(sName, rgb, SHOT_W, SHOT_H);
        }
        free(rgb);
        sCapturing = 0;
    } else if (++sWaited > 30) {
        printf("[shot] no readback for %s after 30 frames\n", sName);
        fflush(stdout);
        sCapturing = 0;
    }
}
