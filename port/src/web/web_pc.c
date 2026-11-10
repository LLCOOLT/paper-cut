/* The browser build's own pieces (cmake/web.cmake):
 * - the disc image: the file the player picked on the page, read where it is
 *   (Blob slices, a few at a time, cached), never copied whole into memory;
 * - the frame: each one handed to the browser (requestAnimationFrame), which
 *   shows it, and paced to the game's 60 Hz on faster screens;
 * - saves: the user folder is IndexedDB, written back now and then.
 * The page (port/web/papercut.js) sets Module.papercutDisc and the folders. */
#include <emscripten.h>
#include <aurora/dvd.h>
#include <dolphin/os.h>
#include <stdlib.h>
#include <string.h>
#include "port/web.h"

/* --- the disc ------------------------------------------------------------ */

EM_JS(double, webDiscLength, (void), {
    const f = Module.papercutDisc;
    return f ? f.size : -1;
});

/* Reads [offset, offset + len) of the picked file into out; the bytes read. */
EM_ASYNC_JS(int, webDiscRead, (double offset, void* out, int len), {
    const f = Module.papercutDisc;
    if (!f) return -1;
    try {
        const buf = new Uint8Array(await f.slice(offset, offset + len).arrayBuffer());
        HEAPU8.set(buf, out);
        return buf.length;
    } catch (e) {
        console.error("disc read failed", e);
        return -1;
    }
});

/* A small cache of 1 MiB blocks: nod and the game read in small pieces, and
 * each read from the page is a trip through the browser's event loop. */
#define BLOCK_SIZE (1u << 20)
#define BLOCK_COUNT 24
typedef struct Block {
    s64 index; /* which block of the image; -1: none */
    u32 size;
    u32 used; /* last use, for eviction */
    u8* data;
} Block;
static Block sBlocks[BLOCK_COUNT];
static u32 sUseClock;
static s64 sDiscSize = -1;

static Block* getBlock(s64 index) {
    Block* free = NULL;
    Block* oldest = NULL;
    int i, n;
    for (i = 0; i < BLOCK_COUNT; i++) {
        Block* b = &sBlocks[i];
        if (b->index == index && b->data != NULL) {
            b->used = ++sUseClock;
            return b;
        }
        if (b->data == NULL && free == NULL) {
            free = b;
        }
        if (oldest == NULL || b->used < oldest->used) {
            oldest = b;
        }
    }
    if (free == NULL) {
        free = oldest;
    }
    if (free->data == NULL) {
        free->data = malloc(BLOCK_SIZE);
        if (free->data == NULL) {
            return NULL;
        }
    }
    free->index = -1;
    n = webDiscRead((double)index * BLOCK_SIZE, free->data, BLOCK_SIZE);
    if (n < 0) {
        return NULL;
    }
    free->index = index;
    free->size = (u32)n;
    free->used = ++sUseClock;
    return free;
}

static int64_t discReadAt(void* user, uint64_t offset, void* out, size_t len) {
    u8* dst = (u8*)out;
    size_t done = 0;
    (void)user;
    if (sDiscSize >= 0 && offset >= (uint64_t)sDiscSize) {
        return 0;
    }
    /* big reads straight through (a whole file of a map), small ones through the cache */
    if (len >= 4 * BLOCK_SIZE) {
        return webDiscRead((double)offset, out, (int)len);
    }
    while (done < len) {
        const uint64_t pos = offset + done;
        Block* b = getBlock((s64)(pos / BLOCK_SIZE));
        u32 within, n;
        if (b == NULL) {
            return done > 0 ? (int64_t)done : -1;
        }
        within = (u32)(pos % BLOCK_SIZE);
        if (within >= b->size) {
            break; /* the end of the image */
        }
        n = b->size - within;
        if (n > len - done) {
            n = (u32)(len - done);
        }
        memcpy(dst + done, b->data + within, n);
        done += n;
    }
    return (int64_t)done;
}

static int64_t discLength(void* user) {
    (void)user;
    return sDiscSize;
}

BOOL portWebOpenDisc(void) {
    static const AuroraDvdStream kStream = {NULL, discReadAt, discLength, NULL};
    int i;
    for (i = 0; i < BLOCK_COUNT; i++) {
        sBlocks[i].index = -1;
    }
    sDiscSize = (s64)webDiscLength();
    if (sDiscSize <= 0) {
        return FALSE;
    }
    return aurora_dvd_open_stream(&kStream);
}

/* --- frames ---------------------------------------------------------------- */

/* Waits for the browser's next frame (it shows the one just drawn, and runs
 * input and the rest meanwhile); its interval in ms, or 0 if unknown. Tests
 * (Module.papercutFast) don't wait for the screen: a message turn only. */
EM_ASYNC_JS(double, webNextFrame, (void), {
    if (Module.papercutFast) {
        await new Promise(r => {
            const c = new MessageChannel();
            c.port1.onmessage = () => r();
            c.port2.postMessage(0);
        });
        if (Module.papercutOnFrame) Module.papercutOnFrame(performance.now());
        return 0;
    }
    const t = await new Promise(r => requestAnimationFrame(r));
    const last = Module.papercutLastFrame || 0;
    Module.papercutLastFrame = t;
    const dt = last ? t - last : 0;
    if (dt > 0 && dt < 100) {
        Module.papercutFrameMs = Module.papercutFrameMs ? Module.papercutFrameMs * 0.9 + dt * 0.1 : dt;
    }
    if (Module.papercutOnFrame) Module.papercutOnFrame(t);
    return Module.papercutFrameMs || 0;
});

/* Writes the user folder back to IndexedDB (the page's syncfs, one at a time). */
EM_JS(void, webSaveUserFolder, (void), {
    if (Module.papercutSave) Module.papercutSave();
});

void portWebRunQueued(void); /* port/src/web/web_later.cpp */

static OSTime sDue; /* when the next game frame is due; 0: at once */

void portWebPaceTo(OSTime when) {
    sDue = when;
}

void portWebYield(void) {
    static double sLastSave;
    double frameMs;
    do {
        frameMs = webNextFrame();
        /* on with the game's frame once it is within half a screen frame of
         * its time (60 Hz screens: every frame; faster ones: some skipped) */
    } while (sDue != 0 && frameMs > 0 &&
             OSGetTime() + (OSTime)(frameMs * 0.5 * (OS_TIMER_CLOCK / 1000)) < sDue);
    sDue = 0;
    portWebRunQueued();
    if (emscripten_get_now() - sLastSave > 3000.0) {
        sLastSave = emscripten_get_now();
        webSaveUserFolder();
    }
}
