# The ice wall (Super Luigi, chapter 2)

A block of packed snow that bars the lane to the tailor and the hollow tree,
until Jerry's Blast bursts it (`docs/super_luigi/ch2.md`, New assets 6: "a
packed-snow block, two sizes").

- **Scale:** 8 texels per unit, like the characters: it stands next to
  Luigi, so it's drawn as an outlined paper prop, lit from the top left.
- **Sources:** made by [`../tools/ice_wall.py`](../tools/ice_wall.py), which
  also writes `parts.json` and `preview.png`.

## Look

Pale cool snow (`#dbe6ee`) cut into staggered packed blocks with blue-grey
joints, a soft lit patch on each block, a few ice crystals, a pillowy snow cap
with icicles along its front, and a blue sheet of ice in the lower right
corner where it's frozen through. The line is a dark winter navy
(`#1c2c48`).

## Cards

Each pivot is in pixels from the PNG's top-left.

| Card | Size | Pivot | What it is |
|---|---|---|---|
| `ice_wall_small` | 176x208 | (88, 200) | 20 units wide and 22 tall (plus the cap): the hollow tree's ice, or a narrow lane. The pivot is the middle of its foot, on the ground. |
| `ice_wall_large` | 336x272 | (168, 264) | 40 by 30 units: across the lane to the tailor |
| `chunk_a`, `chunk_b`, `chunk_c` | 48x48 | (24, 24) | Chunks of packed snow for the burst. Hide the wall, then throw these out spinning (with the game's explosion and smoke). |

A wall's card stands upright across the path; for a thick block, the models
agent can put the same card on the front and back of a box with a flat
snow-tone top.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: multiples of 8 | **pass** | 48 to 336 px |
| R2: 8 texels/unit | **pass** | |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels |
| R4: outline 4–5 px, 3–9 | **pass** | Walls median 5 (p10–p90 4–8/9); chunks 5–6 |
| R4: heavier bottom/right | **pass** | Walls L4 T4, R6 B6 |
| R4: line colour, lum ≤ 60 | **pass** | `#1c2c48` (lum p90 43), a dark tint of the snow's blue; the ice sheet's line is `#142a4e` |
| R5: neutral material | **pass** | Snow `#dbe6ee` s .08 v .93, cool. Ice `#9cc8e6` s .32 v .90. |
| R6: soft top-left shading | **pass** | Light and shadow bands 7 to 9 px, 3 px blur |
| R6b: one sheen per part | **pass** | Walls 17.6–18.3% at (0.41, 0.26–0.27). On a near-white base it uses 0.6 white (measured 0.63–0.64), as STYLE.md asks. Chunks 7.9–11.1%. |

## Rough edges

- The icicles are cut into the cap's outline, so their line is the cap's
  dark navy; they read as icicles at game distance but are heavier than a
  glassy icicle would be.
