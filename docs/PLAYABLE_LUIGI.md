# Playable Luigi (Paper Cut 0.2, M6)

A Gameplay+ option, "Play as Luigi" (setting key `playable_luigi`, off by
default), that shows Luigi in Mario's place. It changes how Mario looks, sounds
and is called, not what he can do: Luigi uses Mario's moves, stats and
collision. Saves are not changed.

## What the disc has

Every release (G8MJ01, G8ME01, G8MP01) has the same Luigi files. The only Luigi
model is the field NPC `a/c_luigi` (used by Luigi in Rogueport's inn, the
Super Luigi books' scenes and the credits). There is no battle model, no paper
form, no back-facing set of its own and no event set.

`c_luigi`'s 36 animations (the R ones are the back-facing versions, in the same
file):

| Luigi | What it is |
|---|---|
| `Z_1` / `Z_1R` | idle (blinking) |
| `S_1` / `S_1R` | standing |
| `W_1` / `W_1R` | walking |
| `R_1` / `R_1R` | running |
| `T_1` / `T_1R` | talking |
| `J_1A` / `J_1AR`, `J_1B` / `J_1BR` | jump: going up, coming down |
| `Z_2`, `S_2`, `T_2` (and R) | a second idle, stand and talk (his story-telling scenes) |
| `L_1` .. `L_11` (`L_1R` .. `L_3R`) | his scenes' gestures |
| `I_1` / `I_1R` | one more scene pose |

Mario, for comparison:

| Set | File | Animations | Used for |
|---|---|---|---|
| field front | `a_mario` (and `_l`, `_w`, `_wl` badge colours) | 59 | stand, walk, run, jump, hammer (`M_H_*`), spin jump, damage (`M_D_*`), talk (`M_I_2`, PAL `M_I_3`), item pose, Yoshi, ... |
| field back | `a_mario_r` (and colours) | 17 | the back-facing versions |
| field events | `e_mario` (and colours) | 37 | scene-only poses (`M_N_*`, `M_L_*`, `M_F_*`, ...) |
| battle | `b_mario` (and `_l`, `_w`, `_wl`) | 85 | every attack, Appeal, guard, hurt, peril, victory |
| paper forms | `p_plane`, `p_hune`, `p_roll`, ... | | plane, boat, tube, sideways, ... (drawn on Mario's model) |

So Luigi covers stand, idle, walk, run, jump and talk, front and back, and
nothing else: no hammer, spin jump, damage, item, Yoshi or scene poses, no paper
forms and no battle moves. The Luigi model's size is close to Mario's (his
bounding box is about 24 units tall, Mario's 22), in the same units, so it can
be drawn with Mario's matrix as it is.

## Where the game picks Mario's look

- Field: `src/mario/mario.c`. `marioSetCharMode(0)` loads the pose sets of
  `marioAnimeGroupData[colorId]` into `paperAnimGroupId[0..2]` (front, back,
  events; `colorId` from the L / W Emblems). Every pose change goes through
  `marioChgPose(name)`; `marioPreDisp` sets `animName` on the set
  `marioAnimeId()` picks and steps it, `marioDisp` and `marioDispBlur` draw it.
  Peach, Bowser and 8-bit Mario swap the sets in the same function.
  About 70 other places (motions, events, effects) read or change
  `paperAnimGroupId[]`'s poses (material flags, colour, frame, paper forms).
- Battle: `src/unit/unit_mario.c`. `unitpartsdata_Mario` names the model
  `b_mario` for Mario's three parts; `_mario_super_emblem_anim_set` swaps them
  for the badge colours. `src/battle/battle_disp.c` picks each part's pose from
  the unit's pose table or the move scripts (`btlevtcmd_AnimeChangePose`), steps
  it (`animPoseMain`) and draws it (`animPoseDrawMtx`, and again for the blur).
- Party code (`mario_party.c`, the partner units) only follows Mario's position;
  nothing there picks his look.
- Menus (`win_mario.c`, `win_badge.c`, `win_log.c`) and the credits draw their
  own `a_mario` poses.

## Design

The smallest change that is safe with the rest of the game: Mario's poses stay
loaded and keep running exactly as before, so every one of the places that read
or change them keeps working, and a second pose, Luigi's, is drawn instead of
Mario's whenever it has a version of what Mario is doing.

`port/src/common/luigi_pc.c` (`port/include/port/luigi.h`) keeps a "stand-in"
pose for a Mario pose:

- `portLuigiUpdate(poseId, battle)`, once a frame after the game stepped
  Mario's pose: if the option is on and Mario's current animation has a Luigi
  version, it loads `c_luigi` (once, field or battle pool), starts the matching
  animation when Mario's changes, copies Mario's pose state (material flags and
  colours, so fades, flashes and Charge shine show; the paper turn; the play
  rate) and steps it.
- `portLuigiDrawId(poseId)`, at the draw calls: Luigi's pose while he stands
  in, otherwise Mario's.

The map, Mario's animation to Luigi's: `M_S_1` stand to `S_1`, `M_Z_1` idle to
`Z_1`, `M_W_1` walk to `W_1`, `M_R_1` run to `R_1`, `M_J_1A` / `M_J_1B` /
`M_J_1C` jump to `J_1A` / `J_1B` / `J_1B`, the talk pose `M_I_2` / `M_I_3`
to `T_1`, and each one's back-facing `R` version to Luigi's `R` version.

Anything else (the hammer, a spin jump, being hurt, holding up an item, the
scene poses, any paper form, which is drawn on Mario's model) falls back to
Mario's model for as long as it lasts. While the option is on, that model is
the L Emblem's green and navy (the W Emblem still adds its colours), so the
fallback reads as "Luigi in Mario's clothes" and not as a different person.

Hooks in the game code, each a small `TARGET_PC` block:

- `src/mario/mario.c`: `marioPreDisp` (update), `marioDisp` and
  `marioDispBlurSub` (draw), `marioBadgeColor` (the green fallback).
- `src/battle/battle_disp.c`: the part loop (update, for Mario's unit's main
  part only) and the part's draw and blur draw.

The stand-in's state is in the save-state section (like the other decoders'
state in CMakeLists.txt), and it checks each frame that its pose is still the
`c_luigi` it loaded (a battle's end frees battle poses; a map change, a save
state or a reset can free or replace the field one), loading it again if not.

## Text, menus, the other Luigi, sounds

All of it only while the option is on; `port/src/common/luigi_pc.c` again.

- Text (`portLuigiText`, from `msgSearch` in `src/driver/msgdrv.c`, a mod's
  message too): every message the game looks up, with the player's name as
  Luigi's: `Mario` and `MARIO` in the Latin languages (English, French,
  German, Spanish, Italian), `マリオ` (Shift-JIS) in Japanese. Not changed:
  the name inside another word (French "Mariole", Spanish "Marioink"), "Paper
  Mario" (the game's title, also the Glitzville play and the Weekly site),
  "Mario Bros." / "Super Mario Bros." and the series ("MARIO SERIES THEME
  MUSIC" in the credits), and in Japanese ペーパーマリオ, マリオブラザーズ,
  マリオストーリー and マリオシリーズ. German's genitive "Marios" becomes
  "Luigis", a shout's drawn-out letters follow ("MARIOOO!" "LUIGIII!"), and
  "Mario and Luigi" (et, e, y, und, &, と, や) becomes "Luigi and Mario", not
  "Luigi and Luigi" (and the other way round). Only lowercase tags (`<...>`,
  message names) are never touched. The texts are made once per message and
  kept on the save-state heap (the game keeps pointers to them). Checked over
  every message file of the three discs with a host-side harness: of about
  1,100 messages per language with the name, the only ones left as they were
  are the ones above.
- Menus: the pause menu's Mario page and the badge page's figure
  (`src/window/win_mario.c`) and the journal map's walking figure
  (`src/window/win_log.c`) go through the same stand-in as the field, in two
  more slots (`PORT_LUIGI_MENU`, `PORT_LUIGI_LOG`): the figure's pose steps as
  before, Luigi's follows it and is drawn instead, with the same name map. Only
  for Mario's own models (`a_mario*`), so 8-bit Mario's `d_mario` stays.
  `portLuigiRelease` (the menus' `animPoseRelease`, also in
  `src/window/win_badge.c`) frees a stand-in with its figure.
- Credits (`rels/end/src/end_00.c`): the parade's silhouettes swap the two
  brothers: Luigi's model and walk where Mario walks with Peach, Mario's where
  Luigi walks with his party.
- The other Luigi: an NPC drawn with `c_luigi` (`NPC_FLAG_LUIGI`, set by
  `npcEntry`: Rogueport's inn and plaza, the harbour, Mario's house in the
  prologue and epilogue) is not drawn (nor his shadow) and is skipped by the
  talk check, the side push, the field attacks' and the blow's hit checks
  (`src/driver/npcdrv.c`). He is not deleted and his flags are not changed,
  so the scenes' scripts that move him, turn him or make him speak run as
  before (his speech balloons point at where he stands).
- Sounds (`portLuigiSfx`, from `__psndSFXOn` in `src/pmario_sound.c`): Mario's
  plain-floor steps (`SFX_MARIO_MOVE1L`/`R`), jump (`SFX_MARIO_JUMP1`) and
  landing (`SFX_MARIO_LANDING1`) play as Luigi's NPC's
  (`SFX_FLD_NPC_LUIGI_*`): the same samples, a little lower and quieter, as the
  game plays them for him. The other floors' steps and landings (grass, metal,
  water, ...) and Mario's voice have no Luigi version and stay.
- Collision: kept as Mario's. Luigi's NPC box is 22x47, but Mario's size is
  what every map's crawlspaces, pipes, low ceilings and the paper and tube
  forms are built around, so changing it could leave a map unbeatable; the
  model only looks taller (his bounding box is 24 units to Mario's 22).

## Limitations

- Every Mario animation has a Luigi version (docs/LUIGI_ANIMS.md) but
  `M_H_12`, the thrown hammer, which looks the same on Mario's model.
- Known issue (not re-checked with this work): in battle Luigi draws grey
  (darker than in the field). His model's vertex colours are white like
  Mario's, so it is in how battle draws him: the light or material state
  copied from Mario's battle part, or the battle pool (type 1) he is loaded
  into.
- In battle the fallback is red Mario: the battle model's colour comes from
  `_mario_super_emblem_anim_set` (unit_mario.c), which only looks at the
  badges.
- Luigi moves at Mario's speed, with Mario's collision (above), and jumps
  Mario's jump.
- Text: a substitution, not a rewrite. Lines where Mario's name stands for the
  person still read naturally, but some don't fit Luigi: other characters
  still speak of Luigi as someone else ("your brother Luigi"), Luigi's own
  lines (his inn talks, the Super Luigi books) call his brother Luigi, and a
  few scenes describe Mario's look (the small one, the red cap). The save
  file's name (the player's own, "Mario" by default) is not changed.
- The other Luigi is hidden everywhere his NPC is, so his inn talks (his
  stories and the Super Luigi books' sales pitch) can't be started while the
  option is on; his scenes still play around the empty spot he stands in.
- The file select draws no Mario model (only textures from `name.tpl`), so
  there is nothing there to change.
- The fallback colours follow the option when Mario's outfit is checked (each
  field frame the partner can be used, as for a badge change); the Luigi model
  itself switches at once. The hidden Luigi NPC, the text and the sounds
  switch at once too; a message already open keeps its text.
- Peach, Bowser and 8-bit Mario are unchanged.
- The menu tabs' titles ("Mario" on the pause menu's first tab) are textures,
  not text, and still say Mario.
- Seen in game (US): the Mario page draws Luigi with "Luigi" by the level and
  "Luigi's Abilities" in its help, and the Gear page's help says "Luigi's
  items". Not yet seen in game: the hidden Luigi NPC (the scripted runs never
  reached a spot where he stands), the badge page's and the journal map's
  figures, the credits, the sounds, and the Japanese and European text in
  game (the text was checked with the harness only).
