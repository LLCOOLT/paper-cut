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

const char* portTattleViewHint(void) {
    return portTrGame("Stick: turn   L, R: in and out   A: as it was");
}
