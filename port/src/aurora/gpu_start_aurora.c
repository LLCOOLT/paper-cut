/* A graphics API that ended the game while it started is passed over at the next start
 * (port/include/port/gpu_start.h). A player's Intel UHD (a 2020 driver) crashed inside the driver while
 * Direct3D 12 started, every time, so neither the launcher nor the game ever opened. */
#include "port/gpu_start.h"
#include "port/file_pc.h"
#include "port/offscreen.h"
#include "port/settings.h"
#include "port/tr.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <aurora/aurora.h>

static const struct {
    AuroraBackend backend;
    const char* key;  /* in gpu_start.txt and $TTYD_GPU */
    const char* name; /* for the player */
} kApis[] = {
    {BACKEND_D3D12, "d3d12", "Direct3D 12"}, {BACKEND_VULKAN, "vulkan", "Vulkan"},
    {BACKEND_D3D11, "d3d11", "Direct3D 11"}, {BACKEND_METAL, "metal", "Metal"},
    {BACKEND_OPENGLES, "opengles", "OpenGL ES"}, {BACKEND_OPENGL, "opengl", "OpenGL"},
    {BACKEND_WEBGPU, "webgpu", "WebGPU"},
};
#define API_COUNT (sizeof(kApis) / sizeof(kApis[0]))

/* every graphics API Aurora tries on this platform (aurora.cpp's order, Null aside): all of them on the list
 * means none would be left, so the list starts over */
#if defined(_WIN32)
static const AuroraBackend kPlatformApis[] = {BACKEND_D3D12, BACKEND_VULKAN, BACKEND_D3D11};
#elif defined(__APPLE__)
static const AuroraBackend kPlatformApis[] = {BACKEND_METAL};
#else
static const AuroraBackend kPlatformApis[] = {BACKEND_VULKAN, BACKEND_OPENGLES};
#endif

static char sPath[1100];
static unsigned sSkip;    /* the APIs passed over: a bit per AuroraBackend */
static unsigned sNewSkip; /* ... found this start (the notice) */
static int sSetting;      /* the Graphics API setting (gfx_backend) when the list was written */
static AuroraBackend sForced = BACKEND_AUTO; /* $TTYD_GPU's: never passed over */
static int sAttempts;
static int sDone = 1; /* (until portGpuStartConfig) */

static int apiIndex(AuroraBackend b) {
    size_t i;
    for (i = 0; i < API_COUNT; i++) {
        if (kApis[i].backend == b) {
            return (int)i;
        }
    }
    return -1;
}

static const char* apiName(AuroraBackend b) {
    const int i = apiIndex(b);
    return i >= 0 ? kApis[i].name : "?";
}

static unsigned bit(AuroraBackend b) {
    return 1u << (unsigned)b;
}

/* gpu_start.txt: the list, the setting it was written under and, while one starts, its name (written through and
 * closed before the driver is called: a crash in it leaves the line on storage) */
static void writeRecord(AuroraBackend starting) {
    FILE* f;
    size_t i;
    if (sPath[0] == '\0') {
        return;
    }
    if (sSkip == 0 && starting == BACKEND_AUTO) {
        remove(sPath);
        return;
    }
    f = portFopen(sPath, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "# Graphics APIs that ended Paper Cut while they started, passed over from then on (choosing another\n"
               "# Graphics API in Settings > Graphics tries them all again). Deleting this file does too.\n");
    fprintf(f, "setting=%d\n", sSetting);
    for (i = 0; i < API_COUNT; i++) {
        if (sSkip & bit(kApis[i].backend)) {
            fprintf(f, "skip=%s\n", kApis[i].key);
        }
    }
    if (starting != BACKEND_AUTO && apiIndex(starting) >= 0) {
        fprintf(f, "starting=%s\n", kApis[apiIndex(starting)].key);
    }
    fflush(f);
    fclose(f);
}

static AuroraBackend fromKey(const char* key) {
    size_t i;
    for (i = 0; i < API_COUNT; i++) {
        if (strcmp(key, kApis[i].key) == 0) {
            return kApis[i].backend;
        }
    }
    return BACKEND_AUTO;
}

/* Aurora, before each API it tries (aurora.cpp): false passes it over */
static bool attemptCallback(AuroraBackend backend) {
    if (backend == BACKEND_NULL || apiIndex(backend) < 0) {
        return true;
    }
    if ((sSkip & bit(backend)) && backend != sForced) {
        printf("[gpu] %s passed over: it ended the game while it started before (gpu_start.txt)\n",
               apiName(backend));
        fflush(stdout);
        return false;
    }
    printf("[gpu] starting %s\n", apiName(backend));
    fflush(stdout);
    writeRecord(backend);
    portCrashSetGpuStarting(apiName(backend));
    sAttempts++;
    {
        /* $TTYD_CRASH_TEST=gpu_start (tests): a crash while the first API starts, as a driver's */
        const char* test = getenv("TTYD_CRASH_TEST");
        if (sAttempts == 1 && test != NULL && strcmp(test, "gpu_start") == 0) {
            printf("[crash test] gpu_start, while %s starts\n", apiName(backend));
            fflush(stdout);
            *(volatile int*)(uintptr_t)8 = 1;
        }
    }
    return true;
}

void portGpuStartConfig(void* configPtr) {
    AuroraConfig* config = (AuroraConfig*)configPtr;
    char* pref = NULL;
    const char* dir = config->userPath;
    char line[128];
    FILE* f;
    int recorded = -1;
    AuroraBackend starting = BACKEND_AUTO;
    size_t i, n;
    unsigned all = 0;
    if (dir == NULL) {
        pref = SDL_GetPrefPath(NULL, "ttyd"); /* Aurora's default (its appName) */
        dir = pref;
    }
    if (dir == NULL || dir[0] == '\0') {
        SDL_free(pref);
        return;
    }
    n = strlen(dir);
    snprintf(sPath, sizeof(sPath), "%s%sgpu_start.txt", dir,
             n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\') ? "" : "/");
    SDL_free(pref);
    if (getenv("TTYD_GPU") != NULL) {
        sForced = config->desiredBackend; /* (main_aurora.c: $TTYD_GPU read just before) */
    }
    sSetting = portSettingsPeek(config->userPath, "gfx_backend", 0);
    f = portFopen(sPath, "r");
    if (f != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            line[strcspn(line, "\r\n")] = '\0';
            if (strncmp(line, "setting=", 8) == 0) {
                recorded = atoi(line + 8);
            } else if (strncmp(line, "skip=", 5) == 0 && fromKey(line + 5) != BACKEND_AUTO) {
                sSkip |= bit(fromKey(line + 5));
            } else if (strncmp(line, "starting=", 9) == 0) {
                starting = fromKey(line + 9);
            }
        }
        fclose(f);
    }
    if (sSkip != 0 && recorded >= 0 && recorded != sSetting) {
        printf("[gpu] the Graphics API setting changed: every graphics API is tried again\n");
        sSkip = 0;
    }
    if (starting != BACKEND_AUTO) {
        /* the last run ended while it started: inside the driver (a crash, or a hang the player ended) */
        printf("[gpu] the last start ended while %s was starting: passed over from now on\n", apiName(starting));
        sSkip |= bit(starting);
        sNewSkip |= bit(starting);
    }
    for (i = 0; i < sizeof(kPlatformApis) / sizeof(kPlatformApis[0]); i++) {
        all |= bit(kPlatformApis[i]);
    }
    if (sSkip != 0 && (sSkip & all) == all) {
        printf("[gpu] every graphics API ended the game while it started: all of them are tried again\n");
        sSkip = 0;
        sNewSkip = 0;
    }
    for (i = 0; i < API_COUNT; i++) {
        if (sSkip & bit(kApis[i].backend)) {
            printf("[gpu] passed over this start: %s%s\n", kApis[i].name,
                   kApis[i].backend == sForced ? " (but $TTYD_GPU asks for it)" : "");
        }
    }
    fflush(stdout);
    writeRecord(BACKEND_AUTO);
    config->backendAttemptCallback = attemptCallback;
    sDone = 0;
}

void portGpuStartFrame(void) {
    AuroraBackend used;
    if (sDone) {
        return;
    }
    sDone = 1;
    used = aurora_get_backend();
    portCrashSetGpuStarting(NULL);
    writeRecord(BACKEND_AUTO); /* (the "starting" line gone: this API draws) */
    printf("[gpu] %s drew the first frame\n", apiName(used));
    if (sNewSkip != 0) {
        char skipped[64] = "";
        size_t i;
        for (i = 0; i < API_COUNT; i++) {
            if (sNewSkip & bit(kApis[i].backend)) {
                snprintf(skipped, sizeof(skipped), "%s", kApis[i].name);
            }
        }
        {
            char text[1024];
            snprintf(text, sizeof(text), "%s",
                     portTrf("Last time, Paper Cut closed while starting its graphics with %s: the graphics driver "
                             "stopped it. This time it uses %s instead, and will from now on.\n\nTo try %s again, "
                             "update your graphics driver (from the maker of your graphics card or PC), then "
                             "change Settings > Graphics > Graphics API.",
                             skipped, apiName(used), skipped));
            printf("[gpu] notice: %s\n", text);
            if (!portOffscreen()) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Paper Cut", text, aurora_get_window());
            }
        }
    }
    fflush(stdout);
}
