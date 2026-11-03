/* The picture's place in the window (port/include/port/layout.h). Aurora
 * fits the picture to, and centers it in, its present area (Aurora patch
 * 0006, AuroraSetPresentArea). */
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <dolphin/gx/GXAurora.h>
#include <math.h>
#include "port/layout.h"
#include "port/settings.h"
#if defined(__ANDROID__)
#include <jni.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#include "port/ios_pc.h"
#endif

static PortLayout sLayout;
static int sValid;
static float sArea[4] = {0.f, 0.f, 1.f, 1.f};

/* the largest rect of this aspect in (x, y, w, h), centered */
static void fit(float x, float y, float w, float h, float aspect, PortLayout* l) {
    if (w / h > aspect) {
        l->picH = h;
        l->picW = h * aspect;
    } else {
        l->picW = w;
        l->picH = w / aspect;
    }
    l->picX = x + (w - l->picW) * 0.5f;
    l->picY = y + (h - l->picH) * 0.5f;
}

void portLayoutFrame(void) {
    SDL_Window* w = aurora_get_window();
    SDL_Rect safe;
    int ww = 0, wh = 0;
    float area[4];
    float aspect = portSettings()->widescreen > 0 ? 16.f / 9.f : 4.f / 3.f;
    u32 rw = 0, rh = 0;
    PortLayout l;
    /* the picture's actual shape (a widescreen game still shows some
     * screens at 4:3) */
    AuroraGetRenderSize(&rw, &rh);
    if (rw > 0 && rh > 0) {
        aspect = (float)rw / (float)rh;
    }
    if (w == NULL || !SDL_GetWindowSize(w, &ww, &wh) || ww <= 0 || wh <= 0) {
        return;
    }
    if (!SDL_GetWindowSafeArea(w, &safe) || safe.w <= 0 || safe.h <= 0) {
        safe.x = safe.y = 0;
        safe.w = ww;
        safe.h = wh;
    }
#ifdef PORT_TOUCH_OVERLAY
    l.portrait = wh > ww;
#else
    l.portrait = 0; /* a tall desktop window: the picture centered */
#endif
    if (l.portrait) {
        /* The controls take the bottom (about 0.8 of their size unit, as
         * port/touch/touch_overlay.cpp lays them out); the picture, the full
         * safe width, is centered in what is above them. */
        const float u = fminf((float)safe.w, 440.f) * portSettings()->touchSize / 100.f;
        const float above = (float)safe.h - 0.8f * u;
        fit((float)safe.x, (float)safe.y, (float)safe.w, above > 0.f ? above : (float)safe.h * 0.5f, aspect, &l);
        l.ctrlX = (float)safe.x;
        l.ctrlY = l.picY + l.picH;
        l.ctrlW = (float)safe.w;
        l.ctrlH = (float)(safe.y + safe.h) - l.ctrlY;
        area[0] = 0.f;
        area[1] = l.picY / wh;
        area[2] = 1.f;
        area[3] = (l.picY + l.picH) / wh;
    } else if (portSettings()->safeArea) {
        /* clear of the notch, the Dynamic Island and the home indicator */
        fit((float)safe.x, (float)safe.y, (float)safe.w, (float)safe.h, aspect, &l);
        l.ctrlX = (float)safe.x;
        l.ctrlY = (float)safe.y;
        l.ctrlW = (float)safe.w;
        l.ctrlH = (float)safe.h;
        area[0] = (float)safe.x / ww;
        area[1] = (float)safe.y / wh;
        area[2] = (float)(safe.x + safe.w) / ww;
        area[3] = (float)(safe.y + safe.h) / wh;
    } else {
        fit(0.f, 0.f, (float)ww, (float)wh, aspect, &l);
        l.ctrlX = (float)safe.x;
        l.ctrlY = (float)safe.y;
        l.ctrlW = (float)safe.w;
        l.ctrlH = (float)safe.h;
        area[0] = area[1] = 0.f;
        area[2] = area[3] = 1.f;
    }
    if (area[0] != sArea[0] || area[1] != sArea[1] || area[2] != sArea[2] || area[3] != sArea[3]) {
        AuroraSetPresentArea(area[0], area[1], area[2], area[3]);
        sArea[0] = area[0];
        sArea[1] = area[1];
        sArea[2] = area[2];
        sArea[3] = area[3];
    }
    sLayout = l;
    sValid = 1;
}

int portLayoutGet(PortLayout* out) {
    if (!sValid) {
        portLayoutFrame();
    }
    if (sValid) {
        *out = sLayout;
    }
    return sValid;
}

void portLayoutOrientation(int orientation) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    static const char* const kHints[] = {"LandscapeLeft LandscapeRight Portrait", "LandscapeLeft LandscapeRight",
                                         "Portrait"};
    static int sLast = -1;
    if (orientation < 0 || orientation > 2 || orientation == sLast) {
        return;
    }
    sLast = orientation;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, kHints[orientation]);
    portIosRefreshOrientation(orientation);
#elif defined(__ANDROID__)
    /* SDL reads its orientation hint only when the window is made: the
     * activity is told directly (ActivityInfo SCREEN_ORIENTATION_SENSOR,
     * _SENSOR_LANDSCAPE, _SENSOR_PORTRAIT; the manifest starts landscape). */
    static const int kRequested[] = {4, 6, 7};
    static int sLast = -1;
    JNIEnv* env;
    jobject activity;
    if (orientation < 0 || orientation > 2 || orientation == sLast) {
        return;
    }
    env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    activity = (jobject)SDL_GetAndroidActivity();
    if (env != NULL && activity != NULL) {
        jclass cls = (*env)->GetObjectClass(env, activity);
        jmethodID set = (*env)->GetMethodID(env, cls, "setRequestedOrientation", "(I)V");
        if (set != NULL) {
            (*env)->CallVoidMethod(env, activity, set, kRequested[orientation]);
            sLast = orientation;
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, cls);
        (*env)->DeleteLocalRef(env, activity);
    }
#else
    (void)orientation;
#endif
}
