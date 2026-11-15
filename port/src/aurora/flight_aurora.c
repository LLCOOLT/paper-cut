/* The flight recorder (port/include/port/flight.h), GitHub #39: what the run was doing, kept on storage as it
 * goes, so a device that resets under the game still leaves it. */
#include "port/flight.h"
#include "port/soft_crash.h"
#include "port/build_id.h"
#include "port/offscreen.h"
#include "port/platform.h"
#include "port/safe_graphics.h"
#include "port/settings.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>

#include "driver/seqdrv.h"
#include "mario/mariost.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <wchar.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#define FLIGHT_PHONE 1
#else
#define FLIGHT_PHONE 0
#endif

int portLowMemoryGfx(void); /* handheld_aurora.c */

#define EVENTS 24
#define EVENT_LEN 200
#define SECONDS 30
#define SECOND_LEN 300
#define RECORD_MAX 16384
#define SYNC_MS (FLIGHT_PHONE ? 1000u : 5000u)

static int sOn;
static char sPath[2][1100];
static char sLast[1100];
static int sHaveLast, sStopped;

/* shared with the writer thread (sLock) */
static SDL_Mutex* sLock;
static SDL_Condition* sWake;
static SDL_Condition* sWrote;
static SDL_Thread* sThread;
static int sWant;
static unsigned long long sSeq, sWrittenSeq;
static const char* sEnd;   /* "a clean exit": the last record, nothing after it */
static int sDone;          /* the end's record written */
static char sEvents[EVENTS][EVENT_LEN];
static unsigned sEventNext;
static char sSeconds[SECONDS][SECOND_LEN];
static unsigned sSecondNext;
static char sSystem[240];
static char sAdapter[400];
static char sCaps[600];
static char sFallbacks[300];
static char sSettingsLine[400];
static int sBackground;
static int sHidden;
static unsigned long long sFrames;

/* the game thread's own */
static Uint64 sLastSecond, sLastSync, sLastCall;
static unsigned sCount;
static double sSum, sWorst, sWork;
static char sRoom[16];
static int sSeqWas = -1;
static unsigned sPipelinesWas;
static unsigned long long sKillAt;
static int sWatching;

/* --- the files --- */

/* text written whole and synced to storage: what survives a reset of the device */
static int writeSynced(const char* path, const char* text, size_t size) {
#if defined(_WIN32)
    /* (the path as UTF-16 for _wopen: a user folder with letters outside the code page; _commit is
     * FlushFileBuffers) */
    wchar_t* wide = (wchar_t*)SDL_iconv_string("UTF-16LE", "UTF-8", path, strlen(path) + 1);
    int fd, ok;
    if (wide == NULL) {
        return 0;
    }
    fd = _wopen(wide, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
    SDL_free(wide);
    if (fd < 0) {
        return 0;
    }
    ok = _write(fd, text, (unsigned int)size) == (int)size && _commit(fd) == 0;
    _close(fd);
    return ok;
#else
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    size_t at = 0;
    int ok = 1;
    if (fd < 0) {
        return 0;
    }
    while (at < size) {
        const ssize_t n = write(fd, text + at, size - at);
        if (n <= 0) {
            ok = 0;
            break;
        }
        at += (size_t)n;
    }
#if defined(__APPLE__)
    ok = ok && fsync(fd) == 0; /* (no fdatasync on Apple's systems) */
#else
    ok = ok && fdatasync(fd) == 0;
#endif
    close(fd);
    return ok;
#endif
}

static const char* settingValue(const char* key, char* out, size_t size) {
    const int* v = portSettingsFind(key);
    int raw;
    if (v != NULL) {
        snprintf(out, size, "%d", *v);
    } else if (portSettingsExtraRaw(key, &raw)) {
        snprintf(out, size, "%d", raw);
    } else {
        snprintf(out, size, "-");
    }
    return out;
}

static const char* backendName(AuroraBackend b) {
    switch (b) {
    case BACKEND_D3D11: return "Direct3D 11";
    case BACKEND_D3D12: return "Direct3D 12";
    case BACKEND_METAL: return "Metal";
    case BACKEND_VULKAN: return "Vulkan";
    case BACKEND_OPENGL: return "OpenGL";
    case BACKEND_OPENGLES: return "OpenGL ES";
    case BACKEND_WEBGPU: return "WebGPU";
    case BACKEND_NULL: return "none";
    default: return "not started";
    }
}

/* the record as it stands (sLock held) */
static size_t buildRecord(char* out, size_t max, unsigned long long seq) {
    size_t n = 0;
    unsigned i, first;
#define PUT(...)                                                                                                    \
    do {                                                                                                          \
        if (n < max) {                                                                                            \
            const int w = snprintf(out + n, max - n, __VA_ARGS__);                                                \
            n += w > 0 ? (size_t)w : 0;                                                                           \
            n = n < max ? n : max - 1;                                                                            \
        }                                                                                                         \
    } while (0)
    PUT("# Paper Cut's flight record: the last moments of a run, kept on storage as it went (newest last).\n"
        "# Share it with whoever helps you (Settings > General > Crash reports copies it).\n");
    PUT("seq %llu\n", seq);
    PUT("version: Paper Cut %s, %s\n", portBuildVersion(), PORT_REGION_SHORT);
    PUT("system: %s\n", sSystem);
    PUT("graphics: %s%s%s\n", backendName(aurora_get_backend()), sAdapter[0] ? "; " : "", sAdapter);
    if (sFallbacks[0]) {
        PUT("%s\n", sFallbacks);
    }
    if (sCaps[0]) {
        PUT("%s\n", sCaps);
    }
    PUT("settings: %s\n", sSettingsLine);
    PUT("state: %s, %llu frames, %llu s\n", sBackground ? "in the background" : sHidden ? "hidden" : "on screen", sFrames,
        (unsigned long long)(SDL_GetTicks() / 1000));
    PUT("--- events\n");
    first = sEventNext > EVENTS ? sEventNext - EVENTS : 0;
    for (i = first; i < sEventNext; i++) {
        PUT("%s\n", sEvents[i % EVENTS]);
    }
    PUT("--- seconds\n");
    first = sSecondNext > SECONDS ? sSecondNext - SECONDS : 0;
    for (i = first; i < sSecondNext; i++) {
        PUT("%s\n", sSeconds[i % SECONDS]);
    }
    if (sEnd != NULL) {
        PUT("end: %s\n", sEnd);
    }
    PUT("#end %llu\n", seq);
#undef PUT
    return n;
}

static int SDLCALL writer(void* data) {
    static char text[RECORD_MAX];
    (void)data;
    SDL_LockMutex(sLock);
    for (;;) {
        unsigned long long seq;
        size_t size;
        int last;
        while (!sWant) {
            SDL_WaitCondition(sWake, sLock);
        }
        sWant = 0;
        seq = ++sSeq;
        size = buildRecord(text, sizeof(text), seq);
        last = sEnd != NULL;
        SDL_UnlockMutex(sLock);
        writeSynced(sPath[seq & 1], text, size);
        SDL_LockMutex(sLock);
        sWrittenSeq = seq;
        SDL_BroadcastCondition(sWrote);
        if (last) {
            sDone = 1;
            break;
        }
        SDL_UnlockMutex(sLock);
        SDL_Delay(50); /* (a burst of events: one write for them all) */
        SDL_LockMutex(sLock);
    }
    SDL_UnlockMutex(sLock);
    return 0;
}

/* a write asked for (sLock held) */
static void want(void) {
    if (!sDone) {
        sWant = 1;
        SDL_SignalCondition(sWake);
    }
}

/* written and synced before returning (the end; a test's kill): at most a short wait */
static void writeNow(void) {
    unsigned long long target;
    const Uint64 until = SDL_GetTicks() + 1500;
    SDL_LockMutex(sLock);
    target = sSeq + 1;
    want();
    while (sWrittenSeq < target && !sDone && SDL_GetTicks() < until) {
        SDL_WaitConditionTimeout(sWrote, sLock, 100);
    }
    SDL_UnlockMutex(sLock);
}

/* --- the last run's --- */

/* a whole record ("#end <seq>" its last line, the same as its "seq" line): its seq, else 0 */
static unsigned long long wholeSeq(const char* text, size_t size) {
    const char* seqLine = strstr(text, "\nseq ");
    const char* end;
    unsigned long long seq;
    if (seqLine == NULL || size < 8) {
        return 0;
    }
    seq = strtoull(seqLine + 5, NULL, 10);
    end = text + size - 1;
    while (end > text && (*end == '\n' || *end == '\r')) {
        end--;
    }
    while (end > text && end[-1] != '\n') {
        end--;
    }
    return strncmp(end, "#end ", 5) == 0 && strtoull(end + 5, NULL, 10) == seq ? seq : 0;
}

static void logTail(const char* text) {
    /* the last events and seconds, for the log */
    const char* events = strstr(text, "\n--- events\n");
    const char* line = events != NULL ? events + 1 : text;
    while (*line != '\0') {
        const size_t len = strcspn(line, "\n");
        if (len > 0 && line[0] != '#') {
            printf("[flight] record: %.*s\n", (int)len, line);
        }
        line += len;
        line += *line == '\n';
    }
}

static long long sEndedAt; /* when the last run's record was last written (portFlightEndedAt) */

long long portFlightEndedAt(void) {
    return sEndedAt;
}

static void lookAtLastRun(void) {
    char* best = NULL;
    size_t bestSize = 0;
    unsigned long long bestSeq = 0;
    int i;
    for (i = 0; i < 2; i++) {
        size_t size = 0;
        char* text = (char*)SDL_LoadFile(sPath[i], &size);
        const unsigned long long seq = text != NULL ? wholeSeq(text, size) : 0;
        if (seq > bestSeq) {
            SDL_PathInfo info;
            SDL_free(best);
            best = text;
            bestSize = size;
            bestSeq = seq;
            /* (its last write: synced each second while on screen, so within a second or so of the end) */
            sEndedAt = SDL_GetPathInfo(sPath[i], &info) ? (long long)(info.modify_time / SDL_NS_PER_SECOND) : 0;
        } else {
            SDL_free(text);
        }
        SDL_RemovePath(sPath[i]);
    }
    if (best == NULL) {
        sHaveLast = SDL_GetPathInfo(sLast, NULL);
        return; /* (a first start, or the recorder was off: an older record kept as it was) */
    }
    sHaveLast = SDL_SaveFile(sLast, best, bestSize);
    if (strstr(best, "\nend: ") != NULL) {
        printf("[flight] the last run ended cleanly\n");
    } else if (strstr(best, "\nstate: on screen") == NULL) {
        printf("[flight] the last run ended in the background or hidden: nothing to say\n");
    } else if (portCrashReportFound()) {
        printf("[flight] the last run crashed: its crash report says more\n");
    } else {
        sStopped = 1;
        printf("[flight] the last run stopped while on screen (no clean exit, no crash report: the device "
               "or its graphics driver ended it); its record is flight_record_last.txt:\n");
        logTail(best);
    }
    fflush(stdout);
    SDL_free(best);
}

/* --- events --- */

static void event(const char* text) {
    char* line;
    size_t i;
    if (!sOn) {
        return;
    }
    SDL_LockMutex(sLock);
    line = sEvents[sEventNext++ % EVENTS];
    snprintf(line, EVENT_LEN, "%7.1f s  %s", (double)SDL_GetTicks() / 1000.0, text);
    for (i = 0; line[i] != '\0'; i++) {
        if (line[i] == '\n' || line[i] == '\r') {
            line[i] = ' ';
        }
    }
    want();
    SDL_UnlockMutex(sLock);
}

void portFlightMark(const char* format, ...) {
    char text[EVENT_LEN];
    va_list args;
    if (!sOn) {
        return;
    }
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    event(text);
}

/* "Graphics adapter information\n  API: ...\n  Device: ...\n  Driver: ..." as one line: a line break "; ", other
 * runs of spaces one space */
static void oneLine(char* out, size_t size, const char* message, unsigned int len) {
    size_t n = 0;
    unsigned int i;
    int gap = 0; /* 1 a space, 2 a line break, before the next letter */
    for (i = 0; i < len && n + 3 < size; i++) {
        const char c = message[i];
        if (c == '\n' || c == '\r') {
            gap = 2;
        } else if (c == ' ' || c == '\t') {
            gap = gap > 1 ? gap : 1;
        } else {
            if (gap != 0 && n > 0) {
                if (gap == 2) {
                    out[n++] = ';';
                }
                out[n++] = ' ';
            }
            gap = 0;
            out[n++] = c;
        }
    }
    out[n] = '\0';
}

void portFlightLog(int level, const char* message, unsigned int len) {
    char text[600];
    if (!sOn || message == NULL) {
        return;
    }
    if (len > 16 && strncmp(message, "Graphics adapter", 16) == 0) {
        SDL_LockMutex(sLock);
        oneLine(sAdapter, sizeof(sAdapter), message, len);
        SDL_UnlockMutex(sLock);
    } else if (len > 21 && strncmp(message, "Adapter capabilities:", 21) == 0) {
        SDL_LockMutex(sLock);
        oneLine(sCaps, sizeof(sCaps), message, len);
        SDL_UnlockMutex(sLock);
    } else if ((len > 18 && strncmp(message, "Graphics fallbacks", 18) == 0) ||
               (len > 15 && strncmp(message, "Immediate data:", 15) == 0)) {
        SDL_LockMutex(sLock);
        if (sFallbacks[0] == '\0' || strncmp(message, "Graphics fallbacks", 18) == 0) {
            oneLine(sFallbacks, sizeof(sFallbacks), message, len);
        }
        SDL_UnlockMutex(sLock);
    }
    if (level >= LOG_WARNING) {
        snprintf(text, sizeof(text), "%s: %.*s", level >= LOG_ERROR ? "Aurora error" : "Aurora warning", (int)len,
                 message);
        event(text);
    }
}

int portFlightStoppedOnScreen(void) {
    return sStopped;
}

const char* portFlightLastPath(void) {
    return sHaveLast ? sLast : NULL;
}

/* --- the run --- */

static void onExit(void) {
    if (!sOn) {
        return;
    }
    SDL_LockMutex(sLock);
    if (sEnd == NULL) {
        sEnd = "a clean exit";
    }
    SDL_UnlockMutex(sLock);
    writeNow();
}

static bool SDLCALL onEvent(void* data, SDL_Event* e) {
    (void)data;
    switch (e->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        if (!sBackground) {
            sBackground = 1;
            event("to the background");
        }
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        if (sBackground) {
            sBackground = 0;
            event("back on screen");
        }
        break;
    case SDL_EVENT_TERMINATING:
        SDL_LockMutex(sLock);
        if (sEnd == NULL) {
            sEnd = "the system ended it as asked";
        }
        want();
        SDL_UnlockMutex(sLock);
        break;
    default:
        break;
    }
    return true;
}

static void systemLine(void) {
    char extra[160];
    extra[0] = '\0';
#if defined(__ANDROID__)
    {
        char maker[PROP_VALUE_MAX], model[PROP_VALUE_MAX], release[PROP_VALUE_MAX], board[PROP_VALUE_MAX];
        maker[0] = model[0] = release[0] = board[0] = '\0';
        __system_property_get("ro.product.manufacturer", maker);
        __system_property_get("ro.product.model", model);
        __system_property_get("ro.build.version.release", release);
        __system_property_get("ro.board.platform", board);
        snprintf(extra, sizeof(extra), ", %s %s, Android %s, %s", maker, model, release, board);
    }
#endif
    snprintf(sSystem, sizeof(sSystem), "%s%s, %d MB, %d threads", SDL_GetPlatform(), extra, SDL_GetSystemRAM(),
             SDL_GetNumLogicalCPUCores());
}

void portFlightStart(const char* userPath) {
    const char* env = getenv("TTYD_FLIGHT");
    char* pref = NULL;
    size_t len;
    if (env != NULL && env[0] == '0') {
        return;
    }
    if (userPath == NULL) {
        pref = SDL_GetPrefPath(NULL, "ttyd"); /* Aurora's default (its appName) */
        userPath = pref;
    }
    if (userPath == NULL || userPath[0] == '\0') {
        SDL_free(pref);
        return;
    }
    len = strlen(userPath);
    while (len > 1 && (userPath[len - 1] == '/' || userPath[len - 1] == '\\')) {
        len--;
    }
    snprintf(sPath[0], sizeof(sPath[0]), "%.*s/flight_record_a.txt", (int)len, userPath);
    snprintf(sPath[1], sizeof(sPath[1]), "%.*s/flight_record_b.txt", (int)len, userPath);
    snprintf(sLast, sizeof(sLast), "%.*s/flight_record_last.txt", (int)len, userPath);
    SDL_free(pref);
    lookAtLastRun();
    sLock = SDL_CreateMutex();
    sWake = SDL_CreateCondition();
    sWrote = SDL_CreateCondition();
    if (sLock == NULL || sWake == NULL || sWrote == NULL) {
        return;
    }
    systemLine();
    snprintf(sSettingsLine, sizeof(sSettingsLine), "not read yet");
    sThread = SDL_CreateThread(writer, "flight recorder", NULL);
    if (sThread == NULL) {
        return;
    }
    SDL_DetachThread(sThread);
    sOn = 1;
    env = getenv("TTYD_FLIGHT_KILL_AT");
    sKillAt = env != NULL ? strtoull(env, NULL, 10) : 0;
    atexit(onExit);
    portFlightMark("phase: starting graphics%s", portSafeGraphicsAtStart() ? " (safe graphics)" : "");
    writeNow(); /* (a driver that takes the device down while starting: this much is there) */
}

/* the graphics settings, short */
static void settingsLine(char* out, size_t size) {
    const PortSettings* s = portSettings();
    char a[8], b[8], c[8], d[8], e[8], f[8], g[8], h[8];
    snprintf(out, size,
             "safe %s, api %s, resolution %d, msaa %d, supersample %s, upscale %s, pack memory %s, aniso %s, "
             "bloom %s, rt %s, frame rate %s, no pop-in %d, low memory %d",
             settingValue("safe_graphics", a, sizeof(a)), settingValue("gfx_backend", b, sizeof(b)), s->resolution,
             s->msaa, settingValue("supersample", c, sizeof(c)), settingValue("texture_upscale", d, sizeof(d)),
             settingValue("texture_pack_memory", e, sizeof(e)), settingValue("aniso", f, sizeof(f)),
             settingValue("bloom", g, sizeof(g)), settingValue("rt_lighting", h, sizeof(h)), s->framerate, s->noPopIn,
             portLowMemoryGfx());
}

static void second(Uint64 now) {
    AuroraResourceStats r;
    const AuroraStats* a = aurora_get_stats();
    char line[SECOND_LEN], set[sizeof(sSettingsLine)];
    const unsigned pipelines = a != NULL ? a->createdPipelines : 0;
    memset(&r, 0, sizeof(r));
    if (aurora_get_backend() != BACKEND_AUTO) {
        AuroraGetResourceStats(&r);
    }
    snprintf(line, sizeof(line),
             "%7.1f s  %s | frames %u, %.1f ms (worst %.0f), work %.1f | draws %u passes %u copies %u readbacks %u "
             "uploads %u (%u KB, %u own) | textures %u (%llu MB), content %llu MB, copies %u (%llu MB) | bind groups "
             "%u samplers %u | pipelines %u (+%u, %u queued)",
             (double)now / 1000.0, sRoom[0] ? sRoom : "-", sCount, sCount ? sSum / sCount : 0.0, sWorst,
             sCount ? sWork / sCount : 0.0, a != NULL ? a->drawCallCount : 0, r.renderPasses, r.textureCopies,
             r.readbacks, r.textureUploads, a != NULL ? a->lastTextureUploadSize >> 10 : 0, r.overflowUploads,
             r.textures, (unsigned long long)(r.textureBytes >> 20), (unsigned long long)(r.contentBytes >> 20),
             r.copyEntries, (unsigned long long)(r.copyBytes >> 20), r.bindGroups, r.samplers, pipelines,
             pipelines >= sPipelinesWas ? pipelines - sPipelinesWas : 0, a != NULL ? a->queuedPipelines : 0);
    sPipelinesWas = pipelines;
    sCount = 0;
    sSum = sWorst = sWork = 0;
    settingsLine(set, sizeof(set));
    {
        SDL_Window* w = aurora_get_window();
        /* (offscreen runs, tests: a window never shown counts as on screen, as the graphics guard's) */
        const int hidden = !portOffscreen() && w != NULL &&
                           (SDL_GetWindowFlags(w) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) != 0;
        int changed;
        SDL_LockMutex(sLock);
        snprintf(sSeconds[sSecondNext++ % SECONDS], SECOND_LEN, "%s", line);
        changed = strcmp(set, sSettingsLine) != 0 && strcmp(sSettingsLine, "not read yet") != 0;
        snprintf(sSettingsLine, sizeof(sSettingsLine), "%s", set);
        sHidden = hidden;
        if (now - sLastSync >= SYNC_MS || changed) {
            sLastSync = now;
            want();
        }
        SDL_UnlockMutex(sLock);
        if (changed) {
            event("graphics settings changed");
        }
    }
}

void portFlightFrame(float intervalMs, float workMs) {
    const Uint64 now = SDL_GetTicks();
    if (!sOn) {
        return;
    }
    if (!sWatching) {
        sWatching = 1;
        SDL_AddEventWatch(onEvent, NULL); /* (Aurora has SDL's events up by now) */
    }
    if (intervalMs < 0.0f) {
        intervalMs = sLastCall != 0 ? (float)(now - sLastCall) : 0.0f;
        workMs = 0.0f;
    }
    sLastCall = now;
    sFrames++;
    sCount++;
    sSum += intervalMs;
    sWork += workMs;
    if (intervalMs > sWorst) {
        sWorst = intervalMs;
    }
    /* the room and the battles, each frame (a few bytes compared) */
    if (gp != NULL && strncmp(sRoom, gp->currentMapName, sizeof(sRoom)) != 0) {
        snprintf(sRoom, sizeof(sRoom), "%.15s", gp->currentMapName);
        if (sRoom[0] != '\0') {
            portFlightMark("room %s", sRoom);
        }
    }
    if (gp != NULL) {
        const int seq = (int)seqGetSeq();
        if (seq != sSeqWas) {
            if (seq == SEQ_BATTLE) {
                portFlightMark("battle starts in %s", sRoom);
            } else if (sSeqWas == SEQ_BATTLE) {
                portFlightMark("battle ends");
            }
            sSeqWas = seq;
        }
    }
    if (sLastSecond == 0) {
        sLastSecond = now;
    } else if (now - sLastSecond >= 1000) {
        sLastSecond = now;
        second(now);
    }
    if (sKillAt != 0 && sFrames >= sKillAt) {
        second(now);
        portFlightMark("test: ended here as a device reset would ($TTYD_FLIGHT_KILL_AT)");
        writeNow();
        printf("[flight] test: ended at frame %llu with no exit handlers\n", sFrames);
        fflush(stdout);
        _Exit(9);
    }
}
