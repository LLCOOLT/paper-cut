"""Chapter 6's Choir torches (Super Luigi): two more torches for the Choir of Torches (jin_07 has
four, ch6.md asks six), drawn to stand beside the room's own: a dark iron post on a foot, a cup at
the top, and the steeple's pale blue flame.

Run: python ch6_torch.py -> ../ch6_torch/*.svg, *.png, parts.json, preview.png
8 texels/unit, outlined, lit from the top left. The model (c_sl_torch, tools/port/sl_ch6.py) has
two poses: S_1 lit (the flame's two frames in turn) and S_2 dark (no flame, the cup cold).
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, stroke_svg, fill_svg, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "ch6_torch")
R = Rig("sl_ch6_torch", OUT)

IRON = dict(fill="#4a5060", light="#6a7286", shadow="#30343e", line="#14161e")
IRON_D = dict(fill="#3a3f4c", light="#545a6a", shadow="#262a32", line="#101218")
FLAME = dict(fill="#9ad8f0", light="#e8f8ff", shadow="#5aa8d8", line="#1a3a5a")
EMBER = "#1e2a3a"


def post():
    c = R.card("torch_post", (-5.0, 0.0, 5.0, 56.0))
    # the foot, a thin post with two collars, and the cup's bracket
    pts = [(-4.6, 0.3), (4.6, 0.3), (4.2, 2.2), (1.6, 3.6), (1.0, 6.0), (1.0, 24.0), (1.8, 25.0), (1.8, 26.6),
           (1.0, 27.6), (1.0, 46.0), (1.8, 47.0), (1.8, 48.6), (1.0, 49.6), (1.0, 52.6), (-1.0, 52.6), (-1.0, 49.6),
           (-1.8, 48.6), (-1.8, 47.0), (-1.0, 46.0), (-1.0, 27.6), (-1.8, 26.6), (-1.8, 25.0), (-1.0, 24.0),
           (-1.0, 6.0), (-1.6, 3.6), (-4.2, 2.2)]
    det = [stroke_svg(c.line([(-3.4, 1.6), (3.4, 1.6)]), IRON["shadow"], 2),
           stroke_svg(c.line([(-0.2, 7.0), (-0.2, 23.0)]), IRON["light"], 2),
           stroke_svg(c.line([(-0.2, 28.6), (-0.2, 45.0)]), IRON["light"], 2)]
    c.shape(pts, IRON, det, seed=3, tension=0.12, jitter=0.25, lo=(3, 3), so=(4, 4), soft=1.2)
    return c.save((0.0, 0.0), "the torch's iron post on its foot, 52.6 tall to the cup's bracket; pivot on the floor "
                              "at its middle")


def cup(lit):
    name = "torch_cup" if lit else "torch_cup_dark"
    c = R.card(name, (-4.0, -0.5, 4.0, 4.5))
    pts = [(-3.6, 4.0), (3.6, 4.0), (2.6, 0.8), (1.0, 0.0), (-1.0, 0.0), (-2.6, 0.8)]
    det = [fill_svg(c.d([(-3.0, 3.9), (3.0, 3.9), (2.6, 3.0), (-2.6, 3.0)], tension=0.2, jitter=0.1),
                    "#5ab4e0" if lit else EMBER),
           stroke_svg(c.line([(-2.8, 2.0), (2.8, 2.0)]), IRON["shadow"], 2)]
    c.shape(pts, IRON_D, det, seed=5, tension=0.2, jitter=0.2, lo=(2, 2), so=(3, 3), soft=0.8)
    return c.save((0.0, 0.0), "the cup on the post's top, its coals %s; pivot at its bottom middle"
                  % ("glowing blue" if lit else "cold"))


def flame(k):
    c = R.card("torch_flame_%d" % k, (-4.0, 0.0, 4.0, 14.0))
    lean = 0.8 if k == 0 else -0.8
    pts = [(-3.2, 1.0), (-2.8, 4.6), (-1.6 + lean * 0.3, 8.2), (-0.6 + lean, 11.4), (0.4 + lean * 1.4, 13.4),
           (1.2 + lean, 10.6), (2.6, 7.0), (3.2, 3.4), (2.8, 0.8), (0.0, 0.2)]
    inner = c.d([(-1.6, 1.4), (-1.0, 4.6), (0.0 + lean * 0.6, 8.4), (1.0, 4.4), (1.6, 1.4), (0.0, 0.8)], tension=0.5,
                jitter=0.2, seed=11 + k)
    c.shape(pts, FLAME, [fill_svg(inner, FLAME["light"])], seed=9 + k, tension=0.6, jitter=0.4, lo=(2, 2),
            so=(3, 3), soft=1.0)
    return c.save((0.0, 0.0), "the pale blue flame, frame %d (it leans %s): over the cup; pivot at its foot"
                  % (k, "left" if k == 0 else "right"))


def preview():
    W, H = 360, 600
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    gy = 560

    def put(name, x, y, at=(0.0, 0.0)):
        m = R.meta[name]
        im = Image.open(os.path.join(OUT, name + ".png")).convert("RGBA")
        px, py = m["pivot"]
        c.alpha_composite(im, (round(x + at[0] * 8 - px), round(y - at[1] * 8 - py)))

    for i, lit in enumerate((True, False)):
        x = 100 + i * 160
        put("torch_post", x, gy)
        put("torch_cup" if lit else "torch_cup_dark", x, gy, (0.0, 52.4))
        if lit:
            put("torch_flame_0", x, gy, (0.0, 55.6))
        d.text((x - 30, gy + 10), "lit" if lit else "dark", fill=(40, 30, 30, 255))
    d.text((10, 10), "The Choir's torches, 1:1 at 8 texels/unit", fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    post()
    cup(True)
    cup(False)
    flame(0)
    flame(1)
    R.write_json({"draw_order": ["torch_post", "torch_cup / torch_cup_dark (at 0, 52.4)", "torch_flame_0/1 "
                                 "(at 0, 55.6)"]})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
