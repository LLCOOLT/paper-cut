# The Choir's torches (Super Luigi, chapter 6)

Two more torches for the Choir of Torches (jin_66 on jin_07, which has four;
`docs/super_luigi/ch6.md` asks six: "three torches burn blue and three are
dark"). Drawn to stand beside the room's own iron torches: a dark iron post
on a foot, a cup at the top, the steeple's pale blue flame.

- **Sources:** made by [`../tools/ch6_torch.py`](../tools/ch6_torch.py),
  which also writes `preview.png` and `parts.json`.
- **Model:** `c_sl_torch` (`tools/port/sl_ch6.py`): `S_1` lit (the flame's
  two frames in turn, every 8), `S_2` dark (no flame, the cup cold).

## Files

| File | Size | Use |
|---|---|---|
| `torch_post` | 80x448 | The post on its foot, 52.6 units to the cup. |
| `torch_cup`, `torch_cup_dark` | 64x40 | The cup, its coals glowing blue or cold. |
| `torch_flame_0`, `_1` | 64x112 | The flame leaning left, then right. |

## Checks (`check_style.py`)

| Check | Status | Measured |
|---|---|---|
| R1: sides multiples of 8 | **pass** | as above |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels on every card |
| R4: outline 4-5 px median, heavier bottom/right, a dark tint of the fill | **pass** (the cup heavier) | post 5 (L4 R6 T4 B5) `#14161e`; flame 5 (L4 R6) `#1a3a5a`; cup 8, a small part where the line is most of it |
| R5: fills | **pass** | iron `#4a5060`, the flame `#9ad8f0` |
| R6b: highlight | **pass** (post, flame) | post 7.6 % of the part, centroid y 0.49 |
