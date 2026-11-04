"""Princess Eclair (c_sl_eclair), the Waffle Kingdom's princess (Super Luigi ch6).

Run: python eclair.py -> ../eclair/*.svg, *.png, parts.json, preview.png
Card list from the models agent: dress, head, head_blink, head_talk, hair, tiara, arm_front,
arm_back, feet (+ tiara_glow: the seventh piece blazing, ch6's "raise the tiara").
ch6.md: a cream-and-gold gown, golden hair, a tiara with a curved golden plate set in it (the
seventh piece; the plate glows). Brave, quick, kind. Waffle Kingdom motifs: a waffle-grid panel
on the skirt, a whipped-cream ruffle hem, a chocolate-glaze sash (an eclair's icing).
8 texels/unit, facing -x, feet at y 0, about 25 units to the tiara's top.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, assemble, stroke_svg, fill_svg, smooth_path, ellipse_pts, star_pts, poly_path, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "eclair")
R = Rig("c_sl_eclair", OUT)

SKIN = dict(fill="#fcd8b8", light="#fff0e0", shadow="#e2a888", line="#5a2a1a")
GOWN = dict(fill="#f4e8c8", light="#fffaec", shadow="#d2bd8c", line="#4a3818")
WAFFLE = dict(fill="#e2ac52", light="#f4cf86", shadow="#b47c2a", line="#4a2a08")
CREAM = dict(fill="#fbf6ea", light="#ffffff", shadow="#d8cfbd", line="#4a3e2a")
CHOC = dict(fill="#6e3c22", light="#94603e", shadow="#4a2414", line="#1e0c04")
GOLD = dict(fill="#e6b63e", light="#f8de80", shadow="#ac7e1c", line="#4a3006")
HAIR = dict(fill="#f2c84a", light="#fce590", shadow="#c8941e", line="#462a04")
GLOVE = dict(fill="#fbf8f2", light="#ffffff", shadow="#d4cec4", line="#3a3640")
PIECE = dict(fill="#f2c040", light="#fff0a8", shadow="#c08a18", line="#4a3006")
IRIS = "#3a72d0"


def far(t, k=0.88):
    def dk(h):
        r, g, b = (int(h[i:i + 2], 16) for i in (1, 3, 5))
        return "#%02x%02x%02x" % (int(r * k), int(g * k), int(b * k))
    return dict(fill=dk(t["fill"]), light=dk(t["light"]), shadow=dk(t["shadow"]), line=t["line"])


# ------------------------------------------------------------------ head --
EYE = (-1.9, 18.9)


def head(kind):
    name = {"open": "head", "blink": "head_blink", "talk": "head_talk"}[kind]
    c = R.card(name, (-4.5, 15.0, 3.5, 23.0))
    face = [(2.4, 17.8), (2.6, 19.6), (1.8, 21.2), (0.2, 21.9), (-1.8, 21.9), (-3.2, 21.0), (-3.6, 19.6),
            (-3.7, 18.8), (-4.2, 18.2), (-3.8, 17.7), (-3.6, 17.0), (-3.0, 16.3), (-1.8, 16.0), (0.2, 16.1), (1.6, 16.6)]
    ex, ey = EYE
    det = [f'<path d="{c.ell(-1.7, 17.6, 0.9, 0.55)}" fill="#f59a9a" fill-opacity="0.65" filter="url(#blush)"/>',
           stroke_svg(c.curve((0.4, 18.6), (1.2, 18.0), (0.5, 17.3)), SKIN["line"], 2.5)]   # ear
    if kind in ("open", "talk"):
        det += [fill_svg(c.ell(ex, ey, 0.62, 0.85, 12), "#ffffff"),
                fill_svg(c.ell(ex - 0.2, ey - 0.08, 0.44, 0.66, 12), IRIS),
                fill_svg(c.ell(ex - 0.24, ey - 0.12, 0.22, 0.36, 10), "#14183a"),
                fill_svg(c.ell(ex - 0.36, ey + 0.28, 0.13, 0.15, 8), "#ffffff"),
                stroke_svg(c.ell(ex, ey, 0.62, 0.85, 12), SKIN["line"], 1.6),
                # lashes: a lid stroke and two flicks
                stroke_svg(c.curve((ex - 0.75, ey + 0.55), (ex, ey + 1.05), (ex + 0.7, ey + 0.6)), "#2a1408", 3),
                stroke_svg(c.line([(ex + 0.62, ey + 0.66), (ex + 1.0, ey + 0.95)]), "#2a1408", 2.2),
                stroke_svg(c.line([(ex + 0.3, ey + 0.86), (ex + 0.55, ey + 1.2)]), "#2a1408", 2.2)]
    else:
        det += [stroke_svg(c.curve((ex - 0.7, ey + 0.1), (ex, ey - 0.4), (ex + 0.7, ey + 0.1)), "#2a1408", 3),
                stroke_svg(c.line([(ex + 0.6, ey + 0.05), (ex + 0.95, ey + 0.3)]), "#2a1408", 2.2)]
    if kind == "talk":
        det += [fill_svg(c.ell(-2.9, 16.8, 0.42, 0.34, 10), "#8a2030"),
                fill_svg(c.ell(-2.85, 16.66, 0.24, 0.12, 8), "#f07a8a")]
    else:
        det.append(stroke_svg(c.curve((-3.3, 17.0), (-2.9, 16.7), (-2.4, 16.9)), "#a83a3a", 2.4))
    c.p.defs.append('<filter id="blush" x="-50%" y="-50%" width="200%" height="200%">'
                    '<feGaussianBlur stdDeviation="2"/></filter>')
    c.shape(face, SKIN, det, hl=(0.42, 0.22, 0.24, 0.14), seed=5, tension=0.6, lo=(4, 4), so=(5, 5))
    # the fringe over the forehead (the long hair is its own card behind)
    fringe = [(-3.6, 20.0), (-3.4, 21.4), (-2.0, 22.4), (0.4, 22.7), (2.2, 22.0), (2.9, 20.4), (3.0, 18.2),
              (2.4, 16.6), (1.5, 17.2), (1.4, 18.8), (1.0, 20.2), (0.0, 20.7), (-1.2, 20.5), (-2.5, 19.8)]
    c.shape(fringe, HAIR, [stroke_svg(c.curve((-1.6, 22.0), (-0.6, 21.4), (0.2, 21.8)), HAIR["shadow"], 2)],
            hl=(0.4, 0.25, 0.26, 0.2), seed=6, tension=0.6, lo=(3, 3), so=(4, 4))
    return c.save((0.0, 16.0), {"open": "the head: big blue eye with lashes, rosy cheek, golden fringe",
                                "blink": "eye shut", "talk": "mouth open"}[kind] + "; pivot at the neck (0, 16)")


def hair():
    c = R.card("hair", (-3.0, 7.0, 6.0, 23.0))
    pts = [(-2.4, 20.4), (-1.6, 22.4), (1.0, 22.8), (3.4, 21.8), (4.4, 19.0), (4.6, 15.0), (5.4, 11.4),
           (5.6, 8.4), (4.4, 7.6), (3.6, 9.0), (2.8, 8.0), (1.8, 9.4), (1.6, 13.0), (1.0, 16.4)]
    det = [stroke_svg(c.curve(a, b, e), HAIR["shadow"], 2.5) for a, b, e in
           (((2.0, 21.0), (3.6, 16.0), (3.4, 9.6)), ((3.2, 20.0), (4.6, 15.0), (4.6, 9.2)))]
    c.shape(pts, HAIR, det, hl=(0.42, 0.18, 0.24, 0.1), seed=7, tension=0.6)
    return c.save((0.5, 21.0), "long golden hair down the back, behind the head and dress; pivot at the crown")


def tiara(glow=False):
    name = "tiara_glow" if glow else "tiara"
    c = R.card(name, (-4.0, 21.0, 3.0, 26.0), grow=1.4, inner=1.8, bias=(1.2, 1.4))
    if glow:   # the plate blazing: a burst of light behind it
        burst = []
        for k in range(24):
            a = math.pi / 2 + k * math.pi / 12
            r = 2.4 if k % 2 == 0 else 1.6
            burst.append((-0.5 + r * math.cos(a), 23.7 + r * 0.9 * math.sin(a)))
        c.shape(burst, dict(fill="#ffe27a", light="#fff6c8", shadow="#f2b440", line="#4e2e04"), seed=1,
                tension=0.1, soft=1, lo=(2, 2), so=(2, 2), grow=1.2, inner=1.5, bias=(1, 1.2))
    band = [(-3.6, 22.0), (-2.0, 22.8), (1.0, 22.8), (2.6, 22.0), (2.4, 21.4), (1.0, 22.0), (-2.0, 22.0),
            (-3.4, 21.4)]
    c.shape(band, GOLD, seed=2, tension=0.5, lo=(2, 2), so=(2, 2), soft=1)
    # the seventh piece: a curved golden plate, a crescent set upright in the band
    plate = [(-2.2, 22.6), (-2.0, 23.8), (-1.2, 24.8), (-0.5, 25.1), (0.2, 24.8), (1.0, 23.8), (1.2, 22.6),
             (0.4, 23.4), (-0.5, 23.8), (-1.4, 23.4)]
    p = PIECE if not glow else dict(PIECE, fill="#ffd858", shadow="#e8a828")
    c.shape(plate, p, [fill_svg(c.ell(-0.5, 24.6, 0.18, 0.18, 6), "#ffffff")], hl=(0.38, 0.3, 0.2, 0.2),
            hl_alpha=0.45 if glow else 0.3, seed=3, tension=0.6, lo=(2, 2), so=(2, 2), soft=1)
    for x in (-2.8, 1.8):   # pearls on the band
        c.shape(ellipse_pts(0, 0, 1, 1, 8), dict(fill="#f6f0e4", shadow="#c8bca6", line="#3a3028"),
                d=c.ell(x, 22.4, 0.32, 0.32, 8), seed=4, soft=0.6, lo=(1, 1), so=(1, 1), grow=0.8, inner=1.2,
                bias=(0.6, 0.6))
    return c.save((-0.5, 22.2), ("the tiara blazing (raise the tiara)" if glow else
                                 "the tiara: a gold band with the seventh piece, a curved golden plate") +
                  "; pivot at its foot on the head")


# ------------------------------------------------------------------ body --
def dress():
    c = R.card("dress", (-7.5, 0.5, 7.5, 16.5))
    skirt = [(-2.0, 11.0), (2.4, 11.0), (4.0, 8.0), (5.8, 4.4), (7.0, 1.6)]
    for k in range(15):   # the whipped-cream ruffle hem, right to left
        skirt.append((7.0 - 14.0 * k / 14, 1.1 + (0.0 if k % 2 == 0 else 0.5)))
    skirt += [(-6.8, 1.8), (-5.4, 4.6), (-3.6, 8.0)]
    # the waffle panel down the front of the skirt
    panel = c.pts([(-1.6, 10.6), (1.6, 10.6), (3.6, 2.2), (-3.8, 2.2)])
    grid = []
    for k in range(1, 6):
        t = k / 6
        l = (panel[0][0] + (panel[3][0] - panel[0][0]) * t, panel[0][1] + (panel[3][1] - panel[0][1]) * t)
        r = (panel[1][0] + (panel[2][0] - panel[1][0]) * t, panel[1][1] + (panel[2][1] - panel[1][1]) * t)
        grid.append(f"M{l[0]:.1f} {l[1]:.1f} L{r[0]:.1f} {r[1]:.1f}")
    for k in range(1, 4):
        t = k / 4
        a = (panel[0][0] + (panel[1][0] - panel[0][0]) * t, panel[0][1])
        b = (panel[3][0] + (panel[2][0] - panel[3][0]) * t, panel[3][1])
        grid.append(f"M{a[0]:.1f} {a[1]:.1f} L{b[0]:.1f} {b[1]:.1f}")
    det = [fill_svg(poly_path(panel), WAFFLE["fill"]),
           stroke_svg(" ".join(grid), WAFFLE["shadow"], 3.2),
           stroke_svg(" ".join(grid), WAFFLE["light"], 1.0),
           stroke_svg(poly_path(panel), GOLD["line"], 2.5),
           stroke_svg(c.curve((3.0, 9.6), (4.6, 5.4), (5.4, 2.4)), GOWN["shadow"], 3),
           stroke_svg(c.curve((-3.0, 9.6), (-4.6, 5.4), (-5.2, 2.4)), GOWN["shadow"], 3),
           stroke_svg(c.line([(-6.6, 2.2), (6.8, 2.2)]), GOLD["fill"], 5),
           stroke_svg(c.line([(-6.6, 2.55), (6.8, 2.55)]), GOLD["shadow"], 1.2)]
    det += [fill_svg(c.ell(-6.0 + k * 0.95, 1.55, 0.36, 0.3, 8), CREAM["light"]) for k in range(14)]
    c.shape(skirt, GOWN, det, hl=(0.4, 0.2, 0.22, 0.12), hl_alpha=0.65, seed=8, tension=0.45)
    # the bodice, with a chocolate-glaze sash at the waist and a gold neckline
    bodice = [(-2.0, 15.8), (1.8, 15.8), (2.4, 13.4), (2.2, 11.0), (-2.2, 11.0), (-2.6, 13.4)]
    det = [stroke_svg(c.curve((-2.0, 15.6), (0.0, 14.8), (1.8, 15.6)), GOLD["fill"], 4),
           fill_svg(smooth_path(star_pts(*c.u(-0.1, 13.8), 4.5, 2.0), tension=0.1), GOLD["light"])]
    c.shape(bodice, GOWN, det, hl=(0.4, 0.22, 0.24, 0.14), hl_alpha=0.65, seed=9, tension=0.4, lo=(4, 4), so=(5, 5))
    sash = [(-2.6, 11.8), (2.6, 11.8), (2.8, 10.6), (-2.8, 10.6)]
    c.shape(sash, CHOC, [stroke_svg(c.curve((-2.4, 11.5), (0.0, 11.7), (2.4, 11.5)), CHOC["light"], 1.6)],
            seed=10, tension=0.3, lo=(2, 2), so=(2, 2), soft=1, hl=(0.4, 0.3, 0.3, 0.3))
    bow = [(2.4, 11.2), (3.8, 12.2), (4.2, 11.0), (3.6, 9.8), (2.4, 10.8)]
    c.shape(bow, CHOC, seed=11, tension=0.6, lo=(2, 2), so=(2, 2), soft=1)
    return c.save((0.0, 1.0), "the gown: cream bell skirt with a waffle-grid panel, gold trim, a whipped-cream "
                              "ruffle hem; bodice with a gold neckline and a chocolate-glaze sash, bow at the back; "
                              "root card, pivot (0, 1)")


def arm(front):
    name = "arm_front" if front else "arm_back"
    ox = 0.0 if front else 3.4
    rect = (-5.5, 7.0, 0.5, 16.0) if front else (-2.0, 7.0, 4.0, 16.0)
    c = R.card(name, rect)
    gown, glove, gold = (GOWN, GLOVE, GOLD) if front else (far(GOWN), far(GLOVE), far(GOLD))
    arm_pts = [(x + ox, y) for x, y in [(-1.4, 13.2), (-2.8, 12.2), (-3.9, 9.6), (-4.8, 8.0), (-3.9, 7.2),
                                        (-2.6, 8.0), (-1.4, 10.6), (-0.3, 12.6)]]
    c.shape(arm_pts, glove, [stroke_svg(c.curve((-3.6 + ox, 9.2), (-3.2 + ox, 9.0), (-2.6 + ox, 9.4)),
                                         glove["shadow"], 1.8)],
            hl=(0.38, 0.25, 0.22, 0.16), hl_alpha=0.6, seed=12 if front else 13, tension=0.5, lo=(2, 2), so=(2.5, 2.5))
    puff = [(x + ox, y) for x, y in [(-0.4, 15.4), (-1.8, 15.6), (-2.8, 14.6), (-3.0, 13.0), (-2.0, 12.2),
                                     (-0.6, 12.6), (0.0, 14.0)]]
    c.shape(puff, gown, [stroke_svg(c.curve((-2.6 + ox, 12.6), (-1.6 + ox, 12.0), (-0.6 + ox, 12.8)), gold["fill"], 4),
                         stroke_svg(c.curve((-1.8 + ox, 15.2), (-2.2 + ox, 14.0), (-1.6 + ox, 12.8)),
                                    gown["shadow"], 2.5)],
            hl=(0.38, 0.22, 0.26, 0.18), hl_alpha=0.65, seed=14 if front else 15, tension=0.6, lo=(3, 3), so=(4, 4))
    return c.save((-1.2 + ox, 14.8), ("the front arm" if front else "the back arm, a tone darker") +
                  ": a cream puff sleeve with gold trim and a long white glove; pivot at the shoulder")


def feet():
    c = R.card("feet", (-4.0, 0.0, 3.0, 2.0))
    for x, t in ((1.0, far(GOLD)), (-1.6, GOLD)):
        pts = [(x - 2.0, 0.9), (x - 1.4, 0.3), (x + 0.6, 0.25), (x + 1.0, 1.0), (x + 0.2, 1.6), (x - 1.4, 1.4)]
        c.shape(pts, t, [fill_svg(c.ell(x - 1.2, 1.0, 0.25, 0.2, 6), "#fff6c8")], seed=16 + int(x),
                tension=0.6, lo=(2, 2), so=(2, 2), grow=1.4, inner=1.8, hl=(0.4, 0.3, 0.3, 0.2))
    return c.save((-0.5, 0.8), "two gold slippers peeking from under the hem; pivot between them")


# --------------------------------------------------------------- preview --
ORDER = ["hair", "arm_back", "feet", "dress", "arm_front", "head", "tiara"]


def preview():
    W, H = 900, 300
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    poses = [("rest", {}), ("blink", {"head": "head_blink"}), ("talk", {"head": "head_talk"}),
             ("raise the tiara: tiara_glow", {"tiara": "tiara_glow"})]
    for i, (label, ov) in enumerate(poses):
        im = assemble(OUT, R.meta, ORDER, (140, 230), (70, 220), ov, bg=(0, 0, 0, 0))
        c.alpha_composite(im, (10 + i * 150, 40))
        d.rectangle([10 + i * 150, 18, 150 + i * 150, 32], fill=(225, 218, 205, 255))
        d.text((14 + i * 150, 20), label, fill=(40, 30, 30, 255))
    d.rectangle([0, 260, W, 262], fill=(180, 170, 150, 255))
    d.rectangle([640, 260 - 192, 644, 260], fill=(60, 140, 60, 255))
    d.text((650, 70), "Luigi's height", fill=(40, 90, 40, 255))
    d.text((10, 275), "Princess Eclair (c_sl_eclair), 1:1 at 8 texels/unit", fill=(40, 30, 30, 255))
    x = 680
    for n in ("tiara", "tiara_glow"):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
        c.alpha_composite(im, (x, 120))
        d.text((x, 210), n + " x2", fill=(70, 50, 50, 255))
        x += im.width + 10
    return c


def build():
    clean(OUT)
    for k in ("open", "blink", "talk"):
        head(k)
    hair()
    tiara()
    tiara(glow=True)
    dress()
    arm(True)
    arm(False)
    feet()
    R.write_json({"draw_order": ORDER})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
