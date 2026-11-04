"""Chapter 4's props (Super Luigi): the Fiery Hat and the chandelier that falls.

Run: python ch4_props.py -> ../ch4_props/*.svg, *.png, parts.json, preview.png
ch4.md, New assets 3-4: "The Fiery Hat (prop): a tall red top hat with paper flames, flickering
(two-frame flame loop). Jerry wears it in the play (a hat on a Bob-omb)"; "a chandelier prop that
can fall", as a battle stage prop in the stage-prop style (flat cards). 8 texels/unit, outlined,
lit from the top left.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, stroke_svg, fill_svg, smooth_path, ellipse_pts, star_pts, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "ch4_props")
R = Rig("sl_ch4_props", OUT)

HAT = dict(fill="#d0283a", light="#ee5a64", shadow="#981424", line="#3e0610")
BAND = dict(fill="#2a2228", light="#4a3e46", shadow="#18121a", line="#0a0608")
FLAME = dict(fill="#ff9a2a", light="#ffd84a", shadow="#e8561c", line="#5a1a04")
FLAME_CORE = "#fff2a0"
GOLD = dict(fill="#e8b440", light="#f8d880", shadow="#ae7e1c", line="#4a3006")
CANDLE = dict(fill="#f4ecd8", light="#ffffff", shadow="#cfc4a8", line="#3e3424")
CRYSTAL = dict(fill="#c8e4f4", light="#f2fbff", shadow="#8ab4d4", line="#1a3450")


def hat():
    c = R.card("fiery_hat", (-6.0, -1.0, 6.0, 15.0))
    brim = [(-5.6, 0.2), (-3.0, -0.4), (3.0, -0.4), (5.6, 0.2), (5.2, 1.4), (3.0, 1.2), (-3.0, 1.2), (-5.2, 1.4)]
    crown = [(-3.4, 0.8), (3.4, 0.8), (3.9, 13.6), (2.0, 14.2), (-2.0, 14.2), (-3.9, 13.6)]
    c.shape(crown, HAT, [stroke_svg(c.curve((-3.3, 13.4), (0.0, 12.9), (3.5, 13.4)), HAT["shadow"], 2)],
            hl=(0.34, 0.26, 0.2, 0.2), seed=1, tension=0.2)
    c.shape([(-3.5, 1.0), (3.5, 1.0), (3.6, 3.0), (-3.6, 3.0)], BAND, seed=2, tension=0.15, lo=(2, 2), so=(2, 2), soft=1)
    # a little flame badge on the band
    c.shape([(-1.6, 1.4), (-0.6, 1.4), (-0.1, 2.4), (-0.8, 3.6), (-1.0, 2.6), (-1.5, 3.1), (-2.0, 2.2)], FLAME,
            seed=3, tension=0.4, lo=(1, 1), so=(1, 1), soft=0.6, grow=1.0, inner=1.2, bias=(0.8, 1.0))
    c.shape(brim, HAT, seed=4, tension=0.4, lo=(3, 3), so=(4, 4), hl=(0.4, 0.3, 0.3, 0.3))
    return c.save((0.0, 0.0), "the Fiery Hat: a tall red top hat with a black band; pivot at the brim's middle, "
                              "set on Jerry's top in front of his fuse cap")


def flames(frame):
    name = f"fiery_hat_flames_{frame}"
    c = R.card(name, (-5.0, 12.0, 6.0, 21.0))
    rnd = random.Random(10 + frame)
    tips = [(-3.6, 14.2), (-1.8, 16.6), (0.4, 17.6), (2.4, 16.2), (4.2, 14.0)]
    if frame == 1:   # the flicker: tips swing back and up, one licks higher
        tips = [(-3.0, 14.8), (-1.2, 15.6), (1.2, 17.8), (3.2, 16.8), (4.8, 14.4)]
    pts = [(-3.8, 10.4)]
    for i, (tx, ty) in enumerate(tips):
        pts.append((tx, ty))
        if i < len(tips) - 1:
            nx, ny = tips[i + 1]
            pts.append(((tx + nx) / 2 + rnd.uniform(-0.2, 0.2), min(ty, ny) - 1.8 - rnd.uniform(0, 0.6)))
    pts += [(3.8, 10.4), (0.0, 9.8)]
    core = [(-2.2, 10.6), (-1.2, 13.4), (0.2, 14.8 if frame == 0 else 15.4), (1.6, 13.2), (2.4, 10.6)]
    pts = [(x, y + 3.0) for x, y in pts]   # drawn for a 10-unit crown; the crown is 13 tall
    core = [(x, y + 3.0) for x, y in core]
    c.shape(pts, FLAME, [fill_svg(c.d(core, 0.5, 0.3, 20 + frame), FLAME_CORE)], seed=12 + frame, tension=0.45,
            lo=(4, 4), so=(5, 5), soft=2)
    return c.save((0.0, 13.0), f"the hat's paper flames, frame {frame}: alternate the two every 4 to 6 frames; "
                               "pivot at their foot, on the crown's top (0, 13)")


# ------------------------------------------------------------ chandelier --
def chandelier():
    c = R.card("chandelier", (-20.0, -6.0, 20.0, 22.0))
    # the gold frame: a crown on top, a central column, two tiers of curved arms
    col = [(-1.4, 20.6), (1.4, 20.6), (1.6, 14.0), (2.4, 12.0), (1.4, 4.0), (0.0, 1.6), (-1.4, 4.0), (-2.4, 12.0),
           (-1.6, 14.0)]
    arms = []
    for tier, (y, span) in enumerate(((10.0, 16.0), (15.0, 10.0))):
        for s in (-1, 1):
            for k in (0.5, 1.0):
                x = s * span * k
                arms.append((tier, [(0.0, y - 1.0), (x * 0.5, y - 3.2), (x, y - 1.6), (x, y + 0.6)]))
    for tier, a in arms:
        c.p.raw(stroke_svg(c.line(a), GOLD["line"], 9))
    for tier, a in arms:
        c.p.raw(stroke_svg(c.line(a), GOLD["fill"], 4.5))
        c.p.raw(stroke_svg(c.line(a[:2]), GOLD["light"], 1.6))
    c.shape(col, GOLD, [stroke_svg(c.line([(-2.0, 12.0), (2.0, 12.0)]), GOLD["shadow"], 2.5)],
            hl=(0.35, 0.25, 0.3, 0.2), seed=30, tension=0.4, lo=(3, 3), so=(4, 4))
    c.shape([(-3.0, 20.0), (3.0, 20.0), (3.6, 21.6), (-3.6, 21.6)], GOLD, seed=31, tension=0.2, lo=(2, 2), so=(2, 2))
    # candles on the arms' ends, each with a flame
    for tier, a in arms:
        x, y = a[-1]
        c.shape([(x - 0.7, y), (x + 0.7, y), (x + 0.7, y + 2.6), (x - 0.7, y + 2.6)], CANDLE, seed=40 + int(x * 3),
                tension=0.15, lo=(1.5, 1.5), so=(2, 2), soft=0.8, grow=1.3, inner=1.6, bias=(1, 1.2))
        c.shape([(x - 0.5, y + 2.8), (x, y + 4.4), (x + 0.5, y + 2.8), (x, y + 2.4)], FLAME, seed=50 + int(x * 3),
                tension=0.5, lo=(1, 1), so=(1, 1), soft=0.5, grow=1.0, inner=1.2, bias=(0.8, 1.0),
                details=[fill_svg(c.ell(x, y + 3.0, 0.22, 0.4, 6), FLAME_CORE)])
    # crystal drops hanging under the arms and the column
    drops = [(x, y - 1.8) for tier, a in arms for x, y in a[2:3]] + [(0.0, 0.4)]
    for i, (x, y) in enumerate(drops):
        c.shape([(x, y + 1.6), (x + 0.8, y), (x, y - 2.2), (x - 0.8, y)], CRYSTAL, seed=60 + i, tension=0.15,
                lo=(1.5, 1.5), so=(2, 2), soft=0.6, grow=1.1, inner=1.4, bias=(0.9, 1.1),
                details=[fill_svg(c.ell(x - 0.25, y + 0.3, 0.18, 0.3, 6), "#ffffff")])
    return c.save((0.0, 21.6), "the chandelier: a gold frame of two tiers of curved arms, eight candles, crystal "
                               "drops; pivot at the top ring, where the chain hooks on (it falls from there)")


def chain():
    c = R.card("chain", (-1.5, 0.0, 1.5, 8.0), grow=1.3, inner=1.6, bias=(1.0, 1.2))
    for k in range(4):   # alternating links; the card tiles vertically
        y = 1.0 + k * 2.0
        if k % 2 == 0:
            c.p.raw(stroke_svg(c.ell(0.0, y, 0.8, 1.2, 10), GOLD["line"], 6))
            c.p.raw(stroke_svg(c.ell(0.0, y, 0.8, 1.2, 10), GOLD["fill"], 3))
        else:
            c.p.raw(stroke_svg(c.line([(0.0, y - 1.2), (0.0, y + 1.2)]), GOLD["line"], 6))
            c.p.raw(stroke_svg(c.line([(0.0, y - 1.2), (0.0, y + 1.2)]), GOLD["fill"], 3))
    return c.save((0.0, 0.0), "four chain links, 8 units; stack copies up to the ceiling, pivot at the bottom link")


def shard(i, pts):
    c = R.card(f"crystal_shard_{i}", (-1.5, -1.5, 1.5, 1.5), grow=1.2, inner=1.4, bias=(0.9, 1.1))
    c.shape(pts, CRYSTAL, [fill_svg(c.ell(-0.3, 0.3, 0.2, 0.3, 6), "#ffffff")], seed=70 + i, tension=0.1,
            lo=(2, 2), so=(2, 2), soft=0.6)
    return c.save((0.0, 0.0), "a crystal shard for the crash: throw them out spinning; pivot at its middle")


def preview():
    W, H = 760, 330
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)

    def put(n, x, y_ground, model_y=0.0):
        m = R.meta[n]
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im, (round(x - m["pivot"][0]), round(y_ground - model_y * 8 - m["pivot"][1])))
    for i, f in enumerate(("fiery_hat_flames_0", "fiery_hat_flames_1")):
        x = 70 + i * 120
        put(f, x, 200, 13.0)
        put("fiery_hat", x, 200)
        d.text((x - 40, 210), f"hat + flames {i}", fill=(40, 30, 30, 255))
    for k in range(3):
        put("chain", 520, 66 - k * 64)
    m = R.meta["chandelier"]
    im = Image.open(os.path.join(OUT, "chandelier.png")).convert("RGBA")
    c.alpha_composite(im, (round(520 - m["pivot"][0]), round(66 - m["pivot"][1])))
    for i in range(3):
        put(f"crystal_shard_{i}", 300 + i * 30, 290)
    d.text((290, 300), "crash shards", fill=(40, 30, 30, 255))
    d.text((8, 310), "Chapter 4 props, 1:1 at 8 texels/unit", fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    hat()
    flames(0)
    flames(1)
    chandelier()
    chain()
    for i, pts in enumerate(([(-1.0, 1.2), (1.2, 0.4), (0.4, -1.2), (-1.0, -0.6)],
                             [(-0.6, 1.3), (1.0, 0.8), (1.1, -0.9), (-1.2, -0.2)],
                             [(0.0, 1.3), (1.2, -0.4), (-0.2, -1.2), (-1.1, 0.0)])):
        shard(i, pts)
    R.write_json()
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
