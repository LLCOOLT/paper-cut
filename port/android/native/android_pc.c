/* Android glue for the Aurora backend (port/include/port/android_pc.h).
 *
 * - stdout/stderr: an app's go to /dev/null, so both are piped to logcat
 *   (tag "ttyd") by a reader thread: OSReport, the trace, Aurora's log.
 * - env.txt: the debugging switches of docs/PC_PORT.md (TTYD_TRACE, TTYD_MAP,
 *   ...) have no command line here; NAME=value lines in the app's external
 *   files folder (adb push env.txt /sdcard/Android/data/<app id>/files/) are
 *   set before the game starts.
 * - crashes: the fault and a backtrace to logcat, then the system's own
 *   handler (debuggerd) writes the tombstone, which crash_pc.c's handler
 *   would have suppressed.
 * - lifecycle: SDL sends WILL_ENTER_BACKGROUND / DID_ENTER_FOREGROUND from its
 *   event pump on this thread just before it blocks while the activity is
 *   paused; the game is paused there (portPause: game clock, MusyX output).
 *   Aurora additionally reports the lost surface (AURORA_PAUSED, vi_aurora.c)
 *   and rebuilds the swapchain when the surface comes back.
 * - content:// URIs: what the file picker gives, read through the activity
 *   (TtydActivity.contentInfo): stat() cannot see them.
 * - updates: a downloaded APK handed to Android's installer
 *   (TtydActivity.installApk, port/settings/features/update_check.cpp). */
#include <android/log.h>
#include <jni.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unwind.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_system.h>
#include "port/android_pc.h"
#include "port/platform.h"

#define TAG "ttyd"

/* --- stdout/stderr to logcat --- */

static int sLogPipe[2] = {-1, -1};

static void* logReader(void* arg) {
    char line[1024];
    size_t len = 0;
    (void)arg;
    for (;;) {
        struct pollfd pfd = {sLogPipe[0], POLLIN, 0};
        ssize_t n;
        if (poll(&pfd, 1, -1) < 0 && errno != EINTR) {
            return NULL;
        }
        n = read(sLogPipe[0], line + len, sizeof(line) - 1 - len);
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                return NULL;
            }
            continue;
        }
        len += (size_t)n;
        for (;;) {
            char* nl = memchr(line, '\n', len);
            size_t take;
            if (nl != NULL) {
                *nl = '\0';
                take = (size_t)(nl - line) + 1;
            } else if (len == sizeof(line) - 1) {
                line[len] = '\0'; /* a full buffer without a newline: log it as is */
                take = len;
            } else {
                break;
            }
            __android_log_write(ANDROID_LOG_INFO, TAG, line);
            len -= take;
            memmove(line, line + take, len);
        }
    }
}

static void redirectOutput(void) {
    pthread_t thread;
    if (pipe(sLogPipe) != 0) {
        return;
    }
    /* non-blocking read end: the crash handler drains what is left */
    fcntl(sLogPipe[0], F_SETFL, fcntl(sLogPipe[0], F_GETFL) | O_NONBLOCK);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(sLogPipe[1], STDOUT_FILENO);
    dup2(sLogPipe[1], STDERR_FILENO);
    if (pthread_create(&thread, NULL, logReader, NULL) == 0) {
        pthread_detach(thread);
    }
}

/* Whatever the reader thread has not logged yet (crash handler). */
static void drainOutput(void) {
    char buf[1024];
    ssize_t n;
    if (sLogPipe[0] < 0) {
        return;
    }
    fflush(stdout);
    while ((n = read(sLogPipe[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        __android_log_write(ANDROID_LOG_INFO, TAG, buf);
    }
}

/* --- env.txt --- */

static void loadEnvFile(void) {
    const char* dir = SDL_GetAndroidExternalStoragePath(); /* creates the folder */
    char path[1024], line[1024];
    FILE* f;
    if (dir == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%s/env.txt", dir);
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t n = strlen(line);
        char* eq;
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) {
            line[--n] = '\0';
        }
        eq = strchr(line, '=');
        if (line[0] == '\0' || line[0] == '#' || eq == NULL || eq == line) {
            continue;
        }
        *eq = '\0';
        setenv(line, eq + 1, 0);
        __android_log_print(ANDROID_LOG_INFO, TAG, "env.txt: %s=%s", line, eq + 1);
    }
    fclose(f);
}

/* --- crash reports --- */

static const int kCrashSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
#define CRASH_SIGNAL_COUNT (int)(sizeof(kCrashSignals) / sizeof(kCrashSignals[0]))
static struct sigaction sSystemAction[CRASH_SIGNAL_COUNT]; /* debuggerd's, from before portCrashInit */

typedef struct {
    int count;
} BacktraceState;

static _Unwind_Reason_Code backtraceFrame(struct _Unwind_Context* ctx, void* arg) {
    BacktraceState* state = (BacktraceState*)arg;
    uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);
    Dl_info info;
    if (pc == 0) {
        return _URC_END_OF_STACK;
    }
    if (dladdr((void*)pc, &info) != 0 && info.dli_fname != NULL) {
        const char* file = strrchr(info.dli_fname, '/');
        __android_log_print(ANDROID_LOG_FATAL, TAG, "  #%02d pc %08zx  %s (%s+0x%zx)", state->count,
                            (size_t)(pc - (uintptr_t)info.dli_fbase), file != NULL ? file + 1 : info.dli_fname,
                            info.dli_sname != NULL ? info.dli_sname : "?",
                            (size_t)(pc - (uintptr_t)(info.dli_saddr != NULL ? info.dli_saddr : info.dli_fbase)));
    } else {
        __android_log_print(ANDROID_LOG_FATAL, TAG, "  #%02d pc %p", state->count, (void*)pc);
    }
    return ++state->count < 64 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

static void onCrash(int sig, siginfo_t* si, void* uc) {
    BacktraceState state = {0};
    int i;
    (void)uc;
    drainOutput();
    __android_log_print(ANDROID_LOG_FATAL, TAG, "crash: signal %d (%s), code %d, address %p", sig, strsignal(sig),
                        si->si_code, si->si_addr);
    _Unwind_Backtrace(backtraceFrame, &state);
    /* Hand over to the system's handler for the tombstone: a fault (code > 0)
     * traps again when the handler returns; a sent signal (abort) is resent. */
    for (i = 0; i < CRASH_SIGNAL_COUNT; i++) {
        if (kCrashSignals[i] == sig) {
            sigaction(sig, &sSystemAction[i], NULL);
        }
    }
    if (si->si_code <= 0) {
        raise(sig);
    }
}

static void installCrashHandler(void) {
    struct sigaction sa;
    int i;
    if (getenv("TTYD_NO_CRASH_HANDLER") != NULL) {
        return;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = onCrash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < CRASH_SIGNAL_COUNT; i++) {
        sigaction(kCrashSignals[i], &sa, NULL);
    }
}

/* --- lifecycle --- */

static BOOL sBackground;

static bool SDLCALL onAppEvent(void* userdata, SDL_Event* event) {
    (void)userdata;
    if (!SDL_IsMainThread()) {
        return true; /* app events come from the event pump on the game's thread */
    }
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND && !sBackground) {
        sBackground = TRUE;
        __android_log_write(ANDROID_LOG_INFO, TAG, "background: paused");
        portPause();
    } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND && sBackground) {
        sBackground = FALSE;
        __android_log_write(ANDROID_LOG_INFO, TAG, "foreground: resumed");
        portResume();
    } else if (event->type == SDL_EVENT_LOW_MEMORY) {
        __android_log_write(ANDROID_LOG_WARN, TAG, "the system is low on memory");
    }
    return true;
}

void portAndroidEarlyInit(void) {
    int i;
    redirectOutput();
    for (i = 0; i < CRASH_SIGNAL_COUNT; i++) {
        sigaction(kCrashSignals[i], NULL, &sSystemAction[i]);
    }
    loadEnvFile();
}

void portAndroidInit(void) {
    installCrashHandler(); /* replaces crash_pc.c's (stderr, then SIG_DFL: no tombstone) */
    if (!SDL_AddEventWatch(onAppEvent, NULL)) {
        __android_log_print(ANDROID_LOG_WARN, TAG, "SDL_AddEventWatch: %s", SDL_GetError());
    }
}

/* --- content:// URIs (the file picker's) --- */

int portAndroidContentInfo(const char* uri, char* name, size_t nameSize, unsigned long long* size) {
    JNIEnv* env;
    jobject activity;
    jclass cls;
    jmethodID info;
    jstring juri, result = NULL;
    int ok = 0;
    if (name != NULL && nameSize > 0) {
        name[0] = '\0';
    }
    if (size != NULL) {
        *size = 0;
    }
    if (!PORT_ANDROID_IS_CONTENT_URI(uri)) {
        return 0;
    }
    /* SDL attaches this thread to the VM; the activity's class (not FindClass,
     * which on a native thread sees only the system's classes) */
    env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    activity = (jobject)SDL_GetAndroidActivity();
    if (env == NULL || activity == NULL) {
        return 0;
    }
    cls = (*env)->GetObjectClass(env, activity);
    info = (*env)->GetStaticMethodID(env, cls, "contentInfo", "(Ljava/lang/String;)Ljava/lang/String;");
    juri = info != NULL && !(*env)->ExceptionCheck(env) ? (*env)->NewStringUTF(env, uri) : NULL;
    if (juri != NULL) {
        result = (jstring)(*env)->CallStaticObjectMethod(env, cls, info, juri);
        (*env)->DeleteLocalRef(env, juri);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        result = NULL;
    }
    if (result != NULL) {
        const char* text = (*env)->GetStringUTFChars(env, result, NULL);
        if (text != NULL) {
            const char* nl = strchr(text, '\n');
            ok = 1;
            if (size != NULL) {
                *size = strtoull(text, NULL, 10);
            }
            if (name != NULL && nameSize > 0 && nl != NULL) {
                snprintf(name, nameSize, "%s", nl + 1);
            }
            (*env)->ReleaseStringUTFChars(env, result, text);
        }
        (*env)->DeleteLocalRef(env, result);
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
    return ok;
}

/* --- updates: the APK to Android's installer (TtydActivity.installApk) --- */

int portAndroidInstallApk(const char* path) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    jclass cls;
    jmethodID install;
    jstring jpath;
    int result = -1;
    if (env == NULL || activity == NULL || path == NULL) {
        return -1;
    }
    cls = (*env)->GetObjectClass(env, activity);
    install = (*env)->GetStaticMethodID(env, cls, "installApk", "(Ljava/lang/String;)I");
    jpath = install != NULL && !(*env)->ExceptionCheck(env) ? (*env)->NewStringUTF(env, path) : NULL;
    if (jpath != NULL) {
        result = (int)(*env)->CallStaticIntMethod(env, cls, install, jpath);
        (*env)->DeleteLocalRef(env, jpath);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        result = -1;
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
    return result;
}

int portAndroidInstallResult(char* message, size_t messageSize) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    jclass cls;
    jmethodID take;
    jstring text = NULL;
    int code = 0;
    if (message != NULL && messageSize > 0) {
        message[0] = '\0';
    }
    if (env == NULL || activity == NULL) {
        return 0;
    }
    cls = (*env)->GetObjectClass(env, activity);
    take = (*env)->GetStaticMethodID(env, cls, "installResult", "()Ljava/lang/String;");
    if (take != NULL && !(*env)->ExceptionCheck(env)) {
        text = (jstring)(*env)->CallStaticObjectMethod(env, cls, take);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        text = NULL;
    }
    if (text != NULL) {
        /* "code\nmessage" */
        const char* utf = (*env)->GetStringUTFChars(env, text, NULL);
        if (utf != NULL) {
            const char* nl = strchr(utf, '\n');
            code = atoi(utf);
            if (message != NULL && messageSize > 0 && nl != NULL) {
                snprintf(message, messageSize, "%s", nl + 1);
            }
            (*env)->ReleaseStringUTFChars(env, text, utf);
        }
        (*env)->DeleteLocalRef(env, text);
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
    return code;
}
