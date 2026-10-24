# Scope: high refresh rates (120/144 Hz) by frame interpolation

Status: proposal, not started. An optional setting, off by default.

## The constraint

TTYD's logic runs once per frame at 60 Hz (NTSC): movement speeds, animation
steps, script waits (`WAIT_FRAMES`), timers and battle action commands all
count frames. Running the logic at 120 Hz would run the game at double speed
unless every per-frame constant in the game changed: not practical, and it
would break the GameCube behaviour the port is built to keep.

So the logic stays at 60 Hz and the port draws extra frames in between, by
interpolating what the game drew in two consecutive logic frames: the method
of the Ocarina of Time PC port (Ship of Harkinian) and the SM64 PC ports. On a
120 Hz display each logic frame is shown as the interpolated frame and the
real one; at 144 Hz or with VRR, at fractional positions.

Today, on a 120 Hz monitor, the port already shows each 60 Hz frame twice
(correct, not smoother).

## What a frame is made of (where interpolation has to happen)

- **Matrices**: the game loads position, normal, texture and projection
  matrices through GX directly (`GXLoadPosMtxImm`/`Indx`, `GXLoadNrmMtx*`,
  `GXLoadTexMtx*`, `GXSetProjection`): 443 call sites in 213 files. They all
  pass through the port's GX layer (Aurora), which is where they can be
  recorded and replaced, without touching game logic.
- **Skinned models**: characters are skinned on the CPU (`animPoseMain`,
  `src/driver/animdrv.c`) into vertex buffers, so their motion is in vertex
  data, not matrices.
- **Draw order and identity**: the game draws through `dispdrv` entries (a
  callback and its parameter per object, per camera), which give a stable
  key for "the same thing as last frame".
- **2D** (HUD, menus, message windows): screen-space; interpolating it is
  optional and mostly invisible.

## Design

1. **Present at the display rate.** Decouple presentation from the logic
   pacing (`port/src/aurora/vi_aurora.c`, `paceFrame`): logic advances on a
   60 Hz accumulator, presentation runs at the monitor's rate with vsync/VRR.
   Interpolation needs logic frames N-1 and N, so the image runs at most one
   logic frame (16.7 ms) behind; with interpolation off nothing changes.
2. **Record each logic frame's draws.** In the Aurora GX layer, keep the
   frame's draw list (Aurora already turns GX calls into a per-frame list of
   GPU draws), each draw tagged with its key: the current `dispdrv` entry
   (callback, parameter, camera) and its index within that entry, plus the
   matrices it used.
3. **Draw in-between frames.** For a fraction t, re-submit frame N's draw
   list with every matrix replaced by the interpolation (translation lerp,
   rotation slerp/decompose, or plain element lerp where safe) of the
   matching draw's matrix in N-1 and N. Draws with no match (spawned or
   removed this frame) and jumps (teleports, camera cuts, map changes: large
   deltas or a flag from `camdrv`/seq) use N's matrices as they are.
4. **Skinned vertices.** For CPU-skinned meshes, keep N-1's skinned buffer per
   pose and lerp positions and normals into a scratch buffer for the
   in-between frame (same vertex count and order by construction).
5. **Exclusions**: 2D layers, the EFB copies the game reads back (fades,
   screen effects, captures), particle systems if they look wrong; each can
   fall back to N.
6. **The setting**: "Frame rate: 60 (original) / Display rate (interpolated)"
   in Aurora's settings menu and config file, plus `$TTYD_INTERPOLATE` for
   tests. Default off.

## Effort (rough, focused work)

| Step | What | Size |
|---|---|---|
| 1 | Present at display rate, logic on a 60 Hz accumulator (frames shown twice at 120) | 1-2 days |
| 2 | Camera-only interpolation (view + projection): smooth camera motion | 2-3 days |
| 3 | Object matrices keyed by `dispdrv` entries | 1-2 weeks |
| 4 | Skinned-vertex interpolation | 3-5 days |
| 5 | Cuts and teleports, exclusions, effects, the setting, polish | ~1 week |

About 3-4 weeks in all; step 2 alone already removes most of the judder in
camera pans. Aurora changes go in as patches (`port/patches/aurora`).

## Risks

- Mismatched draws (an object's draw order changing between frames) give
  wobble or smearing: needs per-system keys where `dispdrv` is not enough.
- Effects that depend on exact frame content (EFB copies, screen-space
  distortion, the stage curtain) may need to be excluded.
- Half a logic frame of extra latency at 120 Hz (8 ms) when on: noticeable
  for battle action commands only at the margin; the setting can stay off.
- Cost: two to three draws per logic frame; fine on desktop GPUs, maybe not
  on older phones (the setting stays per-device).

## Testing

The logic is untouched, so every existing check holds as is: the golden
audio sweeps, battles, story sweeps and card scripts must not change with
the setting on. New: captures of in-between frames for a few scripted camera
pans and walks (compared by eye and against N-1/N bounds), and a sweep that
runs every map with interpolation on to catch crashes in the record/replay
path.
