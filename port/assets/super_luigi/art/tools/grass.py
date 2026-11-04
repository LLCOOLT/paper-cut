"""Luigi as the Grass (Super Luigi ch4): a costume of long paper grass blades over c_luigi.

Run: python grass.py -> ../grass/*.svg, *.png, parts.json, preview.png
ch4.md, New assets 1: "a bush of long grass blades over Luigi, his face and moustache peeking out of
a gap, his shoes at the bottom". Like the bride's veil (../veil/README.md), these are accessory
cards pinned to c_luigi's parts, so they ride his own poses in the field and in battle:
  grass_back   tall blades behind his body and head (pinned to the body card),
  grass_front  blades in front of his body and legs, low on the face side so the face shows,
  grass_hood   a crown of blades over his cap (pinned to the head card), framing the face,
and swaying copies of the front and hood for the walk shuffle and the rustle.
Placement in c_luigi bind-pose units: feet at y 0, cap top y 24.3, head card x -3.7..4.3 y 9.8..20.8,
body card x -3.7..4.3 y 3.6..12.6; he faces -x. 8 texels/unit.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Rig, stroke_svg, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "grass")
R = Rig("c_luigi (Grass costume)", OUT)

BLADE = dict(fill="#74c23e", light="#a2dc68", shadow="#4e9a2a", line="#163a0c")
BLADE_D = dict(fill="#4fa036", light="#76bc52", shadow="#357a22", line="#123010")   # the far blades
BLADE_Y = dict(fill="#9ccc44", light="#c4e47a", shadow="#74a42a", line="#283a08")   # a few yellower tips
HEAD_CARD = (-3.7, 9.8, 4.3, 20.8)
BODY_CARD = (-3.7, 3.6, 4.3, 12.6)
PIN_BODY = (0.5, 8.0)
PIN_HEAD = (0.5, 22.0)


def blade(c, base, tip, w, tone, seed, bend=0.0, details=True):
    """One tapered blade from base to tip (model units), w wide at the base, bowed by `bend`."""
    (bx, by), (tx, ty) = base, tip
    L = math.hypot(tx - bx, ty - by)
    ux, uy = (tx - bx) / L, (ty - by) / L
    nx, ny = -uy, ux
    mx, my = bx + (tx - bx) * 0.5 + nx * bend, by + (ty - by) * 0.5 + ny * bend
    qx, qy = bx + (tx - bx) * 0.8 + nx * bend * 0.7, by + (ty - by) * 0.8 + ny * bend * 0.7
    pts = [(bx - nx * w / 2, by - ny * w / 2), (mx - nx * w * 0.42, my - ny * w * 0.42),
           (qx - nx * w * 0.22, qy - ny * w * 0.22), (tx, ty), (qx + nx * w * 0.22, qy + ny * w * 0.22),
           (mx + nx * w * 0.42, my + ny * w * 0.42), (bx + nx * w / 2, by + ny * w / 2)]
    det = [stroke_svg(c.curve((bx, by), (mx, my), (qx, qy)), tone["shadow"], 1.6)] if details and L > 5 else []
    c.shape(pts, tone, det, seed=seed, tension=0.45, jitter=0.25, lo=(3, 3), so=(4, 4), soft=1.5,
            hl=(0.4, 0.3, 0.26, 0.18) if L > 7 else None)


def fan(c, specs, sway, seed):
    """specs: [(base_x, base_y, tip_x, tip_y, width, tone)]; sway bends every tip back (+x)."""
    rnd = random.Random(seed)
    for i, (bx, by, tx, ty, w, tone) in enumerate(specs):
        lift = (ty - by)
        tx2 = tx + sway * lift * 0.18
        ty2 = ty - abs(sway) * lift * 0.04
        blade(c, (bx, by), (tx2, ty2), w, tone, seed + i, bend=rnd.uniform(-0.6, 0.6) + sway * 0.6)


def tones(n, seed):
    rnd = random.Random(seed)
    return [BLADE_Y if rnd.random() < 0.22 else BLADE for _ in range(n)]


def back(sway=0.0):
    c = R.card("grass_back", (-11.0, 1.0, 12.0, 34.0))
    n = 13
    specs = []
    for i in range(n):   # a tall fan, highest in the middle, wide enough to hide his sides
        t = i / (n - 1)
        a = math.radians(160 - 140 * t)
        h = 20.0 + 11.5 * math.sin(math.pi * t)
        bx = 0.2 + 5.0 * t
        specs.append((bx, 2.0, bx + 10.0 * math.cos(a), 2.0 + h * math.sin(a) ** 0.6, 2.4,
                      BLADE_Y if i in (4, 9) else BLADE_D))
    fan(c, specs, sway, 10)
    return c.save(PIN_BODY, "tall far blades behind Luigi's body and head, reaching above the cap; "
                            "pinned to the body card, drawn behind everything of his")


def front(sway=0.0):
    name = "grass_front" + ("_sway" if sway else "")
    c = R.card(name, (-10.0, 0.0, 13.0, 18.0))
    # low on the face side (-x) so the moustache shows, taller toward the back (+x)
    n = 14
    specs = []
    tn = tones(n, 31)
    for i in range(n):
        t = i / (n - 1)
        bx = -1.4 + 6.8 * t
        tx = -9.0 + 18.5 * t
        ty = 6.6 + 9.6 * t ** 0.9 + 1.0 * (i % 2) - 1.2 * math.sin(math.pi * t) ** 8
        specs.append((bx, 1.2, tx, ty, 2.2, tn[i]))
    order = sorted(range(n), key=lambda i: (i % 3, i))   # interleave so blades cross
    fan(c, [specs[i] for i in order], sway, 30)
    return c.save(PIN_BODY, ("the near blades, swaying back (walk, rustle)" if sway else
                             "the near blades over his body and legs, up to the moustache on the face side; "
                             "his shoes show below y 1") + "; pinned to the body card, in front of the body")


def hood(sway=0.0):
    name = "grass_hood" + ("_sway" if sway else "")
    c = R.card(name, (-9.0, 9.0, 12.0, 32.0))
    # a mop of blades rooted along the cap's crown, drooping forward over the brim and back down
    # the head, and standing up on top; the face (x -6..0, y 11..19) stays clear
    n = 12
    specs = []
    tn = tones(n, 51)
    for i in range(n):
        t = i / (n - 1)
        a = math.radians(175 - 215 * t)          # from forward-down, up, to back-down
        bx = -4.0 + 8.5 * t
        by = 21.6 + 2.2 * math.sin(math.pi * t)
        L = 6.5 + 2.5 * math.sin(math.pi * t)
        ty = by + L * math.sin(a)
        if i == 0:
            ty = max(ty, 19.2)                   # the front blade stops above the eye
        specs.append((bx, by, bx + L * math.cos(a), ty, 2.0, tn[i]))
    # first a leafy mound over the whole cap, so no cap shows between the blades' roots
    mound = [(-5.8, 19.8), (-5.2, 22.6), (-2.6, 25.0), (1.0, 25.8), (4.6, 24.6), (6.6, 21.6), (6.2, 19.2),
             (3.0, 20.2), (0.0, 20.6), (-3.0, 20.0)]
    c.shape(mound, BLADE_D, [stroke_svg(c.curve((-3.6, 21.0), (-1.0, 23.4), (1.6, 21.2)), BLADE_D["shadow"], 2),
                             stroke_svg(c.curve((1.0, 21.4), (3.4, 23.6), (5.4, 20.8)), BLADE_D["shadow"], 2)],
            seed=49, tension=0.55, jitter=0.4, lo=(4, 4), so=(5, 5), soft=2)
    # and a curtain of blades hanging down the back of the head, behind the ear, to meet the front
    # blades, so no hair or cap shows between the hood and the body's grass (the face, x < 0.5, stays clear)
    curtain = [(1.2, 20.0, 1.6, 12.2, 2.4, BLADE_D), (2.6, 20.6, 3.8, 10.8, 2.6, BLADE),
               (4.0, 21.0, 6.0, 11.4, 2.6, BLADE_Y), (5.2, 20.8, 8.0, 12.8, 2.4, BLADE),
               (6.0, 20.2, 9.6, 14.6, 2.2, BLADE_D)]
    fan(c, curtain, sway * 0.6, 70)
    order = sorted(range(n), key=lambda i: (i % 2, i))
    fan(c, [specs[i] for i in order], sway, 50)
    return c.save(PIN_HEAD, ("the crown of blades, swaying back" if sway else
                             "a mop of blades over the cap, framing his face") +
                  "; pinned to the head card, in front of the cap")


# --------------------------------------------------------------- preview --
def pin_on(card, xy):
    x0, y0, x1, y1 = card
    return [round((xy[0] - x0) * 8, 1), round((y1 - xy[1]) * 8, 1)]


def mannequin(d, ox, oy):
    """A neutral stand-in for Luigi (his real textures stay off the repository), as veil.py's."""
    u = lambda x, y: (ox + x * 8, oy - y * 8)
    d.ellipse([*u(-3.7, 12.6), *u(4.3, 3.6)], fill=(186, 192, 210, 255), outline=(110, 116, 130, 255), width=3)
    d.ellipse([*u(-3.7, 20.8), *u(4.3, 9.8)], fill=(214, 206, 196, 255), outline=(120, 110, 100, 255), width=3)
    d.chord([*u(-5.0, 25.0), *u(6.0, 15.5)], 180, 360, fill=(170, 190, 168, 255), outline=(90, 110, 88, 255), width=3)
    d.polygon([u(-6.0, 19.6), u(-1.0, 20.4), u(-1.0, 19.0), u(-6.0, 18.4)], fill=(170, 190, 168, 255),
              outline=(90, 110, 88, 255))
    d.ellipse([*u(-6.2, 17.4), *u(-3.8, 15.0)], fill=(230, 190, 170, 255), outline=(120, 90, 80, 255), width=2)  # nose
    d.ellipse([*u(-6.0, 15.4), *u(-1.6, 13.6)], fill=(70, 50, 40, 255))   # moustache
    for x in (-3.0, 2.0):   # shoes
        d.ellipse([*u(x - 2.2, 2.0), *u(x + 2.2, 0.0)], fill=(120, 80, 50, 255), outline=(60, 40, 20, 255), width=2)


def paste(c, name, ox, oy):
    m = R.meta[name]
    im = Image.open(os.path.join(OUT, name + ".png")).convert("RGBA")
    px, py = m["pivot"]
    c.alpha_composite(im, (round(ox + m["model_xy"][0] * 8 - px), round(oy - m["model_xy"][1] * 8 - py)))


def worn(front_name, hood_name):
    c = Image.new("RGBA", (220, 300), (0, 0, 0, 0))
    ox, oy = 100, 285
    paste(c, "grass_back", ox, oy)
    base = Image.new("RGBA", c.size, (0, 0, 0, 0))
    mannequin(ImageDraw.Draw(base), ox, oy)
    c.alpha_composite(base)
    paste(c, front_name, ox, oy)
    paste(c, hood_name, ox, oy)
    return c


def preview():
    W, H = 1000, 580
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    d.text((8, 6), "Luigi as the Grass: cards 1:1 (8 texels/unit); worn on a grey stand-in for c_luigi",
           fill=(40, 30, 30, 255))
    x = 8
    for n in ("grass_back", "grass_front", "grass_front_sway", "grass_hood", "grass_hood_sway"):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        d.text((x, 24), n, fill=(40, 30, 30, 255))
        c.alpha_composite(im, (x, 40))
        x += im.width + 16
    for i, (f, h, label) in enumerate((("grass_front", "grass_hood", "standing"),
                                       ("grass_front_sway", "grass_hood_sway", "walk / rustle (sway)"))):
        im = worn(f, h)
        c.alpha_composite(im, (20 + i * 230, 270))
        d.text((40 + i * 230, 560), label, fill=(40, 30, 30, 255))
    flat = worn("grass_front", "grass_hood").rotate(-90, expand=True)   # lying flat on the stage, face up
    c.alpha_composite(flat, (500, 340))
    d.text((520, 560), "lie flat (the same cards, riding his pose)", fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    back()
    front()
    front(sway=1.0)
    hood()
    hood(sway=1.0)
    for n, m in R.meta.items():
        on_head = n.startswith("grass_hood")
        m["pins_to"] = "head card pPlaneShape14" if on_head else "body card pPlaneShape17"
        m["pin_on_part"] = pin_on(HEAD_CARD if on_head else BODY_CARD, m["model_xy"])
    R.write_json({"draw_order": ["grass_back", "(Luigi's own cards)", "grass_front / grass_front_sway",
                                 "grass_hood / grass_hood_sway"],
                  "pin_note": "model_xy: the pin in c_luigi's bind pose; pin_on_part: the same point in the "
                              "texels of the part it pins to"})
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta))
