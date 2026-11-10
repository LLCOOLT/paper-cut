#ifndef PORT_GFX_GUARD_H
#define PORT_GFX_GUARD_H

/* The graphics guard (Aurora backend, port/src/aurora/gfx_guard_aurora.c), GitHub #39: a frame rate that
 * collapses (a phone's GPU at 1 fps, then the whole device lost) is caught and the game drops to safer
 * settings for the run instead of driving the device into a reset; Aurora's resources are watched for
 * growth that never stops.
 *
 * Once a presented frame (display_aurora.c), with its interval and work time in ms:
 * - the resources (AuroraGetResourceStats, port/patches/aurora/0063): textures alive and their memory,
 *   cached bind groups and samplers, the texture caches, and this frame's render passes, EFB copies,
 *   readbacks and texture uploads. Past a ceiling (a phone's or a computer's), or textures that grow
 *   for 30 s on end, it logs "[gfx] guard: ..." and flushes Aurora's decoded-texture cache (only it: the EFB
 *   copies the game still samples stay), at most every 5 s, the gap doubling (up to 5 min) while a flush
 *   leaves the level over;
 * - the watchdog: presents more than PORT_GFX_SLOW_MS apart for PORT_GFX_SLOW_FRAMES frames on end
 *   (outside a pause or a load: those are one long frame; not while the window is hidden, minimized or
 *   occluded, whose presents wait) step the run down ("[gfx] watchdog: ..."):
 *   first the costly options off for this run (supersampling, the ray-traced lighting, paper shadows and
 *   edges, bloom, the post filters, stereoscopic 3D, in-between frames, the second screen's live menu),
 *   pipelines compiled in the background (no_pop_in 0); then, still slow, the picture at 480 lines.
 *   settings.txt keeps the player's own values whatever is saved meanwhile: the declared ones through
 *   portSettingsExtraSaveAs, the frame rate, no_pop_in and resolution through portSettingsSetForRun (never
 *   saved while they are still at the run's value; one the player changes is theirs).
 *
 * $TTYD_GFX_STATS=N: a "[gfxstats]" line every N presented frames (1: every 60), what the guard reads.
 * $TTYD_GFX_WATCHDOG=0/1 turns the watchdog off or on (on by default, off under $TTYD_FAST: a loaded
 * test machine must not change what tests capture); $TTYD_GFX_WATCHDOG_FAKE=ms adds that much to every
 * interval it sees (tests: the steps without a slow GPU). $TTYD_GFX_GUARD_TEXTURE_MB: the texture
 * ceiling (tests). */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_GFX_SLOW_MS 250.0f
#define PORT_GFX_SLOW_FRAMES 20

void portGfxGuardFrame(float intervalMs, float workMs);
/* the watchdog's step so far: 0 none, 1 the costly options off, 2 also 480 lines */
int portGfxGuardStep(void);

#ifdef __cplusplus
}
#endif

#endif
