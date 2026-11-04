#ifndef PORT_DISPLAY_H
#define PORT_DISPLAY_H

/* Display options (Aurora backend, port/src/aurora/display_aurora.c), each a
 * setting of its own, off by default (port/settings/features/display.cpp):
 * aniso (anisotropic filtering), lod_bias (texture detail), supersample
 * (rendering 2-4x the window's size, area-downsampled), post_filter (CRT,
 * paper grain, sharpen over the picture), vrr (present without waiting for
 * vblank, for variable refresh displays), perf_overlay (a frame-time graph),
 * hdr (HDR output at a paper-white brightness, where supported).
 * The aspect ratio and HUD scale: port/widescreen.h, port/hud.h.
 *
 * $TTYD_PACING_LOG=<file>: each presented frame's interval and work time
 * (ms, CSV), to measure the pacing. */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_LOD_GAME,  /* the game's own */
    PORT_LOD_SHARP, /* -0.5 */
    PORT_LOD_SHARPER,
    PORT_LOD_SHARPEST,
    PORT_LOD_SOFT, /* +0.5 */
    PORT_LOD_COUNT,
};

#define PORT_PERF_SAMPLES 240

void portDisplayInit(void);
/* Texture upscaling from its setting (upscale_aurora.c), at start: it applies
 * to textures loaded after it, so a change waits for the next start. */
void portTextureUpscaleStart(void);
/* The frame loop (vi_aurora.c): once a game or in-between frame is begun
 * (aurora_begin_frame returned), when its work is done (before the pacing
 * wait), and when it is about to be presented. */
void portDisplayFrameStart(void);
void portDisplayWorkDone(void);
void portDisplayFrame(void);
/* The performance overlay's setting, and the last max frames' timing, oldest
 * first: the time between presents and the part spent computing and drawing,
 * in ms. Returns how many. */
int portPerfOverlay(void);
int portPerfSamples(float* interval, float* work, int max);
/* port/settings/features/perf_overlay.cpp: drawn with the settings page's
 * overlays (ImGui), when on. */
void portPerfOverlayDraw(void);
/* The input display (setting input_display: 0 off, 1-4 a corner TL TR BL BR;
 * port/src/common/input_display_pc.c): controller chan as the game last read
 * it, its buttons (PAD_BUTTON_*), the stick and C-stick (x, y, x, y) and the
 * analog L and R; 0 when no controller is there. */
int portInputDisplayPad(int chan, unsigned short* buttons, signed char* sticks, unsigned char* triggers);
/* Sharp text (setting sharp_text, port/settings/features/sharp_text.cpp):
 * fontmgr.c's glyph sheet once loaded, and once a frame to follow the
 * setting. */
void portFontSheet(const void* texels, int width, int height, int format);
void portSharpTextFrame(void);
/* HDR output (settings hdr, hdr_paper_white in nits, hdr_boost in percent;
 * Aurora's aurora_set_hdr, port/patches/aurora/0014): given to Aurora once a
 * frame (portDisplayFrame) and when the settings page changes them.
 * portHdrSupported: the window's display has HDR on and the graphics backend
 * can present it (Direct3D 12/11, Metal); portHdrActive: presenting in HDR
 * now. Off by default; unsupported or off, output is SDR as before. */
void portHdrApply(void);
int portHdrSupported(void);
int portHdrActive(void);

#ifdef __cplusplus
}
#endif

#endif
