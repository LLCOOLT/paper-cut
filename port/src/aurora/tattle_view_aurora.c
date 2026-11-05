/* The Tattle log's model viewer (port/include/port/tattle_view.h): the turn and zoom of the Journal's Tattle
 * card's enemy, from the controller, eased so it moves smoothly. */
#include "port/tattle_view.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/tr.h"
#include "battle/battle_unit.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include <dolphin/pad.h>
#include <math.h>

#define TURN_RATE 0.055f /* radians a frame at full tilt */
#define ZOOM_RATE 1.025f /* times a frame held */
#define ZOOM_MIN 0.7f
#define ZOOM_MAX 2.6f
#define STICK_DEAD 14

static int sOpen, sId;
static float sYaw, sZoom = 1.0f;       /* where it is going */
static float sShownYaw, sShownZoom = 1.0f; /* where it is drawn */

void portTattleViewSet(int on, int id) {
    sOpen = on;
    sId = id;
    sYaw = sShownYaw = 0.0f;
    sZoom = sShownZoom = 1.0f;
}

int portTattleViewOn(void) {
    return sOpen && gp != NULL && !gp->inBattle && sId != UNIT_SMORG && !portOnlineActive() && !portReplayActive();
}

static float axis(signed char v) {
    if (v > -STICK_DEAD && v < STICK_DEAD) {
        return 0.0f;
    }
    return (float)(v > 0 ? v - STICK_DEAD : v + STICK_DEAD) / (72.0f - STICK_DEAD);
}

void portTattleViewInput(unsigned int buttons, unsigned int pressed, signed char stickX, signed char substickX) {
    float x;
    if (!portTattleViewOn()) {
        return;
    }
    x = axis(stickX) + axis(substickX);
    if (buttons & PAD_BUTTON_LEFT) {
        x -= 1.0f;
    } else if (buttons & PAD_BUTTON_RIGHT) {
        x += 1.0f;
    }
    x = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
    sYaw += x * TURN_RATE;
    if (buttons & PAD_TRIGGER_R) {
        sZoom *= ZOOM_RATE;
    }
    if (buttons & PAD_TRIGGER_L) {
        sZoom /= ZOOM_RATE;
    }
    sZoom = sZoom < ZOOM_MIN ? ZOOM_MIN : sZoom > ZOOM_MAX ? ZOOM_MAX : sZoom;
    if (pressed & PAD_BUTTON_A) {
        /* back as it was: the nearest whole turn, so it turns the short way home */
        sYaw = roundf(sShownYaw / 6.2831853f) * 6.2831853f;
        sZoom = 1.0f;
        psndSFXOn(SFX_NAME_WORD(0x20035));
    }
    sShownYaw += (sYaw - sShownYaw) * 0.3f;
    sShownZoom += (sZoom - sShownZoom) * 0.3f;
}

void portTattleViewGet(float* yaw, float* zoom) {
    *yaw = sShownYaw;
    *zoom = sShownZoom;
}

/* The card's picture frame (w/<lang>/win.tpl's 0x37, 216 x 160): the inner edge of its drawn border, at the
 * most, from each side. The model was once drawn inside a smaller box, so that the largest copy of the soft
 * edge (monosiri_disp's eight texture stages, the smallest scale 0.72 about the middle) stayed inside the
 * frame: zoomed in, a rectangle well inside the frame cut it. Now the card itself draws the picture only
 * inside the border (portTattleViewBorder), soft edge and all, and the model fills the frame. */
#define FRAME_LEFT 15.0f
#define FRAME_RIGHT 13.0f
#define FRAME_TOP 16.0f
#define FRAME_BOTTOM 9.0f

void portTattleViewScissor(unsigned int tw, unsigned int th, unsigned int* x, unsigned int* y, unsigned int* w,
                           unsigned int* h) {
    const float sx = (float)tw / 216.0f, sy = (float)th / 160.0f; /* (the frame's size, in case another's) */
    const float x0 = floorf(FRAME_LEFT * sx), y0 = floorf(FRAME_TOP * sy);
    *x = (unsigned int)x0;
    *y = (unsigned int)y0;
    *w = (unsigned int)(ceilf(tw - FRAME_RIGHT * sx) - x0);
    *h = (unsigned int)(ceilf(th - FRAME_BOTTOM * sy) - y0);
}

void portTattleViewBorder(float* left, float* top, float* right, float* bottom) {
    *left = FRAME_LEFT;
    *top = FRAME_TOP;
    *right = FRAME_RIGHT;
    *bottom = FRAME_BOTTOM;
}

const char* portTattleViewHint(void) {
    return portTrGame("Stick: turn   L, R: in and out   A: as it was");
}
