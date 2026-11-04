/* Entry point for the Aurora backend: initialize Aurora (window, WebGPU,
 * input, disc image), open the first frame, then run the game's main(). */
#include <aurora/aurora.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_surface.h>
#include <aurora/event.h>
#include <aurora/dvd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/dvd.h>
#include <dolphin/pad.h>
#include <dolphin/gx/GXAurora.h>
#include "port/widescreen.h"
#include <stdbool.h>
#include "port/aurora_pc.h"
#include "port/settings.h"
#include "port/rel_pc.h"
#include "port/platform.h"
#include "port/mods.h"
#include "port/luigi.h"
void portControllersInit(const char* userPath); /* port/settings/features/controllers.cpp */
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
#include "port/ios_pc.h"
#endif
#if defined(__APPLE__) && TARGET_OS_OSX
#include <CoreFoundation/CoreFoundation.h>
#endif
#ifdef __ANDROID__
#include "port/android_pc.h"
#endif

int ttyd_main(void); /* src/main.c, compiled with -Dmain=ttyd_main */

static void logCallback(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
    static const char* const sNames[] = {"DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
    FILE* out = level >= LOG_ERROR ? stderr : stdout;
    fprintf(out, "[%s: %s] %.*s\n", sNames[level], module, (int)len, message);
    if (level == LOG_FATAL) {
        char text[400];
        snprintf(text, sizeof(text), "[%s] %.*s", module, (int)len, message);
        portCrashSetMessage(text); /* into the crash report (crash_pc.c) */
        fflush(stdout); /* the trace and Aurora's warnings before it */
        fflush(out);
        abort();
    }
}

int aurora_main(int argc, char* argv[]) {
    AuroraInfo info;
    const char* pos = getenv("TTYD_WINDOW_POS");
    AuroraConfig config = {
        .appName = "ttyd",
        .logCallback = &logCallback,
        /* MEM1, the game's arena: 128 MiB like the headless backend (os_pc.c),
         * as the PC heaps are sized up for 8-byte pointers (memory.c). */
        .mem1Size = 128 * 1024 * 1024,
        /* ARAM (16 MiB, as on the GameCube): MusyX sample memory, see ar_aurora.c. */
        .mem2Size = 16 * 1024 * 1024,
    };

#ifdef __ANDROID__
    /* logcat, env.txt (Android has no environment to set the switches below) */
    portAndroidEarlyInit();
    pos = getenv("TTYD_WINDOW_POS");
#endif
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
    portIosLoadEnv(); /* Documents/env.txt: the switches below, for debugging on a device */
    /* any way up until the settings are read (their Orientation narrows it,
     * port/src/aurora/layout_aurora.c); Aurora's default is landscape only */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait");
#endif
    /* $TTYD_WINDOW_POS=x,y: e.g. off-screen for unattended runs (map_sweep
     * with captures); rendering and captures go on as usual. */
    /* $TTYD_CACHE_DIR: Aurora's pipeline/shader caches elsewhere than the
     * user's app data (map_sweep gives each run its own). */
    if (getenv("TTYD_CACHE_DIR") != NULL) {
        config.cachePath = getenv("TTYD_CACHE_DIR");
    }
    /* $TTYD_USER_DIR: memory card images and settings elsewhere (map_sweep:
     * parallel runs sharing one card image can block each other). */
    if (getenv("TTYD_USER_DIR") != NULL) {
        config.userPath = getenv("TTYD_USER_DIR");
    }
    /* $TTYD_PAUSE_UNFOCUSED: also pause while another window has the focus
     * (minimized or hidden always pauses, port/src/common/pause_pc.c). */
    config.pauseOnFocusLost = getenv("TTYD_PAUSE_UNFOCUSED") != NULL;
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
    /* iOS: memory cards and settings in Documents/Saves (in the Files app, so
     * .gci saves can be exchanged with Dolphin), caches in Library/Caches (not
     * backed up). Aurora's default for both is Library/Application Support. */
    if (config.userPath == NULL) {
        config.userPath = portIosSavesDir();
    }
    if (config.cachePath == NULL) {
        config.cachePath = portIosCacheDir();
    }
#endif
    /* the window's saved size and place, and full screen from the start
     * (port/settings/features/window.cpp). -1: anywhere, as Aurora takes it
     * (0, 0 put the window in the screen's corner). */
    config.windowPosX = config.windowPosY = -1;
    if (pos != NULL && strchr(pos, ',') != NULL) {
        config.windowPosX = atoi(pos);
        config.windowPosY = atoi(strchr(pos, ',') + 1);
    }
    {
        int full = 0;
        portWindowConfig(config.userPath, &config.windowPosX, &config.windowPosY, &config.windowWidth,
                         &config.windowHeight, &full);
        config.startFullscreen = full != 0;
    }
    /* Texture dumps for pack makers ($TTYD_DUMP_TEXTURES, the settings' Mods
     * tab): Aurora takes the option only at its start. */
    config.allowTextureDumps = getenv("TTYD_DUMP_TEXTURES") != NULL ||
                               portSettingsPeek(config.userPath, "dump_textures", 0) != 0;
    /* 4x anti-aliasing (the msaa setting; $TTYD_MSAA=0/1 overrides): fixed
     * once Aurora is up, so read now and applied from the next start */
    {
        const char* msaa = getenv("TTYD_MSAA");
        config.msaa = (msaa != NULL ? atoi(msaa) : portSettingsPeek(config.userPath, "msaa", 0)) ? 4 : 1;
    }
    portCrashInit();
#if defined(__APPLE__) && TARGET_OS_OSX
    /* No window restoration: after a crash or a killed run, macOS would ask
     * "reopen windows?" in a modal alert before the game starts (blocking
     * unattended runs behind an off-screen window). SDL registers the same
     * default, but only once launching has finished, after that alert. */
    CFPreferencesSetAppValue(CFSTR("ApplePersistenceIgnoreState"), kCFBooleanTrue, kCFPreferencesCurrentApplication);
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
#endif
    info = aurora_initialize(argc, argv, &config);
#ifdef __ANDROID__
    portAndroidInit(); /* crash reports to logcat, background pause */
#endif
    portRelInit(argv[0]);
    /* the port's name and logo on the window (Aurora's appName "ttyd" names
     * the user folder, which keeps the saves), and its fonts for the launcher
     * and the settings page (port/settings/launcher_style.cpp) */
    if (aurora_get_window() != NULL) {
        extern const unsigned char portIcon64[];
        SDL_Surface* icon = SDL_CreateSurfaceFrom(64, 64, SDL_PIXELFORMAT_RGBA32, (void*)portIcon64, 64 * 4);
        SDL_SetWindowTitle(aurora_get_window(), "Paper Cut");
        if (icon != NULL) {
            SDL_SetWindowIcon(aurora_get_window(), icon);
            SDL_DestroySurface(icon);
        }
    }
    portControllersInit(info.userPath); /* every kind of controller: the mappings (controllers.cpp) */
    portUiInit();
    portKeyboardDefaults(); /* before PADInit: the default keys (settings_ui.cpp) */
    /* the controllers and keyboard up now, so the launcher's Settings tab
     * shows the keys and can change them (the game's own PADInit, in
     * DEMOPadInit, then finds it done) */
    PADInit();
    portKeyboardActivate();
    /* The 4:3 picture keeps its shape in any window or screen (bars beside it
     * on a phone, a resized or full-screen desktop window). Aurora starts
     * with FIT as its GX state but sizes the framebuffer to the window until
     * a policy is set, which stretched the picture (1.6x wide on a 19.5:9
     * iPhone). */
    AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);
    portWidescreenInit();
    /* $TTYD_FAST (tests, map_sweep captures): draws wait for their pipeline
     * instead of being skipped while it compiles, so every frame is complete
     * and runs are repeatable. Interactive play: the settings' "no pop-in"
     * (on by default, port/src/aurora/settings_aurora.c). */
    if (getenv("TTYD_FAST") != NULL) {
        AuroraSetDrawsBlocking(true);
    }

    portSettingsLoad(info.userPath); /* settings.txt (port/include/port/settings.h); the launcher shows them */
    portWindowRestore(pos != NULL); /* inside a screen, maximized as it was (window.cpp) */
    /* The user's own disc image (disc_aurora.c): the argument, or the launcher. */
    if (!portAuroraOpenDisc(argc, argv, info.userPath)) {
        aurora_shutdown();
        return 1;
    }

    portLuigiModelInit(); /* Luigi's model with our animations, from the disc (docs/LUIGI_ANIMS.md) */
    portModsInit(info.userPath, info.cachePath); /* the mods folder, loose textures (port/mods/mods.cpp) */
    portPipelineWarmupWait(); /* "Preparing graphics": every known pipeline built first */
    portAuroraBeginFrame();
    return ttyd_main();
}
