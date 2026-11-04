"""Future Sight's overlay (Super Luigi ch6): the gold screen border, the vision bubble, the shrinking
gold ring, and the move icons for Crepe's and the Nightmare's moves.

Run: python future_sight.py -> ../future_sight/*.svg, *.png, preview.png
ch6.md, New assets 9 and the battle's phase 2: "a pale picture floats over Crepe's head showing his
next move: its icon and name, and who it'll hit ... in the gold border's style"; "a gold ring shrinks
round the target; when it closes, that's the Superguard moment"; Compass Bolt "shown with a warning
glow". 2D art for the framework's drawing, in the game's HUD style: icons like icon.tpl's (32x32,
a thin dark line, lit from the top left, 1-bit alpha), frames with 1-bit alpha so the framework can
tint and fade them. Drawn in pixels (no model units).
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, smooth_path, poly_path, ellipse_pts, star_pts, stroke_svg, fill_svg, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "future_sight")
GOLD = dict(fill="#e8b840", light="#fadc80", shadow="#b0841e", line="#4a2e04")
CREAM = dict(fill="#f6f0e2", light="#ffffff", shadow="#d8ccb2", line="#5a4420")
FIRE = dict(fill="#ff8a24", light="#ffd84a", shadow="#e04a1a", line="#5a1604")
BATTER = dict(fill="#f2d48a", light="#fff0c0", shadow="#c8a058", line="#5a3a10")
CREPE = dict(fill="#e8b860", light="#f8d898", shadow="#b8863a", line="#4a2a08")
PAGE = dict(fill="#e8dcc0", light="#f6eedc", shadow="#bcaa86", line="#2a1a10")
SHADOW = dict(fill="#3a2650", light="#4e3668", shadow="#28183a", line="#0e0614")
RED = dict(fill="#d8303a", light="#f06a6a", shadow="#9a1824", line="#3a0608")
BLUE = dict(fill="#5a7ad8", light="#8aa8f0", shadow="#3a56a8", line="#101a48")
SKIN = dict(fill="#f6c89a", light="#ffe2c4", shadow="#d49a6a", line="#4a2810")


def sh(p, pts, tone, details=(), hl=None, tension=0.4, seed=1, line=2.4, d=None, soft=0.8):
    p.shape(d or smooth_path(pts, tension=tension, jitter=0.15, seed=seed), tone["fill"], tone["line"],
            tone.get("light"), tone.get("shadow"), (2, 2), (2.5, 2.5), details=details, soft=soft, hl=hl,
            grow=line * 0.45, inner=line * 0.55, bias=(line * 0.3, line * 0.35))


def ell(cx, cy, rx, ry, n=12, rot=0):
    return smooth_path(ellipse_pts(cx, cy, rx, ry, n, rot=rot), tension=0.5)


# ------------------------------------------------------------------ icons --
def icon(name, draw):
    p = Part(name, 32, 32)
    draw(p)
    p.save(OUT)


def i_flambe(p):   # a fan of flame
    for k, a in enumerate((-40, -15, 10, 35)):
        r = math.radians(a - 90)
        tip = (16 + 13 * math.cos(r), 27 + 23 * math.sin(r) * 0.9)
        sh(p, [(14, 28), (tip[0] - 2.6, tip[1] + 8), (tip[0] - 0.6, tip[1] + 3), tip, (tip[0] + 1.6, tip[1] + 6),
               (tip[0] + 3, tip[1] + 9), (18, 28)], FIRE, seed=k, tension=0.25)


def i_fold(p):   # a crepe folded shut over Luigi: his capped head peeks out between the two flaps
    spots = lambda pts: [fill_svg(ell(x, y, 1.6, 1.1, 8), "#b07a30") for x, y in pts]
    sh(p, [(2, 22), (5, 11), (16, 5), (27, 11), (30, 22)], dict(CREPE, fill="#d0a050", light="#e2bc70"),
       spots([(8, 13), (23, 10)]), seed=4, tension=0.5)   # the far flap, folded up behind him
    sh(p, None, SKIN, [fill_svg(ell(13.6, 17.4, 1.1, 2, 8), "#1a1008"), fill_svg(ell(17.6, 17.4, 1.1, 2, 8), "#1a1008")],
       d=ell(16, 17, 6, 5.4, 12), seed=5, line=2.0)
    sh(p, [(9.6, 15), (10.4, 11), (16, 8.6), (21.6, 11), (22.6, 15), (16, 13.6)],
       dict(fill="#3aaa3a", light="#7cd26a", shadow="#1e7a24", line="#0e300c"), seed=6, line=2.0)   # his cap
    sh(p, [(1, 19), (16, 22.5), (31, 19), (29, 26), (16, 31), (3, 26)], CREPE, spots([(9, 25), (20, 27), (24, 23)]),
       hl=(0.4, 0.3, 0.3, 0.2), seed=7)   # the near flap, folded over him


def i_batter(p):   # a mixing bowl of batter with a balloon whisk standing in it, its wire loops open
    sh(p, [(27, 1), (30.5, 3.5), (25, 10), (22, 7.6)], dict(fill="#9a6a3a", light="#ba8a5a", shadow="#6a4a2a",
                                                            line="#2a1808"), seed=6, tension=0.2, line=2.0)
    hx, hy, bx, by = 23.4, 9.0, 11.0, 22.0
    ax, ay = bx - hx, by - hy
    L = math.hypot(ax, ay)
    nx, ny = -ay / L, ax / L
    P = lambda t, w: (hx + ax * t + nx * w, hy + ay * t + ny * w)
    for colour, width in (("#22263a", 3.4), ("#d8dce6", 1.5)):
        for w in (5.6, 2.6):   # two loops seen side on, so the whisk's open cage reads
            d = smooth_path([P(0, 0), P(0.45, w * 0.8), P(0.86, w), P(1.04, 0), P(0.86, -w), P(0.45, -w * 0.8)],
                            tension=0.5)
            p.raw(stroke_svg(d, colour, width))
    sh(p, [(2, 20), (30, 20), (28, 26.5), (22, 30.5), (10, 30.5), (4, 26.5)], BLUE,
       hl=(0.3, 0.3, 0.2, 0.18), seed=9, tension=0.45)   # the bowl
    p.raw(fill_svg(smooth_path([(4.4, 20.6), (9, 17.6), (14, 19), (19, 17.4), (24, 18.6), (27.6, 20.6), (16, 22.8)],
                               tension=0.5), BATTER["fill"]))   # the batter, heaped round the whisk
    p.raw(fill_svg(ell(12.6, 19, 3.2, 1.4, 8), BATTER["light"]))


def i_reshuffle(p):   # two cards swapping, with circling arrows
    sh(p, [(4, 8), (14, 6), (16, 22), (6, 24)], BLUE, seed=7, tension=0.1)
    sh(p, [(16, 10), (26, 8), (28, 24), (18, 26)], RED, seed=8, tension=0.1)
    p.raw(stroke_svg("M8 30 Q16 34 24 30", "#3a2a1a", 2.5))
    p.raw(fill_svg(poly_path([(24, 27), (28, 30), (23, 33)]), "#3a2a1a"))
    p.raw(stroke_svg("M24 3 Q16 -1 8 3", "#3a2a1a", 2.5))
    p.raw(fill_svg(poly_path([(8, 0), (4, 3), (9, 6)]), "#3a2a1a"))


def bolt(p):
    sh(p, [(18, 2), (8, 18), (15, 18), (12, 30), (25, 12), (18, 12), (22, 2)], GOLD, seed=9, tension=0.05,
       hl=(0.4, 0.3, 0.3, 0.2))


def i_compass_bolt(p):   # a needle of gold light
    bolt(p)


def i_compass_bolt_warn(p):   # the same with a red warning glow behind it
    sh(p, star_pts(16, 16, 15, 10, n=10), dict(fill="#f24a4a", light="#ff8a8a", shadow="#c02828", line="#4a0808"),
       seed=10, tension=0.2)
    bolt(p)


def i_doom(p):   # the needle stopped, pointing: a compass with its needle locked
    sh(p, None, GOLD, d=ell(16, 16, 13, 13, 16), seed=11)
    p.raw(fill_svg(ell(16, 16, 9.5, 9.5, 14), "#f2e6c4"))
    p.raw(fill_svg(poly_path([(13.5, 13.5), (27, 5), (18.5, 18.5)]), "#d8303a"))
    p.raw(fill_svg(poly_path([(13.5, 13.5), (5, 27), (18.5, 18.5)]), "#3a3448"))
    p.raw(fill_svg(ell(16, 16, 2, 2, 8), "#a07a1c"))


def i_hatesong(p):   # the howl: a dark mouth with gold teeth, sound waves
    sh(p, None, SHADOW, d=ell(14, 16, 11, 12, 14), seed=12)
    p.raw(fill_svg(ell(14, 17, 7, 6, 12), "#120818"))
    for k in range(3):
        x = 10 + k * 4
        p.raw(fill_svg(poly_path([(x - 1.5, 12), (x + 1.5, 12), (x, 15)]), "#e2b444"))
    for r in (4, 7):
        p.raw(stroke_svg(f"M{26 + r * 0.3} {16 - r} Q{28 + r} 16 {26 + r * 0.3} {16 + r}", "#6a4a8a", 2))


def i_thousand(p):   # a thousand tomorrows: five pages fanned like strikes
    for k in range(5):
        x = 4 + k * 5
        sh(p, [(x, 28), (x + 3, 6 + k % 2 * 3), (x + 9, 8 + k % 2 * 3), (x + 6, 28)], PAGE, seed=13 + k, tension=0.1,
           line=2.0)


def i_page_grab(p):   # a page-hand closing
    sh(p, [(6, 28), (4, 16), (6, 6), (10, 4), (12, 12), (14, 4), (18, 4), (19, 12), (22, 6), (26, 8), (24, 18),
           (28, 20), (26, 26), (18, 30)], PAGE,
       [stroke_svg(f"M8 {y} L22 {y + 1}", "#6a5a48", 1.2) for y in (16, 20, 24)], seed=18, tension=0.4)


ICONS = {"icon_flambe": i_flambe, "icon_fold": i_fold, "icon_batter": i_batter, "icon_reshuffle": i_reshuffle,
         "icon_compass_bolt": i_compass_bolt, "icon_compass_bolt_warn": i_compass_bolt_warn,
         "icon_doom_foretold": i_doom, "icon_hatesong": i_hatesong, "icon_thousand_tomorrows": i_thousand,
         "icon_page_grab": i_page_grab}


# ----------------------------------------------------------------- frames --
def border():
    """the gold screen border as a 9-slice: a corner and an edge strip (tiles along x)"""
    p = Part("border_corner", 32, 32)
    sh(p, [(0, 0), (32, 0), (32, 9), (12, 9), (9, 12), (9, 32), (0, 32)], GOLD, seed=20, tension=0.1, line=2.0)
    p.raw(stroke_svg("M3 30 L3 5 Q3 3 5 3 L30 3", GOLD["light"], 1.4))
    sh(p, None, dict(fill="#5a7ad8", light="#8aa8f0", shadow="#3a56a8", line="#101a48"), d=ell(9, 9, 4.5, 4.5, 10),
       seed=21, line=2.0)   # a blue jewel at the corner, like the tiara's piece
    p.raw(fill_svg(ell(7.5, 7.5, 1.2, 1.2, 6), "#ffffff"))
    p.save(OUT)
    p = Part("border_edge", 32, 16)
    p.raw(f'<rect x="0" y="0" width="32" height="9" fill="{GOLD["fill"]}"/>')
    p.raw(f'<rect x="0" y="6" width="32" height="3" fill="{GOLD["shadow"]}"/>')
    p.raw(f'<rect x="0" y="9" width="32" height="2" fill="{GOLD["line"]}"/>')
    p.raw(f'<rect x="0" y="2.4" width="32" height="1.4" fill="{GOLD["light"]}"/>')
    for x in (8, 24):   # a small diamond stud every 16 px
        p.raw(fill_svg(poly_path([(x, 1.5), (x + 2.5, 4.5), (x, 7.5), (x - 2.5, 4.5)]), GOLD["light"]))
    p.save(OUT)


def bubble():
    """the vision bubble: a pale cloud-shaped frame with room for the icon, the move's name and its target,
    and two small trailing puffs down to Crepe's head"""
    p = Part("vision_bubble", 192, 112)
    pts = []
    n = 14
    for k in range(n * 4):
        a = 2 * math.pi * k / (n * 4)
        bump = abs(math.sin(n * a / 2)) ** 0.5
        pts.append((96 + (86 + 5 * bump) * math.cos(a), 52 + (42 + 5 * bump) * math.sin(a)))
    sh(p, pts, CREAM, [f'<path d="{smooth_path(ellipse_pts(96, 52, 74, 32, 20), tension=0.5)}" fill="none" '
                       f'stroke="{GOLD["fill"]}" stroke-width="2.5" stroke-dasharray="6 5"/>'],
       hl=(0.36, 0.2, 0.24, 0.12), seed=22, tension=0.5, line=3.0, soft=1.5)
    p.save(OUT)
    for name, r in (("vision_puff_big", 8), ("vision_puff_small", 5)):
        size = 24 if r == 8 else 16
        q = Part(name, size, size)
        sh(q, None, CREAM, d=ell(size / 2 - 0.5, size / 2 - 0.5, r, r * 0.85, 12), seed=23, line=2.4)
        q.save(OUT)


def ring():
    """the shrinking gold ring: the framework scales it down round the target; it's closed at 1:1 = 64 px"""
    p = Part("guard_ring", 128, 128)
    outer = smooth_path(ellipse_pts(64, 64, 60, 60, 32), tension=0.5)
    inner = smooth_path(ellipse_pts(64, 64, 50, 50, 32), tension=0.5)
    p.raw(f'<path d="{outer} {inner}" fill="{GOLD["line"]}" fill-rule="evenodd" transform="translate(1 1.5)"/>')
    p.raw(f'<path d="{outer} {inner}" fill="{GOLD["fill"]}" fill-rule="evenodd"/>')
    p.raw(f'<path d="{smooth_path(ellipse_pts(64, 64, 56.5, 56.5, 32), tension=0.5)}" fill="none" '
          f'stroke="{GOLD["light"]}" stroke-width="2.4" stroke-dasharray="40 20"/>')
    p.raw(f'<path d="{outer}" fill="none" stroke="{GOLD["line"]}" stroke-width="2"/>')
    p.raw(f'<path d="{inner}" fill="none" stroke="{GOLD["line"]}" stroke-width="2"/>')
    for k in range(4):   # four sparkles on the ring, at the diagonals
        a = math.radians(45 + 90 * k)
        x, y = 64 + 55 * math.cos(a), 64 + 55 * math.sin(a)
        p.raw(fill_svg(poly_path(star_pts(x, y, 8, 2.2, n=4)), "#fffbe6"))
    p.save(OUT)
    q = Part("guard_ring_closed", 128, 128)   # the moment: the ring flashes white-gold as it closes
    q.raw(f'<path d="{outer} {inner}" fill="#fff2b0" fill-rule="evenodd"/>')
    q.raw(f'<path d="{outer}" fill="none" stroke="{GOLD["fill"]}" stroke-width="3"/>')
    q.raw(f'<path d="{inner}" fill="none" stroke="{GOLD["fill"]}" stroke-width="3"/>')
    for k in range(8):
        a = math.radians(22.5 + 45 * k)
        x, y = 64 + 55 * math.cos(a), 64 + 55 * math.sin(a)
        q.raw(fill_svg(poly_path(star_pts(x, y, 9, 2.4, n=4)), "#ffffff"))
    q.save(OUT)


def preview():
    c = Image.new("RGBA", (620, 300), (60, 70, 90, 255))
    d = ImageDraw.Draw(c)
    bc = Image.open(os.path.join(OUT, "border_corner.png")).convert("RGBA")
    be = Image.open(os.path.join(OUT, "border_edge.png")).convert("RGBA")
    for x in range(32, 620 - 32, 32):   # the screen border's top edge and corners
        c.alpha_composite(be, (x, 0))
    c.alpha_composite(bc, (0, 0))
    c.alpha_composite(bc.transpose(Image.FLIP_LEFT_RIGHT), (620 - 32, 0))
    vb = Image.open(os.path.join(OUT, "vision_bubble.png")).convert("RGBA")
    c.alpha_composite(vb, (30, 40))
    c.alpha_composite(Image.open(os.path.join(OUT, "icon_flambe.png")).convert("RGBA"), (60, 80))
    d.text((100, 82), "Next: Flambé", fill=(60, 40, 20, 255))
    d.text((100, 98), "-> everyone", fill=(60, 40, 20, 255))
    c.alpha_composite(Image.open(os.path.join(OUT, "vision_puff_big.png")).convert("RGBA"), (120, 156))
    c.alpha_composite(Image.open(os.path.join(OUT, "vision_puff_small.png")).convert("RGBA"), (134, 182))
    for i, n in enumerate(("guard_ring", "guard_ring_closed")):
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im if i else im.resize((96, 96)), (250 + i * 130, 40 if i else 56))
        d.text((250 + i * 130, 172), n + (" (x0.75)" if not i else ""), fill=(255, 255, 255, 255))
    x = 20
    for n in ICONS:
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        c.alpha_composite(im.resize((64, 64), Image.NEAREST), (x, 210))
        x += 60
    d.text((20, 280), "move icons at x2: " + ", ".join(n[5:] for n in ICONS), fill=(255, 255, 255, 255))
    return c


def build():
    os.makedirs(OUT, exist_ok=True)
    clean(OUT)
    for n, f in ICONS.items():
        icon(n, f)
    border()
    bubble()
    ring()
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
