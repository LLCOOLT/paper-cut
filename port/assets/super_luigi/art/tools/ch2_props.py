"""Chapter 2's smaller props (Super Luigi): the CONGRATULATIONS banner (three states), the brides'
bone cage and Sergeant Bulkhead's stripe.

Run: python ch2_props.py -> ../ch2_props/*.svg, *.png, parts.json, preview.png
ch2.md, New assets 6-7 (all optional, polish):
  - the banner over the mayor's door: "CONGRATULATIONS FUS" (half-painted), then "CONGRATULATIONS
    LUIGI" after the boss, then "CONGRATULATIONS LUIGI & FUS" after the proposal;
  - the brides' bone cage;
  - Sergeant Bulkhead: the Bulky Bob-omb at 1.4x "with a sergeant's stripe" (a badge card to pin
    on the game's model, so nothing is redrawn from the disc).
8 texels/unit, outlined paper props lit from the top left. The banner's letters are painted with a
lettering.py's hand-cut capitals (the game's cut-paper sign letters), each letter with a small tilt and bob
so it reads hand-painted.
"""
import os
import random
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, stroke_svg, fill_svg, smooth_path, ellipse_pts, clean  # noqa
import lettering as lt  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "ch2_props")
R = Rig("sl_ch2_props", OUT)

CLOTH = dict(fill="#ece0c2", light="#f8f0dc", shadow="#c8b48c", line="#3e2e14")
ROPE = dict(fill="#b48a52", light="#d2aa72", shadow="#8a6434", line="#3a240a")
PAINT = "#c42a36"
PAINT_DARK = "#7a1420"
BONE = dict(fill="#e4dcc4", light="#f6f0e0", shadow="#b8ac8a", line="#3a3020")
GOLD = dict(fill="#e6b63e", light="#f8de80", shadow="#ac7e1c", line="#4a3006")
BADGE = dict(fill="#3a5aa8", light="#5a7ac4", shadow="#283e80", line="#0e1638")


# ----------------------------------------------------------------- banner --
def letters(c, text, x0, x1, y, size, seed, painted=None, drip=False):
    """Set `text` centred between x0 and x1 (units) with its middle on line y, in lettering.py's
    hand-cut capitals. painted: how many letters are painted (the rest are left as pencil guides)."""
    rnd = random.Random(seed)
    U = c.U
    size = min(size, (x1 - x0) / (lt.width(text, 1.0) * 1.04))
    cx, cy = c.u((x0 + x1) / 2, y - 0.5 * size)   # px: the line's middle, then down to the baseline
    fs = size * U
    n = painted if painted is not None else len(text)
    done, rest = text[:n], text[n:]
    full = lt.width(text, fs)
    left = cx - full / 2
    out = []
    if done:
        kw = dict(anchor="start", seed=seed, jitter=0.35, tilt=5, bob=0.04)
        out.append(lt.text_svg(done, left, cy, fs, PAINT, shadow=PAINT_DARK, shadow_off=(1.4, 1.6), **kw))
        if drip:   # paint run down from a few letters
            x = left
            for ch in done:
                w = (lt.advance(ch) - lt.TRACK) * fs / lt.H
                if ch != " " and rnd.random() < 0.35:
                    dx = x + rnd.uniform(0.2, 0.8) * w
                    out.append(stroke_svg(f"M{dx:.1f} {cy - 1:.1f} L{dx:.1f} {cy + rnd.uniform(4, 9):.1f}", PAINT, 2.2))
                x += lt.advance(ch) * fs / lt.H
    if rest.strip():   # not painted yet: a faint pencil guide
        x = left + lt.width(done, fs) + lt.TRACK * fs / lt.H if done else left
        out.append(lt.outline_svg(rest, x, cy, fs, "#a89878", 1.2, anchor="start", seed=seed + 1, jitter=0.35,
                                  tilt=5, bob=0.04))
    return out


def banner(name, text, painted=None, seed=1, brush=False):
    c = R.card(name, (-32.0, -6.0, 32.0, 5.0))
    # the cloth: a long strip that sags a little in the middle, with notched swallowtail ends
    top = [(-29.0, 3.4), (-14.0, 2.9), (0.0, 2.7), (14.0, 2.9), (29.0, 3.4)]
    bot = [(29.0, -3.8), (14.0, -4.4), (0.0, -4.6), (-14.0, -4.4), (-29.0, -3.8)]
    pts = top + [(30.6, 1.4), (29.6, -0.2), (30.8, -2.0)] + bot + [(-30.8, -2.0), (-29.6, -0.2), (-30.6, 1.4)]
    det = letters(c, text, -27.0, 27.0, -0.9, 5.2, seed, painted, drip=True)
    det += [stroke_svg(c.curve((-28.6, 2.4), (0.0, 1.6), (28.6, 2.4)), CLOTH["shadow"], 1.6),
            stroke_svg(c.curve((-28.6, -3.0), (0.0, -3.7), (28.6, -3.0)), CLOTH["shadow"], 1.6)]
    if brush:   # a dropped brush stroke after the last painted letter (it's half-done)
        det.append(stroke_svg(c.curve((19.0, -1.6), (21.5, -0.2), (24.0, -1.9)), PAINT, 3.5))
    c.shape(pts, CLOTH, det, hl=(0.36, 0.3, 0.22, 0.16), hl_alpha=0.6, seed=seed, tension=0.35, jitter=0.4,
            lo=(5, 5), so=(6, 6))
    for x in (-30.4, 30.4):   # rope loops at the ends, to tie over the door
        c.shape(ellipse_pts(0, 0, 1, 1, 10), ROPE, d=c.ell(x, 3.4, 1.0, 1.0, 10), seed=seed + 3, lo=(2, 2),
                so=(2, 2), soft=1, grow=1.4, inner=1.6, bias=(1.1, 1.3),
                details=[fill_svg(c.ell(x, 3.4, 0.4, 0.4, 8), "#5a3a14")])
    return c.save((0.0, 3.4), f'the banner: "{text}"' + (" (half-painted)" if painted is not None else "") +
                  "; pivot at the middle of its top edge, under the door's lintel")


# --------------------------------------------------------------- the cage --
def bone(c, a, b, w, seed):
    """A bone from a to b (units): a bar with two knuckle knobs at each end."""
    import math
    (ax, ay), (bx, by) = a, b
    L = math.hypot(bx - ax, by - ay)
    ux, uy = (bx - ax) / L, (by - ay) / L
    nx, ny = -uy, ux
    pts = []
    for (px, py), s in ((a, -1), (b, 1)):
        # two knobs at this end
        for k in (1, -1):
            cx, cy = px + nx * k * w * 0.55 + ux * s * w * 0.2, py + ny * k * w * 0.55 + uy * s * w * 0.2
            pts.append((cx, cy))
    pa1, pa2, pb1, pb2 = pts
    shape = [(ax + nx * w * 0.35 + ux * w * 0.6, ay + ny * w * 0.35 + uy * w * 0.6),
             (pa1[0] - ux * w * 0.5, pa1[1] - uy * w * 0.5), (ax - ux * w * 0.55, ay - uy * w * 0.55),
             (pa2[0] - ux * w * 0.5, pa2[1] - uy * w * 0.5),
             (ax - nx * w * 0.35 + ux * w * 0.6, ay - ny * w * 0.35 + uy * w * 0.6),
             (bx - nx * w * 0.35 - ux * w * 0.6, by - ny * w * 0.35 - uy * w * 0.6),
             (pb2[0] + ux * w * 0.5, pb2[1] + uy * w * 0.5), (bx + ux * w * 0.55, by + uy * w * 0.55),
             (pb1[0] + ux * w * 0.5, pb1[1] + uy * w * 0.5),
             (bx + nx * w * 0.35 - ux * w * 0.6, by + ny * w * 0.35 - uy * w * 0.6)]
    c.shape(shape, BONE, seed=seed, tension=0.55, jitter=0.25, lo=(3, 3), so=(4, 4), soft=1.5,
            grow=1.6, inner=2.0, bias=(1.3, 1.5), hl=(0.4, 0.3, 0.3, 0.2), hl_alpha=0.6)


def cage(front):
    name = "cage_front" if front else "cage_back"
    c = R.card(name, (-12.0, -1.0, 12.0, 28.0))
    if not front:   # the far bars and the floor ring, a tone darker
        global BONE
        keep = BONE
        BONE = dict(fill="#c9c0a6", light="#dcd4bc", shadow="#a0946e", line="#3a3020")
    xs = (-7.5, -2.5, 2.5, 7.5) if front else (-5.0, 0.0, 5.0)
    import math
    for i, x in enumerate(xs):   # ribs curving in to the crown, like a bird cage
        top = (x * 0.25, 24.0)
        mid = (x * 1.25, 14.0)
        bone(c, (x * 1.15, 1.5), (mid[0], mid[1]), 1.5, 50 + i + (0 if front else 10))
        bone(c, (mid[0], mid[1]), top, 1.4, 60 + i + (0 if front else 10))
    if front:   # a hoop round the middle and the base ring, then the skull on the crown
        bone(c, (-9.6, 8.0), (0.0, 7.2), 1.5, 70)
        bone(c, (0.0, 7.2), (9.6, 8.0), 1.5, 71)
        bone(c, (-10.4, 1.0), (10.4, 1.0), 1.7, 72)
        skull = [(-2.4, 24.0), (-2.6, 26.2), (-1.4, 27.4), (1.4, 27.4), (2.6, 26.2), (2.4, 24.0), (1.4, 23.0),
                 (1.2, 22.0), (-1.2, 22.0), (-1.4, 23.0)]
        det = [fill_svg(c.ell(-1.0, 25.0, 0.7, 0.75, 10), "#3a3020"), fill_svg(c.ell(1.0, 25.0, 0.7, 0.75, 10), "#3a3020"),
               fill_svg(c.ell(0.0, 23.6, 0.3, 0.35, 8), "#3a3020"),
               stroke_svg(c.line([(-0.8, 22.1), (-0.8, 22.8)]) + " " + c.line([(0.0, 22.1), (0.0, 22.8)]) + " " +
                          c.line([(0.8, 22.1), (0.8, 22.8)]), "#3a3020", 1.8)]
        c.shape(skull, BONE, det, seed=73, tension=0.55, lo=(4, 4), so=(5, 5), hl=(0.4, 0.25, 0.3, 0.2), hl_alpha=0.6)
    else:
        bone(c, (-9.0, 2.6), (9.0, 2.6), 1.5, 74)
        BONE = keep
    return c.save((0.0, 0.0), ("the cage's near bars: ribs, a hoop, the base ring and a skull on the crown"
                               if front else "the far bars, a tone darker, behind the bride") +
                  "; pivot at the base's middle (0, 0)")


# ------------------------------------------------------------- the stripe --
def stripe():
    c = R.card("sergeant_stripe", (-3.0, -3.5, 3.0, 3.5), grow=1.5, inner=1.8, bias=(1.2, 1.4))
    shield = [(-2.4, 2.6), (2.4, 2.6), (2.5, -0.6), (0.0, -2.9), (-2.5, -0.6)]
    det = []
    for k in range(3):   # three gold chevrons
        y = 1.6 - k * 1.25
        det.append(stroke_svg(c.line([(-1.8, y - 0.9), (0.0, y), (1.8, y - 0.9)]), GOLD["line"], 6.5, cap="butt"))
        det.append(stroke_svg(c.line([(-1.75, y - 0.9), (0.0, y), (1.75, y - 0.9)]), GOLD["fill"], 3.6, cap="butt"))
    c.shape(shield, BADGE, det, seed=80, tension=0.3, lo=(3, 3), so=(4, 4), soft=1.5, hl=(0.38, 0.22, 0.3, 0.16))
    return c.save((0.0, 0.0), "Sergeant Bulkhead's stripe: a blue badge with three gold chevrons; pin it on the "
                              "Bulky Bob-omb's body (front, upper left of its centre) at the 1.4x model's scale")


def preview():
    W, H = 560, 560
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    y = 12
    for n in ("banner_fus", "banner_luigi", "banner_luigi_fus"):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im, (10, y))
        y += im.height + 6
    cb = Image.open(os.path.join(OUT, "cage_back.png")).convert("RGBA")
    cf = Image.open(os.path.join(OUT, "cage_front.png")).convert("RGBA")
    c.alpha_composite(cb, (40, 540 - cb.height + 8))
    c.alpha_composite(cf, (40, 540 - cf.height + 8))
    d.text((240, 520), "the bone cage: cage_back + cage_front", fill=(40, 30, 30, 255))
    s = Image.open(os.path.join(OUT, "sergeant_stripe.png")).convert("RGBA")
    c.alpha_composite(s.resize((s.width * 2, s.height * 2), Image.NEAREST), (320, 420))
    d.text((380, 450), "sergeant_stripe x2", fill=(40, 30, 30, 255))
    d.rectangle([0, 540, W, 542], fill=(180, 170, 150, 255))
    d.rectangle([250, 540 - 192, 254, 540], fill=(60, 140, 60, 255))
    d.text((262, 360), "Luigi's height", fill=(40, 90, 40, 255))
    return c


def build():
    clean(OUT)
    banner("banner_fus", "CONGRATULATIONS FUSELLE", painted=19, seed=1)
    banner("banner_luigi", "CONGRATULATIONS LUIGI", seed=2)
    banner("banner_luigi_fus", "CONGRATULATIONS LUIGI & FUS", seed=3)
    cage(False)
    cage(True)
    stripe()
    R.write_json({"cage_draw_order": ["cage_back", "(the bride)", "cage_front"]})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
