/* Safe graphics and the graphics API at start (port/include/port/safe_graphics.h), GitHub #39: what has to be
 * chosen before Aurora starts, from settings.txt as it was left (port/settings/features/safe_graphics.cpp holds
 * the settings themselves and what changes while the game runs). */
#include "port/safe_graphics.h"
#include "port/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <aurora/aurora.h>

static int sSafe = -1;

/* an Aurora switch (SDL_getenv) set unless the player or a test set it */
static void auroraDefault(const char* name, const char* value) {
    if (SDL_getenv(name) == NULL) {
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
    }
}

int portSafeGraphicsAtStart(void) {
    return sSafe > 0;
}

void portSafeGraphicsConfig(void* configPtr) {
    AuroraConfig* config = (AuroraConfig*)configPtr;
    const char* env = getenv("TTYD_SAFE_GRAPHICS");
    int api;
    sSafe = env != NULL && env[0] != '\0' ? env[0] != '0' : portSettingsPeek(config->userPath, "safe_graphics", 0) != 0;
    api = portSettingsPeek(config->userPath, "gfx_backend", 0);
#if defined(__ANDROID__)
    /* the graphics API the player chose (Settings > Graphics): Vulkan or OpenGL ES ($TTYD_GPU, read after this,
     * still says); one that can't start falls back to the other (Aurora's order) */
    if (api == 1 || api == 2) {
        config->desiredBackend = api == 1 ? BACKEND_VULKAN : BACKEND_OPENGLES;
        printf("[gfx] the graphics API chosen in the settings: %s\n", api == 1 ? "Vulkan" : "OpenGL ES");
    }
    /* A phone's GPU compiles pipelines slowly (Adreno: tenths of a second each): a frame holds for those it
     * waits on at most a tenth of a second rather than a whole one (a new room at 1 fps), the rest drawn as
     * they are ready */
    if (getenv("TTYD_FAST") == NULL) {
        auroraDefault("AURORA_BLOCKING_BUDGET_MS", "100");
    }
#elif defined(_WIN32)
    /* Windows: Direct3D 12 (Aurora's first), Vulkan or Direct3D 11, the player's choice (Settings > Graphics); an
     * old or Intel driver's Direct3D 12 can go white or stop where its Direct3D 11 plays (a report on an Intel
     * UHD 610). Safe graphics with the API on Auto starts on Direct3D 11. One that can't start falls back to the
     * others (Aurora's order); $TTYD_GPU, read after this, still says */
    {
        static const AuroraBackend kBackends[] = {BACKEND_D3D12, BACKEND_VULKAN, BACKEND_D3D11};
        static const char* const kNames[] = {"Direct3D 12", "Vulkan", "Direct3D 11"};
        if (api == 0 && sSafe) {
            api = 3;
        }
        if (api >= 1 && api <= 3) {
            config->desiredBackend = kBackends[api - 1];
            printf("[gfx] the graphics API %s: %s\n", sSafe && portSettingsPeek(config->userPath, "gfx_backend", 0) == 0
                                                            ? "for Safe graphics"
                                                            : "chosen in the settings",
                   kNames[api - 1]);
        }
    }
#else
    (void)api;
#endif
    if (!sSafe) {
        return;
    }
    /* Safe graphics: what the GPU is asked for kept to the least, from the start */
    config->msaa = 1;
    if (getenv("TTYD_LOW_MEMORY") == NULL) {
        SDL_setenv_unsafe("TTYD_LOW_MEMORY", "1", 1); /* the smaller graphics buffers (handheld_aurora.c) */
    }
    auroraDefault("AURORA_LOW_LIMITS", "1");          /* textures up to 8192, as an older GPU's */
    auroraDefault("AURORA_NO_IMMEDIATES", "1");       /* per-draw values in a uniform binding (older Adreno's path) */
    auroraDefault("AURORA_BLOCKING_BUDGET_MS", "100"); /* no long holds for pipelines */
    printf("[gfx] safe graphics: no anti-aliasing, smaller graphics buffers, lower texture limits, per-draw values "
           "in a uniform binding, short pipeline waits; for the run also no supersampling, upscaling or effects\n");
    fflush(stdout);
}
