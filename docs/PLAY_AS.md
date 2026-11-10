# Play as Mario, Luigi, Peach or Bowser

Settings > Enhancements > Characters → **Play as** (setting key `playable_luigi`: 0 Mario, 1
Luigi, 2 Peach, 3 Bowser; it was the "Play as Luigi" toggle, so a saved 1 is
still Luigi). Whoever is picked takes Mario's place: in the field, in battles,
in the menus' figures, in the scenes and the credits, and the text calls them
by their name. It changes how Mario looks, sounds and is called, not what he
can do: the pick uses Mario's moves, stats and collision (two additions:
Bowser's [fire breath](#fire-breath), and Luigi's jump is a little higher, a setting of its own:
[PLAYABLE_LUIGI.md](PLAYABLE_LUIGI.md), His jump). Saves are not changed. Brothers mode turns it off while on (Mario and Luigi as
themselves). Online, the host's pick is everyone's (it is one of the settings the
host sends), so every screen shows the same text.

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
discs. Peach never jumps on the disc; Bowser has no back view (his is made,
below).

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
  green. The one exception is `M_H_12` (and `M_H_4`, `M_H_8` on the thrown
  hammer's part), where Mario's model shows only Hammer Throw's thrown
  hammer: Luigi throws Mario's hammer, Peach her closed parasol (spinning
  along the same arc, in her hammer rank's colours: her model drawing only
  its parasol plane, put on the hammer's spin), and Bowser a fireball (see
  [Fire breath](#fire-breath)).
- Back-facing poses use Peach's back model (`c_peach_r`; ours of it, with
  her back-facing hammer and jump, first) and Bowser's back view. The disc has
  none of him from behind, so `c_koopa_r_pc` is made at the start from his
  own `c_koopa`: the same parts, placed as he looks from behind and a little
  to his side (his shell over his back as one smooth dome, with more spikes
  copied from his own and a row of them down his spine; its white rim one
  curved band round the dome's bottom, the dome cut along it so none of its
  green shows below; his tail toward you, the back of his head
  centred over the shell's top with his hair over it and a horn each side,
  no snout or cheek, his arms behind the shell with only his claws at its
  edge). `tools/port/bowser_back.py` holds only those numbers (which
  part moves where, which are copied, mirrored or hidden, and the dome's and
  band's cards). The dome and band drawings are made at the start from the
  player's disc by `port/assets/luigi/art/recipes/bowser_back.recipe` (his
  own shell half joined to its mirror, and a level stretch of his own rim)
  and added after the hammer's in `c_koopa_r_pc-`; his parts keep
  their own values' places, so his disc walk, run, stand, jump and talk play
  on it as they are, and our back-facing hammer, push, lift and throw too.
  `bowser_back.py repack --disc DIR` writes changed numbers into his back
  pack without Blender, its animations kept as they are; `char_sheet.py
  --char bowser_r --pitch 15` draws it as the field camera, looking down,
  sees it.
- Bowser is drawn at 0.7 of his size (about 32 units, half again Mario's
  height), so he reads as Bowser and still fits where Mario goes.
- Every stand-in runs on Mario's clock, so it starts, holds and is scrubbed
  as his pose is (the spin hammer's charge, the scenes' held poses). Ours
  have Mario's length and loop. A disc animation of another length is
  fitted to his (`standInClock`): where Mario's loops, it loops; where his
  ends, it is scaled to end on the same frame (a loop of its own in whole
  turns, ending as it starts) and holds there. So the pick never goes on
  moving after Mario's pose has ended and the scene has moved on. The
  scenes wait on Mario's own pose, which always runs underneath, so a
  stand-in can't hold one up either.
- Text: Mario and the pick trade names both ways in every language ("Peach"
  / "PEACH" / ピーチ, "Bowser" / "BOWSER" / クッパ), not inside other words
  (スピーチ, "Peachy"), with the same rules as Luigi's. So "Princess Peach"
  reads "Princess Mario": he is the one held captive. Every message the game
  looks up goes through it (`msgSearch`): the field, battles, menus, item and
  badge descriptions, Tattles, the Journal, mail, the Trouble Center and the
  credits.
- Paper Cut's own text too: the settings page, help lines, the Start menu's
  pages, notices, achievements, duo moves and the rest, in every language,
  as each is looked up (`portTr` and `portTrGame` in `port/settings/tr.cpp`,
  the same swap). A few keep Mario's name whoever is picked (`keepsMario`
  there): a choice that is a name (the Play as choice itself, versus's sides),
  Play as's own help, the looks and colours named after the brothers
  ("Fire Mario", "Luigi green"), versus (always the brothers: Mario's side
  against Luigi's, and its battles' text too), Brothers mode's texts (it
  turns Play as off while on), and the model files' names. The What's
  new page is the release notes as written. The swap trades names, not
  pronouns: "Mario's attack goes up by 2, but he takes double damage" reads
  "Peach's attack ..., but he ...".
- The pause menu's first tab says "Peach" or "Bowser", lettered from the
  game's own tabs as Luigi's is (`peach_tabs.recipe`, `bowser_tabs.recipe`).
  On the JP disc and in the EU disc's Japanese they say ピーチ and クッパ.
- The save library (All settings, Saves) names who a save was played as.
- Mario plays their part: their NPCs (Peach in her scenes and the ending,
  Bowser in his), the player in their interludes, and Bowser's boss fights
  are drawn as Mario (`a_mario`), each of their animations as Mario's of the
  same kind (stand, idle, walk, run, talk, jump, hurt; the rest standing).
  The credits' parade swaps them too.
- Faces: the HP face on the status bar, the head on the pause menu's Mario
  page, the coin on its level row and level-up's BP panel are Peach's or
  Bowser's, made at the
  start from their own models on the disc as Luigi's are
  (`port/assets/luigi/art/recipes/peach_icons.recipe`, `bowser_icons.recipe`).
  Their head steps were written from the models' own numbers by
  `tools/port/char_heads.py` (each card cut from its texture as the model maps
  it, turned and placed as the idle pose stands it); nothing from the disc is
  in the repository. Where a face can't be made, Mario's shows.
- Sounds: Mario's plain-floor steps and landing are Peach's (her jump stays
  Mario's: she has none) or Bowser's, and his jump Bowser's; Bowser's spin
  jump whooshes and lands with the thud of his own ground pound.
- Voices: every moment the game voices Mario (a scene's surprise or nod, a
  jump, an attack's and a Stylish's shout, getting hurt, falling in battle,
  the victory pose, the inn) plays the pick's voice instead
  (`port/src/common/play_as_voice_pc.c`, one table row per Mario clip).
  Peach and Bowser speak with their own clips from the disc, the closest of
  theirs for each moment (Bowser growls where Mario shouts, Peach gives a
  short bright "hm!"); where nothing of theirs fits (Peach's snore) nothing
  plays, and quick repeats of a jump or attack shout rest a moment so a
  cheer isn't heard twice over. The disc has no voice for Luigi, so his are
  Mario's own clips pitched up about one and a half semitones and a little quieter. While
  Peach or Bowser is the pick, Mario stands in for them in their own
  chapters' interludes, so their clips there play as Mario's. In battles
  with two heroes each speaks as itself: Brothers mode's Luigi and a versus
  battle's Luigi in Luigi's voice, a versus battle's Mario in Mario's.
  Online, each player hears their own pick's voice. With Mario as the pick
  nothing changes.
- The pause menu's first tab reads "Bowser" while he's the pick (US and EU
  discs): Mario's tab with its lettering taken off and his name put on letter
  by letter from the game's own lettering on its other tabs and buttons (the
  w is "Number"'s m turned over), recoloured to the tab's
  (`port/assets/luigi/art/recipes/bowser_tabs.recipe`). On the Japanese disc
  (and the European one in Japanese) it reads クッパ: the lettering has no ク
  or パ, so ク is "スターストーン"'s タ without its inner stroke, and パ is
  "バッジ"'s バ with its two dots made a small ring.

## Their new moves

As Luigi's ([LUIGI_ANIMS.md](LUIGI_ANIMS.md)): at startup the port makes
`a/c_peach_pc`, `a/c_peach_r_pc`, `a/c_koopa_pc` and `a/c_koopa_r_pc` from
the disc's models and a pack of our own animations and a hammer plane for each
(`port/assets/luigi/peach_anims.bin`, `peach_r_anims.bin`,
`bowser_anims.bin`, `bowser_r_anims.bin`; `tools/port/luigi_pack.py`; a pack
can also set values of the model's own tables, which is how Bowser's back
view is placed), the hammer showing Mario's
drawings copied from the player's `a_mario-`. If a disc model isn't the one
a pack was made for, that character keeps the disc's model and moves.

The motions are key poses per part of each model, authored in
`tools/port/peach_motions.py` and `bowser_motions.py` (the engine is
`char_motions.py`) and written into the packs in Blender by
`tools/port/char_anims.py pack --char peach|bowser`; `char_sheet.py --char`
draws them without Blender for review.

| Mario's | Peach | Bowser |
|---|---|---|
| `M_H_1` `M_H_4` `M_H_7` (and R) | the field hammer: back over her shoulder, her free hand out before her, then down two-handed | the field hammer, a stamp and a roar (in play his [fire breath](#fire-breath) is drawn instead) |
| `M_H_2` `M_H_5` `M_H_8` (and R) | hitting a wall | hitting a wall |
| `M_J_1A` `M_J_1B` `M_J_1C` (`1BR` `1CR` from behind) | her own jump: a spring, one hand up before her and the other swept back, the skirt billowing | (his disc jump) |
| `M_A_1A` `M_A_1B` | the spin jump, tucked | the spin jump, into his shell |
| `M_D_1` `M_D_2` `M_U_3` | hurt, knocked flat, up again | (his disc hurt poses) |
| `M_I_1` | holding up an item | holding up an item |
| battle `@b`: `M_C_3`, `M_J_1D`, `M_A_3A` `M_A_3B`, `M_H_1A`..`C` (and 5, 9) | guard, the stomp's bounce, the jump attack, the hammer attack; `M_V_1` a curtsy and a wave | guard, bounce, stomp, the hammer attack (victory: his disc laugh) |
| the spin hammer `M_H_3` `3A` `3B` (and 6, 9) | wound up with the hammer low behind her, held up behind her head, whirled out before her | the same, his bulk behind it |
| `M_I_Y` `M_V_2`, hurt and worried `M_D_6`, low HP `M_S_3`, peril `M_I_S` | a start, a fist pump, hands to her cheeks, a tired sway, hunched and gasping | the same, with his jaw and glare |
| riding Yoshi `M_Y_1`..`3` | side-saddle, her skirt gathered over his back | sat up astride him, his legs down his sides, his tail off the ground |
| the spring `M_P_1` `M_P_2`, lifting `M_U_2` (and R), `M_W_7`, arms up `M_S_4` `M_W_6` `M_J_5A` `M_J_5B` | a bend and a leap; her arms open in a V over her head to hold Bobbery where Mario does | the same, in his own reach (Bobbery is drawn higher over him while he holds him, his place in the game unchanged); from behind his shoulders rise too, so his claws show over his hair |
| pushing `M_O_1` `M_O_2` (and R), a throw `M_C_1` (and R), the Appeal `M_C_2`, Flurrie's ride `M_A_2A`..`C`, gestures `M_B_1`..`3` `M_X_1` `M_I_U` `M_Z_9` `M_N_10` | her own, the skirt and hair swinging after her | his own, stamping; R names front-on |
| the story's poses (`e_mario`: nods, talking, listening, thinking, marching, stamping, surprise, sitting, dazed, asleep, eating, hopping for joy, ...) | to Mario's frames: her head turns, her eyes and mouth (her own drawings) change where his face does | the same with his head, jaw and eyes; lying dazed, asleep, keeling over and getting up are his disc poses |
| the rest of battle: `M_N_8` `M_A_4`..`6` `M_Z_10` `M_Z_11` `M_C_4` `M_C_2A`..`C` `M_P_1` `M_P_2`, the hammer moves `M_H_2`..`4` `7` `8` `10` `11` `13`..`19` | a cheer, a tucked somersault, knocked flying, stunned, laid out, an item held out, the Appeal; every hammer move to Mario's hammer angles | the same (stunned and laid out: his disc poses) |

Peach's arms: her near arm (the one nearer the camera) is drawn over her
face wherever it points between about -15 and 70 degrees, so her raised
hands are the far arm's (her head hides it as it passes) or go up behind
her head, and in a hammer swing the arm stays up behind her head while the
wrist turns the hammer over the top, then goes straight to below her chin.
That jump is one frame, and the frame before it holds until it (a step key,
`face_safe` in `peach_motions.py`), so with frame interpolation on her arm
isn't drawn across her face on the frames in between either. Her throw
(`M_C_1`) starts from the lift's V, as Mario's starts overhead, so Bobbery
leaves her hands where he was held.
Mario's battle hammer angles are turned on a little where the hammer is
down before her, so from her higher hand it reaches the ground where his
does.

Peach swings a parasol where Mario swings his hammer: closed in every swing
(the field hammer, wall hits, the spin hammer and every battle hammer move),
on her hammer plane, so where it hits is the game's as for Mario. Otherwise
it is put away, as Mario's hammer is, except when she floats. It is her own
parasol's shape: a dome of alternating panels with a scalloped trim round the
rim, a small knob on top and a pale handle with a crook at its foot; open, it
is about as wide as her skirt, and she holds the handle just above the crook.
Each hammer has its own colours: the wooden Hammer's pink and cream (Keelhaul
Key's beach parasol), the Super Hammer's green and white (Rogueport's), the
Ultra Hammer's the Ultra Hammer's own red and gold. They are drawn at the start
from Keelhaul Key's beach parasol on the player's disc
(`port/assets/luigi/art/recipes/peach_parasol.recipe`, the recipes' `mesh`
step: the pole straightened, the canopy folded for the closed ones and taken
taller for the open one's dome, flat panels, the trim, knob and crook in the
canopy's own cream, and the hammer drawings' own outline), and put in her
models' textures in the hammer drawings' places, on a plane 1.55 times the
hammer's (`luigi_pack.PARASOL_CARD`)
(`port/src/common/luigi_model_pc.c`; the float is
`port/src/common/peach_parasol_pc.c`). If they can't be made, she swings
Mario's hammer drawings as before (and floats with nothing over her head).

**The parasol float.** Holding A as she comes down from a jump, Peach opens
her parasol over her head and sinks slowly under it, both hands up on the
handle (her own pose, `M_J_1P` and from behind `M_J_1PR` in her packs, played
in place of Mario's fall while she floats: the handle's end in her near glove
above her crown, her far hand beside it, the shaft straight up to the canopy) (1 unit a frame; Mario falls up to
18), drifting no faster than three quarters of her walk, for at most 1.75
seconds after the jump's peak. Letting go of A, or the time running out,
closes it, and she falls as Mario does from there. It never lifts her, and it
only follows her own jumps: not a fall off a ledge, a spring, a hit, paper or
8-bit forms, carrying Bobbery or the tube. A jump in place with the Ultra
Boots (no stick before the peak) is the Spring Jump's, which takes A held the
same way, so it doesn't float. On any other jump, the stick turned a third
of a round one way with A held and the Ultra Boots (the Spring Jump's spin
begun in the air) closes it for the rest of that jump, so she lands when
Mario would and the spin carries on into the Spring Jump as his does. The
Spin Jump is untouched: its second press of A starts its own motion. It is part of picking Peach, with no setting
of its own; it reads only the game's state and the buttons, so it plays the
same online and in replays, and with anyone else picked nothing changes.
At the same height it reaches a little farther than Mario's jump (84 units
against 79: her drift is slower than her run, but she stays up longer), and
farther across gaps that drop away to a lower floor (165 against 100 over a
drop of 100); the game's own gaps that need the Spring Jump or a partner stay
that way. `tools/port/luigi_jump_sweep.py --parasol` checks every map's floors
for what that reaches and Mario's jump doesn't; from the few take-offs where
it matters (two pits with no way out, in the Pirate's Grotto and at the
Palace of Shadow's entrance, the X-Naut Fortress's Card Key floor, ways round
in the Palace, the Great Tree's entrance, a ledge in western Rogueport and a
deck at Keelhaul Key) she doesn't open it (`kNoFloat` in `peach_parasol_pc.c`, which
the sweep reads, so a re-run proves them closed). What's left is the tops of a
few blocks. The packs give her models fifteen texture
slots for it (`luigi_pack.PARASOL_TEXTURES`; `luigi_pack.py repack --char
peach` rewrites a pack's hammer without Blender).

`char_sheet.py --motions peach_motions` (or `bowser_motions`) draws the
motions straight from the script, without Blender and before a pack is
written (keys joined in straight lines, so a little stiffer than the game's
curves): the quick way to pose them.

## Fire breath

While Bowser is the pick (not in versus), he breathes fire where Mario
swings the hammer (`port/src/common/fire_breath_pc.c`).

- **The field.** His hammer swing (B) is a breath of fire: his disc's
  `KPA_A_1A`..`D` (inhale, lunge, breathe, close; fitted to the swing's 20
  frames), always in profile with his snout to the side he faces (also when
  the swing starts facing away), with the game's own flame at his mouth
  (`gonbaba_breath` type 7,
  the one his own stages give him), his roar and the flame's sound. Only the
  drawing and the sounds change: the swing's motion runs as Mario's, so its
  reach, timing and every check on it are the hammer's (enemies struck for a
  first strike, yellow blocks and the rest it breaks, switches, trees and
  bushes, wall knocks, the remake's backswing, every script that asks for the
  hammer). No puzzle changes. The spin hammer (Super and Ultra) stays his
  spin. The flame grows with the hammer, as his own stages step his breath
  up: his orange flame with the wooden hammer, the dragons' blue flame
  (`gonbaba_breath` type 3, Bonetail's) with the Super Hammer, and their
  white-hot Mega Breath (type 2, Gloomtail's), half again as big, with the
  Ultra Hammer. In battle his Fire Breath takes the same colours.
- **The first strike** from that breath is his fire breath on the enemy struck:
  the hammer first strike's power, fire, and it burns (Burn, 2 turns). The
  game has no first strike of its own that burns; Burn is its status. Fire's
  rules apply: an enemy immune to fire takes nothing.
- **In battle**, *Fire Breath* in his Hammer list, after the hammers: 4 FP,
  every ground enemy (as Fire Drive aims), fire. The hammer's Action Command
  (hold the stick left while he draws breath, let go as the last light comes
  on; the game's own help line), and the hammer's power by its rank (1, 2, 3
  for each enemy, one more after a good command); a 30 % chance to burn for
  2 turns. The game's element rules decide the rest: no damage to enemies
  immune to fire, more to those weak to it. His disc's breath and the game's
  flame and sounds (Bowser's own fight's), then a Stylish.
- **His battle hammer swings** are drawn as a breath of fire too: the plain
  Hammer, Power and Mega Smash, Ice Smash, Piercing Blow, Head Rattle and the
  Super and Ultra Hammer's whirl. The wind-up is his inhale, the swing his
  lunge and a short jet of flame at the one struck (the hammer's colours by
  rank), with his roar and the flame's sound. He is drawn standing back at
  his Fire Breath's distance from the one struck, not the hammer's snout-in
  reach (his unit stays at the hammer's spot). Only the look and sound change:
  each move's script, damage, rules, Action Command and Stylish are the
  hammer's.
- **Hammer Throw** is a fireball he spits: his inhale as Mario winds up and
  holds the command, his lunge and breath where Mario lets go, with his
  roar, and a ball of his flame (the hammer's colours by rank, its stream
  trailing back the way it flies) that flies the thrown hammer's own arc and
  timing to the one chosen and burns out where the hammer would land. The
  hammer is not drawn.
- **Quake Hammer, Mega Quake and Fire Drive** are a stomp: he leaps as Mario
  winds up (his disc's jump, `KPA_J_1A`), stays up there while the command
  runs (`KPA_J_1B`, a slow bob), and drops onto the ground in his hip drop
  (`KPA_A_2D`, his own stages' ground pound) as the hammer would strike it,
  with his jump and landing sounds. The leap is drawn only (his unit, and his
  shadow's place, stay on the ground), so the quake's shake, effects, hits
  and timing, and Fire Drive's fireball and fire wave, are the badges' own.
- Only the look and sound change in all of these: each move's script, damage,
  targets, rules, Action Command, Stylish and timing are the badge's.
- Text: the move's name as each language's text names his fire (Bowser's
  Tattle: "fire breath", Feuer speien, souffle de feu, soffio infuocato; in
  Spanish *Aliento de fuego*, in Japanese ファイアブレス), its help in all six
  (`port/assets/lang/*/fire-breath.txt`).
- Online and in replays it plays the same everywhere: it is part of the pick
  (the host's is everyone's, and a shared battle's is the inviter's), and it
  reads only the game's state and input; the poses and sounds are drawn and
  played only.

## The Wardrobe

The Start menu's Wardrobe ([COSTUMES.md](COSTUMES.md)) dresses whoever is
picked: its first rows are under their name, with their figure on the page,
and each pick's colours are their own (saved apart, so switching picks keeps
each one's look):

| Pick | Rows |
|---|---|
| Mario | Looks (Luigi, Wario, Waluigi, Fire Mario), Cap and shirt, Overalls, Costume |
| Luigi | Looks (Mario, Wario, Waluigi, Fire Luigi), Cap and shirt, Overalls, Costume |
| Peach | Dress, Crown, Hair, Gloves, Earrings, Shoes, Costume |
| Bowser | Shell, Hair, Skin, Horns and spikes, Bracelets, Costume |

Each row steps through the game's own colour, the 13 named ones and Custom
(the colour picker), as Mario's do. The colours are their models' own
textures recoloured as they're drawn (`port/settings/features/cosmetics.cpp`:
each part a colour range in the textures it's drawn from, the disc's model
only while it's the pick, so a Peach or Bowser NPC keeps theirs), so they
show in the field, in battle, in the menus' figures and on the status bar's
face and the menus' head and emblem (by colour: their crown and hair are one
gold there, so the faces take the hair's). The costume is fitted to each
one's head and body (`port/src/common/costume_pc.c`, its `kFits` table for
the nudges). The settings keys are `cosmetic_luigi_*`, `cosmetic_peach_*`
and `cosmetic_bowser_*` (each player's own online, as Mario's colours are).

## The parity check

`tools/port/anim_parity.py check` (part of CI: `local_ci.py`'s audits
stage) lists every animation of Mario's: 196 of them, 111 in the field
(`a_mario`; from behind, `a_mario_r`; the story's poses, `e_mario`) and 85 in
battle (`b_mario`). It fails if Luigi, Peach or Bowser has none of their own
for one, by the same lookup the game makes (`charAnim`). It also fails if a
map names an animation their model doesn't have, if one of ours doesn't loop
or end as Mario's does, or if a map gives a one-shot where Mario's loops.
The one deliberate exception is the thrown hammer, battle `M_H_12`: Mario's
model draws only the flying hammer there, the same for everyone. It is
listed in `DELIBERATE` with its reason.

Mario's set is kept beside the tool as names, loops and lengths only
(`anim_parity_set.py`). `anim_parity.py disc ISO...` checks it against the
discs (`--write` refreshes it). `anim_parity.py report` prints the whole
table: each animation of Mario's and each pick's counterpart. When you add
or rename an animation, run the check: a new name of Mario's needs a
motion in the pick's `*_motions.py` (then the pack) or a map entry in
`luigi_pc.c`.

The moves themselves have their own check: the test pass's `playas_moves`
group (`tests/port/testpass/playas_moves_*.json`, one per pick, on every
disc) plays the same input as Mario, Luigi, Peach and Bowser and fails if
any of Mario's field moves doesn't happen for one of them: the Spin Jump,
the Spring Jump and its launch, the Ultra Hammer's spin, the hammer (Bowser's
fire breath), Yoshi's ride and getting off, Paper and Tube Mode. The jumps
are played twice: once in tight script steps and once as a person plays them
(A held from take-off, the stick turned round an uneven analog circle at a
thumb's pace, a jump on the move with the spin begun in the air, the Spin
Jump's A let go and pressed again just after the top). A change
made for one pick only (Luigi's higher jump, Peach's parasol float, Bowser's
fire breath) must keep these passing.

## Limitations

- Every animation of Mario's has one of theirs. If a disc's model isn't the
  one the packs were made for, they play the nearest of their interlude set
  instead.
- The pause menu's first tab reads "Peach" or "Bowser" on the US and EU
  discs, in the game's own lettering from its other tabs, as Luigi's. On the
  JP disc and in the EU disc's Japanese, Peach's reads ピーチ
  (`peach_tabs.recipe`, from the tabs and the file select's buttons; the game
  has no チ, so hers is joined from the strokes of テ and ー); Bowser's reads
  クッパ (`bowser_tabs.recipe`: ク is a タ without its inner stroke, パ a バ
  with its two dots made a ゜).
- Collision and speed are Mario's, and so is the jump, but for Luigi's: a
  little higher, his setting Luigi's higher jump (PLAYABLE_LUIGI.md, His
  jump).
