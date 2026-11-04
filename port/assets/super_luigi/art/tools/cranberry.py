"""Cranberry (c_sl_cranberry), the last of the Luffs (Super Luigi ch5).

Run: python cranberry.py -> ../cranberry/*.svg, *.png, parts.json, preview.png
ch5.md: a small boy in white robes, short reddish hair, a red-berry clasp at the collar, bare feet,
sitting cross-legged on the dais. Field only: sleeping seated (slow breathing), open eyes (a blink,
then a smile), talk (seated), fade (script alpha), hand-over (holding out the piece).
The Luffs: an ancient, white-domed civilisation; his robe's trim carries their round glyphs.
8 texels/unit, facing -x, seated on y 0 (the dais is the map's), about 15 units to the hair's top.

Cards: body_seated, head_sleep, head_wake, head, head_smile, head_talk, arm_front, arm_back,
arm_reach (the hand-over), compass_piece (the piece he holds out).
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, assemble, stroke_svg, fill_svg, smooth_path, ellipse_pts, poly_path, clean  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), "..", "cranberry")
R = Rig("c_sl_cranberry", OUT)

SKIN = dict(fill="#f7dcc4", light="#fff0e2", shadow="#e0b294", line="#5a2a1a")
HAIR = dict(fill="#c8503a", light="#e8806a", shadow="#8e2e22", line="#3a0c08")
ROBE = dict(fill="#eeeef4", light="#ffffff", shadow="#c4c4d6", line="#34344a")
TRIM = dict(fill="#e0b040", light="#f6d880", shadow="#a87a18", line="#4a3006")
GLYPH = "#a9b4cc"
BERRY = dict(fill="#cc2a40", light="#f0687a", shadow="#8a1424", line="#3a0610")
LEAF = "#4f9a3a"
PIECE = dict(fill="#e8bc44", light="#fbe496", shadow="#ac801c", line="#4a3006")


def far(t, k=0.9):
    def dk(h):
        r, g, b = (int(h[i:i + 2], 16) for i in (1, 3, 5))
        return "#%02x%02x%02x" % (int(r * k), int(g * k), int(b * k))
    return dict(fill=dk(t["fill"]), light=dk(t["light"]), shadow=dk(t["shadow"]), line=t["line"])


# ------------------------------------------------------------------ head --
EYE = (-1.6, 11.2)


def head(kind):
    name = {"sleep": "head_sleep", "wake": "head_wake", "open": "head", "smile": "head_smile",
            "talk": "head_talk"}[kind]
    c = R.card(name, (-4.5, 7.5, 3.5, 15.5))
    face = [(2.2, 10.2), (2.4, 12.4), (1.4, 14.0), (-0.6, 14.6), (-2.6, 14.2), (-3.6, 12.8), (-3.8, 11.4),
            (-3.6, 10.2), (-3.0, 9.2), (-1.6, 8.6), (0.2, 8.6), (1.4, 9.2)]
    ex, ey = EYE
    det = [f'<path d="{c.ell(-1.4, 10.2, 0.75, 0.45)}" fill="#f4a0a0" fill-opacity="0.55" filter="url(#blush)"/>',
           stroke_svg(c.curve((0.7, 11.2), (1.4, 10.6), (0.8, 10.0)), SKIN["line"], 2.3)]   # ear
    if kind == "sleep":   # peaceful closed eyes, lashes down
        det.append(stroke_svg(c.curve((ex - 0.7, ey + 0.1), (ex, ey - 0.35), (ex + 0.7, ey + 0.1)), "#3a1a10", 2.8))
    elif kind == "smile":  # eyes closed in a happy arc
        det.append(stroke_svg(c.curve((ex - 0.7, ey - 0.15), (ex, ey + 0.45), (ex + 0.7, ey - 0.15)), "#3a1a10", 2.8))
    else:
        lid = 0.45 if kind == "wake" else 0.0
        det += [fill_svg(c.ell(ex, ey, 0.55, 0.72, 12), "#ffffff"),
                fill_svg(c.ell(ex - 0.16, ey - 0.08 - lid * 0.3, 0.38, 0.5, 12), "#b8443a"),
                fill_svg(c.ell(ex - 0.2, ey - 0.1 - lid * 0.3, 0.2, 0.28, 10), "#2a0c0a"),
                fill_svg(c.ell(ex - 0.32, ey + 0.2 - lid * 0.3, 0.11, 0.12, 6), "#ffffff"),
                stroke_svg(c.ell(ex, ey, 0.55, 0.72, 12), SKIN["line"], 1.5)]
        if lid:   # heavy, sleepy lid
            det.append(fill_svg(c.d([(ex - 0.7, ey + 0.8), (ex + 0.7, ey + 0.8), (ex + 0.7, ey + 0.05),
                                     (ex - 0.7, ey + 0.1)], 0.2, 0.05), SKIN["fill"]))
            det.append(stroke_svg(c.line([(ex - 0.65, ey + 0.1), (ex + 0.65, ey + 0.05)]), "#3a1a10", 2.6))
        else:
            det.append(stroke_svg(c.curve((ex - 0.6, ey + 0.55), (ex, ey + 0.95), (ex + 0.6, ey + 0.55)), "#3a1a10", 2.4))
    # the mouth
    if kind == "talk":
        det += [fill_svg(c.ell(-2.7, 9.6, 0.36, 0.3, 10), "#8a2a30"), fill_svg(c.ell(-2.66, 9.48, 0.2, 0.1, 8), "#f08a90")]
    elif kind in ("smile", "open"):
        det.append(stroke_svg(c.curve((-3.1, 9.9), (-2.6, 9.4), (-2.0, 9.8)), "#9a3a3a", 2.3))
    else:
        det.append(stroke_svg(c.line([(-3.0, 9.7), (-2.3, 9.65)]), "#9a3a3a", 2.0))
    c.p.defs.append('<filter id="blush" x="-50%" y="-50%" width="200%" height="200%">'
                    '<feGaussianBlur stdDeviation="2"/></filter>')
    c.shape(face, SKIN, det, hl=(0.42, 0.22, 0.24, 0.14), seed=5, tension=0.6, lo=(4, 4), so=(5, 5))
    # short reddish hair: a tousled cap over the top and back
    hair = [(-3.7, 12.6), (-3.4, 14.0), (-2.2, 15.0), (0.2, 15.3), (2.0, 14.6), (2.9, 13.0), (2.8, 11.0),
            (2.2, 9.6), (1.6, 10.6), (1.4, 12.2), (0.6, 12.6), (0.0, 12.0), (-0.8, 12.8), (-1.6, 12.2),
            (-2.4, 12.9), (-3.0, 12.2)]
    c.shape(hair, HAIR, [stroke_svg(c.curve((-1.6, 14.4), (-0.4, 13.8), (0.6, 14.4)), HAIR["shadow"], 2),
                         stroke_svg(c.curve((1.2, 14.0), (2.2, 13.0), (2.2, 11.4)), HAIR["shadow"], 2)],
            hl=(0.38, 0.22, 0.26, 0.16), seed=6, tension=0.6, lo=(3, 3), so=(4, 4))
    return c.save((0.0, 8.6), {"sleep": "asleep: eyes closed, peaceful", "wake": "waking: heavy, half-open eyes",
                               "open": "awake: a gentle look (reddish-brown eyes)",
                               "smile": "smiling, eyes closed happily", "talk": "talking"}[kind] +
                  "; pivot at the neck (0, 8.6)")


# ------------------------------------------------------------------ body --
def body():
    c = R.card("body_seated", (-7.0, 0.0, 6.0, 9.0))
    # cross-legged: a wide lap of robe, the torso rising from it
    pts = [(-2.0, 8.8), (1.8, 8.8), (2.8, 7.0), (3.4, 4.6), (5.4, 3.6), (5.8, 1.8), (4.4, 0.5), (-5.0, 0.5),
           (-6.6, 1.6), (-6.4, 3.4), (-3.8, 4.4), (-2.8, 6.6)]
    det = [stroke_svg(c.curve((-6.0, 2.4), (-0.4, 3.8), (5.2, 2.4)), ROBE["shadow"], 3),
           stroke_svg(c.curve((-0.4, 8.0), (0.2, 6.0), (-0.2, 4.0)), ROBE["shadow"], 2.5),
           # gold trim down the robe's front edge and round the lap's hem
           stroke_svg(c.curve((-2.0, 8.6), (-2.8, 6.4), (-3.4, 4.4)), TRIM["fill"], 6),
           stroke_svg(c.line([(-6.4, 1.2), (5.4, 1.2)]), TRIM["fill"], 6)]
    # Luff glyphs on the trim: small rings
    det += [stroke_svg(c.ell(x, 1.2, 0.26, 0.26, 8), TRIM["line"], 1.3) for x in (-5.2, -3.4, -1.6, 0.2, 2.0, 3.8)]
    det += [fill_svg(c.ell(x, 5.8, 0.35, 0.35, 8), GLYPH) for x in (1.2,)]
    det.append(stroke_svg(c.ell(1.2, 5.8, 0.7, 0.7, 12), GLYPH, 1.5))
    c.shape(pts, ROBE, det, hl=(0.42, 0.2, 0.26, 0.14), hl_alpha=0.65, seed=7, tension=0.55)
    # bare feet peeking from the lap, toes forward
    for x, s in ((-5.4, 1.4), (-2.8, 1.25)):
        foot = [(x - 1.0 * s, 1.0 + 0.6 * s), (x - 0.9 * s, 0.9), (x + 0.4 * s, 0.7), (x + 0.9 * s, 1.2), (x + 0.4 * s, 1.0 + 0.9 * s)]
        c.shape(foot, SKIN, [stroke_svg(c.line([(x - 0.7 * s, 1.0), (x - 0.7 * s, 1.5)]), SKIN["shadow"], 1.5)],
                seed=int(8 - x), tension=0.6, lo=(2, 2), so=(2, 2), grow=1.3, inner=1.7, bias=(1.1, 1.3))
    # the collar's red-berry clasp: three berries and a leaf
    for (x, y) in ((0.2, 7.7), (0.8, 8.0), (0.7, 7.3)):
        c.shape(ellipse_pts(0, 0, 1, 1, 8), BERRY, [fill_svg(c.ell(x - 0.1, y + 0.1, 0.08, 0.08, 6), "#ffffff")],
                d=c.ell(x, y, 0.36, 0.36, 10), seed=9, soft=0.6, lo=(1, 1), so=(1.5, 1.5), grow=1.0, inner=1.3,
                bias=(0.8, 0.9))
    c.p.raw(fill_svg(c.d([(1.0, 8.4), (1.6, 8.6), (1.8, 8.1), (1.2, 8.1)], 0.5, 0.1), LEAF))
    return c.save((0.0, 0.5), "seated cross-legged: white robe with gold Luff-glyph trim, bare feet, the "
                              "red-berry clasp at the collar; the root card, pivot (0, 0.5) on the dais")


def arm(kind):
    name = {"front": "arm_front", "back": "arm_back", "reach": "arm_reach"}[kind]
    if kind == "reach":
        c = R.card(name, (-9.0, 4.0, 0.0, 9.0))
        sleeve = [(-0.6, 8.4), (-1.4, 8.6), (-5.6, 7.6), (-6.2, 6.0), (-5.2, 5.4), (-1.0, 6.4)]
        cuff = (-5.9, 6.6)
        hand = [(-6.0, 7.2), (-7.8, 6.8), (-8.4, 6.2), (-7.8, 5.8), (-6.0, 5.8)]
        pivot = (-1.2, 7.6)
    else:
        ox = 0.0 if kind == "front" else 3.0
        c = R.card(name, (-5.0 + ox, 1.0, 1.0 + ox, 9.0))
        sleeve = [(x + ox, y) for x, y in [(-0.6, 8.4), (-2.4, 8.2), (-3.6, 5.8), (-4.2, 3.4), (-2.2, 2.8),
                                           (-1.6, 5.2), (-0.4, 7.0)]]
        cuff = (-3.2 + ox, 3.2)
        hand = [(x + ox, y) for x, y in [(-3.8, 3.2), (-4.4, 2.2), (-3.4, 1.5), (-2.4, 2.0), (-2.4, 3.0)]]
        pivot = (-1.2 + ox, 7.6)
    robe, skin, trim = (ROBE, SKIN, TRIM) if kind != "back" else (far(ROBE), far(SKIN), far(TRIM))
    c.shape(hand, skin, hl=(0.4, 0.3, 0.25, 0.2), seed=10, tension=0.6, lo=(2, 2), so=(2, 2), grow=1.3, inner=1.7,
            bias=(1.1, 1.3))
    c.shape(sleeve, robe, [stroke_svg(c.line([(cuff[0] - 0.5, cuff[1] + 0.4), (cuff[0] + 0.6, cuff[1] - 0.4)])
                                      if kind != "reach" else c.line([(cuff[0], cuff[1] + 0.9), (cuff[0] + 0.3, cuff[1] - 0.6)]),
                                      trim["fill"], 6)],
            hl=(0.4, 0.22, 0.26, 0.16), hl_alpha=0.65, seed=11, tension=0.55, lo=(3, 3), so=(4, 4))
    return c.save(pivot, {"front": "the front arm resting on the lap", "back": "the back arm, a tone darker",
                          "reach": "the hand-over: the front arm held out, palm up (the piece sits on it)"}[kind] +
                  "; pivot at the shoulder")


def compass_piece():
    c = R.card("compass_piece", (-2.5, -2.5, 2.5, 2.5), grow=1.4, inner=1.8, bias=(1.2, 1.4))
    arc = [(2.1 * math.cos(math.radians(a)), -0.6 + 2.1 * math.sin(math.radians(a))) for a in range(25, 156, 13)]
    arc += [(0.9 * math.cos(math.radians(a)), -0.6 + 0.9 * math.sin(math.radians(a))) for a in range(155, 24, -26)]
    gem = c.d([(0.0, 1.5), (0.35, 1.05), (0.0, 0.6), (-0.35, 1.05)], 0.2, 0.05)
    c.shape(arc, PIECE, [fill_svg(gem, "#7ad8f8"), stroke_svg(gem, PIECE["line"], 1.4)],
            hl=(0.38, 0.25, 0.26, 0.18), seed=12, tension=0.4, lo=(2, 2), so=(3, 3))
    return c.save((0.0, 0.0), "the compass piece he guarded: a curved golden plate with a pale-blue jewel; held "
                              "on arm_reach's palm, at about (-7.6, 7.2)")


# --------------------------------------------------------------- preview --
ORDER = ["arm_back", "body_seated", "arm_front", "head"]


def preview():
    W, H = 980, 220
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    poses = [("asleep (breathing)", {"head": "head_sleep"}), ("waking", {"head": "head_wake"}),
             ("awake", {}), ("smiling", {"head": "head_smile"}), ("talking", {"head": "head_talk"}),
             ("the hand-over", {"head": "head_smile", "arm_front": "arm_reach"})]
    for i, (label, ov) in enumerate(poses):
        im = assemble(OUT, R.meta, ORDER, (150, 170), (90, 150), ov, bg=(0, 0, 0, 0))
        if label == "the hand-over":
            pc = Image.open(os.path.join(OUT, "compass_piece.png")).convert("RGBA")
            px, py = R.meta["compass_piece"]["pivot"]
            im.alpha_composite(pc, (round(90 - 7.6 * 8 - px), round(150 - 7.2 * 8 - py)))
        c.alpha_composite(im, (10 + i * 160, 30))
        d.rectangle([10 + i * 160, 14, 160 + i * 160, 28], fill=(225, 218, 205, 255))
        d.text((14 + i * 160, 16), label, fill=(40, 30, 30, 255))
    d.rectangle([0, 180, W, 182], fill=(180, 170, 150, 255))
    d.text((10, 196), "Cranberry (c_sl_cranberry), seated, 1:1 at 8 texels/unit; fade = the model's alpha in script",
           fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    for k in ("sleep", "wake", "open", "smile", "talk"):
        head(k)
    body()
    for k in ("front", "back", "reach"):
        arm(k)
    compass_piece()
    R.write_json({"draw_order": ORDER})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
