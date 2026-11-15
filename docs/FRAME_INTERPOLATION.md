# High refresh rates (120/144/240 Hz) by frame interpolation

Status: steps 1 to 3 done (Aurora backend). An optional setting, off by
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

## Replay: the game draws once a frame (`port/src/aurora/interp_replay_aurora.c`)

Redrawing (below) runs the game's draw code again for every in-between frame and for stereo's
second eye, so draw code with side effects ran twice (Art Attack's capture was freed under its
second draw). With replay the game's draw code runs exactly once per game frame:

- Aurora records the frame (patch 0078: every FIFO byte, and a list of events: matrix,
  projection and channel colour loads, the TEV colours, each draw's direct vertices, each vertex
  array as its draw read it; arrays unchanged since the frame before are shared with it).
  `dispDraw` tags each draw-list entry's commands with its identity (callback, parameter,
  camera).
- At the retrace the game's picture is kept; each in-between frame is the recording fed through
  Aurora again, every event paired with the frame before's by (identity, kind, register,
  occurrence) and blended: position matrices by rotation (slerp), scale and translation; the
  rest element by element. An entry whose count of something changed (a particle born or gone)
  is paired by place instead. A camera cut, a map change or a jump is drawn as recorded.
- Stereoscopic 3D: the game draws the right eye; the left is the recording replayed with the
  eye's shift on the 3D cameras' matrices.
- A replay runs no game code, asks for no readbacks, makes its EFB copies into textures of its
  own and defers texture frees until its recording is let go. Port passes inside the frame
  (ray-traced lighting, bloom) are left out of the recording and run again at their place.
- A frame Aurora can't replay (memory moved under its draws, a draw outside the FIFO) gets no
  in-between frames (logged once per reason). Past the memory cap (128 MB a frame, 24 MB on
  phones) a frame isn't replayed; a second of that turns the redraw path on for the run.

When: the `interp_replay` setting in `settings.txt` (0 redraw, 1 replay, 2 automatic, the
default). Automatic replays only stereo's second eye at 60 Hz; every higher frame rate redraws.
A replayed frame costs Aurora's command processor the whole frame again: on an RTX 3090 at 120 Hz
in Rogueport a frame shown took about 7.7 ms of its 8.33 ms slot by replay, against about 2.3 ms
by redraw. The redraw path's draw code with side effects is kept to once a game frame
(`portInterpInBetween`, `portInterpKeep`, the `interp_audit` scenarios below).

With vsync off and a frame rate above the display's refresh (120 Hz on a 60 Hz screen), frames
are presented as they come (Immediate, as the variable refresh rate setting does), not Mailbox:
Direct3D 12 could hold a Mailbox present to the screen's refresh, Aurora's next frame then waited
for a frame slot, and the game's frame came about 7 ms late after each in-between frame (a
1-2/15 ms judder in some runs, whichever path drew the in-between frames).

Tests: `TTYD_INTERP_MODE=replay|redraw` (over the setting);
`TTYD_DRAWONCE_STRICT=1` stops a run when a draw-list entry is drawn twice in a game frame (the
check runs on every run, and `[drawonce] FAIL` fails any test pass scenario; the automatic
mode's own redrawing is counted, not failed); `TTYD_INTERP_STATS=1` logs the recording's size
and the pairing and replay times; `TTYD_LATENCY_LOG=1` the time from a frame's logic to its
picture shown.

## Redraw (`port/src/aurora/interp_aurora.c`): the in-between frames of 120 Hz and up, and stereo with them

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
- **Work stepped or one-shots taken in draw code**: the battle menu ring and its lists (scroll, rows, arrows), unit
  blur trails, the glyph pop, the sys/parlor titles, the Glitz Pit screens,
  water's first-draw flag, the parlor timer, every action command's icon
  slide (one wrapper in `battle_ac.c` for all of `ac_*.c`), Clock Out's
  gauge pulse, the aiming command's afterimages, the shops' coin and Shine Sprite
  tallies, the coin window's count (the inn, the shops), the Pianta Parlor's token count, the opening storybook's text
  window fade, the title's Press Start pulse, the file select's mask, the
  level-up blink, the yes/no cursor's glide, the TEC soft fade, the motion
  blur's capture switch, Fahr Outpost's cannon sight, the Great Tree's Puni
  marks and three Paper Mario 64 effects (smoke colour, flame scroll, star
  trail). These are kept with `portInterpKeep(p, size)` at the top of the draw
  and put back after the frame. Sounds started in a draw (the coin tallies'
  ticks, the parlor count, the audience's throw warning) play only in the game's own frame. Vivian's ribbon
  builds its spline on the stack in such a frame (no map heap blocks). Captures (`imgCapture_Prim`, Art Attack's and Supernova's screen
  captures), Art Attack's cut-out (which frees its capture once drawn), Kiss
  Thief's screen copy (a 285 KB block each time), the movie's decode (the
  attract demo, the digest map: else the movie ran at the frame rate's
  multiple) and Luigi's pose flip skip in-between frames: the game's own frame
  after them draws the same list again. A frame whose draw makes extdrv's or the Punies' textures gets no in-between
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
  file eviction are as without them. A safety net under the rest: the game
  heaps' frees (`smartFree`, `__memFree`, `_mapFree`) in an in-between frame
  are held and done after the game's next own draw (`marioStDisp`,
  memory.c), so a draw callback that frees what it draws with can't have the
  game's own frame use freed memory; a pose released while such a frame is
  drawn is released after it, and none is made in it (`animPoseEntry`
  returns -1: the port's stand-ins make theirs in the game's frame).
- **The draw list's order**: kept, see above.
- **The `$TTYD_FAST` virtual clock**: it advances 1 us per read, so it is put
  back after the in-between frames.

`$TTYD_INTERP_AUDIT` (Windows, `interp_audit.c`) finds such writes during in-between frames:
- the game heaps are made read-only and each write is trapped and reported by function, line and two callers;
- the executable's globals are diffed and reported by name;
- the net check: once the frame's draw is over and what it changed is put back (poses, random numbers,
  `portInterpKeep`), each written heap page is compared with itself before the frame's first write to it, and
  each word still different is put down to its last writer: `[interp-audit] net writer ...` (and `net global`).
  A net site is a draw that changed the game, unless it is a cache the next real frame computes again.
- `TTYD_INTERP_AUDIT=N` audits one in-between frame in N (a long run's pace: each audited write traps).

It runs on single runs, testpass runs (`--env TTYD_INTERP_AUDIT=1 --set framerate=120`) or across a sweep:

    TTYD_INTERP_AUDIT=1 TTYD_FRAMERATE=120 python tools/port/map_sweep.py \
        build-pc-jp-aurora/ttyd-jp.exe data/G8MJ01 --disc <iso> --collect '\[interp-audit\]'

`tools/port/interp_audit_check.py <logs or a testpass run's folder>` fails on any net site not in
`tests/port/interp_audit_allow.txt` (the caches, each with its reason). The testpass group `interp_audit`
(the opening, the title and file select, a battle's action commands and Kiss Thief, every Star Power, and
full-run segments with the cannon, the Punies and a level-up) runs at 120 Hz with the audit on and fails on its
own on any net site the allowlist doesn't name (`interp_audit_check.py --update-scenarios` writes the allowlist into
them as two `log_must_not` patterns: run it after changing the list), and `local_ci.py --only interp` runs the group
and the check (Windows).

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
| 3 | Other moving objects: battle units, items, map objects (`mobj`), effects, HUD slides and fades | done |
| 4 | Skinned vertices | covered by the clocks |
| 5 | A settings page; per-system exclusions; VRR | |

Step 3 (`port/src/aurora/interp_regions_aurora.c`) keeps regions of memory at
each game frame and blends them for the in-between frames, then puts them
back:

- **Effects** (particles, sparkles, smoke, the weather, the battle's dust):
  each effect's own data, whose layout differs per effect, so every 4-byte
  word that reads as an ordinary float in both frames (zero, or about 2^-31 to
  2^31 in size) is blended. Ints read as tiny floats and are left alone, and
  so is an 8-aligned pair whose high half is a small number (a pointer on
  every 64-bit host). Blended while the slot holds the same data and callback.
- **Items** (position, scale) and **map objects** (position, scale,
  rotation): blocks, switches, platforms that move as objects.
- **Battle units and their parts**: position, offsets, rotations, centre,
  scales.
- **The HUD**: the status window's slide in and out, and each window's alpha,
  zoom and turn (`winmgr.c`): fades and pop-ins.

A value that changes by more than 150 in a frame (a jump, an angle wrapping
from 359 to 0) is drawn as it is now. Map geometry that moves through its
own matrices (`mapdrv.c` animations) still moves at the game's rate, as do
menus drawn from integer positions (the pause menu's cursor).

## Risks

- A draw function that steps state not yet found by the audit (it reports
  only what the swept content draws).
- Cost: the scene is drawn 2-4 times per game frame; fine on desktop GPUs,
  maybe not on older phones (the setting is per device).
