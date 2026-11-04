# The Gallery of Murals (Super Luigi, chapter 5)

Six murals along the gallery wall in hei_52 (built on hei_09), telling the
Luffs' story as Screamy reads it (`docs/super_luigi/ch5.md`, beat 8 and New
assets 7: "texture swaps over hei_09's walls, six panels, flat paper-cut
style, or six signboard props").

- **Sources:** made by [`../tools/murals.py`](../tools/murals.py), which also
  writes `preview.png`.
- **Size and palette:** hei_09's wall texture (decoded in scratch, not in the
  repo) is a tiling 256x128 grey stone brick. Swapping it would put the same
  mural on every wall, so these are **256x128 panels** in that texture's size
  and greys, one prop card for each of the back wall's six bays (see
  "Placing them" below), at the wall's own texel density.
- **Style:** STYLE.md R10 (map textures): opaque, flat tones with hard edges,
  no outlines, and a dark drop line under each shape, so the pictures read as
  paper cut-outs pasted on plaster, inside a carved stone frame with Luff
  swirls at the corners. The Luffs look like Cranberry: white robes and
  reddish hair.

## Panels

| File | Size | The wall says (ch5.md) | Picture |
|---|---|---|---|
| `mural_1` | 256x128 | The white city was happy | Domed white houses under a blue sky and a sun; four Luffs dancing and singing on the grass |
| `mural_2` | 256x128 | The wisest Luff made a compass that pointed to tomorrow | A Luff with a staff, the compass, and rays streaming from it toward a sunrise |
| `mural_3` | 256x128 | They knew every storm before it came; they ruled from sea to sea | A crowned Luff raising the compass, a storm held back, the sea at both ends |
| `mural_4` | 256x128 | They looked further, and forgot today | Two Luffs staring through spyglasses at a sky of little suns and moons; a dropped lute; bare, unplanted rows |
| `mural_5` | 256x128 | The compass cracked; everyone went to sleep | The cracked compass over the city gone dark; three Luffs lying asleep |
| `mural_6` | 256x128 | (blank) | Blank plaster, but for a small drawing low in the corner: a boy breaking a circle into seven pieces. A crack runs across it (for Jerry's Blast). |
| `mural_6_broken` | 256x128 | | The same after the Blast: a hole knocked through the left of the panel, cut out of the texture (1-bit alpha), so the door behind shows |

## Placing them in hei_52 (on hei_09's back wall)

Measured on hei_09's map (`m/hei_09/d`, read in scratch; world units are
the map file's ×10, as `mapdrv.c` scales the map):

- **The wall:** the back wall runs x −500..500 at z −100, 200 units tall,
  in the tiling 256x128 brick (`hei_k_kabe`) at **2.33 texels per unit
  across and 2.05 to 2.56 up** (the wall group is scaled 0.8 in y).
- **Six bays:** between its pillars the wall is set back to z −105 in six
  bays, one for each mural, each about 105 units wide and open from y 20 to
  about 135. Their centres are at x **−400, −240, −80, 80, 240 and 400**.

So the murals are **prop cards, one per bay**, not a texture swap (swapping
`hei_k_kabe` would put one picture on every wall face):

| Mural | Bay centre (x) | Card |
|---|---|---|
| `mural_1` | −400 (the first bay from the west door) | 100 by 50 units, centre at y 70, z −103.5 |
| `mural_2` | −240 | the same |
| `mural_3` | −80 | the same |
| `mural_4` | 80 | the same |
| `mural_5` | 240 | the same |
| `mural_6` / `mural_6_broken` | 400 (the last bay, by the east door) | the same; after the Blast swap to `mural_6_broken`, whose hole shows the passage behind |

- **Size:** a 256x128 card at 100 by 50 units is 2.56 texels per unit, the
  wall's own density within a tenth, so the paint and the bricks read at
  the same grain. Each sits in its bay with about 2.5 units of wall either
  side, under the bay's top, at eye height for Luigi.
- **Facing:** +z (toward the camera), flat on the bay, 1.5 units out from
  the recessed face so it never fights it for depth. No collision: the
  wall behind already stops the player; the A check (ch5.md) is a trigger
  in front of each bay.
- **Reading order:** west to east, as Luigi walks through from hei_51 to
  hei_53.

## Checks

| Check | Status | Measured |
|---|---|---|
| Size of the wall texture | **pass** | 256x128, as hei_09's `t` image 0 |
| R10: opaque | **pass** | Alpha 255 throughout (`mural_6_broken`: 0 or 255, for the hole) |
| R10: flat tones, no outlines, drop lines | **pass** | Flat fills, a 1.8 px drop line `#9a9282` under each cut-out; the only strokes are the crack, staffs and the frame's swirls |

## Rough edges

- The pictures are small at 256x128: a Luff is about 26 px tall. They read
  as pictograms, which suits a carved story-wall.
- The one text mark is the sleeping Luffs' "z", in lettering.py's hand-cut
  capitals (a small one).
