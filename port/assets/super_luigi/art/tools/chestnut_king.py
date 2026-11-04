"""The Chestnut King (c_sl_chestnut_king), chapter 6's first boss: King Marron under Crepe's curse.

Run: python chestnut_king.py -> ../chestnut_king/*.svg, *.png, parts.json, preview.png
ch6.md: a huge round body of spiky brown husk (a chestnut burr), a smaller round head on top with a
mouth of fangs and two small sad eyes, short thick legs, green goo dripping from the jaws. The husk
splits open down the front (and closes), and in the curse's breaking falls away in pieces. Regal:
a crown and a cape (the task). Goomba-like: the head is a chestnut (Goombas are chestnuts, kuribo).
8 texels/unit, facing -x, feet at y 0, about 50 units tall (Luigi is 24).

Cards: husk_left, husk_right (the burr's two front halves, meeting at x 0), nut (the chestnut body
inside, seen when the husk splits), head, head_roar, head_hurt, head_sad, head_calm, crown, cape,
leg_front, leg_back, goo_drip_1..3 (a drip loop), husk_shard_1..4 (the curse breaking), chestnut
(a falling chestnut, Chestnut Rain).
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, assemble, stroke_svg, fill_svg, smooth_path, poly_path, ellipse_pts, clean  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), "..", "chestnut_king")
R = Rig("c_sl_chestnut_king", OUT)

HUSK = dict(fill="#a8743c", light="#c89a5c", shadow="#6e4820", line="#24140a")
SPIKE = "#5a3a1a"
NUT = dict(fill="#a64c22", light="#d27a44", shadow="#6a2a10", line="#2a0e04")
NUTBASE = dict(fill="#e6c48e", light="#f6e2b8", shadow="#b8925a", line="#3a2410")
GOO = dict(fill="#7ccf3c", light="#c0f474", shadow="#4a9a1c", line="#163808")
FANG = dict(fill="#f4efe0", light="#ffffff", shadow="#c4bba4", line="#2e2a22")
GOLD = dict(fill="#e6b63e", light="#f8de80", shadow="#ac7e1c", line="#4a3006")
RUBY = "#d02a3a"
CAPE = dict(fill="#a82a3c", light="#d4566a", shadow="#701626", line="#2a0610")
ERMINE = dict(fill="#f4f0e6", light="#ffffff", shadow="#cfc8b8", line="#3a3630")
LEG = dict(fill="#94643a", light="#b48458", shadow="#5e3c1c", line="#1e0e04")
C0 = (0.0, 20.0)
RAD = 15.5


def burr_pts(cx, cy, r, a0, a1, n, spike=2.2, seed=1):
    rnd = random.Random(seed)
    pts = []
    for i in range(n + 1):
        a = a0 + (a1 - a0) * i / n
        rr = r + (spike * (0.8 + 0.4 * rnd.random()) if i % 2 else 0.0)
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts


def husk_texture(c, x0, x1, seed):
    """short spines over the husk: little dark ticks pointing out from the centre"""
    rnd = random.Random(seed)
    out = []
    for _ in range(70):
        a = rnd.uniform(0, 2 * math.pi)
        r = rnd.uniform(3.0, RAD - 1.6)
        x, y = C0[0] + r * math.cos(a), C0[1] + r * math.sin(a)
        if not (x0 <= x <= x1):
            continue
        dx, dy = math.cos(a) * 1.0, math.sin(a) * 1.0
        out.append(stroke_svg(c.line([(x - dx * 0.3, y - dy * 0.3), (x + dx * 0.7, y + dy * 0.7)]), SPIKE, 2.4))
    return out


def husk(side):
    left = side == "left"
    name = "husk_left" if left else "husk_right"
    rect = (-18.5, 2.0, 0.5, 38.0) if left else (-0.5, 2.0, 18.5, 38.0)
    c = R.card(name, rect)
    a0, a1 = (math.pi / 2, 3 * math.pi / 2) if left else (-math.pi / 2, math.pi / 2)
    outer = burr_pts(C0[0], C0[1], RAD, a0, a1, 36, seed=3 if left else 4)
    # the seam down the front: a jagged split
    seam = []
    for k in range(13):
        y = C0[1] + RAD - 2 * RAD * k / 12
        seam.append((0.0 + (0.35 if k % 2 else -0.35) * (1 if left else -1), y))
    pts = outer + seam[1:-1] if left else outer + seam[1:-1]
    det = husk_texture(c, -RAD if left else 0.2, -0.2 if left else RAD, 11 if left else 12)
    hl = (0.55, 0.2, 0.2, 0.08) if left else (0.32, 0.2, 0.2, 0.08)
    c.shape(pts, HUSK, det, hl=hl, hl_alpha=0.22, seed=5 if left else 6, tension=0.15, jitter=0.4)
    pivot = (-2.0, 5.5) if left else (2.0, 5.5)
    return c.save(pivot, f"the burr's {side} half (front); the halves meet at x 0. Split open: swing them apart "
                         "about their pivots near the bottom of the seam")


def nut():
    c = R.card("nut", (-12.5, 5.0, 12.5, 34.0))
    pts = [(0.0, 33.2), (3.2, 31.0), (8.6, 27.6), (11.6, 22.0), (11.8, 15.0), (9.6, 9.0), (5.0, 6.0), (-5.0, 6.0),
           (-9.6, 9.0), (-11.8, 15.0), (-11.6, 22.0), (-8.6, 27.6), (-3.2, 31.0)]
    base = c.d([(-10.6, 11.6), (-6.0, 13.2), (0.0, 13.6), (6.0, 13.2), (10.6, 11.6), (9.6, 9.0), (5.0, 6.0),
                (-5.0, 6.0), (-9.6, 9.0)], 0.5, 0.3)
    det = [fill_svg(base, NUTBASE["fill"]),
           stroke_svg(base, NUTBASE["shadow"], 2.5),
           stroke_svg(c.curve((-3.0, 30.0), (-6.0, 22.0), (-4.0, 15.0)), NUT["shadow"], 3),
           stroke_svg(c.curve((4.0, 29.0), (7.0, 22.0), (5.0, 15.0)), NUT["shadow"], 3)]
    det += [fill_svg(c.ell(x, y, 0.22, 0.22, 6), NUTBASE["shadow"]) for x, y in
            ((-6, 9.6), (-3, 8.4), (0, 9.4), (3, 8.2), (6, 9.8), (-1.6, 11.4), (2, 11.6))]
    c.shape(pts, NUT, det, hl=(0.36, 0.22, 0.2, 0.14), seed=7, tension=0.55)
    return c.save((0.0, 6.0), "the chestnut body inside the husk, glossy red-brown with its pale base; seen when "
                              "the husk splits, and left when it falls away")


HEAD_RECT = (-11.0, 30.0, 9.0, 46.0)


def head(kind):
    name = {"idle": "head", "roar": "head_roar", "hurt": "head_hurt", "sad": "head_sad",
            "calm": "head_calm"}[kind]
    c = R.card(name, HEAD_RECT)
    # the head: a chestnut, Goomba-like, broader than tall, its pale base the face's lower half
    dome = [(-1.0, 45.0), (3.0, 44.2), (6.8, 41.2), (8.2, 37.0), (7.6, 33.0), (4.6, 31.0), (-6.0, 31.0),
            (-9.6, 33.2), (-10.4, 37.2), (-8.6, 41.4), (-5.0, 44.2)]
    face_d = c.d([(-10.0, 37.0), (-5.0, 38.4), (2.0, 38.6), (7.8, 37.2), (7.6, 33.0), (4.6, 31.0), (-6.0, 31.0),
                  (-9.6, 33.2)], 0.5, 0.3)
    det = [fill_svg(face_d, NUTBASE["fill"]), stroke_svg(face_d, NUTBASE["shadow"], 2.5),
           stroke_svg(c.curve((-4.0, 43.6), (-6.5, 41.0), (-7.4, 38.6)), NUT["shadow"], 3)]
    # eyes: small and sad (droopy brows), set toward the front
    for ex in (-6.6, -3.4):
        ey = 39.4
        if kind in ("idle", "roar", "sad", "calm"):
            det += [fill_svg(c.ell(ex, ey, 0.75, 0.95, 10), "#fbf7ea"),
                    fill_svg(c.ell(ex - 0.2, ey - 0.15, 0.38, 0.55, 10), "#1a0e08"),
                    fill_svg(c.ell(ex - 0.3, ey + 0.15, 0.13, 0.15, 6), "#ffffff"),
                    stroke_svg(c.ell(ex, ey, 0.75, 0.95, 10), NUT["line"], 1.6)]
            if kind in ("sad", "idle"):   # tears waiting
                pass
        else:   # hurt: squeezed shut
            k = 1 if ex < -5 else -1
            det.append(stroke_svg(c.line([(ex - 0.6 * k, ey + 0.7), (ex + 0.4 * k, ey), (ex - 0.6 * k, ey - 0.7)]),
                                  "#1a0e08", 3.5))
        # brows: sad slant up toward the middle; furious for the roar; soft for calm
        if kind == "roar":
            det.append(stroke_svg(c.line([(ex - 0.9, ey + 1.6), (ex + 0.9, ey + 1.0)] if ex < -5 else
                                          [(ex - 0.9, ey + 1.0), (ex + 0.9, ey + 1.6)]), "#2a1408", 4))
        elif kind in ("idle", "sad", "hurt"):
            det.append(stroke_svg(c.line([(ex - 0.9, ey + 1.2), (ex + 0.9, ey + 1.8)] if ex < -5 else
                                          [(ex - 0.9, ey + 1.8), (ex + 0.9, ey + 1.2)]), "#2a1408", 3.5))
        else:
            det.append(stroke_svg(c.curve((ex - 0.8, ey + 1.4), (ex, ey + 1.8), (ex + 0.8, ey + 1.4)), "#2a1408", 3))
    if kind == "sad":   # a tear
        det.append(fill_svg(c.d([(-7.0, 38.4), (-6.7, 37.6), (-6.9, 36.8), (-7.3, 37.6)], 0.6, 0.1), "#9ad8f8"))
    # the mouth
    if kind == "calm":
        det.append(stroke_svg(c.curve((-8.2, 34.0), (-5.4, 33.0), (-2.6, 34.0)), NUT["line"], 3.5))
    else:
        open_h = {"idle": 1.4, "hurt": 1.6, "sad": 1.1, "roar": 3.2}[kind]
        mouth = c.d([(-9.6, 34.8), (-5.0, 35.0), (-0.6, 34.6), (-1.2, 34.6 - open_h * 0.6),
                     (-5.0, 34.6 - open_h), (-9.0, 34.4 - open_h * 0.5)], 0.5, 0.2)
        det += [fill_svg(mouth, "#3a0e10"), stroke_svg(mouth, NUT["line"], 3)]
        for i, fx in enumerate((-8.4, -6.6, -4.6, -2.6)):   # fangs, upper
            det.append(fill_svg(poly_path(c.pts([(fx - 0.5, 34.9), (fx + 0.5, 34.9), (fx, 34.9 - 0.9 - 0.2 * (i % 2))])),
                                FANG["fill"]))
            det.append(stroke_svg(poly_path(c.pts([(fx - 0.5, 34.9), (fx + 0.5, 34.9), (fx, 34.9 - 0.9 - 0.2 * (i % 2))])),
                                  FANG["line"], 1.4))
        for fx in (-7.4, -3.6):   # lower fangs
            yb = 34.6 - open_h * 0.9
            det.append(fill_svg(poly_path(c.pts([(fx - 0.45, yb), (fx + 0.45, yb), (fx, yb + 0.8)])), FANG["fill"]))
            det.append(stroke_svg(poly_path(c.pts([(fx - 0.45, yb), (fx + 0.45, yb), (fx, yb + 0.8)])), FANG["line"], 1.4))
        # goo at the jaw's corner
        det.append(fill_svg(c.d([(-9.8, 34.6), (-9.0, 33.6), (-9.4, 32.0), (-10.0, 31.4), (-10.4, 32.6)], 0.6, 0.2),
                            GOO["fill"]))
        det.append(stroke_svg(c.d([(-9.8, 34.6), (-9.0, 33.6), (-9.4, 32.0), (-10.0, 31.4), (-10.4, 32.6)], 0.6, 0.2),
                              GOO["line"], 1.8))
    c.shape(dome, NUT, det, hl=(0.42, 0.18, 0.26, 0.12), seed=8, tension=0.55)
    return c.save((0.0, 31.5), {"idle": "the head: a glossy chestnut, small sad eyes, a mouth of fangs, goo at the "
                                        "jaw", "roar": "roaring, the mouth wide", "hurt": "eyes squeezed shut",
                                "sad": "a tear (hesitate, kneel)", "calm": "calm, no fangs or drool (the fallback "
                                "for King Marron restored)"}[kind] + "; pivot at the neck (0, 31.5)")


def crown():
    c = R.card("crown", (-6.0, 42.0, 6.0, 49.0), grow=1.6, inner=2.0, bias=(1.4, 1.6))
    pts = [(-4.8, 43.0), (-5.2, 47.4), (-3.4, 45.6), (-1.6, 48.4), (0.2, 45.8), (2.0, 48.4), (3.6, 45.6),
           (5.0, 47.4), (4.6, 43.0)]
    det = [fill_svg(c.ell(x, 44.0, 0.42, 0.42, 8), RUBY) for x in (-3.0, 0.0, 3.0)]
    det += [stroke_svg(c.line([(-4.8, 44.8), (4.6, 44.8)]), GOLD["shadow"], 2)]
    det += [fill_svg(c.ell(x, y, 0.32, 0.32, 8), "#f4f0e6") for x, y in ((-5.2, 47.4), (-1.6, 48.4), (2.0, 48.4),
                                                                        (5.0, 47.4))]
    c.shape(pts, GOLD, det, hl=(0.38, 0.4, 0.26, 0.18), seed=9, tension=0.15, lo=(3, 3), so=(4, 4))
    return c.save((0.0, 42.8), "a small gold crown, tipped with pearls, three rubies; worn askew on the head; "
                               "pivot at its base")


def cape():
    c = R.card("cape", (-6.0, 3.0, 22.0, 39.0))
    top = [(-4.0, 36.0), (2.0, 37.6), (8.0, 36.0), (13.6, 30.0), (17.4, 21.0), (20.0, 12.0), (21.0, 4.0)]
    hem = [(21.0 - 15.0 * k / 10, 3.8 + (0.5 if k % 2 else 0.0)) for k in range(11)]
    pts = top + hem + [(4.0, 10.0), (0.0, 24.0)]
    det = [stroke_svg(c.curve(a, b, e), CAPE["shadow"], 3) for a, b, e in
           (((6.0, 34.0), (12.0, 20.0), (12.0, 5.0)), ((10.0, 32.0), (17.0, 18.0), (17.0, 5.0)))]
    # ermine trim along the edge, with black tails
    det.append(stroke_svg(c.line(hem), ERMINE["fill"], 14))
    det += [fill_svg(c.ell(x, 4.4, 0.2, 0.36, 6), "#1a1414") for x in (8.0, 11.0, 14.0, 17.0, 20.0)]
    c.shape(pts, CAPE, det, hl=(0.4, 0.15, 0.24, 0.1), seed=10, tension=0.5)
    collar = [(-4.6, 34.4), (0.0, 37.0), (6.0, 37.4), (10.0, 35.6), (8.8, 33.0), (2.0, 34.6), (-3.6, 32.6)]
    c.shape(collar, ERMINE, [fill_svg(c.ell(x, 35.2, 0.22, 0.36, 6), "#1a1414") for x in (-1.0, 3.0, 7.0)],
            seed=11, tension=0.5, lo=(3, 3), so=(4, 4), hl=(0.4, 0.3, 0.3, 0.2), hl_alpha=0.6)
    return c.save((1.0, 35.0), "the royal cape behind the husk, crimson with an ermine collar and hem; pivot at "
                               "the shoulders")


def leg(front):
    name = "leg_front" if front else "leg_back"
    rect = (-9.0, 0.0, -1.0, 8.0) if front else (1.0, 0.0, 9.0, 8.0)
    c = R.card(name, rect)
    ox = 0.0 if front else 9.0
    t = LEG if front else dict(LEG, fill="#7e5430", light="#9a6e48")
    pts = [(x + ox, y) for x, y in [(-6.4, 7.6), (-3.0, 7.6), (-2.8, 3.0), (-1.8, 1.8), (-2.2, 0.4), (-8.4, 0.4),
                                    (-8.8, 1.8), (-6.8, 3.2)]]
    det = [stroke_svg(c.line([(-7.6 + ox, 0.9), (-7.4 + ox, 2.0)]), t["shadow"], 2.5),
           stroke_svg(c.line([(-5.6 + ox, 0.9), (-5.5 + ox, 2.0)]), t["shadow"], 2.5)]
    c.shape(pts, t, det, hl=(0.4, 0.24, 0.24, 0.14), seed=12 if front else 13, tension=0.5)
    return c.save((-4.8 + ox, 6.8), "a short, thick leg and broad foot" + ("" if front else ", a tone darker") +
                  "; pivot at the hip, under the husk")


def goo_drip(k):
    c = R.card(f"goo_drip_{k}", (-2.0, -6.0, 2.0, 1.0), grow=1.2, inner=1.6, bias=(1, 1.2))
    length = (2.4, 4.2, 5.6)[k - 1]
    pts = [(-0.8, 0.6), (0.8, 0.6), (0.5, -length * 0.5), (0.35, -length + 0.5), (0.0, -length),
           (-0.35, -length + 0.5), (-0.5, -length * 0.5)]
    if k == 3:   # the drop about to fall away
        pts = [(-0.8, 0.6), (0.8, 0.6), (0.3, -2.2), (0.2, -3.6), (0.65, -4.4), (0.0, -5.5), (-0.65, -4.4),
               (-0.2, -3.6), (-0.3, -2.2)]
    c.shape(pts, GOO, [fill_svg(c.ell(-0.2, -length * 0.4, 0.15, 0.4, 6), GOO["light"])], seed=14 + k,
            tension=0.6, soft=1, lo=(1.5, 1.5), so=(2, 2))
    return c.save((0.0, 0.0), f"goo drip loop, frame {k} of 3; pivot at the jaw's corner (head x -9.8, y 34.4)")


def shard(k):
    c = R.card(f"husk_shard_{k}", (-6.0, -6.0, 6.0, 6.0))
    a0 = math.pi / 2 - 0.7 + (k - 2.5) * 0.12
    outer = burr_pts(0, -3.2, 6.0, a0, a0 + 1.4, 10, spike=1.0, seed=20 + k)
    inner = [(1.4 * math.cos(a0 + 1.4), -3.2 + 1.4 * math.sin(a0 + 1.4) + 0.6 * (k % 2)),
             (1.4 * math.cos(a0), -3.2 + 1.4 * math.sin(a0))]
    det = [stroke_svg(c.line([(x * 0.55, -3.2 + (y + 3.2) * 0.55), (x * 0.75, -3.2 + (y + 3.2) * 0.75)]), SPIKE, 2.2)
           for x, y in outer[1:-1:2]]
    c.shape(outer + inner, HUSK, det, hl=(0.4, 0.25, 0.26, 0.16), seed=24 + k, tension=0.15)
    return c.save((0.0, 0.0), f"a piece of the husk falling away as the curse breaks ({k} of 4); pivot at its "
                              "middle")


def chestnut():
    c = R.card("chestnut", (-2.5, -2.5, 2.5, 2.5), grow=1.4, inner=1.8, bias=(1.2, 1.4))
    pts = [(0.0, 2.2), (1.4, 1.0), (2.0, -0.6), (1.2, -1.8), (-1.2, -1.8), (-2.0, -0.6), (-1.4, 1.0)]
    base = c.d([(-1.9, -0.6), (0.0, -0.2), (1.9, -0.6), (1.2, -1.8), (-1.2, -1.8)], 0.5, 0.1)
    c.shape(pts, NUT, [fill_svg(base, NUTBASE["fill"]), stroke_svg(base, NUTBASE["shadow"], 1.6)],
            hl=(0.38, 0.25, 0.22, 0.16), seed=30, tension=0.55, lo=(3, 3), so=(3, 3))
    return c.save((0.0, 0.0), "a falling chestnut (Chestnut Rain); pivot at its middle")


# --------------------------------------------------------------- preview --
ORDER = ["cape", "leg_back", "nut", "husk_right", "husk_left", "leg_front", "head", "crown"]


def preview():
    W, H = 1360, 470
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)

    def label(x, y, s, w=250):
        d.rectangle([x, y, x + w, y + 14], fill=(225, 218, 205, 255))
        d.text((x + 4, y + 2), s, fill=(40, 30, 30, 255))

    label(10, 8, "The Chestnut King, at rest, 1:1 (8 texels/unit)", 330)
    im = assemble(OUT, R.meta, ORDER, (360, 420), (170, 410), bg=(0, 0, 0, 0))
    c.alpha_composite(im, (0, 20))
    # goo hanging from the jaw
    g = Image.open(os.path.join(OUT, "goo_drip_2.png")).convert("RGBA")
    px, py = R.meta["goo_drip_2"]["pivot"]
    c.alpha_composite(g, (round(170 - 9.8 * 8 - px), round(20 + 410 - 34.4 * 8 - py)))
    d.rectangle([340, 430 - 192, 344, 430], fill=(60, 140, 60, 255))
    d.text((330, 222), "Luigi", fill=(40, 90, 40, 255))
    # husk split open: halves swung apart
    label(400, 8, "husk split open (halves swung 22 deg)", 300)
    base = Image.new("RGBA", (360, 420))
    for n in ("cape", "leg_back", "nut", "leg_front", "head", "crown"):
        part = assemble(OUT, R.meta, [n], (360, 420), (170, 410), bg=(0, 0, 0, 0))
        if n == "leg_front":
            for side, ang in (("husk_right", -22), ("husk_left", 22)):
                h = assemble(OUT, R.meta, [side], (360, 420), (170, 410), bg=(0, 0, 0, 0))
                px, py = R.meta[side]["model_xy"]
                h = h.rotate(ang, resample=Image.BICUBIC, center=(170 + px * 8, 410 - py * 8))
                base.alpha_composite(h)
        base.alpha_composite(part)
    c.alpha_composite(base, (390, 20))
    # faces
    label(790, 8, "heads: idle, roar, hurt, sad, calm", 300)
    x = 760
    for i, n in enumerate(("head", "head_roar", "head_hurt", "head_sad", "head_calm")):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im, (790 + (i % 3) * 172, 30 + (i // 3) * 140))
        d.text((790 + (i % 3) * 172, 30 + (i // 3) * 140 + 128), n, fill=(70, 50, 50, 255))
    label(790, 320, "goo drip loop, husk shards, a chestnut", 300)
    x = 790
    for n in ("goo_drip_1", "goo_drip_2", "goo_drip_3", "husk_shard_1", "husk_shard_2", "husk_shard_3",
              "husk_shard_4", "chestnut"):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im, (x, 340))
        x += im.width + 8
    return c


def build():
    clean(OUT)
    husk("left")
    husk("right")
    nut()
    for k in ("idle", "roar", "hurt", "sad", "calm"):
        head(k)
    crown()
    cape()
    leg(True)
    leg(False)
    for k in (1, 2, 3):
        goo_drip(k)
    for k in (1, 2, 3, 4):
        shard(k)
    chestnut()
    R.write_json({"draw_order": ORDER})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
