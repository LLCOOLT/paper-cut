"""Luigi's bride veil (Super Luigi ch2): an accessory over c_luigi's cap and head.

Run: python veil.py -> ../veil/*.svg, *.png, parts.json, preview.png
8 texels/unit; c_luigi faces -x. Placement is given in c_luigi model units
(bind pose: feet at y 0, cap top at y 24.3, cap spans x -6..6, head x -3.7..4.3).
"""
import json
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, smooth_path, ellipse_pts, stroke_svg, fill_svg, poly_path  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), "..", "veil")
TULLE = dict(fill="#ebe9f2", light="#fafaff", shadow="#c3bfd6", line="#3a3646")
FOLD = "#cdc9de"
LACE = "#d3cfe2"
PETAL = dict(fill="#f7f4ee", light="#ffffff", shadow="#d9cfc6", line="#45262f")
HEART = "#ef8fb2"
LEAF = dict(fill="#5daa48", light="#7cc260", shadow="#3f8530", line="#1d3812")
PEARL = dict(fill="#efe8dc", shadow="#c9bca6", line="#352a24")
meta = {}


def note(name, w, h, pivot, at, desc):
    meta[name] = {"size": [w, h], "pivot": list(pivot), "attach_model_xy": list(at), "desc": desc}


def scallops(x0, x1, y, n, depth=5, wobble=1.5):
    """Points for a scalloped hem from x0 to x1 (left to right or right to left)."""
    pts = []
    for i in range(n + 1):
        t = i / n
        x = x0 + (x1 - x0) * t
        pts.append((x, y + (wobble if i % 2 else 0)))
        if i < n:
            xm = x0 + (x1 - x0) * (t + 0.5 / n)
            pts.append((xm, y + depth))
    return pts


def lace_dots(pts_line, r=1.6):
    return [fill_svg(smooth_path(ellipse_pts(x, y, r, r, 6)), LACE) for x, y in pts_line]


def drape(name, flutter=False):
    w, h = 112, 152
    p = Part(name, w, h)
    if not flutter:
        top = [(10, 18), (20, 8), (44, 4), (70, 10), (88, 26), (100, 56), (106, 92), (108, 126)]
        hem = scallops(108, 54, 144, 6, depth=6)
        front = [(56, 118), (60, 88), (54, 58), (40, 36), (22, 28)]
        folds = ["M60 24 Q84 60 92 132", "M46 30 Q70 70 76 136", "M76 18 Q98 56 102 120"]
        hem_y = 138
    else:   # blown back while running: flatter and longer behind
        top = [(10, 18), (22, 8), (46, 4), (74, 8), (96, 16), (108, 30), (110, 58), (108, 86)]
        hem = scallops(108, 50, 100, 6, depth=6)
        front = [(52, 84), (50, 62), (40, 40), (22, 28)]
        folds = ["M54 22 Q84 36 100 84", "M42 30 Q66 52 78 94", "M70 14 Q96 24 106 60"]
        hem_y = 96
    pts = top + hem[1:] + front
    det = [stroke_svg(f, FOLD, 4) for f in folds]
    # lace dots along the hem
    det += lace_dots([(x, hem_y - 2) for x in range(58, 106, 8)])
    det.append(stroke_svg(f"M56 {hem_y - 8} L106 {hem_y - 8}", LACE, 1.5))
    p.shape(smooth_path(pts, tension=0.9, jitter=0.4, seed=7 if not flutter else 8), TULLE["fill"], TULLE["line"],
            TULLE["light"], TULLE["shadow"], (6, 6), (8, 8), details=det, soft=3, hl=(0.52, 0.22, 0.24, 0.16, 30), hl_alpha=0.7)
    note(name, w, h, (24, 14), (1.0, 24.3),
         "tulle drape, drawn BEHIND the cap and head; pivot = where it pins under the crown" +
         (" (blown back: run/fall)" if flutter else ""))
    return p


def flower(p, cx, cy, r, seed):
    pts = []
    import math
    for i in range(40):   # five round petals: r(t) dips between petals
        a = math.radians(-90 + i * 9)
        rr = r * (0.66 + 0.34 * abs(math.cos(2.5 * (a + math.pi / 2))))
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    p.shape(smooth_path(pts, tension=1.0, jitter=0.15, seed=seed), PETAL["fill"], PETAL["line"], PETAL["light"],
            PETAL["shadow"], (2, 2), (2.5, 2.5), grow=1.0, inner=1.4, bias=(0.8, 0.8), soft=1,
            details=[fill_svg(smooth_path(ellipse_pts(cx + 0.5, cy + 0.5, r * 0.3, r * 0.3, 8)), HEART)])


def leaf(p, cx, cy, l, rot, seed):
    import math
    a = math.radians(rot)
    tip = (cx + l * math.cos(a), cy + l * math.sin(a))
    n = (-math.sin(a) * l * 0.32, math.cos(a) * l * 0.32)
    mid = (cx + l * 0.5 * math.cos(a), cy + l * 0.5 * math.sin(a))
    pts = [(cx, cy), (mid[0] + n[0], mid[1] + n[1]), tip, (mid[0] - n[0], mid[1] - n[1])]
    p.shape(smooth_path(pts, tension=0.7, seed=seed), LEAF["fill"], LEAF["line"], LEAF["light"], LEAF["shadow"],
            (2, 2), (2, 2), grow=1.0, inner=1.4, bias=(0.8, 0.8), soft=1)


def band():
    w, h = 80, 40
    p = Part("veil_band", w, h)
    # a pearl band hugging the cap's crown, under a cluster of three flowers
    import math
    for i in range(9):
        t = i / 8
        x = 8 + 64 * t
        y = 32 - 11 * math.sin(math.pi * t)
        p.shape(smooth_path(ellipse_pts(x, y, 3.4, 3.4, 8)), PEARL["fill"], PEARL["line"], None, PEARL["shadow"],
                shadow_off=(1.5, 1.5), grow=0.8, inner=1.2, bias=(0.6, 0.6), soft=0.6)
    leaf(p, 30, 20, 13, 200, 1)
    leaf(p, 50, 18, 13, -20, 2)
    flower(p, 26, 18, 9, 3)
    flower(p, 54, 16, 9, 4)
    flower(p, 40, 12, 11, 5)
    note("veil_band", w, h, (40, 34), (0.6, 22.2),
         "pearl band and flowers, drawn IN FRONT of the cap; pivot = bottom centre on the crown")
    return p


def back():
    w, h = 120, 152
    p = Part("veil_back", w, h)
    top = [(20, 40), (30, 16), (60, 6), (90, 16), (100, 40), (108, 80), (112, 120)]
    hem = scallops(112, 8, 144, 10, depth=6)
    side = [(10, 120), (12, 80)]
    pts = top + hem[1:] + side
    det = [stroke_svg(f, FOLD, 5) for f in ("M60 20 Q60 80 60 136", "M40 28 Q30 80 26 134", "M80 28 Q92 80 96 134")]
    det += lace_dots([(x, 136) for x in range(14, 110, 8)])
    det.append(stroke_svg("M10 130 L110 130", LACE, 1.5))
    p.shape(smooth_path(pts, tension=0.9, jitter=0.4, seed=9), TULLE["fill"], TULLE["line"], TULLE["light"],
            TULLE["shadow"], (6, 6), (8, 8), details=det, soft=3, hl=(0.42, 0.18, 0.24, 0.12), hl_alpha=0.7)
    note("veil_back", w, h, (60, 14), (0.0, 24.3),
         "the veil from behind (back-facing poses): covers the back of cap and head; pivot = crown")
    return p


GOWN = dict(fill="#efedf3", light="#ffffff", shadow="#c9c4d8", line="#3a3646")
SASH = dict(fill="#f2a9c4", light="#fbd2e1", shadow="#cf7c9c", line="#4a1a2c")


def gown(swish=False):
    """The bride's gown (c_luigi_bride): a full white wedding skirt pinned at the waist (y 10), its hem at
    y 1.5 so the shoes show, wide enough for both legs in a stride. gown_side_swish: the same, the hem
    swung back (+x) for walking and running. Two ruffled tiers, a lace hem like the veil's, a pink sash
    with the bow at the back."""
    w, h = 160, 80
    back = False
    name = "gown_side_swish" if swish else "gown_side"
    p = Part(name, w, h)
    # drawn in a design space (waist y 10.4, hem y 2.2, x -7.4..8.4) mapped onto the model:
    # waist y 10.0, hem y 1.5, a little wider; the card spans model x -9..11, y 1..11
    Yf = lambda y: 1.5 + (y - 2.2) * 1.0366
    Xf = lambda x, y: 0.5 + (x - 0.5) * 1.12 + (1.9 * (max(0.0, 10.0 - Yf(y)) / 8.5) ** 1.4 if swish else 0.0)
    u = lambda x, y: ((Xf(x, y) + 9) * 8, (11 - Yf(y)) * 8)
    top = [u(-3.4, 10.4), u(0.5, 10.6), u(4.4, 10.4)]
    right = [u(6.0, 7.0), u(7.6, 4.2), u(8.4, 2.4)]
    hem = []
    for i in range(19):   # scalloped hem, right to left
        x = 8.4 - 15.8 * i / 18
        hem.append(u(x, 2.2 + (0.0 if i % 2 == 0 else -0.6)))
    left = [u(-6.6, 4.4), u(-5.0, 7.2)]
    pts = top + right + hem + left

    def curve(pts_d, colour, width):
        a, b, c3 = (u(*q) for q in pts_d)
        return stroke_svg(f"M{a[0]} {a[1]} Q{b[0]} {b[1]} {c3[0]} {c3[1]}", colour, width)

    det = [curve([(-5.8, 5.2), (0.5, 4.2), (7.0, 5.2)], GOWN["shadow"], 4),
           curve([(-5.8, 5.6), (0.5, 4.6), (7.0, 5.6)], LACE, 1.6)]
    for fx in (-1.6, 1.8, 4.8):   # folds falling from the waist
        det.append(curve([(fx * 0.6, 9.4), (fx, 6.0), (fx * 1.25, 2.8)], FOLD, 3.5))
    det += lace_dots([u(-6.4 + k * 0.92, 2.75) for k in range(16)])
    p.shape(smooth_path(pts, tension=0.8, jitter=0.4, seed=91 if not swish else 92), GOWN["fill"], GOWN["line"],
            GOWN["light"], GOWN["shadow"], (6, 6), (8, 8), details=det, soft=3, hl=(0.40, 0.26, 0.27, 0.17),
            hl_alpha=0.7)
    # the sash at the waist
    sash = [u(-3.6, 10.6), u(0.5, 10.9), u(4.6, 10.6), u(4.8, 9.2), u(0.5, 9.0), u(-3.8, 9.2)]
    p.shape(smooth_path(sash, tension=0.4, jitter=0.3, seed=93), SASH["fill"], SASH["line"], SASH["light"],
            SASH["shadow"], (2, 2), (3, 3), soft=1.2, grow=1.2, inner=1.6, bias=(1, 1))
    # the bow: at the back of the waist from the side, in the middle from behind
    bx, by = u(5.4, 9.9) if not back else u(0.5, 9.9)
    for sgn in (-1, 1):
        loop = [(bx, by), (bx + sgn * 10, by - 8), (bx + sgn * 14, by - 1), (bx + sgn * 10, by + 6)]
        p.shape(smooth_path(loop, tension=0.7, seed=94 + sgn), SASH["fill"], SASH["line"], SASH["light"],
                SASH["shadow"], (2, 2), (2, 2), soft=1, grow=1.0, inner=1.4, bias=(0.8, 0.8))
        tail = [(bx - 2 * sgn, by + 2), (bx + sgn * 6, by + 18), (bx + sgn * 1, by + 16)]
        p.shape(poly_path(tail), SASH["fill"], SASH["line"], None, SASH["shadow"], shadow_off=(1.5, 1.5),
                soft=0.8, grow=1.0, inner=1.4, bias=(0.8, 0.8))
    p.shape(smooth_path(ellipse_pts(bx, by, 4, 4, 8)), SASH["fill"], SASH["line"], SASH["light"], None,
            light_off=(1.5, 1.5), grow=1.0, inner=1.4, bias=(0.8, 0.8))
    note(name, w, h, (round(u(0.5, 9.85)[0]), round(u(0.5, 9.85)[1])), (0.5, 10.0),
         ("the skirt with the hem swung back, for walking and running" if swish else "the skirt, standing") +
         ": pinned at c_luigi's waist (0.5, 10), in front of the body and legs, behind the front glove; "
         "hem at y 1.5, the shoes show below it")
    return p


def mannequin(d, ox, oy):
    """A neutral stand-in for Luigi's head and cap (the real textures stay off the repository)."""
    u = lambda x, y: (ox + x * 8, oy - y * 8)
    d.ellipse([*u(-3.7, 20.8), *u(4.3, 9.8)], fill=(214, 206, 196, 255), outline=(120, 110, 100, 255), width=3)
    d.chord([*u(-5.0, 25.0), *u(6.0, 15.5)], 180, 360, fill=(170, 190, 168, 255), outline=(90, 110, 88, 255), width=3)
    d.polygon([u(-6.0, 19.6), u(-1.0, 20.4), u(-1.0, 19.0), u(-6.0, 18.4)], fill=(170, 190, 168, 255),
              outline=(90, 110, 88, 255))
    d.ellipse([*u(-3.7, 12.6), *u(4.3, 3.6)], fill=(186, 192, 210, 255), outline=(110, 116, 130, 255), width=3)


def paste_at(c, name, model_xy, ox, oy):
    im = Image.open(os.path.join(OUT, name + ".png")).convert("RGBA")
    px, py = meta[name]["pivot"]
    x, y = ox + model_xy[0] * 8 - px, oy - model_xy[1] * 8 - py
    c.alpha_composite(im, (round(x), round(y)))


def preview(luigi_under=None, path=None):
    W, H = 900, 520
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    d.text((8, 6), "Luigi the bride: veil and gown cards 1:1 (8 texels/unit); below, worn"
           + (" (grey stand-in for c_luigi)" if luigi_under is None else ""), fill=(40, 30, 30, 255))
    x = 8
    for n in ("veil_side", "veil_side_flutter", "veil_band", "veil_back", "gown_side", "gown_side_swish"):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        d.rectangle([x, 22, x + im.width + 8, 22 + 14], fill=(225, 218, 205, 255))
        d.text((x + 3, 24), n, fill=(40, 30, 30, 255))
        c.alpha_composite(im, (x + 4, 40))
        x += im.width + 16
    # worn: side (stand + side drape behind, band in front), flutter, back
    for i, (drape_name, label) in enumerate((("veil_side", "worn, standing"), ("veil_side_flutter", "worn, running"),
                                             ("veil_back", "from behind"))):
        ox, oy = 120 + i * 240, 490
        base = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        dd = ImageDraw.Draw(base)
        if drape_name != "veil_back":
            paste_at(c, drape_name, meta[drape_name]["attach_model_xy"], ox, oy)
            if luigi_under is not None:
                c.alpha_composite(luigi_under[0], (ox - luigi_under[1][0], oy - luigi_under[1][1]))
            else:
                mannequin(dd, ox, oy); c.alpha_composite(base)
            g = "gown_side" if drape_name == "veil_side" else "gown_side_swish"
            paste_at(c, g, meta[g]["attach_model_xy"], ox, oy)
            paste_at(c, "veil_band", meta["veil_band"]["attach_model_xy"], ox, oy)
        else:
            if luigi_under is not None:
                c.alpha_composite(luigi_under[0], (ox - luigi_under[1][0], oy - luigi_under[1][1]))
            else:
                mannequin(dd, ox, oy); c.alpha_composite(base)
            paste_at(c, "veil_back", meta["veil_back"]["attach_model_xy"], ox, oy)
        d.text((ox - 40, oy + 8), label, fill=(40, 30, 30, 255))
    return c


def build():
    cards = [drape("veil_side"), drape("veil_side_flutter", flutter=True), band(), back(), gown(), gown(swish=True)]
    for cd in cards:
        cd.save(OUT)
    with open(os.path.join(OUT, "parts.json"), "w", encoding="utf-8") as f:
        json.dump({"for": "c_luigi", "texels_per_unit": 8, "faces": "-x",
                   "pivot_note": "pixels from the PNG's top-left; attach_model_xy in c_luigi bind-pose units",
                   "cards": meta}, f, indent=1)
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print("veil:", ", ".join(meta))
