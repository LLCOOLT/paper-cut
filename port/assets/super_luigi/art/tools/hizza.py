"""Hizza, the two-headed snake (Super Luigi ch2 boss): every card as SVG + PNG.

Run: python hizza.py  -> writes ../hizza/*.svg, *.png and parts.json.
Card names and sizes match the c_sl_hizza rig (8 texels per game unit,
the model faces -x, so every head looks left).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from papercut import HL_DEFAULT, Part, smooth_path, ellipse_pts, star_pts, stroke_svg, fill_svg, poly_path  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), "..", "hizza")

# ---- palette (STYLE.md: fill v 0.55-0.95, s 0.35-0.8; line v 0.15-0.32, same hue family) ----
# olive-green back, cream belly, darker diamonds: docs/super_luigi/ch2.md (New assets, 1)
BODY = dict(fill="#9aa940", light="#bccb62", shadow="#6c7a24", line="#252e0b")
BELLY = dict(fill="#ead690", light="#faf0c2", shadow="#c3a65a", line="#4f3614")
MARK = "#56661c"          # back diamonds (flat, no line)
CREST = dict(fill="#4a5a18", light="#62742a", shadow="#36420f", line="#182006")
EYE = dict(fill="#f6eeb4", shadow="#d9c676", line="#3c4614")   # eye line: thinner and lighter (R4)
COLLAR = dict(fill="#8a5a2c", light="#a87444", shadow="#62401c", line="#2e1c0a")
BRASS = dict(fill="#e2b23a", light="#f6dc7a", shadow="#a87a1c", line="#4a3006")
PIECE = dict(fill="#5ab8e8", light="#b8ecff", shadow="#2a78b0", line="#0c2a48")
PUPIL = "#140a16"
MOUTH = dict(fill="#7a1628", light="#9a2a3c", shadow="#4c0a18", line="#2e0610")
TONGUE = dict(fill="#e2405e", light="#f47c92", shadow="#a8203e", line="#4a0818")
FANG = dict(fill="#f2f0ea", shadow="#b9b6b0", line="#33303a")
SCAR = "#e7a0b8"
STAR = dict(fill="#f2c62c", light="#fbe680", shadow="#e0a010", line="#5a3208")
SMOKE = dict(fill="#e6e4ee", light="#ffffff", shadow="#b0adbd", line="#3a3844")

SOFT = 3.5          # blur (px) of the light/shadow bands at 8 texels/unit
L_OFF, S_OFF = (6, 6), (9, 9)

parts_meta = {}


def note(name, w, h, pivot, desc):
    parts_meta[name] = {"size": [w, h], "pivot": list(pivot), "desc": desc}


def body_shape(p, d, belly_zone=None, details=(), hl=HL_DEFAULT):
    zones = []
    if belly_zone:
        zones.append((belly_zone, BELLY["fill"], BELLY["line"], BELLY["light"], BELLY["shadow"]))
    p.shape(d, BODY["fill"], BODY["line"], BODY["light"], BODY["shadow"], L_OFF, S_OFF,
            zones=zones, details=details, soft=SOFT, hl=hl)


def diamond(cx, cy, w, h, colour=MARK):
    return fill_svg(smooth_path([(cx, cy - h / 2), (cx + w / 2, cy), (cx, cy + h / 2), (cx - w / 2, cy)],
                                tension=0.35, jitter=0.4, seed=int(cx * 7 + cy)), colour)


# ------------------------------------------------------------------ heads --

HEAD_W, HEAD_H = 168, 128
HEAD_PTS = [(8, 78), (12, 62), (26, 50), (44, 44), (66, 44), (92, 40), (118, 36), (142, 42), (158, 60),
            (160, 82), (152, 102), (136, 114), (112, 112), (84, 106), (56, 100), (30, 96), (14, 90)]
HEAD_PIVOT = (132, 110)       # where the head sits on neck_a3 / tail_b3
EYES = [(62, 40, 15, 18), (98, 34, 18, 21)]   # far eye, near eye: cx, cy, rx, ry
BELLY_HEAD = poly_path([(0, 86), (60, 94), (130, 104), (168, 100), (168, 128), (0, 128)])
CREST_PTS = [(100, 24), (112, 4), (122, 22), (132, 10), (140, 30), (152, 24), (152, 50), (100, 40)]


def fangs(p):
    for x, y, s in ((34, 96, 1.0), (56, 100, 0.8)):
        pts = [(x - 7 * s, y - 8), (x + 7 * s, y - 8), (x + 5 * s, y + 6 * s), (x + 1, y + 19 * s),
               (x - 3 * s, y + 9 * s)]
        p.shape(smooth_path(pts, tension=0.5, seed=x), FANG["fill"], FANG["line"], None, FANG["shadow"],
                shadow_off=(3, 2), grow=1.0, inner=1.5, bias=(1, 1))


def eye_open(p, cx, cy, rx, ry, seed, lid=0.0):
    d = smooth_path(ellipse_pts(cx, cy, rx, ry, 14, wobble=0.03, seed=seed), seed=seed, jitter=0.3)
    det = [fill_svg(smooth_path(ellipse_pts(cx - rx * 0.12, cy + ry * 0.08, rx * 0.26, ry * 0.62, 10, seed=seed + 1)),
                    PUPIL),
           fill_svg(smooth_path(ellipse_pts(cx - rx * 0.2, cy - ry * 0.28, rx * 0.12, ry * 0.12, 8)), "#ffffff")]
    if lid > 0:   # upper lid (body colour) covering the top of the eye, with its edge line
        y = cy - ry + 2 * ry * lid
        lid_d = poly_path([(cx - rx - 4, cy - ry - 4), (cx + rx + 4, cy - ry - 4), (cx + rx + 4, y - 4),
                           (cx - rx - 4, y + 3)])
        det.append(fill_svg(lid_d, BODY["fill"]))
        det.append(stroke_svg(f"M{cx - rx - 4} {y + 3} L{cx + rx + 4} {y - 4}", BODY["line"], 4))
    p.shape(d, EYE["fill"], EYE["line"], None, EYE["shadow"], shadow_off=(4, 4), details=det,
            grow=1.0, inner=1.5, bias=(1, 1), soft=2)


def eye_lid(p, cx, cy, rx, ry, seed, mark):
    """Eye covered by its lid (body colour): mark = 'shut' (curved lash line) or '>' / '<' (squeezed)."""
    d = smooth_path(ellipse_pts(cx, cy, rx, ry, 14, wobble=0.03, seed=seed), seed=seed, jitter=0.3)
    if mark == "shut":
        m = stroke_svg(f"M{cx - rx * 0.7} {cy + 1} Q{cx} {cy + ry * 0.55} {cx + rx * 0.7} {cy + 1}", BODY["line"], 4)
    else:
        k = 1 if mark == ">" else -1
        m = stroke_svg(f"M{cx - k * rx * 0.55} {cy - ry * 0.45} L{cx + k * rx * 0.4} {cy + 1} "
                       f"L{cx - k * rx * 0.55} {cy + ry * 0.45}", BODY["line"], 4.5)
    p.shape(d, BODY["fill"], BODY["line"], BODY["light"], BODY["shadow"], (4, 4), (6, 6), details=[m],
            grow=1.2, inner=1.8, bias=(1.2, 1.2), soft=2.5)


def head(name, variant, eyes):
    p = Part(name, HEAD_W, HEAD_H, grow=1.5, inner=2.0, bias=(1.5, 1.5))
    fangs(p)
    if variant == "b":   # the tail's head: a dark crest of three fins behind the eyes
        p.shape(smooth_path(CREST_PTS, tension=0.45, jitter=0.4, seed=5), CREST["fill"], CREST["line"],
                CREST["light"], CREST["shadow"], (4, 4), (5, 5), soft=2.5)
    det = [
        diamond(132, 66, 22, 18), diamond(148, 84, 14, 18), diamond(112, 60, 12, 10),
        # nostril
        fill_svg(smooth_path(ellipse_pts(24, 64, 4.5, 2.6, 8, rot=-30)), BODY["line"]),
        # Hiss: a big greedy grin swept up the cheek, with a dimple
        stroke_svg("M94 106 Q122 110 136 84", BODY["line"], 4),
        stroke_svg("M131 80 Q138 79 140 85", BODY["line"], 3),
    ]
    if variant == "b":
        det = [diamond(132, 66, 22, 18, CREST["fill"]), diamond(148, 84, 14, 18, CREST["fill"]),
               fill_svg(smooth_path(ellipse_pts(24, 64, 4.5, 2.6, 8, rot=-30)), BODY["line"]),
               # Zah: a sly half-smile, one corner up
               stroke_svg("M108 109 Q122 109 128 98", BODY["line"], 3.5),
               # the scar: an old pale cut across the cheek, with stitches
               stroke_svg("M64 58 Q78 70 86 88", SCAR, 5),
               stroke_svg("M66 66 L74 60 M72 74 L80 68 M78 82 L86 76", BODY["line"], 2.5)]
    body_shape(p, smooth_path(HEAD_PTS, jitter=0.5, seed=11 if variant == "a" else 12), BELLY_HEAD, det,
               hl=(0.28, 0.28, 0.21, 0.13, -12))
    for i, (cx, cy, rx, ry) in enumerate(EYES):
        seed = 20 + i
        if eyes == "open":
            eye_open(p, cx, cy, rx, ry, seed, lid=0.30 if variant == "b" else 0.0)
        elif eyes == "blink":
            eye_lid(p, cx, cy, rx, ry, seed, "shut")
        else:  # hurt: squeezed shut, ><
            eye_lid(p, cx, cy, rx, ry, seed, ">" if i == 0 else "<")
    note(name, HEAD_W, HEAD_H, HEAD_PIVOT,
         {"open": "skull and upper jaw, eyes open", "blink": "eyes shut", "hurt": "eyes squeezed shut (><)"}[eyes]
         + ("; tail head: dark crest, scar, heavy lids" if variant == "b" else ""))
    return p


JAW_W, JAW_H = 144, 128
JAW_PIVOT = (130, 70)    # hinge


def jaw(name, variant):
    p = Part(name, JAW_W, JAW_H)
    # mouth interior: a fan rising from the hinge; hidden behind the head when the jaw is shut
    # stays inside head_x's outline when the jaw is shut, so it shows only in the gap (to ~20 deg)
    inner = [(138, 70), (130, 42), (100, 24), (60, 22), (30, 30), (16, 42), (12, 56), (20, 66), (60, 72), (110, 74)]
    p.shape(smooth_path(inner, jitter=0.4, seed=31), MOUTH["fill"], MOUTH["line"], MOUTH["light"],
            MOUTH["shadow"], (-8, 8), (10, -6), soft=5,
            details=[stroke_svg("M124 66 Q74 54 26 58", MOUTH["shadow"], 6)])
    # lower fangs
    for x in (32, 52):
        pts = [(x - 5, 74), (x + 5, 74), (x + 1, 60), (x - 1, 58)]
        p.shape(smooth_path(pts, tension=0.5, seed=x), FANG["fill"], FANG["line"], None, FANG["shadow"],
                shadow_off=(2, 2), grow=1.0, inner=1.5, bias=(1, 1))
    lower = [(6, 72), (16, 66), (50, 68), (92, 66), (126, 58), (138, 66), (136, 82), (120, 98),
             (86, 108), (46, 108), (20, 98), (8, 86)]
    zone = poly_path([(0, 86), (60, 90), (144, 76), (144, 128), (0, 128)])
    det = [stroke_svg("M24 96 Q60 104 104 94", BELLY["line"], 2.5)]
    if variant == "b":
        det.append(stroke_svg("M70 76 Q82 84 90 96", SCAR, 4))
    body_shape(p, smooth_path(lower, jitter=0.5, seed=33 if variant == "a" else 34), zone, det,
               hl=(0.42, 0.22, 0.26, 0.12, -6))
    note(name, JAW_W, JAW_H, JAW_PIVOT,
         "lower jaw at the hinge; its mouth fan hides behind the head when shut and fills the gap up to "
         "~20 deg open (wider: show mouth_x too)")
    return p


MOUTH_W, MOUTH_H = 144, 112
MOUTH_PIVOT = (134, 8)   # the same hinge point as the jaw's pivot


def mouth(name, variant):
    """The open mouth's inside, pinned to the head, shown behind the jaw for a wide bite."""
    p = Part(name, MOUTH_W, MOUTH_H)
    # a wedge from the hinge between the lip line (0 deg) and ~42 deg down: the jaw covers the rest
    pts = [(140, 6), (100, 4), (50, 6), (12, 10), (6, 30), (14, 52), (30, 72), (46, 86), (90, 48), (120, 20)]
    det = [stroke_svg("M126 14 Q84 30 40 70", MOUTH["shadow"], 7),
           fill_svg(smooth_path(ellipse_pts(56, 40, 26, 9, 12, rot=-22)), MOUTH["light"])]
    p.shape(smooth_path(pts, jitter=0.4, seed=51 if variant == "a" else 52), MOUTH["fill"], MOUTH["line"],
            MOUTH["light"], MOUTH["shadow"], (-6, 6), (8, -4), soft=5, details=det)
    note(name, MOUTH_W, MOUTH_H, MOUTH_PIVOT,
         "inside of the open mouth (bite), pinned to the head at the jaw hinge; behind jaw_x; show when the "
         "jaw opens past ~20 deg (covers up to ~45 deg)")
    return p


TONGUE_W, TONGUE_H = 104, 40
TONGUE_PIVOT = (98, 20)


def tongue(name, variant):
    p = Part(name, TONGUE_W, TONGUE_H, grow=1.2, inner=1.6, bias=(1.2, 1.2))
    pts = [(100, 14), (70, 14), (40, 16), (24, 12), (6, 4), (14, 14), (20, 20), (12, 30), (4, 38),
           (24, 28), (42, 26), (70, 26), (100, 26)]
    det = [stroke_svg("M96 20 L36 21", TONGUE["shadow"], 2.5)]
    p.shape(smooth_path(pts, tension=0.7, jitter=0.3, seed=41 if variant == "a" else 42), TONGUE["fill"],
            TONGUE["line"], TONGUE["light"], TONGUE["shadow"], (3, 3), (3, 3), details=det, soft=1.5,
            hl=(0.55, 0.30, 0.30, 0.16))
    note(name, TONGUE_W, TONGUE_H, TONGUE_PIVOT, "forked tongue, root at the pivot; lies inside the mouth")
    return p


# ---------------------------------------------------------------- body --

def segment(name, w, h, belly_w, marks, variant="a", desc=""):
    """An upright body segment (neck or tail): belly (front, -x) on the left."""
    p = Part(name, w, h)
    cx = w / 2
    # a rounded disc, a little narrower at the top (the body tapers toward the head)
    pts = ellipse_pts(cx, h / 2, w / 2 - 8, h / 2 - 7, 12, wobble=0.015, seed=w + h)
    pts = [(x * 0.94 + cx * 0.06, y) if y < h * 0.3 else (x, y) for x, y in pts]   # a touch narrower on top
    zone = smooth_path([(-20, -10), (belly_w - 8, -10), (belly_w + 2, h * 0.5), (belly_w - 8, h + 10),
                        (-20, h + 10)], tension=0.6)
    det = []
    # ventral scutes across the belly
    for k in range(1, 5):
        y = h * k / 5
        det.append(stroke_svg(f"M2 {y + 3} Q{belly_w * 0.5} {y - 3} {belly_w + 2} {y}", BELLY["line"], 2.5))
    for (mx, my, mw, mh) in marks:
        det.append(diamond(mx * w, my * h, mw, mh, CREST["fill"] if variant == "b" else MARK))
    if variant == "b":   # the tail's darker spine stripe
        det.insert(0, fill_svg(smooth_path([(w - 22, 0), (w + 6, 0), (w + 6, h), (w - 22, h), (w - 16, h / 2)],
                                           tension=0.5), CREST["shadow"]))
    body_shape(p, smooth_path(pts, jitter=0.5, seed=len(name) * 13 + w), zone, det)
    note(name, w, h, (int(cx), h - 10), desc)
    return p


def coil(name, w, h, front):
    p = Part(name, w, h)
    if front:
        # a fat tube curving up at both ends (the near loop of the coil)
        pts = [(10, 30), (40, 40), (100, 52), (152, 56), (204, 52), (262, 40), (292, 28), (300, 56), (290, 86),
               (250, 108), (196, 120), (152, 122), (100, 120), (50, 108), (14, 86), (4, 56)]
        zone = smooth_path([(-10, 76), (50, 92), (100, 100), (152, 102), (204, 100), (254, 92), (320, 70),
                            (320, 140), (-10, 140)], tension=0.6)
        det = [diamond(70, 70, 26, 20), diamond(152, 78, 32, 24), diamond(234, 70, 26, 20)]
        for x in range(30, 290, 30):
            y0 = 100 - abs(x - 152) * 0.18
            det.append(stroke_svg(f"M{x} {y0} Q{x - 3} {y0 + 10} {x + 2} {y0 + 26}", BELLY["line"], 2.5))
        desc = "front loop of the coil (drawn over the necks' bases)"
    else:
        # an arch: the far loop, seen above and behind the front one; belly on its inner edge
        pts = [(8, 138), (10, 92), (40, 44), (90, 18), (144, 10), (198, 18), (248, 44), (278, 92), (282, 138),
               (230, 138), (222, 104), (192, 76), (144, 66), (96, 76), (66, 104), (58, 138)]
        zone = smooth_path([(42, 150), (46, 98), (84, 58), (144, 46), (204, 58), (242, 98), (248, 150)],
                           tension=0.6)
        det = [diamond(48, 92, 20, 24), diamond(96, 40, 26, 20), diamond(144, 28, 28, 18),
               diamond(192, 40, 26, 20), diamond(240, 92, 20, 24)]
        for ang in range(200, 341, 20):
            import math
            a = math.radians(ang)
            x0, y0 = 144 + 84 * math.cos(a), 136 + 78 * math.sin(a)
            x1, y1 = 144 + 102 * math.cos(a), 136 + 96 * math.sin(a)
            det.append(stroke_svg(f"M{x0:.1f} {y0:.1f} L{x1:.1f} {y1:.1f}", BELLY["line"], 2.5))
        desc = "back loop of the coil (behind the necks)"
    body_shape(p, smooth_path(pts, jitter=0.6, seed=71 if front else 72), zone, det,
               hl=(0.44, 0.30, 0.26, 0.11, 4) if front else (0.42, 0.13, 0.22, 0.08, -8))
    note(name, w, h, (w // 2, h - 8), desc)
    return p


DIRT = dict(fill="#c9a46a", light="#e4c690", shadow="#94703f", line="#3a2610")
CLOD = dict(fill="#a7834f", light="#c7a370", shadow="#76562c", line="#2e1e0c")


def tail_base():
    """c_sl_hizza_tail: the tail rising out of churned ground at the stage's far left. The stub of the
    tail (tail_b1 stacks on top of it) behind the front lip of a ring of thrown-up dirt."""
    w, h = 144, 80
    p = Part("tail_base", w, h)
    # back of the dirt ring, behind the tail
    back = [(10, 58), (24, 44), (52, 38), (92, 38), (120, 44), (134, 58), (100, 56), (44, 56)]
    p.shape(smooth_path(back, tension=0.6, jitter=0.8, seed=81), DIRT["shadow"], DIRT["line"], DIRT["fill"], None,
            (3, 3), soft=2)
    # the tail's stub, the belly to the front (-x), like the tail segments
    stub = [(26, 64), (24, 30), (40, 10), (72, 4), (104, 10), (118, 30), (118, 64)]
    zone = smooth_path([(-10, -10), (50, -10), (54, 40), (50, 90), (-10, 90)], tension=0.6)
    det = [stroke_svg(f"M24 {y + 2} Q38 {y - 3} 52 {y}", BELLY["line"], 2.5) for y in (24, 40, 56)]
    det += [diamond(90, 26, 16, 14, CREST["fill"]),
            fill_svg(smooth_path([(104, 0), (126, 0), (126, 70), (104, 70), (110, 35)], tension=0.5), CREST["shadow"])]
    body_shape(p, smooth_path(stub, tension=0.5, jitter=0.5, seed=82), zone, det, hl=(0.46, 0.2, 0.2, 0.12))
    # the front lip of churned dirt, with clods and pebbles
    lip = [(4, 72), (8, 60), (22, 54), (40, 60), (58, 56), (76, 61), (96, 55), (114, 60), (132, 54), (140, 62),
           (138, 74), (110, 77), (70, 78), (30, 77)]
    det = [stroke_svg(d, DIRT["shadow"], 2.5) for d in ("M20 66 Q30 62 40 66", "M70 68 Q82 64 94 68",
                                                          "M110 66 Q120 62 130 67")]
    p.shape(smooth_path(lip, tension=0.6, jitter=0.8, seed=83), DIRT["fill"], DIRT["line"], DIRT["light"],
            DIRT["shadow"], (4, 4), (5, 5), soft=2, details=det, hl=(0.4, 0.22, 0.3, 0.14))
    for i, (x, y, r) in enumerate(((16, 54, 6), (50, 52, 5), (88, 51, 6), (126, 52, 5), (66, 70, 4))):
        p.shape(smooth_path(ellipse_pts(x, y, r * 1.2, r, 7, wobble=0.25, seed=i), tension=0.4), CLOD["fill"],
                CLOD["line"], CLOD["light"], CLOD["shadow"], (1.5, 1.5), (2, 2), soft=0.8, grow=1.0, inner=1.4,
                bias=(0.8, 1))
    note("tail_base", w, h, (72, 72), "c_sl_hizza_tail: the tail coming out of churned dirt; pivot on the ground "
         "line under the tail's middle; tail_b1 stacks on the stub's top (about (72, 8))")
    return p


def collar():
    """The compass piece on a collar round the middle of the body (ch2 plan): a leather band,
    seen wrapping a body segment, with a brass mount holding the piece."""
    w, h = 128, 80
    p = Part("collar", w, h)
    band = [(6, 30), (40, 38), (88, 38), (122, 28), (124, 44), (88, 56), (40, 56), (4, 46)]
    det = [stroke_svg("M10 40 Q64 50 118 38", COLLAR["shadow"], 2.5)]
    for x in (16, 30, 98, 112):   # stitching studs
        det.append(fill_svg(smooth_path(ellipse_pts(x, 44 + (2 if 20 < x < 100 else 0), 2.4, 2.4, 8)), BRASS["fill"]))
    p.shape(smooth_path(band, tension=0.6, jitter=0.4, seed=61), COLLAR["fill"], COLLAR["line"], COLLAR["light"],
            COLLAR["shadow"], (3, 3), (4, 4), details=det, soft=2, hl=(0.4, 0.3, 0.3, 0.16))
    # the brass mount
    mount = ellipse_pts(64, 44, 26, 26, 16, wobble=0.02, seed=3)
    p.shape(smooth_path(mount, jitter=0.3, seed=62), BRASS["fill"], BRASS["line"], BRASS["light"], BRASS["shadow"],
            (3, 3), (4, 4), soft=1.5, hl=(0.38, 0.26, 0.24, 0.16))
    # the compass piece: a curved quarter of the compass's ring, a jewel set in it
    import math
    outer, inner = 19, 9
    arc = [(64 + outer * math.cos(math.radians(a)), 47 + outer * math.sin(math.radians(a))) for a in range(200, 341, 20)]
    arc += [(64 + inner * math.cos(math.radians(a)), 47 + inner * math.sin(math.radians(a))) for a in range(340, 199, -20)]
    gem = [fill_svg(poly_path([(64, 30), (70, 36), (64, 42), (58, 36)]), PIECE["fill"]),
           fill_svg(poly_path([(64, 30), (58, 36), (64, 36)]), PIECE["light"]),
           stroke_svg(poly_path([(64, 30), (70, 36), (64, 42), (58, 36)]), PIECE["line"], 1.8)]
    p.shape(poly_path(arc), "#d9a83a", BRASS["line"], BRASS["light"], BRASS["shadow"], (2, 2), (3, 3),
            details=gem + [stroke_svg("M50 52 L78 52", BRASS["shadow"], 2)], grow=1.2, inner=1.6, bias=(1, 1),
            soft=1)
    note("collar", w, h, (64, 46), "the collar with the compass piece, round neck_a1's middle (in front of it); "
         "the band's ends tuck behind the segment's edges")
    return p


# ------------------------------------------------------------- effects --

def dizzy_stars():
    p = Part("dizzy_stars", 104, 48, grow=1.0, inner=1.5, bias=(1, 1))
    for i, (x, y, r) in enumerate(((18, 30, 14), (52, 18, 15), (86, 30, 14))):
        p.shape(smooth_path(star_pts(x, y, r, r * 0.48, rot=-90 + i * 12), tension=0.15, seed=i),
                STAR["fill"], STAR["line"], STAR["light"], STAR["shadow"], (3, 3), (3, 3), soft=1.2)
    note("dizzy_stars", 104, 48, (52, 44), "three stars to circle above a dazed head")
    return p


def smoke_puff():
    p = Part("smoke_puff", 96, 80)
    pts = []
    import math
    blobs = [(30, 50, 22), (52, 32, 24), (72, 50, 20), (50, 58, 20)]
    for i in range(48):
        a = 2 * math.pi * i / 48
        best = 0
        for bx, by, br in blobs:
            # farthest point of the union along this ray from the centre (48,46)
            dx, dy = math.cos(a), math.sin(a)
            ox, oy = bx - 48, by - 46
            bproj = ox * dx + oy * dy
            disc = bproj * bproj - (ox * ox + oy * oy - br * br)
            if disc > 0:
                best = max(best, bproj + math.sqrt(disc))
        pts.append((48 + best * math.cos(a), 46 + best * math.sin(a)))
    det = [stroke_svg("M34 46 Q42 40 50 44", SMOKE["shadow"], 3), stroke_svg("M56 54 Q64 48 70 52", SMOKE["shadow"], 3)]
    p.shape(smooth_path(pts, tension=0.6, jitter=0.4, seed=3), SMOKE["fill"], SMOKE["line"], SMOKE["light"],
            SMOKE["shadow"], (5, 5), (7, 7), details=det, soft=3,
            hl=(0.45, 0.26, 0.22, 0.14))
    note("smoke_puff", 96, 80, (48, 46), "defeat smoke; scale up and fade")
    return p


def build():
    cards = []
    for v in ("a", "b"):
        cards += [head(f"head_{v}", v, "open"), head(f"head_{v}_blink", v, "blink"),
                  head(f"head_{v}_hurt", v, "hurt"), jaw(f"jaw_{v}", v), tongue(f"tongue_{v}", v),
                  mouth(f"mouth_{v}", v)]
    cards += [
        segment("neck_a1", 120, 136, 46, [(0.74, 0.30, 22, 18), (0.80, 0.70, 18, 16)], desc="front neck, lowest"),
        segment("neck_a2", 112, 128, 42, [(0.74, 0.32, 20, 16), (0.80, 0.70, 16, 14)], desc="front neck, middle"),
        segment("neck_a3", 104, 120, 38, [(0.74, 0.34, 18, 16), (0.80, 0.70, 14, 12)], desc="front neck, top"),
        segment("tail_b1", 104, 128, 38, [(0.70, 0.30, 18, 16), (0.76, 0.70, 14, 14)], "b", "tail, lowest"),
        segment("tail_b2", 96, 120, 34, [(0.70, 0.32, 16, 14), (0.76, 0.70, 12, 12)], "b", "tail, middle"),
        segment("tail_b3", 88, 112, 30, [(0.70, 0.34, 14, 12), (0.76, 0.70, 12, 10)], "b", "tail, top"),
        coil("coil_back", 288, 144, False), coil("coil_front", 304, 128, True),
        collar(), tail_base(), dizzy_stars(), smoke_puff(),
    ]
    imgs = {}
    for c in cards:
        imgs[c.name] = c.save(OUT)
    with open(os.path.join(OUT, "parts.json"), "w", encoding="utf-8") as f:
        json.dump({"model": "c_sl_hizza", "texels_per_unit": 8, "faces": "-x",
                   "pivot_note": "pixels from the top-left of each PNG", "cards": parts_meta}, f, indent=1)
    return imgs


if __name__ == "__main__":
    build()
    print("wrote", len(parts_meta), "cards to", os.path.abspath(OUT))
