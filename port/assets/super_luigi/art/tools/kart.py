"""Big Green 01 (c_lp_kart), Luigi's race kart (Super Luigi ch3): its drawn plates, puff, smoke and backfire.

Run: python kart.py -> ../kart/plate_*, exhaust_puff, smoke_puff, flame (*.svg, *.png), their entries in parts.json
The kart itself is the style model's now (cut_kart.py, kart_cards.py, which also writes preview.png); the drawn body,
nose, seat, engine, wheels and steering wheel below are kept for reference and no longer built.
Card list from the models agent: kart_body, kart_nose, kart_seat, kart_engine, wheel_near_front,
wheel_near_back, wheel_far, steering_wheel, plate_01, exhaust_puff, smoke_puff, flame; recolours of
kart_body/kart_nose/kart_seat (_red _blue _yellow _purple _orange _white _black); kart_body_wrecked,
kart_nose_wrecked (and _red .. _black). 8 texels/unit, facing -x, about 34 units long and 12 tall.
Kart model origin: on the ground, midway between the axles. Each card covers a rectangle in model
units (RECTS); px = (x - x0) * 8, py = (y1 - y) * 8.
"""
import json
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, smooth_path, poly_path, ellipse_pts, star_pts, stroke_svg, fill_svg, clean  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), "..", "kart")
U = 8

# body paints: base, light, shadow, line, seat, stripe
PAINTS = {
    "green": ("#42b640", "#7fd66c", "#2a7f28", "#12380f", "#2c6f2a", "#f4f2e8"),
    "red": ("#d93a30", "#f2786a", "#9c2018", "#3e0a06", "#8a2018", "#f4f2e8"),
    "blue": ("#3a6ad8", "#7aa0f0", "#22449c", "#0c1a44", "#24429a", "#f4f2e8"),
    "yellow": ("#f0c43a", "#fae38a", "#b88a18", "#4a3606", "#b07e14", "#2f2f36"),
    "purple": ("#9a4ac0", "#c486e0", "#6a2c8a", "#2a0e3a", "#62287e", "#f4f2e8"),
    "orange": ("#ee8a2a", "#f8b872", "#b0581a", "#4a2206", "#a85216", "#f4f2e8"),
    "white": ("#e8e7e2", "#ffffff", "#b6b4c0", "#34323e", "#9c9aa6", "#d93a30"),
    "black": ("#3c3c46", "#686876", "#24242c", "#0c0c10", "#2a2a32", "#f0c43a"),
}
METAL = dict(fill="#b9bec8", light="#e8ebf2", shadow="#848a97", line="#24262d")
TIRE = dict(fill="#34343c", light="#56565f", shadow="#1e1e24", line="#0e0e12")
RUBBER = dict(fill="#3a3a42", light="#5a5a64", shadow="#24242a", line="#101014")
PLATE = dict(fill="#f4f2e8", light="#ffffff", shadow="#cfccbf", line="#24262d")
INK = "#1c1c22"
SOOT = "#2a2622"
SMOKE_D = dict(fill="#6a6670", light="#8e8a94", shadow="#4a4650", line="#1e1c22")
FIRE = dict(fill="#ff8a1e", light="#ffd84a", shadow="#e0501a", line="#5a1c04")
PUFF = dict(fill="#e6e4ee", light="#ffffff", shadow="#b0adbd", line="#3a3844")

# card rectangles in model units (x0, y0, x1, y1), sized to multiples of 8 texels
RECTS = {
    "kart_nose": (-17.5, 0.5, -7.5, 7.5),
    "kart_body": (-10.0, 0.5, 13.0, 8.5),
    "kart_seat": (1.5, 3.0, 7.5, 12.0),
    "kart_engine": (7.5, 2.5, 17.5, 11.5),
}
WHEELS = {"wheel_near_front": (-11.0, 3.2), "wheel_near_back": (11.0, 3.2)}
FAR = [(-10.4, 3.3), (11.6, 3.3)]
meta = {}


def note(name, w, h, pivot, at, desc):
    meta[name] = {"size": [w, h], "pivot": [round(pivot[0], 1), round(pivot[1], 1)],
                  "model_xy": [round(at[0], 2), round(at[1], 2)], "desc": desc}


def paint(colour):
    b, l, s, line, seat, stripe = PAINTS[colour]
    return dict(fill=b, light=l, shadow=s, line=line, seat=seat, stripe=stripe)


class Card:
    def __init__(self, name, rect, **kw):
        x0, y0, x1, y1 = rect
        self.x0, self.y1 = x0, y1
        self.w, self.h = round((x1 - x0) * U), round((y1 - y0) * U)
        self.p = Part(name, self.w, self.h, **({"grow": 1.8, "inner": 2.2, "bias": (1.6, 1.8)} | kw))
        self.name = name

    def u(self, x, y):
        return ((x - self.x0) * U, (self.y1 - y) * U)

    def pts(self, pts):
        return [self.u(x, y) for x, y in pts]

    def line(self, pts):
        return "M" + " L".join("%.1f %.1f" % self.u(x, y) for x, y in pts)

    def shape(self, pts, tone, details=(), hl=None, soft=3, tension=0.4, seed=1, jitter=0.5, lo=(6, 6),
              so=(8, 8), **kw):
        self.p.shape(smooth_path(self.pts(pts), tension=tension, jitter=jitter, seed=seed), tone["fill"],
                     tone["line"], tone.get("light"), tone.get("shadow"), lo, so, details=details, soft=soft, hl=hl,
                     **kw)

    def save(self, pivot_xy, desc):
        note(self.name, self.w, self.h, self.u(*pivot_xy), pivot_xy, desc)
        return self.p.save(OUT)


def sfx(colour):
    return "" if colour == "green" else "_" + colour


def soot(c, spots):
    return [f'<path d="{smooth_path(ellipse_pts(*c.u(x, y), rx * U, ry * U, 9, wobble=0.3, seed=int(x * 10)))}" '
            f'fill="{SOOT}" fill-opacity="0.75"/>' for x, y, rx, ry in spots]


# ------------------------------------------------------------------- body --
def kart_body(colour, wrecked=False):
    t = paint(colour)
    name = "kart_body" + ("_wrecked" if wrecked else "") + sfx(colour)
    c = Card(name, RECTS["kart_body"])
    jit, seed = (0.5, 3) if not wrecked else (1.6, 13)
    pts = [(-9.6, 1.2), (-9.8, 6.0), (-6.0, 6.8), (-2.6, 7.7), (-1.0, 7.1), (1.0, 6.4), (7.0, 6.4), (10.0, 6.8),
           (12.6, 6.3), (12.8, 1.6), (9.0, 1.0), (-6.0, 1.0)]
    if wrecked:
        pts = [(-9.4, 1.4), (-9.2, 5.2), (-8.0, 5.0), (-6.0, 6.4), (-2.6, 7.2), (-1.0, 6.6), (1.0, 6.2),
               (7.0, 6.0), (9.4, 6.6), (10.6, 5.6), (12.6, 6.0), (12.4, 1.8), (9.0, 1.2), (-6.0, 1.0)]
    det = [stroke_svg(c.line([(-9.6, 4.0), (-2.0, 4.2), (12.6, 4.0)]), t["stripe"], 7),
           stroke_svg(c.line([(-9.6, 4.0), (-2.0, 4.2), (12.6, 4.0)]), t["shadow"], 1.5),
           stroke_svg(c.line([(-2.4, 7.2), (-1.0, 1.4)]), t["line"], 2.5),
           stroke_svg(c.line([(8.6, 6.4), (8.2, 1.4)]), t["line"], 2.5)]
    det += [fill_svg(smooth_path(ellipse_pts(*c.u(x / 2, 1.8), 1.8, 1.8, 6)), t["line"]) for x in range(-18, 26, 4)]
    if wrecked:
        det += soot(c, [(-8.0, 4.0, 2.2, 1.2), (10.4, 5.0, 2.0, 1.4), (2.0, 2.0, 2.6, 0.7)])
        det += [stroke_svg(c.line(seg), t["line"], 2.5) for seg in
                ([(-6.0, 6.4), (-5.0, 5.0), (-5.8, 3.6)], [(5.0, 6.0), (6.2, 4.8), (5.4, 3.4)], [(0.0, 6.2), (0.6, 5.4)])]
    c.shape(pts, t, det, hl=(0.36, 0.22, 0.24, 0.14), seed=seed, jitter=jit)
    return c.save((0, 0), f"side panel and chassis ({colour})" + (", wrecked: dents, scorch" if wrecked else "") +
                  "; in front of Luigi's legs, which it hides (top edge y 6.4 at the seat). Pivot = kart origin")


def kart_nose(colour, wrecked=False):
    t = paint(colour)
    name = "kart_nose" + ("_wrecked" if wrecked else "") + sfx(colour)
    c = Card(name, RECTS["kart_nose"])
    jit, seed = (0.5, 4) if not wrecked else (1.5, 14)
    pts = [(-16.4, 2.0), (-16.8, 3.4), (-15.4, 4.8), (-12.0, 5.7), (-8.0, 6.3), (-7.6, 6.0), (-7.6, 1.6),
           (-12.0, 1.2)]
    if wrecked:
        pts = [(-15.8, 2.2), (-15.6, 3.0), (-14.6, 3.4), (-14.8, 4.4), (-12.0, 5.2), (-8.0, 6.0), (-7.6, 5.8),
               (-7.6, 1.6), (-12.0, 1.4)]
    det = [stroke_svg(c.line([(-16.4, 3.4), (-12.0, 3.9), (-7.6, 4.0)]), t["stripe"], 7),
           stroke_svg(c.line([(-16.4, 3.4), (-12.0, 3.9), (-7.6, 4.0)]), t["shadow"], 1.5),
           fill_svg(smooth_path(star_pts(*c.u(-11.5, 5.0), 6, 2.8), tension=0.1), t["stripe"])]
    if wrecked:
        det += soot(c, [(-14.6, 3.2, 1.8, 1.2), (-10.0, 2.2, 1.6, 0.6)])
        det.append(stroke_svg(c.line([(-13.0, 5.4), (-12.4, 4.4), (-13.2, 3.4)]), t["line"], 2.5))
    c.shape(pts, t, det, hl=(0.42, 0.25, 0.24, 0.14), seed=seed, jitter=jit)
    bump = [(-17.3, 1.2), (-16.2, 1.2), (-15.9, 3.8), (-17.1, 3.8)] if not wrecked else \
        [(-16.8, 0.8), (-15.6, 1.2), (-15.8, 3.0), (-16.9, 2.6)]
    c.shape(bump, RUBBER, seed=seed + 1, jitter=jit, lo=(2, 2), so=(3, 3), tension=0.25)
    return c.save((-8.0, 3.5), f"nose cone and front bumper ({colour})" + (", wrecked: crumpled, scorched" if wrecked
                                                                          else "") +
                  "; behind kart_body, whose front edge covers the join. Pivot at the join (-8, 3.5)")


def kart_seat(colour):
    t = paint(colour)
    c = Card("kart_seat" + sfx(colour), RECTS["kart_seat"])
    seat = dict(fill=t["seat"], light=t["fill"], shadow=t["line"], line=t["line"])
    pts = [(2.0, 4.0), (2.3, 10.4), (3.4, 11.6), (6.0, 11.6), (7.0, 10.4), (7.0, 4.0)]
    det = [stroke_svg(c.line([(2.6, y), (4.6, y + 0.4), (6.6, y)]), t["line"], 2.5) for y in (9.6, 7.8, 6.0)]
    c.shape(pts, seat, det, hl=(0.36, 0.2, 0.24, 0.12), seed=5)
    return c.save((4.5, 4.0), f"padded seat back ({colour}), behind Luigi; pivot at its foot (4.5, 4)")


def kart_engine():
    c = Card("kart_engine", RECTS["kart_engine"])
    pipe = c.line([(12.0, 8.6), (13.4, 10.2), (15.6, 10.8)])
    for col, wd in ((METAL["line"], 14), (METAL["shadow"], 9), (METAL["fill"], 5)):
        c.p.raw(stroke_svg(pipe, col, wd))
    tip = c.u(15.6, 10.8)
    c.p.raw(fill_svg(smooth_path(ellipse_pts(*tip, 7, 7, 10)), METAL["line"]))
    c.p.raw(fill_svg(smooth_path(ellipse_pts(*tip, 4, 4, 8)), "#0c0c10"))
    block = [(8.2, 5.8), (8.4, 9.0), (13.6, 9.2), (14.8, 8.0), (14.6, 5.8)]
    det = [stroke_svg(c.line([(x / 2, 6.4), (x / 2, 8.6)]), METAL["shadow"], 3) for x in range(19, 29, 2)]
    c.shape(block, METAL, det, hl=(0.36, 0.22, 0.26, 0.16), seed=6, lo=(4, 4), so=(6, 6))
    frame = [(8.0, 3.0), (16.4, 3.0), (16.6, 6.0), (8.0, 6.2)]
    c.shape(frame, dict(METAL, fill="#9aa0ac"), [stroke_svg(c.line([(9.0, 4.6), (15.6, 4.6)]), METAL["line"], 2)],
            seed=7, tension=0.2, lo=(3, 3), so=(4, 4), hl=(0.4, 0.25, 0.3, 0.2))
    c.shape([(16.0, 2.6), (17.3, 2.8), (17.3, 6.4), (16.0, 6.6)], RUBBER, seed=8, tension=0.2, lo=(2, 2), so=(3, 3))
    return c.save((12.0, 6.0), "engine block, rear frame, rear bumper and the exhaust pipe (tip at (15.6, 10.8)); "
                               "behind kart_body's back edge. Pivot (12, 6)")


def wheel(name, xy, far=False):
    p = Part(name, 56, 56, grow=1.6, inner=2.0, bias=(1.4, 1.6))
    cx = cy = 28
    tire = TIRE if not far else dict(TIRE, fill="#2a2a30", light="#44444c")
    r = 24.5
    p.shape(smooth_path(ellipse_pts(cx, cy, r, r, 16, wobble=0.01, seed=4), tension=0.5, jitter=0.3),
            tire["fill"], tire["line"], tire["light"], tire["shadow"], (5, 5), (6, 6), soft=2.5,
            details=[stroke_svg(smooth_path(ellipse_pts(cx, cy, 20, 20, 14)), tire["shadow"], 2)] +
                    [stroke_svg(f"M{cx + 21 * math.cos(a)} {cy + 21 * math.sin(a)} L{cx + 24 * math.cos(a)} "
                                f"{cy + 24 * math.sin(a)}", tire["line"], 2.5)
                     for a in [k * math.pi / 6 for k in range(12)]], hl=(0.38, 0.22, 0.22, 0.12))
    rim = METAL if not far else dict(METAL, fill="#9aa0ac")
    p.shape(smooth_path(ellipse_pts(cx, cy, 13, 13, 14), tension=0.5), rim["fill"], rim["line"], rim["light"],
            rim["shadow"], (3, 3), (4, 4), soft=1.5,
            details=[stroke_svg(f"M{cx} {cy - 11} L{cx} {cy + 11} M{cx - 11} {cy} L{cx + 11} {cy}", METAL["shadow"], 2)],
            grow=1.2, inner=1.6, bias=(1, 1.2), hl=(0.38, 0.26, 0.24, 0.16))
    p.shape(smooth_path(ellipse_pts(cx, cy, 6.5, 6.5, 12), tension=0.5), "#3a3a42", "#101014", "#5a5a64", None,
            (2, 2), grow=1.0, inner=1.4, bias=(0.8, 1),
            details=[fill_svg(smooth_path(star_pts(cx, cy, 4.2, 2.0), tension=0.1), "#f0c43a")])
    note(name, 56, 56, (28, 28), xy, ("the far-side wheel, a tone darker, behind everything; reused at both axles"
                                     if far else "spins about its centre"))
    return p.save(OUT)


def steering():
    p = Part("steering_wheel", 40, 48, grow=1.4, inner=1.8, bias=(1.2, 1.4))
    for col, wd in ((METAL["line"], 7), (METAL["fill"], 3)):
        p.raw(stroke_svg("M22 44 L20 26", col, wd))
    rim, inner = ellipse_pts(18, 20, 14, 16, 14), ellipse_pts(18, 20, 8, 10, 12)
    d = smooth_path(rim, tension=0.5) + " " + smooth_path(inner[::-1], tension=0.5)
    p.shape(d, RUBBER["fill"], RUBBER["line"], RUBBER["light"], RUBBER["shadow"], (2, 2), (3, 3), soft=1,
            details=[stroke_svg("M10 20 L26 20 M18 20 L18 30", METAL["fill"], 3)])
    note("steering_wheel", 40, 48, (21, 44), (-3.6, 6.8), "in front of Luigi's front glove; pivot = column base")
    return p.save(OUT)


def plate():
    p = Part("plate_01", 64, 40, grow=1.2, inner=1.6, bias=(1, 1.2))
    t = paint("green")
    det = [stroke_svg(smooth_path(ellipse_pts(32, 27, 5.2, 7, 12)), INK, 3.6),
           stroke_svg("M44 23 L49 19 L49 34", INK, 3.6),
           fill_svg(smooth_path(star_pts(13, 12, 5.5, 2.6), tension=0.1), t["fill"])]
    p.shape(poly_path([(4, 4), (22, 4), (22, 16), (58, 16), (58, 37), (4, 37)]), PLATE["fill"], PLATE["line"],
            PLATE["light"], PLATE["shadow"], (2, 2), (3, 3), soft=1.2, details=det)
    note("plate_01", 64, 40, (32, 20), (-4.0, 3.2), "the L-shaped \"01\" plate on the side pod, in front of kart_body "
                                                    "(and its recolours: they don't redraw it)")
    return p.save(OUT)


def blob(name, w, h, circles, tone, at, desc):
    p = Part(name, w, h)
    cx0, cy0 = w / 2, h / 2
    pts = []
    for i in range(48):
        a = 2 * math.pi * i / 48
        dx, dy = math.cos(a), math.sin(a)
        best = 4
        for bx, by, br in circles:
            ox, oy = bx - cx0, by - cy0
            bp = ox * dx + oy * dy
            disc = bp * bp - (ox * ox + oy * oy - br * br)
            if disc > 0:
                best = max(best, bp + math.sqrt(disc))
        pts.append((cx0 + best * dx, cy0 + best * dy))
    p.shape(smooth_path(pts, tension=0.6, jitter=0.4, seed=len(name)), tone["fill"], tone["line"], tone["light"],
            tone["shadow"], (5, 5), (7, 7), soft=3, hl=(0.42, 0.26, 0.22, 0.14))
    note(name, w, h, (w // 2, h // 2), at, desc)
    return p.save(OUT)


def flame():
    p = Part("flame", 72, 56)
    outer = [(6, 28), (18, 18), (28, 8), (36, 16), (48, 6), (52, 20), (68, 22), (58, 30), (66, 42), (50, 40),
             (40, 50), (32, 40), (18, 40)]
    inner = [(14, 28), (26, 22), (36, 24), (46, 22), (50, 30), (40, 36), (28, 34)]
    p.shape(smooth_path(outer, tension=0.55, jitter=0.4, seed=2), FIRE["fill"], FIRE["line"], FIRE["light"],
            FIRE["shadow"], (4, 4), (5, 5), soft=2, details=[fill_svg(smooth_path(inner, tension=0.6), "#ffe27a")],
            hl=(0.4, 0.3, 0.16, 0.12))
    note("flame", 72, 56, (6, 28), (15.6, 10.8), "a backfire out of the exhaust; pivot at the pipe tip")
    return p.save(OUT)


# ---------------------------------------------------------------- preview --
def card(n):
    return Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")


def at(c, n, ox, oy, xy=None, scale=1.0, angle=0.0):
    im = card(n)
    px, py = meta[n]["pivot"]
    xy = meta[n]["model_xy"] if xy is None else xy
    if scale != 1.0:
        im = im.resize((round(im.width * scale), round(im.height * scale)), Image.NEAREST)
        px, py = px * scale, py * scale
    r = int(max(im.width, im.height) * 1.5)
    big = Image.new("RGBA", (2 * r, 2 * r))
    big.paste(im, (r - round(px), r - round(py)))
    if angle:
        big = big.rotate(angle, resample=Image.BICUBIC, center=(r, r))
    c.alpha_composite(big, (round(ox + xy[0] * U * scale - r), round(oy - xy[1] * U * scale - r)))


def kart(c, ox, oy, colour="green", wrecked=False, scale=1.0):
    s = sfx(colour)
    for xy in FAR:
        at(c, "wheel_far", ox, oy, xy, scale)
    at(c, "kart_engine", ox, oy, scale=scale)
    at(c, "kart_seat" + s, ox, oy, (4.5, 4.0), scale)
    at(c, ("kart_nose_wrecked" + s if wrecked else "kart_nose" + s), ox, oy, (-8.0, 3.5), scale)
    at(c, ("kart_body_wrecked" + s if wrecked else "kart_body" + s), ox, oy, (0, 0), scale)
    at(c, "plate_01", ox, oy, scale=scale)
    at(c, "wheel_near_front", ox, oy, scale=scale)
    at(c, "wheel_near_back", ox, oy, scale=scale)


def preview():
    W, H = 1400, 1110
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)

    def label(x, y, s, w=300):
        d.rectangle([x, y, x + w, y + 14], fill=(225, 218, 205, 255))
        d.text((x + 4, y + 2), s, fill=(40, 30, 30, 255))

    label(10, 8, "Big Green 01 (c_lp_kart) assembled, 1:1, 8 texels/unit (34 x 12 units)", 460)
    kart(c, 160, 140)
    at(c, "steering_wheel", 160, 140)
    at(c, "exhaust_puff", 160, 140, (18.5, 12.0))
    label(500, 8, "wrecked, upside down, smoking", 300)
    tmp = Image.new("RGBA", (340, 140))
    kart(tmp, 170, 110, wrecked=True)
    c.alpha_composite(tmp.rotate(180), (480, 30))
    at(c, "smoke_puff", 650, 100, (2, 5))
    at(c, "smoke_puff", 650, 100, (-6, 8), scale=0.7)
    label(860, 8, "a backfire (flame) at the pipe", 300)
    kart(c, 1040, 140)
    at(c, "flame", 1040, 140)
    label(10, 176, "cards", 1380)
    x, y = 14, 196
    for n in ["kart_body", "kart_nose", "kart_seat", "kart_engine", "wheel_near_front", "wheel_near_back",
              "wheel_far", "steering_wheel", "plate_01", "exhaust_puff", "smoke_puff", "flame",
              "kart_body_wrecked", "kart_nose_wrecked"]:
        im = card(n)
        if x + im.width > W - 10:
            x, y = 14, y + 110
        c.alpha_composite(im, (x, y))
        d.text((x, y + im.height + 2), n, fill=(70, 50, 50, 255))
        x += max(im.width, len(n) * 6) + 14
    label(10, 420, "recolours (kart_body / kart_nose / kart_seat), plate_01 laid on each", 520)
    for i, col in enumerate(PAINTS):
        kart(c, 170 + (i % 4) * 340, 560 + (i // 4) * 150, col)
        d.text((40 + (i % 4) * 340, 568 + (i // 4) * 150), col, fill=(70, 50, 50, 255))
    label(10, 790, "wrecks in every colour (kart_body_wrecked / kart_nose_wrecked), upright", 520)
    for i, col in enumerate(PAINTS):
        kart(c, 170 + (i % 4) * 340, 930 + (i // 4) * 150, col, wrecked=True)
        d.text((40 + (i % 4) * 340, 938 + (i // 4) * 150), col, fill=(70, 50, 50, 255))
    return c


def build():
    plate()
    blob("exhaust_puff", 48, 40, [(16, 22, 12), (30, 18, 13), (32, 28, 10)], PUFF, (17.0, 11.5),
         "a white puff from the pipe tip (15.6, 10.8)")
    blob("smoke_puff", 80, 72, [(26, 44, 20), (44, 28, 22), (58, 46, 18), (40, 52, 18)], SMOKE_D, (2.0, 8.0),
         "dark smoke over the wreck; scale up, drift and fade")
    flame()
    pj = os.path.join(OUT, "parts.json")
    whole = json.load(open(pj, encoding="utf-8"))
    whole["cards"].update(meta)  # the style model's cards stay as kart_cards.py wrote them
    with open(pj, "w", encoding="utf-8") as f:
        json.dump(whole, f, indent=1)


if __name__ == "__main__":
    build()
    print(len(meta), "cards")
