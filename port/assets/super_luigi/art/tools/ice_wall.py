"""The ice wall (Super Luigi ch2): a packed-snow block across the lane to the tailor and in the
hollow tree, burst by Jerry's Blast.

Run: python ice_wall.py -> ../ice_wall/*.svg, *.png, parts.json, preview.png
ch2.md, New assets 6: "the ice wall (a packed-snow block, two sizes)". Front cards, drawn like the
game's paper props at 8 texels/unit (outlined, top-left light), in two sizes, plus three chunks that
fly when it bursts. Standing on y 0, centred on x 0.
"""
import os
import random
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, assemble, stroke_svg, fill_svg, smooth_path, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "ice_wall")
R = Rig("sl_ice_wall", OUT)

SNOW = dict(fill="#dbe6ee", light="#f2f7fa", shadow="#a9bfd4", line="#1c2c48")
ICE = dict(fill="#9cc8e6", light="#cfe8f6", shadow="#6c9cc6", line="#142a4e")
CAP = dict(fill="#eef3f7", light="#fbfdfe", shadow="#c6d4e2", line="#1c2c48")
JOINT = "#7f9cbc"


def wall(name, w, h, seed):
    """w, h in units; the card is w+2 by h+4 (the snow cap and the line's margin)."""
    rnd = random.Random(seed)
    c = R.card(name, (-w / 2 - 1, -1, w / 2 + 1, h + 3))
    x0, x1 = -w / 2, w / 2
    # the block: slightly irregular box, a little narrower at the top (packed and slumped)
    outline = [(x0 + 0.2, 0.3), (x1 - 0.2, 0.3), (x1 + 0.2, h * 0.5), (x1 - 0.3, h), (0, h + 0.2), (x0 + 0.3, h),
               (x0 - 0.2, h * 0.5)]
    # packed-snow blocks: staggered rows of joints, each block a slightly wavy rectangle
    det, rows, row_h = [], max(2, round(h / 5.5)), h / max(2, round(h / 5.5))
    for r in range(rows):
        yb, yt = r * row_h, (r + 1) * row_h
        if r:
            det.append(stroke_svg(c.line([(x0 - 1, yb)] + [(x0 + w * t, yb + rnd.uniform(-0.25, 0.25))
                                         for t in (0.25, 0.5, 0.75)] + [(x1 + 1, yb)]), JOINT, 3))
        bw = 8.0
        off = (r % 2) * bw / 2 + rnd.uniform(-0.6, 0.6)
        x = x0 + off
        while x < x1 - 1:
            if x > x0 + 1:
                det.append(stroke_svg(c.line([(x, yb + 0.2), (x + rnd.uniform(-0.3, 0.3), yt - 0.2)]), JOINT, 3))
            # a soft lit patch in each block's top left (packed snow catches the light)
            det.append(fill_svg(c.ell(x + 2.6, yt - 1.4, 1.6, 0.6, 10, wobble=0.1, seed=int(x * 7 + r)), SNOW["light"]))
            x += bw + rnd.uniform(-1.0, 1.0)
        # a few ice crystals glinting in the snow
        for _ in range(max(1, w // 10)):
            gx, gy = rnd.uniform(x0 + 1.5, x1 - 1.5), rnd.uniform(yb + 1, yt - 1)
            det.append(fill_svg(c.ell(gx, gy, 0.35, 0.35, 6), ICE["fill"]))
    c.shape(outline, SNOW, det, hl=(0.36, 0.22, 0.26, 0.14), hl_alpha=0.6, seed=seed, tension=0.25, jitter=0.6,
            lo=(7, 7), so=(9, 9))
    # the snow cap: a pillowy drift over the top, with icicles hanging off its front
    cap = [(x0 - 0.6, h - 0.6)]
    n = max(4, round(w / 4))
    for k in range(n + 1):
        t = k / n
        cap.append((x0 - 0.4 + (w + 0.8) * t, h + 1.2 + (0.9 if k % 2 else 0.3) + rnd.uniform(-0.2, 0.2)))
    cap += [(x1 + 0.6, h - 0.6), (x1 - 0.5, h - 1.6)]
    for k in range(n - 1, 0, -1):   # icicles hanging off the cap's front edge
        t = k / n
        xi = x0 + w * t + rnd.uniform(-0.5, 0.5)
        cap += [(xi + 0.9, h - 1.3), (xi + 0.1, h - 1.6 - rnd.uniform(1.0, 2.6)), (xi - 0.6, h - 1.3)]
    cap += [(x0 + 0.5, h - 1.6)]
    c.shape(cap, CAP, hl=(0.36, 0.3, 0.3, 0.2), hl_alpha=0.6, seed=seed + 1, tension=0.35, jitter=0.3,
            lo=(4, 4), so=(5, 5))
    # a blue sheet of ice in the lower corner (it's frozen through)
    ice = [(x1 - w * 0.32, 0.6), (x1 - 0.6, 0.6), (x1 - 0.4, h * 0.42), (x1 - w * 0.16, h * 0.3)]
    c.shape(ice, ICE, [stroke_svg(c.line([(x1 - w * 0.22, 1.4), (x1 - w * 0.12, h * 0.22)]), ICE["light"], 2.5)],
            seed=seed + 2, tension=0.3, lo=(4, 4), so=(5, 5), soft=2, hl=(0.4, 0.3, 0.3, 0.2))
    return c.save((0.0, 0.0), f"the packed-snow wall, {w}x{h} units; pivot at its foot's middle (0, 0)")


def chunk(name, pts, seed):
    c = R.card(name, (-3.0, -3.0, 3.0, 3.0))
    c.shape(pts, SNOW, [stroke_svg(c.line([(pts[0][0] * 0.4, pts[0][1] * 0.4), (pts[2][0] * 0.3, pts[2][1] * 0.3)]),
                                   JOINT, 2.5)],
            hl=(0.38, 0.28, 0.3, 0.2), hl_alpha=0.6, seed=seed, tension=0.15, jitter=0.4, lo=(4, 4), so=(5, 5), soft=2)
    return c.save((0.0, 0.0), "a chunk of packed snow for the burst; spin it and throw it out, pivot at its middle")


ORDER = ["ice_wall_small", "ice_wall_large"]


def preview():
    W, H = 760, 340
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    for name, x in (("ice_wall_small", 20), ("ice_wall_large", 250)):
        im = Image.open(os.path.join(OUT, name + ".png")).convert("RGBA")
        c.alpha_composite(im, (x, 308 - im.height))
        d.text((x, 300 - im.height - 16), name, fill=(40, 30, 30, 255))
    for i, n in enumerate(("chunk_a", "chunk_b", "chunk_c")):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im, (660 + (i % 2) * 50, 120 + i * 50))
    d.text((650, 100), "burst chunks", fill=(40, 30, 30, 255))
    d.rectangle([0, 300, W, 302], fill=(180, 170, 150, 255))
    d.rectangle([215, 300 - 192, 219, 300], fill=(60, 140, 60, 255))
    d.text((10, 318), "The ice wall, 1:1 at 8 texels/unit; the green bar is Luigi's height", fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    wall("ice_wall_small", 20, 22, 21)
    wall("ice_wall_large", 40, 30, 31)
    chunk("chunk_a", [(-2.4, 1.6), (1.8, 2.2), (2.4, -1.2), (-0.4, -2.2), (-2.2, -1.0)], 41)
    chunk("chunk_b", [(-1.8, 2.0), (2.2, 1.0), (1.4, -2.0), (-2.0, -1.6)], 42)
    chunk("chunk_c", [(-2.0, 0.6), (0.2, 2.2), (2.0, 0.8), (1.0, -1.6), (-1.6, -1.8)], 43)
    R.write_json({"note": "the two walls are alternatives (two sizes); chunk_* fly when it bursts"})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
