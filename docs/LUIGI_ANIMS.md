# Luigi's animations (Paper Cut 0.2, M6b)

"Play as Luigi" (docs/PLAYABLE_LUIGI.md) draws Luigi's field model, `c_luigi`,
in Mario's place, but the disc gives Luigi only stand, idle, walk, run, jump
and talk. This work adds new animations for his model, made in Blender, and
the tools that move animations between the game's files and Blender.

Nothing from the disc is in the repository or the packages: the tools read
the user's own disc and write everything they make from it under `data/`
(ignored by git). Only the tools and the new animation data (our own work)
are committed.

## The format

`src/driver/animdrv.c` and `include/driver/animdrv.h` have the whole format.
In short:

- A model file (`a/<name>`, AnimationPoseData) is a header, the model's
  tables (shapes, vertex arrays, triangle-fan draws, texture slots and binds,
  the group tree and each group's 24 node values) and a table of animations.
  Every table is packed in order, 4-aligned; the animations follow in table
  order. The textures are a TPL (`a/<texture name>-`); all of them are CMPR.
- A group's node is a transform with Maya's pivots (translation, scale and
  rotation, the rotation stored halved, each about its pivot), or a joint
  (joint orient, and the parent's scale cancelled). The root is the last
  group; a hidden group hides its subtree.
- An animation (AnimData) is a list of keyframes, each the deltas from the one
  before, applied in order from the model's own values:
  - node values: an index step, a value in 1/16 and two tangent angles (whole
    degrees; the slope is tan(angle) in value units a frame). Between two keys
    a value follows the cubic Hermite of the later key's delta and tangents.
    A delta past -128..127 is split into 127 / -128 chunks plus the rest, each
    with its own tangents (the slope shared out by each chunk's part of the
    delta, truncated toward zero); an index step over 255 takes empty
    255-steps. An end tangent of 89 or more (the converter writes 90, with a
    start of 0) makes the change a step at the key.
  - vertex positions and normals (1/16 x y z, linear between keys, chunked
    the same way), group visibility (added at the key), and texture slots (a
    bind index added, which is how Mario's drawings and Luigi's blinks swap,
    and a texture offset).
  - a loop flag and range, one value key, and a bounding box for the paper
    forms.
- Luigi's model is 24 groups (18 textured planes, 6 locators), all plain
  transforms with one pivot each; his animations move translation, scale and
  the Z rotation only, and swap textures and visibility. Mario's model is a
  42-group rig with joints and a hammer, whose planes cut out one whole-body
  drawing per pose (150 of them), swapped by texture animation, and bent by
  vertex animation.

`c_luigi` and `c_luigi-` are byte-identical on the JP, US and EU discs.

## Tools (`tools/port/`)

- `gcdisc.py`: reads single files out of a GameCube disc image.
- `ttyd_anim.py`: the format in Python. Decodes a model and its animations
  and writes them back byte for byte (every model and animation of `c_luigi`,
  `a_mario`, `a_mario_r`, `e_mario` and `b_mario` on all three discs); an
  evaluator that follows `animPoseMain` and the node matrix code; the
  geometry; CMPR textures to PNG.
- `ttyd_blender.py` (inside Blender):
  - `build_model`: an editable model: one object per group (a textured
    plane, or an empty) in the game's tree; location, rotation (XYZ Euler) and
    scale are the node values (the pivot is folded into the location and the
    children's parent inverse); visibility is a `["vis"]` property, with
    drivers so a hidden group hides its subtree; texture slots are
    `["tex<n>"]` on the model root, with materials that switch images. Each
    animation is an Action with a slot per object, the game's curves as
    Bezier keys with free handles a third of the way along each segment,
    which is the game's Hermite exactly; steps are Constant keys. 60 frames a
    second, Y up in the game turned to Blender's Z up by the root.
  - `read_action`: an Action back to an animation. Its key times are the
    union of every F-curve's keys (and the original's); a segment the game's
    curve can't follow closely enough gets a key in its middle. Angles are
    rounded to the nearest whole degree (the original converter truncated),
    and the delta's chunks share the slope so the sum comes closest.
    Untouched deltas from the disc are written back as they were.
  - `build_baked`: any model (Mario's) as one mesh that follows the game's
    evaluation frame by frame (a Mesh Cache modifier on a `.pc2` written per
    animation), with its texture swaps, for reference.
  - `roundtrip_check`, `fresh_check`, `file_roundtrip`: the tests below.
- `luigi_anims.py`: `extract` (the files these tools use, from a disc image),
  `check` (byte round trip in Python), `blend` and `roundtrip` (in Blender).

## Round trip

Run in Blender 5.2.3 on `c_luigi` (US; the JP and EU files are the same):

- Every animation, every group, at every half frame from start to end (1932
  poses): Blender's world matrix against the game evaluator's, largest
  difference 4.6e-6 units, no visibility differences.
- Every Action read back and written: all 36 animations byte for byte, and
  the whole model file written from the Actions is byte-identical to the
  disc's (SHA-1 3b105acb...).
- The writer as a new animation meets it (`fresh_check`: the Actions copied
  without anything kept from the disc, written, decoded again): all 36 play
  back the same as the disc's in the game evaluator (every matrix within
  1e-4), with the same key counts.
- In Python, every model on the three discs decodes and rewrites byte for
  byte (66 of about 21,000 node deltas, Mario's steep tangents, are kept as
  stored because the converter's rounding isn't recoverable from them).

Not reproducible: each animation's bounding box. The original converter
computed it in Maya; new animations get one from the evaluated geometry over
every frame, with a margin.

## In the game

- The pack, `port/assets/luigi/luigi_anims.bin` (our own work, built into
  the executable): the new animations, a hammer plane for Luigi's model (its
  records and node, our numbers), which group fields to set and which of
  `a_mario-`'s drawings to copy. `tools/port/luigi_pack.py` writes and reads
  it.
- At startup, before the mods apply, `port/src/aurora/luigi_model_aurora.c`
  reads the user's `c_luigi`, `c_luigi-` and `a_mario-` and makes
  `a/c_luigi_pc` and `a/c_luigi_pc-` (disc files the game loads like any
  other, through the mods' host files): c_luigi with the hammer added (a
  child of the front glove, hidden unless an animation shows it) and the
  pack's animations after his own, and c_luigi's textures followed by
  Mario's 12 hammer drawings (wooden, Super, Ultra, four angles each). It is
  byte for byte what `luigi_pack.py merge` makes (checked with
  `$TTYD_LUIGI_DUMP`). If the disc's c_luigi isn't the one the pack was made
  for (size and CRC), Luigi stays the disc's model. `$TTYD_LUIGI_DISC` skips
  it (side-by-side checks).
- `port/src/common/luigi_pc.c`: the stand-in loads `c_luigi_pc`; Mario's
  animation is played by Luigi when his model has one of the same name (ours
  are named as Mario's), else by the disc's name map (stand, walk, ...). The
  stand-in takes Mario's clock (`localTime`), so the game's own scrubbing
  and holding of Mario's pose holds Luigi's too. `$TTYD_LUIGI_TRACE` logs
  each change of Mario's animation and what Luigi plays.
- Paper forms: `animPaperPoseDispSub` (the capture a paper form, spin,
  plane, boat, ..., is drawn with) draws Luigi's stand-in and frames it with
  his animation's bounds while there is one; the paper overlay itself still
  draws through Mario's pose.

## What ships

Everything Luigi's animations need is in the repository and works for a
player with no Python or Blender:

- `tools/port/luigi_motions.py`: the motions as authored, key poses per part
  of Luigi's model (the parts by their names in c_luigi), our own data only.
- `port/assets/luigi/luigi_anims.bin`: the same motions in the game's
  animation format and the hammer's plane, built into the executable. No
  geometry, texture or animation from the disc: the hammer's drawings are
  named by their index in the player's `a_mario-`.
- The port builds Luigi's model in memory at startup, on all three versions
  (nothing is written to the user folder).

The .blend is never the only copy: `luigi_anims.py blend --save FILE` builds
the authoring scene from the disc and the committed data alone, and its
Actions give back the committed pack byte for byte (checked). The check of
the C side: a game run with `$TTYD_LUIGI_DUMP=FILE`, then
`luigi_anims.py portcheck FILE`.

## Making motions

`tools/port/luigi_motions.py` (in Blender) holds every motion as key poses,
offsets from Luigi's rest pose per part (its docstring names the parts of
c_luigi's unnamed rig), with Mario's timing. `build()` makes the Actions,
`export_pack()` (`luigi_anims.py pack`) reads them back through the writer (which adds keys where
the game's curve can't follow Blender's) and writes the pack. Scales are
snapped to sixteenths, the format's step. Back-facing versions mirror the
cap's L, as c_luigi's own do; the Super and Ultra Hammer versions are the
same motions with the hammer's drawings 4 and 8 on.

Reviewing: `ttyd_blender.contact_sheet()` renders frames of the scene camera
(Luigi beside the baked Mario), `tile_images()` tiles game captures. The
game captures one frame in flight at a time (every 4-5 frames); runs are
deterministic, so frame-by-frame sets come from runs at shifted
`$TTYD_SHOT` frames.

## Motions

| Mario's | Luigi's | State |
|---|---|---|
| `M_H_1` `M_H_4` `M_H_7` (and R) | the field hammer swing | done; checked in game (front, back, Ultra) |
| `M_H_2` `M_H_5` `M_H_8` (and R) | hitting a wall | done; not seen in game yet |
| `M_H_3` `3A` `3B` (and 6, 9) | the spin hammer's poses | done; the spin itself not reached in a test run yet |
| `M_A_1A` `M_A_1B` | the spin jump: tucked, and landing from the tuck | done; checked in game |
| `M_D_1` | hurt, knocked back | done; not seen in game yet |
| `M_D_2` `M_U_3` | knocked flat, getting up | done; not seen in game yet |
| `M_D_6` | hurt and worried (loop) | done; not seen in game yet |
| `M_I_1` | holding up an item | done; not seen in game yet |
| `M_I_Y` | something catches his eye | done; not seen in game yet |
| `M_Y_1` `M_Y_2` `M_Y_3` | riding Yoshi, his flutter hop | done; not seen in game yet |

| `e_mario`: `M_N_1` `M_N_2` `M_N_3` `M_N_B` `M_N_C` `M_I_N` `M_I_Z` `M_N_10` `M_I_G` `M_I_5` `M_N_7` `M_I_O` `M_N_5A` `M_N_5B` `M_C_3` `M_N_9` `M_N_H` `M_S_5` `M_N_11` `M_D_7` `M_D_7B` `M_I_6` `M_D_8` `M_L_2` `M_L_3` `M_F_1` `M_W_4B` | the story's event poses: nods, looks, talking, surprise, marching, sitting, dazed, asleep, eating, hopping for joy | done; checked beside Mario's in Blender; in game only that they load (they need story scenes) |

The event poses take Mario's own timing: where his drawing tilts his face,
Luigi's head turns as far; where his drawings swap for talking or a blink,
Luigi's mouth opens or his eyes shut on the same frames. Not made: the event
poses no script uses (`M_W_4A` `M_W_4C` `M_W_5` `M_I_7` `M_I_M` `M_L_1` `M_L_4`
`M_F_2`); `M_I_3` (talking) is the disc's `T_1`.

| battle (`b_mario`), as `<name>@b`: `M_C_3` `M_I_S` `M_V_1` `M_N_8` `M_J_1D` `M_A_3A` `M_A_3B` `M_A_4` `M_A_5` `M_A_6` `M_Z_10` `M_Z_11` `M_C_4` `M_N_5`, and the event poses again (`M_N_2` `M_I_Y` `M_I_O` `M_N_C` `M_U_3` `M_A_1A` `M_A_1B` `M_D_7`) | guard stance (the action menu), peril, a bow and wave, cheers, jump-attack bounce, somersault, upside down, knocked flying, stunned, laid out, using an item | done; the guard stance checked in a battle |
| `M_I_S` `M_V_2` | worn out at no HP (field and battle), the victory fist | done |

| battle hammer moves, as `@b`: `M_H_1A` `1B` `1C` (and `5`, `9`), `M_H_13` `14` `15`, `M_H_17A` `B` `C` (and `18`, `19`), `M_H_11`, `M_H_16`, `M_H_2A` `2B` `10` `10A` `10B` | wind-up, hold and strike; the overhead slam; over his head and round behind; a throw; a tumble back to his feet; the spin poses | done; Mario's hammer angle and drawing at each of his keys, Luigi's arms carrying it on; checked in Blender, not yet in a battle (the scripted menu picked Jump) |

`M_H_12` (only the thrown hammer is drawn) needs nothing: through Mario's
model it looks the same.

Battle animations of Luigi's are named `<Mario's name>@b`: `b_mario` reuses
field names for other motions, so `luigi_pc.c` looks for the `@b` one in
battle, then for a field one only where Mario's two are the same
(`kSameInBattle`). Paper forms: checked in game (the sideways paper turn
captures Luigi).

| the rest of the field set: `M_Z_9` `M_S_3` `M_O_1` `M_O_2` `M_O_2R` `M_B_1` `M_B_2` `M_B_3` `M_X_1` `M_I_U` `M_U_2` `M_U_2R` `M_W_7` `M_C_1` `M_C_1R` `M_C_2` `M_A_2A` `M_A_2B` `M_A_2C` `M_J_5A` `M_J_5B` `M_S_4` `M_W_6` `M_P_1` `M_P_2`, and e_mario's unused `M_W_4A` `M_W_4C` `M_W_5` `M_I_7` `M_I_M` `M_L_1` `M_L_4` `M_F_2` | low HP, shading his eyes, pushing, thinking, lifting Bobbery and walking with him, a throw, a fist to the crowd, with Flurrie, arms up (spring), a crouch and a jump | done; checked beside Mario's in Blender |
| battle: `M_C_1` `M_C_2A` `M_C_2B` `M_C_2C` `M_P_1` `M_P_2` `M_H_2` `M_H_3` `M_H_4` `M_H_7` `M_H_8` (as `@b`) | a throw, getting ready, a fist to the crowd, a crouch and a jump, the hammer thrown and held up | done; checked beside Mario's in Blender |

Every animation of Mario's (a_mario, a_mario_r, e_mario, b_mario) now has a
Luigi version, but `M_H_12`, where only the thrown hammer is drawn: it is
right as Mario's. 156 animations of ours in the pack. Luigi's arms are short
of his cap, so where Mario's arms go over his head (lifting Bobbery, the
spring moves) Luigi's stretch, lanky, the front one across his face as
Mario's does.

Still Mario's: `M_H_12` only.

Untried in game (one scripted try each, per the session rules): the damage
set `M_D_*` (no field hazard found near the test saves; walking into an enemy
starts a battle), the heart-block pose `M_I_1` (on hei_06 a Goomba started a
battle first; on hom_00 the scripted run never got control), `M_I_Y`, the
hammer spin (a held B didn't start it), and the event poses (they need their
story scenes). Seen in game: the field hammer (swing, wall hit, Ultra, back
view), spin jump, Yoshi ride and hop, paper turning sideways, the battle
guard stance, the jump-attack bounce and the battle hammer attack (wind-up,
hold, strike).

Earlier plan, by priority:
- The event poses (`e_mario`), paper forms' own animations (the forms
  themselves now capture Luigi when he has the pose), battle (`b_mario`'s 85).
- Anything Luigi has no version of, as before (docs/PLAYABLE_LUIGI.md).

## Luigi's friends

In Super Luigi (docs/SUPER_LUIGI.md) Luigi's five friends are his partners,
in the field and in battle. The disc has them only as the inn's NPCs, with
stand, talk and idle poses; the same tools give them everything else.

| Friend | Model | Parts (the disc's names) |
|---|---|---|
| Blooey | `c_lp_gesso` | one plane, `pPlane1` (its texture's bind 1 is the burnt Blooey) |
| Jerry | `c_lp_bom` | body `pPlane2`, fuse `pPlane3`, feet `locator4`/`pPlane5` and `locator5`/`pPlane6`, the hidden wind-up key `pPlane4` |
| Torque | `c_lp_met` | body `pPlane8`, shell `pPlane9`, wrench `pPlane10`, head `pPlane3`, legs `group1` and `group2` |
| Hayzee | `c_lp_pansy` | body `pPlane6`, flower head `pPlane2` (mouth `pPlane3`), leaf arms `pPlane5` and `pPlane4`, feet `pPlane8` and `pPlane7` |
| Screamy | `c_lp_moa` | body `locator3`/`pPlane5` (eyes `group1`, mouth `pPlane2`), feet `locator2`/`pPlane7` and `locator1`/`pPlane4` |

The three discs' copies of these files are byte-identical.

### How they ship

- `tools/port/friend_motions.py`: every motion, as key poses per part
  (the format of `luigi_motions.py`, plus `v`: a plane's corners moved, the
  game's vertex animation, which Blooey's one plane needs to pulse).
- `port/assets/luigi/friend_anims.bin` (our own work, built into the
  executable): one pack per model (the LUPK format of Luigi's pack, with
  animations only) in a small container (`LUFR`, `luigi_pack.write_friends`).
- At startup, `port/src/aurora/luigi_model_aurora.c` reads each friend's
  model from the user's disc, checks its size and CRC against the pack, and
  hands the game the disc's model with the new animations after its own,
  under the same name (`a/c_lp_gesso`, ...). The game finds every new pose
  on the friend's own model; the inn's NPCs are unchanged (their poses are
  the disc's). `$TTYD_FRIENDS_DISC` skips it; `$TTYD_FRIENDS_DUMP=DIR` writes
  the made files, and `luigi_anims.py friends-portcheck DIR` checks them
  against `luigi_pack.merge_friends`, byte for byte.
- Authoring: `luigi_anims.py friends-blend [--save F.blend]` (in Blender)
  builds the five models from the disc and every motion as an Action;
  `luigi_anims.py friends-pack` reads the Actions back through the writer
  into the pack. `luigi_motions.show(root, name)` shows a motion with its
  vertex offsets (shape keys, linear between keys, as the game's).

### The style

Every friend's set keeps to the game's own partners and the enemies of their
kind (Goombella `c_pkr`, Koops `c_pnk`, Bobbery `c_bomt_n`; Bob-omb
`c_bomhey`, Buzzy Beetle `c_met`, Dayzee `c_pansy`, Smorg `c_moa_a`, the
Blooper boss `c_gesso*`), measured from their files:

| Pose | The game's | Ours |
|---|---|---|
| walk `W_1` | loop 28-40 (partners 28-40, enemies 36-40) | loop 40: two steps of 20, or one pulse |
| run `R_1` | loop 12-20 | loop 20 |
| jump `J_1A` `J_1B` | one held pose, or a short loop (4-10) | 8 held / loop 16 |
| hurt `D_1` | loop 8-16 | loop 8 |
| worn out `D_2` (`W_2`, `R_2`) | loop 80-96 (walk and run as `W_1` `R_1`) | loop 92 (40, 20) |
| KO `D_3` | 14-16, held | 16, held |
| tumble `D_4` | 16-20 | 20 |
| frozen `D_5` | one pose | one pose |
| asleep `N_1` | loop 50-140 | loop 120 |
| dizzy `X_1` | loop 36-70 | loop 48 |
| scared `Q_1` | loop 8-16 | loop 8 |
| ready `C_1` | loop 48-72 | loop 48 |
| guard `G_1` `G_2` | one pose, or 40 | 40 |
| item `I_1` | one pose, or 10 | 10 |
| appeal `O_1` | 56-72 | 56 |
| victory `Y_1` | loop 12-14 | loop 14 |

Amounts, on a body about 16 units tall: a walk's bob 0.4-1.5 units and its
lean 3-8 degrees; a run's lean 8-10; squash and stretch in sixteenths (the
format's step for scale), 1/16 for idles and walks, 2/16-3/16 for jumps, hits
and attacks, more only in an attack's wind-up; turning round is the paper
flip (x scale through 0 to -1), as the game's partners do it. Keys are eased
(Blender's auto-clamped Bezier, the game's Hermite) except into an impact,
which is a sharp corner. Everything is at 60 frames a second, keyed on whole
frames (the game's keys are mostly 2-10 frames apart).

The look, for new drawings and models: flat paper cut-outs, each part a
textured plane facing the camera, a few tenths of a unit apart in depth
(front parts +z); the character faces left (-x). Drawings are flat colour
with one soft shade, a thick dark outline (about 3 texels; a dark tone of the
part's own colour, e.g. Blooey's dark brown, not black) and transparent
outside it. About 8 texels a game unit (Blooey's 128x144 drawing is 16x18
units; Bobbery's body 88 texels for 11 units), sizes multiples of 8; the
disc's are all CMPR. Characters stand 14-25 units tall (Goombella 15, Luigi's
friends 14-18, a Bob-omb 25 with its fuse).

### The poses

Every friend has the game partners' set (`STANDARD` in `friend_motions.py`),
on top of the disc's `Z_1` (still), `S_1` (stand) and `T_1` (talk):

| Pose | What | Where the game asks for it |
|---|---|---|
| `W_1` `R_1` | walk, run | field (`partyPoseTbl` 2, 3), battle (`BTL_POSE_WALK`, `RUN`) |
| `J_1A` `J_1B` | jump: going up, coming down | field 4, 5; battle 49 |
| `D_1` | hurt | field 8 (knocked away), battle `BTL_POSE_DAMAGE` |
| `D_2` `W_2` `R_2` | worn out: standing, walking, running | field 12-14 (low HP); battle `PERIL`, `DANGER`, poison, `ESCAPE_FAIL_END` |
| `D_3` | KO | battle 27 |
| `D_4` | tumbling back | field 9 (knocked away); battle `ESCAPE_FAIL` |
| `D_5` | frozen, stopped | battle 2, 9 |
| `N_1` | asleep | field 6 (Luigi asleep); battle 1 |
| `X_1` | dizzy, confused | battle 3, 5 |
| `Q_1` | scared | a move's target, fright |
| `C_1` | ready | battle `COMMAND_WAIT` |
| `G_1` `G_2` | guard, Superguard | battle `DEFEND`, 31, 37 |
| `I_1` | holding up an item | battle `ITEM_USE` |
| `O_1` | appeal | battle `APPEAL` |
| `Y_1` | victory | the end of a battle |
| `F_1` | the field ability | the ability's own code |

A partner's row for `partyPoseTbl`: `{"Z_1", "S_1", "W_1", "R_1", "J_1A",
"J_1B", "N_1", "T_1", "D_1", "D_4", "S_1", "S_1", "D_2", "W_2", "R_2"}`. A
battle pose table as the partners' (`unit_party_christine.c`): 9 and 2
`D_5`, 3 and 5 `X_1`, 4 `D_2`, 1 `N_1`, 27 `D_3`, `NORMAL` `S_1`, 37 `G_2`,
31 `G_1`, `DAMAGE` `D_1`, `WALK` `W_1`, `RUN` `R_1`, `PERIL` and `DANGER`
`D_2`, `ESCAPE_FAIL` `D_4`, `ESCAPE_FAIL_END` `D_2`, `TALK` `T_1`,
`ITEM_USE` `I_1`, `COMMAND_WAIT` `C_1`, `APPEAL` `O_1`, `ZERO_GRAVITY`
`J_1B`, 49 `J_1A`, `DEFEND` `G_1`, `STAY_DEFAULT` `S_1`.

The moves (each friend's own):

| Friend | Pose | What |
|---|---|---|
| Blooey | `F_1` | Swim: flattened, carrying Luigi on his head, long slow strokes (loop 40) |
| | `A_1A` `A_1B` `A_1C` | Torpedo: winding up (loop 24), shooting head first (loop 8, 9 up and 3 ahead of his rest), bouncing off and righting himself (24) |
| | `A_2A` `A_2B` | Ink Squirt: drawing a breath, head swelling (30, held), squirting and recoiling (24) |
| | `A_3A` `A_3B` `A_3C` | Tentacle Hold: lunging, tentacles flung wide (16, held, 4.4 ahead), squeezing (loop 20), letting go (16) |
| Blooey, burnt | `BURN_<pose>` | every pose above, with the disc's brown burnt drawing (the disc's own burnt ones are `Z_2` `S_2` `T_2`) |
| Blooey, soot black | `SOOT_<pose>` | every pose above and `SOOT_Z_1` `SOOT_S_1` `SOOT_T_1`, soot black with embers and a smoke wisp (below) |
| Jerry | `F_1` | Blast: swells, shaking, and bursts, squashed flat, then pops back (48) |
| | `A_1A` `A_1B` | Lit Fuse: rearing back, the fuse fizzing (loop 8); a hop forward that flings the bomb (24; let go at 4) |
| | `A_2A` `A_2B` | Big Bang: swelling, shaking (loop 8); the burst (30; the blast at 4) |
| | `A_3A` `A_3B` | Short Fuse: hunkered down winding himself up, his wind-up key turning (loop 16); springing up charged (20) |
| Torque | `F_1` | Tune-up: the wrench in his jaws, turning hard, hopping with the effort (loop 24) |
| | `A_1A` `A_1B` `A_1C` | Shell Slide: pulling in head and legs (12, held); sliding, spinning (loop 8: the paper turning round); popping back out (20) |
| | `A_2A` `A_2B` | Wrench Work: taking the wrench from his shell into his jaws (16, held); working it (loop 16) |
| | `A_3A` `A_3B` | Spike Guard: hunkering down, the shell up and bristling (12, held); braced, trembling (loop 10) |
| Hayzee | `F_1` | Sing: a bright little song, bouncing on the beat (loop 40) |
| | `A_1` | Lullaby: a slow swaying song (loop 48) |
| | `A_2` | Encore: an eager cheer, hopping (loop 14) |
| | `A_3A` `A_3B` | Showstopper: winding up (20, held); the grand finale, head thrown back, singing out (30, held) |
| Screamy | `F_1` | Float: risen high, hanging in the air, bobbing slowly (loop 60) |
| | `A_1A` `A_1B` | Swarm: gathering itself (12, held); darting at the foe again and again (loop 8) |
| | `A_2A` `A_2B` | Scream: drawing itself in (20, held); the scream, stretched tall, mouth wide, shaking (loop 6) |
| | `A_3` | Shade: spread wide and flat overhead, a canopy over Luigi (loop 40) |

Hayzee's mouth opens on bind 1 (`pPlane3`'s texture) whenever she sings.
Her leaf arms sit low behind her flower head: at rest and in most poses they
stay there, small; where they carry the gesture (`F_1` `F_2` Sing, `A_1`
Lullaby, `A_2` Encore, `A_3A`-`C` Showstopper, `T_1` talk (ours, in place of
the disc's), `O_1` `O_2` appeal and bow, `I_1`, `Y_1` victory, `E_2` joining:
a hop with both leaves up) they come in front of the head (their z), out at
its sides and raised up to 110 degrees;
Torque's KO turns him over onto his shell (as Buzzy Beetles are flipped);
Screamy hovers half a unit up in its walk, the disc's stand being on the
ground.

Every friend's every pose is checked in Blender (contact sheets of the
scene camera); in the game, each made model matches the Python merge, and
the follower poses (stand, walk, run, jump) are seen following Mario (below).

### Checking in the game

`$TTYD_FOLLOWER=<id>[,<model>]` (with `$TTYD_MAP`, `port/src/common/trace_pc.c`)
puts a follower beside Mario, drawn with another model: `TTYD_FOLLOWER=9,c_lp_gesso`
walks Blooey after Mario with Flavio's follower code (its poses: `S_1` `W_1`
`R_1` `J_1A` `J_1B` `D_1` `T_1`). With `$TTYD_PARTY=0`, a `$TTYD_INPUT` script
(`stick 45 0 50` walks, `hold right 60` runs, `press a` jumps, `shot NAME`)
and `$TTYD_SHOT_DIR`. Checked so: Blooey's walk, run and jump on gor_01 and
hei_00, the statue's stand, walk and run on gor_01 (it doesn't jump: no `J_1A`).

## New models

Characters the disc doesn't have are made the same way, as data of our own:

1. **The drawings.** One per part, a flat paper cut-out (the look above):
   drawn in code with `paper_model.Canvas` (shapes from signed distances:
   `sd_box`, `sd_poly`, `sd_ellipse`, unions and cuts; `shape()` fills with
   a shade, a light edge and the dark outline; `line()` for carved lines;
   `speckle()` for grain), or painted elsewhere and read from PNGs
   (`paper_model.png_part`: 8-bit RGBA PNGs, the image's bottom left corner
   placed in the model, 8 texels a unit, the pivot in model units). Art from
   an artist goes in `port/assets/super_luigi/art/<character>/`, a PNG per
   part, with a README of each part's placement and pivot.
2. **The rig.** A list of `paper_model.Part`s: each part's parent, its
   plane's corners in model units (y up, facing -x, front parts at +z a few
   tenths apart), its pivot, and any other drawings its texture switches to
   (`binds`, the `tex` channel: a gem that glows, eyes that close). Empty
   parts (no image) are movers. The first part with no parent is the root.
3. **The model.** `paper_model.build(name, parts, anims)` writes the model
   file (`a/<name>`) and its TPL (`a/<name>-`, CMPR), laid out as the disc's.
4. **The motions.** A module per model (`tools/port/statue_model.py` is the
   example): `NAME`, `parts()`, `build(anims)` and `MOTIONS` (key poses as
   `friend_motions.py`'s). Add it to `NEW_MODELS` in `luigi_anims.py`.
   `luigi_anims.py friends-blend` builds it in Blender (from its parts alone,
   the motions as Actions, beside the friends) to pose and review;
   `luigi_anims.py friends-pack` reads the Actions back through the writer and
   puts the model and its textures, whole, into
   `port/assets/luigi/friend_anims.bin` (`luigi_pack.write_friends`: an entry
   that isn't an animation pack is a whole file). `$TTYD_NEW_MODEL_OUT=DIR`
   also writes them there, to look at (`ttyd_blender.build_model` on them
   shows the texture switches too).
5. **In the game** the port adds each such entry as `a/<name>` at startup, so
   the game loads it by name like any disc model (an NPC's or a battle
   unit's model name, a follower's `partyDataTbl` entry).

### The stone statue (`c_lp_statue`)

Chapter 1's boss: an ancient idol of Rumblebump Volcano, 68 units tall
(nearly three Luigis): a huge head under a heavy brow, the red gem in its
forehead as the focal point, broad shoulders, long arms with fists near the
ground, short legs on block feet, lava glowing in its cracks.

Its parts, as the art replaces them: each drawing is
`port/assets/super_luigi/art/statue/<file>.png` if that file exists (8-bit
RGBA, 8 texels a unit; a size that isn't a multiple of 8 is padded at the
right and top), else drawn by `statue_model.py`. Placement is the drawing's
bottom left corner, in model units (y up, facing -x); pivots too.
`python tools/port/statue_model.py spec` prints this table from the code.
The art agent's drawings (`super-luigi-art`, painted to its style sheet) are
in that folder and are what ships. A PNG made for another size is scaled to
its drawing's; every drawing now has its own PNG at its size (the six chunks
`rubble_1`..`rubble_6` at 208x176, the loose gem's `gem_glint`).

| Part | Parent | Files (first: at rest; then its texture's other binds) | Texels | Bottom left | Pivot | z |
|---|---|---|---|---|---|---|
| `locator2` | root | (the whole: the mover) | | | 0, 0 | |
| `torso` | `locator2` | (an empty: the upper body's mover) | | | -0.9, 9.0 | |
| `body` | `torso` | `body` | 424x200 | -27.0, 5.4 | -0.9, 9.0 | 0 |
| `head` | `torso` | `head` (with the gem's empty socket) | 416x368 | -27.0, 23.4 | -0.9, 27.0 (neck) | 0.4 |
| `eyes` | `head` | `eyes` (lit), `eyes_dark` | 288x88 | -19.8, 37.1 | -2.2, 42.5 | 0.6 |
| `gem` | `head` | `gem`, `gem_glow`, `gem_cracked` | 144x144 | -11.7, 48.2 | -2.7, 57.2 | 0.8 |
| `arm_f` | `torso` | `arm_f` | 144x224 | -35.1, 1.8 | -26.1, 26.1 (shoulder) | 1.4 |
| `arm_b` | `torso` | `arm_b` (a tone darker) | 144x224 | 13.5, 1.8 | 22.5, 26.1 | -0.8 |
| `foot_f` | `locator2` | `foot_f` | 176x80 | -19.8, -0.7 | -9.0, 1.4 | 1.0 |
| `foot_b` | `locator2` | `foot_b` (a tone darker) | 176x80 | -0.9, -0.7 | 9.9, 1.4 | -1.2 |
| `rubble_1`..`6` | `locator2` | `rubble_1`..`rubble_6`: chunks of the body, hidden until it shatters | 208x176 | see `spec` | each chunk's middle | 0.3, 0.2, 0.5, -0.4, 0.6, -0.2 |
| `gem_loose` | `locator2` | `gem_cracked`: the gem fallen free, hidden until then | 144x144 | -11.7, 48.2 | -2.7, 57.2 | 1.2 |

Its weight: a step takes 48 frames (a partner's 20), each footfall lands
hard (a sharp key) with the torso dropping after it and the arms swinging on
late; its idle grinds, small turns that catch and hold, not a smooth bob.
The frames where something hits the ground, for the battle code's screen
shake, camera hop and rock-dust puff (`IMPACTS` in `statue_model.py`):

| Pose | What | Impacts (frames) |
|---|---|---|
| `Z_1` `S_1` | still; standing, grinding as it breathes, the gem glinting (loop 120) | |
| `W_1` `R_1` | heavy steps: a foot heaves up, swings ahead and lands (loop 96; 60) | 24, 72; 15, 45 |
| `T_1` | talking: the head grinds side to side, the eyes flicker (loop 24) | |
| `A_1A` `A_1B` | the stomp: rearing back, the front foot high, trembling (40, held); bringing it down (44) | A_1B: 6 |
| `A_2A` `A_2B` | the punch: the arm hauls back (30, held); round into the ground before it (44) | A_2B: 7 |
| `A_3A` `A_3B` | the gem: charging, glowing and swelling (loop 20); a flash (30) | |
| `D_1` | hit: a jolt and a shudder (loop 10) | |
| `D_2` | the gem struck: reels back, the gem flashes and cracks (50, held cracked) | |
| `K_1` | cracked: wobbling, the eyes failing now and then (loop 60) | |
| `D_3` | defeated: the eyes go out, it shudders and sags; at 30 it shatters: the body bursts into four chunks, the head and arms drop and tumble, everything lands (46) and bounces (52, 58); the gem pops loose and rolls away to the front (100, held) | 34 (the dust: it shatters), 46, 52, 58 |
| `X_1` `N_1` `D_5` | dizzy (loop 60), asleep (loop 120), frozen | |

A battle pose table for it: `NORMAL` `S_1`, `DAMAGE` `D_1`, `WALK` `W_1`,
`RUN` `R_1`, `TALK` `T_1`, 1 `N_1`, 3 and 5 `X_1`, 2 and 9 `D_5`, 27 `D_3`,
`STAY_DEFAULT` `S_1`; the moves and the gem's states from its scripts.

## Super Luigi's models

The characters Super Luigi needs that the disc doesn't have, made as "New
models" above, from art of our own (`port/assets/super_luigi/art/<character>/`:
a PNG per card, its SVG source, and `parts.json` with each card's size and
pivot in texels; `STYLE.md` the rules the art keeps to).

- **A module per model** (`tools/port/sl_<character>.py`, as
  `statue_model.py`): `NAME`, `parts()` (the art's cards placed as its README
  assembles them, each card's pivot from `parts.json`), `build(anims)` and
  `MOTIONS` (key poses as `friend_motions.py`'s), `POSES` (what each one is)
  and `CAMERA` (the review camera). Registered in `tools/port/sl_models.py`'s
  `MODELS`.
- **Shipping:** a container of their own, `port/assets/super_luigi/sl_models.bin`
  (`luigi_pack`'s LUFR, every entry a whole file: the model and its TPL), so
  this work and the friends' `friend_anims.bin` never touch the same binary.
  Built into the executable; at startup `port/src/aurora/sl_models_aurora.c`
  (`portSuperLuigiModelsInit`, before the mods) adds each entry as `a/<name>`.
  No disc data in any of it.
- **Making it:** `blender --background --python tools/port/sl_models.py -- pack`
  builds each model from its parts, its motions as Actions
  (`ttyd_blender.build_model`, `luigi_motions.build_all`), reads them back
  through the writer and writes the container (a copy of each file in
  `build/sl_models/out_*`). `-- sheet sl_<character> DIR [POSES...]` renders
  a contact sheet per pose from the front; `-- blend sl_<character> F.blend`
  saves the authoring scene. `python tools/port/sl_models.py list` lists the
  container.
- **Necks and chains:** a chain of parts that bends and stretches is a chain
  of locators (joints), each carrying its card: a segment stretches by its
  card's `sy` and the next joint's `ty`, so what hangs off the end (a head)
  isn't stretched with it. `sl_hizza.reach()` aims a chain (coordinate
  descent on the joints' turns, the most even bend) so a point of its head is
  at a place, turned to an angle; the model refuses to build if an aim can't
  be reached.
- **Variants of Luigi:** a model the port makes from the disc can't ship,
  but cards added to it can (`tools/port/sl_attach.py`): `c_luigi_bride` is
  `c_luigi_pc` (the disc's c_luigi with Luigi's pack, made at startup) with
  more groups hung from its own, more drawings after its textures, and any
  animations of our own. The container holds a pack of the records
  (`"<variant>@<base>"`, luigi_pack's LUPK) and a TPL of the drawings
  (`"<variant>-@<base>-"`); after `portLuigiModelInit`, the port merges them
  into the base it made (`portLuigiMergePack`, `portModsGetHostFile`) and adds
  `a/<variant>`. The pack points into the base's tables by count, so the port
  checks the base's position and texture counts first and leaves the variant
  out if Luigi's model grew. `$TTYD_SL_DUMP=DIR` writes what it made, byte
  for byte `sl_attach.merge`'s (checked: the bride's model and textures).
  Every pose of the base plays on the variant, the cards following the
  groups they hang from. A variant can also go on a disc model (Sergeant
  Bulkhead on `c_heavy`) and swap one of its drawings for ours (Petunia on
  `c_pansy`, a module's `SWAPS`); see below.
- **Checking in game:** a mod in a fresh user folder with an `evt/` map
  script that puts the model on a map as an NPC (`evt_npc_entry(name,
  "c_sl_<character>")`, `evt_npc_set_anim`), stepping through poses with
  `WAIT_FRAMES`, and a `TTYD_INPUT` script of `wait`/`shot` lines.

### Hizza (`c_sl_hizza`, `c_sl_hizza_tail`, `c_sl_hizza_whole`)

Chapter 2's boss, the two-headed snake (docs/super_luigi/ch2.md): a purple
coil with the front head, Hiss, on its neck, and a tail whose head, Zah,
has a crest, a scar and heavy lids. Three models from one rig
(`tools/port/sl_hizza.py`; `sl_models.py`'s `MODELS` names them
`sl_hizza:FRONT`, `:TAIL`, `:WHOLE`):

- **`c_sl_hizza`** (battle, the unit's first part): the coil and Hiss,
  facing -x, on the enemy side.
- **`c_sl_hizza_tail`** (battle, the second part): the tail rising out of the
  floor at its origin, Zah's head facing **+x**: it sits at the stage's far
  left, behind Luigi, looking at his back.
- **`c_sl_hizza_whole`** (the field's scenes): both heads on the one coil,
  facing -x; its own poses below.

The rig's parts: `body` (the mover); `coil_back`, `coil_front`; the neck's
joints `ja1` `ja2` `ja3` (cards `neck_a1..3`) to `head_a` (binds 0 open, 1
blink, 2 hurt), with `jaw_a` (rz > 0 opens), `mouth_a` (the open mouth's
inside, shown past 20 degrees), `tongue_a` (tx < 0 slides it out), `stars_a`
(dizzy stars) and the locator `target_a` (the head's middle); the tail the
same, `jb1..3`, `tail_b1..3`, `head_b`, `jaw_b`, `mouth_b`, `tongue_b`,
`stars_b`, `target_b`; `collar` (the compass piece on its collar, round the
neck's lowest segment) and, in Zah's model, `tail_base` (the tail coming out
of churned dirt, which stays on the floor when the tail sinks). Zah's model
is the tail's parts turned round (`body`'s sx -1) with its base moved to
the origin.

**Placing the two parts.** In the pincer's strike (`A_1B`) and the crash
(`X_1`), Hiss's snout tip is 38 units in front of (left of) its origin and
Zah's 45 units in front of (right of) its own, both 17 up, at frame 5 (the
contact frame): `sl_hizza.HISS_REACH`, `ZAH_REACH`, `MEET_Y`, `CONTACT`. So
for Luigi at x = P, the battle puts Hiss's part at P + 38 and Zah's at P - 45
(Hiss's coil slides in for it; Zah sinks with `Q_1`, is moved, and comes up
with `U_2`). Both heads then meet on Luigi; if he dodged, on each other. The
Snap and the Sneak Bite reach the same distances (hit at 3). Zah dazed lies
on the floor 34 units in front of its origin, where Luigi's hammer reaches
it; Hiss's head 40 units in front of its own.

| `c_sl_hizza` (Hiss) | What |
|---|---|
| `S_1` `Z_1` | stand (loop 80); idle, blinks and tongue flicks |
| `T_1` | talking (loop 32) |
| `W_1` `R_1` | slithering (loop 40; 24) |
| `A_1A` `A_1B` `A_1C` | the Pincer: drawing back and up, fixing on Luigi (36, held); the strike (contact at 5, 20); back (30) |
| `X_1` `X_2` `X_3` | the crash: the open mouths meet (contact at 5), squash, recoil, the head droops to the floor seeing stars (60, held); dazed (loop 48); shaking it off (36) |
| `A_2A` `A_2B` | Snap: rearing back, mouth wide (24, held); the bite (hit at 3, 24). Also his bite on Zah |
| `V_1A` `V_1B` | Venom Spit: puffing up (24, held); the spit (it leaves at 4, 30) |
| `C_1A` `C_1B` | Coil Squeeze: tightening (30, held); squeezing (loop 16) |
| `A_4A` `A_4B` | a body slam: coiling (30, held); up and down with a crash (lands at 16, 40) |
| `B_1` | bickering with Zah: hissing and snapping to the left (loop 40) |
| `D_1` | hurt (loop 12) |
| `P_1` `S_2` | enraged, the phase change: sinking, rearing high, hissing and shaking (90, held); the angry stand (loop 48) |
| `D_4` | defeated: a jolt, the head sags to the floor, the coil slumps (80, held; the smoke burst is the battle's) |
| `N_1` `U_1` | coiled asleep on the heap (loop 120); waking and rising (60, held) |

| `c_sl_hizza_tail` (Zah) | What |
|---|---|
| `S_1` `Z_1` `T_1` | stand, idle, talk, as Hiss's |
| `N_1` `Q_1` `U_2` `U_1` | under the floor (out of sight); sinking into it (20, held); bursting out (24); rising slowly, eyes opening (60) |
| `A_1A` `A_1B` `A_1C` | the Pincer from behind (as Hiss's, mirrored; contact at 5) |
| `X_1` `X_2` `X_3` | the crash, dazed on the floor behind Luigi, shaking it off |
| `A_3A` `A_3B` | Sneak Bite: swaying side to side (the telegraph), rearing, mouth wide (40, held); the bite at Luigi's back (hit at 3, 24). Also its bite on Hiss |
| `V_2A` `V_2B` | Venom Spit |
| `B_1` | bickering with Hiss (loop 40) |
| `D_1` | hurt (loop 12) |
| `P_1` `S_2` | enraged; the angry stand |
| `D_4` | defeated: a jolt, the head sags, then the tail sinks into the floor (80, held) |

`c_sl_hizza_whole` has the same kinds of poses for both heads on one coil:
`S_1` `Z_1` `T_1` (Hiss talks) `T_2` (Zah talks) `W_1` `R_1`, the one-model
pincer and crash (`A_1A` `A_1B` `A_1C`, `X_1` `X_2` `X_3`: Hiss swings round
past Luigi and turns to face him), `A_2A` `A_2B` `A_3A` `A_3B` (each head's
bite), `A_4A` `A_4B`, `V_1A` `V_1B` `V_2A` `V_2B`, `C_1A` `C_1B`, `B_1` (the
heads face each other and hiss in turn), `B_2` (Hiss bites Zah) `B_3` (Zah
bites Hiss), `D_1` `D_2` `D_3` (hurt: both, Hiss, Zah), `P_1` `S_2`, `D_4`,
and the field's `N_1` (coiled asleep on the heap) and `U_1` (one head
rises, then the other end of the coil).

A battle pose table for each part: `NORMAL` `S_1` (`S_2` after `P_1`),
`DAMAGE` `D_1`, `WALK` `W_1` (Hiss), `TALK` `T_1`, 3 and 5 `X_2`, 27 `D_4`,
`STAY_DEFAULT` `S_1`.

Seen in game (US, as NPCs on a Fahr Outpost map, placed as above around
Mario): Zah bursting up, both drawing back for the Pincer, the heads meeting
on Mario, and both dazed on the floor with stars; the one-model snake's
idle, pincer, crash, bite, rearing and defeat; the texture swaps (blink,
hurt). Not yet in a battle (that's the story code's).

### Luigi's bride veil (`c_luigi_bride`)

Chapter 2: Luigi goes to Hizza dressed as its bride. A variant of
`c_luigi_pc` (`tools/port/sl_veil.py`, art in
`port/assets/super_luigi/art/veil/`), the veil hung from his cap (`pPlane4`):
`veil_drape` behind his cap and head (binds: 0 hanging, 1 blown back),
`veil_band` (pearls and flowers) in front of the cap's crown, and
`veil_back` (the veil from behind, hidden), and the gown, `gown` (a full
white skirt pinned at his waist, on his body `pPlane17`, in front of his
legs, the shoes showing below the hem; binds: 0 standing, 1 swung back). It
has every pose of Luigi's, the disc's and ours (192), with the veil and gown
on; none of them key them, so they hang in all of them, following his head
and body through walks, jumps, talking and the hammer. Its own poses:

| Pose | What |
|---|---|
| `TW_1` | a twirl: arms out, two paper turns on a little hop, the skirt and veil swung out, then a coy head tilt (48) |
| `FA_1` | fainting: a wobble, the eyes rolling shut, then flat on his back, the veil settling over his face (50, held) |
| `FA_2` | lying in a faint: breathing, a foot twitching (loop 60) |

The flutter, the swish and the back view are there for other scenes of our
own (the "tex" of `veil_drape` and `gown`, the "vis" of `veil_back`).

- **Scenes:** an NPC of Luigi as the bride is `evt_npc_entry(name,
  "c_luigi_bride")` (not `c_luigi`, so the brothers' swap leaves it alone).
- **Playing as the bride:** the stand-in loads `c_luigi_pc`
  (`port/src/common/luigi_pc.c`, `LUIGI_MODEL`); for the bride section the
  story code switches that name to `c_luigi_bride` (and back after it). The
  stand-in checks its pose's model each frame, so the switch should take on
  the next frame (not tried yet).

Seen in game (US, as an NPC on Fahr Outpost's map): standing, walking, the
hammer swing and talking, the veil on; the gown, the twirl and the faint.
In battle (Play as Luigi's stand-in) not yet.

### Big Green 01 (`c_lp_kart`, `c_luigi_kart`)

Chapter 3's kart (docs/super_luigi/ch3.md; art in
`port/assets/super_luigi/art/kart/`, `tools/port/sl_kart.py`): 34 units long,
12 tall, facing -x, its origin on the ground midway between the axles.

- **`c_lp_kart`**, the kart alone (the garage, the grid, the tag scene), and
  the same model in the other teams' colours, `c_lp_kart_red`, `_blue`,
  `_yellow`, `_purple`, `_orange`, `_white`, `_black`, and `c_lp_kart_wreck`
  (dented and scorched, for the course's debris). Each is its own model file
  and textures (the model names its own TPL), so a script picks the colour
  by name.
- **`c_luigi_kart`** (`tools/port/sl_kart_luigi.py`): Luigi seated in it, a
  variant of `c_luigi_pc` (like the bride): the kart's cards hang from his
  `locator2`, so the kart's poses move him with it; his hips on the seat, his
  legs hidden in the body, his hands on the wheel (the wheel's column at
  (-2, 6.8), where his gloves reach with his arms their own length). For the grid, the reverse
  start, the crash, and Kart Mode (drawn in place of the tube's model).

Parts: `kart` (the mover, turning about its middle (0, 5); in `c_luigi_kart`
Luigi's `locator2` moves both), `wheel_far_f` `wheel_far_b`, `kart_engine`,
`kart_seat`, `kart_nose` and `kart_body` (binds 0 whole, 1 dented and
scorched, in the kart's own colour: `kart_body_wrecked_<colour>`,
`kart_nose_wrecked_<colour>`; the crash switches both), `plate_01`,
`wheel_f` `wheel_b` (rz > 0 rolls forward), `steering_wheel`, and the hidden
`puff` (exhaust), `flame` (backfire), `smoke_1` `smoke_2`.

| Pose | What | Luigi (`c_luigi_kart`) |
|---|---|---|
| `S_1` | parked (loop 60) | at the wheel |
| `S_2` | the engine running: a shake, puffs from the pipe (loop 12) | at the wheel |
| `W_1` | driving: the wheels roll a turn every 16 frames, a bob, puffs (loop 16) | leaning in |
| `R_1` | flat out, the flame flickering (loop 8) | leaning hard, eyes narrowed |
| `B_1` | full throttle in reverse (the start): the wheels backwards, a wheelie, the flame (loop 8) | thrown forward, screaming, arms flailing |
| `C_1` | the crash backwards into the wall: crumpled against it, end over end, down on its back, a bounce, smoking (60, held; it ends 16 units behind where it started) | slammed back, flailing, then under the kart |
| `C_2` | on its back, smoking, a wheel still turning (loop 40) | under the kart (he crawls out in the story's scene: `c_lp_kart`'s `C_2` there, and Luigi) |
| `F_1` | flipped back onto its wheels: a shudder, a hop, half a turn, a hard landing and a backfire (40) | back in the seat as it lands |
| `D_1` | spinning out (a wreck on the course): two paper turns on a hop (40) | dizzy |
| `T_1` | (`c_luigi_kart` only) talking at the wheel (loop 20) | |

Seen in game (US, as NPCs on a Fahr Outpost map): Luigi driving, reversing
and crashing; a red kart idling and crashing. Kart Mode's hook in the tube
motion is the story code's.

### Fuselle (`c_sl_fuselle`) and the ice wall (`c_sl_ice_wall`, `c_sl_ice_wall_large`)

Chapter 2's extras (`tools/port/sl_fuselle.py`, `tools/port/sl_icewall.py`,
rigs of `sl_rig.py`'s kind).

Parts: fuselle (the mover) > foot_back, foot_front (their tops), body (its
foot) > key (the wind-up key in her back: it turns by sx, edge on at 0),
fuse (the cap's foot) > spark (hidden: lit), face (binds 0 open, 1 blink,
2 hurt / squeezed shut, 3 shy, blushing).

| Pose | What |
|---|---|
| `S_1` | standing, the key turning (loop 40) |
| `Z_1` | idle, blinking (loop 40) |
| `T_1` | talking, shy and blushing (loop 30) |
| `T_2` | talking, earnest, little hops (loop 24) |
| `W_1` | a waddle (loop 32) |
| `R_1` | hurrying (loop 16) |
| `C_1` | crying for joy (loop 24) |
| `C_2` | her fuse lit (the spark fizzing, loop 8) |
| `G_1` | giving (the pie): a hop toward him (30, held) |
| `Q_1` | a gasp, jumping back (24, held) |

Parts: wall (the wall card) and chunk_1..6 (hidden, thrown out in the burst).

| Pose | What |
|---|---|
| `S_1` | standing (loop 60) |
| `H_1` | struck: a shudder (16) |
| `B_1` | bursting: chunks thrown out, nothing left (40, held) |
| `N_1` | gone (loop 2) |

Seen in game (as NPCs): Fuselle talking and crying for joy, the small wall
standing. The burst checked in Blender.

### Chapter 2's props (`c_sl_banner`, `c_sl_cage`; `tools/port/sl_ch2_props.py`)

- `c_sl_banner`: the "CONGRATULATIONS" banner over the mayor's door, 64 x 11
  units, its origin the middle of its top edge (the rope line: set the NPC's
  position at the lintel, the banner hangs below it). One card, `banner`,
  whose drawing is the story's state: binds 0 "CONGRATULATIONS FUS" (half
  painted, at arrival), 1 "... LUIGI" (after the boss), 2 "... LUIGI & FUS"
  (after the proposal). A pose per state, and the turn from one to the next.
- `c_sl_cage`: the brides' bone cage, 24 x 29 units, its origin the middle of
  its base: `cage_back` (the far bars, z -3) behind the brides and
  `cage_front` (the near bars, z +3) in front, so brides placed at the cage's
  position stand inside it. The near bars hang from `door`, a hinge at the
  cage's front edge (x -11), and swing open about it.

`c_sl_banner`:

| Pose | What |
|---|---|
| `S_1` | "CONGRATULATIONS FUS", half painted, swaying a little (loop 120) |
| `S_2` | "CONGRATULATIONS LUIGI" (after the boss; loop 120) |
| `S_3` | "CONGRATULATIONS LUIGI & FUS" (after the proposal; loop 120) |
| `C_2` | turning over from FUS to LUIGI on a gust, a paper flip (30, held; then S_2) |
| `C_3` | turning over from LUIGI to LUIGI & FUS (30, held; then S_3) |

`c_sl_cage`:

| Pose | What |
|---|---|
| `S_1` | standing shut (loop 60) |
| `H_1` | rattled: the brides shaking the bars (loop 16) |
| `O_1` | opening: a rattle, the near bars swing open on their hinge at the front edge (36, held) |
| `O_2` | standing open (loop 60) |

Seen in game (US, as NPCs on a Fahr Outpost map): the banner hung at a
lintel turning from FUS to LUIGI; the cage round Fuselle (the near bars in
front of her, the far ones behind) and swinging open.

**Sergeant Bulkhead's stripe** (`c_sl_bulkhead`, `tools/port/sl_bulkhead.py`):
a variant of the disc's `c_heavy` (the Bulky Bob-omb), the stripe card hung
from his body `pPlane2`. Variants can now go on a disc model: `sl_models.py`
reads the base from the disc (`variant_base`) and the port, finding no file
of its own by that name, reads the disc's (`sl_models_aurora.c`, `baseFile`).
A draw's index tables are each its own: a draw names where its positions',
normals', colours' and texture coordinates' indices start, and a disc model's
tables needn't be the same length (c_heavy's four untextured planes have
position indices but no texture ones: 48 against 32). The card's draw once
took all four from the position indices' count, so the stripe read texture
indices past the table's end, a transparent corner of its drawing, and drew
nothing. `sl_attach.card_records` (and `luigi_pack.plane_records`) now count
each table on its own; Luigi's variants are byte for byte as they were (his
tables all match). Seen in game (US, NPCs on bom_98): the stripe high on his
back, riding his walk.

**Petunia Primrose** (`c_sl_petunia`, `tools/port/sl_petunia.py`): the
disc's `c_pansy` (the Crazee Dayzee) with pink petals, a variant with no
cards, only a texture swap (`SWAPS = {8: drawing}`,
`port/assets/super_luigi/art/ch4_fans/petunia_petals.png`): our drawing goes
after the base's, and the texture bind showing image 8 is set to it, a field
of another table than the groups' (`luigi_pack.table_patch`: the table's
index + 1 in the patch's top byte, applied by `mergeModel` after the
tables). Her model only, so every other Crazee Dayzee stays lilac. The port
checks that our first drawing (a swap's or a card's) follows the base's
textures. c_pansy's empty tables (texture coordinate sets 1 to 7) have stray
offsets, as 38 of the disc's models do; `mergeModel` skips an empty table
rather than refusing the model. Seen in game beside a plain Crazee Dayzee.

### Chapter 3's props (`c_sl_crown`, `c_sl_mic`, `c_sl_lights`, `c_sl_scoreboard`, `c_sl_trophy`, `c_sl_wheel`, `c_sl_tarp`, `c_sl_shutter`, `c_luigi_crown`; `tools/port/sl_ch3_props.py`, `tools/port/sl_crown_luigi.py`)

- `c_sl_crown`: the King's crown, 8 x 6 units, its origin the middle of its
  base: on Revvo's head (`togenoko_ace`) at 1:1, the NPC placed on his head.
  Luigi's crowning wears the same card on his cap: `c_luigi_crown`
  (sl_crown_luigi.py).
- `c_sl_mic`: Lakky's microphone, its origin at the grip (the Lakitu's hand).
- `c_sl_trophy`: the Grand Prix's trophy, 8 x 14 units on the ground, and the
  compass piece on its lid (`piece`, its own card): S_1 on show, P_1 the piece
  lifted off and away (24, held), S_2 the lid bare.
- `c_sl_wheel`: Big Green 01's stolen steering wheel, 5 units across, its
  origin under its rim: S_1, T_1 a slow swing.
- `c_sl_tarp`: the tarp over the kart in Torque's garage, the kart's length,
  its hem high enough to show the wheels: S_1 draped, K_1 whisked off (30, held).
- `c_sl_shutter`: the Parts Depot's jammed roll-up shutter, 33 x 30 units,
  hanging from its top edge: S_1 a judder now and then, O_1 rolling up (40, held).
- `c_sl_lights`: the start lights, 10 x 5 units, hanging from their origin
  (the hooks, where the fishing rod's line ties on). One card, `lights`,
  whose drawing is the count: binds 0 off, 1, 2, 3 red, 4 green.
- `c_sl_scoreboard`: the big board on legs behind the first corner, 60 x 34
  units on legs 22 tall, its origin on the ground at its middle: `sign` (the
  mover, tipping about its feet: rx < 0 forward, toward the camera) > `legs`
  and `board` (hinged at its bottom edge (0, 22), to flex) > `board_face`
  and `board_back` (turned round, so it shows when the board lies face
  down). It tips over forward onto the pile.

`c_sl_crown`:

| Pose | What |
|---|---|
| `S_1` | still (loop 60) |
| `P_1` | put on: lowered onto the head, settling (24, held) |
| `K_1` | knocked off: flung up and back, spinning, gone (36, held) |

`c_sl_mic`:

| Pose | What |
|---|---|
| `S_1` | held (loop 60) |
| `T_1` | shouting into it, waving it (loop 16) |

`c_sl_lights`:

| Pose | What |
|---|---|
| `L_0` | off, hanging, swaying a little (loop 60) |
| `L_1` | one red (loop 60) |
| `L_2` | two red (loop 60) |
| `L_3` | three red (loop 60) |
| `L_G` | green: go (loop 60) |
| `S_1` | as L_0 |
| `C_1` | the countdown: red at 20, 60 and 100, green at 140, each with a jolt (160, held green) |

`c_sl_scoreboard`:

| Pose | What |
|---|---|
| `S_1` | standing (loop 60) |
| `W_1` | shaken: a wobble (loop 12) |
| `F_1` | tipping over forward onto the pile: rocking, falling face down, slamming onto the pile at 60 degrees, a bounce (48, held) |
| `F_2` | lying on the pile, face down (loop 60) |

`c_luigi_crown`: `c_luigi_crown`, a variant of c_luigi_pc (sl_attach.py, as the bride) with the King's crown (port/assets/super_luigi/art/ch3_props/crown.png) on his cap: the card at 0.8x, tilted a little back, sitting on the cap's crown, hung from the cap (`pPlane4`) so it rides every pose of his. No poses of its own: every one of Luigi's plays with the crown on.

The scoreboard leans at 60 degrees when it lands: from the game's low camera
a board tipped flat is a sliver, so it rests on the pile, its face toward the
camera, Revvo under it.

**The posters and the sign** (`tools/port/ch3_textures.py`): the art's
racing posters and Circuit Break sign as texture packs named as Dolphin names
the textures they replace, in `port/assets/super_luigi/textures/`:
`ch3_posters` (tou_02's other posters, yuu_02's "Pianta Parlor" neon),
`ch3_posters_revvo` (tou_02's Glitz Battle posters, before the race) and
`ch3_posters_revvo_after` (the same names, the fourth Big Green 01: after
the race, in place of `ch3_posters_revvo`). The story code turns the sets on
for the chapter's maps (as `ch1_lava`). The tool checks each name against the
disc's texture (XXH64, Dolphin's hash) when given the disc's files folder.

Seen in game (US): the crown on Luigi's cap through his stand, a crown being
put on, Lakky's microphone waving, the lights' countdown to green, the
scoreboard rocking and tipping onto the pile, on a Fahr Outpost map; tou_02's
posters as racing posters and Revvo's (the after set) with the sets in the
user folder's `textures/`. The sign's name is checked against the disc (it
isn't in view where yuu_02 starts).

### Luigi as the Grass (`c_luigi_grass`, `tools/port/sl_grass.py`)

Cards, pinned as the art's README has them (c_luigi's bind pose):
  grass_back    the far blades, behind all of his cards (his body, pPlane17)
  grass_front   the near blades over his body and legs, in front of them and
                behind his front glove (pPlane17); binds 0 still, 1 swept back
  grass_hood    the mound and crown of blades over his cap and down the back
                of his head (his head, pPlane14); binds 0 still, 1 swept back
Every pose of Luigi's plays with the costume on (the battle's jump and hammer,
hurt, KO...), the blades following his body and head; its own poses below. The near blades sit 1.5 units above the
art's pin (`FRONT_LIFT`), so his shirt doesn't show between them and the hood.

| Pose | What |
|---|---|
| `GR_S` | standing still, being grass: a breath, a blink (loop 120) |
| `GR_W` | the shuffle: small hops, the blades swinging back on each (loop 32) |
| `GR_L` | lying flat on his front (24, held; Lie Low) |
| `GR_L2` | lying flat, one eye opening now and then (loop 90) |
| `GR_R` | the rustle, meaningfully: a shimmy, the blades flicking (loop 20; the bow he can't do) |
| `GR_J` | joy (the fans): rustling, hopping (loop 16) |

- **Scenes:** `evt_npc_entry(name, "c_luigi_grass")`. **Playing as the Grass**
  (the rehearsal, the play's battle): the story code switches the stand-in's
  model name to `c_luigi_grass`, as for the bride.

### Chapter 4's props (`tools/port/sl_ch4_props.py`)

- `c_sl_fiery_hat`: the Fiery Hat, its origin the brim's middle (set it on
  Jerry's top, in front of his fuse cap): `hat` > `flames` (on the crown's
  top, binds 0 and 1 the two flame frames, flickering).
- `c_sl_chandelier`: its origin on the floor under it; the chandelier hangs
  from its top ring at y 44 on a chain of four cards up to y 76 (the
  ceiling), `chain_1`..`4`; `lamp` (the mover) > `chandelier`, and
  `shard_1`..`6` (hidden, thrown out in the crash).
- `c_sl_set_road`, `c_sl_set_ballroom`, `c_sl_set_volcano`: the three acts'
  painted backdrops, 64 x 32 units, standing on their bottom middle (the
  stage floor at the back). The road has `sun` (the cardboard sun on its
  stick, at x 18, raised from below the floor by a stagehand); the volcano
  has `flame` (the eruption's card, rising out of the crater at (0, 22.75),
  binds 0 and 1 its two frames).
- `c_sl_leaf`: a fan's paper leaf on a stick, its origin the grip (binds 0 a
  leaf, 1 the Grass's own fans' paper grass). `c_sl_grass_banner`: "WE ♥ THE
  GRASS", hanging from the middle of its top edge.
- `c_sl_dazzlina_tiara` (her sequin tiara, on the Amazy Dayzee's head at
  1.2x with her) and `c_sl_baron_hat` (the Baron's tiny top hat, on Hayzee's
  head, tipped a little): each its origin at its foot.

`c_sl_fiery_hat`:

| Pose | What |
|---|---|
| `S_1` | the flames flickering (two frames every 5; loop 20) |
| `B_1` | blazing: the flames leaping (loop 12) |

`c_sl_chandelier`:

| Pose | What |
|---|---|
| `S_1` | hanging, swaying a little (loop 120) |
| `W_1` | swinging hard (loop 30) |
| `F_1` | the drop: the chain snaps, it falls to the floor (contact at 16), smashes, crystals flying, settling askew (48, held) |
| `F_2` | fallen (loop 60) |

`c_sl_set_road`:

| Pose | What |
|---|---|
| `S_1` | the road, the sun not up (loop 120) |
| `U_1` | the sun raised by a stagehand, jerkily (60, held) |
| `S_2` | the sun up, bobbing on its stick (loop 60) |
| `K_1` | the sun knocked over, out of sight (30, held; Prop Smash) |

`c_sl_set_ballroom`:

| Pose | What |
|---|---|
| `S_1` | the ballroom (loop 60) |

`c_sl_set_volcano`:

| Pose | What |
|---|---|
| `S_1` | the volcano, quiet (loop 60) |
| `E_1` | the eruption: a rumble, the flame bursting up out of the crater (48, held, flickering) |
| `E_2` | erupting, the flame flickering (loop 12) |

`c_sl_leaf`:

| Pose | What |
|---|---|
| `S_1` | the leaf held up (loop 60) |
| `W_1` | the leaf waved (loop 16) |
| `S_2` | the paper grass held up (loop 60) |
| `W_2` | the paper grass waved (loop 16) |

`c_sl_grass_banner`:

| Pose | What |
|---|---|
| `S_1` | hanging, swaying (loop 90) |
| `W_1` | held up by two fans, bouncing (loop 24) |

`c_sl_dazzlina_tiara`:

| Pose | What |
|---|---|
| `S_1` | glittering, a small pulse (loop 40) |

`c_sl_baron_hat`:

| Pose | What |
|---|---|
| `S_1` | tipped a little (loop 60) |
| `T_1` | doffed and bobbing as he talks (loop 20) |

Seen in game (US, as NPCs on a Fahr Outpost map): the Grass standing,
rustling and lying flat; the hat's flames; the chandelier's drop and smash;
the volcano erupting; a fan's paper grass waving; the tiara. The road's sun,
the ballroom, the banner and the Baron's hat checked in Blender.

### Chapter 5's props (`tools/port/sl_ch5_props.py`)

- `c_sl_socket_moon`, `c_sl_socket_sun`, `c_sl_socket_tear`: a stone stand
  round the dais, its origin on the ground at its middle: `socket`, `lens`
  (hidden until set in; its foot in the socket's cap at (0, 6.6)) and `beam`
  (hidden: the light rising from the lens, 8 x 32 units from (0, 9) up;
  scale its sy for a taller room).
- `c_sl_mural`: a mural, a card 100 x 50 units (the wall's own grain, 2.56
  texels a unit), its origin on the bay's floor, the card from y 45 to 95
  (centred at 70, murals/README.md): binds 0..5 the six panels, 6 the sixth
  broken by Jerry's Blast. A pose per panel. Placed in hei_09's six bays at
  x -400, -240, -80, 80, 240, 400, z -103.5.
- `c_sl_look_edge_25`, `_50`, `_100`, `_200`: Look's glowing edge strips for
  the Room of Nothing, that many units long along x, 2 units wide, centred on
  their origin, with a round cap at each end; `_v` versions stand upright
  (along y, from the origin up). Turn an NPC 90 degrees about y for an edge
  along z. Binds 0 the glow, 1 the pulse (as the Look lands).
- `c_sl_gear_gate`: the Luff gear-gate, its origin on the ground at the
  arch's middle: `gate_teeth` (behind the arch, hung from `lift` at the
  arch's crown: the teeth draw up into the stone as it rises, a notch of 3
  units per push), `gate_frame`, and `gear` on the wall to its left (binds 0
  jammed with grit, 1 clean after the Tune-up).
- `c_sl_signpost`: the Luff signpost, its origin on the ground: `post`,
  `pointer` (on its axle at (0, 10.4): drooping while broken, swinging round
  to point when fixed), `crank` (on the axle, in front).

The beams and the glow strips are soft light in the art; the model's
textures have 1-bit alpha (CMPR, as every model's), so their soft alpha is
ordered-dithered (a 4x4 Bayer screen), which reads as translucent light at
the game's distance.

`c_sl_socket_moon`, `_sun`, `_tear`:

| Pose | What |
|---|---|
| `S_1` | the empty socket (loop 60) |
| `P_1` | the lens set in: lowered, a click, a settle (30, held) |
| `S_2` | the lens in place (loop 60) |
| `L_1` | lit: the beam shooting up to the ceiling (40, held) |
| `L_2` | shining, the beam pulsing (loop 30) |

`c_sl_mural`:

| Pose | What |
|---|---|
| `M_1` | panel 1 (loop 60) |
| `M_2` | panel 2 (loop 60) |
| `M_3` | panel 3 (loop 60) |
| `M_4` | panel 4 (loop 60) |
| `M_5` | panel 5 (loop 60) |
| `M_6` | panel 6 (loop 60) |
| `M_6B` | the sixth, broken through by the Blast (loop 60) |
| `S_1` | as M_1 |
| `B_1` | the Blast on the sixth: a shudder, then the hole (20, held) |

`c_sl_look_edge_*`:

| Pose | What |
|---|---|
| `S_1` | glowing (loop 60) |
| `L_1` | the Look landing: the brighter, wider pulse for 8 frames, then the glow (40, held); the fade over the last second of the 8 is the NPC's alpha (the story code's) |

`c_sl_gear_gate`:

| Pose | What |
|---|---|
| `S_1` | shut, the gear jammed with grit (loop 60) |
| `J_1` | the jammed gear shuddering, stuck (16) |
| `S_2` | shut, the gear clean (loop 60) |
| `P_1` | a push: the gear an eighth of a turn, the teeth up a notch (20, held; P_2..P_5 from the notch below; P_5 opens it) |
| `N_1` | at notch 1 (loop 60; N_2..N_4, N_5 open) |
| `D_1` | falling back shut from notch 1 (16, held; D_2..D_4 from higher) |

`c_sl_signpost`:

| Pose | What |
|---|---|
| `S_1` | broken: the pointer drooping (loop 80) |
| `T_1` | the crank turning, still broken (loop 30) |
| `F_1` | fixed: the crank wound, the pointer swinging round to point (60, held) |
| `S_2` | fixed, pointing (loop 60) |

Seen in game (US, as NPCs on a Fahr Outpost map): the sun lens's beam
shooting up, the sixth mural broken by the Blast (the hole showing what's
behind), the gate's clean gear turning it up two notches, the signpost
fixed, a flat and an upright glow strip.

### The prologue's and chapters 5 and 6's people

Four rigs of the same kind (`tools/port/sl_rig.py`: the art's cards placed
by `parts.json`'s pivots and `model_xy`, a mover, a body, arms and feet,
a head with its drawings as binds; arms turn at the shoulder, rz < 0 swings
one forward), each its own module.

#### Minister Crepe (`c_sl_crepe`, `tools/port/sl_crepe.py`)

Parts: crepe (the mover) > foot_back, foot_front, robe (the hips' pivot) >
cape, arm_back, arm_front (shoulders; rz < 0 swings an arm forward) > scroll,
compass (props in the front glove, hidden), head (the neck; binds 0 rest, 1
blink, 2 talk, 3 eyes gold) > mustache (under the nose), hat (the brow).

| Pose | What |
|---|---|
| `S_1` | stand (loop 80) |
| `Z_1` | idle: blinks, a moustache twitch (loop 80) |
| `T_1` | talking, a finger raised (loop 36) |
| `T_2` | talking smugly, rubbing his hands (loop 32) |
| `W_1` | a pompous waddle (loop 40) |
| `R_1` | hurrying (loop 24) |
| `W_2` | walking down stairs (loop 40) |
| `CL_1` | a slow clap (loop 40) |
| `I_1` | holding out the letter (24, held) |
| `I_2` | presenting the compass's base with a bow (36, held) |
| `PU_1` | the pull: hand out, the compass flies to him (40, held, the compass in hand) |
| `G_1` | looking into the compass, eyes gold (loop 40) |
| `A_1A` | Flambé, the wind-up (24, held) |
| `A_1B` | Flambé, the fan swept (the flame at 4; 30) |
| `A_2A` | Fold, gathering the cape (20, held) |
| `A_2B` | Fold, the cape flung round (hit at 6; 36) |
| `A_3A` | Batter, arms up (16, held) |
| `A_3B` | Batter, three pounds (hits at 4, 10, 16; 28) |
| `A_4` | Cabinet Reshuffle: a turn, a flourish, a bow (48) |
| `A_5A` | Compass Bolt, charging, eyes gold (loop 20) |
| `A_5B` | Compass Bolt, released (at 4; 30) |
| `E_1` | the sidestep (Foresight): a hop 8 units back (24, held there) |
| `D_1` | hurt (loop 12) |
| `D_2` | unfolding into the Nightmare (60, held) |
| `K_1` | kneeling small, after it all (loop 60) |
| `D_3` | defeated, flat on his back (40, held) |

`D_2` stretches him tall, eyes gold, the cape rising, for the battle to swap
in the Nightmare (`c_sl_nightmare`, below, its `E_1`) at its end.

#### Princess Eclair (`c_sl_eclair`, `tools/port/sl_eclair.py`)

Parts: eclair (the mover) > feet, dress (the hips) > hair (behind, from the
crown), arm_back, arm_front (shoulders; rz < 0 swings an arm forward), head
(the neck; binds 0 rest, 1 blink, 2 talk) > tiara (binds 0, 1 glowing).

| Pose | What |
|---|---|
| `S_1` | stand (loop 80) |
| `Z_1` | idle, blinking (loop 80) |
| `T_1` | talking, a hand to her chest (loop 32) |
| `W_1` | a graceful glide (loop 40) |
| `R_1` | running, hair streaming (loop 20) |
| `P_1` | arms out, protecting the King (loop 40) |
| `TI_1` | raising the tiara: lifted from her head, then held up and out in front of her, clear of her face, half as large again on her glove, blazing (40, held) |
| `TI_2` | holding the tiara up, glowing, pulsing larger (loop 24) |
| `E_1` | the embrace (loop 60) |
| `K_1` | kneeling (loop 60) |
| `B_1` | battle: worried at the back of the stage (loop 60) |
| `Y_1` | battle: cheering, hopping (loop 20) |

#### The Chestnut King (`c_sl_chestnut_king`, `tools/port/sl_chestnut.py`)

Parts: king (the mover) > cape (behind), leg_back, leg_front (the hips),
nut (the body inside the husk, pivot at its base) > husk_left, husk_right
(the burr's halves, meeting at x 0; rz > 0 swings the left half open, < 0
the right), head (the neck; binds 0 rest, 1 roar, 2 hurt, 3 sad, 4 calm) >
crown, goo (the drip at the jaw; binds: three frames), target_skull (a
locator); target_husk (a locator in the husk's middle); shard_1..4 (the husk's
pieces, hidden), nut_1..3 (chestnuts thrown up in Chestnut Rain, hidden).

The battle's two parts (ch6: Husk and Skull) follow the locators
`target_husk` and `target_skull`.

| Pose | What |
|---|---|
| `S_1` | standing: heavy breathing, goo dripping (loop 96) |
| `W_1` | a lumbering walk (loop 60) |
| `A_1A` | Fang Lunge, rearing back (30, held) |
| `A_1B` | Fang Lunge (hit at 5; 30) |
| `O_1` | the overreach: past, off balance, the husk split open, the skull low (40, held) |
| `O_2` | recovering from it, the husk closing (30) |
| `H_2` | the overreach's turn: open and low (loop 48) |
| `A_2A` | Goo Drool, leaning in (24, held) |
| `A_2B` | Goo Drool (the stream from 6; 40) |
| `A_3` | Chestnut Rain: a shake, chestnuts flying up (48) |
| `R_2` | Roar (60) |
| `Q_1` | hesitating: a look back at the curtain, a tear (60) |
| `D_1` | hurt (loop 12) |
| `K_1` | on his knees, staggering (loop 60) |
| `C_1` | the curse breaking: the husk falls away in pieces, the face calm (80, held) |
| `S_3` | King Marron (the husk gone, calm), standing (loop 96) |
| `T_3` | King Marron talking (loop 32) |

A battle pose table: `NORMAL` `S_1`, `DAMAGE` `D_1`, `WALK` `W_1`, `TALK`
`T_3` (as Marron), 27 `K_1`, `STAY_DEFAULT` `S_1`; after an overreach the
turn's stand is `H_2`.

#### Cranberry (`c_sl_cranberry`, `tools/port/sl_cranberry.py`)

Parts: boy (the mover) > body (seated, the hips) > arm_back, arm_front (the
shoulders; rz < 0 swings forward), reach (the arm held out, palm up; hidden)
> piece (the compass piece on the palm; hidden), head (the neck; binds 0
asleep, 1 waking, 2 awake, 3 smiling, 4 talking).

| Pose | What |
|---|---|
| `N_1` | asleep, seated (loop 120) |
| `U_1` | waking: a start, a blink, a smile (90, held) |
| `S_1` | awake, seated, blinking (loop 80) |
| `S_2` | smiling (loop 80) |
| `T_1` | talking softly (loop 36) |
| `H_1` | the hand-over: arm out, the piece on his palm (40, held) |
| `F_1` | fading: rising a little, eyes closed, smiling (120, held; the fade is the scene's alpha) |

Seen in game (US, as NPCs on a Fahr Outpost map): Crepe presenting the
compass's base and charging Compass Bolt, Eclair raising the tiara, the King
standing and overreaching (the husk open, the skull low), Cranberry handing
over the piece. The rest checked in Blender only.

### Chapter 6's models (`c_sl_nightmare`, `c_sl_goo_pit`, `c_sl_goo_moat`, `c_sl_marron`; `tools/port/sl_ch6.py`)

- `c_sl_nightmare`: Crepe joined with the six pieces, battle only: a tall
  shadow 48 units to the top of its compass face (the art is 4 texels a
  unit). Parts: nm (the mover) > body (the hem's middle; binds 0, 1
  cracked), arm_back, arm_front (the shoulders; the arm reaching forward and
  down; rz < 0 raises it), stub_back, stub_front (the torn stumps, hidden:
  shown when an arm is lost), face (the dial's centre; binds 0 the dial, 1
  open (Hatesong), 2 cracked) > needle (rz 0 points up, 90 forward (-x) at
  the target), shard_1..6 (glowing shards of the shatter, hidden), and the
  locators target_body (the face) and target_arm_front / target_arm_back
  (each hand) for the battle's three parts.
- `c_sl_goo_pit` (48 x 48 units) and `c_sl_goo_moat` (192 x 48): the green
  goo, a floor of tiles lying flat (its origin at the middle, at floor
  height), churning between the surface's two frames, bubbles swelling and
  popping, wisps of pale smoke rising (dithered: soft in the art).
- `c_sl_marron`: King Marron restored, about 31 units to the leaf crown:
  marron (the mover) > boots, coat (the hips) > cape (behind), arm_back,
  arm_front (shoulders; rz < 0 swings forward), head (binds 0 rest, 1 blink,
  2 talk, 3 happy) > crown.

`c_sl_nightmare` (battle only):

| Pose | What |
|---|---|
| `S_1` | idle: hovering, swaying, the needle spinning (loop 60); `S_1F` `S_1B` `S_1N` the same with the front, the back or both arms lost (the stumps showing) |
| `T_1` | talking: the face swelling on each word (loop 32) |
| `W_1` | gliding (loop 40) |
| `R_1` | gliding fast (loop 20) |
| `D_1` | hurt (loop 12); `D_1F` `D_1B` `D_1N` with arms lost |
| `A_1A` | Doom Foretold: the needle spinning, slowing, stopping on its target, forward (40, held) |
| `A_1C` | the count: the needle quivering on the target (loop 20) |
| `A_1B` | the strike: rearing, both arms slammed down on the target (hit at 10; 40) |
| `A_2A` | Hatesong: the dial splitting open into a howling mouth, rising (24, held) |
| `A_2B` | the howl, shaking (loop 12) |
| `A_3` | A Thousand Tomorrows: five quick page-slaps (hits at 8, 14, 20, 26, 32; 48) |
| `A_4A` | Page Grab: the back arm lunging out, closing on the partner (24, held) |
| `A_4B` | holding them (loop 30) |
| `A_4C` | letting go (20) |
| `L_1F` | the front arm lost: torn off, flung back, gone, the stump left (24, held; `L_1B` the back arm) |
| `G_1F` | the front arm regrowing from the stump (30; `G_1B` the back arm) |
| `X_1` | stunned: drooping, the needle wobbling (loop 48) |
| `E_1` | its entrance: unfolding up out of Crepe's crumple, the needle starting (48, held) |
| `C_1` | Smite lands: cracking, light breaking through, a shudder (40, held) |
| `C_2` | the shatter: bursting into shards of light that fly out and vanish (60, held: nothing left) |

A battle pose table for it: `NORMAL` `S_1` (`S_1F` `S_1B` `S_1N` while arms
are lost), `DAMAGE` `D_1` (likewise), `WALK` `W_1`, `RUN` `R_1`, `TALK` `T_1`,
3 and 5 `X_1`, `STAY_DEFAULT` `S_1`; the three parts (Body, the two Arms)
follow `target_body`, `target_arm_front`, `target_arm_back`. The battle opens
with `E_1` after Crepe's `D_2`; Smite plays `C_1` then `C_2`.

`c_sl_goo_pit`, `c_sl_goo_moat`:

| Pose | What |
|---|---|
| `S_1` | churning: the surface's two frames every 30, bubbles swelling and popping, wisps rising (loop 120) |

`c_sl_marron`:

| Pose | What |
|---|---|
| `S_1` | stand, blinking (loop 96) |
| `T_1` | talking, a hand raised (loop 32) |
| `W_1` | walking (loop 48) |
| `R_1` | running to her (loop 24) |
| `WV_1` | waving goodbye (loop 24) |
| `H_1` | the embrace with Eclair: arms forward and in, eyes happy (loop 60) |
| `U_1` | restored: coming to where the husk fell away, looking at his hands, then up (90, held) |
| `B_1` | a bow of thanks (40) |

In the wave his front arm is drawn behind his face (its `tz`), so the sleeve
never crosses it. The curse breaking: the Chestnut King's `C_1` throws the
husk off; the scene then swaps him for `c_sl_marron` playing `U_1`.

Seen in game (US, as NPCs on a Fahr Outpost map): the Nightmare idle, its
Hatesong mouth, the crack and the shatter into shards; the goo pit lying on
the floor, churning, wisps rising; King Marron waving and in the embrace
beside Eclair.

## Story poses (Super Luigi chapters 2-6)

From the chapter plans' "New assets" and beats (`docs/super_luigi/ch*.md`).
Luigi's are in `luigi_motions.py`, named `SL_*` (none of Mario's stands for
them; the story scripts ask for them by name on `c_luigi_pc`); the friends'
in `friend_motions.py`.

| Done | Who | Pose | For |
|---|---|---|---|
| yes | Luigi | `SL_HANG` hanging from both hands, legs swinging (loop 48) | ch5: Screamy's Float carries him |
| yes | Luigi | `SL_SNEEZE_A` the "ah... ahh..." (60, held at the top), `SL_SNEEZE_B` the "CHOO", thrown forward, then sheepish (40) | ch5 |
| yes | Luigi | `SL_WOBBLE` woozy (loop 40), `SL_FAINT` fainting flat (24, held) | ch3 (the wobble), ch2 (faints; the battle's "Luigi fainted!") |
| models agent | Luigi the bride | twirl, faint (on `c_luigi_bride`) | ch2 |
| yes (`c_luigi_grass`) | Luigi as the Grass | `GR_S` stand, `GR_W` shuffle, `GR_L` `GR_L2` lie flat, `GR_R` rustle, `GR_J` joy; battle: all of Luigi's | ch4 (a costume model) |
| yes | Blooey | `F_2` diving into a well (30, held: gone below), `F_3` surfacing (30) | ch2: the pearls |
| yes | Blooey | `N_1` asleep | ch2, ch3 |
| yes (`BURN_*`, `SOOT_*`) | Blooey, burnt | every pose with the disc's brown burnt drawing (`BURN_*`), and soot black with embers and a smoke wisp (`SOOT_*`, as the plan has him; made at startup from the disc's drawing) | ch6 |
| yes | Hayzee | `E_2` joining: a hop, both leaves up in front | ch4 |
| yes | Jerry | `L_1` the fuse lit, hurrying (loop 36); `F_1` Blast | ch2 |
| yes | Torque | `F_1` Tune-up (the wrench, loop 24), `F_2` its finish: head out, a claw up (30, held) | ch3 |
| yes | Hayzee | `F_1` Sing, `F_2` a held note (loop 40), `A_3C` conducting (loop 24), `O_2` the bow (30) | ch4 |
| yes | Screamy | `F_2` Float carrying Luigi, straining up (loop 30), `F_3` Look, eyes wide, peering (loop 40), `E_1` a sigh (40), `A_2B` the scream | ch5 |
| yes | all | battle sets (above) | ch2-6 |

## Soot-black Blooey

Chapter 6's Blooey, scorched black as the plan has him, without a drawing of
the disc's in the repository: the port makes it at startup.

- `friend_extras.py` gives `c_lp_gesso` two things beyond animations: a third
  bind on his plane's texture (`tex` 2; 0 is Blooey, 1 the disc's brown burnt
  one) and a small card, `pSmoke`, a wisp of smoke of our own drawing above
  his head (his plane's child, so it rides every move; hidden unless a pose
  shows it). Both go into his pack (LUPK records and a group patch, as
  Luigi's hammer).
- His textures come from a recipe (`c_lp_gesso-`, a LUTX entry in the
  friends' container, `luigi_pack.make_tx`): the port reads the disc's
  `c_lp_gesso-`, decodes Blooey's drawing, darkens every texel to soot from
  its brightness, lights six ember spots on it, encodes it again and appends
  it, then the smoke card's drawing (ours, stored). Integers only, the same
  in `luigi_model_aurora.c` (`applyTx`) and Python (`apply_tx`); a game run
  with `$TTYD_FRIENDS_DUMP` and `friends-portcheck` checks the two make the
  same bytes (they do). A disc whose `c_lp_gesso-` differs keeps the disc's
  (and `SOOT_*` then shows the plain drawing).
- His poses: `SOOT_<pose>` for every one of his (and `SOOT_Z_1` `SOOT_S_1`
  `SOOT_T_1`), the smoke curling up over each. **The story code switches to
  him** by using those names: a partner pose row and battle pose table of
  `SOOT_` names (e.g. `{"SOOT_Z_1", "SOOT_S_1", "SOOT_W_1", ...}`), and
  `SOOT_` names in its scripts. `BURN_*` stays, the disc's brown burns.
- Checked in game: `TTYD_FOLLOWER=9,c_lp_gesso,SOOT_` (the third field
  prefixes the follower's pose names) shows him black with embers, the
  smoke rising, standing, walking and running.

## Luigi's story poses and Mario

Setting Mario's pose to an `SL_*` name is safe but shows nothing of it:
`a_mario` has no such animation and the game's lookup gives an unknown name
its first animation (`animPoseGetAnimIdx`), so Mario stands, and Luigi's
stand-in (which follows Mario's animation) stands too. (Checked: a follower
drawn with `a_mario` asked for `S_1` and `W_1`, names it hasn't, stands in
its first pose and slides along; nothing breaks.)

So Luigi's story poses have their own switch: `evt_luigi_story_pose(name)`
in a script (`portLuigiSetStoryAnim` in C, `port/include/port/luigi.h`) makes
Luigi play his animation `name` in the field whatever Mario's is, until
`evt_luigi_story_pose(0)`. A name his model hasn't is ignored. Checked in
game with `$TTYD_LUIGI_STORY_POSE=SL_HANG` (the same switch from the start):
Luigi hangs while Mario walks and runs under him.
