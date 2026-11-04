"""Big Green 01's stolen steering wheel, ch3_props/wheel.png: `python ch3_wheel.py`.

cutout.py cuts it from the style model's design (cut_wheel.py); the design's background also shows through the four
openings between the spokes, which a flood from the edges can't reach, so they are flooded from inside here, cleared
on the card, and lined in the card's LINE.
"""
import json
import os
import shutil
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cut_wheel as S  # noqa: E402
import cutout  # noqa: E402
from ink import cmpr_round_trip, rgb  # noqa: E402

ART = os.path.join(HERE, "..")
CARD = os.path.join(ART, "ch3_props", "wheel.png")


def holes():
    """the openings' mask on the design: inside the rim, the background's tan (the openings between the spokes and the
    slots in them)"""
    a = np.asarray(Image.open(os.path.join(ART, S.SRC)).convert("RGB")).astype(np.float32)
    cx, cy = S.ORIGIN
    # the background seen through the wheel is a warm tan in the wheel's shade; the rim and spokes are neutral steel
    # and black and the boss green, so warmth tells them apart where brightness can't
    yy, xx = np.mgrid[:a.shape[0], :a.shape[1]]
    inside = (xx - cx) ** 2 + (yy - cy) ** 2 < 420 ** 2
    warm = (a[..., 0] - a[..., 2] > 25) & (a[..., 0] > 120) & (a[..., 0] >= a[..., 1])
    return inside & warm


def main():
    cutout.main("cut_wheel")
    out = os.path.join(ART, S.OUT)
    meta = json.load(open(os.path.join(out, "parts.json")))["cards"]["wheel"]
    card = np.asarray(Image.open(os.path.join(out, "wheel.png")).convert("RGBA")).copy()
    h, w = card.shape[:2]
    m = holes()
    px, py = meta["pivot"]
    k = cutout.DENSITY  # texels a game texel
    yy, xx = np.mgrid[:h, :w]
    # each texel's middle on the design
    dx = np.clip(((xx + 0.5 - px) * S.PX / k + S.ORIGIN[0]).astype(int), 0, m.shape[1] - 1)
    dy = np.clip(((yy + 0.5 - py) * S.PX / k + S.ORIGIN[1]).astype(int), 0, m.shape[0] - 1)
    hole = m[dy, dx]
    card[hole, 3] = 0
    op = card[..., 3] > 0

    def grow(a):
        for _ in range(k):  # one game texel
            p = np.pad(a, 1)
            a = a | p[:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, :-2] | p[1:-1, 2:]
        return a
    edge = op & grow(~op)
    near = grow(hole)
    card[edge & near, :3] = rgb(S.LINE).astype(np.uint8)
    Image.fromarray(cmpr_round_trip(card), "RGBA").save(CARD)
    pj = os.path.join(ART, "ch3_props", "parts.json")
    pm = json.load(open(pj, encoding="utf-8"))
    pm["cards"]["wheel"].update(size=[w, h], pivot=meta["pivot"], texels_per_unit=cutout.TEXELS_PER_UNIT, desc=meta["desc"])
    json.dump(pm, open(pj, "w", encoding="utf-8"), indent=1)
    print("wheel: %dx%d, pivot %s" % (w, h, meta["pivot"]))


if __name__ == "__main__":
    main()
