# The friends' partner icons (Super Luigi)

Icons for Luigi's five friends, Blooey (`c_lp_gesso`), Jerry (`c_lp_bom`),
Torque (`c_lp_met`), Hayzee (`c_lp_pansy`) and Screamy (`c_lp_moa`), plus
Blooey burnt for chapter 6, in the two forms the game's partner UI uses.
Each friend's icons are in its own folder here.

- **Sources:** made by [`../tools/icons.py`](../tools/icons.py), which also
  writes `preview.png` (every icon at 2x and at game size).
- **Measured** on the game's own icons in `icon.tpl` and `icon.bin`, decoded
  into scratch (nothing from the disc is in the repo). An icon's image in
  `icon.tpl` is its `icon.bin` tplId + 8. The friends' looks follow their
  textures in `a/c_lp_*-`, which were decoded the same way and redrawn here.

## The two forms

| Form | Size | The game's (Goombella's) | Where the game shows it | What it looks like |
|---|---|---|---|---|
| `_hud` | 48x48 | `ICON_1A4`, image 448 (Koops `1A5`, Bobbery `1A6`, Yoshi `1A7`..`1AD`, Flurrie `1AE`, Vivian `1AF`, Ms. Mowz `1B0`) | The status bar, beside the partner's HP (`statuswindow.c`, `party_icon[]`) | The head in 3/4 view, low and to the left, with the pink HP heart (two eyes) behind it at the top right. The pair is cut out as one sticker: a white border 2 to 3 px wide with a light grey inner edge, a dark grey (`#424242`) rim heavier on the bottom right, and 2 to 3 px of clear margin. The face's own line is thin, about 2 px. |
| `_menu` | 40x40 | `ICON_155`, image 354 (Koops `156`, Bobbery `157`, Yoshi `158`..`15E`, Flurrie `15F`, Vivian `160`, Ms. Mowz `161`) | The battle menu's partner list (`battle_seq_command.c`) and the pause menu's party page (`win_party.c`) | The whole friend standing, filling the square top to bottom, with no border and a dark line in a tint of each colour (the game's measures 3 to 4 px median) |

The game has **no hurt or low-HP partner faces**: each partner has one
icon of each form, and Yoshi's colours are the only variants. So each
friend here has one of each, and Blooey has a burnt pair.

## Icons

Each friend is drawn from its model as it stands in the game: the `a/c_lp_*`
model and its textures, decoded into scratch and rendered flat from the stand
pose (`Z_1`, frame 0), then put beside the icon at the same height and at
40 px. Colours are sampled from the model's own textures.

| Friend | Files | Look |
|---|---|---|
| Blooey | `blooey/blooey_hud.png`, `blooey/blooey_menu.png` | One orange-gold Blooper card: a rounded-point mantle with side fins, the outer tentacles flaring sideways and the inner ones hanging, and the brown mask band with two ringed eyes |
| Blooey, burnt (ch6, from jin_70 on) | `blooey/blooey_soot_hud.png`, `blooey/blooey_soot_menu.png` | The same shape scorched near-black, a few white patches left, a smoke wisp off the top |
| Jerry | `jerry/jerry_hud.png`, `jerry/jerry_menu.png` | A round red bomb with two tall white eyes to the front, an orange glint, a brown fuse stem arcing up and right to a crossbar, and two big red feet (no key, no cap: the model has neither) |
| Torque | `torque/torque_hud.png`, `torque/torque_menu.png` | A green shell with a white rim sweeping across it, his tan head peeking out low at the front with one dark eye, white claws, and a big open-end wrench standing up out of the top of the shell on a white collar (no helmet: `t_5` isn't shown in any of his poses) |
| Hayzee | `hayzee/hayzee_hud.png`, `hayzee/hayzee_menu.png` | A mint four-petal flower (up, down, left, right) round a yellow oval face with two dark eyes, pink blush and a smile; white gloves out at the sides and pale blue shoes. No cap. |
| Screamy | `screamy/screamy_hud.png`, `screamy/screamy_menu.png` | A pale puff whose top rises off-centre into a wisp curling over to the right; two big salmon eyes under heavy lowered lids (his sleepy look, not round open eyes), a small wavy mouth, two little white feet |

The HUD form shows the same whole figure as the menu form, larger and low on
the left, since each friend's body is mostly head.

Friends don't fight (SUPER_LUIGI.md), so where these are used is up to the
UI work: the `_hud` icon beside the friend in the status bar, and the
`_menu` icon wherever a menu lists the friend.

## Checks

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Check | Status | Measured |
|---|---|---|
| Sizes as the game's | **pass** | 48x48 and 40x40 |
| 1-bit alpha | **pass** | 2 levels, 0 partial texels on all twelve |
| HUD sticker rim | **pass** | `#424242`, 1 to 2 px, B 4.5 against T 2 (heavier at the bottom right, as on image 448) |
| Menu line, 3 to 4 px, dark tint | **pass** | Median 3 to 4 (Torque 2, where the white rim and claws use a thinner line so they read). Colours from each model's own line: `#382820`, `#0d2020`, `#2a2a30`, `#184030`, `#2a2222`, burnt `#151012` (lum p90 18 to 62). |

## Rough edges

- Small parts (Torque's wrench, rim and claws; Hayzee's gloves and shoes;
  Screamy's feet; Blooey's band) use a thinner line than the body, about 0.6
  to 0.8 of it, so they keep their fill at 40 px instead of reading as dark
  blobs. Burnt Blooey's white patches still mostly disappear at 40 px.
- Not seen in the game's HUD or menus yet.

## Luigi's own faces (`luigi/`)

`ICON_1A3` (the HP face, 52x52), `ICON_19E` (the pause menu's head, 40x40) and
`ICON_1A1` (the level row's coin, 40x40), shown in place of Mario's while
Luigi is the player (`sl_assets_aurora.c`, `portIconTexOverride`), are made
**from Mario's own icons on the player's disc**, when the game starts, by the
recipes in [`../recipes/luigi_icons.recipe`](../recipes/luigi_icons.recipe)
(the format: [`../RECIPES.md`](../RECIPES.md)). Nothing of them is in the repo
but the recipe. Same canvas, pose, line, sticker border, heart and shading
bands as Mario's. Changed:

- the cap's reds are mapped to Luigi's greens band for band (by lightness
  against Mario's fill, on c_luigi's cap ramp);
- the emblem's M is repainted as an L, and the disc's pink whites as
  green-tinted whites;
- Luigi's moustache (wider, two lobes, square swept-up ends) is repainted in
  Mario's own browns;
- the jaw is 1-2 texels longer.

Nose and eyes are Mario's. The images are texel for texel those of commit
8996fe085, which committed them as PNGs, on the US, JP and EU discs.

- **Fallback:** `luigi/drawn/` holds the drawn faces from before (original
  art), used if the recipes can't run (`fallback` lines; `TTYD_ART=off`).
- **The menus' textures** (`luigi/ui/`, `luigi/ui_jp/`): the pause menu's
  first tab ("Luigi", ルイージ) is made the same way, from the disc's own tab
  and the lettering of its other tabs ([`../recipes/luigi_tabs.recipe`](../recipes/luigi_tabs.recipe));
  level-up's BP panel is drawn ([`../tools/luigi_icons.py`](../tools/luigi_icons.py)).
- **Seen in game:** the HP face in the field (US); the pause menu's Luigi page
  with the tab, the head and the coin (US and JP).
- **Rough edges:** the HUD face's longer chin leaves a one-texel step in the
  sticker's border at its bottom right; the moustache is hand-placed per
  column (the `tache` spans) and can be tuned there. The tabs' letters come
  from tabs of other sizes, so their heights differ by a texel or two.
