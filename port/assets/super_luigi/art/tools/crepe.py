"""Minister Crepe (c_sl_crepe), the Waffle Kingdom's scheming minister (Super Luigi ch0 and ch6).

Run: python crepe.py -> ../crepe/*.svg, *.png, parts.json, preview.png
Card list from the models agent: robe, cape, head, head_blink, head_talk, hat, mustache, arm_front,
arm_back, foot_front, foot_back, scroll, compass_base (+ head_gold: the eyes gone gold, ch6's "look
into the compass"). 8 texels/unit, facing -x, feet at y 0, about 27 units to the hat's top.
Crepe motifs: a cape of golden crepe with browned spots and a scalloped folded edge, a crepe-fold
ruff, strawberry-red trim, a strawberry jewel on the hat; a curled moustache and a sly lidded eye.
"""
import math
import os
import sys

from PIL import ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, assemble, stroke_svg, fill_svg, smooth_path, ellipse_pts, star_pts, poly_path, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "crepe")
R = Rig("c_sl_crepe", OUT)

SKIN = dict(fill="#f3c79c", light="#fde3c6", shadow="#d39a6e", line="#4a2414")
ROBE = dict(fill="#efe3c4", light="#fbf6e6", shadow="#c7b086", line="#3e2e16")
TRIM = dict(fill="#d8384a", light="#f07484", shadow="#a01e30", line="#3a0810")
CREPE = dict(fill="#e8b860", light="#f7d892", shadow="#b8822f", line="#3e2408")
BROWN = "#c48a3c"            # browned crepe spots
PLUM = dict(fill="#6a2c5c", light="#8f4c80", shadow="#4a1a40", line="#1e0818")
GOLD = dict(fill="#e2b23a", light="#f6dc7a", shadow="#a87a1c", line="#4a3006")
GLOVE = dict(fill="#f2f0ea", light="#ffffff", shadow="#c8c4bc", line="#33303a")
STACHE = dict(fill="#5e3c22", light="#7e5636", shadow="#3e2412", line="#1c0e06")
BROW = "#4a3a30"
HAIR = dict(fill="#d9d4cc", light="#f2efe9", shadow="#a8a29a", line="#3a3632")
PAPER = dict(fill="#f2e6c6", light="#fffaea", shadow="#c9b88e", line="#3e3016")
SHOE = dict(PLUM)
DARKEN = lambda t: dict(t, fill=t["shadow"], light=t["fill"])


def far(t):
    """the far side, a tone darker (STYLE R5)"""
    def dk(h, k=0.86):
        r, g, b = (int(h[i:i + 2], 16) for i in (1, 3, 5))
        return "#%02x%02x%02x" % (int(r * k), int(g * k), int(b * k))
    return dict(fill=dk(t["fill"]), light=dk(t["light"]), shadow=dk(t["shadow"]), line=t["line"])


# ------------------------------------------------------------------ head --
HEAD_RECT = (-6.0, 12.0, 3.0, 21.0)
EYE = (-2.5, 17.7)


def head(kind):
    name = {"open": "head", "blink": "head_blink", "talk": "head_talk", "gold": "head_gold"}[kind]
    c = R.card(name, HEAD_RECT)
    # grey hair at the back of the head, under the hat
    c.shape([(0.6, 19.6), (2.6, 19.2), (2.9, 17.4), (2.2, 15.6), (1.2, 16.6), (0.4, 18.4)], HAIR,
            [stroke_svg(c.curve((1.4, 19.0), (2.4, 18.0), (1.8, 16.6)), HAIR["shadow"], 2)], seed=3, soft=1.5)
    face = [(2.4, 16.0), (2.2, 19.4), (0.8, 20.6), (-2.2, 20.6), (-3.8, 19.6), (-4.0, 18.2), (-4.6, 17.4),
            (-5.6, 16.6), (-5.7, 15.3), (-4.6, 14.7), (-3.9, 14.2), (-4.0, 13.4), (-2.6, 12.5), (0.2, 12.4),
            (1.8, 13.4)]
    ex, ey = EYE
    det = [
        # cheek blush, soft
        f'<path d="{c.ell(-2.2, 15.2, 1.2, 0.7)}" fill="#ef9f8a" fill-opacity="0.6" filter="url(#blush)"/>',
        # the ear
        stroke_svg(c.curve((0.8, 17.0), (1.8, 16.2), (0.9, 15.2)), SKIN["line"], 3),
        # the arched, scheming brow
        stroke_svg(c.curve((-3.8, 18.6), (-2.6, 19.5), (-1.0, 18.7)), BROW, 4.5),
        # the chin's crease
        stroke_svg(c.curve((-3.2, 13.0), (-2.2, 12.8), (-1.4, 13.2)), SKIN["shadow"], 2.5),
    ]
    if kind in ("open", "talk", "gold"):
        iris = "#1a1210" if kind != "gold" else "#f6c83a"
        det += [fill_svg(c.ell(ex, ey, 0.85, 0.5, 12, rot=-8), "#fbf8f0"),
                fill_svg(c.ell(ex - 0.42, ey - 0.06, 0.32, 0.4, 10), iris),
                fill_svg(c.ell(ex - 0.5, ey + 0.12, 0.1, 0.1, 6), "#ffffff")]
        if kind == "gold":
            det.append(stroke_svg(c.ell(ex - 0.42, ey - 0.06, 0.32, 0.4, 10), "#a8700c", 1.5))
        # heavy upper lid: sly
        det += [stroke_svg(c.ell(ex, ey, 0.85, 0.5, 12, rot=-8), SKIN["line"], 1.8),
                f'<path d="{c.d([(ex - 1.0, ey + 0.1), (ex, ey + 0.2), (ex + 0.95, ey + 0.05), (ex + 0.95, ey + 0.7), (ex - 1.0, ey + 0.7)], 0.3, 0.1)}" fill="{SKIN["shadow"]}"/>',
                stroke_svg(c.curve((ex - 1.0, ey + 0.12), (ex, ey + 0.28), (ex + 0.95, ey + 0.05)), SKIN["line"], 3)]
    else:
        det.append(stroke_svg(c.curve((ex - 0.9, ey + 0.05), (ex, ey - 0.35), (ex + 0.9, ey + 0.0)),
                              SKIN["line"], 3.2))
    if kind == "talk":   # the mouth open under the moustache
        det += [fill_svg(c.ell(-2.5, 13.1, 0.75, 0.6, 10), "#5a1420"),
                fill_svg(c.ell(-2.4, 12.8, 0.45, 0.22, 8), "#e0606e")]
    c.p.defs.append('<filter id="blush" x="-50%" y="-50%" width="200%" height="200%">'
                    '<feGaussianBlur stdDeviation="2"/></filter>')
    c.shape(face, SKIN, det, hl=(0.42, 0.22, 0.24, 0.14), seed=5, tension=0.55)
    # the nose: big and round, its own lump over the face
    c.shape([(-4.2, 17.0), (-5.0, 17.3), (-5.9, 16.6), (-6.0, 15.4), (-5.2, 14.8), (-4.2, 15.0)], SKIN,
            hl=(0.4, 0.3, 0.26, 0.18), seed=6, tension=0.6, lo=(3, 3), so=(5, 5), grow=1.4, inner=1.8)
    return c.save((0.0, 13.0), {"open": "the head: sly lidded eye, arched brow, big round nose",
                                "blink": "eye shut", "talk": "mouth open (under the moustache)",
                                "gold": "eyes gone gold (ch6: looking into the compass)"}[kind] +
                  "; pivot at the neck (0, 13), the collar covers it")


def mustache():
    c = R.card("mustache", (-8.0, 13.0, -2.0, 16.0), grow=1.4, inner=1.8, bias=(1.2, 1.4))
    pts = [(-4.4, 15.0), (-3.0, 14.6), (-2.6, 14.0), (-3.4, 13.7), (-4.4, 14.0), (-5.6, 13.6), (-6.8, 13.7),
           (-7.6, 14.3), (-7.7, 15.2), (-7.1, 15.6), (-6.7, 15.0), (-7.0, 14.5), (-6.4, 14.4), (-5.4, 14.9)]
    det = [stroke_svg(c.curve((-3.4, 14.4), (-4.6, 14.3), (-6.0, 14.0)), STACHE["shadow"], 2),
           stroke_svg(c.curve((-3.2, 14.1), (-4.4, 14.0), (-5.6, 13.8)), STACHE["light"], 1.4)]
    c.shape(pts, STACHE, det, hl=(0.45, 0.25, 0.25, 0.2), seed=7, tension=0.6, soft=1.5, lo=(3, 3), so=(3, 3))
    return c.save((-4.4, 14.9), "the curled moustache, its curl swept forward and up; pivot under the nose, "
                                "for the talk wiggle")


def hat():
    c = R.card("hat", (-4.5, 19.0, 3.5, 29.0))
    body = [(-3.1, 20.2), (-3.4, 24.0), (-3.2, 27.6), (-2.6, 28.4), (1.4, 28.6), (2.2, 27.8), (2.4, 24.0),
            (2.4, 20.2)]
    det = [stroke_svg(c.line([(-3.0, 27.4), (2.1, 27.6)]), PLUM["shadow"], 2.5)]
    c.shape(body, PLUM, det, hl=(0.36, 0.2, 0.22, 0.12), seed=8, tension=0.35)
    # the band: a folded crepe, scalloped, with a strawberry jewel
    band = [(-3.3, 20.6), (-3.3, 22.4)] + [(-3.3 + 5.8 * k / 8, 22.4 + (0.3 if k % 2 else 0)) for k in range(9)] + \
        [(2.5, 20.6)]
    c.shape(band, CREPE, [fill_svg(c.ell(-1.4 + k * 1.3, 21.4, 0.28, 0.22), BROWN) for k in range(4)],
            seed=9, tension=0.4, lo=(3, 3), so=(3, 3), soft=1.5)
    berry = [(-2.6, 22.0), (-1.6, 22.0), (-1.4, 21.2), (-2.1, 20.4), (-2.8, 21.2)]
    c.shape(berry, TRIM, [fill_svg(c.ell(x, y, 0.08, 0.1, 6), "#f6e48a") for x, y in
                          ((-2.3, 21.4), (-1.9, 21.6), (-1.8, 21.0), (-2.2, 20.9))] +
            [fill_svg(c.d([(-2.7, 22.0), (-2.1, 22.5), (-1.5, 22.0), (-2.1, 21.8)], 0.3, 0.1), "#4f9a3a")],
            seed=10, tension=0.6, lo=(2, 2), so=(2, 2), soft=1, grow=1.2, inner=1.5, bias=(1, 1.2),
            hl=(0.38, 0.28, 0.2, 0.16))
    brim = [(-4.2, 19.4), (3.2, 19.4), (3.0, 20.4), (-4.0, 20.6)]
    c.shape(brim, PLUM, seed=11, tension=0.3, lo=(3, 3), so=(3, 3))
    return c.save((-0.5, 19.8), "the tall minister's hat, crepe band and strawberry jewel; pivot at the brow")


# ------------------------------------------------------------------ body --
def robe():
    c = R.card("robe", (-8.0, 1.0, 6.0, 15.0))
    pts = [(-2.4, 13.8), (2.2, 13.8), (3.8, 12.6), (4.8, 9.8), (5.0, 5.6), (4.6, 1.8), (1.8, 1.3), (-3.4, 1.3),
           (-6.2, 2.6), (-7.5, 5.6), (-7.6, 8.4), (-6.6, 11.0), (-4.4, 13.0)]
    det = [
        # strawberry-red trim down the front and round the hem
        stroke_svg(c.curve((-4.8, 12.4), (-7.4, 7.4), (-5.2, 1.6)), TRIM["fill"], 11),
        stroke_svg(c.curve((-4.8, 12.4), (-7.4, 7.4), (-5.2, 1.6)), TRIM["shadow"], 2),
        stroke_svg(c.line([(-6.2, 2.2), (4.6, 2.2)]), TRIM["fill"], 11),
        stroke_svg(c.line([(-6.2, 2.85), (4.6, 2.85)]), TRIM["shadow"], 2),
        # a sash across the belly
        stroke_svg(c.curve((-7.4, 7.0), (-1.0, 8.6), (5.0, 6.6)), GOLD["fill"], 13),
        stroke_svg(c.curve((-7.4, 6.3), (-1.0, 7.9), (5.0, 5.9)), GOLD["shadow"], 2),
        # folds
        stroke_svg(c.curve((-1.0, 11.0), (0.0, 7.0), (-0.6, 3.0)), ROBE["shadow"], 3),
        stroke_svg(c.curve((2.6, 11.6), (3.4, 7.0), (2.8, 3.0)), ROBE["shadow"], 3),
    ]
    for y in (11.2, 9.4, 5.4, 4.0):   # gold buttons on the trim
        bx = -6.4 + 0.5 * abs(y - 7.4)
        det.append(fill_svg(c.ell(bx, y, 0.32, 0.32, 8), GOLD["light"]))
        det.append(stroke_svg(c.ell(bx, y, 0.32, 0.32, 8), GOLD["line"], 1.6))
    c.shape(pts, ROBE, det, hl=(0.40, 0.24, 0.2, 0.1), seed=12, tension=0.5, hl_alpha=0.6)
    # the ruff: a folded-crepe collar round the neck
    ruff = []
    for k in range(13):
        a = math.pi * k / 12
        r = 1.0 if k % 2 == 0 else 0.78
        ruff.append((-0.4 + 3.8 * r * math.cos(a + math.pi), 12.8 - 1.5 * r * math.sin(a)))
    ruff += [(3.0, 13.4), (-3.8, 13.2)]
    c.shape(ruff, CREPE, [stroke_svg(c.line([(x, 11.6), (x * 0.9, 12.6)]), CREPE["shadow"], 2)
                          for x in (-3.0, -1.4, 0.4, 2.0)] + [fill_svg(c.ell(-2.2, 12.0, 0.3, 0.22), BROWN)],
            seed=13, tension=0.5, lo=(3, 3), so=(4, 4), soft=2, hl=(0.38, 0.3, 0.26, 0.2))
    return c.save((0.0, 1.5), "the portly robe: cream, strawberry trim, gold sash and buttons, a crepe ruff; "
                              "the root card, pivot at the hips' base (0, 1.5)")


def cape():
    c = R.card("cape", (-2.0, 1.0, 9.0, 16.0))
    top = [(-1.0, 14.8), (2.0, 15.4), (5.0, 14.4), (7.2, 11.0), (8.4, 6.0), (8.6, 1.8)]
    hem = []
    for k in range(11):   # the scalloped, folded crepe edge, right to left
        x = 8.6 - 7.2 * k / 10
        hem.append((x, 1.6 + (0.0 if k % 2 == 0 else 0.55)))
    back = [(1.2, 5.6), (0.6, 10.0), (-0.4, 13.4)]
    det = [fill_svg(c.ell(x, y, rx, ry, 9, wobble=0.25, seed=int(x * 10 + y)), BROWN) for x, y, rx, ry in
           ((4.0, 12.4, 0.7, 0.45), (6.2, 8.6, 0.6, 0.5), (3.4, 6.8, 0.8, 0.4), (6.8, 3.6, 0.6, 0.45),
            (2.4, 3.2, 0.5, 0.35), (5.2, 10.4, 0.35, 0.3))]
    det += [stroke_svg(c.curve(a, b, e), CREPE["shadow"], 3) for a, b, e in
            (((2.6, 14.2), (4.8, 8.0), (4.4, 2.2)), ((4.6, 13.6), (7.0, 8.0), (7.0, 2.2)))]
    # the red lining showing at the hem
    det.append(stroke_svg(c.line([(1.6, 2.1), (8.4, 2.1)]), TRIM["shadow"], 3))
    c.shape(top + hem + back, CREPE, det, hl=(0.36, 0.2, 0.26, 0.14), seed=14, tension=0.5)
    return c.save((1.0, 14.5), "the cape, behind the robe: golden crepe with browned spots and a scalloped, "
                               "folded edge; pivot at the neck (1, 14.5)")


def arm(front):
    name = "arm_front" if front else "arm_back"
    rect = (-7.0, 2.8, -1.0, 12.8) if front else (0.0, 2.8, 6.0, 12.8)
    c = R.card(name, rect)
    ox = 0.0 if front else 6.4
    dy = -1.2
    robe, trim, glove = (ROBE, TRIM, GLOVE) if front else (far(ROBE), far(TRIM), far(GLOVE))
    sleeve = [(x + ox, y + dy) for x, y in [(-1.6, 13.6), (-3.8, 13.4), (-5.4, 10.0), (-6.4, 7.0), (-3.4, 6.6),
                                            (-2.4, 9.6), (-1.4, 12.0)]]
    det = [stroke_svg(c.line([(-6.3 + ox, 7.6 + dy), (-3.3 + ox, 7.2 + dy)]), trim["fill"], 10),
           stroke_svg(c.line([(-6.3 + ox, 8.2 + dy), (-3.3 + ox, 7.8 + dy)]), trim["shadow"], 1.6),
           stroke_svg(c.curve((-3.0 + ox, 12.6 + dy), (-4.4 + ox, 10.0 + dy), (-4.6 + ox, 7.8 + dy)), robe["shadow"], 3)]
    c.shape(sleeve, robe, det, hl=(0.36, 0.2, 0.24, 0.14), seed=15 if front else 16, tension=0.5)
    hand = [(x + ox, y + dy) for x, y in [(-5.8, 6.8), (-3.8, 6.8), (-3.6, 5.4), (-4.4, 4.5), (-5.6, 4.6), (-6.2, 5.6)]]
    c.shape(hand, glove, [stroke_svg(c.curve((-5.6 + ox, 5.6 + dy), (-5.0 + ox, 5.2 + dy), (-4.4 + ox, 5.6 + dy)),
                                     glove["shadow"], 2)],
            hl=(0.38, 0.26, 0.24, 0.16), seed=17 if front else 18, tension=0.6, lo=(3, 3), so=(4, 4))
    pivot = (-2.6, 11.8) if front else (3.8, 11.8)
    return c.save(pivot, ("the front arm" if front else "the back arm, a tone darker") +
                  ": wide cream sleeve, red cuff, white glove; pivot at the shoulder")


def foot(front):
    name = "foot_front" if front else "foot_back"
    rect = (-6.0, 0.0, -1.0, 3.0) if front else (-1.0, 0.0, 4.0, 3.0)
    c = R.card(name, rect)
    ox = 0.0 if front else 4.6
    t = SHOE if front else far(SHOE)
    pts = [(-5.6 + ox, 1.9), (-5.2 + ox, 1.1), (-4.0 + ox, 0.4), (-1.6 + ox, 0.4), (-1.4 + ox, 1.8),
           (-2.4 + ox, 2.4), (-4.2 + ox, 1.6), (-5.0 + ox, 2.2)]
    c.shape(pts, t, [fill_svg(c.ell(-2.4 + ox, 1.6, 0.34, 0.28), GOLD["fill"])], hl=(0.4, 0.25, 0.24, 0.16),
            seed=19 if front else 20, tension=0.55, lo=(3, 3), so=(4, 4))
    return c.save((-2.2 + ox, 1.8), "a plum court shoe with a curled toe" + ("" if front else ", a tone darker") +
                  "; pivot at the ankle")


# ----------------------------------------------------------------- props --
def scroll():
    c = R.card("scroll", (-1.0, -1.0, 4.0, 6.0), grow=1.5, inner=1.9, bias=(1.3, 1.5))
    sheet = [(-0.3, 4.8), (3.3, 4.8), (3.3, 0.0), (-0.3, 0.0)]
    det = [stroke_svg(c.line([(0.3, y), (2.8, y)]), PAPER["shadow"], 2) for y in (3.8, 3.0, 2.2, 1.4)]
    det.append(fill_svg(c.ell(1.5, 0.8, 0.5, 0.5, 10), TRIM["fill"]))
    det.append(stroke_svg(c.ell(1.5, 0.8, 0.5, 0.5, 10), TRIM["line"], 1.6))
    c.shape(sheet, PAPER, det, hl=(0.4, 0.22, 0.24, 0.14), seed=21, tension=0.2, lo=(3, 3), so=(4, 4))
    for y in (5.0, -0.2):   # the rolled ends
        c.shape([(-0.7, y + 0.5), (3.7, y + 0.5), (3.7, y - 0.4), (-0.7, y - 0.4)], PAPER,
                [stroke_svg(c.line([(-0.4, y), (3.4, y)]), PAPER["shadow"], 2)], seed=22, tension=0.6,
                lo=(2, 2), so=(2, 2))
    return c.save((1.5, 2.4), "the letter (ch0): a scroll with a red wax seal; pivot at its middle, held in the "
                              "front glove")


def compass_base():
    c = R.card("compass_base", (-3.5, -3.5, 3.5, 3.5), grow=1.5, inner=1.9, bias=(1.3, 1.5))
    det = []
    for k in range(7):
        a0, a1 = math.radians(90 + k * 360 / 7 + 4), math.radians(90 + (k + 1) * 360 / 7 - 4)
        ring = [(2.6 * math.cos(a0), 2.6 * math.sin(a0)), (2.6 * math.cos((a0 + a1) / 2), 2.6 * math.sin((a0 + a1) / 2)),
                (2.6 * math.cos(a1), 2.6 * math.sin(a1)), (1.2 * math.cos(a1), 1.2 * math.sin(a1)),
                (1.2 * math.cos(a0), 1.2 * math.sin(a0))]
        col = "#5ab8e8" if k == 0 else "#3a2a10"
        det.append(fill_svg(poly_path(c.pts(ring)), col))
        if k == 0:   # the one intact section: a jewelled wedge
            det.append(fill_svg(poly_path(c.pts([ring[0], ring[1], (0.0, 1.6)])), "#b8ecff"))
        det.append(stroke_svg(poly_path(c.pts(ring)), GOLD["line"], 1.6))
    det.append(fill_svg(c.ell(0, 0, 0.7, 0.7, 10), GOLD["light"]))
    det.append(stroke_svg(c.ell(0, 0, 0.7, 0.7, 10), GOLD["line"], 1.6))
    c.shape(ellipse_pts(0, 0, 3.2, 3.2, 16), GOLD, det, d=c.ell(0, 0, 3.2, 3.2, 16, wobble=0.02, seed=3),
            hl=(0.38, 0.22, 0.24, 0.14), seed=23)
    return c.save((0.0, 0.0), "the Marvelous Compass's base (ch0): a gold ring of seven sockets, one section "
                              "intact; pivot at its centre, held in the front glove")


# --------------------------------------------------------------- preview --
ORDER = ["cape", "arm_back", "foot_back", "robe", "foot_front", "arm_front", "head", "mustache", "hat"]


def preview():
    from PIL import Image
    W, H = 1100, 330
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    poses = [("rest", {}), ("blink", {"head": "head_blink"}), ("talk", {"head": "head_talk"}),
             ("eyes gold (ch6)", {"head": "head_gold"})]
    for i, (label, ov) in enumerate(poses):
        im = assemble(OUT, R.meta, ORDER, (150, 250), (75, 240), ov, bg=(0, 0, 0, 0))
        c.alpha_composite(im, (10 + i * 140, 40))
        d.rectangle([10 + i * 140, 18, 140 + i * 140, 32], fill=(225, 218, 205, 255))
        d.text((14 + i * 140, 20), label, fill=(40, 30, 30, 255))
    # holding the props
    im = assemble(OUT, R.meta, ORDER, (150, 250), (75, 240), bg=(0, 0, 0, 0))
    c.alpha_composite(im, (580, 40))
    for prop, xy in (("compass_base", (-5.4, 4.8)),):
        pi = Image.open(os.path.join(OUT, prop + ".png")).convert("RGBA")
        px, py = R.meta[prop]["pivot"]
        mx, my = xy or (-5.0, 4.4)
        if prop == "scroll":
            ox = 720
        else:
            ox = 580
        c.alpha_composite(pi, (round(ox + 75 + mx * 8 - px), round(40 + 240 - my * 8 - py)))
    im2 = assemble(OUT, R.meta, ORDER, (150, 250), (75, 240), bg=(0, 0, 0, 0))
    c.alpha_composite(im2, (720, 40))
    pi = Image.open(os.path.join(OUT, "scroll.png")).convert("RGBA")
    px, py = R.meta["scroll"]["pivot"]
    c.alpha_composite(pi, (round(720 + 75 - 5.0 * 8 - px), round(40 + 240 - 4.4 * 8 - py)))
    d.rectangle([580, 18, 860, 32], fill=(225, 218, 205, 255))
    d.text((584, 20), "with the compass base / the letter", fill=(40, 30, 30, 255))
    d.rectangle([880, 18 + 0, 1090, 32], fill=(225, 218, 205, 255))
    d.text((884, 20), "Luigi's height (24 units)", fill=(40, 30, 30, 255))
    d.rectangle([900, 280 - 192, 904, 280], fill=(60, 140, 60, 255))
    d.rectangle([0, 280, W, 282], fill=(180, 170, 150, 255))
    d.text((10, 300), "Minister Crepe (c_sl_crepe), 1:1 at 8 texels/unit", fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    for k in ("open", "blink", "talk", "gold"):
        head(k)
    mustache()
    hat()
    robe()
    cape()
    arm(True)
    arm(False)
    foot(True)
    foot(False)
    scroll()
    compass_base()
    R.write_json({"draw_order": ORDER})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
