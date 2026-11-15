#ifndef PORT_INTERP_REPLAY_H
#define PORT_INTERP_REPLAY_H

/* Frame interpolation by replay (Aurora backend; port/src/aurora/interp_replay_aurora.c,
 * docs/FRAME_INTERPOLATION.md): the game's draw runs once a game frame and is recorded (Aurora keeps its GX
 * commands); the in-between frames of a higher frame rate, and stereoscopic 3D's other eye, are that recording
 * replayed with its matrices, colours and vertices blended toward the frame before. No game code runs for them. */

#ifdef __cplusplus
extern "C" {
#endif

/* The replay path is on (Graphics setting interp_replay, or $TTYD_INTERP_MODE=replay|redraw) and there is
 * something to replay (a frame rate above 60, or stereoscopic 3D). */
int portInterpReplayOn(void);

/* dispdrv.c's dispDraw, before each draw-list entry: what the GX commands that follow belong to (their identity
 * from one frame to the next), and the check that no entry is drawn twice in a game frame. */
void portInterpEntry(int cameraId, const void* entry, void* callback, void* param);
/* camdrv.c: what follows is this camera's (outside its entries). */
void portInterpCamera(int cameraId);

/* A port pass inside the game's frame (camDraw's lighting and bloom): left out of the recording, and run again
 * at its place in each replay (port code, never the game's). */
enum { PORT_INTERP_HOOK_LIGHT_BLOOM = 1 };
void portInterpHookBegin(void);
void portInterpHookEnd(int id);

/* vi_aurora.c: the recording ended at the retrace (drawn: the game drew this frame), and begun again for the next
 * game frame. */
void portInterpRecordStop(int drawn);
void portInterpRecordStart(void);
/* The retrace's frames by replay: the in-between frames before the game's own (and stereo's left eye), each
 * presented by present(k, frames) at its time (present(k, -frames) first asks whether to draw it: 0, too late);
 * the game's frame left in the open Aurora frame for the caller to present. Returns the in-between frames shown. */
typedef int (*PortInterpPresent)(int k, int frames);
int portInterpReplayFrames(int frames, PortInterpPresent present, void (*beginFrame)(void));
/* Frame advance and such holds: the last frame replayed as it was (no game code). 0 if there is none. */
int portInterpReplayRedraw(void);
/* A hold (frame advance, a lost controller) drawing the game's frame again with its own code (no recording to
 * replay): not a second draw of a game frame for the draw-once check. */
void portInterpDrawOnceHold(void);
/* Save states and such: the scene changed under the recordings. */
void portInterpReplayForget(void);
/* shot_aurora.c's GXCopyDisp while a frame is recorded: 1, its capture (and stereo's composing of the eyes) wait
 * until the frame's picture is whole and next to be shown (after stereo's left eye and the in-between frames). */
int portInterpReplayDeferCopyDisp(void);

/* port/settings/features/interp_hold.cpp: the game's picture kept and the frame cleared; the frame cleared; the
 * kept picture drawn into the frame. */
int portInterpGpuHold(void);
void portInterpGpuClear(void);
int portInterpGpuShowHeld(void);

/* stereo: the left eye from the right's draw (stereo_aurora.c); the right eye kept first, a frame composing more
 * than one picture, a frame whose picture is composed already (port/settings/features/stereo.cpp) */
int portStereoReplayShift(int cameraId, float* viewDx, float* projK, float* orthoShift);
int portStereoGpuRightDone(void);
void portStereoGpuNewPicture(void);
void portStereoGpuSkipCompose(void);

#ifdef __cplusplus
}
#endif

#endif
