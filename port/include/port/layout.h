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

#ifdef __cplusplus
}
#endif

#endif
