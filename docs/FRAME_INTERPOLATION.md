# High refresh rates (120/144/240 Hz) by frame interpolation

Status: steps 1 and 2 done (Aurora backend). An optional setting, off by
default: Frame rate on the settings page (F1), `framerate=` in
`settings.txt` in Aurora's user folder (by the memory cards), or
`$TTYD_FRAMERATE`: `60` (off), `120`, `144`, `240`, ... or
`display` (the monitor's rate). The rate is shown as the nearest multiple of
60, at most 8 frames per game frame.

## The constraint

TTYD's logic runs once per frame at 60 Hz (NTSC): movement speeds, animation
steps, script waits (`WAIT_FRAMES`), timers and battle action commands all
count frames. Running the logic faster would run the game faster, so the
logic stays at 60 Hz and the port draws extra frames in between.

## How it works (`port/src/aurora/interp_aurora.c`)

The game draws (`marioStDisp` -> `camDraw`), then runs its logic. So at the
retrace (`VIWaitForRetrace`, `vi_aurora.c`), the port holds the state it has
just shown (frame N, kept in a snapshot) and the next one (N+1, live, after
`viPostCallback` has advanced the render clocks). Instead of recording and
replaying GX draws, an in-between frame **draws the live scene again** with
the game's own draw code, after blending toward the snapshot:

- **The render clocks** (`renderTime`, `renderFieldTime`, `systemLv0-3Time`):
  skinned models are posed from them (`animPoseMain` computes the pose from
  the clock, it does not step it), so characters are posed at the in-between
  moment: smooth animation without touching vertex data.
- **Every camera's** view and projection matrices, eye and target.
- **The positions** of Mario, the partners and the field NPCs: the draw code
  builds their matrices from these.

Then it puts everything back. Anything that jumped is drawn as it is now (a
camera moving more than 250 units, an actor more than 150, a map or battle
change, the NPC table set up again). The draw repeats `camDraw` without
`camLetterBox` (which advances the cutscene bars) and keeps the draw list as
the game left it (its own frame sorts it with an unstable sort, then
`dispReInit`).

Frame timing: each game frame is presented at its field's start and the
in-between frames evenly through the field (`SDL_DelayPrecise`). In-between
frames run after the game's frame N+1 is computed, so at 120 Hz the image is
half a game frame (8 ms) behind the logic.

## Purity: in-between frames must not change the game

The logic never runs for these frames, but the game's draw code also writes
state, which a redraw must not change:

- **Caches**: poses, skinned vertex buffers, shadow raycasts, matrices. These
  are recomputed by the next real frame before anything reads them.
  `animPoseMain`'s incremental keyframe deltas add up the same either way.
- **Counters stepped per draw**: the HUD's blink timer (`statusWinDisp`) and
  the message window's background scroll (`windowdrv.c`). These skip their
  step while `portInterpInBetween()` is true.
- **Work stepped or one-shots taken in draw code**: the battle menu ring, unit
  blur trails, the glyph pop, the sys/parlor titles, the Glitz Pit screens,
  water's first-draw flag, the parlor timer. These are kept with
  `portInterpKeep(p, size)` at the top of the draw and put back after the
  frame. Captures (`imgCapture_Prim`) and Luigi's pose flip skip in-between
  frames. A frame whose draw makes extdrv's textures gets no in-between
  frames.
- **Poses**: the pose table and each drawn pose's keyframe, vertex and world
  buffers are kept and restored (`animPoseInterpBegin/End`, animdrv.c). Pose
  state is only a cache when every real frame draws the pose again, and the
  paper turn steps per draw.
- **Random numbers**: text shakes its glyphs with `irand` while drawing. The
  game now uses the GameCube's own `rand` (MSL's LCG, `ttyd_rand`), the same
  sequence on every host, and its state is put back after the frame.
- **The smart heap**: frame-temporary blocks (`smartAlloc` type 3) come from
  a scratch arena in in-between frames, so the heap's layout, compaction and
  file eviction are as without them.
- **The draw list's order**: kept, see above.
- **The `$TTYD_FAST` virtual clock**: it advances 1 us per read, so it is put
  back after the in-between frames.

`$TTYD_INTERP_AUDIT` (Windows, `interp_audit.c`) finds such writes during in-between frames:
- the game heaps are made read-only and each write is trapped and reported by function, line and two callers;
- the executable's globals are diffed and reported by name.

It runs on single runs or across a sweep:

    TTYD_INTERP_AUDIT=1 TTYD_FRAMERATE=120 python tools/port/map_sweep.py \
        build-pc-jp-aurora/ttyd-jp.exe data/G8MJ01 --disc <iso> --collect '\[interp-audit\]'

Check: with the setting on, every capture of a real frame and the whole trace
are byte-identical to the setting off (the gor_01 walk, 4:3 and 16:9). Across
every map's last frame, 10 of the 256 maps whose captures repeat between two
runs with the setting off still differ slightly at 120 Hz (the Glitz Pit
screen, Pianta Parlor, Excess Express, a few others): next for the audit. `$TTYD_SHOT_INBETWEEN=k` captures the
k-th in-between frame after each `$TTYD_SHOT` frame instead (a walk in
`gor_01`: the scene sits exactly halfway, 3 px from each real frame 6 px
apart).

## Next steps

| Step | What | State |
|---|---|---|
| 1 | Present at the display rate | done |
| 2 | Camera interpolation (+ clocks, so skinned poses; actor positions) | done |
| 3 | Other moving objects: battle units, items, map objects (`mobj`), effects | next |
| 4 | Skinned vertices | covered by the clocks |
| 5 | A settings page; per-system exclusions; VRR | |

Battle units, items, effects and map parts move at 60 Hz today: smooth
camera, smooth characters, choppy particles and moving platforms. Each needs its
positions added to the snapshot (step 3).

## Risks

- A draw function that steps state not yet found by the audit (it reports
  only what the swept content draws).
- Cost: the scene is drawn 2-4 times per game frame; fine on desktop GPUs,
  maybe not on older phones (the setting is per device).
