/* Picture effects (port/include/port/picture.h): the Picture filter's paper modes and photo mode's look, given
 * to Aurora together (aurora_set_picture_effects) when they change. */
#include <string.h>
#include <aurora/picture.h>
#include "port/picture.h"

static int sPaper;     /* the setting's: 0 none, 1 cut paper, 2 folding light */
static struct {
    int on, filter, frame, dof;
    float focusX, focusY;
} sPhoto;
static AuroraPictureEffects sSent;
static int sSentOnce;

int portPictureResampleFilter(int postFilter) {
    return postFilter >= 0 && postFilter <= 3 ? postFilter : 0;
}

void portPictureSetPaper(int paper) {
    sPaper = paper >= 0 && paper <= 2 ? paper : 0;
}

void portPictureSetPhoto(int on, int filter, int frame, int dof, float focusX, float focusY) {
    sPhoto.on = on;
    sPhoto.filter = filter;
    sPhoto.frame = frame;
    sPhoto.dof = dof;
    sPhoto.focusX = focusX;
    sPhoto.focusY = focusY;
}

/* r, g, b rows of three weights and an offset */
static void setLook(AuroraPictureEffects* e, const float m[12]) {
    memcpy(e->look, m, sizeof(e->look));
    e->lookOn = 1;
}

/* saturation s and contrast k round mid grey, then an offset */
static void saturated(float out[12], float s, float k, float off) {
    static const float kLuma[3] = {0.299f, 0.587f, 0.114f};
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            out[i * 4 + j] = k * ((1.0f - s) * kLuma[j] + (i == j ? s : 0.0f));
        }
        out[i * 4 + 3] = 0.5f * (1.0f - k) + off;
    }
}

static void photoLook(AuroraPictureEffects* e) {
    static const float kSepia[12] = {0.393f, 0.769f, 0.189f, 0.0f, 0.349f, 0.686f, 0.168f, 0.0f,
                                     0.272f, 0.534f, 0.131f, 0.0f};
    static const float kWarm[12] = {1.08f, 0.0f, 0.0f, 0.02f, 0.0f, 1.02f, 0.0f, 0.005f, 0.0f, 0.0f, 0.86f, 0.0f};
    static const float kCool[12] = {0.90f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.01f, 0.0f, 0.0f, 1.10f, 0.03f};
    float m[12];
    int i;
    switch (sPhoto.filter) {
    case PORT_PHOTO_FILTER_SEPIA:
        setLook(e, kSepia);
        e->vignette = 0.35f;
        break;
    case PORT_PHOTO_FILTER_MONO:
        saturated(m, 0.0f, 1.0f, 0.0f);
        setLook(e, m);
        break;
    case PORT_PHOTO_FILTER_NOIR:
        saturated(m, 0.0f, 1.45f, 0.0f);
        setLook(e, m);
        e->vignette = 0.55f;
        break;
    case PORT_PHOTO_FILTER_VIVID:
        saturated(m, 1.45f, 1.08f, 0.0f);
        setLook(e, m);
        break;
    case PORT_PHOTO_FILTER_WARM:
        setLook(e, kWarm);
        e->vignette = 0.15f;
        break;
    case PORT_PHOTO_FILTER_COOL:
        setLook(e, kCool);
        break;
    case PORT_PHOTO_FILTER_FADED:
        saturated(m, 0.7f, 0.80f, 0.04f);
        for (i = 0; i < 3; i++) {
            m[i * 4 + 3] += i == 0 ? 0.03f : i == 2 ? -0.02f : 0.0f; /* a little warm, as old prints */
        }
        setLook(e, m);
        e->vignette = 0.3f;
        break;
    case PORT_PHOTO_FILTER_CUT_PAPER:
        e->paper = 1;
        break;
    case PORT_PHOTO_FILTER_FOLDING_LIGHT:
        e->paper = 2;
        break;
    default:
        break;
    }
    e->frame = sPhoto.frame > 0 && sPhoto.frame < PORT_PHOTO_FRAME_COUNT ? sPhoto.frame : 0;
    if (sPhoto.dof > 0) {
        e->focusX = sPhoto.focusX;
        e->focusY = sPhoto.focusY;
        e->focusRadius = sPhoto.dof == 1 ? 0.22f : 0.15f;
        e->blur = sPhoto.dof == 1 ? 5.0f : 10.0f;
    }
}

void portPictureFrame(void) {
    AuroraPictureEffects e;
    memset(&e, 0, sizeof(e));
    e.paper = sPaper;
    if (sPhoto.on) {
        photoLook(&e);
    }
    e.paperStrength = e.paper != 0 ? 1.0f : 0.0f;
    if (sSentOnce && memcmp(&e, &sSent, sizeof(e)) == 0) {
        return;
    }
    sSent = e;
    sSentOnce = 1;
    aurora_set_picture_effects(&e);
}
