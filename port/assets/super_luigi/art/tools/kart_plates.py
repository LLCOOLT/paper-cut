"""The rivals' kart plates: `python kart_plates.py` writes port/assets/super_luigi/art/kart/plate_NN.png from plate_01.png, each
kart's number as the race scoreboard lists it (scoreboard.py's GRID), the star in its team's colour; plate_blank.png for the wrecks.
Nothing here reads the disc."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "..", "tools", "port"))
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

from ink import Card, SS, cmpr_round_trip, rgb, save  # noqa: E402

KART = os.path.join(HERE, "..", "kart")
INK = "#23211f"
# a digit in a 12 x 19 box, its strokes as (x, y, width); round, hand-inked, as the plate's own 0 and 1
DIGITS = {
    "0": [[(6, 1, 3.4), (10.5, 5, 3.6), (11, 12, 3.6), (6, 18, 3.4), (1.5, 13, 3.6), (1, 6, 3.6), (6, 1, 3.4)]],
    "1": [[(3, 5, 3.2), (7, 1, 3.6), (7, 10, 3.6), (7, 18, 3.8)]],
    "2": [[(1.5, 5, 3.2), (6, 1, 3.6), (10.5, 5, 3.6), (7, 11, 3.6), (1.5, 18, 3.8)], [(1.5, 18, 3.8), (6, 17.5, 3.4), (11, 18, 3.6)]],
    "3": [[(1.5, 3, 3.2), (6, 1, 3.6), (10, 4.5, 3.6), (5.5, 9, 3.2)], [(5.5, 9, 3.2), (10.5, 13, 3.6), (6, 18, 3.6), (1.5, 15.5, 3.2)]],
    "4": [[(8.5, 1, 3.6), (1.5, 12.5, 3.4), (6, 12.5, 3.2), (11.5, 12.5, 3.4)], [(8.5, 5, 3.6), (8.5, 12, 3.6), (8.5, 18, 3.8)]],
    "5": [[(10.5, 1.5, 3.4), (5, 1.5, 3.4), (2.5, 2, 3.4), (2, 8.5, 3.4), (6.5, 7.5, 3.4), (10.5, 12, 3.6), (6, 18, 3.6), (1.5, 15.5, 3.2)]],
    "6": [[(9.5, 1.5, 3.2), (3.5, 6, 3.6), (1.5, 12, 3.6), (6, 18, 3.6), (10.5, 13, 3.6), (6, 9, 3.4), (2, 12, 3.4)]],
    "7": [[(1.5, 2, 3.4), (6, 1.5, 3.4), (11, 2, 3.6), (7.5, 9, 3.6), (4.5, 18, 3.8)]],
    "8": [[(6, 9, 3.2), (2.5, 5, 3.4), (6, 1, 3.4), (9.5, 5, 3.4), (6, 9, 3.2), (1.5, 13.5, 3.6), (6, 18, 3.6), (10.5, 13.5, 3.6), (6, 9, 3.2)]],
    "9": [[(10, 6, 3.4), (6, 10, 3.4), (1.5, 6, 3.6), (6, 1, 3.6), (10.5, 6, 3.6), (9, 12, 3.6), (3, 18, 3.2)]],
}
# number, the team's colour (scoreboard.py's GRID, the name as sl_kart.py's COLOURS)
PLATES = {"13": "#d8303a", "07": "#3a6ad8", "22": "#d8a820", "45": "#8a4ac8", "88": "#f08a2a", "64": "#8a9098", "99": "#2a2a32"}


def plate(number, star):
    base = np.array(Image.open(os.path.join(KART, "plate_01.png")).convert("RGBA"))
    out = base.copy()
    op = base[..., 3] >= 128
    r, g, b = [base[..., i].astype(int) for i in range(3)]
    # the plate's cream, sampled where nothing is drawn (its bottom left)
    cream = base[31, 12].copy()
    # the old number gone, only inside the plate's own line
    box = np.zeros(op.shape, dtype=bool)
    box[19:35, 23:57] = True
    wipe = box & op
    out[wipe] = cream
    # the star, team-coloured; its shading kept
    green = op & (g > r + 25) & (g > b + 25)
    if star is None:
        out[green] = cream
    else:
        tone = g[green] / 170.0
        out[..., :3][green] = np.clip(np.outer(tone, rgb(star)), 0, 255).astype(np.uint8)
    if number:
        c = Card(64, 40, INK)
        for i, d in enumerate(number):
            ox, oy = 27.0 + i * 13.0, 20.0
            for stroke in DIGITS[d]:
                c.ink([(ox + x * 0.9, oy + y * 0.72, w * 0.8) for x, y, w in stroke])
        # what the strokes painted: anything Card's black canvas no longer holds
        painted = (c.rgb.sum(axis=2) > 1).astype(np.float32)
        ink_a = painted.reshape(40, SS, 64, SS).mean(axis=(1, 3))
        for i in range(3):
            out[..., i] = (out[..., i] * (1 - ink_a) + rgb(INK)[i] * ink_a).astype(np.uint8)
        out[..., 3] = base[..., 3]
    return cmpr_round_trip(out)


def main():
    for number, star in PLATES.items():
        save(plate(number, star), os.path.join(KART, "plate_%s.png" % number))
    save(plate("", None), os.path.join(KART, "plate_blank.png"))


if __name__ == "__main__":
    main()
