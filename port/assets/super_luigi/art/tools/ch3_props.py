"""Chapter 3's small props (Super Luigi): the King's crown (Revvo's, then Luigi's), Lakky's
microphone and the start lights that hang from his fishing rod.

Run: python ch3_props.py -> ../ch3_props/*.svg, *.png, parts.json, preview.png
ch3.md, New assets 5-6: "Revvo's crown (prop on the Koopatrol's head) and Luigi's crown (the same
crown, on Luigi's cap)"; "Lakky's microphone and start lights (props for the Lakitu model): the
Lakitu's fishing rod can hold the light bar". 8 texels/unit, outlined, top-left light.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, stroke_svg, fill_svg, smooth_path, ellipse_pts, star_pts, poly_path, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "ch3_props")
R = Rig("sl_ch3_props", OUT)

GOLD = dict(fill="#ecba3c", light="#fadc7c", shadow="#b2821c", line="#4a3006")
RUBY = dict(fill="#d8303e", light="#f27280", shadow="#9a1424", line="#40060e")
SAPPH = dict(fill="#3a6ad8", light="#78a2f0", shadow="#2444a0", line="#0c1a48")
PEARL = dict(fill="#f2ece0", light="#ffffff", shadow="#c8bea8", line="#3a3226")
BLACK = dict(fill="#3a3a44", light="#5a5a66", shadow="#24242c", line="#0e0e14")
METAL = dict(fill="#b8bac6", light="#dcdee6", shadow="#8a8c9c", line="#262834")
LAMP_OFF = dict(fill="#5a4a4c", light="#74646a", shadow="#3e3034", line="#140a0c")
LAMP_RED = dict(fill="#f2343c", light="#ff8a8a", shadow="#c01822", line="#3e0408")
LAMP_GREEN = dict(fill="#3ad24a", light="#9af08a", shadow="#1e9a2c", line="#0a3a10")


def crown():
    c = R.card("crown", (-4.0, -1.0, 4.0, 5.0))
    pts = [(-3.2, 0.0), (3.2, 0.0), (3.4, 1.6), (3.5, 4.0), (2.4, 2.6), (1.4, 4.4), (0.0, 2.8), (-1.4, 4.4),
           (-2.4, 2.6), (-3.5, 4.0), (-3.4, 1.6)]
    det = []
    # a checkered band round the base: it's a racing crown
    for k in range(10):
        x = -3.2 + k * 0.64
        for r in range(2):
            if (k + r) % 2 == 0:
                det.append(fill_svg(poly_path(c.pts([(x, 0.2 + r * 0.55), (x + 0.64, 0.2 + r * 0.55),
                                                    (x + 0.64, 0.75 + r * 0.55), (x, 0.75 + r * 0.55)])),
                                    "#2a2418"))
    det.append(stroke_svg(c.line([(-3.3, 1.35), (3.3, 1.35)]), GOLD["shadow"], 1.6))
    c.shape(pts, GOLD, det, hl=(0.4, 0.42, 0.3, 0.16), seed=1, tension=0.15, jitter=0.25, lo=(3, 3), so=(4, 4),
            soft=1.5)
    for x, y in ((-3.5, 4.0), (-1.4, 4.4), (1.4, 4.4), (3.5, 4.0)):   # pearl tips
        c.shape(None, PEARL, d=c.ell(x, y + 0.1, 0.45, 0.45, 8), seed=2, lo=(1, 1), so=(1.5, 1.5), soft=0.6,
                grow=1.2, inner=1.4, bias=(0.9, 1.1))
    c.shape(None, RUBY, d=c.ell(0.0, 2.1, 0.7, 0.62, 10), seed=3, lo=(1.5, 1.5), so=(2, 2), soft=0.8,
            grow=1.2, inner=1.5, bias=(0.9, 1.1), details=[fill_svg(c.ell(-0.22, 2.35, 0.14, 0.14, 6), "#ffffff")])
    for x in (-2.2, 2.2):
        c.shape(None, SAPPH, d=c.ell(x, 1.95, 0.45, 0.42, 8), seed=4, lo=(1, 1), so=(1.5, 1.5), soft=0.6,
                grow=1.1, inner=1.4, bias=(0.9, 1.1))
    return c.save((0.0, 0.0), "the King's crown: five gold points with pearl tips, a ruby and two sapphires, a "
                              "checkered band; pivot at the middle of its base (sit it on the head or cap)")


def mic():
    c = R.card("microphone", (-2.0, -5.0, 2.0, 3.0))
    handle = [(-0.55, 0.6), (0.55, 0.6), (0.4, -3.8), (0.0, -4.2), (-0.4, -3.8)]
    c.shape(handle, BLACK, [stroke_svg(c.line([(-0.5, -0.4), (0.5, -0.4)]), METAL["fill"], 2.2)],
            hl=(0.35, 0.3, 0.3, 0.25), seed=5, tension=0.3, lo=(2, 2), so=(3, 3), soft=1)
    head = ellipse_pts(*c.u(0.0, 1.6), 1.25 * 8, 1.3 * 8, 14, wobble=0.02, seed=6)
    d = smooth_path(head, tension=0.5, jitter=0.3, seed=6)
    grille = []
    for k in range(-3, 4):
        grille.append(stroke_svg(c.line([(k * 0.32, 0.2), (k * 0.32, 3.0)]), METAL["shadow"], 1.3))
        grille.append(stroke_svg(c.line([(-1.4, 1.6 + k * 0.32), (1.4, 1.6 + k * 0.32)]), METAL["shadow"], 1.3))
    c.p.shape(d, METAL["fill"], METAL["line"], METAL["light"], METAL["shadow"], (3, 3), (4, 4), soft=1.2,
              details=grille, hl=(0.36, 0.28, 0.3, 0.2))
    c.p.raw(stroke_svg(c.curve((0.0, -4.1), (0.6, -4.9), (1.6, -4.6)), "#1a1a20", 3))   # the cord's start
    return c.save((0.0, -1.6), "Lakky's microphone: a round metal grille on a black handle; pivot at the grip")


def lights(name, lit):
    c = R.card(name, (-5.0, -2.0, 5.0, 3.0))
    bar = [(-4.4, -1.4), (4.4, -1.4), (4.6, 1.4), (-4.6, 1.4)]
    c.shape(bar, BLACK, [stroke_svg(c.line([(-4.2, 1.0), (4.2, 1.0)]), BLACK["light"], 1.4)], seed=7, tension=0.15,
            jitter=0.25, lo=(3, 3), so=(4, 4), soft=1.2)
    for x in (-2.6, 2.6):   # the strings' hooks on top
        c.p.raw(stroke_svg(c.ell(x, 2.1, 0.5, 0.55, 8), BLACK["line"], 2.2))
    for i, x in enumerate((-2.9, 0.0, 2.9)):
        tone = LAMP_GREEN if lit == "go" else (LAMP_RED if i < lit else LAMP_OFF)
        c.shape(None, tone, d=c.ell(x, 0.0, 1.15, 1.1, 12), seed=8 + i, lo=(2, 2), so=(3, 3), soft=1,
                grow=1.3, inner=1.6, bias=(1, 1.2), hl=(0.38, 0.3, 0.3, 0.22) if tone is not LAMP_OFF else None,
                hl_alpha=0.35)
    desc = {0: "all off", 1: "one red", 2: "two red", 3: "three red", "go": "all green: GO"}[lit]
    return c.save((0.0, 2.1), f"the start lights, {desc}; pivot between the hooks, where the rod's line ties on")


PIECE = dict(fill="#e8bc44", light="#fbe496", shadow="#ac801c", line="#4a3006")   # cranberry.py's piece
TARP = dict(fill="#78a652", light="#a2c87a", shadow="#4e7a32", line="#1e3614")
STEEL = dict(fill="#9aa6b4", light="#c4ccd6", shadow="#6e7a8a", line="#1e2632")
STRIPE = dict(fill="#e8c23a", light="#f6dc78", shadow="#b08a18", line="#3e2c06")
BOSS = dict(fill="#3aa64a", light="#7ad27a", shadow="#24782e", line="#0e3014")


def trophy():
    """the Grand Prix's trophy: a gold cup on a black plinth with a checkered band, its lid flat on top
    (the compass piece sits on it: trophy_piece)"""
    c = R.card("trophy", (-5.0, -1.0, 5.0, 15.0))
    plinth = [(-3.6, 0.0), (3.6, 0.0), (3.3, 3.0), (-3.3, 3.0)]
    det = []
    for k in range(9):   # the checkered band
        x = -3.15 + k * 0.7
        for r in range(2):
            if (k + r) % 2 == 0:
                det.append(fill_svg(poly_path(c.pts([(x, 1.0 + r * 0.6), (x + 0.7, 1.0 + r * 0.6),
                                                    (x + 0.7, 1.6 + r * 0.6), (x, 1.6 + r * 0.6)])), "#e8e2d2"))
    c.shape(plinth, BLACK, det, seed=21, tension=0.1, jitter=0.2, lo=(3, 3), so=(4, 4), soft=1)
    for side in (-1, 1):   # the handles, behind the bowl
        c.p.raw(stroke_svg(c.curve((side * 2.9, 11.6), (side * 4.9, 11.2), (side * 2.6, 8.0)), GOLD["line"], 7.5))
        c.p.raw(stroke_svg(c.curve((side * 2.9, 11.6), (side * 4.9, 11.2), (side * 2.6, 8.0)), GOLD["fill"], 3.6))
    cup = [(-0.9, 3.0), (0.9, 3.0), (0.7, 4.6), (1.4, 6.0), (2.9, 8.2), (3.6, 12.2), (3.8, 12.8), (-3.8, 12.8),
           (-3.6, 12.2), (-2.9, 8.2), (-1.4, 6.0), (-0.7, 4.6)]
    det = [stroke_svg(c.line([(-3.4, 11.7), (3.4, 11.7)]), GOLD["shadow"], 1.6),
           stroke_svg(c.line([(-1.6, 6.6), (1.6, 6.6)]), GOLD["shadow"], 1.4)]
    star = star_pts(*c.u(0.0, 9.4), 1.3 * 8, 0.55 * 8, 5)
    det.append(fill_svg(poly_path(star), GOLD["light"]))
    det.append(stroke_svg(poly_path(star), GOLD["line"], 1.4))
    c.shape(cup, GOLD, det, hl=(0.36, 0.4, 0.26, 0.2), seed=22, tension=0.25, jitter=0.2, lo=(3, 3), so=(4, 4),
            soft=1.5)
    lid = [(-3.9, 12.8), (3.9, 12.8), (3.4, 13.6), (-3.4, 13.6)]
    c.shape(lid, GOLD, (), seed=23, tension=0.2, jitter=0.15, lo=(2, 2), so=(3, 3), soft=1)
    return c.save((0.0, 0.0), "the Grand Prix's trophy: a gold cup with a star on a black checkered plinth, its "
                              "lid flat at y 13.6; pivot on the ground at its middle")


def trophy_piece():
    """the compass piece on the trophy's lid (cranberry.py's drawing, smaller), a card of its own to take off"""
    c = R.card("trophy_piece", (-2.0, -1.0, 2.0, 3.0), grow=1.3, inner=1.6, bias=(1.1, 1.3))
    arc = [(1.7 * math.cos(math.radians(a)), -0.2 + 1.7 * math.sin(math.radians(a))) for a in range(25, 156, 13)]
    arc += [(0.75 * math.cos(math.radians(a)), -0.2 + 0.75 * math.sin(math.radians(a))) for a in range(155, 24, -26)]
    gem = c.d([(0.0, 1.35), (0.3, 0.95), (0.0, 0.55), (-0.3, 0.95)], 0.2, 0.05)
    c.shape(arc, PIECE, [fill_svg(gem, "#7ad8f8"), stroke_svg(gem, PIECE["line"], 1.3)],
            hl=(0.38, 0.25, 0.26, 0.18), seed=24, tension=0.4, lo=(2, 2), so=(3, 3))
    return c.save((0.0, 0.0), "the compass piece standing on the trophy's lid (pivot at its foot: at (0, 13.6) "
                              "on the trophy)")


def wheel():
    """Big Green 01's stolen steering wheel: a black rim, three steel spokes, a green boss with the 01"""
    c = R.card("wheel", (-3.0, -3.0, 3.0, 3.0), grow=1.4, inner=1.8, bias=(1.2, 1.4))
    ends = ((-1.9, 0.4), (1.9, 0.4), (0.0, -1.95))
    c.p.raw("".join(stroke_svg(c.line([(0.0, 0.0), e]), STEEL["line"], 6.0) for e in ends))
    c.p.raw("".join(stroke_svg(c.line([(0.0, 0.0), e]), STEEL["fill"], 2.6) for e in ends))
    rim = ellipse_pts(*c.u(0.0, 0.0), 2.6 * 8, 2.6 * 8, 18, wobble=0.02, seed=31)
    hole = ellipse_pts(*c.u(0.0, 0.0), 1.85 * 8, 1.85 * 8, 16, wobble=0.02, seed=32)
    d = smooth_path(rim, tension=0.5) + " " + smooth_path(list(reversed(hole)), tension=0.5)
    c.p.shape(d, BLACK["fill"], BLACK["line"], BLACK["light"], BLACK["shadow"], (2, 2), (3, 3), soft=1,
              hl=(0.38, 0.3, 0.3, 0.2))
    c.shape(None, BOSS, d=c.ell(0.0, 0.0, 0.8, 0.8, 10), seed=33, lo=(1, 1), so=(2, 2), soft=0.6, grow=1.2,
            inner=1.4, bias=(0.9, 1.1), details=[stroke_svg(c.line([(-0.35, -0.3), (-0.35, 0.3)]), "#f2ece0", 1.6),
                                                 stroke_svg(c.ell(0.25, 0.0, 0.18, 0.3, 8), "#f2ece0", 1.6)])
    return c.save((0.0, 0.0), "Big Green 01's steering wheel, stolen: black rim, steel spokes, a green 01 boss; "
                              "pivot at its middle")


def tarp():
    """the green tarp over the Girl in Torque's garage: a canvas sheet over a kart's shape, its hem hanging
    above the ground so the wheels stick out"""
    c = R.card("tarp", (-19.0, 2.0, 19.0, 14.0))
    top = [(-17.5, 3.6), (-17.8, 5.8), (-15.0, 7.4), (-9.0, 8.0), (-4.0, 11.6), (1.0, 12.8), (5.0, 12.2),
           (9.0, 10.2), (14.0, 9.6), (17.4, 8.6), (18.0, 5.6), (17.6, 3.4)]
    hem = [(14.0, 4.2), (10.5, 3.2), (7.0, 4.4), (3.0, 3.4), (-1.0, 4.4), (-5.0, 3.3), (-9.0, 4.3), (-13.0, 3.2)]
    det = []
    for a, b, cc in (((-3.0, 11.0), (-4.5, 7.0), (-6.0, 4.0)), ((5.0, 12.0), (5.5, 8.0), (3.2, 3.6)),
                     ((11.0, 9.8), (12.0, 7.0), (10.4, 3.4)), ((-12.0, 7.6), (-11.0, 6.0), (-12.8, 3.5))):
        det.append(stroke_svg(c.curve(a, b, cc), TARP["shadow"], 2.4))   # the folds
    for x in (-15.5, -6.0, 6.5, 15.5):   # the tie-down grommets along the hem
        det.append(fill_svg(c.ell(x, 4.4, 0.35, 0.35, 8), STEEL["light"]))
        det.append(stroke_svg(c.ell(x, 4.4, 0.35, 0.35, 8), STEEL["line"], 1.2))
    c.shape(top + hem, TARP, det, hl=(0.3, 0.25, 0.3, 0.2), seed=41, tension=0.45, jitter=0.4, lo=(5, 5),
            so=(6, 6), soft=2)
    return c.save((0.0, 0.0), "the tarp over Big Green 01 (34 long, faces -x like the kart): its hem at y 3.2 "
                              "to 4.4, so the kart's wheels show under it; pivot on the ground at its middle")


def shutter():
    """the Parts Depot's roll-up shutter, jammed crooked: corrugated steel, a yellow and black warning bar
    along its foot"""
    c = R.card("shutter", (-18.0, -1.0, 18.0, 31.0))
    body = [(-16.5, 4.0), (16.5, 1.6), (16.6, 30.0), (-16.6, 30.0)]
    ribs = (8.0, 11.0, 14.0, 17.0, 20.0, 23.0, 26.0)
    det = [stroke_svg(c.line([(-16.4, y), (16.4, y)]), STEEL["shadow"], 2.2) for y in ribs + (28.6,)]
    det += [stroke_svg(c.line([(-16.4, y + 0.7), (16.4, y + 0.7)]), STEEL["light"], 1.2) for y in ribs]
    c.shape(body, STEEL, det, hl=(0.3, 0.2, 0.3, 0.16), seed=51, tension=0.08, jitter=0.2, lo=(4, 4), so=(5, 5),
            soft=1.5)
    foot = lambda x: 4.0 - (x + 16.5) * (2.4 / 33.0)   # the slanted foot
    bar = [(-16.6, foot(-16.6)), (16.6, foot(16.6)), (16.6, foot(16.6) + 3.0), (-16.6, foot(-16.6) + 3.0)]
    stripes = []
    for k in range(-16, 15, 4):
        stripes.append(fill_svg(poly_path(c.pts([(k, foot(k) + 0.1), (k + 1.6, foot(k + 1.6) + 0.1),
                                                 (k + 2.8, foot(k + 2.8) + 2.9), (k + 1.2, foot(k + 1.2) + 2.9)])),
                                "#2a2418"))
    c.shape(bar, STRIPE, stripes, seed=52, tension=0.05, jitter=0.15, lo=(2, 2), so=(3, 3), soft=1)
    c.p.raw(stroke_svg(c.line([(-7.0, foot(-7.0) + 1.5), (-4.0, foot(-4.0) + 1.5)]), BLACK["line"], 3.0))
    return c.save((0.0, 30.0), "the Parts Depot's roll-up shutter, jammed crooked (its foot 1.6 to 4.0 above the "
                               "floor); pivot at the middle of its top edge, so it rolls up (sy) toward it")


def preview():
    W, H = 1100, 430
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    x = 10
    for n in ("crown", "microphone", "lights_0", "lights_1", "lights_2", "lights_3", "lights_go", "wheel"):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im, (x, 100 - im.height))
        d.text((x, 108), n, fill=(40, 30, 30, 255))
        x += im.width + 12
    x = 10
    for n in ("trophy", "trophy_piece", "tarp", "shutter"):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im, (x, 400 - im.height))
        d.text((x, 404), n, fill=(40, 30, 30, 255))
        x += im.width + 12
    d.text((700, 132), "Chapter 3 props, 1:1 at 8 texels/unit", fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    crown()
    mic()
    for k in (0, 1, 2, 3, "go"):
        lights(f"lights_{k}", k)
    trophy()
    trophy_piece()
    wheel()
    tarp()
    shutter()
    R.write_json()
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
