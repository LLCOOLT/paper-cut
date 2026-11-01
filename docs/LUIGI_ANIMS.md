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
