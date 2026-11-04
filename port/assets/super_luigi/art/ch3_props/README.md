# Chapter 3's small props (Super Luigi)

From `docs/super_luigi/ch3.md`, New assets 5 and 6 (both optional): the
King's crown, and Lakky's microphone and start lights. The kart is in
[`../kart/`](../kart/), the scoreboard in [`../scoreboard/`](../scoreboard/)
and the posters and sign in [`../ch3_posters/`](../ch3_posters/).

- **Scale:** 8 texels per unit, outlined, lit from the top left.
- **Sources:** made by [`../tools/ch3_props.py`](../tools/ch3_props.py),
  which also writes `parts.json` and `preview.png`.

## Cards

Each pivot is in pixels from the PNG's top-left.

| Card | Size | Pivot | What it is |
|---|---|---|---|
| `crown` | 64x48 | (32, 40) | The King's crown: five gold points with pearl tips, a ruby and two sapphires, and a checkered band round the base (it's a racing island). The pivot is the middle of its base. On Revvo's head (`togenoko_ace`) at 1:1; on Luigi's cap for the crowning, the same card, at about 0.8x so it sits on the cap's crown, tilted a little back. |
| `microphone` | 32x64 | (16, 36.8) | Lakky's microphone: a round metal grille on a black handle, with the cord's start. The pivot is at the grip, for the Lakitu's hand. |
| `lights_0` | 80x40 | (40, 16.8) | The start lights, all off. The pivot is between the two hooks, where the fishing rod's line ties on. |
| `lights_1`, `lights_2`, `lights_3` | 80x40 | (40, 16.8) | One, two, three red |
| `lights_go` | 80x40 | (40, 16.8) | All green: go |
| `trophy` | 80x128 | (40, 120) | The Grand Prix's trophy: a gold cup with a star, on a black plinth with a checkered band; its lid flat at y 13.6. The pivot is on the ground at its middle. `c_sl_trophy` (1.6x on the awards stage) |
| `trophy_piece` | 32x32 | (16, 24) | The compass piece standing on the trophy's lid (cranberry's drawing, smaller): a card of its own so it can come off (`c_sl_trophy`'s P_1, then S_2 bare) |
| `wheel` | 56x56 | (28.2, 27.7) | Big Green 01's stolen steering wheel, cut from the style model's design (`tools/ch3_wheel.py`): black rim, steel spokes, a green boss. `c_sl_wheel`, in the King's Suite with Revvo's crown hung on it |
| `tarp` | 304x96 | (152, 112) | The green tarp over Big Green 01 in Torque's garage, its hem high enough for the wheels to show. `c_sl_tarp` (K_1 whisks it off at the build) |
| `shutter` | 288x256 | (144, 8) | The Parts Depot's roll-up shutter, jammed crooked, a yellow and black bar along its foot. The pivot is the middle of its top edge, so it rolls up toward it (`c_sl_shutter`'s O_1) |

Swap the lights cards in place for the countdown.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: multiples of 8 | **pass** | 32 to 304 px |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels |
| R4: outline 4–5 px, 3–9 | **pass** | Crown median 6 (L5 T6, R6 B9), microphone 5 (L4 T3, R6 B8), trophy 4 (L4 T4, R7 B7), tarp 6 (L7 T4, R6 B6), shutter 4 (L4 T4, R6 B8); the wheel measures 9 because its black rim is counted as line, as the light bar's body is. The light bar measures 8 to 9, because its near-black body is counted as line; the line itself is 4 to 5. |
| R4: line colour, lum ≤ 60 | **pass** | Gold `#4a3006` (lum p90 52), metal `#262834` (42), black `#0e0e14` (16), lamps in a dark tint of each colour |
| R5: fills | **pass** | Gold `#ecba3c` s .75 v .93. The bar `#3a3a44` v .27 is a dark material (a black-painted box), like a marking. Lit lamps s .79 v .95. |
| R6b: sheen | **pass** | One sheen on the crown, the grille and each lit lamp, white 0.30 to 0.35. The finder measures the bar's lamps together (20 to 23%). The crown's sheen sits on the band and points (0.5% by the finder, which counts the checkers as the base). |
| R6b: specular dot | **pass** | One white dot on the ruby (a glossy gem) |
