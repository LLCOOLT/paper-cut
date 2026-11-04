# Chapter 3's texture swaps: racing posters and the Circuit Break sign

From `docs/super_luigi/ch3.md`, New assets 8 (polish): "tou_02's fighter
posters to racing posters (Revvo on four of them, 'Big Green 01' on one
after the race); yuu_02's 'Pianta Parlor' neon to 'Circuit Break'", shipped
through the texture pack in the built-in mod's `textures/`.

- **Sources:** made by [`../tools/ch3_posters.py`](../tools/ch3_posters.py),
  which also writes `preview.png`.
- **Sizes and layout** were measured on the decoded disc in scratch (nothing
  from the disc is in the repo). Each replacement keeps its texture's size,
  as STYLE.md R10 asks for map textures.

## Files

| File | Size | Replaces | What it is |
|---|---|---|---|
| `posters_revvo` | 128x128 | `m/tou_02/t` images 3 and 18 (the "Glitz Battle" atlases) | Four Revvo posters, 64x64 each: KING REVVO (his crowned, spiked helmet), REVVO #13 (in his red kart), 31 WINS! (the trophy), ALL HAIL! |
| `posters_revvo_after` | 128x128 | The same, after the race | The same atlas, with the fourth poster now BIG GREEN 01: Luigi's kart and "NEW KING!" |
| `posters_racing` | 128x128 | `m/tou_02/t` images 4 and 21 (the other atlases) | RACE DAY! (a checkered flag), CIRCUIT BREAK (two karts), ONE CROWN (the trophy), 8 KARTS / 30,000 FANS! |
| `sign_circuit_break` | 320x192 | `m/yuu_02/t` image 18 ("Pianta Parlor") | CIRCUIT arched over a sunburst in a red arch, BREAK below, with two checkered flags at the arch's feet |

Image numbers are the order in each map's `t` (TPL) file. The texture pack
names each file by its Dolphin hash; that's for whoever wires the swap.

## Look, against the originals

- **Posters:** like the game's, a title strip across the top in yellow
  letters on a dark band, a character painted full bleed on a loud colour
  with a sunburst, and a thin dark border round each poster. They're opaque.
- **Sign:** like the game's Pianta Parlor and casino signs: chunky cut
  capitals, CIRCUIT set on the arch with each letter turned to it, faced red
  at the top to yellow at the foot, with a light grey edge and a dark drop
  below right, over a sunburst, with 1-bit alpha round the cut-out.
- **Lettering:** the hand-cut capitals of
  [`../tools/lettering.py`](../tools/lettering.py) (STYLE.md R12), drawn to
  match the game's own sign letters. No font file is used.

## Checks

| Check | Measured |
|---|---|
| Size matches the original | 128x128, 128x128, 128x128, 320x192 |
| Alpha | Posters fully opaque (as CMPR fills). Sign 1-bit: 2 levels, 0 partial texels. |
| Map-texture rules (R10) | Flat tones and hard edges; the sign's gradient is the original sign's own look. |

## Rough edges

- At 64x64 a poster's title is only 7 to 10 px tall, as on the originals.
  Revvo's helmet face is simplified (a slit with two angry eyes and a beak).
