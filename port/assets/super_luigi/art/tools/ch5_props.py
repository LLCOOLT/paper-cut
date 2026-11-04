"""Chapter 5's smaller props (Super Luigi): Mossbeard's button, the Luff gear-gate and the Luff
signpost.

Run: python ch5_props.py -> ../ch5_props/*.svg, *.png, parts.json, preview.png
ch5.md, New assets 8-9: "The Luff gear-gate (prop: a gate of stone teeth with a big wall gear) and
the Luff signpost (a stone post with a crank), optional"; "Mossbeard's buttons (an icon: a big
button)". The gear is "jammed with a thousand years of grit" until Torque's Tune-up; the gate "rises
a notch per push"; the signpost is broken until he fixes it, then "turns and points at a stump".
Props: 8 texels/unit, outlined, lit from the top left, in the Luffs' white stone (the white ruin of
the murals, ../murals/). The button is a 40x40 item icon, the size of icon.tpl's item icons.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from papercut import Part, Rig, stroke_svg, fill_svg, smooth_path, ellipse_pts, clean  # noqa

OUT = os.path.join(os.path.dirname(__file__), "..", "ch5_props")
R = Rig("sl_ch5_props", OUT)

STONE = dict(fill="#e2dcce", light="#f4f0e4", shadow="#b8ae9a", line="#3a3226")
STONE_D = dict(fill="#c8c0ae", light="#dcd6c6", shadow="#a0967e", line="#2e281c")
INLAY = "#4aa8a8"            # the Luffs' teal glyph inlay, as on the murals
GRIT = dict(fill="#8a6a44", light="#a8865a", shadow="#664a2a", line="#2a1a08")
DARK = "#2a2420"
BUTTON = dict(fill="#d8483a", light="#f07a5e", shadow="#a42a24", line="#3e0a08")
WOOD = dict(fill="#b88a52", light="#d4a872", shadow="#8a6230", line="#3a2208")


# ------------------------------------------------------------- the gate --
def gate_frame():
    c = R.card("gate_frame", (-9.0, 0.0, 9.0, 20.0))
    # one simple outline: up the outside of the arch and back down its inside (the opening at the bottom)
    pts = [(-8.6, 0.2), (-8.6, 13.0), (-7.6, 16.6), (-4.8, 19.0), (0.0, 19.8), (4.8, 19.0), (7.6, 16.6),
           (8.6, 13.0), (8.6, 0.2), (5.4, 0.2), (5.4, 12.0), (4.6, 14.6), (2.4, 16.2), (0.0, 16.6), (-2.4, 16.2),
           (-4.6, 14.6), (-5.4, 12.0), (-5.4, 0.2)]
    det = []
    # the arch's voussoirs and the jambs' courses, as dark joints
    for k in range(1, 8):
        a = math.pi * k / 8
        det.append(stroke_svg(c.line([(-5.6 * math.cos(a) * 1.0, 12.0 + 4.6 * math.sin(a)),
                                      (-8.6 * math.cos(a), 13.0 + 6.8 * math.sin(a))]), STONE["shadow"], 2))
    for y in (3.2, 6.4, 9.6):
        det += [stroke_svg(c.line([(-8.6, y), (-5.4, y)]), STONE["shadow"], 2),
                stroke_svg(c.line([(5.4, y), (8.6, y)]), STONE["shadow"], 2)]
    # the keystone's glyph: the Luffs' compass mark
    kx, ky = c.u(0.0, 18.2)
    det.append(f'<circle cx="{kx:.1f}" cy="{ky:.1f}" r="5" fill="none" stroke="{INLAY}" stroke-width="2"/>')
    det.append(stroke_svg(c.line([(0.0, 17.2), (0.0, 19.2)]), INLAY, 2))
    c.shape(pts, STONE, det, hl=(0.4, 0.14, 0.24, 0.08), seed=1, tension=0.25, jitter=0.3)
    return c.save((0.0, 0.0), "the gate's stone arch, an opening 10.8 wide and 16 tall at the crown; in front of "
                              "the teeth; pivot on the ground at its middle")


def gate_teeth():
    c = R.card("gate_teeth", (-6.0, 0.0, 6.0, 17.0))
    pts = [(-5.6, 16.6), (5.6, 16.6), (5.6, 4.0)]
    n = 6
    for k in range(n):   # a row of stone fangs along the bottom edge
        x0 = 5.6 - 11.2 * k / n
        x1 = 5.6 - 11.2 * (k + 1) / n
        pts += [((x0 + x1) / 2 + 0.2, 0.3), (x1, 4.0)]
    det = [stroke_svg(c.line([(-5.4, y), (5.4, y)]), STONE_D["shadow"], 2) for y in (8.0, 12.2)]
    det += [stroke_svg(c.line([(x, 4.2), (x, 16.4)]), STONE_D["shadow"], 1.6) for x in (-1.9, 1.9)]
    c.shape(pts, STONE_D, det, hl=(0.4, 0.22, 0.24, 0.12), seed=3, tension=0.1, jitter=0.3)
    return c.save((0.0, 0.0), "the gate of stone teeth, behind the arch: down (shut) at rest; it rises a notch "
                              "(about 3 units) per push of the gear and falls back if the pushing stops; pivot at "
                              "its bottom middle")


def gear(jammed):
    name = "gear_jammed" if jammed else "gear"
    c = R.card(name, (-4.5, -4.5, 4.5, 4.5))
    pts = []
    teeth = 10
    for k in range(teeth * 4):
        a = 2 * math.pi * k / (teeth * 4)
        r = 4.2 if (k % 4) in (1, 2) else 3.4
        pts.append((r * math.cos(a), r * math.sin(a)))
    det = [fill_svg(c.ell(0, 0, 1.4, 1.4, 12), STONE["shadow"]), fill_svg(c.ell(0, 0, 0.7, 0.7, 10), DARK)]
    for k in range(4):   # four spoke-holes
        a = math.pi / 4 + k * math.pi / 2
        det.append(fill_svg(c.ell(2.2 * math.cos(a), 2.2 * math.sin(a), 0.55, 0.55, 10), DARK))
    det.append(f'<circle cx="{c.u(0, 0)[0]:.1f}" cy="{c.u(0, 0)[1]:.1f}" r="{2.2 * 8:.1f}" fill="none" '
               f'stroke="{INLAY}" stroke-width="1.6" stroke-dasharray="5 4"/>')
    if jammed:   # a thousand years of grit packed between the teeth and over the hub
        for k in range(teeth):
            a = 2 * math.pi * (k + 0.5) / teeth
            det.append(fill_svg(c.ell(3.5 * math.cos(a), 3.5 * math.sin(a), 0.9, 0.7, 8, rot=math.degrees(-a)),
                                GRIT["fill"]))
        det += [fill_svg(c.ell(-0.6, 0.8, 1.8, 1.2, 10), GRIT["fill"]), fill_svg(c.ell(1.6, -1.4, 1.2, 0.8, 10),
                                                                                  GRIT["shadow"]),
                fill_svg(c.ell(-2.2, -1.8, 0.9, 0.6, 8), GRIT["light"])]
    c.shape(pts, STONE, det, hl=(0.38, 0.28, 0.24, 0.16), seed=4, tension=0.15, jitter=0.2)
    return c.save((0.0, 0.0), ("the wall gear jammed with grit, before the Tune-up" if jammed else
                               "the wall gear, clean: turn it about its hub") + "; pivot at the hub")


# ------------------------------------------------------------ signpost --
def signpost():
    c = R.card("signpost", (-2.0, 0.0, 2.0, 13.0))
    post = [(-1.4, 0.2), (1.4, 0.2), (1.2, 11.0), (0.8, 12.4), (-0.8, 12.4), (-1.2, 11.0)]
    det = [stroke_svg(c.line([(-1.2, y), (1.2, y)]), STONE["shadow"], 2) for y in (4.0, 8.0)]
    det.append(fill_svg(c.ell(0.0, 10.4, 0.8, 0.8, 10), INLAY))   # the axle's socket, a Luff mark round it
    det.append(fill_svg(c.ell(0.0, 10.4, 0.36, 0.36, 8), DARK))
    c.shape(post, STONE, det, hl=(0.4, 0.2, 0.26, 0.12), seed=5, tension=0.2)
    return c.save((0.0, 0.0), "the Luff signpost's stone post; the pointer turns on its axle at (0, 10.4); "
                              "pivot on the ground")


def pointer():
    c = R.card("signpost_pointer", (-1.0, -1.5, 9.0, 1.5))
    pts = [(-0.6, 0.9), (6.6, 0.9), (6.8, 1.3), (8.6, 0.0), (6.8, -1.3), (6.6, -0.9), (-0.6, -0.9)]
    det = [fill_svg(c.ell(0.0, 0.0, 0.5, 0.5, 8), DARK)]
    det += [stroke_svg(c.line([(x, -0.5), (x + 0.6, 0.5)]), INLAY, 2) for x in (2.0, 3.4, 4.8)]   # glyphs
    c.shape(pts, STONE, det, hl=(0.4, 0.3, 0.3, 0.2), seed=6, tension=0.05, lo=(3, 3), so=(3, 3), soft=1.5)
    return c.save((0.0, 0.0), "the pointer, a stone arrow with Luff glyphs: hang it drooping (rotate -100°) "
                              "while broken; once fixed it swings round to point at the stump; pivot at its axle")


def crank():
    c = R.card("signpost_crank", (-1.0, -1.0, 3.0, 3.0))
    c.shape([(-0.4, 0.4), (2.2, 0.4), (2.2, -0.4), (-0.4, -0.4)], WOOD, seed=7, tension=0.1, lo=(1, 1), so=(1, 1),
            soft=0.6)
    c.shape(None, WOOD, d=c.ell(2.2, 1.2, 0.6, 1.4, 10), hl=(0.4, 0.3, 0.3, 0.2), seed=8, lo=(1, 1), so=(1, 1),
            soft=0.6)
    c.shape(None, STONE_D, d=c.ell(0.0, 0.0, 0.7, 0.7, 10), seed=9, lo=(1, 1), so=(1, 1), soft=0.6)
    return c.save((0.0, 0.0), "the crank, a wooden handle on the post's side (in front of it at the axle); "
                              "turn it about its hub as Torque fixes the post; pivot at the hub")


# ----------------------------------------------------------------- icon --
def icon_button():
    p = Part("icon_button", 40, 40)
    cx, cy, r = 19.5, 19.5, 17.0
    line = 2.8

    def sh(d, tone, det=(), hl=None):
        p.shape(d, tone["fill"], tone["line"], tone.get("light"), tone.get("shadow"), (2.5, 2.5), (3, 3),
                details=det, soft=1.0, hl=hl, grow=line * 0.5, inner=line * 0.55, bias=(line * 0.3, line * 0.36))
    ell = lambda x, y, rx, ry, n=14: smooth_path(ellipse_pts(x, y, rx, ry, n), tension=0.5)
    det = [stroke_svg(ell(cx, cy, r * 0.7, r * 0.7, 16), BUTTON["shadow"], 2.6),   # the raised rim's inner edge
           stroke_svg(ell(cx - 0.6, cy - 0.6, r * 0.66, r * 0.66, 16), BUTTON["light"], 1.2)]
    for dx, dy in ((-4, -4), (4, -4), (-4, 4), (4, 4)):   # four thread holes, with a scrap of thread through two
        det.append(fill_svg(ell(cx + dx, cy + dy, 2.2, 2.2, 10), BUTTON["line"]))
    det.append(stroke_svg(f"M{cx - 4:.1f} {cy - 4:.1f} L{cx + 4:.1f} {cy + 4:.1f}", "#f4ecd8", 1.8))
    sh(ell(cx, cy, r, r, 18), BUTTON, det, hl=(0.36, 0.26, 0.24, 0.16))
    p.save(OUT)
    return p


# --------------------------------------------------------------- preview --
def preview():
    W, H = 760, 260
    c = Image.new("RGBA", (W, H), (236, 232, 222, 255))
    d = ImageDraw.Draw(c)
    gy = 200

    def put(n, x, y_ground, model_xy=(0.0, 0.0), rot=0):
        m = R.meta[n]
        im = Image.open(os.path.join(OUT, n + ".png")).convert("RGBA")
        px, py = m["pivot"]
        if rot:
            big = Image.new("RGBA", (im.width * 3, im.height * 3), (0, 0, 0, 0))
            big.alpha_composite(im, (im.width, im.height))
            im = big.rotate(rot, center=(im.width + px, im.height + py))
            px, py = px + im.width / 3, py + im.height / 3
        c.alpha_composite(im, (round(x + model_xy[0] * 8 - px), round(y_ground - model_xy[1] * 8 - py)))
    for i, raised in enumerate((0.0, 9.0)):
        x = 90 + i * 190
        put("gate_teeth", x, gy, (0.0, raised))
        put("gate_frame", x, gy)
        put("gear_jammed" if i == 0 else "gear", x, gy, (11.0, 8.0))
        d.text((x - 70, 214), "jammed, shut" if i == 0 else "clean, gate raised 3 notches", fill=(40, 30, 30, 255))
    for i, rot in enumerate((-100, 0)):
        x = 480 + i * 110
        put("signpost", x, gy)
        put("signpost_pointer", x, gy, (0.0, 10.4), rot=rot)
        put("signpost_crank", x, gy, (0.4, 10.4))
        d.text((x - 20, 214), "broken" if i == 0 else "fixed", fill=(40, 30, 30, 255))
    im = Image.open(os.path.join(OUT, "icon_button.png")).convert("RGBA")
    c.alpha_composite(im.resize((80, 80), Image.NEAREST), (670, 60))
    c.alpha_composite(im, (690, 150))
    d.text((660, 40), "icon_button x2, x1", fill=(40, 30, 30, 255))
    d.rectangle([0, gy, W, gy + 1], fill=(180, 170, 150, 255))
    d.text((10, 240), "Chapter 5's props, 1:1 at 8 texels/unit", fill=(40, 30, 30, 255))
    return c


def build():
    clean(OUT)
    gate_frame()
    gate_teeth()
    gear(False)
    gear(True)
    signpost()
    pointer()
    crank()
    R.write_json({"draw_order_gate": ["gate_teeth", "gate_frame", "gear / gear_jammed (on the wall beside it)"],
                  "draw_order_post": ["signpost", "signpost_pointer", "signpost_crank"]})
    icon_button()
    preview().convert("RGB").save(os.path.join(OUT, "preview.png"))


if __name__ == "__main__":
    build()
    print(", ".join(R.meta) + ", icon_button")
