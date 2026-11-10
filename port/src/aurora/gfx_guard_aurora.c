/* The graphics guard (port/include/port/gfx_guard.h), GitHub #39: Aurora's resources watched each
 * presented frame, and a frame rate that collapses stepped down to safer settings for the run. */
#include "port/gfx_guard.h"
#include "port/companion_menu.h"
#include "port/offscreen.h"
#include "port/savestate.h"
#include "port/settings.h"
#include "port/tr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

int portLowMemoryGfx(void); /* handheld_aurora.c */

/* --- the ceilings --- */

/* Textures alive, in MB: a phone's or tablet's GPU shares the CPU's memory (and a driver that runs out
 * resets the whole device), a computer's has its own */
static unsigned long long textureCeilingMB(void) {
    const char* v = getenv("TTYD_GFX_GUARD_TEXTURE_MB");
    if (v != NULL && atoi(v) > 0) {
        return (unsigned long long)atoi(v);
    }
#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    return 1024;
#else
    return portLowMemoryGfx() ? 768 : 3072;
#endif
}

/* far past anything the game draws in a frame (its busiest scenes: a few dozen passes and copies) */
#define MAX_PASSES 1024
#define MAX_COPIES 1024
#define MAX_READBACKS 256
#define MAX_OVERFLOW_UPLOADS 256
#define MAX_TEXTURES 60000
#define MAX_BIND_GROUPS 200000
#define MAX_SAMPLERS 4096
/* textures that grow every sample (each GROWTH_FRAMES) for GROWTH_SAMPLES samples on end, by at least
 * GROWTH_MB in all: a leak, not a scene loading */
#define GROWTH_FRAMES 300
#define GROWTH_SAMPLES 6
#define GROWTH_MB 64ull
/* a flush at most this often; one that left the level over a ceiling doubles the gap (up to the most), back to
 * the least once the level is under */
#define FLUSH_GAP_MS 5000u
#define FLUSH_GAP_MAX_MS 300000u

static unsigned long long sFrames;
static Uint64 sLastFlush;
static Uint64 sFlushGap = FLUSH_GAP_MS;
static int sFlushes;
static unsigned long long sGrowthLast;
static int sGrowthRun;
static unsigned long long sGrowthFrom;
static int sStatsEvery = -1;
/* the most seen (the log's last line and the scenarios' checks) */
static AuroraResourceStats sPeak;

static void peak(const AuroraResourceStats* s) {
#define PEAK(f)                                                                                                       \
    if (s->f > sPeak.f) {                                                                                             \
        sPeak.f = s->f;                                                                                               \
    }
    PEAK(renderPasses)
    PEAK(textureCopies)
    PEAK(readbacks)
    PEAK(textureUploads)
    PEAK(overflowUploads)
    PEAK(textures)
    PEAK(textureBytes)
    PEAK(bindGroups)
    PEAK(samplers)
    PEAK(textureObjects)
    PEAK(contentEntries)
    PEAK(contentBytes)
    PEAK(copyEntries)
    PEAK(copyBytes)
    PEAK(offscreenEntries)
    PEAK(passSnapshots)
#undef PEAK
}

static void statsLine(const char* tag, const AuroraResourceStats* s) {
    const AuroraStats* a = aurora_get_stats();
    printf("[gfxstats] %s %llu: draws %u passes %u copies %u readbacks %u uploads %u (%u KB, %u own buffers), "
           "textures %u (%llu MB), bind groups %u, samplers %u, texture objects %u, content %u (%llu MB), "
           "copies %u (%llu MB, %llu MB current), offscreen %u, snapshots %u, pipelines %u (%u queued)\n",
           tag, sFrames, a != NULL ? a->drawCallCount : 0, s->renderPasses, s->textureCopies, s->readbacks,
           s->textureUploads, a != NULL ? a->lastTextureUploadSize >> 10 : 0, s->overflowUploads, s->textures,
           (unsigned long long)(s->textureBytes >> 20), s->bindGroups, s->samplers, s->textureObjects,
           s->contentEntries, (unsigned long long)(s->contentBytes >> 20), s->copyEntries,
           (unsigned long long)(s->copyBytes >> 20), (unsigned long long)(s->copyPinnedBytes >> 20),
           s->offscreenEntries, s->passSnapshots, a != NULL ? a->createdPipelines : 0,
           a != NULL ? a->queuedPipelines : 0);
    fflush(stdout);
}

/* "[gfx] guard: ..." once per kind (the log is not flooded), the decoded textures' cache flushed when that can
 * help (only it: the EFB copies the game still samples stay, and the content and copy caches keep their own
 * budgets) */
static void guard(int kind, int flush, const char* what, unsigned long long value, unsigned long long limit,
                  const AuroraResourceStats* s) {
    static unsigned sSaid;
    const Uint64 now = SDL_GetTicks();
    if (!(sSaid & (1u << kind))) {
        sSaid |= 1u << kind;
        printf("[gfx] guard: %s %llu, over %llu (frame %llu)%s\n", what, value, limit, sFrames,
               flush ? ": Aurora's texture cache flushed" : "");
        statsLine("at", s);
    }
    if (flush && (sLastFlush == 0 || now - sLastFlush >= sFlushGap)) {
        if (sLastFlush != 0 && sFlushGap < FLUSH_GAP_MAX_MS) {
            sFlushGap *= 2; /* still over since the last one: it doesn't help, so less often */
        }
        sLastFlush = now;
        sFlushes++;
        AuroraFlushStaticTextureCaches(); /* (at the next frame's start) */
    }
}

static void watchResources(void) {
    AuroraResourceStats s;
    unsigned long long mb;
    memset(&s, 0, sizeof(s));
    AuroraGetResourceStats(&s);
    peak(&s);
    if (sStatsEvery < 0) {
        const char* v = getenv("TTYD_GFX_STATS");
        sStatsEvery = v != NULL ? atoi(v) : 0;
        sStatsEvery = sStatsEvery == 1 ? 60 : sStatsEvery < 0 ? 0 : sStatsEvery;
    }
    if (sStatsEvery > 0 && sFrames % (unsigned long long)sStatsEvery == 0) {
        statsLine("frame", &s);
        printf("[gfxstats] peak: passes %u copies %u readbacks %u uploads %u own buffers %u textures %u (%llu MB) "
               "bind groups %u samplers %u texture objects %u copies %u (%llu MB) offscreen %u snapshots %u\n",
               sPeak.renderPasses, sPeak.textureCopies, sPeak.readbacks, sPeak.textureUploads, sPeak.overflowUploads,
               sPeak.textures, (unsigned long long)(sPeak.textureBytes >> 20), sPeak.bindGroups, sPeak.samplers,
               sPeak.textureObjects, sPeak.copyEntries, (unsigned long long)(sPeak.copyBytes >> 20),
               sPeak.offscreenEntries, sPeak.passSnapshots);
        fflush(stdout);
    }
    /* per frame: nothing to free, only told (a pass or copy without end is the game's or a feature's loop) */
    if (s.renderPasses > MAX_PASSES) {
        guard(0, 0, "render passes in a frame", s.renderPasses, MAX_PASSES, &s);
    }
    if (s.textureCopies > MAX_COPIES) {
        guard(1, 0, "texture copies in a frame", s.textureCopies, MAX_COPIES, &s);
    }
    if (s.readbacks > MAX_READBACKS) {
        guard(2, 0, "readbacks in a frame", s.readbacks, MAX_READBACKS, &s);
    }
    if (s.overflowUploads > MAX_OVERFLOW_UPLOADS) {
        guard(3, 0, "texture uploads with a buffer of their own in a frame", s.overflowUploads,
              MAX_OVERFLOW_UPLOADS, &s);
    }
    if (s.bindGroups > MAX_BIND_GROUPS) {
        guard(4, 0, "cached bind groups", s.bindGroups, MAX_BIND_GROUPS, &s);
    }
    if (s.samplers > MAX_SAMPLERS) {
        guard(5, 0, "cached samplers", s.samplers, MAX_SAMPLERS, &s);
    }
    /* alive: the caches let go, which the textures are mostly held by */
    mb = (unsigned long long)(s.textureBytes >> 20);
    if (mb > textureCeilingMB()) {
        guard(6, 1, "MB of textures alive", mb, textureCeilingMB(), &s);
    }
    if (s.textures > MAX_TEXTURES) {
        guard(7, 1, "textures alive", s.textures, MAX_TEXTURES, &s);
    }
    if (mb <= textureCeilingMB() && s.textures <= MAX_TEXTURES) {
        sFlushGap = FLUSH_GAP_MS; /* (under again: the next flush, if any, as soon as needed) */
        sLastFlush = 0;
    }
    if (sFrames % GROWTH_FRAMES == 0) {
        if (s.textureBytes > sGrowthLast && sGrowthLast != 0) {
            if (sGrowthRun++ == 0) {
                sGrowthFrom = sGrowthLast;
            }
            if (sGrowthRun >= GROWTH_SAMPLES && s.textureBytes - sGrowthFrom >= (GROWTH_MB << 20)) {
                guard(8, 1, "MB of textures, growing for 30 s on end, by", (s.textureBytes - sGrowthFrom) >> 20,
                      GROWTH_MB, &s);
                sGrowthRun = 0;
            }
        } else {
            sGrowthRun = 0;
        }
        sGrowthLast = s.textureBytes;
    }
}

/* --- the watchdog --- */

static int sStep;
static int sSlow;

int portGfxGuardStep(void) {
    return sStep;
}

/* a declared setting set for this run, settings.txt keeping the player's */
static int offForRun(const char* key, int safe) {
    int* v = portSettingsFind(key);
    if (v == NULL || *v == safe) {
        return 0;
    }
    portSettingsExtraSaveAs(key, 1, *v);
    *v = safe;
    return 1;
}

static void stepDown(float intervalMs) {
    char changed[256];
    PortSettings* ps = portSettings();
    changed[0] = '\0';
#define NOTE(s) snprintf(changed + strlen(changed), sizeof(changed) - strlen(changed), "%s%s", changed[0] ? ", " : "", s)
    sStep++;
    if (sStep == 1) {
        if (offForRun("supersample", 0)) {
            NOTE("supersampling");
        }
        if (offForRun("rt_lighting", 0)) {
            NOTE("ray-traced lighting");
        }
        if (offForRun("paper_shadows", 0)) {
            NOTE("paper shadows");
        }
        if (offForRun("paper_edges", 0)) {
            NOTE("paper edges");
        }
        if (offForRun("bloom", 0)) {
            NOTE("bloom");
        }
        if (offForRun("post_filter", 0)) {
            NOTE("post filter");
        }
        if (offForRun("stereo_3d", 0)) {
            NOTE("stereoscopic 3D");
        }
        if (strcmp(ps->framerate, "60") != 0) {
            portSettingsSetForRun("framerate", "60");
            NOTE("in-between frames");
        }
        if (ps->noPopIn) {
            portSettingsSetForRun("no_pop_in", "0");
            NOTE("waiting for pipelines");
        }
        if (portCompanionMenuSuspend(1)) {
            NOTE("the second screen's live menu");
        }
    } else {
        if (ps->resolution != 1) {
            portSettingsSetForRun("resolution", "1");
            NOTE("the picture at 480 lines");
        }
    }
#undef NOTE
    portSettingsApply();
    printf("[gfx] watchdog: %d frames on end over %.0f ms apart (the last %.0f ms): step %d, for this run: %s\n",
           PORT_GFX_SLOW_FRAMES, (double)PORT_GFX_SLOW_MS, (double)intervalMs, sStep,
           changed[0] ? changed : "nothing left to turn down");
    fflush(stdout);
    if (changed[0]) {
        portSaveStateNotice(portTr("Graphics slowed right down: safer settings for this session"));
    }
}

/* Present gaps say something only while the window is seen: hidden, minimized or occluded (macOS, another
 * workspace on Wayland), it keeps running (pause_unfocused is off by default) but its presents block or wait,
 * and the run must not step down for that. A window without the focus but on screen still counts (a
 * dual-screen handheld's other screen takes the focus while this one plays). Offscreen (tests) always counts. */
static int presentsCount(void) {
    SDL_Window* w;
    if (portOffscreen()) {
        return 1;
    }
    w = aurora_get_window();
    return w == NULL || (SDL_GetWindowFlags(w) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) == 0;
}

static void watchFrameTime(float intervalMs) {
    static int sOn = -1;
    static float sFake;
    if (sOn < 0) {
        const char* v = getenv("TTYD_GFX_WATCHDOG");
        const char* fake = getenv("TTYD_GFX_WATCHDOG_FAKE");
        /* tests ($TTYD_FAST) only when asked: a loaded test machine must not change what they capture */
        sOn = v != NULL && v[0] != '\0' ? v[0] != '0' : getenv("TTYD_FAST") == NULL && !portCleanFrame();
        sFake = fake != NULL ? (float)atof(fake) : 0.0f;
        if (sOn && sFake > 0.0f) {
            printf("[gfx] watchdog: every interval seen %.0f ms longer (tests)\n", (double)sFake);
        }
    }
    if (!sOn || sStep >= 2) {
        return;
    }
    if (!presentsCount()) {
        sSlow = 0;
        return;
    }
    intervalMs += sFake;
    if (intervalMs > PORT_GFX_SLOW_MS) {
        if (++sSlow >= PORT_GFX_SLOW_FRAMES) {
            sSlow = 0;
            stepDown(intervalMs);
        }
    } else {
        sSlow = 0;
    }
}

void portGfxGuardFrame(float intervalMs, float workMs) {
    (void)workMs;
    sFrames++;
    watchResources();
    watchFrameTime(intervalMs);
}
