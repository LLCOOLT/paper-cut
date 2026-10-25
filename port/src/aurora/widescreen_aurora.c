/* Widescreen: port/include/port/widescreen.h. */
#include <stdlib.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#include "driver/camdrv.h"
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
 * 1.2666666); the widened frame is 16:9. */
#define GAME_ASPECT (608.0f / 480.0f)
#define WIDE_ASPECT (16.0f / 9.0f)

static int sEnabled = -1;
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

static void apply(void) {
    if (sEnabled) {
        VILockAspectRatio(16, 9);
        /* a hair past the edge: s16 quad coordinates round toward 0 */
        gPortWidescreenEdgeScale = WIDE_ASPECT / GAME_ASPECT + 0.005f;
    } else {
        VIUnlockAspectRatio();
        gPortWidescreenEdgeScale = 1.0f;
    }
}

void portWidescreenInit(void) {
    if (portWidescreenEnabled()) {
        apply();
    }
}

/* The settings page (settings_aurora.c): on or off while running. */
void portWidescreenSet(int on) {
    portWidescreenEnabled();
    if (sEnabled != (on != 0)) {
        sEnabled = on != 0;
        apply();
    }
}

void portWidescreenCamera(int cameraId) {
    f32 scale;
    if (!portWidescreenEnabled()) {
        return;
    }
    switch (cameraId) {
    case CAMERA_BACKGROUND: /* full-screen background pictures */
    case CAMERA_FADE:       /* full-screen fades */
    case CAMERA_FADE2:
        scale = 1.0f;
        break;
    default:
        scale = GAME_ASPECT / WIDE_ASPECT;
        break;
    }
    GXSetProjectionScaleX(scale);
}
