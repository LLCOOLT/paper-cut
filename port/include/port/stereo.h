#ifndef PORT_STEREO_H
#define PORT_STEREO_H

/* Stereoscopic 3D (Graphics > Stereoscopic 3D; docs/STEREOSCOPIC_3D.md): the scene drawn for a left and a right
 * eye in one frame, side by side, top and bottom or as an anaglyph. Aurora backend
 * (port/src/aurora/stereo_aurora.c, the GPU passes and the settings in port/settings/features/stereo.cpp). */

enum {
    PORT_STEREO_OFF,
    PORT_STEREO_SBS,       /* side by side, each eye keeping its shape (letterboxed in its half) */
    PORT_STEREO_SBS_HALF,  /* side by side, each eye squeezed to half the width (3D TVs) */
    PORT_STEREO_TB,        /* top and bottom, each eye keeping its shape */
    PORT_STEREO_TB_HALF,   /* top and bottom, each eye squeezed to half the height */
    PORT_STEREO_RED_CYAN,  /* anaglyph: left red, right cyan */
    PORT_STEREO_GREEN_MAGENTA,
    PORT_STEREO_MODES
};

/* The setting (stereo_3d): a PORT_STEREO_ mode. */
int portStereoMode(void);
/* camdrv's camDraw and interp_aurora.c's in-between frames: how many times the cameras are drawn this frame
 * (2 with stereo on, else 1), and around each time, eye 0 (left) then 1 (right). */
int portStereoEyes(void);
void portStereoEyeBegin(int eye, int eyes);
void portStereoEyeEnd(int eye, int eyes);
/* In the camera loop, after portWidescreenCamera: the camera moved to the eye (view and projection, put back at
 * portStereoEyeEnd). */
void portStereoCamera(int cameraId);
/* The two eyes put together into the frame (nothing when off), once a frame: before the game's GXCopyDisp and the
 * other captures (shot_aurora.c, vi_aurora.c), and at the latest at the frame's end, before its overlays. */
void portStereoCompose(void);

/* port/settings/features/stereo.cpp (C++, WebGPU): the frame so far kept as the left eye and the frame cleared
 * for the right (0 when it can't), and the two eyes drawn into the frame in a mode. */
int portStereoGpuLeftDone(void);
void portStereoGpuCompose(int mode);

/* interp_aurora.c: the game's frame drawn once more without changing anything (stereo's first eye), and between
 * the two eyes of an in-between frame; whether the draw list can be drawn twice. */
void portInterpPureBegin(void);
void portInterpPureEnd(void);
void portInterpPureRewind(void);
int portInterpListRedrawable(void);
/* paper_pc.c: the frame's paper shadows started again for the second eye. */
void portPaperEyeBegin(void);

#endif
