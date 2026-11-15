#ifndef PORT_GPU_START_H
#define PORT_GPU_START_H

/* A graphics API that ended the game while it started is passed over at the next start
 * (port/src/aurora/gpu_start_aurora.c; Aurora patch 0080 passes over one whose device creation reported a
 * failure, in the same run).
 *
 * Before Aurora tries each graphics API (Direct3D 12, Vulkan, Direct3D 11, ...) its name goes into
 * <user>/gpu_start.txt as "starting=<api>"; that line goes once the first frame is drawn. A start that finds it
 * there (the last run ended inside the graphics driver while that API started: a crash, a hang ended by the
 * player) adds "skip=<api>": from then on that API is passed over (said in the log, and once in a message box
 * with the next step: update the graphics driver). Choosing another Graphics API in Settings > Graphics tries them
 * all again; so does every API of this platform being on the list. $TTYD_GPU's API is never passed over.
 *
 * A crash while an API starts ends the process with PORT_EXIT_GPU_START (Windows, crash_pc.c), so Paper Cut.exe
 * (port/windows/papercut_start.c) starts the game again, which then passes that API over, and after the last
 * one says plainly what to do instead.
 *
 * Tests: $TTYD_CRASH_TEST=gpu_start crashes while the first API starts (after its line is written);
 * $AURORA_FAIL_BACKEND=d3d12 makes Direct3D 12's start report a device-creation failure. "[gpu] ..." log lines. */

/* The exit code of a crash while a graphics API started (Windows): "GPU" */
#define PORT_EXIT_GPU_START 0x00475055

#ifndef PORT_GPU_START_EXIT_ONLY
#ifdef __cplusplus
extern "C" {
#endif

/* main_aurora.c, just before aurora_initialize: the last start's record read, the callback put in the config
 * (an AuroraConfig*) */
void portGpuStartConfig(void* config);
/* after each aurora_end_frame (the launcher's, Preparing graphics', the game's): the first clears the record */
void portGpuStartFrame(void);

/* crash_pc.c: the API starting now (NULL: none), for the report and the exit code */
void portCrashSetGpuStarting(const char* api);

#ifdef __cplusplus
}
#endif
#endif

#endif
