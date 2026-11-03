/* disc_sweep: decode every file of a disc with the PC loaders and report
 * every failure in one run (instead of finding them one boot at a time).
 *
 * Reads "kind path" lines on stdin (tools/port/disc_sweep.py builds them from
 * an extracted disc), kinds: anim, map, camroad, tpl, font. Each file is read
 * into a fresh 32-byte-aligned buffer, decoded, checked and released. Prints
 * one line per failing file and a summary; exit status 1 if anything failed. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/anim_pc.h"
#include "port/bedecode.h"
#include "port/camroad_pc.h"
#include "port/font_pc.h"
#include "port/map_pc.h"
#include "port/tpl_pc.h"

static char sError[512];
static int sErrors;

static void onError(struct PortDecodeCtx* ctx, const char* msg) {
    (void)ctx;
    if (sErrors++ == 0) {
        snprintf(sError, sizeof(sError), "%s", msg);
    }
}

static void* readFile(const char* path, uint32_t* size, void** base) {
    FILE* f = fopen(path, "rb");
    long n;
    uint8_t* p;
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *base = malloc((size_t)n + 64);
    p = (uint8_t*)(((uintptr_t)*base + 31) & ~(uintptr_t)31);
    if (fread(p, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(*base);
        return NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return p;
}

/* Returns 1 if the file decoded cleanly; fills sError otherwise. */
static int sweepOne(const char* kind, const char* path) {
    void* base;
    uint32_t size;
    void* blob = readFile(path, &size, &base);
    int ok = 1;
    sErrors = 0;
    sError[0] = '\0';
    if (!blob) {
        snprintf(sError, sizeof(sError), "cannot read");
        return 0;
    }
    if (strcmp(kind, "anim") == 0) {
        ok = portAnimDecode(blob, size, path) != NULL;
        portAnimRelease(blob);
    } else if (strcmp(kind, "map") == 0) {
        PortMapData* d = portMapDecode(blob, size, path);
        ok = d != NULL;
        if (d) {
            uint32_t offs[4];
            uint32_t n = portMapUncoveredRelocs(d, offs, 4);
            if (n) {
                snprintf(sError, sizeof(sError), "%u relocations not covered by the decoder (first at 0x%X)",
                         (unsigned)n, (unsigned)offs[0]);
                ok = 0;
            }
            portMapRelease(d);
        }
    } else if (strcmp(kind, "camroad") == 0) {
        ok = portCamRoadDecode(blob, size, path) != NULL;
    } else if (strcmp(kind, "tpl") == 0) {
        ok = portTplDecode(blob, size, path) != NULL;
        portTplRelease(blob);
    } else if (strcmp(kind, "font") == 0) {
        ok = portFontSwap(blob, size) != 0;
        if (!ok && !sError[0]) {
            snprintf(sError, sizeof(sError), "portFontSwap rejected the file");
        }
    } else {
        snprintf(sError, sizeof(sError), "unknown kind '%s'", kind);
        ok = 0;
    }
    free(base);
    return ok && sErrors == 0;
}

int main(void) {
    char line[1024];
    int files = 0, failed = 0;
    portAnimErrorHook = onError;
    portMapErrorHook = onError;
    portCamRoadErrorHook = onError;
    portTplErrorHook = onError;
    while (fgets(line, sizeof(line), stdin)) {
        char* sp = strchr(line, ' ');
        char* path;
        size_t n;
        if (!sp) {
            continue;
        }
        *sp = '\0';
        path = sp + 1;
        n = strlen(path);
        while (n && (path[n - 1] == '\n' || path[n - 1] == '\r')) {
            path[--n] = '\0';
        }
        files++;
        fprintf(stderr, "@ %s\n", path); /* the last one names the file if a decoder crashes */
        fflush(stderr);
        if (!sweepOne(line, path)) {
            failed++;
            printf("FAIL %-7s %s: %s%s\n", line, path, sError[0] ? sError : "decoder returned NULL",
                   sErrors > 1 ? " (and more)" : "");
            fflush(stdout);
        }
    }
    printf("disc_sweep: %d files, %d failed\n", files, failed);
    return failed ? 1 : 0;
}
