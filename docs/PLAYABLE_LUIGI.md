# Playable Luigi (Paper Cut 0.2, M6)

A Gameplay+ option, "Play as Luigi" (setting key `playable_luigi`, off by
default), that shows Luigi in Mario's place. It changes how Mario looks, not
what he can do: Luigi uses Mario's moves, stats, collision and sounds. Saves are
not changed.

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

## Limitations

- Luigi only has stand, idle, walk, run, jump and talk. Everything else Mario
  does shows Mario's model (in green in the field); in battle that is every attack, Appeal,
  guarding, being hurt and the victory pose, so in battle Luigi is mostly seen
  standing, walking and running between moves.
- Known issue: in battle Luigi draws grey (darker than in the field). His
  model's vertex colours are white like Mario's, so it is in how battle draws
  him: the light or material state copied from Mario's battle part, or the
  battle pool (type 1) he is loaded into. Not fixed yet.
- In battle the fallback is red Mario: the battle model's colour comes from
  `_mario_super_emblem_anim_set` (unit_mario.c), which only looks at the
  badges.
- No paper forms on Luigi's model (plane, boat, tube, sideways, Vivian's
  shadow): they are Mario's.
- Luigi's run and walk have his own timing; he moves at Mario's speed, with
  Mario's collision and footstep sounds, and jumps Mario's jump.
- Text still says Mario, and the menus (the Mario page, badges, the journal,
  the credits) still draw Mario.
- Luigi in Rogueport's inn and in the Super Luigi scenes is still there: two
  Luigis.
- The fallback colours follow the option when Mario's outfit is checked (each
  field frame the partner can be used, as for a badge change); the Luigi model
  itself switches at once.
- Peach, Bowser and 8-bit Mario are unchanged.

## What a full Luigi would need

- New animations for Luigi's model: hammer, spin jump, damage, item, Yoshi
  ride and the battle moves (`b_mario`'s 85). The disc has none, so this is new
  art (a model tool that writes the game's animation format), or a retarget of
  Mario's animations onto Luigi's skeleton, which the two models' different
  node layouts make far from automatic.
- Paper forms drawn from Luigi's model (the paper capture is of the pose, so
  this may only need the stand-in pose attached to Mario's paper pose).
- Luigi's sounds (`SFX_FLD_NPC_LUIGI_*` exist for his NPC: steps, jump,
  landing) and his taller collision box (22x47 as an NPC).
- Text: a "Luigi" for Mario's name in messages, and the menus' models.
