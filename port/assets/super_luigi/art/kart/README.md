# Big Green 01 (`c_lp_kart`), Luigi's race kart (Super Luigi, chapter 3)

Big Green 01 is an open green-and-white racing kart: a tall front fender, a
rail his legs lie along, a white side pod and rear cowl, a grey bucket seat,
a steering wheel on a long column and an orange silencer. It's the style
model's own kart, cut into cards (ART_PLAN, wave 3 row 1). It is 34 units long and about
17 tall at the steering wheel.

- **Card list:** the models agent's: `docs/super_luigi/ch3.md`, New assets 1
  and 2.
- **Scale:** the cards cut from the design at `tools/cutout.py`'s DENSITY (32 texels per unit; each says its
  `texels_per_unit` in parts.json), the drawn plates, flame, puff and smoke at 8; facing -x (the nose is on the left).
- **Kart origin:** on the ground, midway between the axles.
- **Sources:**
  - The design is the style model's seed 31 (`../designs/kart/`), redrawn by Qwen-Image-Edit as a side view
    (`side_restyled_d40_s2.png`).
  - Qwen's in-place pieces: the shell with everything else taken off (`parts_qwen_shell.png`) and the seat alone
    (`parts_qwen_seat.png`).
  - [`../tools/cut_kart.py`](../tools/cut_kart.py) cuts the cards with `cutout.py` into `qwen/`.
  - [`../tools/kart_cards.py`](../tools/kart_cards.py) makes the cards here from those: the paints, the wrecks, the
    far wheel, `parts.json` and `preview.png`.
  - [`../tools/kart.py`](../tools/kart.py) still draws the plates, the puff, the smoke and the backfire.
- **The recipe** (`../recipes/model_kart.recipe`) makes only the game's effects over these: the backfire (the Lava
  Bubble's flame), the puff and the smoke (effect.tpl's cloud).

## Cards

The pivot is in pixels from the card's top-left corner. "At (model)" is
where that pivot sits, in kart model units.

| Card | Size | Pivot | At (model) | What it is |
|---|---|---|---|---|
| `kart_body` | 168x80 | (40.3, 94.1) | (0, 0) | The white side pod on its green floor and the green tail. It sits **in front of Luigi's hips**; its rim is about 9 up at the seat. |
| `kart_nose` | 120x112 | (60.9, 93.3) | (−11.1, 4.3) | The front fender and the rail back to the pod, in front of his legs |
| `kart_seat` | 88x80 | (44.7, 73.1) | (1.4, 7.2) | The bucket seat, behind Luigi. His hips sit at about (0.5, 9.2). |
| `kart_engine` | 160x72 | (86.7, 34.9) | (5.3, 9.3) | The rear cowl behind the seat, the pipe and the orange silencer (its mouth at (14.3, 11.7)) |
| `wheel_near_front` | 64x64 | (32, 33) | (−9.9, 3.9) | A tyre and its hub, round (the design hides its top under the rail: filled from its own bottom). It spins about its axle. |
| `wheel_near_back` | 72x72 | (35.9, 36.8) | (10.0, 3.9) | The bigger back tyre, the same way |
| `wheel_far` | 64x64 | (32, 33) | (−9.4, 4) and (10.6, 4) | The front tyre in shade, behind everything, at both axles |
| `steering_wheel` | 72x88 | (8.9, 79.6) | (−11.0, 7.6) | The wheel on its column, in front of Luigi's glove; the pivot is the column's foot on the rail |
| `plate_01` | 64x40 | (32, 20) | (2, 6.6) (sl_kart.py) | The L-shaped "01" plate on the side pod. The numbers are `kart_plates.py`'s. |
| `exhaust_puff` | 48x40 | (24, 20) | about (15.2, 12.2) | A white puff at the silencer |
| `smoke_puff` | 80x72 | (40, 36) | over the wreck | Dark smoke. Scale it up, drift it and fade it out. |
| `flame` | 72x56 | (6, 28) | (14.3, 11.8) | A backfire out of the silencer, pivoting at its mouth |
| `kart_body_wrecked` | as `kart_body` | | (0, 0) | Six deep bites out of the rim (dents), seven soot splats, two broad burns, three cracks |
| `kart_nose_wrecked` | as `kart_nose` | | (−11.1, 4.3) | The fender bent well forward, dented, burnt, sooted and cracked |

### Paints and wrecks

`kart_body`, `kart_nose` and `kart_seat` come in every team's paint at the same sizes and pivots: `_red` (Revvo's
team), `_blue`, `_yellow`, `_purple`, `_orange`, `_white` and `_black`. The design's green and its line are taken
through each paint's line, shadow, base and light by their lightness, so the model's shading stays. The white pod,
the grey seat, the tyres and the soot keep their own colours. Green is Luigi's cap's. Every paint has its wreck too:
`kart_body_wrecked_<colour>` and `kart_nose_wrecked_<colour>`. The engine, wheels and steering wheel are shared.

### Draw order, back to front

1. `wheel_far` (×2)
2. `kart_engine`
3. `kart_seat`
4. Luigi
5. `kart_nose`
6. `kart_body`
7. `plate_01`
8. `wheel_near_front` and `wheel_near_back`
9. `steering_wheel`, in front of the glove

## Checks

`check_manifesto.py` passes on the body, nose, seat, engine, wheels and steering wheel. The old drawn wheels and
steering wheel failed it on their CMPR colour count and line evenness. Its detail figure flags the wheels, engine and
steering wheel (hubs, the silencer and the column are fine detail at their size). That is a warning, not a failure.

## Rough edges

- Qwen wouldn't split the kart into laid-out pieces (it handed the kart back whole), and asked for a wreck it drew
  a new kart in a new view, so the wrecks are made from the whole cards.
- `c_lp_kart_wreck` at rest (S_1, `sl_kart.py` `wreck_rest`) has its front wheels off, the near one lying flat in
  front, sits nose down 5 degrees, the steering wheel knocked askew, and smokes: it reads as a wreck at gallery size.
- Luigi rides higher than in the drawn kart: `sl_kart_luigi.py` moves his seat by (−2.5, 4.4).
