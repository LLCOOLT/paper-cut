"""The walking stone statue, the boss of Super Luigi's chapter 1 (Rumblebump
Volcano; docs/SUPER_LUIGI.md): a new model of our own, `c_lp_statue`, built by
paper_model.py (docs/LUIGI_ANIMS.md, "New models").

An ancient idol, 68 units tall (nearly three Luigis): a huge block of a head
under a heavy brow, eyes that glow, and the red gem in its forehead (Blooey is
thrown at it), broad shoulders, long stone arms with fists near the ground,
short legs on block feet. Lava glows in its cracks. Facing left.

The drawings are made here (Canvas), or come from PNGs: a file
port/assets/super_luigi/art/statue/<file>.png replaces that drawing (its
bottom left corner where the drawing's is, 8 texels a unit; `spec()` and
docs/LUIGI_ANIMS.md list every part's file, size, place and pivot).

Parts (groups):
  |locator1   the root              locator2  the whole (the mover)
  torso       an empty: the upper body's mover, pivot at the hips
  body        the body's drawing (the torso's child): hidden as it shatters
  head        the head (the torso's child), pivot at the neck
  eyes        the eyes' glow (the head's child): lit, dark
  gem         the gem (the head's child): whole, glowing, cracked
  arm_f arm_b the arms (the torso's), pivots at the shoulders
  foot_f foot_b the feet (locator2's), pivots at the heels
  rubble_1..4 chunks of the body (locator2's), hidden until it shatters
  gem_loose   the cracked gem fallen free (locator2's), hidden until then
"""
import math
import os

import paper_model as PM
from paper_model import sd_box, sd_circle, sd_ellipse, sd_poly

NAME = "c_lp_statue"
SCALE = 1.8  # drawn in units of a 38-tall idol, built 68 tall
PX = 8 * SCALE  # texels a drawing unit: 8 a model unit, as the game's
SIZE = None  # the header's (radius, height) words: from its bounds (paper_model.build)
ART = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                   "port", "assets", "super_luigi", "art", "statue")

STONE = dict(fill="#a39684", shade="#776c5f", light="#c4b9a7", outline="#3d352c", shade_off=(8.0, 7.0),
             light_off=(-5.0, -5.0))
STONE_B = dict(STONE, fill="#948876", shade="#6c6256", light="#b2a795")  # the far side, a tone darker
LAVA = ("#e8541c", "#ffd04a")  # the glow in its cracks: the rim, the core
CARVE = "#5f564a"
DARK = "#4a4239"


def _canvas(rect):
    x0, y0, x1, y1 = rect
    w, h = int(round((x1 - x0) * PX)), int(round((y1 - y0) * PX))
    c = PM.Canvas(w + (-w % 8), h + (-h % 8))
    top = y0 + (h + (-h % 8)) / PX  # grown at the top: the bottom left corner stays put
    c.u = lambda x, y: ((x - x0) * PX, (top - y) * PX)
    c.s = lambda d: d * PX
    c.origin = (x0, y0)
    return c


def _poly(c, pts, r=0.0):
    return sd_poly([c.u(x, y) for x, y in pts], c.s(r))


def _stone(c, f, tone=STONE, width=3.5, grain=60, seed=1):
    c.shape(f, outline=tone["outline"], width=width, **{k: v for k, v in tone.items() if k != "outline"})
    c.speckle("#8d826f", grain, seed, inside=f)
    c.speckle("#c9bfae", grain // 2, seed + 1, size=(0.6, 1.2), inside=f)


def _lava(c, pts):
    """a crack glowing with the volcano's heat"""
    c.line(pts, LAVA[0], width=c.s(0.55))
    c.line(pts, LAVA[1], width=c.s(0.2))


# the eye sockets, slanted down toward the nose: a scowl
_EYES = [[(-10.0, 25.4), (-3.6, 23.9), (-3.8, 21.8), (-9.6, 22.8)],
         [(0.6, 23.9), (7.0, 25.4), (7.2, 22.8), (0.8, 21.8)]]
GEM_C = (-1.5, 31.8)  # the gem's middle, drawing units


def draw_head():
    c = _canvas((-15.0, 13.0, 13.5, 38.5))
    u, s = c.u, c.s
    # a huge block: broad at the brow, a heavy jaw, the crown a little rounded
    block = _poly(c, [(-13.0, 14.5), (11.5, 14.5), (12.4, 27.5), (11.0, 36.4), (-12.0, 37.2), (-14.0, 28.0)], 0.8)
    _stone(c, block, grain=120, seed=7)
    # the brow, a ledge shading the eyes
    brow = _poly(c, [(-14.2, 25.8), (12.4, 25.8), (12.2, 28.2), (-14.2, 28.4)], 0.3)
    c.shape(brow, "#b3a793", outline=STONE["outline"], width=3.0, shade="#857a69", shade_off=(0, 5))
    for pts in _EYES:
        c.shape(_poly(c, pts, 0.7), DARK, outline=STONE["outline"], width=2.5)
    # the nose: a long flat wedge, toward the front; a grim mouth
    nose = _poly(c, [(-5.6, 23.2), (-1.4, 23.2), (-0.6, 18.4), (-7.4, 18.2)], 0.4)
    c.shape(nose, STONE["fill"], outline=STONE["outline"], width=3.0, shade=STONE["shade"], shade_off=(5, 0))
    c.line([u(-10.0, 16.8), u(-3.6, 16.2), u(4.2, 16.6)], CARVE, width=s(0.5))
    c.line([u(-8.0, 15.6), u(2.0, 15.5)], "#8b806f", width=s(0.3))
    # glyphs on the cheeks, cracks, a chip
    c.line([u(7.6, 22.0), u(9.6, 20.2), u(7.6, 18.4), u(9.6, 16.6)], CARVE, width=s(0.4))
    c.line([u(-12.6, 22.0), u(-11.4, 20.0), u(-12.6, 18.0)], CARVE, width=s(0.4))
    _lava(c, [u(10.6, 35.6), u(8.8, 33.0), u(9.8, 30.6), u(8.6, 29.0)])
    c.line([u(-12.0, 34.0), u(-10.2, 32.4)], CARVE, width=s(0.3))
    # the gem's setting: a deep round socket with a carved ring (the gem is its own part)
    ring = sd_circle(*u(*GEM_C), s(5.4))
    c.shape(ring, "#8a7f6e", outline=STONE["outline"], width=3.0, shade="#6e6457", shade_off=(-3, -3))
    c.shape(sd_circle(*u(*GEM_C), s(4.2)), "#3e3630", outline="#2e2721", width=2.0)
    for k in range(8):
        a = k * math.pi / 4
        c.line([u(GEM_C[0] + 4.7 * math.cos(a), GEM_C[1] + 4.7 * math.sin(a)),
                u(GEM_C[0] + 5.3 * math.cos(a), GEM_C[1] + 5.3 * math.sin(a))], CARVE, width=s(0.3))
    return c


def draw_eyes(lit=True):
    c = _canvas((-11.0, 20.6, 8.6, 26.6))
    for pts in _EYES:
        mx = sum(x for x, _y in pts) / 4.0
        my = sum(y for _x, y in pts) / 4.0
        inner = [(mx + (x - mx) * 0.86, my + (y - my) * 0.7) for x, y in pts]
        if lit:
            c.shape(_poly(c, pts, 0.6), (255, 170, 48))
            c.shape(_poly(c, inner, 0.3), "#fff2a0", outline="#ffb020", width=2.0, light="#ffffff",
                    light_off=(-2, -2))
        else:
            c.shape(_poly(c, inner, 0.3), "#3a332c", outline="#2a241f", width=2.0)
    if lit:  # the glow's rim half see-through
        for row in c.px:
            for i, (r, g, b, a) in enumerate(row):
                if a > 0 and r > 250 and g < 200:
                    row[i] = (r, g, b, a * 0.55)
    return c


def draw_gem(state=0):
    """0 whole, 1 glowing, 2 cracked, 3 cracked and glinting (fallen free)"""
    gx, gy = GEM_C
    c = _canvas((gx - 5.0, gy - 5.0, gx + 5.0, gy + 5.0))
    u, s = c.u, c.s
    gem = _poly(c, [(gx, gy + 4.2), (gx + 3.8, gy), (gx, gy - 4.2), (gx - 3.8, gy)], 0.3)
    fill, shade, light = ("#e3322a", "#9e1a16", "#ff8a78")
    if state in (1, 3):
        c.shape(sd_circle(*u(gx, gy), s(4.9)), (255, 120, 90) if state == 1 else (255, 230, 170))
        for row in c.px:
            for i, (r, g, b, a) in enumerate(row):
                row[i] = (r, g, b, a * 0.6)
        fill, shade, light = ("#ff5a3c", "#d0281e", "#fff0d0")
    c.shape(gem, fill, outline="#4e0c0a", width=3.0, shade=shade, shade_off=(4, 4), light=light, light_off=(-3, -3))
    c.line([u(gx, gy + 4.0), u(gx, gy - 4.0)], "#b22520", width=s(0.2))
    c.line([u(gx - 3.6, gy), u(gx + 3.6, gy)], "#b22520", width=s(0.2))
    c.line([u(gx - 1.8, gy + 2.1), u(gx + 1.8, gy - 2.1)], "#c8302a", width=s(0.14))
    c.shape(sd_ellipse(*u(gx - 1.5, gy + 1.6), s(0.8), s(0.5)), "#ffffff")
    if state in (2, 3):
        c.line([u(gx - 1.4, gy + 3.6), u(gx + 0.6, gy + 1.2), u(gx - 0.8, gy - 0.6), u(gx + 1.0, gy - 3.2)],
               "#2a0605", width=s(0.36))
        c.line([u(gx + 0.6, gy + 1.2), u(gx + 2.6, gy + 0.4)], "#2a0605", width=s(0.28))
    if state == 3:  # a four-pointed sparkle at its top left
        sx_, sy_ = gx - 2.2, gy + 2.4
        star = sd_poly([u(sx_, sy_ + 2.4), u(sx_ + 0.45, sy_ + 0.45), u(sx_ + 2.4, sy_), u(sx_ + 0.45, sy_ - 0.45),
                        u(sx_, sy_ - 2.4), u(sx_ - 0.45, sy_ - 0.45), u(sx_ - 2.4, sy_), u(sx_ - 0.45, sy_ + 0.45)])
        c.shape(star, "#ffffff", outline="#ffe680", width=1.5)
    return c


def draw_body():
    c = _canvas((-15.0, 3.0, 14.0, 16.5))
    u, s = c.u, c.s
    torso = _poly(c, [(-9.0, 4.2), (8.4, 4.2), (12.6, 13.4), (11.6, 15.4), (-13.4, 15.4), (-14.0, 13.0)], 0.6)
    _stone(c, torso, grain=110, seed=11)
    c.line([u(-10.0, 7.6), u(9.4, 7.6)], CARVE, width=s(0.45))
    c.line([u(-9.8, 6.6), u(9.0, 6.6)], "#8b806f", width=s(0.3))
    sp = [u(-1.0 + 2.8 * (1 - k / 40.0) * math.cos(k * 0.42), 11.4 + 2.8 * (1 - k / 40.0) * math.sin(k * 0.42))
          for k in range(40)]
    c.line(sp, CARVE, width=s(0.45))
    _lava(c, [u(9.6, 15.0), u(8.2, 12.6), u(9.4, 10.6), u(8.4, 8.4)])
    _lava(c, [u(-11.6, 14.4), u(-10.2, 12.6)])
    return c


def draw_arm(front=True):
    c = _canvas((-5.0, 1.0, 5.0, 16.5))
    u, s = c.u, c.s
    tone = STONE if front else STONE_B
    upper = _poly(c, [(-3.0, 15.4), (3.0, 15.4), (2.6, 7.0), (-2.6, 7.0)], 0.7)
    _stone(c, upper, tone, 3.0, 30, 21 if front else 22)
    fist = sd_box(*u(0.0, 4.6), s(4.0), s(3.4), s(1.5))
    _stone(c, fist, tone, 3.0, 24, 23 if front else 24)
    for kx in (-1.8, -0.2, 1.4):
        c.line([u(kx - 1.0, 2.4), u(kx - 1.0, 3.8)], CARVE, width=s(0.32))
    c.line([u(-2.6, 11.0), u(2.4, 11.6)], CARVE, width=s(0.32))
    return c


def draw_foot(front=True):
    c = _canvas((-6.0, -0.4, 6.0, 5.0))
    u, s = c.u, c.s
    foot = _poly(c, [(-5.4, 0.2), (5.2, 0.2), (4.6, 4.2), (-3.6, 4.2), (-5.4, 2.8)], 0.5)
    _stone(c, foot, STONE if front else STONE_B, 3.0, 26, 31 if front else 32)
    for tx in (-4.2, -2.6):
        c.line([u(tx, 0.6), u(tx + 0.4, 2.0)], CARVE, width=s(0.32))
    return c


_RUBBLE = [  # the body's chunks: (middle, outline points about it), drawing units
    ((-6.5, 10.0), [(-4.2, 3.6), (3.4, 2.8), (4.6, -1.8), (0.6, -3.8), (-4.6, -2.4)]),
    ((5.0, 11.0), [(-3.6, 3.0), (3.0, 3.4), (3.8, -2.6), (-1.2, -3.4), (-4.0, -0.8)]),
    ((-1.5, 6.2), [(-3.0, 2.6), (3.6, 2.2), (2.8, -2.4), (-2.6, -2.8)]),
    ((7.6, 5.8), [(-2.6, 2.4), (2.4, 2.0), (2.6, -2.0), (-2.2, -2.4)]),
    ((-10.5, 5.4), [(-2.8, 2.2), (2.6, 2.8), (3.0, -2.2), (-2.4, -2.6)]),
    ((1.5, 13.0), [(-3.4, 1.8), (3.6, 2.4), (3.0, -2.0), (-1.0, -2.6), (-3.6, -1.0)]),
]
# each chunk's flight: how far out (model units), how high, how far it turns
_RUBBLE_FLIGHT = [(-44.0, 22.0, 200.0), (42.0, 26.0, -230.0), (-20.0, 14.0, 120.0), (32.0, 12.0, -140.0),
                  (-34.0, 10.0, 160.0), (14.0, 30.0, -260.0)]


def draw_rubble(i):
    (mx, my), pts = _RUBBLE[i]
    c = _canvas((mx - 7.0, my - 6.0, mx + 7.0, my + 6.0))
    chunk = _poly(c, [(mx + x * 1.45, my + y * 1.45) for x, y in pts], 0.6)
    _stone(c, chunk, STONE if i % 2 == 0 else STONE_B, 3.0, 18, 41 + i)
    if i == 0:
        _lava(c, [c.u(mx - 1.0, my + 2.0), c.u(mx + 1.0, my - 0.4)])
    return c


# every part: name, parent, drawings (file name: maker), z, pivot (drawing units);
# a part's first drawing is its rest one, the rest its texture's other binds
SPEC = [
    ("|locator1", None, [], 0.0, (0, 0)),
    ("locator2", "|locator1", [], 0.0, (0, 0)),
    ("foot_b", "locator2", [("foot_b", lambda: draw_foot(False))], -1.2, (5.5, 0.8)),
    ("torso", "locator2", [], 0.0, (-0.5, 5.0)),
    ("arm_b", "torso", [("arm_b", lambda: draw_arm(False))], -0.8, (12.5, 14.5)),
    ("body", "torso", [("body", draw_body)], 0.0, (-0.5, 5.0)),
    ("head", "torso", [("head", draw_head)], 0.4, (-0.5, 15.0)),
    ("eyes", "head", [("eyes", lambda: draw_eyes(True)), ("eyes_dark", lambda: draw_eyes(False))], 0.6,
     (-1.2, 23.6)),
    ("gem", "head", [("gem", lambda: draw_gem(0)), ("gem_glow", lambda: draw_gem(1)),
                     ("gem_cracked", lambda: draw_gem(2))], 0.8, GEM_C),
    ("arm_f", "torso", [("arm_f", lambda: draw_arm(True))], 1.4, (-14.5, 14.5)),
    ("foot_f", "locator2", [("foot_f", lambda: draw_foot(True))], 1.0, (-5.0, 0.8)),
    ("rubble_1", "locator2", [("rubble_1", lambda: draw_rubble(0))], 0.3, _RUBBLE[0][0]),
    ("rubble_2", "locator2", [("rubble_2", lambda: draw_rubble(1))], 0.2, _RUBBLE[1][0]),
    ("rubble_3", "locator2", [("rubble_3", lambda: draw_rubble(2))], 0.5, _RUBBLE[2][0]),
    ("rubble_4", "locator2", [("rubble_4", lambda: draw_rubble(3))], -0.4, _RUBBLE[3][0]),
    ("rubble_5", "locator2", [("rubble_5", lambda: draw_rubble(4))], 0.6, _RUBBLE[4][0]),
    ("rubble_6", "locator2", [("rubble_6", lambda: draw_rubble(5))], -0.2, _RUBBLE[5][0]),
    ("gem_loose", "locator2", [("gem_cracked", lambda: draw_gem(2)), ("gem_glint", lambda: draw_gem(3))], 1.2,
     GEM_C),
]
# where the parts drawn about x = 0 go (drawing units); their pivots above are where they end up
OFFSETS = {"foot_b": (5.5, 0.0), "foot_f": (-5.0, 0.0), "arm_b": (12.5, 0.0), "arm_f": (-14.5, 0.0)}
HIDDEN = {"rubble_%d" % i for i in range(1, 7)} | {"gem_loose"}


# a drawing with no PNG of its own could use another's; every drawing has its
# own now (rubble_1..6 at 208x176, gem_glint), so none does
ART_FALLBACK = {}


def _resize(rows, w, h):
    """bilinear, alpha-weighted: an art PNG to the size of the drawing it stands for"""
    sh, sw = len(rows), len(rows[0])
    out = []
    for y in range(h):
        fy = (y + 0.5) * sh / h - 0.5
        y0 = max(0, min(sh - 1, int(fy // 1)))
        y1, ty = min(sh - 1, y0 + 1), min(1.0, max(0.0, fy - y0))
        row = []
        for x in range(w):
            fx = (x + 0.5) * sw / w - 0.5
            x0 = max(0, min(sw - 1, int(fx // 1)))
            x1, tx = min(sw - 1, x0 + 1), min(1.0, max(0.0, fx - x0))
            acc, wsum = [0.0, 0.0, 0.0, 0.0], 0.0
            for (yy, wy) in ((y0, 1 - ty), (y1, ty)):
                for (xx, wx) in ((x0, 1 - tx), (x1, tx)):
                    r, g, b, a = rows[yy][xx]
                    k = wy * wx
                    acc[3] += a * k
                    acc[0] += r * a * k
                    acc[1] += g * a * k
                    acc[2] += b * a * k
                    wsum += k
            a = acc[3] / wsum if wsum else 0.0
            row.append((int(acc[0] / acc[3]) if acc[3] else 0, int(acc[1] / acc[3]) if acc[3] else 0,
                        int(acc[2] / acc[3]) if acc[3] else 0, int(round(a))))
        out.append(row)
    return out


def _image(file, maker):
    """(rows, bottom left corner in drawing units): the art's PNG if there is
    one (or its fallback's), scaled to the drawing's size when it was made for
    another (the chunks grew), else the drawing"""
    c = maker()
    for name in (file, ART_FALLBACK.get(file)):
        path = os.path.join(ART, "%s.png" % name) if name else None
        if path and os.path.exists(path):
            rows = PM.pad8(PM.read_png(path))
            if (len(rows[0]), len(rows)) != (c.w, c.h):
                rows = _resize(rows, c.w, c.h)
            return rows, c.origin
    return c.rows(), c.origin


def parts():
    """the model's parts, drawn (or read from the art folder), in model units"""
    out = []
    for name, parent, files, z, pivot in SPEC:
        dx, dy = OFFSETS.get(name, (0.0, 0.0))
        if not files:
            out.append(PM.Part(name, parent, pivot=(pivot[0] * SCALE, pivot[1] * SCALE), z=z))
            continue
        imgs = [_image(f, m) for f, m in files]
        rows, (x0, y0) = imgs[0]
        h, w = len(rows), len(rows[0])
        x0, y0 = (x0 + dx) * SCALE, (y0 + dy) * SCALE
        rect = (x0, y0, x0 + w / 8.0, y0 + h / 8.0)
        out.append(PM.Part(name, parent, rows, rect, z=z, pivot=(pivot[0] * SCALE, pivot[1] * SCALE),
                           binds=[r for r, _o in imgs[1:]], visible=name not in HIDDEN))
    return out


def spec():
    """[(part, parent, files, size in texels, bottom left corner, pivot, z)] in model units, for the art"""
    rows = []
    for pt, (name, parent, files, z, _p) in zip(parts(), SPEC):
        size = (len(pt.image[0]), len(pt.image)) if pt.image else None
        corner = (round(pt.rect[0], 2), round(pt.rect[1], 2)) if pt.rect else None
        rows.append((name, parent, [f for f, _m in files], size, corner,
                     (round(pt.pivot[0], 2), round(pt.pivot[1], 2)), z))
    return rows


def build(anims=()):
    """(model, textures) bytes"""
    anims = list(anims)
    if not any(x.name == "Z_1" for x in anims):
        anims.insert(0, PM.still("Z_1"))  # the still pose, first, as every model has
    return PM.build(NAME, parts(), anims, size=SIZE)


# ---------------------------------------------------------------- motions ----
# Key poses per part, offsets from rest in model units (as friend_motions.py),
# in the shared style (docs/LUIGI_ANIMS.md), slowed for its weight: a step takes
# 48 frames, a partner's 20; every footfall lands hard (a sharp key), the torso
# dropping after it and the arms swinging on late. Its idle grinds: small turns
# that catch and hold (stepped keys), not a smooth bob.
#
# IMPACTS: the frames a foot or fist hits the ground, for the battle code's
# screen shake, camera hop and rock-dust puff.
IMPACTS = {"W_1": [24, 72], "R_1": [15, 45], "A_1B": [6], "A_2B": [7], "D_2": [], "D_3": [34, 46, 52, 58],
           "D_4": [20]}

_STEP = {"ease": "step"}


def _walk(end, lift, stride, dip, lean):
    """two heavy steps in `end` frames, keyed every eighth: a foot heaves up
    (slow), swings ahead, lands (sharp) and is pushed back under the body"""
    keys = []
    for k in range(9):
        f = int(round(k * end / 8.0))
        pose = {}
        for i, g in enumerate(("foot_f", "foot_b")):
            t = ((k / 8.0) - 0.5 * i) % 1.0
            if t < 0.25:
                u = t / 0.25
                ch = {"tx": stride * (1 - 1.2 * u), "ty": lift * u, "rz": 12.0 * u}
            elif t < 0.5:
                u = (t - 0.25) / 0.25
                ch = {"tx": stride * (-0.2 - 0.8 * u), "ty": lift * (1 - u), "rz": 12.0 * (1 - u)}
            else:
                u = (t - 0.5) / 0.5
                ch = {"tx": -stride + 2 * stride * u, "ty": 0.0, "rz": 0.0}
            pose[g] = ch
        step = k % 4
        side = 1 if (k // 4) % 2 == 0 else -1
        pose["torso"] = {"ty": {0: 0.0, 1: 1.2, 2: -dip, 3: -dip * 0.4}[step],
                         "rz": side * lean * {0: 0.2, 1: 1.0, 2: 0.7, 3: 0.4}[step]}
        if step == 2:
            pose["torso"]["sy"] = 0.9375
        pose["head"] = {"rz": -side * lean * {0: 0.2, 1: 0.6, 2: -0.5, 3: 0.1}[step],
                        "ty": {0: 0.0, 1: 0.0, 2: -dip * 0.5, 3: 0.2}[step]}
        pose["arm_f"] = {"rz": side * {0: 2.0, 1: 8.0, 2: 12.0, 3: 6.0}[step]}
        pose["arm_b"] = {"rz": -side * {0: 2.0, 1: 8.0, 2: 12.0, 3: 6.0}[step]}
        keys.append((f, pose, {"ease": "vector"} if step == 2 else {}))
    return {"end": end, "loop": True, "keys": keys}


MOTIONS = {}
# standing: stone grinding as it breathes, small turns that catch and hold; the
# gem glints (120)
# standing (ART_PLAN: its idle was too still): the long slow sway the game's bosses have, head and arms
# out of step over 240 frames, grinding: small turns that catch and hold; the gem glints (at 120)
def _stand_key(f):
    p = 2 * math.pi * f / 240.0
    return {"torso": {"rz": 4.0 * math.sin(p), "ty": 0.5 * math.sin(2 * p)},
            "head": {"rz": 12.0 * math.sin(p + 0.9)}, "arm_f": {"rz": 16.0 * math.sin(p + 1.8)},
            "arm_b": {"rz": -12.0 * math.sin(p + 2.6)}, "gem": {"tex": 1 if 116 <= f <= 126 else 0}}


MOTIONS["S_1"] = {"end": 240, "loop": True, "keys": [(f, _stand_key(f)) for f in range(0, 241, 20)] }
MOTIONS["W_1"] = _walk(96, lift=7.0, stride=4.5, dip=2.2, lean=3.0)
MOTIONS["R_1"] = _walk(60, lift=8.0, stride=6.0, dip=2.6, lean=5.0)
# talking (a rumble): the head grinds side to side, the eyes flicker
MOTIONS["T_1"] = {"end": 24, "loop": True, "keys": [
    (0, {"head": {"rz": -2.0, "ty": 0.0}, "eyes": {"tex": 0}}),
    (6, {"head": {"rz": 2.0, "ty": 0.8}, "eyes": {"tex": 0}}),
    (12, {"head": {"rz": -1.0, "ty": 0.2}, "eyes": {"tex": 0}}),
    (16, {"head": {"rz": 1.0, "ty": 0.6}, "eyes": {"tex": 1}}),
    (18, {"head": {"rz": 0.0, "ty": 0.4}, "eyes": {"tex": 0}}),
    (24, {"head": {"rz": -2.0, "ty": 0.0}, "eyes": {"tex": 0}}),
]}
# the stomp: rears back on one leg, the front foot high, held there trembling
# (A_1A, 40), then brings it down with all its weight (A_1B; the impact at 6)
_REAR = {"locator2": {"rz": -7.0}, "torso": {"rz": -5.0, "ty": 1.4}, "head": {"rz": -5.0},
         "foot_f": {"tx": -2.0, "ty": 14.0, "rz": 20.0}, "arm_f": {"rz": -45.0}, "arm_b": {"rz": 35.0}}
MOTIONS["A_1A"] = {"end": 40, "keys": [
    (0, {}),
    (16, {"locator2": {"rz": -3.0}, "torso": {"rz": -2.0, "ty": -1.4, "sy": 0.9375}, "head": {"rz": -1.0},
          "foot_f": {"tx": 0.0, "ty": 5.0, "rz": 8.0}, "arm_f": {"rz": -15.0}, "arm_b": {"rz": 10.0}}),
    (32, _REAR),
    (36, dict(_REAR, locator2={"rz": -7.5, "tx": 0.3})),
    (40, dict(_REAR, foot_f={"tx": -2.0, "ty": 14.6, "rz": 21.0})),
]}
MOTIONS["A_1B"] = {"end": 44, "keys": [
    (0, _REAR),
    (6, {"locator2": {"rz": 4.0}, "torso": {"rz": 4.0, "ty": -3.6, "sy": 0.9375}, "head": {"rz": 3.0, "ty": -1.6},
         "foot_f": {"tx": -6.0, "ty": 0.0, "rz": -4.0}, "arm_f": {"rz": 34.0}, "arm_b": {"rz": -22.0}},
     {"ease": "vector"}),
    (10, {"locator2": {"rz": 2.5}, "torso": {"rz": 2.5, "ty": -2.0}, "head": {"rz": 1.2, "ty": -0.4},
          "foot_f": {"tx": -6.0}, "arm_f": {"rz": 22.0}, "arm_b": {"rz": -14.0}}),
    (24, {"locator2": {"rz": 1.0}, "torso": {"ty": -0.6}, "foot_f": {"tx": -5.0}, "arm_f": {"rz": 6.0}}),
    (44, {"foot_f": {"tx": -2.0}}),
]}
# the punch: the front arm hauls back (A_2A, held), and round in a great arc
# into the ground before it (A_2B; the fist lands at 7)
_WIND = {"torso": {"rz": -7.0}, "head": {"rz": -4.0}, "arm_f": {"rz": -75.0}, "arm_b": {"rz": 18.0},
         "foot_b": {"tx": 1.5}}
MOTIONS["A_2A"] = {"end": 30, "keys": [(0, {}), (22, _WIND), (30, dict(_WIND, arm_f={"rz": -79.0}))]}
MOTIONS["A_2B"] = {"end": 44, "keys": [
    (0, _WIND),
    (7, {"torso": {"rz": 12.0, "ty": -1.6}, "head": {"rz": 7.0}, "arm_f": {"rz": 120.0, "tx": -2.0},
         "arm_b": {"rz": -28.0}, "foot_f": {"tx": -2.0}}, {"ease": "vector"}),
    (12, {"torso": {"rz": 10.0, "ty": -1.0}, "head": {"rz": 5.0}, "arm_f": {"rz": 112.0, "tx": -2.0},
          "arm_b": {"rz": -22.0}, "foot_f": {"tx": -2.0}}),
    (44, {}),
]}
# the gem charges, glowing and swelling (A_3A, loop) and flashes (A_3B)
MOTIONS["A_3A"] = {"end": 20, "loop": True, "keys": [
    (0, {"gem": {"tex": 1, "sx": 1.0, "sy": 1.0}, "head": {"tx": 0.0}}),
    (5, {"gem": {"tex": 1, "sx": 1.125, "sy": 1.125}, "head": {"tx": 0.3}}),
    (10, {"gem": {"tex": 1, "sx": 1.0625, "sy": 1.0625}, "head": {"tx": -0.3}}),
    (15, {"gem": {"tex": 1, "sx": 1.1875, "sy": 1.1875}, "head": {"tx": 0.3}}),
    (20, {"gem": {"tex": 1, "sx": 1.0, "sy": 1.0}, "head": {"tx": 0.0}}),
]}
MOTIONS["A_3B"] = {"end": 30, "keys": [
    (0, {"gem": {"tex": 1, "sx": 1.1875, "sy": 1.1875}, "torso": {"rz": 0.0}, "head": {"rz": 0.0}}),
    (3, {"gem": {"tex": 1, "sx": 1.75, "sy": 1.75}, "torso": {"rz": -4.0}, "head": {"rz": -6.0}}, {"ease": "vector"}),
    (10, {"gem": {"tex": 1, "sx": 1.25, "sy": 1.25}, "torso": {"rz": -3.0}, "head": {"rz": -4.0}}),
    (30, {"gem": {"tex": 0, "sx": 1.0, "sy": 1.0}, "torso": {"rz": 0.0}, "head": {"rz": 0.0}}),
]}
# hit: a jolt and a shudder (10)
MOTIONS["D_1"] = {"end": 10, "loop": True, "keys": [
    (0, {"locator2": {"tx": 1.0}, "torso": {"rz": -3.0}, "head": {"rz": -4.0}, "arm_f": {"rz": 12.0}}),
    (5, {"locator2": {"tx": -0.6}, "torso": {"rz": 2.0}, "head": {"rz": 3.0}, "arm_f": {"rz": -6.0}}),
    (10, {"locator2": {"tx": 1.0}, "torso": {"rz": -3.0}, "head": {"rz": -4.0}, "arm_f": {"rz": 12.0}}),
]}
# the gem struck: it reels back, the gem flashes and cracks (50, held cracked)
MOTIONS["D_2"] = {"end": 50, "keys": [
    (0, {"gem": {"tex": 1}}),
    (4, {"gem": {"tex": 1, "sx": 1.5, "sy": 1.5}, "locator2": {"rz": -8.0, "tx": 3.0},
         "head": {"rz": -10.0, "ty": 1.0}, "arm_f": {"rz": -50.0}, "arm_b": {"rz": -30.0},
         "foot_f": {"ty": 3.0, "rz": 10.0}, "eyes": {"tex": 1}}, {"ease": "vector"}),
    (14, {"gem": {"tex": 2}, "locator2": {"rz": -5.0, "tx": 4.0}, "head": {"rz": -6.0, "ty": 0.6},
          "arm_f": {"rz": -30.0}, "arm_b": {"rz": -20.0}, "eyes": {"tex": 0}}),
    (24, {"gem": {"tex": 2}, "locator2": {"rz": 2.0, "tx": 4.0}, "head": {"rz": 3.0},
          "arm_f": {"rz": 6.0}, "arm_b": {"rz": 4.0}, "eyes": {"tex": 0}}, {"ease": "vector"}),
    (50, {"gem": {"tex": 2}, "locator2": {"tx": 4.0}, "eyes": {"tex": 0}}),
]}
# cracked, after: a wobble, the eyes failing now and then (60)
MOTIONS["K_1"] = {"end": 60, "loop": True, "keys": [
    (0, {"gem": {"tex": 2}, "head": {"rz": -2.0}, "torso": {"rz": -1.0}, "eyes": {"tex": 0}}),
    (20, {"gem": {"tex": 2}, "head": {"rz": 2.0}, "torso": {"rz": 1.0}, "eyes": {"tex": 0}}),
    (34, {"gem": {"tex": 2}, "head": {"rz": 1.0}, "torso": {"rz": 0.5}, "eyes": {"tex": 1}}),
    (38, {"gem": {"tex": 2}, "head": {"rz": 0.5}, "torso": {"rz": 0.0}, "eyes": {"tex": 0}}),
    (60, {"gem": {"tex": 2}, "head": {"rz": -2.0}, "torso": {"rz": -1.0}, "eyes": {"tex": 0}}),
]}


def _crumble():
    """defeated: the eyes go out, it shudders and sags, then shatters (30): the
    body bursts into chunks, the head and arms drop and tumble, everything lands
    with a bounce; the gem pops loose and rolls away (100, held)"""
    S = SCALE
    head_drop = -14.6 * S + 6.0   # the head's bottom from 26 units up to the ground (the torso is 6 down)
    gem_drop = -(GEM_C[1] - 4.4) * S  # the loose gem's bottom to the ground
    keys = []

    def k(f, ease=None, **groups):
        pose = {"gem": {"tex": 2}, "gem_loose": {"vis": 1 if f >= 30 else 0}}
        pose["eyes"] = {"tex": 1 if f >= 8 else 0}
        pose["body"] = {"vis": 0 if f >= 30 else 1}
        for i in range(1, 7):
            pose["rubble_%d" % i] = {"vis": 1 if f >= 30 else 0}
        if f >= 30:
            pose["gem"]["vis"] = 0
        for g, ch in groups.items():
            pose.setdefault(g, {}).update(ch)
        keys.append((f, pose, {"ease": ease} if ease else {}))
    k(0)
    k(8, locator2={"tx": 0.6})
    k(12, locator2={"tx": -0.6})
    k(16, locator2={"tx": 0.5})
    k(20, locator2={"tx": 0.0}, torso={"ty": -1.2, "rz": 2.0}, head={"rz": 3.0})
    k(29, torso={"ty": -2.0, "rz": 3.0}, head={"rz": 5.0, "ty": -0.6}, arm_f={"rz": 6.0}, arm_b={"rz": -6.0})
    # it shatters: chunks fly out, everything starts to fall
    k(30, "vector", torso={"ty": -2.0, "rz": 3.0}, head={"rz": 5.0, "ty": -0.6}, arm_f={"rz": 6.0},
      arm_b={"rz": -6.0}, gem_loose={"ty": -1.6})
    k(34, "vector", torso={"ty": -4.0, "rz": 2.0}, head={"rz": 10.0, "ty": head_drop * 0.25},
      arm_f={"rz": 30.0, "tx": -4.0, "ty": -6.0}, arm_b={"rz": -30.0, "tx": 4.0, "ty": -6.0},
      gem_loose={"tx": -3.0, "ty": 6.0, "rz": 40.0},
      foot_f={"rz": -10.0}, foot_b={"rz": 8.0})
    k(40, "linear", torso={"ty": -6.0, "rz": 2.0}, head={"rz": -10.0, "tx": 2.0, "ty": head_drop * 0.8},
      arm_f={"rz": 70.0, "tx": -10.0, "ty": -16.0}, arm_b={"rz": -75.0, "tx": 10.0, "ty": -16.0},
      gem_loose={"tx": -9.0, "ty": 2.0, "rz": 140.0},
      foot_f={"rz": -14.0, "tx": -1.0}, foot_b={"rz": 12.0, "tx": 1.0})
    # the head hits the ground (46), arms and chunks land
    k(46, "vector", torso={"ty": -6.0, "rz": 2.0}, head={"rz": -18.0, "tx": 3.0, "ty": head_drop},
      arm_f={"rz": 88.0, "tx": -13.0, "ty": -19.0}, arm_b={"rz": -90.0, "tx": 13.0, "ty": -19.0},
      gem_loose={"tx": -16.0, "ty": gem_drop, "rz": 230.0},
      foot_f={"rz": -16.0, "tx": -1.5}, foot_b={"rz": 14.0, "tx": 1.5})
    # a bounce
    k(52, "vector", torso={"ty": -6.0, "rz": 2.0}, head={"rz": -14.0, "tx": 3.6, "ty": head_drop + 3.0},
      arm_f={"rz": 84.0, "tx": -14.0, "ty": -17.0}, arm_b={"rz": -86.0, "tx": 14.0, "ty": -17.0},
      gem_loose={"tx": -22.0, "ty": gem_drop + 4.0, "rz": 300.0},
      foot_f={"rz": -16.0, "tx": -1.5}, foot_b={"rz": 14.0, "tx": 1.5})
    k(58, "vector", torso={"ty": -6.0, "rz": 2.0}, head={"rz": -18.0, "tx": 4.0, "ty": head_drop},
      arm_f={"rz": 88.0, "tx": -14.5, "ty": -19.0}, arm_b={"rz": -90.0, "tx": 14.5, "ty": -19.0},
      gem_loose={"tx": -27.0, "ty": gem_drop, "rz": 370.0},
      foot_f={"rz": -16.0, "tx": -1.5}, foot_b={"rz": 14.0, "tx": 1.5})
    # the gem rolls on, slowing, and rocks to a stop
    rest = dict(torso={"ty": -6.0, "rz": 2.0}, head={"rz": -18.0, "tx": 4.0, "ty": head_drop},
                arm_f={"rz": 88.0, "tx": -14.5, "ty": -19.0}, arm_b={"rz": -90.0, "tx": 14.5, "ty": -19.0},
                foot_f={"rz": -16.0, "tx": -1.5}, foot_b={"rz": 14.0, "tx": 1.5})
    k(76, None, gem_loose={"tx": -40.0, "ty": gem_drop, "rz": 610.0}, **rest)
    k(86, None, gem_loose={"tx": -43.0, "ty": gem_drop, "rz": 660.0}, **rest)
    k(92, None, gem_loose={"tx": -42.5, "ty": gem_drop, "rz": 645.0}, **rest)
    # settled, the gem glints: the prize
    k(94, None, gem_loose={"tx": -42.8, "ty": gem_drop, "rz": 650.0, "tex": 1, "sx": 1.25, "sy": 1.25}, **rest)
    k(97, None, gem_loose={"tx": -42.8, "ty": gem_drop, "rz": 650.0, "tex": 1, "sx": 1.125, "sy": 1.125}, **rest)
    k(100, None, gem_loose={"tx": -42.8, "ty": gem_drop, "rz": 650.0, "tex": 0}, **rest)
    # the chunks: thrown out from where they sat in the body, each on its own
    # arc, landing as the head does (46), a bounce (52) and settled (58)
    for i, (dx, peak, spin) in enumerate(_RUBBLE_FLIGHT):
        (mx, my), _pts = _RUBBLE[i]
        ground = -(my - 4.4) * S
        for f, pose, _o in keys:
            if f <= 30:
                ch = {"tx": 0.0, "ty": 0.0, "rz": 0.0}
            elif f <= 46:
                t = (f - 30) / 16.0
                ch = {"tx": dx * t, "ty": 4 * peak * t * (1 - t) + ground * t, "rz": spin * t}
            elif f <= 52:
                ch = {"tx": dx * 1.12, "ty": ground + peak * 0.12, "rz": spin * 1.15}
            else:
                ch = {"tx": dx * 1.2, "ty": ground, "rz": spin * 1.2}
            pose.setdefault("rubble_%d" % (i + 1), {}).update(ch)
    return {"end": 100, "keys": keys}


MOTIONS["D_3"] = _crumble()
# dizzy: the head grinds round, the eyes flicker (60)
MOTIONS["X_1"] = {"end": 60, "loop": True, "keys": [
    (0, {"head": {"rz": 5.0, "tx": 0.0}, "torso": {"rz": 2.0}, "eyes": {"tex": 0}}),
    (15, {"head": {"rz": 0.0, "tx": -1.0}, "torso": {"rz": 0.0}, "eyes": {"tex": 1}}),
    (18, {"head": {"rz": -1.0, "tx": -1.0}, "torso": {"rz": -0.5}, "eyes": {"tex": 0}}),
    (30, {"head": {"rz": -5.0, "tx": 0.0}, "torso": {"rz": -2.0}, "eyes": {"tex": 0}}),
    (45, {"head": {"rz": 0.0, "tx": 1.0}, "torso": {"rz": 0.0}, "eyes": {"tex": 0}}),
    (60, {"head": {"rz": 5.0, "tx": 0.0}, "torso": {"rz": 2.0}, "eyes": {"tex": 0}}),
]}
# asleep: the eyes dark, settling and rising, very slow (120)
MOTIONS["N_1"] = {"end": 120, "loop": True, "keys": [
    (0, {"eyes": {"tex": 1}, "torso": {"ty": -0.8}, "head": {"rz": 3.0, "ty": -0.6}}),
    (60, {"eyes": {"tex": 1}, "torso": {"ty": 0.0}, "head": {"rz": 1.0, "ty": 0.0}}),
    (120, {"eyes": {"tex": 1}, "torso": {"ty": -0.8}, "head": {"rz": 3.0, "ty": -0.6}}),
]}
# frozen, stopped
MOTIONS["D_5"] = {"end": 0, "keys": [(0, {"torso": {"rz": -2.0}, "head": {"rz": -3.0}, "arm_f": {"rz": -10.0}})]}


if __name__ == "__main__":
    import sys
    if sys.argv[1:2] == ["spec"]:
        for row in spec():
            print(row)
        sys.exit(0)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    m, t = build()
    open(os.path.join(out, NAME), "wb").write(m)
    open(os.path.join(out, NAME + "-"), "wb").write(t)
    print(NAME, len(m), len(t))
