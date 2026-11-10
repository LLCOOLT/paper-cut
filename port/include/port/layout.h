#ifndef PORT_LAYOUT_H
#define PORT_LAYOUT_H

/* Where the picture and the on-screen controls go in the window (Aurora
 * backend, port/src/aurora/layout_aurora.c), in window points. Held
 * upright (taller than wide, with on-screen controls), the picture is at
 * the top and the controls below it; otherwise the picture is centered and
 * the controls are over the window's safe area, in the margins beside it. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortLayout {
    int portrait;
    float picX, picY, picW, picH; /* the game's picture */
    float ctrlX, ctrlY, ctrlW, ctrlH; /* the on-screen controls' area */
} PortLayout;

/* Once per drawn frame: recomputes the layout and moves the picture there. */
void portLayoutFrame(void);
/* The layout as of the last frame; 0 before there is a window. */
int portLayoutGet(PortLayout* out);
/* The settings' orientation changed (iOS: which ways the screen turns). */
void portLayoutOrientation(int orientation);

/* The screen's real size: window points per millimetre (Android's display
 * metrics, the iPhone's or iPad's own pixels an inch, the desktop's display
 * scale, the Steam Deck's panel; or $TTYD_TOUCH_DEVICE's). */
float portLayoutPointsPerMm(void);
/* The on-screen controls' size unit as the settings ask for it, in window
 * points (portTouchUnitMm at touch_size 100 %), before it is fitted to the
 * controls' area (port/touch/touch_overlay.cpp). */
float portLayoutTouchUnit(void);
/* The size unit at touch_size 100 %, in mm, for a screen whose short side is
 * that many mm: PORT_TOUCH_UNIT_PHONE_MM on a phone-sized screen (a short side
 * of PORT_TOUCH_PHONE_SIDE_MM or less: phones, a 6 in handheld),
 * PORT_TOUCH_UNIT_MM on a tablet's (PORT_TOUCH_TABLET_SIDE_MM or more), in
 * step between them. In real mm on every screen, so a phone's are never as
 * small as 0.4.1's were on Android, nor a tablet's as large as on an iPad. */
float portTouchUnitMm(float shortSideMm);
#define PORT_TOUCH_UNIT_MM 68.f       /* the A button 11.6 mm across, the stick's gate 24.5 mm, L and R 9 mm tall */
#define PORT_TOUCH_UNIT_PHONE_MM 61.f /* the A button 10.4 mm across, the stick's gate 22 mm, L and R 8.1 mm tall */
#define PORT_TOUCH_PHONE_SIDE_MM 80.f   /* (an iPhone Pro Max's short side is 73 mm, a 6 in 1080p handheld's 75) */
#define PORT_TOUCH_TABLET_SIDE_MM 110.f /* (an iPad mini's is 116 mm) */

/* An iPhone's or iPad's screen in pixels an inch, which iOS doesn't say: from
 * its model (hw.machine, "iPhone18,1"; the simulator's
 * $SIMULATOR_MODEL_IDENTIFIER) where it is one of a kind (the minis, the
 * Plus phones), else from what kind it is: a 3x iPhone 460, a 2x one 326, an
 * iPad mini (a short side of 744 points or less) 326, any other iPad 264.
 * scale: UIScreen's (2 or 3); shortSidePts: the screen's short side in points.
 * Points an inch are this over UIScreen's nativeScale. */
float portIosScreenPpi(const char* model, int pad, int scale, float shortSidePts);

/* $TTYD_TOUCH_DEVICE=<w>x<h>@<dpi>[,<left>,<top>,<right>,<bottom>] (tests): a
 * phone's screen faked on the desktop: the window that size in device pixels
 * (halved until it fits 1600x1000, the dpi with it), that many pixels an
 * inch, the safe area inset by those pixels, and the on-screen controls shown.
 * 1 with it set: the window's size (points) in *w, *h (either may be NULL).
 * $TTYD_TOUCH_IOS=<model>@<scale> with it ("iPhone18,1@3"): the pixels an inch
 * are the ones portIosScreenPpi gives that iPhone or iPad instead.
 * $TTYD_TOUCH_UNIT_MM=<mm> with it: that size unit (at touch_size 100 %)
 * instead of portTouchUnitMm's, to compare with another release's. */
int portTouchDevice(int* w, int* h);

#ifdef __cplusplus
}
#endif

#endif
