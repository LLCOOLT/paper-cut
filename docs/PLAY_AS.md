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
- Back-facing poses use Peach's back model (`c_peach_r`); Bowser shows his
  front.
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

## Limitations

- Peach's and Bowser's moves are their interlude set and the nearest of it
  where they have no move of Mario's (no hammer, spin jump or battle
  attacks of their own yet: Bowser breathes fire where Mario swings the
  hammer, Peach spreads her arms where Mario jumps).
- The pause menu's first tab ("Mario") and level-up's BP panel are still
  Mario's for Peach and Bowser (Luigi has his own).
- Collision, speed and jump are Mario's.
- The credits draw Bowser at his full size.
