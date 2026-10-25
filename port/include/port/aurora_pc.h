#ifndef PORT_AURORA_PC_H
#define PORT_AURORA_PC_H

/* Pump Aurora events until a new frame can be started (exits on quit). */
void portAuroraBeginFrame(void);
/* Open the user's disc (port/src/aurora/disc_aurora.c): the argument,
 * $TTYD_DISC, the last one chosen, or the file dialog. 0 if there is none. */
int portAuroraOpenDisc(int argc, char* argv[], const char* userPath);

/* Frames drawn between the game's 60 Hz frames (interp_aurora.c): the
 * display rate's multiple of 60 from $TTYD_FRAMERATE, 1 when off. */
int portInterpFrames(void);
/* The frame rate setting (settings_aurora.c). */
void portInterpSetRate(const char* rate);
/* The settings page (port/settings/settings_ui.cpp): an SDL event (F1, F11,
 * a controller's Back, a three-finger tap, the focus), and its draw, over
 * each presented frame. */
void portSettingsEvent(const void* sdlEvent);
void portSettingsDraw(void);
/* There is a frame to blend toward (not a map or battle change). */
int portInterpReady(void);
/* Draws the scene t (0..1) of the way from the frame shown to the next one,
 * leaving the game's state as it was. FALSE if there is nothing to blend. */
int portInterpDraw(float t);
/* The retrace's in-between frames are done: keep this frame's state. */
void portInterpFrameDone(void);

#endif
