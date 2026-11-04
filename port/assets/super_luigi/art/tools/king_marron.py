"""King Marron restored (c_sl_marron), the Chestnut King's true form (Super Luigi ch6).

Run: python king_marron.py -> ../king_marron/*.svg, *.png, parts.json, preview.png
ch6.md, Characters and New assets 2 (optional): "A tall, kind-faced king with a chestnut-brown cape and
a crown of leaves"; "a tall gentle king in a chestnut-brown cape, leaf crown. Stand, talk, hold (for
the embrace with Eclair), wave." He steps out of the Chestnut King's husk when the curse breaks, so
he keeps its colours: chestnut-brown hair and cape, the nut's pale cream as the cape's lining, and
a crown of chestnut leaves with one chestnut at the front. He is drawn as Eclair's match
(../eclair/README.md): the same face construction, a head taller.
8 texels/unit, facing -x, feet at y 0, about 31 units to the top of the crown.
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, assemble, stroke_svg, fill_svg, smooth_path, ellipse_pts, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "king_marron")
R = Rig("c_sl_marron", OUT)

SKIN = dict(fill="#f8d0a8", light="#ffe8cc", shadow="#dca07a", line="#5a2a1a")
HAIR = dict(fill="#9a4a22", light="#c06a3a", shadow="#6e3014", line="#2a0e04")       # chestnut, as the King's head
CAPE = dict(fill="#a4582c", light="#c07648", shadow="#7e3e1a", line="#2a0e02")
LINING = dict(fill="#efe0bc", light="#fbf2da", shadow="#cdb88c", line="#4a3818")     # the nut's pale base
COAT = dict(fill="#5ca844", light="#86c466", shadow="#3e7e2c", line="#122a0a")       # chestnut-leaf green
GOLD = dict(fill="#e6b63e", light="#f8de80", shadow="#ac7e1c", line="#4a3006")
BOOT = dict(fill="#5e3618", light="#7e522c", shadow="#40220c", line="#1a0a02")
BELT = dict(fill="#6a3a1a", light="#8a5632", shadow="#4a240c", line="#1e0a02")
LEAF = dict(fill="#7cb84a", light="#a6d878", shadow="#4e8a2a", line="#18320a")
NUT = dict(fill="#a24a1e", light="#c8703e", shadow="#74300e", line="#2a0c02")
IRIS = "#7a4a1e"


def far(t, k=0.88):
    def dk(h):
        r, g, b = (int(h[i:i + 2], 16) for i in (1, 3, 5))
        return "#%02x%02x%02x" % (int(r * k), int(g * k), int(b * k))
    return dict(fill=dk(t["fill"]), light=dk(t["light"]), shadow=dk(t["shadow"]), line=t["line"])


# ------------------------------------------------------------------ head --
EYE = (-2.3, 23.0)


def head(kind):
    name = {"open": "head", "blink": "head_blink", "talk": "head_talk", "happy": "head_happy"}[kind]
    c = R.card(name, (-6.0, 19.0, 4.0, 28.0))
    face = [(2.9, 21.6), (3.1, 23.8), (2.2, 25.6), (0.2, 26.4), (-2.2, 26.4), (-3.9, 25.3), (-4.4, 23.6),
            (-4.5, 22.6), (-5.1, 21.9), (-4.6, 21.2), (-4.4, 20.6), (-3.6, 19.7), (-2.2, 19.4), (0.2, 19.5),
            (1.9, 20.1)]
    ex, ey = EYE
    det = [f'<path d="{c.ell(-1.9, 21.4, 0.9, 0.5)}" fill="#f09a8a" fill-opacity="0.45" filter="url(#blush)"/>',
           stroke_svg(c.curve((0.5, 22.6), (1.4, 21.9), (0.6, 21.1)), SKIN["line"], 2.5)]   # ear
    if kind in ("open", "talk"):
        det += [fill_svg(c.ell(ex, ey, 0.66, 0.86, 12), "#ffffff"),
                fill_svg(c.ell(ex - 0.2, ey - 0.1, 0.46, 0.64, 12), IRIS),
                fill_svg(c.ell(ex - 0.24, ey - 0.14, 0.22, 0.34, 10), "#1a0e06"),
                fill_svg(c.ell(ex - 0.36, ey + 0.26, 0.13, 0.15, 8), "#ffffff"),
                stroke_svg(c.ell(ex, ey, 0.66, 0.86, 12), SKIN["line"], 1.6),
                stroke_svg(c.curve((ex - 0.7, ey + 0.62), (ex, ey + 0.95), (ex + 0.7, ey + 0.66)), "#2a1408", 2.4)]
    elif kind == "blink":
        det.append(stroke_svg(c.curve((ex - 0.7, ey + 0.1), (ex, ey - 0.4), (ex + 0.7, ey + 0.1)), "#2a1408", 3))
    else:   # happy: the eye squeezed up in a smile, for the embrace
        det.append(stroke_svg(c.curve((ex - 0.7, ey - 0.2), (ex, ey + 0.5), (ex + 0.7, ey - 0.2)), "#2a1408", 3))
    # a soft brow, raised a little in the middle: kind, not stern
    det.append(stroke_svg(c.curve((ex - 0.9, ey + 1.3), (ex - 0.1, ey + 1.75), (ex + 0.8, ey + 1.45)), HAIR["line"], 3.2))
    if kind == "talk":
        det += [fill_svg(c.ell(-3.6, 20.2, 0.45, 0.36, 10), "#7a1e24"),
                fill_svg(c.ell(-3.55, 20.05, 0.28, 0.12, 8), "#f07a8a")]
    elif kind == "happy":
        det.append(stroke_svg(c.curve((-4.2, 20.5), (-3.5, 19.8), (-2.7, 20.4)), "#7a2a2a", 2.6))
    else:
        det.append(stroke_svg(c.curve((-4.1, 20.3), (-3.5, 19.95), (-2.9, 20.3)), "#7a2a2a", 2.4))
    c.p.defs.append('<filter id="blush" x="-50%" y="-50%" width="200%" height="200%">'
                    '<feGaussianBlur stdDeviation="2"/></filter>')
    c.shape(face, SKIN, det, hl=(0.42, 0.22, 0.24, 0.14), seed=5, tension=0.6, lo=(4, 4), so=(5, 5))
    # a neat chestnut moustache under the nose
    c.shape([(-4.9, 21.4), (-4.2, 21.55), (-3.5, 21.4), (-3.1, 21.0), (-3.5, 20.9), (-4.1, 21.05), (-4.7, 20.85),
             (-5.1, 21.05)], HAIR, seed=8, tension=0.6, lo=(1.5, 1.5), so=(1.5, 1.5), soft=1, grow=1.2, inner=1.4)
    # chestnut hair: a side-swept fringe over the forehead, short at the back, a sideburn by the ear
    hair = [(-4.3, 24.4), (-4.1, 26.0), (-2.4, 27.3), (0.5, 27.6), (2.7, 26.8), (3.5, 24.8), (3.5, 22.2),
            (2.9, 20.4), (1.9, 20.6), (1.5, 21.4), (1.6, 22.6), (1.0, 24.0), (-0.2, 24.6), (-1.6, 25.2),
            (-2.8, 24.6), (-3.6, 24.0)]
    c.shape(hair, HAIR, [stroke_svg(c.curve((-2.6, 26.4), (-0.6, 26.0), (1.2, 26.6)), HAIR["shadow"], 2.2),
                         stroke_svg(c.curve((1.6, 26.4), (2.6, 25.0), (2.6, 22.6)), HAIR["shadow"], 2.2)],
            hl=(0.4, 0.22, 0.26, 0.18), seed=6, tension=0.6, lo=(3, 3), so=(4, 4))
    return c.save((0.0, 20.0), {"open": "the head: a kind brown eye under a soft brow, a neat chestnut moustache, "
                                        "chestnut hair swept over the forehead",
                                "blink": "eye shut", "talk": "mouth open",
                                "happy": "eye squeezed up in a smile (the embrace)"}[kind] +
                  "; pivot at the neck (0, 20)")


def crown():
    c = R.card("crown", (-5.0, 25.0, 4.0, 31.0), grow=1.4, inner=1.8, bias=(1.2, 1.4))
    # five chestnut leaves standing up from a gold band, fanned; long, pointed, serrated
    import math
    for i, (x, ang, L) in enumerate(((-3.6, 128, 3.0), (-2.0, 108, 3.6), (-0.4, 90, 4.0), (1.2, 72, 3.6),
                                     (2.6, 54, 3.0))):
        a = math.radians(ang)
        ux, uy = math.cos(a), math.sin(a)
        nx, ny = -uy, ux
        bx, by = x, 26.4
        pts = [(bx, by)]
        for k in range(1, 5):   # a serrated edge: little teeth up one side and down the other
            t = k / 5
            w = 0.55 * math.sin(math.pi * min(1.0, t * 1.15)) + 0.1
            pts.append((bx + ux * L * t + nx * w, by + uy * L * t + ny * w))
            pts.append((bx + ux * L * (t + 0.1) + nx * (w - 0.18), by + uy * L * (t + 0.1) + ny * (w - 0.18)))
        pts.append((bx + ux * L, by + uy * L))
        for k in range(4, 0, -1):
            t = k / 5
            w = 0.55 * math.sin(math.pi * min(1.0, t * 1.15)) + 0.1
            pts.append((bx + ux * L * (t + 0.1) - nx * (w - 0.18), by + uy * L * (t + 0.1) - ny * (w - 0.18)))
            pts.append((bx + ux * L * t - nx * w, by + uy * L * t - ny * w))
        tone = LEAF if i % 2 == 0 else dict(LEAF, fill="#6aa83e")
        c.shape(pts, tone, [stroke_svg(c.line([(bx, by), (bx + ux * L * 0.85, by + uy * L * 0.85)]), LEAF["shadow"], 1.4)],
                seed=20 + i, tension=0.2, jitter=0.1, lo=(2, 2), so=(2, 2), soft=1)
    band = [(-4.4, 26.2), (-2.0, 26.8), (1.0, 26.8), (3.4, 26.2), (3.2, 25.3), (1.0, 25.8), (-2.0, 25.8),
            (-4.2, 25.3)]
    c.shape(band, GOLD, hl=(0.4, 0.3, 0.3, 0.3), seed=2, tension=0.5, lo=(2, 2), so=(2, 2), soft=1)
    # one chestnut at the front of the band: glossy brown with its pale base and a little tip
    c.shape([(-1.6, 26.0), (-1.9, 26.9), (-1.2, 27.8), (-0.5, 28.1), (0.2, 27.8), (0.8, 26.9), (0.5, 26.0)], NUT,
            [fill_svg(c.ell(-0.55, 26.2, 1.0, 0.35, 8), LINING["fill"])], hl=(0.36, 0.28, 0.2, 0.18), hl_alpha=0.35,
            seed=3, tension=0.6, lo=(2, 2), so=(2, 2), soft=1)
    return c.save((-0.5, 25.8), "the crown of chestnut leaves: five serrated leaves fanned up from a gold band, "
                                "a chestnut at the front; pivot at its foot on the head")


# ------------------------------------------------------------------ body --
def cape():
    c = R.card("cape", (-4.0, 3.0, 9.0, 22.0))
    pts = [(-2.4, 20.6), (0.6, 21.4), (3.6, 20.8), (5.2, 17.0), (6.8, 11.0), (8.4, 4.6), (6.0, 4.0), (3.4, 4.4),
           (1.0, 4.0), (-1.2, 4.6), (-0.8, 10.0), (-1.6, 16.0)]
    lining = [(-1.2, 4.6), (-0.8, 10.0), (-1.6, 16.0), (-0.4, 16.4), (0.6, 10.4), (0.4, 4.3)]
    det = [fill_svg(c.d(lining, seed=31), LINING["fill"]),
           stroke_svg(c.curve((-0.2, 15.6), (0.4, 10.0), (0.2, 4.8)), LINING["shadow"], 2),
           stroke_svg(c.curve((3.0, 18.4), (4.6, 11.0), (5.0, 4.8)), CAPE["shadow"], 3),
           stroke_svg(c.curve((1.6, 17.6), (2.2, 11.0), (2.4, 4.6)), CAPE["shadow"], 2.5)]
    c.shape(pts, CAPE, det, hl=(0.42, 0.2, 0.24, 0.12), seed=30, tension=0.45)
    # the collar: a roll of the cream lining over the shoulders
    c.shape([(-2.8, 20.2), (-1.0, 21.6), (1.6, 22.0), (4.0, 21.2), (4.2, 20.0), (1.6, 20.4), (-1.0, 19.8)], LINING,
            hl=(0.4, 0.3, 0.3, 0.3), hl_alpha=0.6, seed=32, tension=0.5, lo=(3, 3), so=(3, 3))
    return c.save((0.6, 20.6), "the chestnut-brown cape, behind everything, its cream lining turned at the front "
                               "edge and rolled as a collar; pivot at the shoulders")


def coat():
    c = R.card("coat", (-6.0, 3.0, 6.0, 21.0))
    pts = [(-2.4, 20.4), (0.4, 20.8), (2.6, 20.2), (3.2, 17.6), (2.8, 13.6), (3.8, 10.0), (4.6, 6.4), (2.0, 5.8),
           (-0.4, 6.0), (-2.6, 5.8), (-4.8, 6.4), (-4.0, 10.0), (-3.0, 13.6), (-3.4, 17.6)]
    det = [stroke_svg(c.line([(-1.2, 20.4), (-1.6, 13.4), (-2.6, 6.0)]), GOLD["fill"], 5),   # the placket
           stroke_svg(c.line([(-1.2, 20.4), (-1.6, 13.4), (-2.6, 6.0)]), GOLD["shadow"], 1.4),
           stroke_svg(c.line([(-4.6, 6.6), (4.4, 6.6)]), GOLD["fill"], 5),                    # the hem's band
           stroke_svg(c.line([(-4.6, 6.95), (4.4, 6.95)]), GOLD["shadow"], 1.2),
           stroke_svg(c.curve((1.6, 12.6), (2.6, 9.4), (3.2, 6.8)), COAT["shadow"], 3),
           stroke_svg(c.curve((-0.6, 12.4), (-0.6, 9.4), (-0.2, 6.8)), COAT["shadow"], 2.5)]
    det += [fill_svg(c.ell(-1.3 - 0.06 * k, 19.2 - 1.6 * k, 0.32, 0.32, 8), NUT["fill"]) for k in range(3)]
    c.shape(pts, COAT, det, hl=(0.4, 0.2, 0.22, 0.14), seed=40, tension=0.45)
    c.shape([(-3.4, 14.2), (3.1, 14.2), (3.0, 12.8), (-3.2, 12.8)], BELT,
            [stroke_svg(c.line([(-3.0, 13.8), (2.8, 13.8)]), BELT["light"], 1.4)], seed=41, tension=0.3,
            lo=(2, 2), so=(2, 2), soft=1)
    c.shape([(-2.4, 14.5), (-0.8, 14.5), (-0.8, 12.5), (-2.4, 12.5)], GOLD,
            [fill_svg(c.ell(-1.6, 13.5, 0.36, 0.5, 8), BELT["fill"])], seed=42, tension=0.2,
            lo=(1, 1), so=(1, 1), soft=0.6, grow=1.2, inner=1.4)   # the buckle
    return c.save((0.0, 6.0), "the coat: chestnut-leaf green with a gold placket and hem, three chestnut buttons, "
                              "a brown belt with a gold buckle; root card, pivot (0, 6)")


def arm(front):
    name = "arm_front" if front else "arm_back"
    ox = 0.0 if front else 3.4
    c = R.card(name, (-6.0 + ox, 9.0, 1.0 + ox, 21.0))
    coat, gold, skin = (COAT, GOLD, SKIN) if front else (far(COAT), far(GOLD), far(SKIN))
    hand = [(x + ox, y) for x, y in [(-3.6, 12.4), (-4.6, 12.0), (-5.0, 11.0), (-4.4, 10.0), (-3.2, 10.0),
                                     (-2.6, 10.8), (-2.8, 11.8)]]
    c.shape(hand, skin, [stroke_svg(c.curve((-4.6 + ox, 11.0), (-4.0 + ox, 10.8), (-3.6 + ox, 11.2)),
                                    skin["shadow"], 1.6)],
            hl=(0.38, 0.25, 0.26, 0.2), seed=50 if front else 51, tension=0.55, lo=(2, 2), so=(2.5, 2.5))
    sleeve = [(x + ox, y) for x, y in [(-0.2, 20.2), (-1.8, 20.4), (-2.8, 18.8), (-3.4, 15.4), (-4.2, 12.6),
                                       (-2.4, 12.0), (-1.4, 14.6), (0.4, 17.6)]]
    c.shape(sleeve, coat, [stroke_svg(c.line([(-4.1 + ox, 13.0), (-2.4 + ox, 12.4)]), gold["fill"], 5),
                           stroke_svg(c.curve((-1.6 + ox, 19.6), (-2.2 + ox, 17.0), (-2.8 + ox, 14.0)),
                                      coat["shadow"], 2.5)],
            hl=(0.38, 0.2, 0.26, 0.14), seed=52 if front else 53, tension=0.55, lo=(3, 3), so=(4, 4))
    return c.save((-1.0 + ox, 19.2), ("the front arm" if front else "the back arm, a tone darker") +
                  ": a green sleeve with a gold cuff and a bare hand; pivot at the shoulder")


def boots():
    c = R.card("boots", (-6.0, 0.0, 4.0, 7.0))
    for x, t in ((1.6, far(BOOT)), (-1.6, BOOT)):
        pts = [(x - 0.9, 6.6), (x + 1.1, 6.6), (x + 1.2, 1.6), (x + 1.0, 0.2), (x - 2.6, 0.2), (x - 2.9, 1.0),
               (x - 2.0, 1.7), (x - 1.0, 2.0)]
        c.shape(pts, t, [stroke_svg(c.line([(x - 1.0, 5.6), (x + 1.1, 5.6)]), t["light"], 2.2)],
                hl=(0.4, 0.25, 0.3, 0.16), seed=60 + int(x), tension=0.35, lo=(3, 3), so=(3, 3))
    return c.save((0.0, 0.0), "two brown riding boots, the far one a tone darker, under the coat's hem; "
                              "pivot on the ground between them")


# --------------------------------------------------------------- preview --
ORDER = ["cape", "arm_back", "boots", "coat", "arm_front", "head", "crown"]


def preview():
    W, H = 1100, 330
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    poses = [("rest", {}), ("blink", {"head": "head_blink"}), ("talk", {"head": "head_talk"}),
             ("happy (embrace)", {"head": "head_happy"})]
    for i, (label, ov) in enumerate(poses):
        im = assemble(OUT, R.meta, ORDER, (150, 270), (75, 262), ov, bg=(0, 0, 0, 0))
        c.alpha_composite(im, (10 + i * 160, 36))
        d.rectangle([10 + i * 160, 14, 160 + i * 160, 28], fill=(225, 218, 205, 255))
        d.text((14 + i * 160, 16), label, fill=(40, 30, 30, 255))
    eclair = os.path.join(OUT, "..", "eclair")
    if os.path.exists(os.path.join(eclair, "parts.json")):   # beside Eclair, at the same scale
        import json
        meta = json.load(open(os.path.join(eclair, "parts.json")))["cards"]
        im = assemble(eclair, meta, ["hair", "arm_back", "feet", "dress", "arm_front", "head", "tiara"], (140, 230),
                      (70, 220), bg=(0, 0, 0, 0))
        c.alpha_composite(im, (660, 36 + 262 - 220))
        d.text((664, 16), "with Eclair (same scale)", fill=(40, 30, 30, 255))
        mar = assemble(OUT, R.meta, ORDER, (150, 270), (75, 262), {"head": "head_happy"}, bg=(0, 0, 0, 0))
        c.alpha_composite(mar, (720, 36))
    d.rectangle([0, 298, W, 300], fill=(180, 170, 150, 255))
    d.rectangle([900, 298 - 192, 904, 298], fill=(60, 140, 60, 255))
    d.text((910, 110), "Luigi's height", fill=(40, 90, 40, 255))
    d.text((10, 310), "King Marron restored (c_sl_marron), 1:1 at 8 texels/unit", fill=(40, 30, 30, 255))
    im = Image.open(os.path.join(OUT, "crown.png")).convert("RGBA")
    c.alpha_composite(im.resize((im.width * 2, im.height * 2), Image.NEAREST), (930, 140))
    d.text((930, 240), "crown x2", fill=(70, 50, 50, 255))
    return c


def build():
    clean(OUT)
    for k in ("open", "blink", "talk", "happy"):
        head(k)
    crown()
    cape()
    coat()
    arm(True)
    arm(False)
    boots()
    R.write_json({"draw_order": ORDER})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
