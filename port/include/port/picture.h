#ifndef PORT_PICTURE_H
#define PORT_PICTURE_H

/* Picture effects over the presented picture (port/src/aurora/picture_aurora.c; Aurora's
 * aurora_set_picture_effects, port/patches/aurora/0023). Only how the game is seen, never what it plays out:
 * nothing here reaches the game's state, and they are each player's own online.
 *
 * The paper filters: the Picture filter setting (post_filter, Graphics) 4 and 5, a cut-paper outline and
 * folding light, alongside its CRT, paper grain and sharpen (those three are the present resample's own).
 *
 * Photo mode's look (port/settings/features/photo_mode.cpp): a filter, a frame and depth of field, while it is
 * on; its filter, when one of the paper ones, in place of the setting's. */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_PHOTO_FILTER_NONE,
    PORT_PHOTO_FILTER_SEPIA,
    PORT_PHOTO_FILTER_MONO,
    PORT_PHOTO_FILTER_NOIR,
    PORT_PHOTO_FILTER_VIVID,
    PORT_PHOTO_FILTER_WARM,
    PORT_PHOTO_FILTER_COOL,
    PORT_PHOTO_FILTER_FADED,
    PORT_PHOTO_FILTER_CUT_PAPER,
    PORT_PHOTO_FILTER_FOLDING_LIGHT,
    PORT_PHOTO_FILTER_COUNT,
};
enum { PORT_PHOTO_FRAME_COUNT = 5 }; /* none, paper border, stamp, film strip, storybook */
enum { PORT_PHOTO_DOF_COUNT = 3 };   /* off, soft, strong */

/* The post filter's paper modes: post_filter's value (0-5) to the resample's (0-3) and the paper mode
 * (0 none, 1 cut paper, 2 folding light). */
int portPictureResampleFilter(int postFilter);
void portPictureSetPaper(int paper);
/* Photo mode's look while it is on (on 0: none); the focus 0-1 from the picture's top left. */
void portPictureSetPhoto(int on, int filter, int frame, int dof, float focusX, float focusY);
/* Once a frame (portDisplayFrame): what changed, to Aurora. */
void portPictureFrame(void);

#ifdef __cplusplus
}
#endif

#endif
