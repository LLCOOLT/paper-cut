# Play as Mario, Luigi, Peach or Bowser

An Enhancements choice, **Play as** (setting key `playable_luigi`: 0 Mario, 1
Luigi, 2 Peach, 3 Bowser; it was the "Play as Luigi" toggle, so a saved 1 is
still Luigi). Whoever is picked takes Mario's place: in the field, in battles,
in the menus' figures, in the scenes and the credits, and the text calls them
by their name. It changes how Mario looks, sounds and is called, not what he
can do: the pick uses Mario's moves, stats and collision. Saves are not
changed, and online play isn't affected (it is drawn on each screen alone).

Luigi is described in [PLAYABLE_LUIGI.md](PLAYABLE_LUIGI.md) and
[LUIGI_ANIMS.md](LUIGI_ANIMS.md). Peach and Bowser work the same way, from
their interlude models on the player's own disc.

## What the disc has

| | Model | Animations | Size |
|---|---|---|---|
| Peach | `c_peach` (front), `c_peach_r` (back) | 82 + 27: stand, idle, walk, run, talk in eight moods (`P_*_1` .. `P_*_8`), her dance with TEC (`P_O_*`), gestures | 27.6 units tall (Mario 22) |
| Bowser | `c_koopa` (his 3D interludes; `c_koopa2` is the side-scrolling stages') | 63: stand, idle, walk, run, jump, talk, fire breath (`KPA_A_1A`..`D`), hurt, his scenes' poses | 46 units tall |

Both are made of plain flat parts (like Luigi's, unlike Mario's jointed rig),
so they open in Blender as editable objects with the character tools
([CHARACTERS.md](CHARACTERS.md)). The files are the same on the JP, US and EU
discs. Peach never jumps on the disc; Bowser has no back view.

## How it works

`port/src/common/luigi_pc.c` (`port/include/port/luigi.h`), the same stand-in
as Luigi's: Mario's own pose keeps running exactly as before, and the pick's
model follows it and is drawn instead.

- Each of Mario's animations is played as: an animation of the same name in
  the pick's model (the new ones are named as Mario's; battle ones
  `<name>@b`), else the pick's disc animation for it (stand, walk, run, jump,
  talk, and for Bowser his fire breath for the hammer and his hurt poses),
  else the nearest of their plain moves by the kind of Mario's (walking,
  running, jumping, hurt, talking, an attack, or standing). So Peach and
  Bowser never fall back to Mario's model, where Luigi falls back to Mario in
  green. The one exception is `M_H_12`, where only the thrown hammer is
  drawn.
- Back-facing poses use Peach's back model (`c_peach_r`; ours of it, with
  her back-facing hammer and jump, first); Bowser shows his front.
- Bowser is drawn at 0.7 of his size (about 32 units, half again Mario's
  height), so he reads as Bowser and still fits where Mario goes.
- The disc's animations keep their own clock (they are other lengths than
  Mario's); ours take Mario's.
- Text: Mario and the pick trade names both ways in every language ("Peach"
  / "PEACH" / ピーチ, "Bowser" / "BOWSER" / クッパ), not inside other words
  (スピーチ, "Peachy"), with the same rules as Luigi's. So "Princess Peach"
  reads "Princess Mario": he is the one held captive.
- Mario plays their part: their NPCs (Peach in her scenes and the ending,
  Bowser in his), the player in their interludes, and Bowser's boss fights
  are drawn as Mario (`a_mario`), each of their animations as Mario's of the
  same kind (stand, idle, walk, run, talk, jump, hurt; the rest standing).
  The credits' parade swaps them too.
- Faces: the HP face on the status bar, the head on the pause menu's Mario
  page and the coin on its level row are Peach's or Bowser's, made at the
  start from their own models on the disc as Luigi's are
  (`port/assets/luigi/art/recipes/peach_icons.recipe`, `bowser_icons.recipe`).
  Their head steps were written from the models' own numbers by
  `tools/port/char_heads.py` (each card cut from its texture as the model maps
  it, turned and placed as the idle pose stands it); nothing from the disc is
  in the repository. Where a face can't be made, Mario's shows.
- Sounds: Mario's plain-floor steps and landing are Peach's (her jump stays
  Mario's: she has none) or Bowser's, and his jump Bowser's.

## Their new moves

As Luigi's ([LUIGI_ANIMS.md](LUIGI_ANIMS.md)): at startup the port makes
`a/c_peach_pc`, `a/c_peach_r_pc` and `a/c_koopa_pc` from the disc's models
and a pack of our own animations and a hammer plane for each
(`port/assets/luigi/peach_anims.bin`, `peach_r_anims.bin`,
`bowser_anims.bin`; `tools/port/luigi_pack.py`), the hammer showing Mario's
drawings copied from the player's `a_mario-`. If a disc model isn't the one
a pack was made for, that character keeps the disc's model and moves.

The motions are key poses per part of each model, authored in
`tools/port/peach_motions.py` and `bowser_motions.py` (the engine is
`char_motions.py`) and written into the packs in Blender by
`tools/port/char_anims.py pack --char peach|bowser`; `char_sheet.py --char`
draws them without Blender for review.

| Mario's | Peach | Bowser |
|---|---|---|
| `M_H_1` `M_H_4` `M_H_7` (and R) | the field hammer, one-handed, her free arm out | the field hammer, a stamp and a roar |
| `M_H_2` `M_H_5` `M_H_8` (and R) | hitting a wall | hitting a wall |
| `M_J_1A` `M_J_1B` `M_J_1C` (`1BR` `1CR` from behind) | her own jump: a spring, arms out, the skirt billowing | (his disc jump) |
| `M_A_1A` `M_A_1B` | the spin jump, tucked | the spin jump, into his shell |
| `M_D_1` `M_D_2` `M_U_3` | hurt, knocked flat, up again | (his disc hurt poses) |
| `M_I_1` | holding up an item | holding up an item |
| battle `@b`: `M_C_3`, `M_J_1D`, `M_A_3A` `M_A_3B`, `M_H_1A`..`C` (and 5, 9) | guard, the stomp's bounce, the jump attack, the hammer attack; `M_V_1` a curtsy and a wave | guard, bounce, stomp, the hammer attack (victory: his disc laugh) |
| the spin hammer `M_H_3` `3A` `3B` (and 6, 9) | wound up, braced, whirled flat | the same, his bulk behind it |
| `M_I_Y` `M_V_2`, hurt and worried `M_D_6`, low HP `M_S_3`, peril `M_I_S` | a start, a fist pump, hands to her cheeks, a tired sway, hunched and gasping | the same, with his jaw and glare |
| riding Yoshi `M_Y_1`..`3` | side-saddle, her skirt gathered over his back | squatting on him, feet out in front |
| the spring `M_P_1` `M_P_2`, lifting `M_U_2` (and R), `M_W_7`, arms up `M_S_4` `M_W_6` `M_J_5A` `M_J_5B` | a bend and a leap; arms stretched up to hold Bobbery where Mario does | the same, in his own reach |
| pushing `M_O_1` `M_O_2` (and R), a throw `M_C_1` (and R), the Appeal `M_C_2`, Flurrie's ride `M_A_2A`..`C`, gestures `M_B_1`..`3` `M_X_1` `M_I_U` `M_Z_9` `M_N_10` | her own, the skirt and hair swinging after her | his own, stamping; R names front-on |
| the story's poses (`e_mario`: nods, talking, listening, thinking, marching, stamping, surprise, sitting, dazed, asleep, eating, hopping for joy, ...) | to Mario's frames: her head turns, her eyes and mouth (her own drawings) change where his face does | the same with his head, jaw and eyes; lying dazed, asleep, keeling over and getting up are his disc poses |
| the rest of battle: `M_N_8` `M_A_4`..`6` `M_Z_10` `M_Z_11` `M_C_4` `M_C_2A`..`C` `M_P_1` `M_P_2`, the hammer moves `M_H_2`..`4` `7` `8` `10` `11` `13`..`19` | a cheer, a tucked somersault, knocked flying, stunned, laid out, an item held out, the Appeal; every hammer move to Mario's hammer angles | the same (stunned and laid out: his disc poses) |

`char_sheet.py --motions peach_motions` (or `bowser_motions`) draws the
motions straight from the script, without Blender and before a pack is
written (keys joined in straight lines, so a little stiffer than the game's
curves): the quick way to pose them.

## Limitations

- Every animation of Mario's has one of theirs (Bowser's back-facing ones are
  his front-facing). If a disc's model isn't the one the packs were made
  for, they play the nearest of their interlude set instead.
- The pause menu's first tab ("Mario") and level-up's BP panel are still
  Mario's for Peach and Bowser (Luigi has his own).
- Collision, speed and jump are Mario's.
- The credits draw Bowser at his full size.
