"""The Gallery of Murals (Super Luigi ch5, hei_52 on hei_09): six panels telling the Luffs' story.

Run: python murals.py -> ../murals/*.svg, *.png, preview.png
ch5.md, beat 8 and New assets 7: "texture swaps over hei_09's walls, six panels, flat paper-cut style,
or six signboard props". hei_09's wall texture (decoded in scratch) is a tiling 256x128 grey stone
brick, so a straight swap would put one mural on every wall; these are 256x128 panels in that
wall's size and stone palette, for six panel props (or six wall faces given their own material).
Style: STYLE.md R10 (map textures): opaque, flat tones with hard edges, no outlines, a dark drop line
under each shape; the pictures are paper cut-outs pasted on plaster, in a carved stone frame.
The sixth is blank but for a small drawing of a boy breaking a circle into seven pieces; it is
cracked, and Jerry's Blast breaks it open (mural_6_broken, with a hole cut through).
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, smooth_path, poly_path, ellipse_pts, star_pts, stroke_svg, fill_svg, harden, clean  # noqa
import lettering as lt  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "murals")
W, H = 256, 128
PLASTER, PLASTER_D = "#e2dccc", "#c8c0ac"
FRAME, FRAME_D, FRAME_L = "#8e8a84", "#625e58", "#a8a49c"
DROP = "#9a9282"
ROBE, ROBE_D = "#f4f0e6", "#c8c2b2"
SKIN = "#ecc8a4"
HAIR = "#c4583a"
SKY = "#8ab4d8"
GOLD, GOLD_D = "#d8a83a", "#9a7220"
DARK = "#3a3a52"
STORM = "#6a6a82"
GREEN = "#7aa45a"


def shp(p, pts, colour, drop=True, tension=0.45, seed=1, smooth=True):
    d = smooth_path(pts, tension=tension, jitter=0.25, seed=seed) if smooth else poly_path(pts)
    if drop:
        p.raw(f'<path d="{d}" fill="{DROP}" transform="translate(0.8 1.8)"/>')
    p.raw(fill_svg(d, colour))


def base(name):
    p = Part(name, W, H)
    p.raw(f'<rect width="{W}" height="{H}" fill="{FRAME}"/>')
    p.raw(f'<rect x="4" y="4" width="{W - 8}" height="{H - 8}" fill="{FRAME_D}"/>')
    p.raw(f'<rect x="3" y="3" width="{W - 9}" height="{H - 9}" fill="{FRAME_L}"/>')
    p.raw(f'<rect x="10" y="10" width="{W - 20}" height="{H - 20}" fill="{PLASTER_D}"/>')
    p.raw(f'<rect x="10" y="10" width="{W - 22}" height="{H - 22}" fill="{PLASTER}"/>')
    for x in (4, W - 8):   # carved Luff swirls on the frame's corners
        for y in (4, H - 8):
            p.raw(stroke_svg(smooth_path(ellipse_pts(x + 2, y + 2, 1.6, 1.6, 6)), FRAME_D, 1.2))
    return p


def luff(p, x, y, s=1.0, arms="down", lying=False, crown=False, seed=0):
    """a Luff in white robes with reddish hair, like Cranberry: x, y is the robe's hem centre"""
    if lying:
        shp(p, [(x - 14 * s, y), (x + 8 * s, y - 6 * s), (x + 10 * s, y), ], ROBE, seed=seed, smooth=False)
        shp(p, ellipse_pts(x - 16 * s, y - 3 * s, 4.5 * s, 4.5 * s, 10), SKIN, seed=seed)
        shp(p, ellipse_pts(x - 17.5 * s, y - 4.5 * s, 3.5 * s, 3 * s, 8), HAIR, drop=False, seed=seed)
        return
    shp(p, [(x - 8 * s, y), (x - 3 * s, y - 16 * s), (x + 3 * s, y - 16 * s), (x + 8 * s, y)], ROBE, seed=seed,
        smooth=False)
    if arms == "up":
        for sx in (-1, 1):
            p.raw(stroke_svg(f"M{x + sx * 3 * s} {y - 13 * s} L{x + sx * 9 * s} {y - 22 * s}", ROBE_D, 3 * s))
    elif arms == "out":
        p.raw(stroke_svg(f"M{x - 3 * s} {y - 12 * s} L{x - 11 * s} {y - 13 * s}", ROBE_D, 3 * s))
    shp(p, ellipse_pts(x, y - 20 * s, 5 * s, 5 * s, 10), SKIN, seed=seed + 1)
    shp(p, [(x - 5.5 * s, y - 20 * s), (x - 4 * s, y - 25 * s), (x + 2 * s, y - 26.5 * s), (x + 5.5 * s, y - 22 * s),
            (x + 4 * s, y - 20.5 * s), (x, y - 22.5 * s)], HAIR, drop=False, seed=seed + 2)
    if crown:
        shp(p, [(x - 4 * s, y - 25 * s), (x - 4 * s, y - 29 * s), (x - 1.5 * s, y - 27 * s), (x, y - 30 * s),
                (x + 1.5 * s, y - 27 * s), (x + 4 * s, y - 29 * s), (x + 4 * s, y - 25 * s)], GOLD, seed=seed,
            smooth=False)


def compass(p, x, y, r, cracked=False, seed=0):
    shp(p, ellipse_pts(x, y, r, r, 16), GOLD, seed=seed)
    p.raw(fill_svg(smooth_path(ellipse_pts(x, y, r * 0.72, r * 0.72, 14)), "#f6ecc8"))
    p.raw(fill_svg(poly_path([(x - r * 0.12, y), (x + r * 0.5, y - r * 0.5), (x + r * 0.12, y)]), "#c83a3a"))
    p.raw(fill_svg(poly_path([(x - r * 0.12, y), (x - r * 0.5, y + r * 0.5), (x + r * 0.12, y)]), DARK))
    if cracked:
        p.raw(stroke_svg(f"M{x - r * 0.2} {y - r} L{x + r * 0.1} {y - r * 0.3} L{x - r * 0.25} {y + r * 0.2} "
                         f"L{x + r * 0.2} {y + r}", "#2a2420", 2))


def city(p, x0, y, n=4, s=1.0, dark=False):
    wall, dome = ("#f2f0ea", "#dcd8ce") if not dark else ("#9a96a8", "#7a7690")
    for k in range(n):
        x = x0 + k * 22 * s
        h = (18 + 8 * (k % 2)) * s
        shp(p, [(x, y), (x, y - h), (x + 16 * s, y - h), (x + 16 * s, y)], wall, seed=k, smooth=False)
        shp(p, [(x + 1 * s, y - h), (x + 3 * s, y - h - 6 * s), (x + 8 * s, y - h - 9 * s), (x + 13 * s, y - h - 6 * s),
                (x + 15 * s, y - h)], dome, seed=k + 10)
        p.raw(f'<rect x="{x + 6 * s}" y="{y - 8 * s}" width="{4 * s}" height="{8 * s}" fill="{DARK}"/>')


def mural1():
    p = base("mural_1")
    p.raw(f'<rect x="10" y="10" width="{W - 22}" height="60" fill="{SKY}"/>')
    shp(p, star_pts(210, 32, 14, 9, n=10), GOLD, seed=1)
    shp(p, ellipse_pts(210, 32, 8, 8, 12), "#f2d270", drop=False)
    city(p, 24, 74, 5)
    shp(p, [(10, 108), (10, 78), (W - 12, 78), (W - 12, 108)], GREEN, drop=False, smooth=False)
    for i, x in enumerate((60, 96, 132, 168)):
        luff(p, x, 106, 1.0, arms="up" if i % 2 else "out", seed=10 * i)
    for x, y in ((80, 86), (150, 84)):   # music notes: the city was happy
        p.raw(fill_svg(smooth_path(ellipse_pts(x, y, 2.5, 2, 8)), DARK))
        p.raw(stroke_svg(f"M{x + 2.2} {y} L{x + 2.2} {y - 9} L{x + 6} {y - 7}", DARK, 1.4))
    p.save(OUT)


def mural2():
    p = base("mural_2")
    p.defs.append(f'<clipPath id="plaster"><rect x="10" y="10" width="{W - 22}" height="{H - 22}"/></clipPath>')
    for k in range(9):   # rays from the compass toward the right: toward tomorrow
        a = math.radians(-40 + k * 10)
        p.raw(f'<path d="{poly_path([(100, 64), (100 + 160 * math.cos(a - 0.04), 64 + 160 * math.sin(a - 0.04)), (100 + 160 * math.cos(a + 0.04), 64 + 160 * math.sin(a + 0.04))])}" fill="#ecdcb0" clip-path="url(#plaster)"/>')
    luff(p, 66, 108, 1.4, arms="out", seed=3)
    p.raw(stroke_svg("M60 88 Q52 70 58 50", "#8a6a3a", 3))   # the wise one's staff
    compass(p, 100, 64, 16, seed=4)
    shp(p, [(196, 72), (216, 52), (236, 72)], GOLD, seed=5, smooth=False)   # a rising sun at the horizon
    p.raw(f'<rect x="186" y="72" width="60" height="4" fill="{GOLD_D}"/>')
    p.save(OUT)


def mural3():
    p = base("mural_3")
    for x0 in (12, W - 44):   # the sea at both ends
        for k in range(3):
            p.raw(stroke_svg(f"M{x0} {72 + k * 10} q8 -6 16 0 t16 0", "#5a8ac8", 3))
    shp(p, ellipse_pts(200, 30, 26, 11, 12, wobble=0.15, seed=7), STORM, seed=7)   # a storm, held back
    p.raw(stroke_svg("M196 40 L188 54 L196 54 L188 70", "#f2d270", 3))
    p.raw(fill_svg(poly_path([(150, 24), (166, 34), (150, 44)]), DARK))   # a halting hand, an arrow back
    luff(p, 128, 106, 1.5, arms="up", crown=True, seed=8)
    compass(p, 128, 46, 10, seed=9)
    for x in (88, 168):
        luff(p, x, 106, 1.0, arms="out", seed=x)
    p.save(OUT)


def mural4():
    p = base("mural_4")
    rnd = random.Random(4)
    for k in range(14):   # a sky full of little tomorrows (suns and moons)
        x, y = rnd.uniform(110, 236), rnd.uniform(16, 60)
        p.raw(fill_svg(smooth_path(ellipse_pts(x, y, 3, 3, 8)), GOLD if k % 3 else "#b4b8d0"))
    for i, x in enumerate((50, 84)):   # Luffs sitting, staring through spyglasses
        luff(p, x, 106, 1.1, seed=20 + i)
        p.raw(stroke_svg(f"M{x + 2} {106 - 22} L{x + 20} {106 - 34}", "#8a6a3a", 4))
    shp(p, [(120, 108), (130, 92), (146, 92), (156, 108)], "#b8a070", seed=21)   # a dropped lute
    p.raw(stroke_svg("M138 92 L152 72", "#8a6a3a", 3))
    for x in range(170, 236, 12):   # bare, unplanted rows
        p.raw(stroke_svg(f"M{x} 108 L{x + 6} 96", "#a8987a", 2.5))
    p.save(OUT)


def mural5():
    p = base("mural_5")
    p.raw(f'<rect x="10" y="10" width="{W - 22}" height="64" fill="#5a5a78"/>')   # the city gone dark
    city(p, 120, 76, 5, dark=True)
    compass(p, 60, 42, 18, cracked=True, seed=11)
    shp(p, [(10, 108), (10, 78), (W - 12, 78), (W - 12, 108)], "#a8a090", drop=False, smooth=False)
    for i, x in enumerate((92, 140, 188)):   # asleep, not waking
        luff(p, x, 104, 1.0, lying=True, seed=30 + i)
        p.raw(lt.text_svg("z", x - 3, 88, 9, DARK, seed=i))
    p.save(OUT)


def mural6(state):
    name = {0: "mural_6", 1: "mural_6_broken"}[state]
    p = base(name)
    # the blank panel, with only a small drawing low in the corner: a boy breaking a circle into seven
    x, y = 206, 104
    luff(p, x - 10, y, 0.8, arms="out", seed=40)
    cx, cy, r = x + 14, y - 16, 8
    for k in range(7):
        a0, a1 = 2 * math.pi * k / 7, 2 * math.pi * (k + 1) / 7
        ox, oy = 1.6 * math.cos((a0 + a1) / 2), 1.6 * math.sin((a0 + a1) / 2)
        p.raw(fill_svg(poly_path([(cx + ox, cy + oy), (cx + ox + r * math.cos(a0), cy + oy + r * math.sin(a0)),
                                  (cx + ox + r * math.cos(a1), cy + oy + r * math.sin(a1))]), GOLD))
    # the crack, across the blank plaster (Blast it)
    crack = "M40 12 L62 40 L54 58 L84 78 L76 96 L98 116"
    p.raw(stroke_svg(crack, "#f6f2e8", 4))
    p.raw(stroke_svg(crack, "#4a443a", 2.4))
    p.raw(stroke_svg("M62 40 L84 34 M84 78 L104 72", "#4a443a", 1.6))
    if state == 1:   # broken open: a hole knocked through, with jagged plaster round it
        hole = [(30, 116), (34, 82), (24, 58), (42, 30), (64, 22), (92, 30), (108, 54), (100, 82), (112, 116)]
        p.raw(fill_svg(smooth_path([(x + 3, y + 3) for x, y in hole], tension=0.1), PLASTER_D))
        p.raw(f'<path d="{smooth_path(hole, tension=0.1)}" fill="#000000" class="hole"/>')
    p.save(OUT)
    if state == 1:   # cut the hole out of the texture: transparent there (1-bit, for the door)
        im = Image.open(os.path.join(OUT, name + ".png")).convert("RGBA")
        px = im.load()
        for yy in range(im.height):
            for xx in range(im.width):
                if px[xx, yy][:3] == (0, 0, 0):
                    px[xx, yy] = (0, 0, 0, 0)
        harden(im).save(os.path.join(OUT, name + ".png"))


def preview():
    c = Image.new("RGBA", (540, 584), (60, 56, 64, 255))
    d = ImageDraw.Draw(c)
    names = ["mural_1", "mural_2", "mural_3", "mural_4", "mural_5", "mural_6", "mural_6_broken"]
    for i, n in enumerate(names):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        x, y = 8 + (i % 2) * 266, 22 + (i // 2) * 140
        c.alpha_composite(im, (x, y))
        d.text((x, y - 13), n, fill=(255, 255, 255, 255))
    return c


def build():
    clean(OUT)
    mural1()
    mural2()
    mural3()
    mural4()
    mural5()
    mural6(0)
    mural6(1)
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
