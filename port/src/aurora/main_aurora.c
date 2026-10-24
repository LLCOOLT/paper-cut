/* Entry point for the Aurora backend: initialize Aurora (window, WebGPU,
 * input, disc image), open the first frame, then run the game's main(). */
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/dvd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/dvd.h>
#include <dolphin/gx/GXAurora.h>
#include <stdbool.h>
#include "port/aurora_pc.h"
#include "port/rel_pc.h"
#include "port/platform.h"

int ttyd_main(void); /* src/main.c, compiled with -Dmain=ttyd_main */

static void logCallback(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
    static const char* const sNames[] = {"DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
    FILE* out = level >= LOG_ERROR ? stderr : stdout;
    fprintf(out, "[%s: %s] %.*s\n", sNames[level], module, (int)len, message);
    if (level == LOG_FATAL) {
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
    if (pos != NULL && strchr(pos, ',') != NULL) {
        config.windowPosX = atoi(pos);
        config.windowPosY = atoi(strchr(pos, ',') + 1);
    }
    portCrashInit();
    info = aurora_initialize(argc, argv, &config);
    portRelInit(argv[0]);
    /* $TTYD_FAST (tests, map_sweep captures): draws wait for their pipeline
     * instead of being skipped while it compiles, so every frame is complete
     * and runs are repeatable. Interactive play keeps Aurora's async default. */
    if (getenv("TTYD_FAST") != NULL) {
        AuroraSetDrawsBlocking(true);
    }

    /* The user's own disc image (disc_aurora.c). */
    if (!portAuroraOpenDisc(argc, argv, info.userPath)) {
        aurora_shutdown();
        return 1;
    }

    portAuroraBeginFrame();
    return ttyd_main();
}
