/* Widescreen: port/include/port/widescreen.h. */
#include <stdlib.h>
#include <math.h>
#include <aurora/aurora.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#include "driver/camdrv.h"
#include "port/settings.h"
#include "port/view.h"
#include "port/widescreen.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

/* Off by default everywhere: much of the game was built for 4:3 only (the
 * title diorama, the opening story, indoor rooms end at the 4:3 edges), so
 * 16:9 shows past those sets. It is the player's choice. */
#define WIDESCREEN_DEFAULT 0

/* The game's frame is 608x480 with square pixels (camdrv's 3D aspect,
 * 1.2666666); the widened frame is the chosen aspect (16:9 by default). */
#define GAME_ASPECT (608.0f / 480.0f)
#define WIDE_ASPECT (16.0f / 9.0f)

static int sEnabled = -1;
static int* sAspectChoice; /* settings: aspect_ratio (PORT_ASPECT_*) */
static int* sAspectMax;    /* settings: aspect_max, 0 none or the widest (16:9, 21:9) */
static float sAspect;      /* the frame's now; 0: the game's 4:3 */
extern float gPortWidescreenEdgeScale; /* include/wide_x.h */

int portWidescreenEnabled(void) {
    if (sEnabled < 0) {
        const char* e = getenv("TTYD_WIDESCREEN");
        sEnabled = WIDESCREEN_DEFAULT;
#if defined(__APPLE__)
        {
            /* iOS: the switch in the Settings app (port/ios/Settings.bundle);
             * macOS: `defaults write <bundle id> widescreen -bool true` */
            Boolean valid = false;
            Boolean on = CFPreferencesGetAppBooleanValue(CFSTR("widescreen"), kCFPreferencesCurrentApplication, &valid);
            if (valid) {
                sEnabled = on ? 1 : 0;
            }
        }
#endif
        if (e != NULL) {
            sEnabled = e[0] == '1';
        }
    }
    return sEnabled;
}

static void declare(void) {
    if (sAspectChoice == NULL) {
        sAspectChoice = portSettingsDeclare("aspect_ratio", PORT_ASPECT_SWITCH, 0, PORT_ASPECT_WINDOW);
        sAspectMax = portSettingsDeclare("aspect_max", 0, 0, 2);
    }
}

/* The aspect the settings ask for now: 0 for the game's own 4:3. */
static float wanted(void) {
    static const float kMax[] = {0.0f, 16.0f / 9.0f, 21.0f / 9.0f};
    float aspect = 0.0f;
    declare();
    switch (*sAspectChoice) {
    case PORT_ASPECT_SWITCH:
        aspect = portWidescreenEnabled() ? WIDE_ASPECT : 0.0f;
        break;
    case PORT_ASPECT_16_9:
        aspect = 16.0f / 9.0f;
        break;
    case PORT_ASPECT_16_10:
        aspect = 16.0f / 10.0f;
        break;
    case PORT_ASPECT_21_9:
        aspect = 21.0f / 9.0f;
        break;
    case PORT_ASPECT_32_9:
        aspect = 32.0f / 9.0f;
        break;
    case PORT_ASPECT_WINDOW: {
        const AuroraWindowSize size = aurora_get_window_size();
        if (size.native_fb_width > 0 && size.native_fb_height > 0) {
            aspect = (float)size.native_fb_width / (float)size.native_fb_height;
        }
        break;
    }
    default: /* PORT_ASPECT_4_3 */
        break;
    }
    if (*sAspectMax > 0 && aspect > kMax[*sAspectMax]) {
        aspect = kMax[*sAspectMax]; /* the rest: bars (blurred with side_bars) */
    }
    /* a window no wider than 4:3 shows the game's own frame */
    return aspect > 4.0f / 3.0f + 0.01f ? aspect : 0.0f;
}

static void apply(void) {
    if (sAspect > 0.0f) {
        VILockAspectRatio((int)lroundf(sAspect * 1000.0f), 1000);
        /* a hair past the edge: s16 quad coordinates round toward 0 */
        gPortWidescreenEdgeScale = sAspect / GAME_ASPECT + 0.005f;
    } else {
        VIUnlockAspectRatio();
        gPortWidescreenEdgeScale = 1.0f;
    }
}

/* The settings or the window's shape changed what the frame should be. */
void portWidescreenFrame(void) {
    const float aspect = wanted();
    if (fabsf(aspect - sAspect) > 0.001f) {
        sAspect = aspect;
        apply();
    }
}

float portWidescreenAspect(void) {
    return sAspect > 0.0f ? sAspect : 4.0f / 3.0f;
}

int portWidescreenWide(void) {
    return sAspect > 0.0f;
}

void portWidescreenInit(void) {
    portWidescreenEnabled();
    portWidescreenFrame();
}

/* The settings page (settings_aurora.c): on or off while running. */
void portWidescreenSet(int on) {
    portWidescreenEnabled();
    if (sEnabled != (on != 0)) {
        sEnabled = on != 0;
        portWidescreenFrame();
    }
}

/* The cameras mirror mode flips (port/include/port/view.h): the world's
 * (the background pictures, the 3D and its effects and images, the fades,
 * whose wipes close on Mario where he is seen), not the offscreen and shadow
 * passes or the 2D one (the HUD, menus and message windows). */
static int mirrored(int cameraId) {
    switch (cameraId) {
    case CAMERA_BACKGROUND:
    case CAMERA_3D:
    case CAMERA_3D_EFFECTA:
    case CAMERA_3D_IMAGE:
    case CAMERA_3D_EFFECTB:
    case CAMERA_FADE:
    case CAMERA_FADE2:
        return portViewMirrored();
    default:
        return 0;
    }
}

static f32 sScaleX = 1.0f; /* GXSetProjectionScaleX's now */

void portWidescreenCamera(int cameraId) {
    f32 scale = 1.0f;
    if (sAspect > 0.0f) {
        switch (cameraId) {
        case CAMERA_BACKGROUND: /* full-screen background pictures */
        case CAMERA_FADE:       /* full-screen fades */
        case CAMERA_FADE2:
            break;
        default:
            scale = GAME_ASPECT / sAspect;
            break;
        }
    }
    if (mirrored(cameraId)) {
        scale = -scale;
    }
    if (scale != 1.0f || sScaleX != 1.0f) {
        GXSetProjectionScaleX(scale);
        sScaleX = scale;
    }
}

void portWidescreenCameraDone(void) {
    if (sScaleX < 0.0f) { /* what draws after the cameras is not mirrored */
        GXSetProjectionScaleX(-sScaleX);
        sScaleX = -sScaleX;
    }
}
