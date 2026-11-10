/* Linux ARM64 handhelds: found, configured and given their preset
 * (port/include/port/handheld.h; docs/HANDHELDS.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <aurora/aurora.h>

#include "port/handheld.h"
#include "port/resume.h"
#include "port/settings.h"
#include "port/widescreen.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/sysinfo.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

/* The chips of the handhelds PortMaster runs on (their device tree's
 * "compatible" strings): Anbernic's RG35XX/RG40XX/RG CubeXX/RG34XX (H700),
 * the RG353 family and Powkiddy's (RK3566), the R36S and its kin (RK3326),
 * the Retroid and Odroid Go Ultra kind (RK3588, S922X), H616/H618 boards. */
static const char* const kChips[] = {
    "allwinner,sun50i-h700", "allwinner,sun50i-h616", "allwinner,sun50i-h618", "rockchip,rk3326",
    "rockchip,rk3566",       "rockchip,rk3568",       "rockchip,rk3588",       "rockchip,rk3588s",
    "amlogic,s922x",         "amlogic,g12b",
};

static int detect(void) {
    const char* env = getenv("TTYD_HANDHELD");
    if (env != NULL && env[0] != '\0') {
        return env[0] != '0';
    }
#if defined(__linux__) && defined(__aarch64__) && !defined(__ANDROID__)
    {
        /* NUL-separated strings */
        char buf[1024];
        size_t n, i, j;
        FILE* f = fopen("/proc/device-tree/compatible", "rb");
        if (f == NULL) {
            return 0;
        }
        n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';
        for (i = 0; i < n; i += strlen(buf + i) + 1) {
            for (j = 0; j < sizeof(kChips) / sizeof(kChips[0]); j++) {
                if (strcmp(buf + i, kChips[j]) == 0) {
                    return 1;
                }
            }
        }
    }
#endif
    return 0;
}

int portHandheld(void) {
    static int sFound = -1;
    if (sFound < 0) {
        sFound = detect();
    }
    return sFound;
}

/* the machine's memory in MiB (0: unknown): Linux's own count, SDL's on Windows and macOS; phones and
 * tablets keep 0 (their own defaults and Aurora's mobile budgets) */
static unsigned long memoryMiB(void) {
#if defined(__linux__) && !defined(__ANDROID__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (unsigned long)((unsigned long long)info.totalram * info.mem_unit >> 20);
    }
#elif defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_OSX)
    const int mib = SDL_GetSystemRAM();
    return mib > 0 ? (unsigned long)mib : 0;
#endif
    return 0;
}

static int sLowMemoryGfx, sLowEndPc;

int portLowMemoryGfx(void) {
    return sLowMemoryGfx;
}

int portLowEndPc(void) {
    return sLowEndPc;
}

/* A weak computer (a community report: a 2-core Pentium with 4 GB and Intel UHD 610): under 6 GB of memory,
 * or 4 threads or fewer. $TTYD_LOW_END=0/1 says. Never a phone or tablet (their own defaults). */
static int lowEndPc(unsigned long mib) {
    const char* env = getenv("TTYD_LOW_END");
    if (env != NULL && env[0] != '\0') {
        return env[0] != '0';
    }
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    (void)mib;
    return 0;
#else
    {
        const int threads = SDL_GetNumLogicalCPUCores();
        return (mib != 0 && mib < 6144) || (threads > 0 && threads <= 4);
    }
#endif
}

void portHandheldConfig(void* configPtr) {
    AuroraConfig* config = (AuroraConfig*)configPtr;
    const unsigned long mib = memoryMiB();
    /* the smaller graphics buffers: on a handheld, and wherever memory is short ($TTYD_LOW_MEMORY=0/1 says) */
    const char* low = getenv("TTYD_LOW_MEMORY");
    /* (under 6 GB on a computer too: a 4 GB one's integrated GPU shares that memory with the system and the
     * game, and the normal staging buffers alone are about 700 MiB) */
    config->lowMemoryGfx = low != NULL ? low[0] != '0' : portHandheld() || (mib != 0 && mib < 6144);
    sLowMemoryGfx = config->lowMemoryGfx;
    sLowEndPc = !portHandheld() && lowEndPc(mib);
    if (sLowEndPc || sLowMemoryGfx) {
        printf("[gfx] a low-end computer (%lu MiB, %d threads)%s%s\n", mib, SDL_GetNumLogicalCPUCores(),
               sLowMemoryGfx ? ", the smaller graphics buffers" : "",
               sLowEndPc ? ", pipelines built in the background unless no_pop_in is chosen" : "");
        fflush(stdout);
    }
    if (!portHandheld()) {
        return;
    }
    printf("[handheld] a handheld (%lu MiB): OpenGL ES, smaller graphics buffers%s\n", mib,
           config->lowMemoryGfx ? "" : " off");
    /* OpenGL ES: these chips' Vulkan (Panfrost's PanVK, the vendors' blobs) is missing or young;
     * $TTYD_GPU (main_aurora.c, read after this) still chooses */
    config->desiredBackend = BACKEND_OPENGLES;
    config->msaa = 1;
    config->startFullscreen = true;
}

static void setKey(const char* key, int value) {
    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    portSettingsSetKey(key, text);
}

void portHandheldPreset(void) {
    static const struct {
        const char* key;
        int value;
    } kPreset[] = {
        {"msaa", 0},          {"supersample", 0},     {"post_filter", 0},  {"aniso", 0},
        {"texture_upscale", 0}, {"bloom", 0},         {"hdr", 0},          {"paper_shadows", 0},
        {"paper_edges", 0},   {"paper_physics", 0},   {"rewind", 0},       {"side_bars", 0},
        {"resolution", 1},    /* 640x480 inside: the screen's own lines */
        {"battery_saver", 2}, /* always: drawn at 30 frames a second (the game still runs at 60) */
        {"vsync", 1},         {"fullscreen", 2},
    };
    int* done = portSettingsDeclare("handheld_preset", 0, 0, 1);
    size_t i;
    if (!portHandheld() || *done) {
        return;
    }
    for (i = 0; i < sizeof(kPreset) / sizeof(kPreset[0]); i++) {
        setKey(kPreset[i].key, kPreset[i].value);
    }
    portSettingsSetKey("framerate", "60"); /* no in-between frames */
    {
        /* the screen's shape: 4:3 (640x480) as the game is; wider ones (3:2 at 720x480, 16:9) at their own
         * aspect, not 16:9's bars */
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
        const float aspect = mode != NULL && mode->h > 0 ? (float)mode->w / (float)mode->h : 4.f / 3.f;
        const int wide = aspect > 1.4f;
        setKey("widescreen", wide);
        setKey("aspect_ratio", wide ? PORT_ASPECT_WINDOW : PORT_ASPECT_4_3);
        printf("[handheld] the Handheld preset: %dx%d, %s\n", mode != NULL ? mode->w : 0, mode != NULL ? mode->h : 0,
               wide ? "the screen's own aspect" : "4:3");
    }
    *done = 1;
    portSettingsApply();
    portSettingsSave();
}

/* Select + Start: held together, a clean quit (PortMaster's exit hotkey, without killing the game) */
static int sBack, sStart;

void portHandheldEvent(const void* sdlEvent) {
    const SDL_Event* event = (const SDL_Event*)sdlEvent;
    if (!portHandheld()) {
        return;
    }
    switch (event->type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const int down = event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (event->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
            sBack = down;
        } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_START) {
            sStart = down;
        } else {
            break;
        }
        if (down && sBack && sStart) {
            SDL_Event quit;
            SDL_zero(quit);
            quit.type = SDL_EVENT_QUIT;
            printf("[handheld] Select + Start: quitting\n");
            portResumeFlush();
            portSettingsSave();
            SDL_PushEvent(&quit); /* Aurora's exit, as closing the window (vi_aurora.c) */
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN:
        /* the power or sleep key (the firmware sleeps or ends us next): where the player is, saved now */
        if (event->key.scancode == SDL_SCANCODE_POWER || event->key.scancode == SDL_SCANCODE_SLEEP) {
            printf("[handheld] the power key: saving\n");
            portResumeFlush();
            portSettingsSave();
        }
        break;
    default:
        break;
    }
}
