"""The walking stone statue (c_lp_statue), chapter 1's boss: its drawings redrawn to STYLE.md.

Run: python statue.py -> ../statue/*.svg, *.png, preview.png
The part names, sizes and canvases match tools/port/statue_model.py (branch super-luigi-anims):
each drawing covers the same rectangle in that file's drawing units (1.8 model units each,
so 14.4 texels a drawing unit = 8 texels a model unit), padded to a multiple of 8 at the right
and top, exactly as statue_model.py lays its own drawings out. statue_model.py picks these
PNGs up from this folder by name.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, smooth_path, poly_path, ellipse_pts, stroke_svg, fill_svg  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), "..", "statue")
PX = 8 * 1.8

STONE = dict(fill="#a99b86", light="#c8bca6", shadow="#76695a", line="#33291f")
STONE_B = dict(fill="#978a76", light="#b4a893", shadow="#685c4f", line="#2c231b")  # the far side
BLOTCH = "#988a76"          # painted weathering patches (flat, soft-edged, few)
GROOVE = "#4b4035"          # carved line
GROOVE_LIT = "#d3c8b4"      # the lit lower lip of a carved line
SOCKET = "#2f2721"
LAVA = ("#3a1406", "#ea5a1c", "#ffd64e")   # crack edge, rim, core
GEM = dict(fill="#df2f2a", light="#ff7a68", shadow="#9a1713", deep="#640b09", line="#3c0705")
GLOW = dict(fill="#ffb347", core="#fff3b0", line="#5a2204")

HL_STONE = 0.22   # matte stone: a gentle sheen (R6b, matte end)


class Canvas:
    """A Part laid out on statue_model.py's drawing-unit rectangle."""

    def __init__(self, name, rect):
        x0, y0, x1, y1 = rect
        w, h = int(round((x1 - x0) * PX)), int(round((y1 - y0) * PX))
        self.W, self.H = w + (-w % 8), h + (-h % 8)
        self.x0, self.top = x0, y0 + self.H / PX
        self.p = Part(name, self.W, self.H, grow=2.0, inner=2.2, bias=(1.8, 2.0))
        self.name, self.rect = name, rect

    def u(self, x, y):
        return ((x - self.x0) * PX, (self.top - y) * PX)

    def s(self, d):
        return d * PX

    def pts(self, pts):
        return [self.u(x, y) for x, y in pts]

    def path(self, pts, tension=0.5, jitter=0.6, seed=1):
        return smooth_path(self.pts(pts), tension=tension, jitter=jitter, seed=seed)

    def line(self, pts):
        return "M" + " L".join("%.1f %.1f" % self.u(x, y) for x, y in pts)

    def groove(self, pts, w=0.30):
        """a carved line: the groove, and its lit lower lip"""
        d = self.line(pts)
        x0, y0 = 1.3, 1.8
        lit = "M" + " L".join("%.1f %.1f" % (a + x0, b + y0) for a, b in (self.u(x, y) for x, y in pts))
        return [stroke_svg(lit, GROOVE_LIT, self.s(w) * 0.55), stroke_svg(d, GROOVE, self.s(w))]

    def lava(self, pts):
        d = self.line(pts)
        return [stroke_svg(d, LAVA[0], self.s(0.62)), stroke_svg(d, LAVA[1], self.s(0.42)),
                stroke_svg(d, LAVA[2], self.s(0.16))]

    def blotch(self, cx, cy, rx, ry, seed, colour=BLOTCH):
        return (f'<path d="{smooth_path(ellipse_pts(*self.u(cx, cy), self.s(rx), self.s(ry), 9, wobble=0.25, seed=seed))}" '
                f'fill="{colour}" filter="url(#blotchsoft)"/>')

    def stone(self, pts, tone=STONE, details=(), hl=(0.38, 0.22, 0.26, 0.16), seed=1, tension=0.35, light_off=(9, 9)):
        self.p.shape(self.path(pts, tension=tension, seed=seed), tone["fill"], tone["line"], tone["light"],
                     tone["shadow"], light_off, (13, 12), details=details, soft=4, hl=hl, hl_alpha=HL_STONE)

    def save(self):
        self.p.defs.append('<filter id="blotchsoft" x="-30%" y="-30%" width="160%" height="160%">'
                           '<feGaussianBlur stdDeviation="2.5"/></filter>')
        return self.p.save(OUT)


# ------------------------------------------------------------------ head --
EYES = [[(-10.0, 25.4), (-3.6, 23.9), (-3.8, 21.8), (-9.6, 22.8)],
        [(0.6, 23.9), (7.0, 25.4), (7.2, 22.8), (0.8, 21.8)]]
GEM_C = (-1.5, 31.8)


def head():
    c = Canvas("head", (-15.0, 13.0, 13.5, 38.5))
    gx, gy = GEM_C
    det = [
        # painted weathering, and soot darkening the jaw
        c.blotch(-9.5, 33.5, 2.4, 1.4, 1), c.blotch(8.0, 19.0, 2.2, 1.6, 2), c.blotch(-11.5, 17.5, 1.8, 1.2, 3),
        c.blotch(0.0, 15.2, 11.5, 1.1, 4, "#8e816d"),
        # the shadow the brow casts over the eyes
        fill_svg(c.path([(-14.5, 25.9), (12.9, 25.9), (12.9, 24.2), (-14.5, 24.4)], seed=5), "#857867"),
    ]
    # eye sockets (the eyes part glows inside them)
    for i, e in enumerate(EYES):
        det.append(f'<path d="{c.path(e, tension=0.25, seed=10 + i, jitter=0.3)}" fill="{SOCKET}" '
                   f'stroke="{STONE["line"]}" stroke-width="3"/>')
    # cheek glyphs and the grim mouth with carved teeth
    det += c.groove([(7.6, 22.0), (9.6, 20.2), (7.6, 18.4), (9.6, 16.6)])
    det += c.groove([(-12.6, 22.0), (-11.4, 20.0), (-12.6, 18.0)])
    det += c.groove([(-10.0, 17.0), (-3.6, 16.2), (4.2, 16.7)], 0.42)
    for tx in (-7.6, -5.0, -2.4, 0.2, 2.6):
        det += c.groove([(tx, 16.4 - 0.04 * tx), (tx + 0.1, 15.5)], 0.22)
    # carved bands on the crown, a chip, a lava crack
    det += c.groove([(-12.6, 35.2), (-8.6, 35.6)], 0.24) + c.groove([(5.2, 35.6), (10.4, 35.2)], 0.24)
    det += c.lava([(10.6, 35.6), (8.8, 33.0), (9.8, 30.6), (8.6, 29.0)])
    det += c.lava([(-12.4, 30.4), (-11.0, 29.2)])
    det += c.groove([(-12.0, 34.0), (-10.2, 32.4)], 0.22)
    # the gem's setting: a carved ring of eight rays round a deep socket (the gem is its own part)
    det.append(fill_svg(smooth_path(ellipse_pts(*c.u(gx, gy), c.s(5.6), c.s(5.6), 16, seed=3), jitter=0.4),
                        "#8b7e6b"))
    det.append(stroke_svg(smooth_path(ellipse_pts(*c.u(gx, gy), c.s(5.6), c.s(5.6), 16, seed=3), jitter=0.4),
                          STONE["line"], 4))
    for k in range(8):
        a = k * math.pi / 4 + math.pi / 8
        det += c.groove([(gx + 4.6 * math.cos(a), gy + 4.6 * math.sin(a)),
                         (gx + 5.3 * math.cos(a), gy + 5.3 * math.sin(a))], 0.26)
    det.append(f'<path d="{smooth_path(ellipse_pts(*c.u(gx, gy), c.s(4.3), c.s(4.3), 14, seed=4), jitter=0.3)}" '
               f'fill="{SOCKET}" stroke="#211b16" stroke-width="3"/>')
    block = [(-13.0, 14.4), (-4.0, 14.0), (11.5, 14.5), (12.4, 20.0), (12.6, 27.5), (11.6, 33.0), (11.0, 36.4),
             (6.2, 37.0), (5.3, 36.3), (4.2, 37.1), (-12.0, 37.2), (-13.4, 33.0), (-14.0, 28.0), (-13.6, 21.0)]
    c.stone(block, details=det, hl=(0.30, 0.10, 0.22, 0.07), seed=7)
    # the brow: a ledge in front, shading the eyes
    brow = [(-14.4, 25.7), (-6.0, 25.5), (12.6, 25.8), (12.4, 28.3), (2.0, 28.5), (-14.4, 28.5)]
    c.p.shape(c.path(brow, tension=0.3, seed=8), "#b6a993", STONE["line"], "#d0c5b0", "#86796a", (5, 5), (7, 8),
              soft=2.5, details=c.groove([(-12.0, 27.0), (10.0, 27.2)], 0.2))
    # the nose: a long flat wedge, lit on its left face
    nose = [(-5.6, 23.4), (-1.4, 23.4), (-0.4, 18.4), (-7.6, 18.2)]
    c.p.shape(c.path(nose, tension=0.25, seed=9), STONE["fill"], STONE["line"], STONE["light"], STONE["shadow"],
              (6, 4), (9, 6), soft=3, hl=(0.35, 0.3, 0.2, 0.3), hl_alpha=HL_STONE)
    c.save()
    return c


def eyes(lit):
    c = Canvas("eyes" if lit else "eyes_dark", (-11.0, 20.6, 8.6, 26.6))
    for i, pts in enumerate(EYES):
        mx = sum(x for x, _ in pts) / 4
        my = sum(y for _, y in pts) / 4
        inner = [(mx + (x - mx) * 0.84, my + (y - my) * 0.66) for x, y in pts]
        if lit:
            core = [(mx + (x - mx) * 0.55, my + (y - my) * 0.32) for x, y in pts]
            det = [fill_svg(c.path(core, tension=0.3, seed=20 + i, jitter=0.2), GLOW["core"]),
                   fill_svg(smooth_path(ellipse_pts(*c.u(mx - 1.4, my + 0.3), 4.5, 3.2, 8)), "#ffffff")]
            c.p.shape(c.path(inner, tension=0.3, seed=30 + i, jitter=0.3), GLOW["fill"], GLOW["line"], "#ffd27a",
                      "#e9771c", (4, 4), (5, 5), details=det, soft=2, grow=1.2, inner=1.6, bias=(1, 1.2))
        else:
            det = [stroke_svg(c.line([(inner[0][0] + 0.6, (inner[0][1] + inner[3][1]) / 2),
                                      (inner[1][0] - 0.6, (inner[1][1] + inner[2][1]) / 2)]), "#4a3f35", 3)]
            c.p.shape(c.path(inner, tension=0.3, seed=30 + i, jitter=0.3), "#2b241e", "#1a1511", None, None,
                      details=det, grow=1.2, inner=1.6, bias=(1, 1.2))
    c.save()
    return c


def gem(state):
    name = ("gem", "gem_glow", "gem_cracked", "gem_glint")[state]
    gx, gy = GEM_C
    c = Canvas(name, (gx - 5.0, gy - 5.0, gx + 5.0, gy + 5.0))
    tone = dict(GEM)
    if state == 1:
        tone.update(fill="#ff4a34", light="#ffb09c", shadow="#c92a1c", deep="#8c140e")
        # a burst of light behind it: sixteen points, the long ones on the diagonals and axes
        burst = []
        for k in range(32):
            a = math.pi / 2 + k * math.pi / 16
            r = (4.95 if k % 4 == 0 else 4.55) if k % 2 == 0 else 4.05
            burst.append((gx + r * math.cos(a), gy + r * math.sin(a)))
        core = [(gx + (x - gx) * 0.93, gy + (y - gy) * 0.93) for x, y in burst]
        c.p.shape(poly_path(c.pts(burst)), GLOW["fill"], GLOW["line"], None, "#f08a2a", shadow_off=(3, 3), soft=1.2,
                  grow=1.2, inner=1.6, bias=(1, 1), details=[fill_svg(poly_path(c.pts(core)), "#ffd36a")])
        R_SHRINK = 0.92
    else:
        R_SHRINK = 1.0
    if state == 3:   # fallen free: a pale gold halo behind the cracked gem (it catches the light)
        halo = []
        for k in range(24):
            a = math.pi / 2 + k * math.pi / 12
            r = 4.85 if k % 2 == 0 else 4.4
            halo.append((gx + r * math.cos(a), gy + r * math.sin(a)))
        c.p.shape(smooth_path(c.pts(halo), tension=0.3), "#ffe6a6", "#5a3a08", None, "#f2c870", shadow_off=(3, 3),
                  soft=1.2, grow=1.2, inner=1.6, bias=(1, 1))
    if state in (2, 3):
        tone.update(fill="#b8312b", light="#e06a5c", shadow="#80150f", deep="#520a08")
    R = 4.2 * R_SHRINK   # glowing: a touch smaller, so the burst shows all round
    # a brilliant cut, seen from the front: an octagon with a table and four facet bands
    out = [(gx + R * math.cos(a), gy + R * math.sin(a)) for a in [math.pi / 2 + k * math.pi / 4 for k in range(8)]]
    if state in (2, 3):   # a chip broken off the upper right
        out[7] = (gx + 1.4, gy + 2.2)
    tab = [(gx + 0.48 * R * math.cos(a), gy + 0.48 * R * math.sin(a)) for a in [math.pi / 2 + k * math.pi / 4 for k in range(8)]]
    det = []
    # facets: light on the upper left, deep on the lower right
    for k in range(8):
        a, b = out[k], out[(k + 1) % 8]
        ta, tb = tab[k], tab[(k + 1) % 8]
        ang = math.pi / 2 + (k + 0.5) * math.pi / 4
        lit = math.cos(ang - math.radians(135))       # +1 toward the upper left
        col = tone["light"] if lit > 0.6 else tone["fill"] if lit > -0.2 else tone["shadow"] if lit > -0.8 else tone["deep"]
        det.append(f'<path d="{poly_path(c.pts([a, b, tb, ta]))}" fill="{col}"/>')
    det.append(fill_svg(poly_path(c.pts(tab)), tone["fill"]))
    for k in range(8):
        det.append(stroke_svg(c.line([out[k], tab[k]]), tone["deep"], 2))
    det.append(stroke_svg(poly_path(c.pts(tab)), tone["deep"], 2))
    # the sheen on the table, and a specular glint
    det.append(f'<path d="{poly_path(c.pts([tab[1], tab[2], (gx - 0.2, gy + 0.2), (gx + 0.3, gy + 1.6)]))}" '
               f'fill="#ffffff" fill-opacity="0.45"/>')
    det.append(fill_svg(smooth_path(ellipse_pts(*c.u(gx - 1.9, gy + 1.9), c.s(0.55), c.s(0.38), 8, rot=-40)),
                        "#ffffff"))
    if state in (2, 3):
        crack = [(gx - 1.4, gy + 3.6), (gx + 0.6, gy + 1.2), (gx - 0.8, gy - 0.6), (gx + 1.0, gy - 3.2)]
        det += [stroke_svg(c.line(crack), "#ffd6cc", c.s(0.5)), stroke_svg(c.line(crack), "#250404", c.s(0.3)),
                stroke_svg(c.line([(gx + 0.6, gy + 1.2), (gx + 2.6, gy + 0.4)]), "#250404", c.s(0.24)),
                stroke_svg(c.line([(gx - 0.8, gy - 0.6), (gx - 2.6, gy - 1.2)]), "#250404", c.s(0.2))]
    c.p.shape(poly_path(c.pts(out)), tone["fill"], tone["line"], None, None, details=det, grow=1.8, inner=2.2,
              bias=(1.6, 1.8))
    if state == 3:   # the glint: a four-pointed white sparkle at its top left
        sx, sy = gx - 2.2, gy + 2.4
        star = [(sx, sy + 2.4), (sx + 0.45, sy + 0.45), (sx + 2.4, sy), (sx + 0.45, sy - 0.45), (sx, sy - 2.4),
                (sx - 0.45, sy - 0.45), (sx - 2.4, sy), (sx - 0.45, sy + 0.45)]
        c.p.shape(poly_path(c.pts(star)), "#fffaf0", "#7a5a10", None, "#ffe680", shadow_off=(2, 2), soft=0.6,
                  grow=1.0, inner=1.3, bias=(0.8, 1.0))
    c.save()
    return c


# ------------------------------------------------------------------ body --
def body():
    c = Canvas("body", (-15.0, 3.0, 14.0, 16.5))
    det = [c.blotch(-8.0, 12.5, 2.4, 1.4, 11), c.blotch(6.0, 6.0, 2.6, 1.2, 12), c.blotch(-2.0, 5.2, 7.0, 0.9, 13,
                                                                                          "#8e816d")]
    # the belt: two carved bands with a row of studs
    det += c.groove([(-10.6, 8.0), (9.6, 8.0)], 0.4) + c.groove([(-10.0, 6.4), (9.0, 6.4)], 0.3)
    for k in range(7):
        x = -8.6 + k * 2.8
        det.append(fill_svg(smooth_path(ellipse_pts(*c.u(x, 7.2), c.s(0.42), c.s(0.42), 8)), GROOVE))
    # the volcano emblem: a carved spiral
    sp = [(-1.0 + 2.8 * (1 - k / 40.0) * math.cos(k * 0.42), 11.6 + 2.8 * (1 - k / 40.0) * math.sin(k * 0.42))
          for k in range(40)]
    det += c.groove(sp, 0.4)
    det += c.lava([(9.6, 15.0), (8.2, 12.6), (9.4, 10.6), (8.4, 8.6)])
    det += c.lava([(-11.6, 14.4), (-10.2, 12.6), (-10.6, 11.2)])
    torso = [(-9.0, 4.2), (-2.0, 3.9), (8.4, 4.2), (10.8, 9.0), (12.6, 13.4), (11.6, 15.4), (2.0, 15.6),
             (-13.4, 15.4), (-14.0, 13.0), (-11.6, 8.6)]
    c.stone(torso, details=det, hl=(0.32, 0.22, 0.24, 0.16), seed=11)
    c.save()
    return c


def arm(front):
    c = Canvas("arm_f" if front else "arm_b", (-5.0, 1.0, 5.0, 16.5))
    tone = STONE if front else STONE_B
    upper = [(-3.0, 15.6), (0.2, 15.9), (3.0, 15.4), (2.7, 11.0), (2.6, 7.0), (-2.6, 7.0), (-2.9, 11.0)]
    c.stone(upper, tone, details=c.groove([(-2.6, 11.0), (2.4, 11.6)], 0.32) + [c.blotch(1.2, 13.0, 1.0, 1.6, 21)],
            hl=(0.36, 0.2, 0.22, 0.14), seed=21 if front else 22)
    fist = [(-4.0, 6.6), (0.0, 7.0), (4.0, 6.6), (4.2, 3.6), (3.4, 1.4), (-3.6, 1.4), (-4.2, 3.6)]
    det = []
    for kx in (-1.9, -0.2, 1.5):
        det += c.groove([(kx - 0.9, 2.0), (kx - 0.9, 4.0)], 0.34)
    det += c.groove([(-3.6, 4.6), (3.4, 4.8)], 0.26)
    c.stone(fist, tone, details=det, hl=(0.34, 0.2, 0.24, 0.16), seed=23 if front else 24)
    c.save()
    return c


def foot(front):
    c = Canvas("foot_f" if front else "foot_b", (-6.0, -0.4, 6.0, 5.0))
    tone = STONE if front else STONE_B
    pts = [(-5.4, 0.2), (0.0, 0.0), (5.2, 0.2), (4.8, 2.6), (4.6, 4.2), (-3.6, 4.3), (-5.6, 2.8)]
    det = []
    for tx in (-4.2, -2.6, -1.0):
        det += c.groove([(tx, 0.6), (tx + 0.4, 2.2)], 0.32)
    det += c.groove([(-3.4, 3.2), (4.2, 3.3)], 0.22)
    det.append(c.blotch(1.5, 1.0, 3.0, 0.6, 31, "#8e816d"))
    c.stone(pts, tone, details=det, hl=(0.36, 0.22, 0.26, 0.18), seed=31 if front else 32)
    c.save()
    return c


RUBBLE = [((-6.5, 10.0), [(-4.2, 3.6), (3.4, 2.8), (4.6, -1.8), (0.6, -3.8), (-4.6, -2.4)]),
          ((5.0, 11.0), [(-3.6, 3.0), (3.0, 3.4), (3.8, -2.6), (-1.2, -3.4), (-4.0, -0.8)]),
          ((-1.5, 6.2), [(-3.0, 2.6), (3.6, 2.2), (2.8, -2.4), (-2.6, -2.8)]),
          ((7.6, 5.8), [(-2.6, 2.4), (2.4, 2.0), (2.6, -2.0), (-2.2, -2.4)]),
          ((-10.5, 5.4), [(-2.8, 2.2), (2.6, 2.8), (3.0, -2.2), (-2.4, -2.6)]),
          ((1.5, 13.0), [(-3.4, 1.8), (3.6, 2.4), (3.0, -2.0), (-1.0, -2.6), (-3.6, -1.0)])]
RUBBLE_K = 1.45   # statue_model.py draws each chunk 1.45x its outline, on a 14x12 drawing-unit card (208x176)


def rubble(i):
    (mx, my), pts = RUBBLE[i]
    pts = [(x * RUBBLE_K, y * RUBBLE_K) for x, y in pts]
    c = Canvas(f"rubble_{i + 1}", (mx - 7.0, my - 6.0, mx + 7.0, my + 6.0))
    det = [c.blotch(mx + 1.0, my - 1.0, 1.4, 0.9, 40 + i)]
    # each chunk keeps a scrap of the body's carving, and a broken (darker) face
    if i == 0:
        det += c.lava([(mx - 1.0, my + 2.0), (mx + 1.0, my - 0.4)])
    if i == 1:
        det += c.groove([(mx - 2.4, my - 0.6), (mx + 2.6, my - 0.4)], 0.32)
    if i == 2:
        det += c.groove([(mx - 2.0, my + 0.6), (mx + 2.4, my + 0.6)], 0.36)
    if i == 3:
        det += c.groove([(mx - 1.0, my + 1.0), (mx + 0.6, my - 0.4), (mx - 0.2, my - 1.4)], 0.3)
    if i == 4:   # a corner of a zigzag glyph
        det += c.groove([(mx - 2.4, my - 0.4), (mx - 1.2, my + 1.0), (mx, my - 0.4), (mx + 1.2, my + 1.0)], 0.3)
    if i == 5:   # a hairline lava crack and a groove
        det += c.lava([(mx - 2.0, my + 0.4), (mx - 0.4, my - 0.2), (mx + 0.6, my - 1.6)])
        det += c.groove([(mx + 1.2, my + 1.6), (mx + 3.4, my + 1.4)], 0.3)
    det.append(c.blotch(mx - 1.6, my + 1.2, 0.9, 0.6, 50 + i))
    jag = []
    for k, (x, y) in enumerate(pts):   # broken edges: a notch between every two corners
        jag.append((mx + x, my + y))
        nx, ny = pts[(k + 1) % len(pts)]
        jag.append((mx + (x + nx) / 2 * 0.93, my + (y + ny) / 2 * 0.93))
    c.stone(jag, STONE if i % 2 == 0 else STONE_B, details=det, hl=(0.36, 0.26, 0.2, 0.14), seed=41 + i, light_off=(5, 5),
            tension=0.15)
    c.save()
    return c


# ------------------------------------------------------------- preview --
# rest placement (statue_model.SPEC / OFFSETS), drawing units: x offset of the parts drawn about x = 0
OFFS = {"foot_b": 5.5, "foot_f": -5.0, "arm_b": 12.5, "arm_f": -14.5}
ORDER = ["foot_b", "arm_b", "body", "head", "eyes", "gem", "arm_f", "foot_f"]


def place(canvas, cards, name, file=None, ox=0, oy=0, dx=0.0, scale=1.0):
    cd = cards[file or name]
    im = Image.open(os.path.join(OUT, cd.name + ".png")).convert("RGBA")
    x0 = (cd.rect[0] + OFFS.get(name, 0.0) + dx) * PX
    yb = cd.rect[1] * PX
    if scale != 1.0:
        im = im.resize((int(im.width * scale), int(im.height * scale)), Image.NEAREST)
    canvas.alpha_composite(im, (int(ox + x0 * scale), int(oy - yb * scale - im.height)))


def preview(cards):
    W, H = 1500, 1180
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    d.text((10, 6), "c_lp_statue redrawn: assembled at rest, 1:1 (8 texels a model unit); 68 units tall", fill=(40, 30, 30, 255))
    d.rectangle([0, 1100, 760, 1102], fill=(180, 170, 150, 255))
    for n in ORDER:
        place(c, cards, n, ox=430, oy=1100)
    d.rectangle([700, 1100 - 192, 704, 1100], fill=(60, 140, 60, 255))
    d.text((708, 1100 - 192), "Luigi", fill=(40, 90, 40, 255))
    # variants
    x, y = 780, 30
    for label, names in (("eyes lit / dark", ["eyes", "eyes_dark"]), ("gem whole / glowing / cracked / loose, glinting",
                                                                        ["gem", "gem_glow", "gem_cracked", "gem_glint"]),
                         ("rubble chunks 1-3", ["rubble_1", "rubble_2", "rubble_3"]),
                         ("rubble chunks 4-6", ["rubble_4", "rubble_5", "rubble_6"])):
        d.rectangle([x, y, x + 700, y + 14], fill=(225, 218, 205, 255))
        d.text((x + 4, y + 2), label, fill=(40, 30, 30, 255))
        xx, hmax = x, 0
        for n in names:
            im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
            c.alpha_composite(im, (xx, y + 20))
            d.text((xx, y + 22 + im.height), n, fill=(70, 50, 50, 255))
            xx += im.width + 14
            hmax = max(hmax, im.height)
        y += hmax + 56
    # the defeated look: eyes dark, gem cracked
    d.rectangle([x, y, x + 700, y + 14], fill=(225, 218, 205, 255))
    d.text((x + 4, y + 2), "head: lit + gem glowing (charging)  |  dark + cracked (defeated), x0.5", fill=(40, 30, 30, 255))
    for k, (e, g) in enumerate((("eyes", "gem_glow"), ("eyes_dark", "gem_cracked"))):
        ox, oy = x + 30 + k * 300 - int(-15.0 * PX * 0.5), y + 20 + int(38.6 * PX * 0.5)
        for n, f in (("head", None), ("eyes", e), ("gem", g)):
            place(c, cards, n, f, ox=ox, oy=oy, scale=0.5)
    return c


def build():
    cards = [head(), eyes(True), eyes(False), gem(0), gem(1), gem(2), body(), arm(True), arm(False),
             foot(True), foot(False), gem(3)] + [rubble(i) for i in range(6)]
    cmap = {cd.name: cd for cd in cards}
    preview(cmap).convert("RGB").save(os.path.join(OUT, "preview.png"))
    return cmap


if __name__ == "__main__":
    cm = build()
    for n, cd in cm.items():
        print(n, cd.W, cd.H)
