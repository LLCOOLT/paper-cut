# Future Sight's overlay (Super Luigi, chapter 6)

The 2D art for Future Sight, the tiara's gift in the last battle
(`docs/super_luigi/ch6.md`, New assets 9 and the battle's phase 2): "a pale
picture floats over Crepe's head showing his next move: its icon and name,
and who it'll hit ... in the gold border's style"; "a gold ring shrinks round
the target; when it closes, that's the Superguard moment"; Compass Bolt is
"shown with a warning glow".

- **Sources:** made by [`../tools/future_sight.py`](../tools/future_sight.py),
  which also writes `preview.png` (a mock-up of the overlay).
- **Style:** the game's HUD: icons like `icon.tpl`'s small icons (a thin dark
  line, lit from the top left), gold and cream frames. All 1-bit alpha, so
  the framework can tint, scale and fade them. The move's name and target
  ("Next: Flambé → everyone") are game text, drawn by the framework.

## Files

| File | Size | Use |
|---|---|---|
| `border_corner` | 32x32 | The gold screen border's corner, with a blue jewel (the tiara's piece). Flip it for the other three corners. |
| `border_edge` | 32x16 | The border's edge strip; tiles along x (rotate it for the sides). Gold with a lit top, a shadow band and a diamond stud every 16 px. |
| `vision_bubble` | 192x112 | The vision: a pale cream cloud-shaped frame with a dashed gold inner ring. Room for a 32x32 icon on the left and two lines of text. |
| `vision_puff_big`, `vision_puff_small` | 24x24, 16x16 | The two trailing puffs from the bubble down to Crepe's head |
| `guard_ring` | 128x128 | The gold ring with four sparkles. Scale it down round the target; it's "closed" at the target's size. |
| `guard_ring_closed` | 128x128 | The flash as it closes (the Superguard moment): white-gold with eight sparkles. Show it for a few frames. |
| `icon_flambe` | 32x32 | Crepe: **Flambé**, a fan of flame |
| `icon_fold` | 32x32 | Crepe: **Fold**, a crepe folded shut over Luigi: his green cap and face peek out between the browned flaps |
| `icon_batter` | 32x32 | Crepe: **Batter**, a blue mixing bowl of batter with a balloon whisk standing in it, its wire loops open |
| `icon_reshuffle` | 32x32 | Crepe: **Cabinet Reshuffle**, two cards swapping with circling arrows |
| `icon_compass_bolt` | 32x32 | Crepe: **Compass Bolt**, a bolt of gold light |
| `icon_compass_bolt_warn` | 32x32 | The same on a red warning burst (the vision's "warning glow") |
| `icon_doom_foretold` | 32x32 | The Nightmare: **Doom Foretold**, the compass with its needle locked (the vision adds the countdown 3, 2, 1 as text) |
| `icon_hatesong` | 32x32 | The Nightmare: **Hatesong**, a howling mouth with gold teeth and sound waves |
| `icon_thousand_tomorrows` | 32x32 | The Nightmare: **A Thousand Tomorrows**, five pages fanned like five strikes |
| `icon_page_grab` | 32x32 | The Nightmare: **Page Grab**, a page-hand closing |

The plan asks for seven icons; Crepe has five moves and the Nightmare four,
so all nine are here, plus Compass Bolt's warning variant.

## Checks

| Check | Status | Measured |
|---|---|---|
| Sizes in multiples of 8 | **pass** | 16 to 192 px |
| 1-bit alpha | **pass** | 2 levels, 0 partial texels |
| Icons' line | **pass** | About 2 to 3 px in a dark tint of each fill, as the game's 32x32 icons |

## Rough edges

- `icon_batter` and `icon_fold` were redrawn to read at 32 px on their own
  (a bowl and whisk; Luigi's capped head between two crepe flaps).
- Not drawn in the game's battle yet: the layout is the framework's.
