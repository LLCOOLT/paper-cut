/* The picture's place in the window (port/include/port/layout.h). Aurora
 * fits the picture to, and centers it in, its present area (Aurora patch
 * 0006, AuroraSetPresentArea). */
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <dolphin/gx/GXAurora.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* Android's bars kept on the screen (system_bars, port/settings/features/window.cpp):
 * the picture keeps clear of them. Hidden (the default), only the controls
 * keep to the safe area. */
static int systemBarsShown(void) {
#if defined(__ANDROID__)
    static int* sBars;
    if (sBars == NULL) {
        sBars = portSettingsDeclare("system_bars", 0, 0, 1);
    }
    return *sBars != 0;
#else
    return 0;
#endif
}

/* $TTYD_TOUCH_DEVICE (port/include/port/layout.h): read once */
static struct {
    int on;
    int w, h;       /* the window, in points */
    float dpi;      /* points an inch */
    float inset[4]; /* the safe area's left, top, right, bottom insets, in points */
} sDevice;

int portTouchDevice(int* w, int* h) {
    static int sRead;
    if (!sRead) {
        const char* v = getenv("TTYD_TOUCH_DEVICE");
        int dw = 0, dh = 0;
        float dpi = 0.f;
        float in[4] = {0.f, 0.f, 0.f, 0.f};
        sRead = 1;
        if (v != NULL && sscanf(v, "%dx%d@%f,%f,%f,%f,%f", &dw, &dh, &dpi, &in[0], &in[1], &in[2], &in[3]) >= 3 &&
            dw > 0 && dh > 0 && dpi > 0.f) {
            int d = 1, i;
            while (dw / d > 1600 || dh / d > 1000) {
                d++;
            }
            const char* ios = getenv("TTYD_TOUCH_IOS");
            char model[32];
            int scale = 0;
            if (ios != NULL && sscanf(ios, "%31[^@]@%d", model, &scale) == 2 && scale > 0) {
                const int pad = strncmp(model, "iPad", 4) == 0;
                dpi = portIosScreenPpi(model, pad, scale, (float)(dw < dh ? dw : dh) / (float)scale);
                printf("[touch] test device: iOS's %s at %dx, %.0f pixels an inch (%.1f points)\n", model, scale,
                       (double)dpi, (double)(dpi / (float)scale));
            }
            sDevice.on = 1;
            sDevice.w = dw / d;
            sDevice.h = dh / d;
            sDevice.dpi = dpi / (float)d;
            for (i = 0; i < 4; i++) {
                sDevice.inset[i] = in[i] / (float)d;
            }
            printf("[touch] test device: %dx%d at %.0f dpi, as a %dx%d window (%.2f points a mm)\n", dw, dh,
                   (double)dpi, sDevice.w, sDevice.h, (double)(sDevice.dpi / 25.4f));
        }
    }
    if (sDevice.on) {
        if (w != NULL) {
            *w = sDevice.w;
        }
        if (h != NULL) {
            *h = sDevice.h;
        }
    }
    return sDevice.on;
}

#if defined(__ANDROID__)
/* The screen's pixels an inch from Android's DisplayMetrics: the panel's own
 * (xdpi, ydpi) when they are plausible, else its density bucket (densityDpi;
 * some devices report made-up xdpi). 0 if they can't be read. */
static float androidDpi(void) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    float dpi = 0.f;
    jclass actCls;
    jmethodID getRes;
    jobject res;
    if (env == NULL || activity == NULL) {
        return 0.f;
    }
    actCls = (*env)->GetObjectClass(env, activity);
    getRes = (*env)->GetMethodID(env, actCls, "getResources", "()Landroid/content/res/Resources;");
    res = getRes != NULL ? (*env)->CallObjectMethod(env, activity, getRes) : NULL;
    if (res != NULL) {
        jclass resCls = (*env)->GetObjectClass(env, res);
        jmethodID getDm = (*env)->GetMethodID(env, resCls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
        jobject dm = getDm != NULL ? (*env)->CallObjectMethod(env, res, getDm) : NULL;
        if (dm != NULL) {
            jclass dmCls = (*env)->GetObjectClass(env, dm);
            jfieldID fx = (*env)->GetFieldID(env, dmCls, "xdpi", "F");
            jfieldID fy = (*env)->GetFieldID(env, dmCls, "ydpi", "F");
            jfieldID fd = (*env)->GetFieldID(env, dmCls, "densityDpi", "I");
            const float bucket = fd != NULL ? (float)(*env)->GetIntField(env, dm, fd) : 0.f;
            const float real =
                fx != NULL && fy != NULL ? 0.5f * ((*env)->GetFloatField(env, dm, fx) + (*env)->GetFloatField(env, dm, fy))
                                         : 0.f;
            dpi = bucket > 0.f && real > bucket * 0.75f && real < bucket * 1.33f ? real : bucket;
            (*env)->DeleteLocalRef(env, dmCls);
            (*env)->DeleteLocalRef(env, dm);
        }
        (*env)->DeleteLocalRef(env, resCls);
        (*env)->DeleteLocalRef(env, res);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        dpi = 0.f;
    }
    (*env)->DeleteLocalRef(env, actCls);
    (*env)->DeleteLocalRef(env, activity);
    return dpi;
}
#endif

float portIosScreenPpi(const char* model, int pad, int scale, float shortSidePts) {
    /* the models whose screens are unlike the rest of their kind */
    static const struct {
        const char* model;
        float ppi;
    } kOwn[] = {
        /* the 3x Plus phones: a 1080p panel under a 1242-pixel-wide picture (nativeScale 2.6) */
        {"iPhone7,1", 401.f}, {"iPhone8,2", 401.f}, {"iPhone9,2", 401.f}, {"iPhone9,4", 401.f},
        {"iPhone10,2", 401.f}, {"iPhone10,5", 401.f},
        /* the 12 mini and the 13 mini */
        {"iPhone13,1", 476.f}, {"iPhone14,4", 476.f},
        /* the iPad minis 2 to 5 (768 points wide, as the 9.7 in iPads are) */
        {"iPad4,4", 326.f}, {"iPad4,5", 326.f}, {"iPad4,6", 326.f}, {"iPad4,7", 326.f}, {"iPad4,8", 326.f},
        {"iPad4,9", 326.f}, {"iPad5,1", 326.f}, {"iPad5,2", 326.f}, {"iPad11,1", 326.f}, {"iPad11,2", 326.f},
    };
    size_t i;
    if (model != NULL) {
        for (i = 0; i < sizeof(kOwn) / sizeof(kOwn[0]); i++) {
            if (strcmp(model, kOwn[i].model) == 0) {
                return kOwn[i].ppi;
            }
        }
    }
    if (pad) {
        /* an iPad mini (6 and later: 744 points wide) 326; the rest 264 */
        return shortSidePts > 0.f && shortSidePts <= 744.f ? 326.f : 264.f;
    }
    /* an iPhone (or an iPod touch): 3x OLED ones 458 to 460, 2x ones 326 */
    return scale >= 3 ? 460.f : 326.f;
}

float portTouchUnitMm(float shortSideMm) {
    float t;
    if (!(shortSideMm > PORT_TOUCH_PHONE_SIDE_MM)) {
        return PORT_TOUCH_UNIT_PHONE_MM;
    }
    if (shortSideMm >= PORT_TOUCH_TABLET_SIDE_MM) {
        return PORT_TOUCH_UNIT_MM;
    }
    t = (shortSideMm - PORT_TOUCH_PHONE_SIDE_MM) / (PORT_TOUCH_TABLET_SIDE_MM - PORT_TOUCH_PHONE_SIDE_MM);
    return PORT_TOUCH_UNIT_PHONE_MM + (PORT_TOUCH_UNIT_MM - PORT_TOUCH_UNIT_PHONE_MM) * t;
}

float portLayoutPointsPerMm(void) {
    SDL_Window* w = aurora_get_window();
    float density = 1.f;
    if (portTouchDevice(NULL, NULL)) {
        return sDevice.dpi / 25.4f;
    }
    if (w != NULL && SDL_GetWindowPixelDensity(w) > 0.f) {
        density = SDL_GetWindowPixelDensity(w);
    }
#if defined(__ANDROID__)
    {
        /* (the window's points are the screen's pixels) */
        static float sDpi = -1.f;
        if (sDpi < 0.f) {
            sDpi = androidDpi();
        }
        if (sDpi > 0.f) {
            return sDpi / density / 25.4f;
        }
        /* Android's own baseline: 160 pixels an inch a unit of display scale */
        return 160.f * (w != NULL ? SDL_GetWindowDisplayScale(w) : 1.f) / density / 25.4f;
    }
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    {
        /* The screen's own pixels an inch over its pixels a point. (Not one
         * figure for every iPhone: 163 points an inch is a 2x one's; a 3x
         * one's are 153, so its controls came out 6 % larger than meant.) */
        static float sPts = -1.f;
        (void)density;
        if (sPts < 0.f) {
            char model[32];
            int pad = 0, scale = 0;
            float nativeScale = 0.f, shortSide = 0.f;
            sPts = 0.f;
            if (portIosScreenInfo(model, sizeof(model), &pad, &scale, &nativeScale, &shortSide) && nativeScale > 0.f) {
                sPts = portIosScreenPpi(model, pad, scale, shortSide) / nativeScale;
                printf("[touch] %s at %dx (native %.3f): %.1f points an inch\n", model, scale, (double)nativeScale,
                       (double)sPts);
            }
        }
        return (sPts > 0.f ? sPts : 163.f) / 25.4f;
    }
#else
    {
        /* the Steam Deck's own panel (7 inches, 1280x800): 216 pixels an inch */
        const SDL_DisplayID d = w != NULL ? SDL_GetDisplayForWindow(w) : 0;
        const SDL_DisplayMode* m = d != 0 ? SDL_GetDesktopDisplayMode(d) : NULL;
        const float scale = w != NULL && SDL_GetWindowDisplayScale(w) > 0.f ? SDL_GetWindowDisplayScale(w) : 1.f;
        if (portSteamDeck() && m != NULL && ((m->w == 1280 && m->h == 800) || (m->w == 800 && m->h == 1280))) {
            return 216.f / density / 25.4f;
        }
        /* a desktop's display scale is meant to make 96 pixels an inch a
         * comfortable size: as good a guess as there is (a touch laptop) */
        return 96.f * scale / density / 25.4f;
    }
#endif
}

float portLayoutTouchUnit(void) {
    /* (the window's short side: the whole screen on a phone or a tablet; the same either way up) */
    SDL_Window* w = aurora_get_window();
    const float mm = portLayoutPointsPerMm();
    int ww = 0, wh = 0;
    float side = 0.f;
    if (!portTouchDevice(&ww, &wh) && (w == NULL || !SDL_GetWindowSize(w, &ww, &wh))) {
        ww = wh = 0;
    }
    if (ww > 0 && wh > 0 && mm > 0.f) {
        side = (float)(ww < wh ? ww : wh) / mm;
    }
    if (portTouchDevice(NULL, NULL)) {
        /* (tests: $TTYD_TOUCH_UNIT_MM, another unit to compare with: an older release's) */
        const char* v = getenv("TTYD_TOUCH_UNIT_MM");
        if (v != NULL && atof(v) > 0.0) {
            return (float)atof(v) * mm * (float)portSettings()->touchSize / 100.f;
        }
    }
    return portTouchUnitMm(side) * mm * (float)portSettings()->touchSize / 100.f;
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
    if (portTouchDevice(NULL, NULL)) {
        /* (the hidden window kept at the device's size: the window's restore
         * fits a window to the desktop's screen) */
        if (ww != sDevice.w || wh != sDevice.h) {
            SDL_SetWindowSize(w, sDevice.w, sDevice.h);
        }
        safe.x = (int)sDevice.inset[0];
        safe.y = (int)sDevice.inset[1];
        safe.w = ww - (int)(sDevice.inset[0] + sDevice.inset[2]);
        safe.h = wh - (int)(sDevice.inset[1] + sDevice.inset[3]);
    }
#ifdef PORT_TOUCH_OVERLAY
    l.portrait = wh > ww;
#else
    l.portrait = 0; /* a tall desktop window: the picture centered */
#endif
    if (l.portrait) {
        /* The controls take the bottom (about 0.9 of their size unit, as
         * port/touch/touch_overlay.cpp lays them out); the picture, the full
         * safe width, is centered in what is above them. */
        const float u = fminf((float)safe.w, portLayoutTouchUnit());
        const float above = (float)safe.h - 0.9f * u;
        fit((float)safe.x, (float)safe.y, (float)safe.w, above > 0.f ? above : (float)safe.h * 0.5f, aspect, &l);
        l.ctrlX = (float)safe.x;
        l.ctrlY = l.picY + l.picH;
        l.ctrlW = (float)safe.w;
        l.ctrlH = (float)(safe.y + safe.h) - l.ctrlY;
        area[0] = 0.f;
        area[1] = l.picY / wh;
        area[2] = 1.f;
        area[3] = (l.picY + l.picH) / wh;
    } else if (portSettings()->safeArea || systemBarsShown()) {
        /* clear of the notch, the Dynamic Island and the home indicator
         * (and of Android's status and navigation bars when they're kept) */
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
