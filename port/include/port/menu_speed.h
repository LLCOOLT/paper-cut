#ifndef PORT_MENU_SPEED_H
#define PORT_MENU_SPEED_H

/* Menu speed (port/settings/features/menu_speed.cpp): the Start menu's
 * pages and tabs slide in and out in fewer frames. src/window/win_root.c
 * passes each slide's length (in frames, already at the frame rate) through
 * portMenuFrames: Original as it is, Fast in half, Instant in an eighth
 * (never under one frame). */
#ifdef __cplusplus
extern "C" {
#endif

int portMenuFrames(int frames);

#ifdef __cplusplus
}
#endif

#endif
