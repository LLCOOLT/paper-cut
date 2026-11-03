#ifndef PORT_AURORA_PC_H
#define PORT_AURORA_PC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pump Aurora events until a new frame can be started (exits on quit). */
void portAuroraBeginFrame(void);
/* Open the user's disc (port/src/aurora/disc_aurora.c): the argument,
 * $TTYD_DISC, or the launcher (the last disc chosen, or the file dialog,
 * with $TTYD_NO_LAUNCHER). 0 if there is none. */
int portAuroraOpenDisc(int argc, char* argv[], const char* userPath);
/* For the launcher (disc_aurora.c): the disc image at path opened as this
 * build's (else FALSE, the reason in why); the version of the image at path
 * (opened and closed; NULL if unreadable or not the game); the system's file
 * dialog (iOS: the document picker), FALSE if cancelled. */
int portAuroraOpenDiscPath(const char* path, char* why, size_t size);
struct PortVersionInfo;
const struct PortVersionInfo* portAuroraProbeDisc(const char* path);
int portAuroraPickDisc(char* out, size_t size);
/* The launcher (port/settings/launcher_ui.cpp): shown before the game when no
 * disc was given. Returns TRUE with a disc of this build's version in path to
 * play; starts another version's executable itself (and doesn't return);
 * FALSE if the player quit. finder: a disc image opened with the app (macOS
 * Finder), or "". */
int portLauncherRun(const char* userPath, const char* finder, char* path, size_t size);
/* The launcher's and settings page's fonts (port/settings/launcher_style.cpp), once Aurora is up. */
void portUiInit(void);
/* The keyboard's default keys for controller 1, before PADInit, and the
 * keyboard on after it (port/settings/settings_ui.cpp). */
void portKeyboardDefaults(void);
void portKeyboardActivate(void);
/* Before the game starts: waits, on a "Preparing graphics" screen, until Aurora
 * has built every pipeline it knows of (port/settings/features/warmup.cpp). */
void portPipelineWarmupWait(void);

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
/* Frame advance: this frame drawn again (FALSE if there is no scene). */
int portInterpRedraw(void);
/* The retrace's in-between frames are done: keep this frame's state. */
void portInterpFrameDone(void);

#ifdef __cplusplus
}
#endif

#endif
