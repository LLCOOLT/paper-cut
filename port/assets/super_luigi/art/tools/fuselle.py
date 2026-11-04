"""Fuselle (c_sl_fuselle), Plumpbelly's pink Bob-omb (Super Luigi ch2).

Run: python fuselle.py -> ../fuselle/*.svg, *.png, parts.json, preview.png
ch2.md, New assets 5: a Bob-omb with a pink body tint, a bow on the fuse, eyelashes. Drawn as a
card set the models agent can rig like the game's Bob-omb: the round body, a swappable face layer
(open, blink, hurt, talk-blush), the fuse cap with its wick and bow, the wind-up key, two feet and
the fuse spark. 8 texels/unit, facing -x, feet at y 0, about 21 units to the bow's top.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, assemble, stroke_svg, fill_svg, smooth_path, ellipse_pts, star_pts, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "fuselle")
R = Rig("c_sl_fuselle", OUT)

PINK = dict(fill="#ea8cb8", light="#f8b4d2", shadow="#c46892", line="#4c1430")
BOW = dict(fill="#d8304e", light="#ee6078", shadow="#a81c38", line="#4a0816")
METAL = dict(fill="#b4b6c2", light="#d8dae2", shadow="#8a8c9c", line="#262834")
WICK = dict(fill="#c8a46a", light="#e2c48c", shadow="#9a7a44", line="#3e2a10")
BRASS = dict(fill="#e6b440", light="#f6d680", shadow="#ae801e", line="#4a3006")
FOOT = dict(fill="#f0b434", light="#fad070", shadow="#c48618", line="#4e2c04")
EYE_LINE = "#6a2a48"
LASH = "#2a0a1c"
PUPIL = "#1a0c18"

BODY_C, BODY_R = (0.0, 9.5), 7.6


def far(t, k=0.88):
    def dk(h):
        r, g, b = (int(h[i:i + 2], 16) for i in (1, 3, 5))
        return "#%02x%02x%02x" % (int(r * k), int(g * k), int(b * k))
    return dict(fill=dk(t["fill"]), light=dk(t["light"]), shadow=dk(t["shadow"]), line=t["line"])


# ------------------------------------------------------------------ body --
def body():
    c = R.card("body", (-9.0, 1.0, 9.0, 19.0))
    cx, cy = BODY_C
    pts = ellipse_pts(cx, cy, BODY_R, BODY_R * 0.98, 18, wobble=0.015, seed=3)
    # a faint seam ring round the bomb's middle, as on the game's Bob-omb
    det = [stroke_svg(c.curve((-7.4, 8.4), (0.0, 6.4), (7.4, 8.4)), PINK["shadow"], 2.5)]
    c.shape(pts, PINK, det, hl=(0.40, 0.24, 0.24, 0.17), seed=4, tension=0.5, lo=(7, 7), so=(9, 9))
    return c.save((0.0, 2.0), "the round pink bomb body; root card, pivot at its foot (0, 2)")


EYES = [(-5.2, 11.6), (-2.4, 11.9)]   # near the front (-x): the back eye is the near one, both visible


def face(kind):
    name = {"open": "face", "blink": "face_blink", "hurt": "face_hurt", "talk": "face_talk"}[kind]
    c = R.card(name, (-8.0, 6.0, 0.0, 16.0))
    p = c.p
    for i, (ex, ey) in enumerate(EYES):
        rx, ry = (0.95, 1.6) if i else (0.8, 1.5)   # the far eye is a touch narrower (turned away)
        if kind in ("open", "talk"):
            p.raw(fill_svg(c.ell(ex, ey, rx, ry, 14), "#fbf4f8"))
            p.raw(fill_svg(c.ell(ex - 0.25 * rx, ey - 0.35, 0.27 * rx * 2 / 2 + 0.05, 0.6 * ry, 12), PUPIL))
            p.raw(fill_svg(c.ell(ex - 0.42 * rx, ey + 0.55, 0.14, 0.16, 8), "#ffffff"))
            p.raw(stroke_svg(c.ell(ex, ey, rx, ry, 14), EYE_LINE, 2.6))
            # lashes: three flicks off the top, swept back (+x)
            for k, (a, l) in enumerate(((-30, 0.9), (0, 1.0), (30, 0.9))):
                t = math.radians(90 - 35 + a * 0.9)
                bx, by = ex + 0.55 * rx * math.cos(t) + 0.2, ey + ry * 0.92 * math.sin(t)
                p.raw(stroke_svg(c.line([(bx, by), (bx + 0.45 * l, by + 0.55 * l)]), LASH, 2.6))
        elif kind == "blink":
            p.raw(stroke_svg(c.curve((ex - rx, ey - 0.1), (ex, ey - 0.8), (ex + rx, ey - 0.1)), LASH, 4.5))
            p.raw(stroke_svg(c.line([(ex + rx - 0.1, ey - 0.15), (ex + rx + 0.5, ey + 0.35)]), LASH, 2.6))
            p.raw(stroke_svg(c.line([(ex + 0.2, ey - 0.5), (ex + 0.5, ey - 0.0)]), LASH, 2.4))
        else:   # hurt: squeezed shut, > <
            s = 1 if i == 0 else -1
            a, b, e = (ex - 0.7 * s, ey + 0.8), (ex + 0.6 * s, ey), (ex - 0.7 * s, ey - 0.8)
            p.raw(stroke_svg(c.line([a, b, e]), LASH, 5))
    if kind == "talk":   # blush under the eyes, for the shy lines
        p.defs.append('<filter id="blush" x="-50%" y="-50%" width="200%" height="200%">'
                      '<feGaussianBlur stdDeviation="1.6"/></filter>')
        for ex, ey in EYES:
            p.raw(f'<path d="{c.ell(ex + 0.3, ey - 2.3, 0.85, 0.45, 10)}" fill="#e0406a" filter="url(#blush)"/>')
    return c.save((0.0, 6.0), {"open": "the eyes: white ovals with low dark pupils and lashes",
                               "blink": "eyes shut, lashes down", "hurt": "eyes squeezed shut (> <)",
                               "talk": "eyes open with a blush under them (the shy lines)"}[kind] +
                  "; a face layer over the body, pivot at (0, 6) like the body's centre line")


# ------------------------------------------------------------------ fuse --
def fuse():
    c = R.card("fuse", (-4.0, 15.0, 4.0, 24.0))
    cap = [(-2.0, 15.6), (2.0, 15.6), (2.2, 17.8), (1.6, 18.4), (-1.6, 18.4), (-2.2, 17.8)]
    c.shape(cap, METAL, [stroke_svg(c.line([(-1.9, 16.6), (2.0, 16.6)]), METAL["shadow"], 2.2)],
            hl=(0.38, 0.3, 0.3, 0.22), seed=5, tension=0.35, lo=(3, 3), so=(4, 4), soft=1.5)
    wick = [(-0.45, 18.0), (0.45, 18.0), (0.7, 19.8), (1.2, 22.4), (0.3, 22.6), (-0.2, 20.0)]
    c.shape(wick, WICK, [stroke_svg(c.line([(-0.2, 18.6), (0.4, 19.2), (0.0, 19.9), (0.7, 20.6)]),
                                    WICK["shadow"], 1.6)],
            seed=6, tension=0.4, lo=(1.5, 1.5), so=(2, 2), soft=0.8, grow=1.3, inner=1.6, bias=(1.1, 1.3))
    # the bow, tied round the wick just above the cap: two loops, a knot, two tails
    def sh(pts, dy=1.0, k=0.8):
        return [(x * k + 0.15, y + dy) for x, y in pts]
    for pts, s in ((sh([(-0.1, 19.2), (-1.6, 20.6), (-3.4, 20.4), (-3.4, 18.6), (-1.6, 18.2)]), 7),
                   (sh([(0.3, 19.2), (1.8, 20.8), (3.5, 20.6), (3.4, 18.6), (1.8, 18.2)]), 8),
                   (sh([(-0.4, 18.9), (-1.6, 16.8), (-0.9, 16.4), (0.1, 18.4)]), 9),
                   (sh([(0.4, 18.9), (1.9, 16.7), (2.4, 17.2), (0.8, 18.6)]), 10)):
        c.shape(pts, BOW, [stroke_svg(c.curve(pts[0], pts[2], pts[-1]), BOW["shadow"], 1.6)] if len(pts) == 5 else (),
                seed=s, tension=0.55, lo=(2, 2), so=(3, 3), soft=1, grow=1.5, inner=1.8, bias=(1.2, 1.4),
                hl=(0.4, 0.3, 0.3, 0.2) if len(pts) == 5 else None)
    c.shape(None, BOW, d=c.ell(0.25, 20.0, 0.6, 0.55, 10), seed=11, lo=(1, 1), so=(1.5, 1.5), soft=0.6,
            grow=1.3, inner=1.6, bias=(1, 1.2))
    return c.save((0.0, 16.0), "the metal fuse cap with the wick and a red bow tied round it; "
                               "pivot at the cap's foot, sunk into the body's top (0, 16)")


def spark():
    c = R.card("spark", (-2.0, -2.0, 2.0, 2.0), grow=1.3, inner=1.4, bias=(1.0, 1.2))
    burst = star_pts(*c.u(0, 0), 14, 6, n=6, rot=-90)
    c.p.shape(smooth_path(burst, tension=0.15, jitter=0.4, seed=12), "#ffd84a", "#5a2004", "#fff2a8", "#f29a20",
              (2, 2), (3, 3), soft=0.8)
    c.p.raw(fill_svg(smooth_path(ellipse_pts(*c.u(-0.2, 0.2), 4, 4, 8), tension=0.5), "#fff8dc"))
    return c.save((0.0, 0.0), "the lit fuse's spark; sits on the wick's tip (0.75, 22.5) and flickers (scale, spin)")


def key():
    c = R.card("key", (6.0, 6.0, 13.0, 15.0))
    shaft = [(6.2, 10.0), (9.4, 10.1), (9.4, 11.1), (6.2, 11.2)]
    c.shape(shaft, BRASS, seed=13, tension=0.2, lo=(2, 2), so=(2.5, 2.5), soft=1)
    for cy, s in ((12.6, 14), (8.6, 15)):   # two round wings of the key
        wing = ellipse_pts(*c.u(10.9, cy), 1.9 * 8, 1.75 * 8, 14, wobble=0.03, seed=s)
        c.p.shape(smooth_path(wing, tension=0.5, jitter=0.4, seed=s), BRASS["fill"], BRASS["line"], BRASS["light"],
                  BRASS["shadow"], (4, 4), (5, 5), soft=2,
                  details=[fill_svg(smooth_path(ellipse_pts(*c.u(11.1, cy), 0.75 * 8, 0.65 * 8, 10), tension=0.5),
                                    "#7a5410")], hl=(0.38, 0.28, 0.26, 0.18))
    c.shape([(9.0, 9.4), (10.3, 9.4), (10.3, 11.8), (9.0, 11.8)], BRASS, seed=16, tension=0.2, lo=(2, 2), so=(2, 2),
            soft=1, hl=(0.4, 0.3, 0.3, 0.25))
    return c.save((6.6, 10.6), "the wind-up key: spins round its shaft; pivot at the shaft's root, in the body's back")


def foot(front):
    name = "foot_front" if front else "foot_back"
    c = R.card(name, (-3.0, 0.0, 3.0, 3.0))
    t = FOOT if front else far(FOOT)
    pts = [(-2.6, 0.9), (-2.0, 0.25), (1.6, 0.25), (2.4, 0.9), (2.0, 2.2), (0.0, 2.7), (-1.8, 2.2)]
    c.shape(pts, t, hl=(0.4, 0.28, 0.28, 0.2), seed=17 if front else 18, tension=0.6, lo=(3, 3), so=(4, 4), soft=1.5)
    return c.save((0.0, 1.8), ("the front foot" if front else "the back foot, a tone darker") +
                  ": a round orange shoe; pivot at its top, under the body")


# --------------------------------------------------------------- preview --
ORDER = ["foot_back", "key", "fuse", "body", "face", "foot_front"]
REST = {"foot_back": (1.8, 1.4), "foot_front": (-1.8, 1.4), "key": (6.6, 10.6), "fuse": (0.0, 16.0),
        "body": (0.0, 2.0), "face": (0.0, 6.0), "spark": (0.75, 22.5)}


def preview():
    for n, xy in REST.items():
        R.meta[n]["model_xy"] = list(xy)
    W, H = 960, 300
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    poses = [("rest", {}, ORDER), ("blink", {"face": "face_blink"}, ORDER),
             ("hurt", {"face": "face_hurt"}, ORDER), ("talk (blush)", {"face": "face_talk"}, ORDER),
             ("fuse lit", {}, ORDER + ["spark"])]
    for i, (label, ov, order) in enumerate(poses):
        im = assemble(OUT, R.meta, order, (190, 220), (90, 210), ov, bg=(0, 0, 0, 0))
        c.alpha_composite(im, (10 + i * 172 - 10, 40))
        d.text((14 + i * 172, 20), label, fill=(40, 30, 30, 255))
    d.rectangle([0, 250, W, 252], fill=(180, 170, 150, 255))
    d.rectangle([900, 250 - 192, 904, 250], fill=(60, 140, 60, 255))
    d.text((910, 70), "Luigi's\nheight", fill=(40, 90, 40, 255))
    d.text((10, 275), "Fuselle (c_sl_fuselle), 1:1 at 8 texels/unit", fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    body()
    for k in ("open", "blink", "hurt", "talk"):
        face(k)
    fuse()
    spark()
    key()
    foot(True)
    foot(False)
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))
    R.write_json({"draw_order": ORDER, "rest_note": "model_xy is each card's pivot at rest"})


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
